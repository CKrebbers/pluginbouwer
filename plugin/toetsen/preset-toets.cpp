// preset-toets — de eigen toets van fase 4 (fabriekspresets), als kleine CLAP-host (zoals param-toets).
//
//   preset-toets <plugin.clap> <verwacht.tsv>
//
// <verwacht.tsv> komt uit `node gereedschap/presets-plat.mjs presets` (de JSON zelf, niet de tabel die CMake maakte).
// Eist:
//   - preset-discovery (ook onder het oude draft-id): per preset één provider met één locatie van soort PLUGIN en de
//     vlag FACTORY_CONTENT; samen geven ze precies de presets uit verwacht.tsv, in die volgorde, met naam, load_key
//     (bestandsnaam), uitleg en het plugin-id; een andere locatie geeft false;
//   - preset-load: elke preset laadt (de host hoort `loaded`), en daarna geeft get_value voor elke parameter de waarde
//     uit de JSON, of de standaard als de preset hem niet noemt; een onbekende load_key of locatie geeft false (en
//     on_error);
//   - geluid: een preset geladen terwijl de plugin draait (tussen twee process-aanroepen), of vóór activate, klinkt
//     bit voor bit als dezelfde waarden als host-events op sample 0.
// Exitcode 0 = geslaagd; elke fout staat in de uitvoer met "FOUT". Met -fsanitize=realtime loopt process in een
// real-time context: het overnemen van een preset op de audiothread mag niet alloceren of wachten.
#include <clap/clap.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
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
std::string str(const char* c) { return c ? c : "(null)"; }

// --- host ------------------------------------------------------------------------------------------------------
struct HostLog {
  std::vector<std::string> geladen;  // load_keys van clap_host_preset_load::loaded
  int fouten = 0;                    // clap_host_preset_load::on_error
  int rescans = 0;
} g_host;
void hpFout(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char*) { ++g_host.fouten; }
void hpGeladen(const clap_host_t*, uint32_t, const char*, const char* k) { g_host.geladen.push_back(str(k)); }
const clap_host_preset_load_t HOST_PRESET_LOAD = {hpFout, hpGeladen};
void hpRescan(const clap_host_t*, clap_param_rescan_flags) { ++g_host.rescans; }
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpFlush(const clap_host_t*) {}
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return &HOST_PRESET_LOAD;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "preset-toets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

struct Events { std::vector<clap_event_param_value_t> ev; };
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<const Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<const Events*>(l->ctx)->ev[i].header;
}
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }
clap_event_param_value_t maakEvent(clap_id id, double v) {
  clap_event_param_value_t e{};
  e.header = {sizeof(e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.param_id = id; e.cookie = nullptr; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
  return e;
}

// noinline + nonblocking: zo instrumenteert RTSan deze aanroep als real-time context (zie pb-render.cpp)
__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT {
  return pl->process(pl, pr);
}

// --- preset-discovery: indexer en metadata-ontvanger -------------------------------------------------------------
struct Locatie { uint32_t flags; std::string naam; uint32_t soort; bool nullLocatie; };
struct Indexer { std::vector<Locatie> locaties; int bestandstypen = 0; };
bool ixType(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_filetype_t*) {
  ++static_cast<Indexer*>(i->indexer_data)->bestandstypen;
  return true;
}
bool ixLocatie(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
  static_cast<Indexer*>(i->indexer_data)->locaties.push_back({l->flags, str(l->name), l->kind, l->location == nullptr});
  return true;
}
bool ixPakket(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; }
const void* ixExt(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }

struct Gevonden {
  std::string naam, sleutel, uitleg;
  std::vector<std::string> pluginIds;
  uint32_t flags = 0;
  bool flagsGezet = false;
};
struct Ontvanger { std::vector<Gevonden> presets; int fouten = 0; };
Ontvanger* van(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Ontvanger*>(r->receiver_data); }
Gevonden* laatste(const clap_preset_discovery_metadata_receiver_t* r) {
  auto* o = van(r);
  if (o->presets.empty()) { ++o->fouten; return nullptr; }  // een veld zonder begin_preset
  return &o->presets.back();
}
void rvFout(const clap_preset_discovery_metadata_receiver_t* r, int32_t, const char*) { ++van(r)->fouten; }
bool rvBegin(const clap_preset_discovery_metadata_receiver_t* r, const char* naam, const char* sleutel) {
  Gevonden g;
  g.naam = str(naam);
  g.sleutel = str(sleutel);
  van(r)->presets.push_back(g);
  return true;
}
void rvPlugin(const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
  if (auto* g = laatste(r)) g->pluginIds.push_back(str(id->abi) + ":" + str(id->id));
}
void rvPakket(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rvVlaggen(const clap_preset_discovery_metadata_receiver_t* r, uint32_t f) {
  if (auto* g = laatste(r)) { g->flags = f; g->flagsGezet = true; }
}
void rvMaker(const clap_preset_discovery_metadata_receiver_t* r, const char*) { laatste(r); }
void rvUitleg(const clap_preset_discovery_metadata_receiver_t* r, const char* t) {
  if (auto* g = laatste(r)) g->uitleg = str(t);
}
void rvTijden(const clap_preset_discovery_metadata_receiver_t* r, clap_timestamp, clap_timestamp) { laatste(r); }
void rvKenmerk(const clap_preset_discovery_metadata_receiver_t* r, const char*) { laatste(r); }
void rvExtra(const clap_preset_discovery_metadata_receiver_t* r, const char*, const char*) { laatste(r); }

// --- de plugin ---------------------------------------------------------------------------------------------------
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

struct Param { clap_id id; std::string naam; double standaard; };

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_preset_load_t* pr = nullptr;
  bool actief = false;
  explicit Instantie(const Bibliotheek& b) {
    pl = b.fab->create_plugin(b.fab, &HOST, b.id.c_str());
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    pr = static_cast<const clap_plugin_preset_load_t*>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
    if (!pp) stop("geen params-extensie");
    if (!pr) stop("geen preset-load-extensie (" + std::string(CLAP_EXT_PRESET_LOAD) + ")");
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
      r.push_back({info.id, info.name, info.default_value});
    }
    return r;
  }
  bool laad(const std::string& sleutel) {
    return pr->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, sleutel.c_str());
  }
  void activeer(uint32_t maxBlok) {
    if (!pl->activate(pl, 48000, 1, maxBlok)) stop("activate mislukt");
    if (!pl->start_processing(pl)) stop("start_processing mislukt");
    actief = true;
  }
};

struct Verwacht {
  std::string sleutel, naam, uitleg;
  std::map<std::string, double> waarden;
};
std::vector<Verwacht> leesVerwacht(const char* pad) {
  std::ifstream f(pad);
  if (!f) stop(std::string("kan ") + pad + " niet lezen");
  std::vector<Verwacht> r;
  std::string regel;
  while (std::getline(f, regel)) {
    if (regel.empty()) continue;
    std::vector<std::string> v;
    std::stringstream ss(regel);
    std::string veld;
    while (std::getline(ss, veld, '\t')) v.push_back(veld);
    if (v.size() < 3) stop("onleesbare regel in " + std::string(pad) + ": " + regel);
    Verwacht p{v[0], v[1], v[2], {}};
    for (size_t k = 3; k < v.size(); ++k) {
      const size_t is = v[k].find('=');
      if (is == std::string::npos) stop("onleesbaar veld: " + v[k]);
      p.waarden[v[k].substr(0, is)] = std::strtod(v[k].c_str() + is + 1, nullptr);
    }
    r.push_back(p);
  }
  if (r.empty()) stop(std::string("geen presets in ") + pad);
  return r;
}

// --- audio ---------------------------------------------------------------------------------------------------------
constexpr uint32_t BLOK = 512, BLOKKEN = 200;  // ruim 2 s op 48 kHz
float monster(uint32_t& r) {
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  return (float)(((double)(r >> 8) / 8388608.0) - 1.0) * 0.25f;
}
// Rendert ruisstoten. `vooraf`: host-events op sample 0 van het eerste blok. `presetNa`: na zoveel blokken laadt de
// host de preset (main thread, de plugin draait), daarna gaat het renderen door. Geeft beide kanalen achter elkaar.
std::vector<float> render(Instantie& ins, const std::vector<clap_event_param_value_t>& vooraf, int presetNa,
                          const std::string& sleutel) {
  if (!ins.actief) ins.activeer(BLOK);
  std::vector<float> in0(BLOK), in1(BLOK), u0(BLOK), u1(BLOK), uit;
  float* pi[2] = {in0.data(), in1.data()};
  float* pu[2] = {u0.data(), u1.data()};
  uint32_t r = 4242;
  for (uint32_t b = 0; b < BLOKKEN; ++b) {
    if ((int)b == presetNa && !ins.laad(sleutel)) fout("preset '" + sleutel + "' laadt niet terwijl de plugin draait");
    for (uint32_t i = 0; i < BLOK; ++i) {
      const bool stoot = (b % 40) < 3;
      in0[i] = stoot ? monster(r) : 0.0f;
      in1[i] = stoot ? 0.5f * monster(r) : 0.0f;
    }
    Events ev;
    if (b == 0) ev.ev = vooraf;
    clap_input_events_t inEv = {&ev, evSize, evGet};
    clap_output_events_t uitEv = {nullptr, evPush};
    clap_audio_buffer_t ib{}, ub{};
    ib.data32 = pi; ib.channel_count = 2;
    ub.data32 = pu; ub.channel_count = 2;
    clap_process_t p{};
    p.steady_time = (int64_t)b * BLOK;
    p.frames_count = BLOK;
    p.audio_inputs = &ib; p.audio_inputs_count = 1;
    p.audio_outputs = &ub; p.audio_outputs_count = 1;
    p.in_events = &inEv; p.out_events = &uitEv;
    if (doeProcess(ins.pl, &p) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
    uit.insert(uit.end(), u0.begin(), u0.end());
    uit.insert(uit.end(), u1.begin(), u1.end());
  }
  return uit;
}
size_t verschil(const std::vector<float>& a, const std::vector<float>& b) {
  size_t n = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) n += std::memcmp(&a[i], &b[i], 4) != 0;
  return n + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}

int toets(const char* pad, const char* verwachtPad) {
  const std::vector<Verwacht> verwacht = leesVerwacht(verwachtPad);
  Bibliotheek bib(pad);

  // 1. preset-discovery
  auto* pd = static_cast<const clap_preset_discovery_factory_t*>(bib.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  if (!pd) stop(std::string("geen ") + CLAP_PRESET_DISCOVERY_FACTORY_ID);
  if (bib.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) != pd)
    fout(std::string("de factory ontbreekt onder het draft-id ") + CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT);
  // Per preset één provider, met één locatie van soort PLUGIN en daarin precies die preset (zie dub_echo.cpp: één
  // provider met een container van alle presets laat clap-validator 0.4.1 vastlopen).
  const uint32_t nProv = pd->count(pd);
  if (nProv != verwacht.size())
    fout("verwacht " + std::to_string(verwacht.size()) + " providers (één per preset), kreeg " + std::to_string(nProv));
  if (pd->get_descriptor(pd, nProv)) fout("get_descriptor(count) hoort nullptr te zijn");
  if (pd->create(pd, nullptr, "bestaat.niet")) fout("create met een onbekend provider-id gaf iets terug");
  std::vector<Gevonden> gevonden;
  std::map<std::string, int> ids;
  for (uint32_t k = 0; k < nProv; ++k) {
    const auto* desc = pd->get_descriptor(pd, k);
    if (!desc || !desc->id || !desc->name) { fout("provider " + std::to_string(k) + " zonder descriptor"); continue; }
    if (ids[desc->id]++) fout("provider-id dubbel: " + str(desc->id));
    Indexer ix;
    const clap_preset_discovery_indexer_t indexer = {CLAP_VERSION_INIT, "preset-toets", "Pluginbouwer", "", "0.1", &ix,
                                                     ixType, ixLocatie, ixPakket, ixExt};
    const auto* prov = pd->create(pd, &indexer, desc->id);
    if (!prov) { fout("create van provider " + str(desc->id) + " mislukt"); continue; }
    if (prov->desc != desc) fout("provider->desc is niet de descriptor van de factory");
    if (!prov->init(prov)) fout("provider->init gaf false voor " + str(desc->id));
    if (ix.locaties.size() != 1) fout(str(desc->id) + ": verwacht 1 locatie, kreeg " + std::to_string(ix.locaties.size()));
    for (const Locatie& loc : ix.locaties) {
      if (loc.soort != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || !loc.nullLocatie)
        fout(str(desc->id) + ": de locatie is niet van soort PLUGIN met location = nullptr");
      if (!(loc.flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT)) fout(str(desc->id) + ": geen vlag FACTORY_CONTENT");
    }
    Ontvanger ov;
    clap_preset_discovery_metadata_receiver_t rv = {&ov, rvFout, rvBegin, rvPlugin, rvPakket, rvVlaggen,
                                                    rvMaker, rvUitleg, rvTijden, rvKenmerk, rvExtra};
    if (prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/niets.json", &rv) || !ov.presets.empty())
      fout(str(desc->id) + ": get_metadata voor een bestand gaf presets of true");
    ov.presets.clear();
    if (!prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &rv)) fout(str(desc->id) + ": get_metadata gaf false");
    if (ov.fouten) fout(str(desc->id) + ": velden zonder begin_preset, of on_error");
    if (ov.presets.size() != 1) fout(str(desc->id) + ": verwacht 1 preset, kreeg " + std::to_string(ov.presets.size()));
    for (auto& g : ov.presets) gevonden.push_back(g);
    std::printf("  provider %u: %s (%s)\n", k, str(desc->name).c_str(), str(desc->id).c_str());
    prov->destroy(prov);
  }
  if (gevonden.size() != verwacht.size())
    fout("verwacht " + std::to_string(verwacht.size()) + " presets, kreeg " + std::to_string(gevonden.size()));
  for (size_t k = 0; k < gevonden.size() && k < verwacht.size(); ++k) {
    const Gevonden& g = gevonden[k];
    const Verwacht& v = verwacht[k];
    const std::string wie = "preset " + std::to_string(k) + " ('" + v.sleutel + "')";
    if (g.sleutel != v.sleutel) fout(wie + ": load_key '" + g.sleutel + "'");
    if (g.naam != v.naam) fout(wie + ": naam '" + g.naam + "', verwacht '" + v.naam + "'");
    if (g.uitleg != v.uitleg) fout(wie + ": uitleg '" + g.uitleg + "', verwacht '" + v.uitleg + "'");
    if (g.pluginIds.size() != 1 || g.pluginIds[0] != "clap:" + bib.id) fout(wie + ": plugin-id klopt niet");
    if (!g.flagsGezet || !(g.flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT)) fout(wie + ": niet als FACTORY_CONTENT");
  }
  if (!g_fouten)
    ok("preset-discovery: " + std::to_string(gevonden.size()) + " fabriekspresets (elk een provider met een locatie in de "
       "plugin), naam/uitleg/load_key/plugin-id zoals in presets/*.json, in die volgorde");

  // 2. preset-load: elke parameter zoals de JSON, de rest op zijn standaard
  {
    Instantie ins(bib);
    const std::vector<Param> ps = ins.params();
    for (const Verwacht& v : verwacht) {
      for (auto& [naam, w] : v.waarden) {
        bool bekend = false;
        for (const Param& p : ps) bekend = bekend || p.naam == naam;
        if (!bekend) fout("presets/" + v.sleutel + ".json: onbekende parameter '" + naam + "'");
      }
      g_host.geladen.clear();
      const int fouten = g_fouten;
      if (!ins.laad(v.sleutel)) { fout("from_location('" + v.sleutel + "') gaf false"); continue; }
      if (g_host.geladen.size() != 1 || g_host.geladen[0] != v.sleutel) fout(v.sleutel + ": host hoorde geen 'loaded'");
      for (const Param& p : ps) {
        auto it = v.waarden.find(p.naam);
        const double moet = it != v.waarden.end() ? it->second : p.standaard;
        double is = 0;
        if (!ins.pp->get_value(ins.pl, p.id, &is) || is != moet) {
          char t[160];
          std::snprintf(t, sizeof t, "%s: %s = %.17g, verwacht %.17g", v.sleutel.c_str(), p.naam.c_str(), is, moet);
          fout(t);
        }
      }
      if (g_fouten == fouten) ok("preset '" + v.naam + "' laadt: alle " + std::to_string(ps.size()) + " parameters exact");
    }
    const int voor = g_host.fouten;
    if (ins.laad("bestaat-niet")) fout("from_location met een onbekende load_key gaf true");
    if (ins.pr->from_location(ins.pl, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/standaard.json", "standaard"))
      fout("from_location met een bestandslocatie gaf true");
    if (g_host.fouten != voor + 2) fout("een mislukte preset meldde geen on_error bij de host");
    else ok("onbekende load_key of locatie: false en on_error");
  }

  // 3. geluid: preset (vóór activate of terwijl hij draait) = dezelfde waarden als host-events op sample 0
  for (const Verwacht& v : verwacht) {
    std::vector<clap_event_param_value_t> ev;
    {
      Instantie tmp(bib);
      for (const Param& p : tmp.params()) {
        auto it = v.waarden.find(p.naam);
        ev.push_back(maakEvent(p.id, it != v.waarden.end() ? it->second : p.standaard));
      }
    }
    std::vector<float> ref, voorAct, tijdens;
    { Instantie a(bib); ref = render(a, ev, -1, ""); }
    { Instantie b(bib); if (!b.laad(v.sleutel)) fout("laden mislukt"); voorAct = render(b, {}, -1, ""); }
    { Instantie c(bib); tijdens = render(c, {}, 0, v.sleutel); }  // geladen na activate, vóór de eerste process
    const size_t d1 = verschil(ref, voorAct), d2 = verschil(ref, tijdens);
    if (d1) fout(v.sleutel + ": preset vóór activate klinkt anders dan host-events (" + std::to_string(d1) + " samples)");
    if (d2) fout(v.sleutel + ": preset terwijl hij draait klinkt anders dan host-events (" + std::to_string(d2) + " samples)");
    if (!d1 && !d2)
      ok("geluid '" + v.naam + "': preset = host-events, bit voor bit (" + std::to_string(ref.size()) + " samples, 0 verschillend)");
  }
  // Midden in het renderen een andere preset: de overgang moet er zijn (anders werd hij niet overgenomen)
  if (verwacht.size() >= 2) {
    Instantie a(bib), b(bib);
    const std::vector<float> zonder = render(a, {}, -1, "");
    const std::vector<float> met = render(b, {}, BLOKKEN / 2, verwacht[1].sleutel);
    size_t eerste = 0;
    while (eerste < zonder.size() && !std::memcmp(&zonder[eerste], &met[eerste], 4)) ++eerste;
    if (eerste < (size_t)BLOKKEN / 2 * 2 * BLOK || eerste == zonder.size())
      fout("een preset midden in het renderen werd niet (of te vroeg) overgenomen");
    else ok("preset midden in het renderen: gelijk tot blok " + std::to_string(BLOKKEN / 2) + ", daarna anders");
  }
  return g_fouten ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "gebruik: preset-toets <plugin.clap> <verwacht.tsv>\n");
    return 2;
  }
  const int r = toets(argv[1], argv[2]);
  std::printf(r == 0 ? "GESLAAGD\n" : "MISLUKT (%d fouten)\n", g_fouten);
  return r;
}
