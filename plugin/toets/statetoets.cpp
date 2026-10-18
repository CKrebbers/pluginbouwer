// statetoets — de eigen toets van fase 2, door het gebouwde .clap (zoals pb-render: dlopen, geen code van de plugin).
//
//   pb-statetoets <plugin.clap>
//
// State (ARCHITECTUUR.md "State"), met met de hand gemaakte blobs:
//   (a) versie 1 met aantal = N − 1 (zoals een oudere build hem schreef): laadt, de laatste parameter krijgt zijn
//       standaard, ook als hij vóór het laden anders stond; de render is gelijk aan een instantie met die waarden.
//   (b) versie 2 met een extra onbekend id: laadt, de bekende waarden komen aan.
//   (c) andere magic, versie 0, een afgekapte blob (minder dan aantal belooft, en korter dan de kop): false, en er
//       verandert niets (get_value, een nieuwe save byte voor byte, en de render).
// Elk geval inactief én actief (laden tussen twee process-aanroepen; process neemt de waarden over).
// Na een geslaagde load: rescan(VALUES), en request_flush als de plugin actief is; na een mislukte: geen van beide.
// Tekst (SPEC.md "Tekst ↔ waarde"): value_to_text van de standaard geeft precies de tekst uit de tabel en
// text_to_value daarvan geeft de standaard terug; plus de voorbeelden uit de tabel en de grenzen.
// params.flush buiten process: inactief en actief, de waarde komt aan in get_value en in de render.
//
// Uitvoer: één regel per toets ("ok …" of "FOUT …"), aan het eind een samenvatting; exit 0 alleen als alles ok is.
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

void toets(bool ok, const std::string& wat) {
  std::printf("%s %s\n", ok ? "ok  " : "FOUT", wat.c_str());
  ok ? ++goed : ++fouten;
}

[[noreturn]] void stop(const std::string& t) {
  std::fprintf(stderr, "pb-statetoets: %s\n", t.c_str());
  std::exit(2);
}

// --- host: telt rescan en request_flush ------------------------------------------------------------------------------
int rescans = 0, flushVragen = 0;
void hostRescan(const clap_host_t*, clap_param_rescan_flags) { ++rescans; }
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostRequestFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hostRescan, hostClear, hostRequestFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return !std::strcmp(id, CLAP_EXT_PARAMS) ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-statetoets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// --- events en streams -----------------------------------------------------------------------------------------------
union Ev {
  clap_event_header_t h;
  clap_event_param_value_t p;
  clap_event_note_t n;
};
struct Events {
  std::vector<Ev> ev;
};
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t) static_cast<const Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<const Events*>(l->ctx)->ev[i].h;
}
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

Ev paramEv(uint32_t tijd, clap_id id, double v) {
  Ev e{};
  e.p.header = {sizeof(e.p), tijd, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.p.param_id = id;
  e.p.note_id = -1;
  e.p.port_index = -1;
  e.p.channel = -1;
  e.p.key = -1;
  e.p.value = v;
  return e;
}

Ev nootEv(uint32_t tijd, bool aan, int key) {
  Ev e{};
  e.n.header = {sizeof(e.n), tijd, CLAP_CORE_EVENT_SPACE_ID, (uint16_t)(aan ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
  e.n.note_id = -1;
  e.n.port_index = 0;
  e.n.channel = 0;
  e.n.key = (int16_t)key;
  e.n.velocity = aan ? 0.8 : 0.0;
  return e;
}

using Blob = std::vector<unsigned char>;
struct Lezer {
  const Blob* b;
  size_t pos = 0;
};
int64_t ostWrite(const clap_ostream_t* s, const void* p, uint64_t n) {
  auto* b = static_cast<Blob*>(s->ctx);
  auto* c = static_cast<const unsigned char*>(p);
  b->insert(b->end(), c, c + n);
  return (int64_t)n;
}
// geeft hooguit 5 bytes per keer: de plugin moet een stream lezen tot het eind, niet één read verwachten
int64_t istRead(const clap_istream_t* s, void* p, uint64_t n) {
  auto* l = static_cast<Lezer*>(s->ctx);
  uint64_t m = l->b->size() - l->pos;
  if (n < m) m = n;
  if (m > 5) m = 5;
  std::memcpy(p, l->b->data() + l->pos, m);
  l->pos += m;
  return (int64_t)m;
}

// --- blobs met de hand: "PBST", versie, aantal, per parameter (id u32, waarde f64), little-endian ------------------
void u32(Blob& b, uint32_t v) {
  for (int k = 0; k < 4; ++k) b.push_back((unsigned char)(v >> (8 * k)));
}
void f64(Blob& b, double v) {
  uint64_t u;
  std::memcpy(&u, &v, 8);
  for (int k = 0; k < 8; ++k) b.push_back((unsigned char)(u >> (8 * k)));
}
Blob maakBlob(const char* magic, uint32_t versie, const std::vector<std::pair<uint32_t, double>>& waarden) {
  Blob b(magic, magic + 4);
  u32(b, versie);
  u32(b, (uint32_t)waarden.size());
  for (auto& [id, v] : waarden) {
    u32(b, id);
    f64(b, v);
  }
  return b;
}

// --- de plugin -------------------------------------------------------------------------------------------------------
const clap_plugin_factory_t* fab = nullptr;
const char* pluginId = nullptr;
constexpr double SR = 48000;
constexpr uint32_t BLOK = 256;

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* params = nullptr;
  const clap_plugin_state_t* state = nullptr;
  bool actief = false;

  Instantie() {
    pl = fab->create_plugin(fab, &HOST, pluginId);
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    params = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    state = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    if (!params || !state) stop("geen params- of state-extensie");
  }
  ~Instantie() {
    if (actief) {
      pl->stop_processing(pl);
      pl->deactivate(pl);
    }
    pl->destroy(pl);
  }
  void activeer() {
    if (!pl->activate(pl, SR, 1, BLOK) || !pl->start_processing(pl)) stop("activate mislukt");
    actief = true;
  }
  void flush(const std::vector<std::pair<clap_id, double>>& w) {
    Events e;
    for (auto& [id, v] : w) e.ev.push_back(paramEv(0, id, v));
    clap_input_events_t in = {&e, evSize, evGet};
    clap_output_events_t uit = {nullptr, evPush};
    params->flush(pl, &in, &uit);
  }
  double waarde(clap_id id) {
    double v = NAN;
    if (!params->get_value(pl, id, &v)) stop("get_value mislukt voor id " + std::to_string(id));
    return v;
  }
  Blob bewaar() {
    Blob b;
    clap_ostream_t os = {&b, ostWrite};
    if (!state->save(pl, &os)) stop("state.save mislukt");
    return b;
  }
  bool laad(const Blob& b) {
    Lezer l{&b};
    clap_istream_t is = {&l, istRead};
    return state->load(pl, &is);
  }
  // `blokken` blokken van BLOK samples; een noot aan in het eerste blok en uit in blok `uitBlok`
  std::vector<float> render(int blokken, int aanBlok = -1, int uitBlok = -1) {
    std::vector<float> l(BLOK), r(BLOK), alles;
    float* uit[2] = {l.data(), r.data()};
    clap_audio_buffer_t au = {uit, nullptr, 2, 0, 0};
    clap_output_events_t uitEv = {nullptr, evPush};
    for (int k = 0; k < blokken; ++k) {
      Events e;
      if (k == aanBlok) e.ev.push_back(nootEv(17, true, 60));
      if (k == uitBlok) e.ev.push_back(nootEv(101, false, 60));
      clap_input_events_t in = {&e, evSize, evGet};
      clap_process_t pr = {(int64_t)k * BLOK, BLOK, nullptr, nullptr, &au, 0, 1, &in, &uitEv};
      if (pl->process(pl, &pr) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
      alles.insert(alles.end(), l.begin(), l.end());
      alles.insert(alles.end(), r.begin(), r.end());
    }
    return alles;
  }
};

bool bitGelijk(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && !std::memcmp(a.data(), b.data(), a.size() * sizeof(float));
}
bool nietStil(const std::vector<float>& a) {
  for (float x : a)
    if (x != 0) return true;
  return false;
}

struct Param {
  clap_id id;
  std::string naam;
  double min, max, standaard;
};
std::vector<Param> ps;

// de render na de toets: een noot aan in blok 0 en uit in blok 20 (het loslaten laat de Loslaten-tijd horen), 140
// blokken (≈ 0,75 s); Poort staat in de waarden zelf op 0, dus zonder de noot is het stil
constexpr int BLOKKEN = 140, UIT_BLOK = 20;

std::string tekst(const Instantie& in, clap_id id, double v) {
  char b[256];
  std::memset(b, 'x', sizeof b);
  if (!in.params->value_to_text(in.pl, id, v, b, sizeof b)) return "<false>";
  return b;
}

// --- de state-gevallen -----------------------------------------------------------------------------------------------
// Een "andere" toestand die de plugin vóór het laden heeft: elke parameter ver van zijn standaard.
std::vector<std::pair<clap_id, double>> anders() {
  std::vector<std::pair<clap_id, double>> w;
  for (auto& p : ps) w.push_back({p.id, p.standaard == p.max ? p.min : p.max});
  return w;
}

// Laad `blob` in een instantie die op `anders()` stond (inactief, of actief na een stuk render) en vergelijk met een
// instantie die `verwacht` via flush kreeg. `slaagt`: wat load moet geven; bij false moet alles blijven zoals het was.
void geval(const std::string& naam, const Blob& blob, bool slaagt, const std::vector<std::pair<clap_id, double>>& verwacht) {
  for (int actief = 0; actief < 2; ++actief) {
    const std::string wie = naam + (actief ? " (actief)" : " (inactief)");
    Instantie a, ref;
    a.flush(anders());
    ref.flush(anders());
    std::vector<float> voorA, voorRef;
    if (actief) {
      a.activeer();
      ref.activeer();
      voorA = a.render(10, 0);
      voorRef = ref.render(10, 0);
    }
    const Blob ervoor = a.bewaar();
    const int rescansVoor = rescans, flushVoor = flushVragen;
    const bool ok = a.laad(blob);
    toets(ok == slaagt, wie + ": load geeft " + (ok ? "true" : "false"));
    if (slaagt) {
      toets(rescans == rescansVoor + 1, wie + ": rescan na de load");
      toets(flushVragen == flushVoor + (actief ? 1 : 0),
            wie + (actief ? ": request_flush na de load" : ": geen request_flush (inactief)"));
      ref.flush(verwacht);  // de referentie krijgt dezelfde waarden langs de gewone weg
    } else {
      toets(rescans == rescansVoor && flushVragen == flushVoor, wie + ": geen rescan of request_flush");
      toets(a.bewaar() == ervoor, wie + ": state daarna byte voor byte gelijk aan ervoor");
    }
    for (auto& p : ps) {
      double v = a.waarde(p.id), r = ref.waarde(p.id);
      char b[160];
      std::snprintf(b, sizeof b, ": %s = %.17g (verwacht %.17g)", p.naam.c_str(), v, r);
      toets(v == r, wie + b);
    }
    if (!actief) {
      a.activeer();
      ref.activeer();
    }
    const auto uitA = a.render(BLOKKEN, actief ? -1 : 0, UIT_BLOK), uitRef = ref.render(BLOKKEN, actief ? -1 : 0, UIT_BLOK);
    toets(nietStil(uitRef) && bitGelijk(uitA, uitRef), wie + ": render bit voor bit gelijk aan de referentie");
    if (actief) toets(bitGelijk(voorA, voorRef), wie + ": (render vóór de load gelijk)");
  }
}

void stateToetsen() {
  std::printf("\n# State (ARCHITECTUUR.md \"State\")\n");
  const int N = (int)ps.size();
  // waarden die geen standaard zijn en niet gelijk aan anders(): zo is te zien waar elke waarde vandaan komt
  std::vector<std::pair<clap_id, double>> nieuw;
  for (auto& p : ps) nieuw.push_back({p.id, p.min + (p.max - p.min) * 0.3});
  // Poort op 0 houden: de noot in de render opent hem
  for (auto& [id, v] : nieuw)
    if (id == 2) v = 0;

  {  // (a) een oudere build: aantal = N − 1, de laatste parameter ontbreekt
    std::vector<std::pair<uint32_t, double>> w(nieuw.begin(), nieuw.end() - 1);
    auto verwacht = nieuw;
    verwacht.back().second = ps.back().standaard;
    char b[160];
    std::snprintf(b, sizeof b, "(a) versie 1, aantal %d = N − 1 (zonder %s)", N - 1, ps.back().naam.c_str());
    geval(b, maakBlob("PBST", 1, w), true, verwacht);
  }
  {  // (b) een nieuwere build: versie 2, een onbekend id ertussen en erachter
    std::vector<std::pair<uint32_t, double>> w(nieuw.begin(), nieuw.end());
    w.insert(w.begin() + 1, {9999, 12345.0});
    w.push_back({77, -1.0});
    geval("(b) versie 2, aantal N + 2 met onbekende id's 9999 en 77", maakBlob("PBST", 2, w), true, nieuw);
  }
  // (c) mag niet laden: false, en niets verandert
  const std::vector<std::pair<uint32_t, double>> w(nieuw.begin(), nieuw.end());
  geval("(c) andere magic \"PBSX\"", maakBlob("PBSX", 1, w), false, {});
  geval("(c) versie 0", maakBlob("PBST", 0, w), false, {});
  {
    Blob b = maakBlob("PBST", 1, w);
    b.pop_back();
    geval("(c) afgekapt: één byte minder dan aantal belooft", b, false, {});
    b.resize(12 + 12 * (N - 1));
    geval("(c) afgekapt: één parameter minder dan aantal belooft", b, false, {});
    b.resize(10);
    geval("(c) afgekapt: midden in de kop", b, false, {});
    geval("(c) leeg", Blob{}, false, {});
  }
  {  // en het gewone rondje: save → load in een nieuwe instantie → save geeft dezelfde bytes
    Instantie a, b;
    a.flush(nieuw);
    const Blob s = a.bewaar();
    toets(s == maakBlob("PBST", 1, std::vector<std::pair<uint32_t, double>>(nieuw.begin(), nieuw.end())),
          "save schrijft \"PBST\", versie 1, aantal N, (id, waarde) little-endian");
    toets(b.laad(s) && b.bewaar() == s, "rondje: save → load → save byte voor byte gelijk");
  }
}

// --- tekst ↔ waarde --------------------------------------------------------------------------------------------------
void tekstToetsen() {
  std::printf("\n# Tekst ↔ waarde (SPEC.md)\n");
  Instantie in;
  // de tabel "Tekst ↔ waarde", kolom "standaard als tekst"
  const struct {
    clap_id id;
    const char* tekst;
  } standaard[] = {{1, "100 %"}, {2, "0 %"}, {3, "900 Hz"}, {4, "450 ms"}};
  for (auto& s : standaard) {
    const Param* p = nullptr;
    for (auto& q : ps)
      if (q.id == s.id) p = &q;
    if (!p) {
      toets(false, "parameter id " + std::to_string(s.id) + " bestaat");
      continue;
    }
    const std::string t = tekst(in, p->id, p->standaard);
    toets(t == s.tekst, p->naam + ": value_to_text(standaard) = \"" + t + "\" (tabel: \"" + s.tekst + "\")");
    double v = NAN;
    const bool ok = in.params->text_to_value(in.pl, p->id, s.tekst, &v);
    char b[200];
    std::snprintf(b, sizeof b, ": text_to_value(\"%s\") = %.17g (standaard %.17g)", s.tekst, v, p->standaard);
    toets(ok && v == p->standaard, p->naam + b);
  }

  // de voorbeelden uit de kolom "regel", en de grenzen (afronden vóór de grens kiezen)
  const struct {
    clap_id id;
    double v;
    const char* tekst;
  } voorbeelden[] = {
      {1, 0.38, "38 %"},      {1, 1.25, "125 %"},       {2, 0.38, "38 %"},       {2, 1.0, "100 %"},
      {2, 0.0, "0 %"},        {3, 300, "300 Hz"},       {3, 3000, "3,00 kHz"},   {3, 12000, "12,0 kHz"},
      {3, 999.4, "999 Hz"},   {3, 999.7, "1,00 kHz"},   {3, 9999.7, "10,0 kHz"}, {3, 1234.5, "1,23 kHz"},
      {3, 8000, "8,00 kHz"},  {3, 100, "100 Hz"},       {4, 20, "20,0 ms"},      {4, 375, "375 ms"},
      {4, 99.96, "100 ms"},   {4, 10, "10,0 ms"},       {4, 4000, "4000 ms"},    {4, 12.34, "12,3 ms"},
  };
  for (auto& x : voorbeelden) {
    const std::string t = tekst(in, x.id, x.v);
    char b[200];
    std::snprintf(b, sizeof b, "id %u: value_to_text(%g) = \"%s\" (verwacht \"%s\")", x.id, x.v, t.c_str(), x.tekst);
    toets(t == x.tekst, b);
  }

  // text_to_value: komma en punt, met of zonder eenheid, "k" voor kHz, procenten met of zonder %, klemmen, onleesbaar
  const struct {
    clap_id id;
    const char* tekst;
    bool ok;
    double v;
  } lezen[] = {
      {4, "375", true, 375},        {4, "375 ms", true, 375},       {4, "375ms", true, 375},
      {4, "20,0 ms", true, 20},     {4, "20.5", true, 20.5},        {4, "12,3 ms", true, 12.3},
      {3, "3,00 kHz", true, 3000},  {3, "3.5k", true, 3500},        {3, "3k", true, 3000},
      {3, "300 Hz", true, 300},     {3, "300", true, 300},          {3, "1,23 kHz", true, 1230},
      {1, "38", true, 0.38},        {1, "38 %", true, 0.38},        {1, "125 %", true, 1.25},
      {2, "38,5 %", true, 0.385},   {2, "0 %", true, 0},
      {3, "99999 Hz", true, 8000},  {3, "5", true, 100},            {4, "-3 ms", true, 10},
      {1, "1000 %", true, 4},       {2, "250 %", true, 1},
      {3, "abc", false, 0},         {3, "", false, 0},              {4, "375 kg", false, 0},
      {1, "%", false, 0},           {4, "12,3,4", false, 0},
  };
  for (auto& x : lezen) {
    double v = NAN;
    const bool ok = in.params->text_to_value(in.pl, x.id, x.tekst, &v);
    char b[200];
    if (x.ok)
      std::snprintf(b, sizeof b, "id %u: text_to_value(\"%s\") = %.17g (verwacht %.17g)", x.id, x.tekst, ok ? v : NAN, x.v);
    else
      std::snprintf(b, sizeof b, "id %u: text_to_value(\"%s\") geeft false", x.id, x.tekst);
    toets(ok == x.ok && (!x.ok || v == x.v), b);
  }

  // een onbekend id: false, en geen schrijven buiten de buffer
  char klein[4];
  toets(!in.params->value_to_text(in.pl, 9999, 1.0, klein, sizeof klein), "value_to_text van een onbekend id geeft false");
  std::memset(klein, 'x', sizeof klein);
  in.params->value_to_text(in.pl, 3, 900, klein, sizeof klein);
  toets(klein[3] == 0 && !std::strncmp(klein, "900", 3), "value_to_text kapt af binnen de buffer (\"900 Hz\" in 4 bytes)");

  // elke waarde die value_to_text geeft, leest text_to_value terug op de getoonde precisie
  int rondFout = 0;
  for (auto& p : ps)
    for (int k = 0; k <= 400; ++k) {
      const double v = p.min + (p.max - p.min) * k / 400.0;
      const std::string t = tekst(in, p.id, v);
      double terug = NAN;
      if (!in.params->text_to_value(in.pl, p.id, t.c_str(), &terug) || tekst(in, p.id, terug) != t) {
        if (++rondFout <= 5) std::printf("     %s: %g → \"%s\" → %g\n", p.naam.c_str(), v, t.c_str(), terug);
      }
    }
  toets(rondFout == 0, "tekst → waarde → tekst blijft gelijk over het hele bereik (4 × 401 waarden)");
}

// --- params.flush buiten process -------------------------------------------------------------------------------------
void flushToetsen() {
  std::printf("\n# params.flush buiten process\n");
  for (int actief = 0; actief < 2; ++actief) {
    const std::string wie = actief ? "flush (actief, tussen process)" : "flush (inactief)";
    Instantie a, ref;
    if (actief) {
      a.activeer();
      ref.activeer();
      a.render(4);
      ref.render(4);
    }
    a.flush({{3, 2500.0}, {4, 1200.0}});
    toets(a.waarde(3) == 2500.0 && a.waarde(4) == 1200.0, wie + ": get_value ziet de nieuwe waarden");
    a.flush({{3, 99999.0}});
    toets(a.waarde(3) == 8000.0, wie + ": een waarde buiten het bereik wordt geklemd");
    a.flush({{3, 2500.0}, {9999, 1.0}});
    toets(a.waarde(3) == 2500.0, wie + ": een onbekend id wordt overgeslagen");
    if (!actief) {
      a.activeer();
      ref.activeer();
    }
    // de referentie krijgt dezelfde waarden als event in het eerste blok van process
    const auto uitA = a.render(BLOKKEN, 0, UIT_BLOK);
    std::vector<float> l(BLOK), r(BLOK), uitRef;
    float* uit[2] = {l.data(), r.data()};
    clap_audio_buffer_t au = {uit, nullptr, 2, 0, 0};
    clap_output_events_t uitEv = {nullptr, evPush};
    for (int k = 0; k < BLOKKEN; ++k) {
      Events e;
      if (k == 0) {
        e.ev.push_back(paramEv(0, 3, 2500.0));
        e.ev.push_back(paramEv(0, 4, 1200.0));
        e.ev.push_back(nootEv(17, true, 60));
      }
      if (k == UIT_BLOK) e.ev.push_back(nootEv(101, false, 60));
      clap_input_events_t in = {&e, evSize, evGet};
      clap_process_t pr = {(int64_t)k * BLOK, BLOK, nullptr, nullptr, &au, 0, 1, &in, &uitEv};
      ref.pl->process(ref.pl, &pr);
      uitRef.insert(uitRef.end(), l.begin(), l.end());
      uitRef.insert(uitRef.end(), r.begin(), r.end());
    }
    toets(nietStil(uitRef) && bitGelijk(uitA, uitRef), wie + ": render gelijk aan dezelfde waarden als event in process");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) stop("gebruik: pb-statetoets <plugin.clap>");
  std::string pad = argv[1], lib = pad;
#ifdef __APPLE__
  {  // een .clap is op macOS een bundle: de code staat in Contents/MacOS/<naam zonder .clap>
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    lib = pad + "/Contents/MacOS/" + naam;
  }
#endif
  void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) stop(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
  if (!entry || !entry->init(pad.c_str())) stop("geen clap_entry");
  fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fab || fab->get_plugin_count(fab) < 1) stop("geen plugin in de factory");
  pluginId = fab->get_plugin_descriptor(fab, 0)->id;

  {
    Instantie in;
    const uint32_t n = in.params->count(in.pl);
    for (uint32_t i = 0; i < n; ++i) {
      clap_param_info_t info;
      if (!in.params->get_info(in.pl, i, &info)) stop("get_info mislukt");
      ps.push_back({info.id, info.name, info.min_value, info.max_value, info.default_value});
      toets(!(info.flags & CLAP_PARAM_IS_STEPPED), std::string(info.name) + ": continu (geen CLAP_PARAM_IS_STEPPED)");
      toets(in.waarde(info.id) == info.default_value, std::string(info.name) + ": een verse instantie staat op de standaard");
    }
    if (ps.size() < 2) stop("te weinig parameters voor de toets");
  }

  stateToetsen();
  tekstToetsen();
  flushToetsen();

  entry->deinit();
  std::printf("\npb-statetoets: %d toetsen, %d ok, %d fout\n", goed + fouten, goed, fouten);
  return fouten == 0 ? 0 : 1;
}
