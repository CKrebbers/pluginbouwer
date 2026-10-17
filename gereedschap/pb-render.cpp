// pb-render — een kleine CLAP-host: stuurt een WAV door een gebouwd .clap en schrijft de uitvoer als WAV.
// Dit is de native kant van de null-test die niet via pluginval loopt (pluginval kent geen CLAP).
//
//   pb-render <plugin.clap> <in.wav> <uit.wav> [--sr 48000] [--blok 512 | --blok 3,128,1,4096] [--p id=waarde ...]
//             [--state-rondje]    (state opslaan, nieuwe instantie, state laden, dan pas renderen)
//             [--automatie K]     (zoals een DAW met automatisering: de --p-waarden niet via flush vóór activate, maar
//                                  als events in process, op sample 0 en daarna elke K samples opnieuw, op hun sample)
//             [--glij id=van:naar] (met --automatie: deze parameter loopt in stappen van K samples van `van` naar
//                                  `naar` over de hele render; toetst dat de uitvoer niet van de blokgrootte afhangt)
//
// Met -fsanitize=realtime (clang ≥ 20) loopt elke process-aanroep in een real-time context: een malloc, lock of
// syscall in de plugin breekt dan het programma af met een stacktrace. Zie .github/workflows/ci.yml.
#include <clap/clap.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define PB_RT [[clang::nonblocking]]
#endif
#endif
#ifndef PB_RT
#define PB_RT
#endif

namespace {

[[noreturn]] void fout(const std::string& t) { std::fprintf(stderr, "pb-render: %s\n", t.c_str()); std::exit(2); }

struct Wav { uint32_t sr = 48000; std::vector<std::vector<float>> k; };

Wav leesWav(const char* pad) {
  FILE* f = std::fopen(pad, "rb");
  if (!f) fout(std::string("kan niet lezen: ") + pad);
  std::vector<unsigned char> b;
  unsigned char buf[65536]; size_t r;
  while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + r);
  std::fclose(f);
  auto u16 = [&](size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) fout("geen WAV");
  size_t p = 12; uint32_t tag = 0, kan = 0, bits = 0, sr = 0; size_t dOff = 0, dLen = 0;
  while (p + 8 <= b.size()) {
    uint32_t len = u32(p + 4);
    if (!std::memcmp(&b[p], "fmt ", 4)) { tag = u16(p + 8); kan = u16(p + 10); sr = u32(p + 12); bits = u16(p + 22); }
    if (!std::memcmp(&b[p], "data", 4)) { dOff = p + 8; dLen = len; }
    p += 8 + len + (len & 1);
  }
  if (tag == 0xfffe) tag = 3;
  if (tag != 3 || bits != 32 || !kan || !dOff) fout("alleen 32-bit float WAV");
  Wav w; w.sr = sr; w.k.assign(kan, {});
  size_t n = dLen / (4 * kan);
  for (auto& c : w.k) c.resize(n);
  for (size_t i = 0; i < n; ++i)
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(&w.k[c][i], &b[dOff + (i * kan + c) * 4], 4);
  return w;
}

void schrijfWav(const char* pad, const Wav& w) {
  FILE* f = std::fopen(pad, "wb");
  if (!f) fout(std::string("kan niet schrijven: ") + pad);
  uint32_t kan = (uint32_t)w.k.size(), n = (uint32_t)w.k[0].size(), data = n * kan * 4;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); put32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(16); put16(3); put16((uint16_t)kan); put32(w.sr); put32(w.sr * kan * 4); put16((uint16_t)(kan * 4)); put16(32);
  std::fwrite("data", 1, 4, f); put32(data);
  for (uint32_t i = 0; i < n; ++i) for (uint32_t c = 0; c < kan; ++c) std::fwrite(&w.k[c][i], 4, 1, f);
  std::fclose(f);
}

// --- host --------------------------------------------------------------------------------------------------
const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-render", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

struct Events {
  std::vector<clap_event_param_value_t> ev;
};
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return &static_cast<Events*>(l->ctx)->ev[i].header; }
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

struct Buf { std::vector<unsigned char> d; size_t pos = 0; };
int64_t ostWrite(const clap_ostream_t* s, const void* p, uint64_t n) {
  auto* b = static_cast<Buf*>(s->ctx); auto* c = static_cast<const unsigned char*>(p); b->d.insert(b->d.end(), c, c + n); return (int64_t)n;
}
int64_t istRead(const clap_istream_t* s, void* p, uint64_t n) {
  auto* b = static_cast<Buf*>(s->ctx); uint64_t m = b->d.size() - b->pos; if (n < m) m = n;
  std::memcpy(p, b->d.data() + b->pos, m); b->pos += m; return (int64_t)m;
}

// noinline: anders vouwt -O2 hem in main() vóór RTSan instrumenteert, en dan is er geen real-time context
// (gezien in CI: de kanarie werd niet gevangen).
__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT { return pl->process(pl, pr); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) fout("gebruik: pb-render <plugin.clap> <in.wav> <uit.wav> [--sr N] [--blok N[,N…]] [--p id=waarde] [--state-rondje]");
  std::string pad = argv[1];
  double sr = 0; std::vector<uint32_t> blokken = {512}; bool rondje = false;
  std::vector<std::pair<clap_id, double>> params;
  uint32_t automatie = 0; clap_id glijId = 0; double glijVan = 0, glijNaar = 0;
  for (int i = 4; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--sr" && i + 1 < argc) sr = std::atof(argv[++i]);
    else if (a == "--blok" && i + 1 < argc) {
      blokken.clear(); std::string s = argv[++i]; size_t q = 0;
      while (q <= s.size()) { size_t k = s.find(',', q); if (k == std::string::npos) k = s.size(); blokken.push_back((uint32_t)std::atoi(s.substr(q, k - q).c_str())); q = k + 1; }
    } else if (a == "--p" && i + 1 < argc) {
      std::string s = argv[++i]; size_t k = s.find('=');
      if (k == std::string::npos) fout("--p id=waarde");
      params.push_back({(clap_id)std::atoi(s.substr(0, k).c_str()), std::atof(s.substr(k + 1).c_str())});
    } else if (a == "--state-rondje") rondje = true;
    else if (a == "--automatie" && i + 1 < argc) { automatie = (uint32_t)std::atoi(argv[++i]); if (!automatie) fout("--automatie K, K > 0"); }
    else if (a == "--glij" && i + 1 < argc) {
      std::string s = argv[++i]; size_t k = s.find('='), d = s.find(':');
      if (k == std::string::npos || d == std::string::npos || d < k) fout("--glij id=van:naar");
      glijId = (clap_id)std::atoi(s.substr(0, k).c_str());
      glijVan = std::atof(s.substr(k + 1, d - k - 1).c_str()); glijNaar = std::atof(s.substr(d + 1).c_str());
    }
    else fout("onbekende optie " + a);
  }

  std::string lib = pad;
#ifdef __APPLE__
  {  // een .clap is op macOS een bundle: de code staat in Contents/MacOS/<naam zonder .clap>
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    lib = pad + "/Contents/MacOS/" + naam;
  }
#endif
  void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) fout(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
  if (!entry || !entry->init(pad.c_str())) fout("geen clap_entry");
  auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fab || fab->get_plugin_count(fab) < 1) fout("geen plugin in de factory");
  const char* id = fab->get_plugin_descriptor(fab, 0)->id;

  Wav in = leesWav(argv[2]);
  if (sr <= 0) sr = in.sr;
  uint32_t maxBlok = 1; for (auto b : blokken) if (b > maxBlok) maxBlok = b;

  auto maak = [&]() {
    const clap_plugin_t* pl = fab->create_plugin(fab, &HOST, id);
    if (!pl || !pl->init(pl)) fout("plugin start niet");
    return pl;
  };
  const clap_plugin_t* pl = maak();
  auto* pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));

  // parameters vóór activate, via flush (zoals een host die een project opent)
  Events ev;
  for (auto& [pid, v] : params) {
    clap_event_param_value_t e{};
    e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    e.param_id = pid; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
    ev.ev.push_back(e);
  }
  clap_input_events_t inEv = {&ev, evSize, evGet};
  clap_output_events_t uitEv = {nullptr, evPush};
  if (pp && !ev.ev.empty() && !automatie) pp->flush(pl, &inEv, &uitEv);

  if (rondje) {
    auto* st = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    if (!st) fout("geen state-extensie");
    Buf b; clap_ostream_t os = {&b, ostWrite};
    if (!st->save(pl, &os)) fout("state save mislukt");
    pl->destroy(pl);
    pl = maak();
    st = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    clap_istream_t is = {&b, istRead};
    if (!st->load(pl, &is)) fout("state load mislukt");
  }

  if (!pl->activate(pl, sr, 1, maxBlok)) fout("activate mislukt");
  if (!pl->start_processing(pl)) fout("start_processing mislukt");

  const uint32_t kan = (uint32_t)in.k.size() > 2 ? 2 : (uint32_t)in.k.size();
  const size_t n = in.k[0].size();
  Wav uit; uit.sr = (uint32_t)sr; uit.k.assign(kan, std::vector<float>(n));
  std::vector<std::vector<float>> bi(kan, std::vector<float>(maxBlok)), bu(kan, std::vector<float>(maxBlok));
  std::vector<float*> pi(kan), pu(kan);
  for (uint32_t c = 0; c < kan; ++c) { pi[c] = bi[c].data(); pu[c] = bu[c].data(); }
  if (glijId && !automatie) fout("--glij kan alleen met --automatie");
  // automatisering: alle events vooraf, op hun absolute sample (geen allocatie in de lus van process)
  std::vector<clap_event_param_value_t> auto_;
  if (automatie) {
    for (size_t t = 0; t < n; t += automatie)
      for (auto& [pid, v] : params) {
        clap_event_param_value_t e{};
        e.header = {sizeof(e), (uint32_t)t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = pid; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        e.value = (pid == glijId && n > 1) ? glijVan + (glijNaar - glijVan) * ((double)t / (double)(n - 1)) : v;
        auto_.push_back(e);
      }
  }
  Events blokEv; blokEv.ev.reserve(auto_.size());
  clap_input_events_t blokIn = {&blokEv, evSize, evGet};
  size_t volgende = 0;

  size_t pos = 0, bnr = 0; int64_t steady = 0;
  while (pos < n) {
    uint32_t m = blokken[bnr++ % blokken.size()];
    if (pos + m > n) m = (uint32_t)(n - pos);
    blokEv.ev.clear();
    for (; volgende < auto_.size() && auto_[volgende].header.time < pos + m; ++volgende) {
      blokEv.ev.push_back(auto_[volgende]);
      blokEv.ev.back().header.time -= (uint32_t)pos;  // tijd binnen dit blok
    }
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(pi[c], &in.k[c][pos], m * 4);
    clap_audio_buffer_t ai = {pi.data(), nullptr, kan, 0, 0}, au = {pu.data(), nullptr, kan, 0, 0};
    clap_process_t pr = {steady, m, nullptr, &ai, &au, 1, 1, &blokIn, &uitEv};
    if (doeProcess(pl, &pr) == CLAP_PROCESS_ERROR) fout("process gaf een fout");
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(&uit.k[c][pos], pu[c], m * 4);
    pos += m; steady += m;
  }
  pl->stop_processing(pl);
  pl->deactivate(pl);
  pl->destroy(pl);
  entry->deinit();
  schrijfWav(argv[3], uit);
  return 0;
}
