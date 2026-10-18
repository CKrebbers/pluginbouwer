// pb-statetoets — de toets van fase 2 (PROMPTS.md), door het gebouwde .clap: state laden en tekst ↔ waarde.
//
//   pb-statetoets <plugin.clap>
//
// State (ARCHITECTUUR.md "State": "PBST", versie u32, aantal u32, per parameter id u32 + waarde f64, little-endian).
// De blobs worden hier met de hand gemaakt, zonder de plugin te vragen:
//   (a) aantal = N − 1, zonder de laatste parameter (zoals een oudere build hem schreef): laadt, en die parameter
//       houdt zijn standaard;
//   (b) versie 2 met een extra onbekend id ertussen: laadt, het onbekende id wordt overgeslagen;
//   (c) andere magic, versie 0, of afgekapt (korter dan `aantal` belooft, en korter dan de kop): load geeft false en
//       verandert niets (get_value en state.save zijn daarna precies als ervoor).
// Elk geval draait twee keer: met de plugin niet actief en actief (dan neemt params.flush de geladen waarden over,
// buiten process). Daarna rendert dezelfde instantie een paar noten en moet de uitvoer bit voor bit gelijk zijn aan
// een verse instantie die dezelfde waarden via params.flush kreeg: de DSP kreeg dus echt de geladen waarden.
// Na een geslaagde load moet de plugin `rescan(CLAP_PARAM_RESCAN_VALUES)` vragen.
//
// Tekst (SPEC.md "Tekst ↔ waarde"): value_to_text van de standaard geeft precies de tekst uit de tabel, en
// text_to_value daarvan terug geeft de standaard; plus de voorbeelden uit de regels, komma én punt, met en zonder
// eenheid, klemmen buiten het bereik en false bij onleesbare tekst.
//
// Exitcode 0 = alles groen; anders 1, met per mislukte controle een regel "MISLUKT".
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int fouten = 0, goed = 0;
void controle(bool ok, const std::string& wat) {
  if (ok) ++goed;
  else ++fouten;
  std::printf("%s %s\n", ok ? "ok      " : "MISLUKT ", wat.c_str());
}
[[noreturn]] void fout(const std::string& t) {
  std::fprintf(stderr, "pb-statetoets: %s\n", t.c_str());
  std::exit(2);
}

// --- host: params-extensie (rescan tellen) -----------------------------------------------------------------------
int rescans = 0;
void hpRescan(const clap_host_t*, clap_param_rescan_flags f) {
  if (f & CLAP_PARAM_RESCAN_VALUES) ++rescans;
}
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpFlush(const clap_host_t*) {}
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return !std::strcmp(id, CLAP_EXT_PARAMS) ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-statetoets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// --- streams en events -------------------------------------------------------------------------------------------
struct Buf { std::vector<unsigned char> d; size_t pos = 0; };
int64_t ostWrite(const clap_ostream_t* s, const void* p, uint64_t n) {
  auto* b = static_cast<Buf*>(s->ctx);
  auto* c = static_cast<const unsigned char*>(p);
  b->d.insert(b->d.end(), c, c + n);
  return (int64_t)n;
}
int64_t istRead(const clap_istream_t* s, void* p, uint64_t n) {  // in stukjes van 5: de plugin moet blijven lezen
  auto* b = static_cast<Buf*>(s->ctx);
  uint64_t m = b->d.size() - b->pos;
  if (m > n) m = n;
  if (m > 5) m = 5;
  std::memcpy(p, b->d.data() + b->pos, m);
  b->pos += m;
  return (int64_t)m;
}

union Ev { clap_event_header_t h; clap_event_param_value_t p; clap_event_note_t n; };
struct Events { std::vector<Ev> ev; };
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<Events*>(l->ctx)->ev[i].h;
}
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

Ev paramEv(clap_id id, double v, uint32_t t = 0) {
  Ev e{};
  e.p.header = {sizeof(e.p), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.p.param_id = id; e.p.note_id = -1; e.p.port_index = -1; e.p.channel = -1; e.p.key = -1; e.p.value = v;
  return e;
}
Ev nootEv(bool aan, int16_t key, uint32_t t) {
  Ev e{};
  e.n.header = {sizeof(e.n), t, CLAP_CORE_EVENT_SPACE_ID, (uint16_t)(aan ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
  e.n.note_id = -1; e.n.port_index = 0; e.n.channel = 0; e.n.key = key; e.n.velocity = aan ? 0.8 : 0.0;
  return e;
}

// --- de plugin ---------------------------------------------------------------------------------------------------
const clap_plugin_factory_t* fab = nullptr;
const char* pluginId = nullptr;

struct Inst {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_state_t* st = nullptr;
  bool actief = false;
  Inst() {
    pl = fab->create_plugin(fab, &HOST, pluginId);
    if (!pl || !pl->init(pl)) fout("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    st = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    if (!pp || !st) fout("geen params- of state-extensie");
  }
  ~Inst() {
    if (actief) pl->deactivate(pl);
    pl->destroy(pl);
  }
  void activeer() {
    if (!pl->activate(pl, 48000, 1, 512)) fout("activate mislukt");
    actief = true;
  }
  void flush(Events& ev) {
    clap_input_events_t in = {&ev, evSize, evGet};
    clap_output_events_t uit = {nullptr, evPush};
    pp->flush(pl, &in, &uit);
  }
  void flushLeeg() { Events ev; flush(ev); }
  double waarde(clap_id id) {
    double v = NAN;
    if (!pp->get_value(pl, id, &v)) return NAN;
    return v;
  }
  std::vector<unsigned char> save() {
    Buf b;
    clap_ostream_t os = {&b, ostWrite};
    if (!st->save(pl, &os)) fout("state.save mislukt");
    return b.d;
  }
  bool load(const std::vector<unsigned char>& blob) {
    Buf b;
    b.d = blob;
    clap_istream_t is = {&b, istRead};
    return st->load(pl, &is);
  }
  // een paar noten, 4 blokken van 512 (activeert zelf als het nog niet gebeurd is)
  std::vector<float> render() {
    if (!actief) activeer();
    if (!pl->start_processing(pl)) fout("start_processing mislukt");
    std::vector<float> l(512), r(512), alles;
    float* uitK[2] = {l.data(), r.data()};
    clap_output_events_t uitEv = {nullptr, evPush};
    for (int blok = 0; blok < 4; ++blok) {
      Events ev;
      if (blok == 0) ev.ev.push_back(nootEv(true, 60, 7));
      if (blok == 2) ev.ev.push_back(nootEv(false, 60, 100));
      clap_input_events_t in = {&ev, evSize, evGet};
      clap_audio_buffer_t au = {uitK, nullptr, 2, 0, 0};
      clap_process_t pr = {blok * 512, 512, nullptr, nullptr, &au, 0, 1, &in, &uitEv};
      if (pl->process(pl, &pr) == CLAP_PROCESS_ERROR) fout("process gaf een fout");
      alles.insert(alles.end(), l.begin(), l.end());
      alles.insert(alles.end(), r.begin(), r.end());
    }
    pl->stop_processing(pl);
    return alles;
  }
};

struct Param { clap_id id; std::string naam; double min, max, standaard; bool getrapt; };  // getrapt: CLAP_PARAM_IS_STEPPED
std::vector<Param> params;

// --- blobs met de hand -------------------------------------------------------------------------------------------
void zet32(std::vector<unsigned char>& b, uint32_t v) {
  for (int k = 0; k < 4; ++k) b.push_back((unsigned char)(v >> (8 * k)));
}
void zet64(std::vector<unsigned char>& b, double v) {
  uint64_t u;
  std::memcpy(&u, &v, 8);
  zet32(b, (uint32_t)u);
  zet32(b, (uint32_t)(u >> 32));
}
std::vector<unsigned char> blob(const char* magic, uint32_t versie, const std::vector<std::pair<uint32_t, double>>& w) {
  std::vector<unsigned char> b(magic, magic + 4);
  zet32(b, versie);
  zet32(b, (uint32_t)w.size());
  for (auto& [id, v] : w) {
    zet32(b, id);
    zet64(b, v);
  }
  return b;
}

// een waarde binnen het bereik die niet de standaard is (exact in float32, zoals een AU hem doorgeeft)
// Getrapt (Bypass, fase 5): een hele waarde, zoals een host hem stuurt; met twee standen is dat steeds de andere.
double anders(const Param& p, int keer) {
  if (p.getrapt) {
    for (double v = p.max - (keer % 2); v >= p.min; v -= 1.0)
      if (v != p.standaard) return v;
    return p.max != p.standaard ? p.max : p.min;
  }
  const double f[] = {0.25, 0.625, 0.75};
  double v = (double)(float)(p.min + (p.max - p.min) * f[keer % 3]);
  if (v == p.standaard) v = (double)(float)(p.min + (p.max - p.min) * 0.125);
  return v;
}

std::string getal(double v) {
  char t[64];
  std::snprintf(t, sizeof t, "%.17g", v);
  return t;
}

// zet de plugin op `w` (via params.flush, buiten process) en geef de render van een verse instantie daarmee
std::vector<float> renderMet(const std::vector<double>& w) {
  Inst x;
  Events ev;
  for (size_t i = 0; i < params.size(); ++i) ev.ev.push_back(paramEv(params[i].id, w[i]));
  x.flush(ev);
  return x.render();
}

// een geval: zet eerst `voor` (via flush), laad de blob, kijk naar het resultaat
void geval(const std::string& naam, const std::vector<unsigned char>& b, bool moetLaden,
           const std::vector<double>& voor, const std::vector<double>& verwacht, bool actief) {
  const std::string wie = naam + (actief ? " [actief]" : " [niet actief]") + ": ";
  Inst x;
  if (actief) x.activeer();
  Events ev;
  for (size_t i = 0; i < params.size(); ++i) ev.ev.push_back(paramEv(params[i].id, voor[i]));
  x.flush(ev);
  const std::vector<unsigned char> stateVoor = x.save();
  const int rescansVoor = rescans;
  const bool geladen = x.load(b);
  controle(geladen == moetLaden, wie + "load geeft " + (geladen ? "true" : "false"));
  if (moetLaden) controle(rescans > rescansVoor, wie + "rescan(CLAP_PARAM_RESCAN_VALUES) na de load");
  if (actief) x.flushLeeg();  // params.flush buiten process neemt de geladen waarden over
  const std::vector<double>& doel = moetLaden ? verwacht : voor;
  for (size_t i = 0; i < params.size(); ++i) {
    const double v = x.waarde(params[i].id);
    controle(v == doel[i], wie + params[i].naam + " = " + getal(v) + " (verwacht " + getal(doel[i]) + ")");
  }
  if (!moetLaden) controle(x.save() == stateVoor, wie + "state.save na de load is byte voor byte als ervoor");
  controle(x.render() == renderMet(doel), wie + "de DSP klinkt bit voor bit als een verse instantie met die waarden");
}

void stateToetsen() {
  const size_t n = params.size();
  std::vector<double> standaard, a, b2, voor;
  for (size_t i = 0; i < n; ++i) {
    standaard.push_back(params[i].standaard);
    voor.push_back(anders(params[i], 0));
    a.push_back(anders(params[i], 1));
    b2.push_back(anders(params[i], 2));
  }
  uint32_t onbekend = 1000;
  for (bool bezet = true; bezet;) {
    bezet = false;
    for (auto& p : params)
      if (p.id == onbekend) { ++onbekend; bezet = true; }
  }

  for (bool actief : {false, true}) {
    // (a) een oudere build: aantal = N − 1, de laatste parameter ontbreekt en houdt zijn standaard
    {
      std::vector<std::pair<uint32_t, double>> w;
      for (size_t i = 0; i + 1 < n; ++i) w.push_back({params[i].id, a[i]});
      std::vector<double> verwacht = a;
      verwacht[n - 1] = standaard[n - 1];
      geval("(a) aantal N-1, zonder " + params[n - 1].naam, blob("PBST", 1, w), true, voor, verwacht, actief);
    }
    // (b) een nieuwere build: versie 2, een extra onbekend id (in het midden) wordt overgeslagen
    {
      std::vector<std::pair<uint32_t, double>> w;
      for (size_t i = 0; i < n; ++i) {
        if (i == n / 2) w.push_back({onbekend, 12345.678});
        w.push_back({params[i].id, b2[i]});
      }
      geval("(b) versie 2 met onbekend id " + std::to_string(onbekend), blob("PBST", 2, w), true, voor, b2, actief);
    }
    // (c) false en niets veranderd
    std::vector<std::pair<uint32_t, double>> heel;
    for (size_t i = 0; i < n; ++i) heel.push_back({params[i].id, a[i]});
    geval("(c) andere magic \"PBSX\"", blob("PBSX", 1, heel), false, voor, a, actief);
    geval("(c) versie 0", blob("PBST", 0, heel), false, voor, a, actief);
    {
      auto k = blob("PBST", 1, heel);
      k.resize(k.size() - 1);
      geval("(c) afgekapt: 1 byte te kort voor het aantal", k, false, voor, a, actief);
      k.resize(12 + 12 * (n - 1));
      geval("(c) afgekapt: een hele parameter te weinig", k, false, voor, a, actief);
      k.resize(10);
      geval("(c) afgekapt: midden in de kop", k, false, voor, a, actief);
      geval("(c) leeg", {}, false, voor, a, actief);
    }
  }
}

// --- tekst ↔ waarde ----------------------------------------------------------------------------------------------
struct TekstRegel { const char* naam; const char* standaard; };
// SPEC.md "Tekst ↔ waarde", kolom "standaard als tekst"
const TekstRegel TABEL[] = {{"Noot", "100 %"}, {"Poort", "0 %"}, {"Filter", "900 Hz"}, {"Loslaten", "450 ms"},
                            {"Bypass", "Uit"}};  // Bypass: van de schil (fase 5), getrapt

const Param* zoek(const char* naam) {
  for (auto& p : params)
    if (p.naam == naam) return &p;
  return nullptr;
}

void naarTekst(Inst& x, const char* naam, double v, const char* verwacht) {
  const Param* p = zoek(naam);
  char t[256] = {0};
  const bool ok = x.pp->value_to_text(x.pl, p->id, v, t, sizeof t);
  controle(ok && !std::strcmp(t, verwacht),
           std::string("value_to_text(") + naam + ", " + getal(v) + ") = \"" + t + "\" (verwacht \"" + verwacht + "\")");
}
void naarWaarde(Inst& x, const char* naam, const char* tekst, double verwacht) {
  const Param* p = zoek(naam);
  double v = NAN;
  const bool ok = x.pp->text_to_value(x.pl, p->id, tekst, &v);
  controle(ok && v == verwacht,
           std::string("text_to_value(") + naam + ", \"" + tekst + "\") = " + (ok ? getal(v) : "false") +
               " (verwacht " + getal(verwacht) + ")");
}
void onleesbaar(Inst& x, const char* naam, const char* tekst) {
  const Param* p = zoek(naam);
  double v = 0;
  controle(!x.pp->text_to_value(x.pl, p->id, tekst, &v),
           std::string("text_to_value(") + naam + ", \"" + tekst + "\") = false");
}

void tekstToetsen() {
  Inst x;
  // de standaard: precies de tekst uit de tabel, en terug precies de standaard
  for (const auto& r : TABEL) {
    const Param* p = zoek(r.naam);
    controle(p != nullptr, std::string("parameter ") + r.naam + " bestaat");
    if (!p) continue;
    naarTekst(x, r.naam, p->standaard, r.standaard);
    naarWaarde(x, r.naam, r.standaard, p->standaard);
  }
  // de voorbeelden uit de kolom "regel"
  naarTekst(x, "Poort", 0.38, "38 %");
  naarTekst(x, "Noot", 1.25, "125 %");
  naarTekst(x, "Noot", 0.25, "25 %");
  naarTekst(x, "Noot", 4, "400 %");
  naarTekst(x, "Filter", 300, "300 Hz");
  naarTekst(x, "Filter", 3000, "3,00 kHz");
  naarTekst(x, "Filter", 12000, "12,0 kHz");
  naarTekst(x, "Filter", 999.7, "1,00 kHz");  // de grens kiest op de afgeronde waarde
  naarTekst(x, "Filter", 8000, "8,00 kHz");
  naarTekst(x, "Loslaten", 20, "20,0 ms");
  naarTekst(x, "Loslaten", 375, "375 ms");
  naarTekst(x, "Loslaten", 99.97, "100 ms");
  naarTekst(x, "Loslaten", 4000, "4000 ms");

  // Bypass (fase 5): "Aan" (omzeild) of "Uit"; als woord (ook on/off) of als getal, afgerond naar een stap
  naarTekst(x, "Bypass", 1, "Aan");
  naarTekst(x, "Bypass", 0.7, "Aan");
  naarTekst(x, "Bypass", 0.3, "Uit");
  naarWaarde(x, "Bypass", "Aan", 1);
  naarWaarde(x, "Bypass", "uit", 0);
  naarWaarde(x, "Bypass", "On", 1);
  naarWaarde(x, "Bypass", "1", 1);
  naarWaarde(x, "Bypass", "0,7", 1);
  naarWaarde(x, "Bypass", "0", 0);
  naarWaarde(x, "Bypass", "5", 1);
  onleesbaar(x, "Bypass", "half");
  onleesbaar(x, "Bypass", "1 %");

  naarWaarde(x, "Poort", "38", 0.38);
  naarWaarde(x, "Poort", "38 %", 0.38);
  naarWaarde(x, "Poort", "38%", 0.38);
  naarWaarde(x, "Poort", "38,5 %", 0.385);
  naarWaarde(x, "Poort", "38.5", 0.385);
  naarWaarde(x, "Noot", "125 %", 1.25);
  naarWaarde(x, "Filter", "300 Hz", 300);
  naarWaarde(x, "Filter", "300", 300);
  naarWaarde(x, "Filter", "3,00 kHz", 3000);
  naarWaarde(x, "Filter", "3.5k", 3500);
  naarWaarde(x, "Filter", "2,25 kHz", 2250);
  naarWaarde(x, "Loslaten", "375 ms", 375);
  naarWaarde(x, "Loslaten", "375", 375);
  naarWaarde(x, "Loslaten", "20,0 ms", 20);
  naarWaarde(x, "Loslaten", " 37.5 ms ", 37.5);
  // buiten het bereik: klemmen
  naarWaarde(x, "Filter", "12,0 kHz", 8000);
  naarWaarde(x, "Filter", "5 Hz", 100);
  naarWaarde(x, "Poort", "150 %", 1);
  naarWaarde(x, "Noot", "-20 %", 0.25);
  naarWaarde(x, "Loslaten", "100000", 4000);
  // onleesbaar
  onleesbaar(x, "Filter", "");
  onleesbaar(x, "Filter", "veel");
  onleesbaar(x, "Filter", "300 ms");
  onleesbaar(x, "Loslaten", "1,2,3 ms");
  onleesbaar(x, "Poort", "38 Hz");
  onleesbaar(x, "Noot", "%");

  // rondje: elke tekst van value_to_text leest terug als de waarde die hij toont (op de afronding na: de tekst nog
  // eens maken geeft precies dezelfde tekst)
  for (const auto& p : params) {
    bool ok = true;
    std::string eerste;
    for (int k = 0; k <= 1000 && ok; ++k) {
      const double v = p.min + (p.max - p.min) * k / 1000.0;
      char t1[256], t2[256];
      double w;
      ok = x.pp->value_to_text(x.pl, p.id, v, t1, sizeof t1) && x.pp->text_to_value(x.pl, p.id, t1, &w) &&
           x.pp->value_to_text(x.pl, p.id, w, t2, sizeof t2) && !std::strcmp(t1, t2);
      if (!ok) eerste = std::string(" (mislukt bij ") + getal(v) + ": \"" + t1 + "\")";
    }
    controle(ok, p.naam + ": tekst -> waarde -> tekst blijft gelijk over 1001 waarden" + eerste);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) fout("gebruik: pb-statetoets <plugin.clap>");
  std::string pad = argv[1], lib = pad;
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
  fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fab || fab->get_plugin_count(fab) < 1) fout("geen plugin in de factory");
  pluginId = fab->get_plugin_descriptor(fab, 0)->id;

  {
    Inst x;
    const uint32_t n = x.pp->count(x.pl);
    for (uint32_t i = 0; i < n; ++i) {
      clap_param_info_t info{};
      if (!x.pp->get_info(x.pl, i, &info)) fout("get_info mislukt");
      params.push_back({info.id, info.name, info.min_value, info.max_value, info.default_value,
                        (info.flags & CLAP_PARAM_IS_STEPPED) != 0});
      controle(x.waarde(info.id) == info.default_value, std::string(info.name) + ": een verse instantie staat op de standaard");
    }
    if (params.size() < 2) fout("te weinig parameters voor de toets");
  }

  std::printf("--- state (N = %zu)\n", params.size());
  stateToetsen();
  std::printf("--- tekst <-> waarde\n");
  tekstToetsen();

  entry->deinit();
  std::printf("pb-statetoets: %d controles, %d geslaagd, %d mislukt\n", goed + fouten, goed, fouten);
  return fouten ? 1 : 0;
}
