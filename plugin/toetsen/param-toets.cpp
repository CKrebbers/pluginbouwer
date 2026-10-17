// param-toets — de eigen toetsen van fase 2, als kleine CLAP-host (zoals gereedschap/pb-render).
//
//   param-toets state <volledig.clap> <zonder.clap> <id>   state van een build zonder parameter <id> laadt in de
//                                                         volledige build (vooruit-compatibel), en andersom
//   param-toets automatisering <plugin.clap>              events in process, via flush tussen blokken, in blokken van
//                                                         elke grootte: de uitvoer is bit voor bit gelijk; een
//                                                         automatisering die de waarde niet verandert, verandert niets
//   param-toets tekst <plugin.clap>                       tekst <-> waarde: voorbeelden, grenzen, en dat elke tekst na
//                                                         één rondje vast blijft (dicht bemonsterd over het bereik)
//
// Exitcode 0 = geslaagd; elke fout staat in de uitvoer met "FOUT". Met -fsanitize=realtime (clang >= 20) lopen
// process en flush (op de audiothread) in een real-time context, net als in pb-render.
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

int g_fouten = 0;
void fout(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); ++g_fouten; }
[[noreturn]] void stop(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); std::exit(2); }
void ok(const std::string& t) { std::printf("ok: %s\n", t.c_str()); }

// --- host ------------------------------------------------------------------------------------------------------
const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "param-toets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

struct Events { std::vector<clap_event_param_value_t> ev; };
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<const Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<const Events*>(l->ctx)->ev[i].header;
}
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }
clap_event_param_value_t maakEvent(uint32_t tijd, clap_id id, double v) {
  clap_event_param_value_t e{};
  e.header = {sizeof(e), tijd, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.param_id = id; e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
  return e;
}

struct Buf { std::vector<unsigned char> d; size_t pos = 0; };
int64_t ostWrite(const clap_ostream_t* s, const void* p, uint64_t n) {
  auto* b = static_cast<Buf*>(s->ctx);
  auto* c = static_cast<const unsigned char*>(p);
  b->d.insert(b->d.end(), c, c + n);
  return (int64_t)n;
}
int64_t istRead(const clap_istream_t* s, void* p, uint64_t n) {
  auto* b = static_cast<Buf*>(s->ctx);
  uint64_t m = b->d.size() - b->pos;
  if (n < m) m = n;
  std::memcpy(p, b->d.data() + b->pos, m);
  b->pos += m;
  return (int64_t)m;
}

// noinline + nonblocking: zo instrumenteert RTSan deze aanroepen als real-time context (zie pb-render.cpp)
__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT {
  return pl->process(pl, pr);
}
__attribute__((noinline)) void doeFlushAudio(const clap_plugin_params_t* pp, const clap_plugin_t* pl,
                                             const clap_input_events_t* in, const clap_output_events_t* uit) PB_RT {
  pp->flush(pl, in, uit);
}

struct Bibliotheek {
  void* h = nullptr;
  const clap_plugin_entry_t* entry = nullptr;
  const clap_plugin_factory_t* fab = nullptr;
  std::string id;
  explicit Bibliotheek(const std::string& pad) {
    std::string lib = pad;
    struct stat st{};
    if (stat(pad.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {  // macOS-bundle: Contents/MacOS/<naam zonder .clap>
      std::string naam = pad.substr(pad.find_last_of('/') + 1);
      if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
      lib = pad + "/Contents/MacOS/" + naam;
    }
    h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) stop(std::string("dlopen: ") + dlerror());
    entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
    if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry in " + pad);
    fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!fab || fab->get_plugin_count(fab) < 1) stop("geen plugin in de factory van " + pad);
    id = fab->get_plugin_descriptor(fab, 0)->id;
  }
};

struct Param { clap_id id; std::string naam; double min, max, standaard; uint32_t flags; };

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_state_t* st = nullptr;
  bool actief = false;
  explicit Instantie(const Bibliotheek& b) {
    pl = b.fab->create_plugin(b.fab, &HOST, b.id.c_str());
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    st = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    if (!pp || !st) stop("geen params- of state-extensie");
  }
  ~Instantie() {
    if (actief) { pl->stop_processing(pl); pl->deactivate(pl); }
    pl->destroy(pl);
  }
  std::vector<Param> params() const {
    std::vector<Param> r;
    for (uint32_t i = 0; i < pp->count(pl); ++i) {
      clap_param_info_t info{};
      if (!pp->get_info(pl, i, &info)) stop("get_info mislukt");
      r.push_back({info.id, info.name, info.min_value, info.max_value, info.default_value, info.flags});
    }
    return r;
  }
  bool waarde(clap_id id, double& v) const { return pp->get_value(pl, id, &v); }
  void flush(const std::vector<std::pair<clap_id, double>>& w) {
    Events ev;
    for (auto& [id, v] : w) ev.ev.push_back(maakEvent(0, id, v));
    clap_input_events_t in = {&ev, evSize, evGet};
    clap_output_events_t uit = {nullptr, evPush};
    if (actief) doeFlushAudio(pp, pl, &in, &uit);
    else pp->flush(pl, &in, &uit);
  }
  Buf bewaar() const {
    Buf b;
    clap_ostream_t os = {&b, ostWrite};
    if (!st->save(pl, &os)) stop("state save mislukt");
    return b;
  }
  bool laad(Buf b) {
    b.pos = 0;
    clap_istream_t is = {&b, istRead};
    return st->load(pl, &is);
  }
  void activeer(double sr, uint32_t maxBlok) {
    if (!pl->activate(pl, sr, 1, maxBlok)) stop("activate mislukt");
    if (!pl->start_processing(pl)) stop("start_processing mislukt");
    actief = true;
  }
};

// --- audio -----------------------------------------------------------------------------------------------------
constexpr double SR = 48000;
using Stereo = std::vector<std::vector<float>>;

// vast testsignaal: tikken, ruisstoten en een glijdende sinus, links en rechts verschillend
Stereo testsignaal(size_t n) {
  Stereo s(2, std::vector<float>(n, 0.0f));
  uint32_t r = 12345;
  double fase = 0;
  for (size_t i = 0; i < n; ++i) {
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    const double ruis = ((double)(r >> 8) / 8388608.0) - 1.0;
    const double t = (double)i / SR;
    const double stoot = (i % 24000) < 2400 ? 0.5 * ruis * std::exp(-(double)(i % 24000) / 600.0) : 0.0;
    fase += 2 * M_PI * (220.0 + 200.0 * t) / SR;
    s[0][i] = (float)(stoot + 0.25 * std::sin(fase) + ((i % 9600) == 0 ? 0.8 : 0.0));
    s[1][i] = (float)(0.7 * stoot + 0.2 * std::sin(1.5 * fase));
  }
  return s;
}

struct Auto { uint32_t sample; clap_id id; double waarde; };  // automatisering op een absolute sample

enum class Route { InProcess, Flush };

// Rendert `in` door een verse, al ingestelde instantie. Blokken volgen `blokken` (cyclisch). Route::InProcess stuurt
// de events met hun sample in het blok mee; Route::Flush knipt de blokken bij elk event en stuurt het event via
// params.flush vóór de process-aanroep (buiten process, op de audiothread).
Stereo render(Instantie& ins, const Stereo& in, const std::vector<uint32_t>& blokken, std::vector<Auto> aut, Route r) {
  uint32_t maxBlok = 1;
  for (auto b : blokken) maxBlok = b > maxBlok ? b : maxBlok;
  if (!ins.actief) ins.activeer(SR, maxBlok);
  const size_t n = in[0].size();
  Stereo uit(2, std::vector<float>(n));
  std::vector<float> bi0(maxBlok), bi1(maxBlok), bu0(maxBlok), bu1(maxBlok);
  float* pi[2] = {bi0.data(), bi1.data()};
  float* pu[2] = {bu0.data(), bu1.data()};
  clap_output_events_t uitEv = {nullptr, evPush};
  size_t pos = 0, bnr = 0, a = 0;
  int64_t steady = 0;
  while (pos < n) {
    uint32_t m = blokken[bnr++ % blokken.size()];
    if (pos + m > n) m = (uint32_t)(n - pos);
    Events ev;
    if (r == Route::Flush) {
      std::vector<std::pair<clap_id, double>> nu;
      while (a < aut.size() && aut[a].sample <= pos) { nu.push_back({aut[a].id, aut[a].waarde}); ++a; }
      if (!nu.empty()) ins.flush(nu);
      if (a < aut.size() && aut[a].sample < pos + m) m = (uint32_t)(aut[a].sample - pos);
    } else {
      while (a < aut.size() && aut[a].sample < pos + m) {
        const uint32_t t = aut[a].sample > pos ? (uint32_t)(aut[a].sample - pos) : 0;
        ev.ev.push_back(maakEvent(t, aut[a].id, aut[a].waarde));
        ++a;
      }
    }
    clap_input_events_t inEv = {&ev, evSize, evGet};
    std::memcpy(pi[0], &in[0][pos], m * 4);
    std::memcpy(pi[1], &in[1][pos], m * 4);
    clap_audio_buffer_t ai = {pi, nullptr, 2, 0, 0}, au = {pu, nullptr, 2, 0, 0};
    clap_process_t pr = {steady, m, nullptr, &ai, &au, 1, 1, &inEv, &uitEv};
    if (doeProcess(ins.pl, &pr) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
    std::memcpy(&uit[0][pos], pu[0], m * 4);
    std::memcpy(&uit[1][pos], pu[1], m * 4);
    pos += m;
    steady += m;
  }
  return uit;
}

// aantal samples dat niet bit voor bit gelijk is
size_t verschil(const Stereo& a, const Stereo& b) {
  size_t n = 0;
  for (size_t c = 0; c < 2; ++c)
    for (size_t i = 0; i < a[c].size(); ++i)
      if (std::memcmp(&a[c][i], &b[c][i], 4)) ++n;
  return n;
}
bool eindig(const Stereo& a) {
  for (auto& k : a) for (float x : k) if (!std::isfinite(x)) return false;
  return true;
}
void zelfde(const char* wat, const Stereo& a, const Stereo& b) {
  const size_t d = verschil(a, b);
  if (d) fout(std::string(wat) + ": " + std::to_string(d) + " samples verschillen");
  else ok(std::string(wat) + ": bit voor bit gelijk");
}

std::string tekst(const Instantie& ins, clap_id id, double v) {
  char t[256] = {0};
  if (!ins.pp->value_to_text(ins.pl, id, v, t, sizeof t)) return "<geen tekst>";
  return t;
}

// --- state: build zonder parameter <id> -> volledige build -----------------------------------------------------
// een waarde per parameter die niet de standaard is en exact terug moet komen
double proefwaarde(const Param& p, int k) {
  double f = std::fmod(0.137 + 0.219 * k, 1.0);
  double v = p.min + (p.max - p.min) * f;
  if (std::fabs(v - p.standaard) < 1e-9 * (1 + std::fabs(v))) v = p.min + (p.max - p.min) * std::fmod(f + 0.5, 1.0);
  if (p.flags & CLAP_PARAM_IS_STEPPED) v = std::floor(v + 0.5);
  return v;
}

int toetsState(const std::string& volledigPad, const std::string& zonderPad, clap_id zonder) {
  Bibliotheek bv(volledigPad), bz(zonderPad);
  std::printf("== state: build zonder parameter %u -> volledige build\n", zonder);

  Instantie z(bz);
  const auto pz = z.params();
  std::vector<Param> pv;
  {
    Instantie v(bv);
    pv = v.params();
  }
  if (pz.size() + 1 != pv.size()) fout("de build zonder parameter heeft " + std::to_string(pz.size()) + " parameters, de volledige " + std::to_string(pv.size()));
  for (auto& p : pz) if (p.id == zonder) fout("de build zonder parameter kent id " + std::to_string(zonder) + " toch");
  const Param* gemist = nullptr;
  for (auto& p : pv) if (p.id == zonder) gemist = &p;
  if (!gemist) stop("de volledige build kent id " + std::to_string(zonder) + " niet");

  // 1. waarden zetten in de oude build en bewaren
  std::map<clap_id, double> gezet;
  std::vector<std::pair<clap_id, double>> lijst;
  for (size_t k = 0; k < pz.size(); ++k) {
    const double w = proefwaarde(pz[k], (int)pz[k].id);
    gezet[pz[k].id] = w;
    lijst.push_back({pz[k].id, w});
  }
  z.flush(lijst);
  const Buf oud = z.bewaar();
  std::printf("   state van de build zonder %u: %zu bytes (%zu parameters)\n", zonder, oud.d.size(), pz.size());

  // 2. laden in de volledige build (niet actief); de ontbrekende parameter stond eerst ergens anders
  Instantie v(bv);
  v.flush({{zonder, proefwaarde(*gemist, 99)}});
  if (!v.laad(oud)) fout("de volledige build weigert de state van de build zonder parameter");
  for (auto& p : pv) {
    double w = 0;
    if (!v.waarde(p.id, w)) { fout("get_value " + std::to_string(p.id)); continue; }
    const double verwacht = p.id == zonder ? p.standaard : gezet[p.id];
    if (w != verwacht)
      fout("id " + std::to_string(p.id) + " (" + p.naam + "): " + std::to_string(w) + ", verwacht " + std::to_string(verwacht));
  }
  ok("waarden na laden: elke bewaarde parameter exact terug, id " + std::to_string(zonder) + " (" + gemist->naam +
     ") op zijn standaard " + tekst(v, zonder, gemist->standaard));

  // 3. het geluid: gelijk aan een volledige build die dezelfde waarden via flush kreeg
  const Stereo in = testsignaal((size_t)SR * 2);
  const std::vector<uint32_t> blok = {512};
  Stereo ref;
  {
    Instantie r(bv);
    r.flush(lijst);
    ref = render(r, in, blok, {}, Route::InProcess);
  }
  zelfde("render na state laden (niet actief) tegen dezelfde waarden via flush", render(v, in, blok, {}, Route::InProcess), ref);
  {
    Instantie a(bv);
    a.activeer(SR, 512);
    if (!a.laad(oud)) fout("laden terwijl actief mislukt");
    zelfde("render na state laden terwijl actief", render(a, in, blok, {}, Route::InProcess), ref);
  }

  // 4. opnieuw bewaren in de volledige build: nu met alle parameters, en dat laadt weer gelijk
  {
    const Buf nieuw = v.bewaar();
    Instantie w(bv);
    if (!w.laad(nieuw)) fout("state van de volledige build laadt niet terug");
    if (w.bewaar().d != nieuw.d) fout("state na een tweede rondje niet byte voor byte gelijk");
    else ok("state opnieuw bewaard (" + std::to_string(nieuw.d.size()) + " bytes) en rondje byte voor byte gelijk");
  }

  // 5. andersom (terug naar de oude build): het onbekende id wordt overgeslagen, de rest komt aan
  {
    Instantie vol(bv);
    std::vector<std::pair<clap_id, double>> alle;
    for (auto& p : pv) alle.push_back({p.id, proefwaarde(p, (int)p.id + 3)});
    vol.flush(alle);
    Instantie terug(bz);
    if (!terug.laad(vol.bewaar())) fout("de build zonder parameter weigert de state van de volledige build");
    for (auto& [id, w] : alle) {
      double x = 0;
      if (id == zonder) { if (terug.waarde(id, x)) fout("de build zonder parameter geeft toch een waarde voor id " + std::to_string(id)); continue; }
      if (!terug.waarde(id, x) || x != w) fout("terug: id " + std::to_string(id) + " kwam niet aan");
    }
    ok("andersom: state van de volledige build laadt in de build zonder " + std::to_string(zonder) + " (onbekend id overgeslagen)");
  }
  return g_fouten ? 1 : 0;
}

// --- automatisering ----------------------------------------------------------------------------------------------
int toetsAutomatisering(const std::string& pad) {
  Bibliotheek b(pad);
  std::printf("== automatisering\n");
  std::vector<Param> ps;
  {
    Instantie i(b);
    ps = i.params();
  }
  const Stereo in = testsignaal((size_t)SR * 3);
  const std::vector<uint32_t> blok512 = {512}, wisselend = {3, 128, 1, 4096, 77}, een = {1};

  Stereo stil;
  {
    Instantie i(b);
    stil = render(i, in, blok512, {}, Route::InProcess);
  }
  // 1. automatisering die de waarde niet verandert (een vlakke lijn in de DAW): elke ~37 samples alle parameters
  //    op hun standaard, op willekeurige plekken in het blok
  {
    std::vector<Auto> vlak;
    uint32_t r = 777;
    for (uint32_t s = 0; s < in[0].size(); s += 20 + (r % 37)) {
      r ^= r << 13; r ^= r >> 17; r ^= r << 5;
      for (auto& p : ps) vlak.push_back({s, p.id, p.standaard});
    }
    for (Route rt : {Route::InProcess, Route::Flush}) {
      Instantie i(b);
      zelfde(rt == Route::InProcess ? "vlakke automatisering in process (wisselende blokken)"
                                    : "vlakke automatisering via flush tussen blokken",
             render(i, in, wisselend, vlak, rt), stil);
    }
  }
  // 2. echte automatisering: Tijd glijdt in stapjes, Toon springt, Terugkoppeling over 1 en terug, Mix verandert,
  //    plus een salvo op één sample. Elke route en elke blokindeling moet hetzelfde geven.
  std::vector<Auto> aut;
  auto zoek = [&](const char* naam) -> const Param& {
    for (auto& p : ps) if (p.naam == naam) return p;
    stop(std::string("geen parameter ") + naam);
  };
  const Param& tijd = zoek("Tijd");
  const Param& fb = zoek("Terugkoppeling");
  const Param& toon = zoek("Toon");
  const Param& mix = zoek("Mix");
  for (uint32_t k = 0; k < 200; ++k) aut.push_back({6000 + k * 97, tijd.id, 375.0 - k * 1.5});
  aut.push_back({30001, toon.id, 900.0});
  aut.push_back({50000, fb.id, 1.1});
  aut.push_back({50000, mix.id, 0.8});
  aut.push_back({50000, mix.id, 0.6});  // twee events op dezelfde sample: de laatste telt
  aut.push_back({100003, fb.id, 0.4});
  aut.push_back({120000, toon.id, 12000.0});
  Stereo ref;
  {
    Instantie i(b);
    ref = render(i, in, blok512, aut, Route::InProcess);
  }
  if (!eindig(ref)) fout("automatisering geeft NaN of Inf");
  if (verschil(ref, stil) == 0) fout("automatisering verandert niets (de toets toetst dan niets)");
  {
    Instantie i(b);
    zelfde("automatisering in process, wisselende blokken 3,128,1,4096,77 tegen blok 512", render(i, in, wisselend, aut, Route::InProcess), ref);
  }
  {
    Instantie i(b);
    zelfde("automatisering in process, blokken van 1 sample", render(i, in, een, aut, Route::InProcess), ref);
  }
  {
    Instantie i(b);
    zelfde("automatisering via params.flush tussen de blokken (buiten process)", render(i, in, blok512, aut, Route::Flush), ref);
  }
  // 3. get_value volgt de automatisering
  {
    Instantie i(b);
    render(i, in, blok512, aut, Route::InProcess);
    double v = 0;
    if (!i.waarde(mix.id, v) || v != 0.6) fout("get_value(Mix) na automatisering: " + std::to_string(v) + ", verwacht 0.6");
    else ok("get_value volgt de automatisering (Mix = " + tekst(i, mix.id, v) + ")");
  }
  return g_fouten ? 1 : 0;
}

// --- tekst <-> waarde ------------------------------------------------------------------------------------------
int toetsTekst(const std::string& pad) {
  Bibliotheek b(pad);
  Instantie ins(b);
  const auto ps = ins.params();
  std::printf("== tekst <-> waarde\n");
  std::printf("   %-15s %-12s %-12s %-12s\n", "parameter", "min", "standaard", "max");
  for (auto& p : ps)
    std::printf("   %-15s %-12s %-12s %-12s\n", p.naam.c_str(), tekst(ins, p.id, p.min).c_str(),
                tekst(ins, p.id, p.standaard).c_str(), tekst(ins, p.id, p.max).c_str());

  auto naarWaarde = [&](clap_id id, const char* t, double& v) { return ins.pp->text_to_value(ins.pl, id, t, &v); };
  auto idVan = [&](const char* naam) {
    for (auto& p : ps) if (p.naam == naam) return p.id;
    stop(std::string("geen parameter ") + naam);
  };
  struct Voorbeeld { const char* param; const char* tekst; double verwacht; };
  const Voorbeeld goed[] = {
      {"Tijd", "375 ms", 375}, {"Tijd", "375", 375}, {"Tijd", "0,4 s", 400}, {"Tijd", "  20.5ms ", 20.5},
      {"Tijd", "5000 ms", 1200}, {"Tijd", "1 ms", 20},  // buiten het bereik: geklemd
      {"Toon", "3.00 kHz", 3000}, {"Toon", "3k", 3000}, {"Toon", "450 Hz", 450}, {"Toon", "1,5 KHZ", 1500},
      {"Laag weg", "100 Hz", 100}, {"Resonantie", "0.71", 0.71}, {"Resonantie", "2 q", 2},
      {"Verzadiging", "6.0 dB", 6}, {"Verzadiging", "-3 dB", 0}, {"Terugkoppeling", "50.0 %", 0.5},
      {"Terugkoppeling", "110%", 1.1}, {"Mix", "37,5 %", 0.375}, {"Mix", "100", 1}, {"Haperen", "25 %", 0.25},
  };
  for (auto& v : goed) {
    double x = -1;
    if (!naarWaarde(idVan(v.param), v.tekst, x)) fout(std::string(v.param) + ": '" + v.tekst + "' niet gelezen");
    else if (std::fabs(x - v.verwacht) > 1e-9 * (1 + std::fabs(v.verwacht)))
      fout(std::string(v.param) + ": '" + v.tekst + "' -> " + std::to_string(x) + ", verwacht " + std::to_string(v.verwacht));
  }
  ok("voorbeelden gelezen (eenheden ms/s, Hz/kHz/k, dB, Q, %; komma of punt; geklemd op het bereik)");
  const Voorbeeld fout_[] = {{"Tijd", "", 0}, {"Tijd", "abc", 0}, {"Tijd", "375 Hz", 0}, {"Toon", "3 ms", 0},
                             {"Mix", "0.5 dB", 0}, {"Verzadiging", "6 dB extra", 0}, {"Tijd", "-", 0}};
  for (auto& v : fout_) {
    double x = 0;
    if (naarWaarde(idVan(v.param), v.tekst, x)) fout(std::string(v.param) + ": '" + v.tekst + "' had geweigerd moeten worden");
  }
  ok("onzin en verkeerde eenheden geweigerd");

  // elke tekst moet na één rondje vast blijven: waarde -> tekst -> waarde -> tekst geeft dezelfde tekst
  size_t n = 0;
  for (auto& p : ps) {
    for (int k = 0; k <= 20000; ++k) {
      const double w = p.min + (p.max - p.min) * k / 20000.0;
      const std::string t1 = tekst(ins, p.id, w);
      double w2 = 0;
      if (!naarWaarde(p.id, t1.c_str(), w2)) { fout(p.naam + ": eigen tekst '" + t1 + "' niet leesbaar"); break; }
      const std::string t2 = tekst(ins, p.id, w2);
      if (t1 != t2) { fout(p.naam + ": " + std::to_string(w) + " -> '" + t1 + "' -> " + std::to_string(w2) + " -> '" + t2 + "'"); break; }
      if (w2 < p.min || w2 > p.max) { fout(p.naam + ": '" + t1 + "' buiten het bereik"); break; }
      ++n;
    }
  }
  ok(std::to_string(n) + " rondjes waarde -> tekst -> waarde -> tekst stabiel");
  return g_fouten ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string soort = argc > 1 ? argv[1] : "";
  int r;
  if (soort == "state" && argc == 5) r = toetsState(argv[2], argv[3], (clap_id)std::atoi(argv[4]));
  else if (soort == "automatisering" && argc == 3) r = toetsAutomatisering(argv[2]);
  else if (soort == "tekst" && argc == 3) r = toetsTekst(argv[2]);
  else {
    std::fprintf(stderr, "gebruik: param-toets state <volledig.clap> <zonder.clap> <id> | automatisering <plugin.clap> | tekst <plugin.clap>\n");
    return 2;
  }
  std::printf(r == 0 ? "GESLAAGD\n" : "MISLUKT (%d fouten)\n", g_fouten);
  return r;
}
