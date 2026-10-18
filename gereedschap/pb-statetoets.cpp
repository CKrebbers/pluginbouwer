// pb-statetoets — toetst state en tekst ↔ waarde door het gebouwde .clap (PROMPTS.md fase 2). Een kleine CLAP-host.
//
//   pb-statetoets <plugin.clap> [--presets <verwacht.tsv>]
//
// State (ARCHITECTUUR.md "State"), met de hand gemaakte blobs, elk geval terwijl de plugin niet actief is én actief:
//   (a) een blob zonder de laatste parameter (aantal = N − 1, zoals een oudere build hem schreef) laadt; die parameter
//       houdt zijn standaard (niet de waarde van vóór de load);
//   (b) een blob met versie 2 en een extra onbekend id laadt; de bekende id's krijgen hun waarde;
//   (c) andere magic, versie 0 of een afgekapte blob geeft false en verandert niets: get_value, de bytes van state.save
//       en het geluid blijven gelijk, en de host krijgt geen rescan.
// Bij elk geval ook het geluid: een noot door de plugin na de load is bit voor bit gelijk aan dezelfde noot door een verse
// instantie die de verwachte waarden via params.flush kreeg (de state komt dus echt in het Ontwerp).
// Tekst (SPEC.md "Tekst ↔ waarde"): value_to_text van de standaard geeft precies de tekst uit de tabel, en text_to_value
// van die tekst geeft de standaard terug; plus de voorbeelden uit de kolom "regel" en de leesregels.
// Presets (PROMPTS.md fase 4, met --presets; de tabel maakt gereedschap/presets-verwacht.sh uit presets/*.json):
//   preset-discovery: per preset een provider met één locatie van soort PLUGIN en een container van één preset (valkuil
//   15), met de naam, de load key en het plugin-id; preset-load van elke preset (niet actief en actief) geeft precies de
//   waarden uit de JSON (ontbrekende namen hun standaard), rescan, loaded, het geluid bit voor bit en een state-rondje;
//   een onbekende load key geeft false en on_error en verandert niets.
// Exitcode 0 = alles groen; elke mislukte controle staat als "FOUT" in de uitvoer.
#include <clap/clap.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int fouten = 0, controles = 0;
void check(bool ok, const std::string& wat) {
  ++controles;
  if (!ok) ++fouten;
  std::printf("%s  %s\n", ok ? "ok  " : "FOUT", wat.c_str());
}
[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-statetoets: %s\n", t.c_str()); std::exit(2); }

// --- host: telt rescan en request_flush ---------------------------------------------------------------------
int rescans = 0, flushVragen = 0;
void hRescan(const clap_host_t*, clap_param_rescan_flags f) { if (f & CLAP_PARAM_RESCAN_VALUES) ++rescans; }
void hClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hRequestFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hRescan, hClear, hRequestFlush};
// preset-load van de host: telt loaded en on_error, en onthoudt de laatste load key
int geladen = 0, laadFouten = 0;
std::string laatsteSleutel;
void hLaadFout(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char* msg) {
  ++laadFouten;
  std::printf("     (on_error van de plugin: %s)\n", msg ? msg : "");
}
void hGeladen(const clap_host_t*, uint32_t soort, const char* locatie, const char* sleutel) {
  ++geladen;
  laatsteSleutel = (soort == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && !locatie && sleutel) ? sleutel : "?";
}
const clap_host_preset_load_t HOST_PRESET_LOAD = {hLaadFout, hGeladen};
const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return &HOST_PRESET_LOAD;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-statetoets", "Pluginbouwer", "", "1",
                          hostExt, hostNiets, hostNiets, hostNiets};

const clap_plugin_factory_t* fabriek = nullptr;
std::string pluginId;

// --- events --------------------------------------------------------------------------------------------------
struct Events {
  std::vector<clap_event_param_value_t> p;
  std::vector<clap_event_note_t> n;
  std::vector<const clap_event_header_t*> h;
  void bouw() {
    h.clear();
    for (auto& e : p) h.push_back(&e.header);
    for (auto& e : n) h.push_back(&e.header);
  }
};
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Events*>(l->ctx)->h.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return static_cast<Events*>(l->ctx)->h[i]; }
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

clap_event_param_value_t paramEvent(clap_id id, double v) {
  clap_event_param_value_t e{};
  e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.param_id = id;
  e.note_id = e.port_index = e.channel = e.key = -1;
  e.value = v;
  return e;
}

// --- streams -------------------------------------------------------------------------------------------------
struct Lezer { const std::vector<unsigned char>* b; size_t pos; };
int64_t lees(const clap_istream_t* s, void* buf, uint64_t n) {
  auto* l = static_cast<Lezer*>(s->ctx);
  // in stukjes van hooguit 5 bytes: een plugin moet een stream ook bij korte reads helemaal lezen
  uint64_t m = l->b->size() - l->pos;
  if (m > n) m = n;
  if (m > 5) m = 5;
  std::memcpy(buf, l->b->data() + l->pos, (size_t)m);
  l->pos += (size_t)m;
  return (int64_t)m;
}
int64_t schrijf(const clap_ostream_t* s, const void* buf, uint64_t n) {
  auto* b = static_cast<std::vector<unsigned char>*>(s->ctx);
  const auto* c = static_cast<const unsigned char*>(buf);
  b->insert(b->end(), c, c + n);
  return (int64_t)n;
}

// --- blobs ---------------------------------------------------------------------------------------------------
void put32(std::vector<unsigned char>& b, uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back((unsigned char)(v >> (8 * k))); }
void putF64(std::vector<unsigned char>& b, double v) {
  uint64_t u;
  std::memcpy(&u, &v, 8);
  put32(b, (uint32_t)u);
  put32(b, (uint32_t)(u >> 32));
}
// blob(magic, versie, aantal, paren): het aantal is wat de kop belooft, de paren wat er werkelijk staat
std::vector<unsigned char> blob(const char* magic, uint32_t versie, uint32_t aantal,
                                const std::vector<std::pair<uint32_t, double>>& paren) {
  std::vector<unsigned char> b(magic, magic + 4);
  put32(b, versie);
  put32(b, aantal);
  for (auto& pr : paren) { put32(b, pr.first); putF64(b, pr.second); }
  return b;
}

// --- een instantie -------------------------------------------------------------------------------------------
constexpr uint32_t BLOK = 256;
constexpr uint32_t RENDER = 48000 / 2;  // een halve seconde: noot aan op 0, uit op de helft

struct Instantie {
  const clap_plugin_t* p = nullptr;
  const clap_plugin_params_t* par = nullptr;
  const clap_plugin_state_t* st = nullptr;
  bool actief = false;

  Instantie() {
    p = fabriek->create_plugin(fabriek, &HOST, pluginId.c_str());
    if (!p || !p->init(p)) stop("plugin maken/init mislukt");
    par = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
    st = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    if (!par || !st) stop("geen params- of state-extensie");
  }
  ~Instantie() {
    if (actief) { p->stop_processing(p); p->deactivate(p); }
    p->destroy(p);
  }
  void activeer() {
    if (!p->activate(p, 48000, 1, BLOK)) stop("activate mislukt");
    if (!p->start_processing(p)) stop("start_processing mislukt");
    actief = true;
  }
  void zet(const std::map<uint32_t, double>& w) {  // params.flush, buiten process
    Events ev;
    for (auto& kv : w) ev.p.push_back(paramEvent(kv.first, kv.second));
    ev.bouw();
    clap_input_events_t in{&ev, evSize, evGet};
    clap_output_events_t uit{nullptr, evPush};
    par->flush(p, &in, &uit);
  }
  std::map<uint32_t, double> waarden() {
    std::map<uint32_t, double> w;
    for (uint32_t i = 0; i < par->count(p); ++i) {
      clap_param_info_t info;
      par->get_info(p, i, &info);
      double v = NAN;
      if (!par->get_value(p, info.id, &v)) stop("get_value mislukt");
      w[info.id] = v;
    }
    return w;
  }
  std::vector<unsigned char> bewaar() {
    std::vector<unsigned char> b;
    clap_ostream_t os{&b, schrijf};
    if (!st->save(p, &os)) stop("state.save mislukt");
    return b;
  }
  bool laad(const std::vector<unsigned char>& b) {
    Lezer l{&b, 0};
    clap_istream_t is{&l, lees};
    return st->load(p, &is);
  }
  // een noot (A onder de middelste C) aan op sample 0, uit op de helft; geeft links en rechts achter elkaar
  std::vector<float> speel() {
    if (!actief) activeer();
    std::vector<float> l(BLOK), r(BLOK), alles;
    for (uint32_t pos = 0; pos < RENDER; pos += BLOK) {
      Events ev;
      for (int aan = 0; aan < 2; ++aan) {
        const uint32_t t = aan ? 0 : RENDER / 2;
        if (t < pos || t >= pos + BLOK) continue;
        clap_event_note_t e{};
        e.header = {sizeof(e), t - pos, CLAP_CORE_EVENT_SPACE_ID, (uint16_t)(aan ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
        e.note_id = -1; e.port_index = 0; e.channel = 0; e.key = 57; e.velocity = 1.0;
        ev.n.push_back(e);
      }
      ev.bouw();
      clap_input_events_t in{&ev, evSize, evGet};
      clap_output_events_t uit{nullptr, evPush};
      float* kan[2] = {l.data(), r.data()};
      clap_audio_buffer_t ob{};
      ob.data32 = kan;
      ob.channel_count = 2;
      clap_process_t pr{};
      pr.steady_time = pos;
      pr.frames_count = BLOK;
      pr.audio_outputs = &ob;
      pr.audio_outputs_count = 1;
      pr.in_events = &in;
      pr.out_events = &uit;
      if (p->process(p, &pr) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
      alles.insert(alles.end(), l.begin(), l.end());
      alles.insert(alles.end(), r.begin(), r.end());
    }
    return alles;
  }
};

struct Param { uint32_t id; std::string naam; double min, max, standaard; };
std::vector<Param> params;

std::string tekst(const std::map<uint32_t, double>& w) {
  std::string t;
  char b[64];
  for (auto& kv : w) { std::snprintf(b, sizeof b, "%s%u=%.17g", t.empty() ? "" : " ", kv.first, kv.second); t += b; }
  return t;
}
bool gelijk(const std::map<uint32_t, double>& a, const std::map<uint32_t, double>& b) {
  if (a.size() != b.size()) return false;
  for (auto& kv : a) {
    auto it = b.find(kv.first);
    if (it == b.end() || std::memcmp(&it->second, &kv.second, sizeof(double))) return false;
  }
  return true;
}
bool bitGelijk(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && !std::memcmp(a.data(), b.data(), a.size() * sizeof(float));
}
bool hoorbaar(const std::vector<float>& a) {
  for (float x : a) if (std::fabs(x) > 1e-4f) return true;
  return false;
}
std::vector<float> referentie(const std::map<uint32_t, double>& w) {  // een verse instantie, waarden via flush
  Instantie ref;
  ref.zet(w);
  return ref.speel();
}

// waarden ver van de standaard en van elkaar, exact in float32 (zoals de grenzen en standaarden)
std::map<uint32_t, double> waardenSet(double t) {
  std::map<uint32_t, double> w;
  for (auto& p : params) w[p.id] = (double)(float)(p.min + t * (p.max - p.min));
  return w;
}
std::map<uint32_t, double> standaarden() {
  std::map<uint32_t, double> w;
  for (auto& p : params) w[p.id] = p.standaard;
  return w;
}

// --- state ---------------------------------------------------------------------------------------------------
void toetsState(bool actiefLaden) {
  const char* modus = actiefLaden ? "actief" : "niet actief";
  const size_t N = params.size();
  auto voor = waardenSet(0.9), A = waardenSet(0.3), C = waardenSet(0.6);
  std::printf("\n## state, laden terwijl %s\n", modus);

  {  // (a) aantal = N − 1: de laatste parameter ontbreekt en houdt zijn standaard
    std::vector<std::pair<uint32_t, double>> paren;
    auto verwacht = A;
    for (size_t k = 0; k + 1 < N; ++k) paren.push_back({params[k].id, A[params[k].id]});
    verwacht[params[N - 1].id] = params[N - 1].standaard;
    const auto b = blob("PBST", 1, (uint32_t)(N - 1), paren);
    Instantie s;
    s.zet(voor);
    if (actiefLaden) s.activeer();
    const int r0 = rescans;
    const bool ok = s.laad(b);
    check(ok, std::string("(a) blob zonder de laatste parameter (aantal = N-1) laadt [") + modus + "]");
    check(rescans == r0 + 1, "(a) na de load een rescan(VALUES) naar de host");
    const auto w = s.waarden();
    check(gelijk(w, verwacht), "(a) waarden " + tekst(w) + " = verwacht " + tekst(verwacht));
    check(w.at(params[N - 1].id) == params[N - 1].standaard,
          "(a) " + params[N - 1].naam + " (de laatste) houdt zijn standaard, niet de waarde van voor de load");
    const auto geluid = s.speel();
    check(hoorbaar(geluid) && bitGelijk(geluid, referentie(verwacht)),
          "(a) het geluid na de load is bit voor bit dat van de verwachte waarden");
    Instantie t;  // de state die hij nu bewaart, is weer volledig (versie 1, N parameters)
    check(t.laad(s.bewaar()) && gelijk(t.waarden(), verwacht), "(a) state.save daarna laadt in een nieuwe instantie tot dezelfde waarden");
  }

  {  // (b) versie 2 met een extra onbekend id (vooraan, in het midden en achteraan)
    std::vector<std::pair<uint32_t, double>> paren;
    paren.push_back({9999, 1.5});
    for (size_t k = 0; k < N; ++k) {
      paren.push_back({params[k].id, C[params[k].id]});
      if (k == N / 2) paren.push_back({77, -123.0});
    }
    paren.push_back({0xFFFFFFFFu, 42.0});
    const auto b = blob("PBST", 2, (uint32_t)paren.size(), paren);
    Instantie s;
    s.zet(voor);
    if (actiefLaden) s.activeer();
    const int r0 = rescans;
    check(s.laad(b), std::string("(b) blob met versie 2 en extra onbekende id's laadt [") + modus + "]");
    check(rescans == r0 + 1, "(b) na de load een rescan(VALUES) naar de host");
    const auto w = s.waarden();
    check(gelijk(w, C), "(b) waarden " + tekst(w) + " = verwacht " + tekst(C));
    const auto geluid = s.speel();
    check(hoorbaar(geluid) && bitGelijk(geluid, referentie(C)), "(b) het geluid na de load is bit voor bit dat van de verwachte waarden");
  }

  {  // (c) andere magic, versie 0, afgekapt: false en niets veranderd
    std::vector<std::pair<uint32_t, double>> alle;
    for (auto& p : params) alle.push_back({p.id, A[p.id]});
    const auto goed = blob("PBST", 1, (uint32_t)N, alle);
    struct Slecht { std::string naam; std::vector<unsigned char> b; };
    std::vector<Slecht> slecht = {
        {"andere magic \"PBSX\"", blob("PBSX", 1, (uint32_t)N, alle)},
        {"andere magic \"pbst\"", blob("pbst", 1, (uint32_t)N, alle)},
        {"versie 0", blob("PBST", 0, (uint32_t)N, alle)},
        {"lege blob", {}},
        {"afgekapt in de magic (3 bytes)", std::vector<unsigned char>(goed.begin(), goed.begin() + 3)},
        {"afgekapt in de kop (11 bytes)", std::vector<unsigned char>(goed.begin(), goed.begin() + 11)},
        {"afgekapt na de kop (12 bytes, aantal N)", std::vector<unsigned char>(goed.begin(), goed.begin() + 12)},
        {"afgekapt in de laatste waarde (1 byte te kort)", std::vector<unsigned char>(goed.begin(), goed.end() - 1)},
        {"aantal belooft er een meer dan er staat", blob("PBST", 1, (uint32_t)N + 1, alle)},
        {"aantal 0xFFFFFFFF", blob("PBST", 1, 0xFFFFFFFFu, alle)},
    };
    for (auto& g : slecht) {
      Instantie s;
      s.zet(voor);
      if (actiefLaden) s.activeer();
      const auto w0 = s.waarden();
      const auto b0 = s.bewaar();
      const int r0 = rescans, f0 = flushVragen;
      const bool ok = s.laad(g.b);
      check(!ok, "(c) " + g.naam + " geeft false [" + modus + "]");
      check(gelijk(s.waarden(), w0) && s.bewaar() == b0 && rescans == r0 && flushVragen == f0,
            "(c) " + g.naam + ": get_value, state.save en de host ongewijzigd");
      const auto geluid = s.speel();
      check(hoorbaar(geluid) && bitGelijk(geluid, referentie(voor)), "(c) " + g.naam + ": het geluid is dat van de waarden van voor de load");
    }
    Instantie s;  // de goede blob werkt wel (anders bewijzen de gevallen hierboven niets)
    s.zet(voor);
    if (actiefLaden) s.activeer();
    check(s.laad(goed) && gelijk(s.waarden(), A), "(c) controle: dezelfde blob heel en met de goede kop laadt wel");
  }
}

// --- presets -------------------------------------------------------------------------------------------------
struct VerwachtePreset { std::string naam; std::map<std::string, double> waarden; };
std::map<std::string, VerwachtePreset> leesVerwacht(const char* bestand) {  // sleutel → naam en waarden
  std::ifstream f(bestand);
  if (!f) stop(std::string("kan ") + bestand + " niet lezen");
  std::map<std::string, VerwachtePreset> v;
  std::string regel;
  while (std::getline(f, regel)) {
    if (regel.empty()) continue;
    std::vector<std::string> d;
    std::stringstream ss(regel);
    std::string veld;
    while (std::getline(ss, veld, '\t')) d.push_back(veld);
    while (d.size() < 4) d.push_back("");
    auto& p = v[d[0]];
    p.naam = d[1];
    if (!d[2].empty()) p.waarden[d[2]] = std::strtod(d[3].c_str(), nullptr);
  }
  if (v.empty()) stop(std::string(bestand) + " bevat geen presets");
  return v;
}

// wat een provider tijdens init en get_metadata meldt
struct Gemeld {
  std::vector<clap_preset_discovery_location_t> locaties;
  std::vector<std::string> locatieNamen;
  int bestandstypen = 0, soundpacks = 0;
  struct P { std::string naam, sleutel; std::vector<std::pair<std::string, std::string>> ids; uint32_t vlaggen = 0; bool vlaggenGezet = false; };
  std::vector<P> presets;
};
Gemeld* gemeld(const clap_preset_discovery_indexer_t* i) { return static_cast<Gemeld*>(i->indexer_data); }
Gemeld* gemeld(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Gemeld*>(r->receiver_data); }

void toetsPresets(const clap_plugin_entry_t* entry, const char* bestand) {
  std::printf("\n## presets (preset-discovery en preset-load)\n");
  const auto verwacht = leesVerwacht(bestand);
  auto* pf = static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  auto* pfCompat = static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT));
  check(pf != nullptr, "de plugin heeft een preset-discovery-factory");
  check(pfCompat == pf, std::string("ook onder het oude id (") + CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT + ")");
  if (!pf) return;
  const uint32_t n = pf->count(pf);
  check(n == verwacht.size(), "aantal providers " + std::to_string(n) + " = aantal presets in presets/ " + std::to_string(verwacht.size()));

  std::map<std::string, std::string> gevonden;  // sleutel → naam
  std::set<std::string> providerIds;
  for (uint32_t i = 0; i < n; ++i) {
    const auto* d = pf->get_descriptor(pf, i);
    check(d && d->id && d->name && *d->name && clap_version_is_compatible(d->clap_version),
          "provider " + std::to_string(i) + ": descriptor met id, naam en een geldige CLAP-versie");
    if (!d || !d->id) continue;
    check(providerIds.insert(d->id).second, std::string("provider-id ") + d->id + " is uniek");
    Gemeld g;
    const clap_preset_discovery_indexer_t ix = {
        CLAP_VERSION_INIT, "pb-statetoets", "Pluginbouwer", "", "1", &g,
        [](const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_filetype_t*) { ++gemeld(i)->bestandstypen; return true; },
        [](const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
          gemeld(i)->locaties.push_back(*l);
          gemeld(i)->locatieNamen.push_back(l->name ? l->name : "");
          return true;
        },
        [](const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_soundpack_t*) { ++gemeld(i)->soundpacks; return true; },
        [](const clap_preset_discovery_indexer_t*, const char*) -> const void* { return nullptr; }};
    const auto* pr = pf->create(pf, &ix, d->id);
    check(pr && pr->desc == d, std::string(d->id) + ": create geeft een provider met dezelfde descriptor");
    if (!pr) continue;
    const bool init = pr->init(pr);
    check(init, std::string(d->id) + ": init");
    const bool eenLocatie = g.locaties.size() == 1 && g.locaties[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
                            g.locaties[0].location == nullptr && !g.locatieNamen[0].empty() &&
                            (g.locaties[0].flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    check(eenLocatie, std::string(d->id) + ": precies één locatie, soort PLUGIN, zonder pad, met een naam, fabriekspresets");
    check(g.bestandstypen == 0, std::string(d->id) + ": geen bestandstypen (de presets zitten in het binary)");
    const clap_preset_discovery_metadata_receiver_t r = {
        &g,
        [](const clap_preset_discovery_metadata_receiver_t*, int32_t, const char* m) { std::printf("     (on_error: %s)\n", m ? m : ""); },
        [](const clap_preset_discovery_metadata_receiver_t* r, const char* naam, const char* sleutel) {
          Gemeld::P p;
          p.naam = naam ? naam : "";
          p.sleutel = sleutel ? sleutel : "";
          gemeld(r)->presets.push_back(p);
          return true;
        },
        [](const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
          if (!gemeld(r)->presets.empty()) gemeld(r)->presets.back().ids.push_back({id->abi ? id->abi : "", id->id ? id->id : ""});
        },
        [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
        [](const clap_preset_discovery_metadata_receiver_t* r, uint32_t f) {
          if (!gemeld(r)->presets.empty()) { gemeld(r)->presets.back().vlaggen = f; gemeld(r)->presets.back().vlaggenGezet = true; }
        },
        [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
        [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
        [](const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {},
        [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
        [](const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}};
    const bool meta = init && pr->get_metadata(pr, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &r);
    check(meta, std::string(d->id) + ": get_metadata op de eigen locatie");
    const bool eenPreset = g.presets.size() == 1 && !g.presets[0].sleutel.empty() && !g.presets[0].naam.empty() &&
                           g.presets[0].ids.size() == 1 && g.presets[0].ids[0].first == "clap" &&
                           g.presets[0].ids[0].second == pluginId;
    check(eenPreset, std::string(d->id) + ": een container van precies één preset (valkuil 15), met naam, load key en plugin-id clap/" + pluginId);
    if (eenPreset) {
      check(!g.presets[0].vlaggenGezet || (g.presets[0].vlaggen & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT),
            std::string(d->id) + ": de preset is een fabriekspreset");
      check(gevonden.insert({g.presets[0].sleutel, g.presets[0].naam}).second, "load key " + g.presets[0].sleutel + " is uniek");
    }
    Gemeld leeg;  // een vreemde locatie: geen presets
    const clap_preset_discovery_metadata_receiver_t r2 = {&leeg, r.on_error, r.begin_preset, r.add_plugin_id, r.set_soundpack_id,
                                                          r.set_flags, r.add_creator, r.set_description, r.set_timestamps,
                                                          r.add_feature, r.add_extra_info};
    pr->get_metadata(pr, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/bestaat-niet.json", &r2);
    check(leeg.presets.empty(), std::string(d->id) + ": een bestandslocatie geeft geen presets");
    pr->destroy(pr);
  }
  for (auto& kv : verwacht) {
    auto it = gevonden.find(kv.first);
    check(it != gevonden.end() && it->second == kv.second.naam,
          "presets/" + kv.first + ".json staat in de discovery als \"" + kv.second.naam + "\" (load key " + kv.first + ")");
  }

  // laden: elke preset, terwijl niet actief en actief
  for (int actiefLaden = 0; actiefLaden < 2; ++actiefLaden) {
    const char* modus = actiefLaden ? "actief" : "niet actief";
    const auto voor = waardenSet(0.9);
    for (auto& kv : verwacht) {
      std::map<uint32_t, double> w = standaarden();
      bool namenOk = true;
      for (auto& pw : kv.second.waarden) {
        bool bestaat = false;
        for (auto& p : params)
          if (p.naam == pw.first) { w[p.id] = pw.second; bestaat = true; }
        namenOk = namenOk && bestaat;
      }
      check(namenOk, "presets/" + kv.first + ".json: alle parameternamen bestaan");
      Instantie s;
      auto* pl = static_cast<const clap_plugin_preset_load_t*>(s.p->get_extension(s.p, CLAP_EXT_PRESET_LOAD));
      check(pl && pl->from_location, "de plugin heeft de extensie preset-load");
      if (!pl) return;
      s.zet(voor);
      if (actiefLaden) s.activeer();
      const int r0 = rescans, g0 = geladen, f0 = laadFouten;
      laatsteSleutel.clear();
      const bool ok = pl->from_location(s.p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, kv.first.c_str());
      const std::string wie = "preset " + kv.first + " (" + kv.second.naam + ") [" + modus + "]";
      check(ok, wie + ": from_location geeft true");
      check(geladen == g0 + 1 && laatsteSleutel == kv.first && laadFouten == f0, wie + ": de host krijgt loaded met dezelfde locatie en load key");
      check(rescans == r0 + 1, wie + ": een rescan(VALUES) naar de host");
      const auto echt = s.waarden();
      check(gelijk(echt, w), wie + ": waarden " + tekst(echt) + " = de JSON (rest standaard) " + tekst(w));
      const auto geluid = s.speel();
      check(hoorbaar(geluid) && bitGelijk(geluid, referentie(w)), wie + ": het geluid is bit voor bit dat van die waarden");
      Instantie t;
      check(t.laad(s.bewaar()) && gelijk(t.waarden(), w), wie + ": state.save daarna laadt in een nieuwe instantie tot dezelfde waarden");
    }
    Instantie s;  // een onbekende load key
    auto* pl = static_cast<const clap_plugin_preset_load_t*>(s.p->get_extension(s.p, CLAP_EXT_PRESET_LOAD));
    s.zet(voor);
    if (actiefLaden) s.activeer();
    const auto w0 = s.waarden();
    const int r0 = rescans, g0 = geladen, f0 = laadFouten;
    const bool ok = pl->from_location(s.p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "bestaat-niet");
    check(!ok && laadFouten == f0 + 1 && geladen == g0 && rescans == r0 && gelijk(s.waarden(), w0),
          std::string("onbekende load key: false, on_error, geen loaded of rescan, waarden ongewijzigd [") + modus + "]");
    const bool ok2 = pl->from_location(s.p, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/zacht.json", verwacht.begin()->first.c_str());
    check(!ok2 && gelijk(s.waarden(), w0), std::string("een bestandslocatie laadt niets [") + modus + "]");
  }
}

// --- tekst ↔ waarde ------------------------------------------------------------------------------------------
void toetsTekst() {
  std::printf("\n## tekst <-> waarde (SPEC.md)\n");
  Instantie s;
  const clap_plugin_params_t* par = s.par;
  // de kolom "standaard als tekst" van de tabel
  const std::map<std::string, std::string> tabel = {{"Noot", "100 %"}, {"Poort", "0 %"}, {"Filter", "900 Hz"}, {"Loslaten", "450 ms"}};
  for (auto& p : params) {
    auto it = tabel.find(p.naam);
    check(it != tabel.end(), "parameter " + p.naam + " staat in de tabel van SPEC.md");
    if (it == tabel.end()) continue;
    char t[256];
    const bool ok = par->value_to_text(s.p, p.id, p.standaard, t, sizeof t);
    check(ok && it->second == t, "value_to_text(" + p.naam + ", standaard) = \"" + (ok ? t : "?") + "\", tabel: \"" + it->second + "\"");
    double v = NAN;
    const bool ok2 = par->text_to_value(s.p, p.id, it->second.c_str(), &v);
    char vb[64];
    std::snprintf(vb, sizeof vb, "%.17g", v);
    check(ok2 && v == p.standaard, "text_to_value(" + p.naam + ", \"" + it->second + "\") = " + vb + " = de standaard");
  }
  auto idVan = [&](const char* naam) {
    for (auto& p : params) if (p.naam == naam) return p.id;
    stop(std::string("geen parameter ") + naam);
  };
  // de voorbeelden uit de kolom "regel"
  struct Naar { const char* naam; double v; const char* t; };
  const Naar naar[] = {
      {"Noot", 0.38, "38 %"}, {"Noot", 1.25, "125 %"}, {"Poort", 0.38, "38 %"}, {"Poort", 1.0, "100 %"},
      {"Filter", 300, "300 Hz"}, {"Filter", 999, "999 Hz"}, {"Filter", 1000, "1,00 kHz"}, {"Filter", 3000, "3,00 kHz"},
      {"Filter", 12000, "12,0 kHz"}, {"Filter", 8000, "8,00 kHz"},
      {"Loslaten", 20, "20,0 ms"}, {"Loslaten", 99.5, "99,5 ms"}, {"Loslaten", 100, "100 ms"}, {"Loslaten", 375, "375 ms"},
  };
  for (auto& g : naar) {
    char t[256];
    const bool ok = par->value_to_text(s.p, idVan(g.naam), g.v, t, sizeof t);
    char vb[64];
    std::snprintf(vb, sizeof vb, "%g", g.v);
    check(ok && !std::strcmp(t, g.t), std::string("value_to_text(") + g.naam + ", " + vb + ") = \"" + (ok ? t : "?") + "\", verwacht \"" + g.t + "\"");
  }
  // leesregels: komma én punt, met of zonder eenheid, "k" voor kHz, procenten; buiten bereik klemmen; onleesbaar false
  struct Van { const char* naam; const char* t; bool ok; double v; };
  const Van van[] = {
      {"Loslaten", "375", true, 375}, {"Loslaten", "375 ms", true, 375}, {"Loslaten", "375ms", true, 375},
      {"Loslaten", "20,5 ms", true, 20.5}, {"Loslaten", "20.5", true, 20.5}, {"Loslaten", "1", true, 10},
      {"Loslaten", "99999 ms", true, 4000}, {"Loslaten", "-5", true, 10},
      {"Filter", "3k", true, 3000}, {"Filter", "3,5 kHz", true, 3500}, {"Filter", "3.5kHz", true, 3500},
      {"Filter", "1,25 k", true, 1250}, {"Filter", "440 Hz", true, 440}, {"Filter", "440", true, 440},
      {"Filter", "12,0 kHz", true, 8000}, {"Filter", "5", true, 100},
      {"Noot", "38", true, 0.38}, {"Noot", "38 %", true, 0.38}, {"Noot", "125 %", true, 1.25}, {"Noot", "0,5 %", true, 0.25},
      {"Poort", "38 %", true, 0.38}, {"Poort", "38,5", true, 0.385}, {"Poort", "250 %", true, 1.0},
      {"Filter", "", false, 0}, {"Filter", "abc", false, 0}, {"Filter", "Hz", false, 0}, {"Filter", "300 ms", false, 0},
      {"Loslaten", "375 Hz", false, 0}, {"Noot", "38 Hz", false, 0}, {"Poort", ",", false, 0},
  };
  for (auto& g : van) {
    double v = NAN;
    const bool ok = par->text_to_value(s.p, idVan(g.naam), g.t, &v);
    char vb[64];
    std::snprintf(vb, sizeof vb, "%.17g", v);
    if (g.ok) check(ok && v == g.v, std::string("text_to_value(") + g.naam + ", \"" + g.t + "\") = " + (ok ? vb : "false"));
    else check(!ok, std::string("text_to_value(") + g.naam + ", \"" + g.t + "\") = false (onleesbaar)");
  }
  // rondje: elke tekst die value_to_text maakt, leest text_to_value terug tot dezelfde tekst
  for (auto& p : params) {
    bool alles = true;
    for (int k = 0; k <= 200; ++k) {
      const double v = p.min + (p.max - p.min) * k / 200.0;
      char t[256], t2[256];
      double w = NAN;
      if (!par->value_to_text(s.p, p.id, v, t, sizeof t) || !par->text_to_value(s.p, p.id, t, &w) ||
          !par->value_to_text(s.p, p.id, w, t2, sizeof t2) || std::strcmp(t, t2)) {
        std::printf("     %s: %.17g -> \"%s\" -> %.17g -> \"%s\"\n", p.naam.c_str(), v, t, w, t2);
        alles = false;
      }
    }
    check(alles, "rondje tekst -> waarde -> tekst over het hele bereik van " + p.naam);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && !(argc == 4 && !std::strcmp(argv[2], "--presets")))
    stop("gebruik: pb-statetoets <plugin.clap> [--presets <verwacht.tsv>]");
  std::string pad = argv[1];
#ifdef __APPLE__
  // een .clap is op macOS een bundel
  if (pad.size() > 5 && pad.compare(pad.size() - 5, 5, ".clap") == 0) {
    const auto schuin = pad.find_last_of('/');
    std::string naam = pad.substr(schuin == std::string::npos ? 0 : schuin + 1);
    naam = naam.substr(0, naam.size() - 5);
    pad += "/Contents/MacOS/" + naam;
  }
#endif
  void* lib = dlopen(pad.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) stop(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!entry || !entry->init(argv[1])) stop("geen clap_entry of init mislukt");
  fabriek = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fabriek || fabriek->get_plugin_count(fabriek) < 1) stop("geen plugin-factory");
  pluginId = fabriek->get_plugin_descriptor(fabriek, 0)->id;
  {
    Instantie s;
    for (uint32_t i = 0; i < s.par->count(s.p); ++i) {
      clap_param_info_t info;
      s.par->get_info(s.p, i, &info);
      params.push_back({info.id, info.name, info.min_value, info.max_value, info.default_value});
    }
    check(gelijk(s.waarden(), standaarden()), "een verse instantie staat op de standaarden");
  }
  toetsState(false);
  toetsState(true);
  toetsTekst();
  if (argc == 4) toetsPresets(entry, argv[3]);
  entry->deinit();
  std::printf("\n%d controles, %d fout\n", controles, fouten);
  return fouten ? 1 : 0;
}
