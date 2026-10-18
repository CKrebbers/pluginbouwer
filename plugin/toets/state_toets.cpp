// state_toets — de eigen toets van fase 2 (fase 5: de bypass, id 10, telt mee als stepped parameter), door het gebouwde .clap (dlopen, zoals een host).
//
//   state_toets <plugin.clap>
//
// State (ARCHITECTUUR.md "State": "PBST", versie u32, aantal u32, per parameter id u32 + waarde f64):
//   (a) een blob met de hand zonder de laatste parameter (aantal = N − 1, zoals een oudere build hem schreef) laadt,
//       en die parameter houdt zijn standaard;
//   (b) een blob met versie 2 en een extra onbekend id laadt (per id; het onbekende id wordt overgeslagen);
//   (c) andere magic, versie 0 of een afgekapte blob geeft false en verandert niets (ook geen rescan).
// Elk geval inactief en actief (actief neemt process de waarden over; get_value leest ze meteen).
// Tekst (SPEC.md "Tekst ↔ waarde"): value_to_text van de standaard geeft precies de tekst uit de tabel, en
// text_to_value daarvan geeft de waarde terug (binnen de resolutie van de tekst, en dezelfde tekst).
// Plus een paar invoerregels uit SPEC (punt en komma, met of zonder eenheid, "k", procenten, klemmen, onleesbaar).
// Uitvoer: per controle "ok" of "FOUT"; exitcode 0 alleen als alles ok is.
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int fouten = 0, controles = 0;
void controle(bool goed, const std::string& wat) {
  ++controles;
  if (!goed) ++fouten;
  std::printf("%s  %s\n", goed ? "ok  " : "FOUT", wat.c_str());
}

// --- host: telt rescan en request_flush --------------------------------------------------------------------
int rescans = 0, flushVerzoeken = 0;
void hRescan(const clap_host_t*, clap_param_rescan_flags) { ++rescans; }
void hClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hRequestFlush(const clap_host_t*) { ++flushVerzoeken; }
const clap_host_params_t HOST_PARAMS = {hRescan, hClear, hRequestFlush};
const void* hExt(const clap_host_t*, const char* id) {
  return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &HOST_PARAMS : nullptr;
}
void hNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-statetoets", "Pluginbouwer", "", "1", hExt, hNiets, hNiets, hNiets};

// --- streams ------------------------------------------------------------------------------------------------
struct Lees { const std::vector<unsigned char>* b; size_t p = 0; };
int64_t leesFn(const clap_istream_t* s, void* buf, uint64_t n) {
  auto* l = static_cast<Lees*>(s->ctx);
  // in stukjes van hoogstens 7 bytes: een plugin moet ook korte reads aankunnen
  uint64_t m = l->b->size() - l->p;
  if (m > n) m = n;
  if (m > 7) m = 7;
  std::memcpy(buf, l->b->data() + l->p, m);
  l->p += m;
  return (int64_t)m;
}
int64_t schrijfFn(const clap_ostream_t* s, const void* buf, uint64_t n) {
  auto* b = static_cast<std::vector<unsigned char>*>(s->ctx);
  const auto* c = static_cast<const unsigned char*>(buf);
  b->insert(b->end(), c, c + n);
  return (int64_t)n;
}

// --- blobs met de hand --------------------------------------------------------------------------------------
struct Blob {
  std::vector<unsigned char> b;
  void u32(uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back((unsigned char)(v >> (8 * k))); }
  void f64(double v) { uint64_t u; std::memcpy(&u, &v, 8); for (int k = 0; k < 8; ++k) b.push_back((unsigned char)(u >> (8 * k))); }
  void kop(const char* magic, uint32_t versie, uint32_t aantal) { b.insert(b.end(), magic, magic + 4); u32(versie); u32(aantal); }
  void param(uint32_t id, double v) { u32(id); f64(v); }
};

struct Info { clap_id id; std::string naam; double min, max, standaard; bool stappen = false; };

// een waarde binnen het bereik die niet de standaard is (bij stappen, zoals de bypass: een hele stap)
double anders(const Info& p, int k) {
  if (p.stappen) return p.standaard == p.max ? p.min : p.max;
  const double t = 0.15 + std::fmod(0.07 * k, 0.7);  // binnen (0, 1): nooit geklemd
  double v = p.min + (p.max - p.min) * t;
  if (v == p.standaard) v = p.min + (p.max - p.min) * (t + 0.1);
  return v;
}

struct Toets {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_state_t* st = nullptr;
  std::vector<Info> par;
  bool actief = false;

  double waarde(size_t i) { double v = NAN; pp->get_value(pl, par[i].id, &v); return v; }
  std::vector<double> alle() { std::vector<double> v; for (size_t i = 0; i < par.size(); ++i) v.push_back(waarde(i)); return v; }
  bool laad(const std::vector<unsigned char>& b) {
    Lees l{&b};
    clap_istream_t s{&l, leesFn};
    return st->load(pl, &s);
  }
  std::vector<unsigned char> bewaar() {
    std::vector<unsigned char> b;
    clap_ostream_t s{&b, schrijfFn};
    if (!st->save(pl, &s)) b.clear();
    return b;
  }
  // één process-blok stilte (actief: process neemt een klaarstaande state over)
  void blok() {
    if (!actief) return;
    float l[64] = {}, r[64] = {}, ul[64], ur[64];
    float* in[2] = {l, r};
    float* uit[2] = {ul, ur};
    clap_audio_buffer_t bi{in, nullptr, 2, 0, 0}, bu{uit, nullptr, 2, 0, 0};
    clap_input_events_t ie{nullptr, [](const clap_input_events_t*) -> uint32_t { return 0; },
                           [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; }};
    clap_output_events_t oe{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
    clap_process_t pr{};
    pr.steady_time = -1;
    pr.frames_count = 64;
    pr.audio_inputs = &bi; pr.audio_inputs_count = 1;
    pr.audio_outputs = &bu; pr.audio_outputs_count = 1;
    pr.in_events = &ie; pr.out_events = &oe;
    pl->process(pl, &pr);
  }
  // zet alle parameters op een niet-standaard waarde, met een volledige v1-blob
  std::vector<double> zetAnders() {
    Blob b;
    b.kop("PBST", 1, (uint32_t)par.size());
    std::vector<double> v;
    for (size_t i = 0; i < par.size(); ++i) { v.push_back(anders(par[i], (int)i)); b.param(par[i].id, v.back()); }
    laad(b.b);
    blok();
    return v;
  }
  std::string tekst(size_t i, double v) {
    char t[256] = {};
    if (!pp->value_to_text(pl, par[i].id, v, t, sizeof t)) return "<false>";
    return t;
  }
  bool naarWaarde(size_t i, const char* t, double& v) { return pp->text_to_value(pl, par[i].id, t, &v); }
};

std::string getal(double v) { char t[64]; std::snprintf(t, sizeof t, "%.17g", v); return t; }

// params.flush buiten process: een PARAM_VALUE-event via flush is meteen te lezen met get_value (en na een blok nog)
struct Lijst { std::vector<clap_event_param_value_t> e; };
uint32_t lijstGrootte(const clap_input_events_t* l) { return (uint32_t)static_cast<const Lijst*>(l->ctx)->e.size(); }
const clap_event_header_t* lijstGet(const clap_input_events_t* l, uint32_t i) { return &static_cast<const Lijst*>(l->ctx)->e[i].header; }
bool uitNiets(const clap_output_events_t*, const clap_event_header_t*) { return true; }

void flushToets(Toets& t, const char* label) {
  Lijst l;
  std::vector<double> w;
  for (size_t i = 0; i < t.par.size(); ++i) {
    clap_event_param_value_t e{};
    e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    e.param_id = t.par[i].id;
    e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
    w.push_back(anders(t.par[i], (int)i + 9));
    e.value = w.back();
    l.e.push_back(e);
  }
  clap_event_param_value_t buiten = l.e[0];
  buiten.value = t.par[0].max * 10;  // buiten het bereik: klemmen
  l.e.push_back(buiten);
  w[0] = t.par[0].max;
  clap_input_events_t in{&l, lijstGrootte, lijstGet};
  clap_output_events_t uit{nullptr, uitNiets};
  t.pp->flush(t.pl, &in, &uit);
  bool alle = true;
  for (size_t i = 0; i < t.par.size(); ++i) alle = alle && t.waarde(i) == w[i];
  controle(alle, std::string(label) + ": params.flush buiten process: get_value geeft de nieuwe waarden (buiten het bereik geklemd)");
  t.blok();
  alle = true;
  for (size_t i = 0; i < t.par.size(); ++i) alle = alle && t.waarde(i) == w[i];
  controle(alle, std::string(label) + ": en na een process-blok nog steeds");
}

void stateToetsen(Toets& t, const char* label) {
  flushToets(t, label);
  const size_t N = t.par.size();
  std::printf("-- state (%s) --\n", label);

  // (a) een oudere build: aantal = N − 1, de laatste parameter ontbreekt
  {
    const auto v = t.zetAnders();
    controle(t.waarde(N - 1) != t.par[N - 1].standaard, std::string(label) + ": voorbereiding: " + t.par[N - 1].naam + " staat niet op zijn standaard");
    Blob b;
    b.kop("PBST", 1, (uint32_t)(N - 1));
    std::vector<double> w;
    for (size_t i = 0; i + 1 < N; ++i) { w.push_back(anders(t.par[i], (int)i + 3)); b.param(t.par[i].id, w.back()); }
    const int r0 = rescans;
    const bool ok = t.laad(b.b);
    t.blok();
    controle(ok, std::string(label) + ": (a) blob zonder de laatste parameter (aantal = N - 1) laadt");
    controle(rescans == r0 + 1, std::string(label) + ": (a) na de load een rescan");
    bool rest = true;
    for (size_t i = 0; i + 1 < N; ++i) rest = rest && t.waarde(i) == w[i];
    controle(rest, std::string(label) + ": (a) de andere parameters hebben de waarden uit de blob");
    controle(t.waarde(N - 1) == t.par[N - 1].standaard,
             std::string(label) + ": (a) " + t.par[N - 1].naam + " houdt zijn standaard (" + getal(t.waarde(N - 1)) + " = " + getal(t.par[N - 1].standaard) + ")");
    (void)v;
  }

  // (b) een nieuwere build: versie 2, een extra onbekend id (midden en aan het eind)
  {
    t.zetAnders();
    Blob b;
    b.kop("PBST", 2, (uint32_t)(N + 2));
    std::vector<double> w;
    for (size_t i = 0; i < N; ++i) {
      w.push_back(anders(t.par[i], (int)i + 5));
      b.param(t.par[i].id, w.back());
      if (i == N / 2) b.param(4242, 123.0);  // onbekend id
    }
    b.param(0xFFFFFFF0u, -7.0);  // nog een onbekend id
    const int r0 = rescans;
    const bool ok = t.laad(b.b);
    t.blok();
    controle(ok, std::string(label) + ": (b) blob versie 2 met onbekende id's laadt");
    controle(rescans == r0 + 1, std::string(label) + ": (b) na de load een rescan");
    bool alle = true;
    for (size_t i = 0; i < N; ++i) alle = alle && t.waarde(i) == w[i];
    controle(alle, std::string(label) + ": (b) alle bekende parameters hebben de waarden uit de blob");
  }

  // (c) ongeldige blobs: false, niets veranderd, geen rescan
  {
    t.zetAnders();
    const auto voor = t.alle();
    const auto opgeslagen = t.bewaar();
    auto geldig = [&](uint32_t versie) {
      Blob b;
      b.kop("PBST", versie, (uint32_t)N);
      for (size_t i = 0; i < N; ++i) b.param(t.par[i].id, t.par[i].standaard);  // zou alles naar de standaard zetten
      return b.b;
    };
    struct Geval { std::string naam; std::vector<unsigned char> b; };
    std::vector<Geval> gevallen;
    { auto b = geldig(1); std::memcpy(b.data(), "PBSX", 4); gevallen.push_back({"andere magic (PBSX)", b}); }
    { auto b = geldig(1); std::memcpy(b.data(), "TSBP", 4); gevallen.push_back({"andere magic (TSBP)", b}); }
    gevallen.push_back({"versie 0", geldig(0)});
    { auto b = geldig(1); b.pop_back(); gevallen.push_back({"afgekapt: 1 byte te kort", b}); }
    { auto b = geldig(1); b.resize(12 + (N - 1) * 12); gevallen.push_back({"afgekapt: laatste parameter ontbreekt maar aantal = N", b}); }
    { auto b = geldig(1); b.resize(10); gevallen.push_back({"afgekapt: midden in de kop", b}); }
    gevallen.push_back({"leeg", {}});
    for (const auto& g : gevallen) {
      const int r0 = rescans, f0 = flushVerzoeken;
      const bool ok = t.laad(g.b);
      t.blok();
      controle(!ok, std::string(label) + ": (c) " + g.naam + " geeft false");
      controle(t.alle() == voor, std::string(label) + ": (c) " + g.naam + ": geen parameter veranderd");
      controle(rescans == r0 && flushVerzoeken == f0, std::string(label) + ": (c) " + g.naam + ": geen rescan of flush-verzoek");
    }
    controle(t.bewaar() == opgeslagen, std::string(label) + ": (c) state.save daarna is byte voor byte dezelfde");
  }

  // rondje: save → load geeft dezelfde blob
  {
    t.zetAnders();
    const auto b = t.bewaar();
    controle(b.size() == 12 + N * 12 && std::memcmp(b.data(), "PBST", 4) == 0, std::string(label) + ": state.save schrijft PBST met N parameters");
    controle(t.laad(b), std::string(label) + ": de eigen blob laadt weer");
    t.blok();
    controle(t.bewaar() == b, std::string(label) + ": save na load is byte voor byte dezelfde");
  }
}

void tekstToetsen(Toets& t) {
  std::printf("-- tekst <-> waarde --\n");
  // SPEC.md "Tekst ↔ waarde", kolom "standaard als tekst", en het getal dat in die tekst staat
  struct Rij { clap_id id; const char* tekst; double getal; double resolutie; };
  const Rij tabel[] = {
      {1, "375 ms", 375, 1},       {2, "50 %", 0.5, 0.01},     {3, "3,00 kHz", 3000, 10},
      {4, "100 Hz", 100, 1},       {5, "Q 0,71", 0.71, 0.01},  {6, "6,0 dB", 6, 0.1},
      {7, "25 %", 0.25, 0.01},     {8, "38 %", 0.38, 0.01},    {9, "0 %", 0, 0.01},
      {10, "Uit", 0, 0},  // de bypass (fase 5)
  };
  controle(t.par.size() == sizeof tabel / sizeof tabel[0], "evenveel parameters als rijen in de tabel");
  for (const auto& r : tabel) {
    size_t i = 0;
    while (i < t.par.size() && t.par[i].id != r.id) ++i;
    if (i == t.par.size()) { controle(false, "parameter " + std::to_string(r.id) + " bestaat"); continue; }
    const std::string s = t.tekst(i, t.par[i].standaard);
    controle(s == r.tekst, t.par[i].naam + ": value_to_text(" + getal(t.par[i].standaard) + ") = \"" + s + "\" (tabel: \"" + r.tekst + "\")");
    double v = NAN;
    const bool ok = t.naarWaarde(i, r.tekst, v);
    controle(ok && std::fabs(v - r.getal) < 1e-9 && std::fabs(v - t.par[i].standaard) <= r.resolutie / 2 + 1e-12,
             t.par[i].naam + ": text_to_value(\"" + r.tekst + "\") = " + getal(v) + " (standaard " + getal(t.par[i].standaard) + ", binnen de halve resolutie " + getal(r.resolutie / 2) + ")");
    controle(ok && t.tekst(i, v) == r.tekst, t.par[i].naam + ": en die waarde geeft weer \"" + r.tekst + "\"");
  }

  // invoerregels uit SPEC: komma én punt, met of zonder eenheid, "k" voor kHz, procenten, klemmen, onleesbaar = false
  struct Invoer { clap_id id; const char* tekst; bool leesbaar; double waarde; };
  const Invoer invoer[] = {
      {1, "375", true, 375},        {1, "375 ms", true, 375},     {1, "20,0 ms", true, 20},   {1, "20.5", true, 20.5},
      {1, "5000", true, 1200},      {1, "1", true, 20},           {1, "abc", false, 0},       {1, "375 Hz", false, 0},
      {2, "38", true, 0.38},        {2, "38 %", true, 0.38},      {2, "125 %", true, 1.25},   {2, "200 %", true, 1.25},
      {3, "3k", true, 3000},        {3, "3,00 kHz", true, 3000},  {3, "12,0 kHz", true, 12000}, {3, "300 Hz", true, 300},
      {3, "2.5 khz", true, 2500},   {4, "50", true, 50},          {5, "Q 0,71", true, 0.71},  {5, "2.5", true, 2.5},
      {6, "6,0 dB", true, 6},       {6, "-3", true, 0},           {8, "", false, 0},          {9, "%", false, 0},
      {10, "Aan", true, 1},         {10, "uit", true, 0},         {10, "1", true, 1},         {10, "0", true, 0},
      {10, "on", true, 1},          {10, "half", false, 0},
  };
  for (const auto& r : invoer) {
    size_t i = 0;
    while (i < t.par.size() && t.par[i].id != r.id) ++i;
    if (i == t.par.size()) continue;
    double v = NAN;
    const bool ok = t.naarWaarde(i, r.tekst, v);
    if (r.leesbaar)
      controle(ok && std::fabs(v - r.waarde) < 1e-9, t.par[i].naam + ": text_to_value(\"" + r.tekst + "\") = " + (ok ? getal(v) : "false") + " (verwacht " + getal(r.waarde) + ")");
    else
      controle(!ok, t.par[i].naam + ": text_to_value(\"" + r.tekst + "\") is onleesbaar (false)");
  }

  // elke tekst heen en terug: over het hele bereik geeft text_to_value(value_to_text(v)) dezelfde tekst
  int mis = 0, totaal = 0;
  for (size_t i = 0; i < t.par.size(); ++i)
    for (int k = 0; k <= 2000; ++k) {
      const double v = t.par[i].min + (t.par[i].max - t.par[i].min) * k / 2000.0;
      const std::string s = t.tekst(i, v);
      double w = NAN;
      ++totaal;
      if (!t.naarWaarde(i, s.c_str(), w) || t.tekst(i, w) != s) {
        if (mis++ < 5) std::printf("      %s: %s -> \"%s\" -> %s\n", t.par[i].naam.c_str(), getal(v).c_str(), s.c_str(), getal(w).c_str());
      }
    }
  controle(mis == 0, "heen en terug over het bereik: " + std::to_string(totaal - mis) + "/" + std::to_string(totaal) + " teksten gelijk");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "gebruik: state_toets <plugin.clap>\n"); return 2; }
  std::string pad = argv[1];
#ifdef __APPLE__
  // een .clap op macOS is een bundel
  if (pad.size() > 5 && pad.compare(pad.size() - 5, 5, ".clap") == 0) {
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    naam = naam.substr(0, naam.size() - 5);
    pad += "/Contents/MacOS/" + naam;
  }
#endif
  void* lib = dlopen(pad.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!entry || !entry->init(argv[1])) { std::fprintf(stderr, "geen clap_entry\n"); return 2; }
  auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  const clap_plugin_descriptor_t* d = fab ? fab->get_plugin_descriptor(fab, 0) : nullptr;
  if (!d) { std::fprintf(stderr, "geen plugin\n"); return 2; }

  for (int ronde = 0; ronde < 2; ++ronde) {
    Toets t;
    t.pl = fab->create_plugin(fab, &HOST, d->id);
    if (!t.pl || !t.pl->init(t.pl)) { std::fprintf(stderr, "init mislukt\n"); return 2; }
    t.pp = static_cast<const clap_plugin_params_t*>(t.pl->get_extension(t.pl, CLAP_EXT_PARAMS));
    t.st = static_cast<const clap_plugin_state_t*>(t.pl->get_extension(t.pl, CLAP_EXT_STATE));
    if (!t.pp || !t.st) { std::fprintf(stderr, "params- of state-extensie ontbreekt\n"); return 2; }
    for (uint32_t i = 0; i < t.pp->count(t.pl); ++i) {
      clap_param_info_t info{};
      t.pp->get_info(t.pl, i, &info);
      t.par.push_back({info.id, info.name, info.min_value, info.max_value, info.default_value,
                       (info.flags & CLAP_PARAM_IS_STEPPED) != 0});
    }
    if (t.par.size() < 2) { std::fprintf(stderr, "te weinig parameters\n"); return 2; }
    if (ronde == 0) {
      // een verse instantie staat op de standaarden
      bool std_ = true;
      for (size_t i = 0; i < t.par.size(); ++i) std_ = std_ && t.waarde(i) == t.par[i].standaard;
      controle(std_, "een verse instantie staat op de standaarden");
      stateToetsen(t, "inactief");
      tekstToetsen(t);
    } else {
      t.actief = t.pl->activate(t.pl, 48000, 1, 64);
      controle(t.actief, "activate(48000, 1, 64)");
      t.pl->start_processing(t.pl);
      stateToetsen(t, "actief");
      t.pl->stop_processing(t.pl);
      t.pl->deactivate(t.pl);
    }
    t.pl->destroy(t.pl);
  }
  entry->deinit();
  std::printf("state-toets: %d/%d controles ok, %d fout\n", controles - fouten, controles, fouten);
  return fouten == 0 ? 0 : 1;
}
