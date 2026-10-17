// pb-state-toets — is de state vooruit-compatibel? Een kleine CLAP-host die twee bouwen van de plugin naast elkaar
// laadt: een "oude" die parameter-id N niet kent (plugin gebouwd met -DPB_ZONDER_PARAM=N) en de volledige.
//
//   pb-state-toets <oud.clap> <nieuw.clap> <N> [<in.wav>]
//
// Wat hij toetst (exitcode 1 bij de eerste fout, met uitleg):
//  1. oud → nieuw: state van de oude bouw (alle parameters ≠ standaard) laadt in de nieuwe; elk bekend id krijgt zijn
//     waarde bit voor bit, id N houdt zijn standaard, en de host krijgt een rescan van de waarden.
//  2. De nieuwe bouw met die state klinkt bit voor bit als de oude bouw met dezelfde instellingen, en als de nieuwe
//     bouw met dezelfde waarden via params.flush (het Ontwerp krijgt dus echt de geladen waarden).
//  3. State laden terwijl de plugin actief is (tussen twee process-blokken) geeft dezelfde uitvoer als dezelfde
//     waarden via flush op dat moment.
//  4. nieuw → oud: het onbekende id N wordt overgeslagen, de rest laadt.
//  5. Een state van een nieuwere versie (versie 2, een extra onbekend id 999) laadt; kapotte state (leeg, andere
//     kop, te kort) geeft false en verandert niets.
#include <clap/clap.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

int g_fouten = 0, g_gemeld = 0;
void fout(const std::string& t) { std::fprintf(stderr, "FOUT: %s\n", t.c_str()); ++g_fouten; }
// "ok" alleen als er sinds de vorige melding geen fout bij kwam
template <class... A> void ok(const char* f, A... a) {
  if (g_fouten == g_gemeld) { std::printf("  ok  "); std::printf(f, a...); std::printf("\n"); }
  g_gemeld = g_fouten;
}
[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-state-toets: %s\n", t.c_str()); std::exit(2); }

// --- host met params.rescan (we tellen de rescans) -----------------------------------------------------------------
int g_rescans = 0;
void hpRescan(const clap_host_t*, clap_param_rescan_flags f) { if (f & CLAP_PARAM_RESCAN_VALUES) ++g_rescans; }
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpFlush(const clap_host_t*) {}
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-state-toets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

struct Events { std::vector<clap_event_param_value_t> ev; };
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t) static_cast<Events*>(l->ctx)->ev.size(); }
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

__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT { return pl->process(pl, pr); }

// --- een geladen .clap ---------------------------------------------------------------------------------------------
struct Bib {
  const clap_plugin_factory_t* fab = nullptr;
  const char* id = nullptr;
  std::string naam;
};
Bib laad(const std::string& pad) {
  std::string lib = pad;
  struct stat st {};
  if (stat(pad.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {  // macOS-bundle: Contents/MacOS/<naam zonder .clap>
    std::string n = pad.substr(pad.find_last_of('/') + 1);
    if (n.size() > 5 && n.substr(n.size() - 5) == ".clap") n.resize(n.size() - 5);
    lib = pad + "/Contents/MacOS/" + n;
  }
  void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) stop(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
  if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry in " + pad);
  Bib b;
  b.fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!b.fab || b.fab->get_plugin_count(b.fab) < 1) stop("geen plugin in " + pad);
  b.id = b.fab->get_plugin_descriptor(b.fab, 0)->id;
  b.naam = pad;
  return b;
}

struct Inst {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_state_t* st = nullptr;
  explicit Inst(const Bib& b) {
    pl = b.fab->create_plugin(b.fab, &HOST, b.id);
    if (!pl || !pl->init(pl)) stop("plugin start niet: " + b.naam);
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    st = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    if (!pp || !st) stop("geen params- of state-extensie: " + b.naam);
  }
  ~Inst() { pl->destroy(pl); }
  Inst(const Inst&) = delete;
  Inst& operator=(const Inst&) = delete;

  struct Info { double min, max, standaard; };
  std::map<clap_id, Info> params() const {
    std::map<clap_id, Info> m;
    for (uint32_t k = 0, n = pp->count(pl); k < n; ++k) {
      clap_param_info_t i{};
      if (!pp->get_info(pl, k, &i)) stop("get_info faalt");
      m[i.id] = {i.min_value, i.max_value, i.default_value};
    }
    return m;
  }
  double waarde(clap_id id) const {
    double v = NAN;
    if (!pp->get_value(pl, id, &v)) return NAN;
    return v;
  }
  void flush(const std::map<clap_id, double>& w) const {
    Events ev;
    for (auto& [id, v] : w) {
      clap_event_param_value_t e{};
      e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
      e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
      ev.ev.push_back(e);
    }
    clap_input_events_t in = {&ev, evSize, evGet};
    clap_output_events_t uit = {nullptr, evPush};
    pp->flush(pl, &in, &uit);
  }
  Buf bewaar() const {
    Buf b; clap_ostream_t os = {&b, ostWrite};
    if (!st->save(pl, &os)) stop("state save mislukt");
    return b;
  }
  bool laadState(Buf b) const { b.pos = 0; clap_istream_t is = {&b, istRead}; return st->load(pl, &is); }
};

// --- renderen --------------------------------------------------------------------------------------------------
struct Wav { uint32_t sr = 48000; std::vector<std::vector<float>> k; };

Wav leesWav(const char* pad) {
  FILE* f = std::fopen(pad, "rb");
  if (!f) stop(std::string("kan niet lezen: ") + pad);
  std::vector<unsigned char> b;
  unsigned char buf[65536]; size_t r;
  while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + r);
  std::fclose(f);
  auto u16 = [&](size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) stop("geen WAV");
  size_t p = 12; uint32_t tag = 0, kan = 0, bits = 0, sr = 0; size_t dOff = 0, dLen = 0;
  while (p + 8 <= b.size()) {
    uint32_t len = u32(p + 4);
    if (!std::memcmp(&b[p], "fmt ", 4)) { tag = u16(p + 8); kan = u16(p + 10); sr = u32(p + 12); bits = u16(p + 22); }
    if (!std::memcmp(&b[p], "data", 4)) { dOff = p + 8; dLen = len; }
    p += 8 + len + (len & 1);
  }
  if (tag == 0xfffe) tag = 3;
  if (tag != 3 || bits != 32 || !kan || !dOff) stop("alleen 32-bit float WAV");
  Wav w; w.sr = sr; w.k.assign(kan > 2 ? 2 : kan, {});
  size_t n = dLen / (4 * kan);
  for (auto& c : w.k) c.resize(n);
  for (size_t i = 0; i < n; ++i)
    for (uint32_t c = 0; c < w.k.size(); ++c) std::memcpy(&w.k[c][i], &b[dOff + (i * kan + c) * 4], 4);
  return w;
}

Wav ruis(size_t n) {  // als er geen WAV is: een paar tikken en ruis, deterministisch
  Wav w; w.k.assign(2, std::vector<float>(n, 0.f));
  uint32_t x = 0x9e3779b9u;
  for (size_t i = 0; i < n; ++i) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    float r = (float)((int32_t)x) / 2147483648.f;
    float a = (i % 12000 < 2400) ? 0.25f : 0.f;
    w.k[0][i] = a * r; w.k[1][i] = a * r * 0.5f;
  }
  return w;
}

// render `in` door `inst` (blok 512). `tussendoor(blokNr)` mag tussen twee blokken iets doen (state laden, flush).
template <class F>
std::vector<std::vector<float>> render(const Inst& inst, const Wav& in, F tussendoor) {
  const uint32_t blok = 512, kan = (uint32_t)in.k.size();
  if (!inst.pl->activate(inst.pl, in.sr, 1, blok)) stop("activate mislukt");
  if (!inst.pl->start_processing(inst.pl)) stop("start_processing mislukt");
  const size_t n = in.k[0].size();
  std::vector<std::vector<float>> uit(kan, std::vector<float>(n));
  std::vector<std::vector<float>> bi(kan, std::vector<float>(blok)), bu(kan, std::vector<float>(blok));
  std::vector<float*> pi(kan), pu(kan);
  for (uint32_t c = 0; c < kan; ++c) { pi[c] = bi[c].data(); pu[c] = bu[c].data(); }
  Events leeg;
  clap_input_events_t geenEv = {&leeg, evSize, evGet};
  clap_output_events_t uitEv = {nullptr, evPush};
  size_t pos = 0; int nr = 0;
  while (pos < n) {
    tussendoor(nr++);
    uint32_t m = blok; if (pos + m > n) m = (uint32_t)(n - pos);
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(pi[c], &in.k[c][pos], m * 4);
    clap_audio_buffer_t ai = {pi.data(), nullptr, kan, 0, 0}, au = {pu.data(), nullptr, kan, 0, 0};
    clap_process_t pr = {(int64_t)pos, m, nullptr, &ai, &au, 1, 1, &geenEv, &uitEv};
    if (doeProcess(inst.pl, &pr) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(&uit[c][pos], pu[c], m * 4);
    pos += m;
  }
  inst.pl->stop_processing(inst.pl);
  inst.pl->deactivate(inst.pl);
  return uit;
}
std::vector<std::vector<float>> render(const Inst& i, const Wav& in) { return render(i, in, [](int) {}); }

void vergelijkAudio(const char* wat, const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b) {
  double piek = 0, verschil = 0;
  bool gelijk = a.size() == b.size();
  for (size_t c = 0; gelijk && c < a.size(); ++c) {
    gelijk = a[c].size() == b[c].size() && std::memcmp(a[c].data(), b[c].data(), a[c].size() * 4) == 0;
    for (size_t i = 0; i < a[c].size() && i < b[c].size(); ++i) {
      piek = std::fmax(piek, std::fabs(a[c][i]));
      verschil = std::fmax(verschil, std::fabs((double)a[c][i] - b[c][i]));
    }
  }
  if (!gelijk) fout(std::string(wat) + ": verschilt, max |verschil| " + std::to_string(verschil));
  if (piek == 0) fout(std::string(wat) + ": stilte; de toets zegt dan niets");
  ok("%s: bit voor bit gelijk (piek %.3f)", wat, piek);
}

// waarden die niet de standaard zijn: per parameter een eigen plek in het bereik
std::map<clap_id, double> anders(const std::map<clap_id, Inst::Info>& ps, double f) {
  std::map<clap_id, double> w;
  int k = 0;
  for (auto& [id, i] : ps) {
    double g = f + 0.07 * k++;
    g -= std::floor(g);
    double v = i.min + (i.max - i.min) * g;
    if (v == i.standaard) v = i.min + (i.max - i.min) * (1 - g);
    w[id] = v;
  }
  return w;
}

bool zelfde(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) stop("gebruik: pb-state-toets <oud.clap> <nieuw.clap> <N> [<in.wav>]");
  const clap_id N = (clap_id)std::atoi(argv[3]);
  Bib oud = laad(argv[1]), nieuw = laad(argv[2]);
  Wav in = argc > 4 ? leesWav(argv[4]) : ruis(48000);
  if (in.k[0].size() > 96000) for (auto& c : in.k) c.resize(96000);  // 2 s is genoeg
  std::printf("== state-toets: oude bouw zonder parameter %u → nieuwe bouw\n", N);

  std::map<clap_id, Inst::Info> psOud, psNieuw;
  { Inst o(oud); psOud = o.params(); }
  { Inst n(nieuw); psNieuw = n.params(); }
  if (psOud.count(N)) fout("de oude bouw kent parameter " + std::to_string(N) + " nog");
  if (!psNieuw.count(N)) fout("de nieuwe bouw kent parameter " + std::to_string(N) + " niet");
  if (psOud.size() + 1 != psNieuw.size()) fout("de oude bouw heeft niet precies één parameter minder");
  for (auto& [id, i] : psOud)
    if (!psNieuw.count(id) || !zelfde(psNieuw[id].standaard, i.standaard)) fout("id " + std::to_string(id) + " verschilt tussen de bouwen");
  if (g_fouten) return 1;

  // 1. oud → nieuw
  const auto wOud = anders(psOud, 0.3);
  Buf stateOud;
  { Inst o(oud); o.flush(wOud); stateOud = o.bewaar(); }
  {
    uint32_t aantal = 0;
    if (stateOud.d.size() >= 12) std::memcpy(&aantal, &stateOud.d[8], 4);
    for (size_t p = 12; p + 12 <= stateOud.d.size(); p += 12) {
      uint32_t id; std::memcpy(&id, &stateOud.d[p], 4);
      if (id == N) fout("de oude state bevat id " + std::to_string(N));
    }
    ok("oude state: %zu bytes, %u parameters", stateOud.d.size(), aantal);
  }
  {
    Inst n(nieuw);
    g_rescans = 0;
    if (!n.laadState(stateOud)) fout("de nieuwe bouw weigert de oude state");
    if (g_rescans < 1) fout("na state.load geen params.rescan(VALUES) naar de host");
    for (auto& [id, info] : psNieuw) {
      double v = n.waarde(id);
      double verwacht = id == N ? info.standaard : wOud.at(id);
      if (!zelfde(v, verwacht))
        fout("id " + std::to_string(id) + " = " + std::to_string(v) + ", verwacht " + std::to_string(verwacht));
    }
    ok("oud → nieuw: %zu waarden overgenomen, id %u op zijn standaard (%g), rescan %d×", wOud.size(), N,
                psNieuw[N].standaard, g_rescans);
  }

  // 2. klinkt hetzelfde
  {
    Inst o(oud); o.flush(wOud);
    auto ref = render(o, in);
    Inst a(nieuw); a.laadState(stateOud);
    auto metState = render(a, in);
    Inst b(nieuw); b.flush(wOud);
    auto metFlush = render(b, in);
    vergelijkAudio("nieuw met oude state = oud", metState, ref);
    vergelijkAudio("nieuw met oude state = nieuw met dezelfde waarden via flush", metState, metFlush);
  }

  // 3. state laden terwijl actief (tussen blok 20 en 21)
  {
    Inst a(nieuw);
    auto metState = render(a, in, [&](int nr) { if (nr == 21) { if (!a.laadState(stateOud)) fout("laden terwijl actief faalt"); } });
    std::map<clap_id, double> wN = wOud; wN[N] = psNieuw[N].standaard;
    Inst b(nieuw);
    auto metFlush = render(b, in, [&](int nr) { if (nr == 21) b.flush(wN); });
    vergelijkAudio("state laden terwijl actief = flush op hetzelfde moment", metState, metFlush);
    for (auto& [id, v] : wN)
      if (!zelfde(a.waarde(id), v)) fout("na laden terwijl actief: id " + std::to_string(id) + " klopt niet");
  }

  // 4. nieuw → oud
  {
    const auto wNieuw = anders(psNieuw, 0.55);
    Buf stateNieuw;
    { Inst n(nieuw); n.flush(wNieuw); stateNieuw = n.bewaar(); }
    Inst o(oud);
    if (!o.laadState(stateNieuw)) fout("de oude bouw weigert de nieuwe state");
    for (auto& [id, info] : psOud)
      if (!zelfde(o.waarde(id), wNieuw.at(id))) fout("nieuw → oud: id " + std::to_string(id) + " klopt niet");
    ok("nieuw → oud: id %u overgeslagen, %zu waarden overgenomen", N, psOud.size());
  }

  // 5. nieuwere versie en kapotte state
  {
    Inst n(nieuw);
    Buf toekomst = stateOud;
    uint32_t v2 = 2, aantal; std::memcpy(&aantal, &toekomst.d[8], 4);
    ++aantal;
    std::memcpy(&toekomst.d[4], &v2, 4); std::memcpy(&toekomst.d[8], &aantal, 4);
    uint32_t id999 = 999; double w = 0.123;
    toekomst.d.insert(toekomst.d.end(), (unsigned char*)&id999, (unsigned char*)&id999 + 4);
    toekomst.d.insert(toekomst.d.end(), (unsigned char*)&w, (unsigned char*)&w + 8);
    if (!n.laadState(toekomst)) fout("state van een nieuwere versie (extra id 999) wordt geweigerd");
    for (auto& [id, v] : wOud) if (!zelfde(n.waarde(id), v)) fout("versie 2: id " + std::to_string(id) + " klopt niet");
    ok("state van versie 2 met onbekend id 999: geladen, 999 overgeslagen");

    std::map<clap_id, double> voor;
    for (auto& [id, i] : psNieuw) voor[id] = n.waarde(id);
    Buf leeg, kop = stateOud, kort = stateOud;
    std::memcpy(kop.d.data(), "XXXX", 4);
    kort.d.resize(kort.d.size() - 5);
    if (n.laadState(leeg)) fout("lege state geaccepteerd");
    if (n.laadState(kop)) fout("state met een andere kop geaccepteerd");
    if (n.laadState(kort)) fout("afgekapte state geaccepteerd");
    for (auto& [id, v] : voor) if (!zelfde(n.waarde(id), v)) fout("kapotte state veranderde id " + std::to_string(id));
    ok("kapotte state (leeg, andere kop, afgekapt): geweigerd, waarden onveranderd");
  }

  if (g_fouten) { std::printf("state-toets N=%u: %d fout(en)\n", N, g_fouten); return 1; }
  std::printf("state-toets N=%u: geslaagd\n", N);
  return 0;
}
