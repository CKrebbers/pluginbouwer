// pb-render — een kleine CLAP-host: stuurt een WAV door een gebouwd .clap en schrijft de uitvoer als WAV.
// Dit is de native kant van de null-test die niet via pluginval loopt (pluginval kent geen CLAP).
//
//   pb-render <plugin.clap> <in.wav> <uit.wav> [--sr 48000] [--blok 512 | --blok 3,128,1,4096] [--p id=waarde ...]
//             [--state-rondje]    (state opslaan, nieuwe instantie, state laden, dan pas renderen)
//             [--automatie f]     parameter-events op hun sample tijdens het renderen; f: per regel "sample id waarde"
//                                 (ook midden in een blok: zo bewijst de null-test het splitsen bij events op de bits)
//             [--noten f [--midi]] noten op hun sample (instrument); f: per regel "sample aan|uit noot". Standaard als
//                                 CLAP-noten, met --midi als MIDI-bytes (0x90/0x80): beide dialecten van de note port
//             [--tempo bpm]       een transport dat speelt vanaf tel 0 op dit tempo (de sequencer volgt de host)
//             [--indeling stereo|mono|mono-stereo]  kies vóór activate de audio-ports-config met 2 → 2, 1 → 1 of
//                                 1 → 2 kanalen (zonder: de poorten zoals de plugin ze geeft). Ingangskanaal c leest
//                                 WAV-kanaal c (of het laatste); de uitvoer-WAV heeft de kanalen van de uitgang.
//             [--in-place]        uitgangskanaal c is dezelfde buffer als ingangskanaal c (zoals Logic het doet)
//
// Met -fsanitize=realtime (clang ≥ 20) loopt elke process-aanroep in een real-time context: een malloc, lock of
// syscall in de plugin breekt dan het programma af met een stacktrace. Zie .github/workflows/ci.yml.
#include <clap/clap.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

union Ev { clap_event_header_t h; clap_event_param_value_t p; clap_event_note_t n; clap_event_midi_t m; };
struct Events {
  std::vector<Ev> ev;
};
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return &static_cast<Events*>(l->ctx)->ev[i].h; }
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
  // automatisering en noten in één lijst, op sample (een noot: id = -1 aan, -2 uit, waarde = noot)
  struct Auto { uint64_t sample; int64_t id; double waarde; };
  std::vector<Auto> automatie;
  bool alsMidi = false; double tempo = 0;
  std::string indeling; bool inPlace = false;
  std::vector<std::pair<clap_id, double>> params;
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
    else if (a == "--automatie" && i + 1 < argc) {
      std::ifstream f(argv[++i]); if (!f) fout("--automatie: bestand niet te lezen");
      Auto x; while (f >> x.sample >> x.id >> x.waarde) automatie.push_back(x);
    }
    else if (a == "--noten" && i + 1 < argc) {
      std::ifstream f(argv[++i]); if (!f) fout("--noten: bestand niet te lezen");
      uint64_t smp; std::string wat; int noot;
      while (f >> smp >> wat >> noot) automatie.push_back({smp, wat == "aan" ? -1 : -2, (double)noot});
    }
    else if (a == "--midi") alsMidi = true;
    else if (a == "--tempo" && i + 1 < argc) tempo = std::atof(argv[++i]);
    else if (a == "--indeling" && i + 1 < argc) indeling = argv[++i];
    else if (a == "--in-place") inPlace = true;
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
    Ev e{};
    e.p.header = {sizeof(e.p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    e.p.param_id = pid; e.p.note_id = -1; e.p.port_index = -1; e.p.channel = -1; e.p.key = -1; e.p.value = v;
    ev.ev.push_back(e);
  }
  clap_input_events_t inEv = {&ev, evSize, evGet};
  clap_output_events_t uitEv = {nullptr, evPush};
  if (pp && !ev.ev.empty()) pp->flush(pl, &inEv, &uitEv);

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

  // op sample sorteren (stabiel: binnen één sample de volgorde van de bestanden)
  for (size_t a = 1; a < automatie.size(); ++a)
    for (size_t b = a; b > 0 && automatie[b - 1].sample > automatie[b].sample; --b) std::swap(automatie[b - 1], automatie[b]);
  // een instrument heeft geen audio-ingang: dan geven we er ook geen
  auto* ap = static_cast<const clap_plugin_audio_ports_t*>(pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS));
  if (!indeling.empty()) {
    uint32_t wilIn = 2, wilUit = 2;
    if (indeling == "mono") wilIn = wilUit = 1;
    else if (indeling == "mono-stereo") wilIn = 1;
    else if (indeling != "stereo") fout("--indeling stereo|mono|mono-stereo");
    auto* cfg = static_cast<const clap_plugin_audio_ports_config_t*>(pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS_CONFIG));
    if (!cfg) fout("--indeling: de plugin heeft geen audio-ports-config");
    bool gekozen = false;
    for (uint32_t i = 0; i < cfg->count(pl) && !gekozen; ++i) {
      clap_audio_ports_config_t c{};
      if (!cfg->get(pl, i, &c) || !c.has_main_input || !c.has_main_output) continue;
      if (c.main_input_channel_count == wilIn && c.main_output_channel_count == wilUit) {
        if (!cfg->select(pl, c.id)) fout("--indeling: select gaf false");
        gekozen = true;
      }
    }
    if (!gekozen) fout("--indeling: geen audio-ports-config met " + std::to_string(wilIn) + " → " + std::to_string(wilUit));
  }
  const uint32_t nIn = ap ? ap->count(pl, true) : 1;
  uint32_t kanIn = 2, kanUit = 2;
  if (ap) {
    clap_audio_port_info_t pi{};
    if (nIn && ap->get(pl, 0, true, &pi)) kanIn = pi.channel_count;
    if (ap->get(pl, 0, false, &pi)) kanUit = pi.channel_count;
  }
  if (!nIn) kanIn = 0;
  if (!indeling.empty()) std::fprintf(stderr, "pb-render: indeling %s: %u → %u kanalen%s\n", indeling.c_str(), kanIn, kanUit,
                                      inPlace ? ", in-place" : "");
  if (!pl->activate(pl, sr, 1, maxBlok)) fout("activate mislukt");
  if (!pl->start_processing(pl)) fout("start_processing mislukt");

  const uint32_t wavKan = (uint32_t)in.k.size();
  const size_t n = in.k[0].size();
  Wav uit; uit.sr = (uint32_t)sr; uit.k.assign(kanUit, std::vector<float>(n));
  std::vector<std::vector<float>> bi(kanIn, std::vector<float>(maxBlok)), bu(kanUit, std::vector<float>(maxBlok));
  std::vector<float*> pi(kanIn ? kanIn : 1, nullptr), pu(kanUit);
  for (uint32_t c = 0; c < kanIn; ++c) pi[c] = bi[c].data();
  for (uint32_t c = 0; c < kanUit; ++c) pu[c] = (inPlace && c < kanIn) ? bi[c].data() : bu[c].data();
  Events blokEv; blokEv.ev.reserve(automatie.size());
  clap_input_events_t blokIn = {&blokEv, evSize, evGet};
  size_t volgende = 0;

  size_t pos = 0, bnr = 0; int64_t steady = 0;
  while (pos < n) {
    uint32_t m = blokken[bnr++ % blokken.size()];
    if (pos + m > n) m = (uint32_t)(n - pos);
    for (uint32_t c = 0; c < kanIn; ++c) std::memcpy(pi[c], &in.k[c < wavKan ? c : wavKan - 1][pos], m * 4);
    clap_audio_buffer_t ai = {pi.data(), nullptr, kanIn, 0, 0}, au = {pu.data(), nullptr, kanUit, 0, 0};
    // de events van dit blok, met hun tijd binnen het blok (buiten doeProcess opgebouwd: geen allocatie in RT)
    blokEv.ev.clear();
    while (volgende < automatie.size() && automatie[volgende].sample < pos + m) {
      const auto& x = automatie[volgende++];
      const uint32_t t = (uint32_t)(x.sample < pos ? 0 : x.sample - pos);
      Ev e{};
      if (x.id >= 0) {
        e.p.header = {sizeof(e.p), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.p.param_id = (clap_id)x.id; e.p.note_id = -1; e.p.port_index = -1; e.p.channel = -1; e.p.key = -1; e.p.value = x.waarde;
      } else if (alsMidi) {
        e.m.header = {sizeof(e.m), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        e.m.port_index = 0;
        e.m.data[0] = x.id == -1 ? 0x90 : 0x80; e.m.data[1] = (uint8_t)x.waarde; e.m.data[2] = x.id == -1 ? 100 : 0;
      } else {
        e.n.header = {sizeof(e.n), t, CLAP_CORE_EVENT_SPACE_ID, (uint16_t)(x.id == -1 ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
        e.n.note_id = -1; e.n.port_index = 0; e.n.channel = 0; e.n.key = (int16_t)x.waarde; e.n.velocity = x.id == -1 ? 0.8 : 0.0;
      }
      blokEv.ev.push_back(e);
    }
    clap_event_transport_t tr{};
    if (tempo > 0) {
      const double tel = (double)steady / sr * tempo / 60.0;
      tr.header = {sizeof(tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
      tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
      tr.tempo = tempo;
      tr.song_pos_beats = (clap_beattime)(tel * (double)CLAP_BEATTIME_FACTOR + 0.5);
      tr.tsig_num = 4; tr.tsig_denom = 4;
    }
    clap_process_t pr = {steady, m, tempo > 0 ? &tr : nullptr, &ai, &au, nIn ? 1u : 0u, 1, &blokIn, &uitEv};
    if (doeProcess(pl, &pr) == CLAP_PROCESS_ERROR) fout("process gaf een fout");
    for (uint32_t c = 0; c < kanUit; ++c) std::memcpy(&uit.k[c][pos], pu[c], m * 4);
    pos += m; steady += m;
  }
  pl->stop_processing(pl);
  pl->deactivate(pl);
  pl->destroy(pl);
  entry->deinit();
  schrijfWav(argv[3], uit);
  return 0;
}
