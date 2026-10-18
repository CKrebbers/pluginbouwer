// presettoets — de eigen toets van fase 4, door het gebouwde .clap (zoals pb-statetoets: dlopen, geen code van de
// plugin). De verwachte waarden komen uit presets_tabel.h, die CMake uit presets/*.json maakt.
//
//   pb-presettoets <plugin.clap>
//
// preset-discovery (ARCHITECTUUR.md "Presets"): de factory onder beide id's, één provider per preset (valkuil 15),
// descriptor gelijk aan die van de factory; init declareert precies één locatie van soort PLUGIN (location nullptr,
// fabrieksinhoud); get_metadata geeft één preset met naam, load key = sleutel, het CLAP-id van de plugin en de vlag
// fabrieksinhoud. Een onbekend provider-id geeft nullptr.
// preset-load: elke preset laadt (inactief en actief), get_value geeft de waarden uit het JSON-bestand en voor een
// ontbrekende naam de standaard, ook als de instantie vooraf op andere waarden stond; daarna rescan en loaded (en
// request_flush als actief). Actief klinkt de render bit voor bit als een instantie die dezelfde waarden via flush
// kreeg. Een onbekende load key of een andere soort locatie: false, on_error, en er verandert niets.
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

#include "presets_tabel.h"

namespace {

int fouten = 0, goed = 0;

void toets(bool ok, const std::string& wat) {
  std::printf("%s %s\n", ok ? "ok  " : "FOUT", wat.c_str());
  ok ? ++goed : ++fouten;
}

[[noreturn]] void stop(const std::string& t) {
  std::fprintf(stderr, "pb-presettoets: %s\n", t.c_str());
  std::exit(2);
}

std::string getal(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.17g", v);
  return b;
}

// --- host: telt rescan, request_flush, loaded en on_error --------------------------------------------------------------
int rescans = 0, flushVragen = 0, geladen = 0, laadFouten = 0;
std::string laatsteKey;
void hostRescan(const clap_host_t*, clap_param_rescan_flags) { ++rescans; }
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostRequestFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hostRescan, hostClear, hostRequestFlush};
void hostOnError(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char*) { ++laadFouten; }
void hostLoaded(const clap_host_t*, uint32_t kind, const char* location, const char* key) {
  ++geladen;
  laatsteKey = kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && !location && key ? key : "?";
}
const clap_host_preset_load_t HOST_PRESET_LOAD = {hostOnError, hostLoaded};
const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return &HOST_PRESET_LOAD;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-presettoets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// --- events -------------------------------------------------------------------------------------------------------------
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

Ev paramEv(clap_id id, double v) {
  Ev e{};
  e.p.header = {sizeof(e.p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
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

// --- indexer en metadata-receiver -----------------------------------------------------------------------------------
struct Locatie {
  uint32_t flags, kind;
  std::string naam;
  bool zonderPad;
};
struct Indexer {
  std::vector<Locatie> locaties;
  int filetypes = 0, soundpacks = 0;
};
Indexer* ix(const clap_preset_discovery_indexer_t* i) { return static_cast<Indexer*>(i->indexer_data); }
bool ixFiletype(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_filetype_t*) {
  ++ix(i)->filetypes;
  return true;
}
bool ixLocatie(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
  ix(i)->locaties.push_back({l->flags, l->kind, l->name ? l->name : "", l->location == nullptr});
  return true;
}
bool ixSoundpack(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_soundpack_t*) {
  ++ix(i)->soundpacks;
  return true;
}
const void* ixExt(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }

struct GevondenPreset {
  std::string naam, key, beschrijving;
  std::vector<std::string> pluginIds;
  uint32_t flags = 0;
  bool flagsGezet = false;
};
struct Ontvanger {
  std::vector<GevondenPreset> presets;
  int fouten = 0;
};
Ontvanger* on(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Ontvanger*>(r->receiver_data); }
GevondenPreset* huidig(const clap_preset_discovery_metadata_receiver_t* r) {
  auto* o = on(r);
  if (o->presets.empty()) {
    ++o->fouten;  // metadata zonder begin_preset
    return nullptr;
  }
  return &o->presets.back();
}
void rOnError(const clap_preset_discovery_metadata_receiver_t* r, int32_t, const char*) { ++on(r)->fouten; }
bool rBegin(const clap_preset_discovery_metadata_receiver_t* r, const char* naam, const char* key) {
  GevondenPreset p;
  p.naam = naam ? naam : "";
  p.key = key ? key : "";
  on(r)->presets.push_back(p);
  return true;
}
void rPluginId(const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
  if (auto* p = huidig(r)) p->pluginIds.push_back(std::string(id->abi) + ":" + id->id);
}
void rSoundpack(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rFlags(const clap_preset_discovery_metadata_receiver_t* r, uint32_t f) {
  if (auto* p = huidig(r)) {
    p->flags = f;
    p->flagsGezet = true;
  }
}
void rCreator(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rBeschrijving(const clap_preset_discovery_metadata_receiver_t* r, const char* d) {
  if (auto* p = huidig(r)) p->beschrijving = d ? d : "";
}
void rTijden(const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
void rFeature(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rExtra(const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}

// --- de plugin ------------------------------------------------------------------------------------------------------
const clap_plugin_factory_t* fab = nullptr;
const char* pluginId = nullptr;
constexpr double SR = 48000;
constexpr uint32_t BLOK = 256;

struct Param {
  clap_id id;
  std::string naam;
  double min, max, standaard;
};
std::vector<Param> params;

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* par = nullptr;
  const clap_plugin_preset_load_t* laad = nullptr;
  bool actief = false;

  Instantie() {
    pl = fab->create_plugin(fab, &HOST, pluginId);
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    par = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    laad = static_cast<const clap_plugin_preset_load_t*>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
    if (!par) stop("geen params-extensie");
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
  void flush(const std::vector<double>& w) {
    Events e;
    for (size_t i = 0; i < params.size(); ++i) e.ev.push_back(paramEv(params[i].id, w[i]));
    clap_input_events_t in = {&e, evSize, evGet};
    clap_output_events_t uit = {nullptr, evPush};
    par->flush(pl, &in, &uit);
  }
  std::vector<double> waarden() {
    std::vector<double> w;
    for (auto& p : params) {
      double v = NAN;
      if (!par->get_value(pl, p.id, &v)) stop("get_value mislukt");
      w.push_back(v);
    }
    return w;
  }
  bool laadPreset(uint32_t kind, const char* location, const char* key) {
    return laad && laad->from_location(pl, kind, location, key);
  }
  // `blokken` blokken; een noot aan in het eerste blok en uit in het derde, zodat Filter en Loslaten te horen zijn
  std::vector<float> render(int blokken) {
    std::vector<float> l(BLOK), r(BLOK), alles;
    float* uit[2] = {l.data(), r.data()};
    clap_audio_buffer_t au = {uit, nullptr, 2, 0, 0};
    clap_output_events_t uitEv = {nullptr, evPush};
    for (int k = 0; k < blokken; ++k) {
      Events e;
      if (k == 0) e.ev.push_back(nootEv(17, true, 60));
      if (k == 2) e.ev.push_back(nootEv(101, false, 60));
      clap_input_events_t in = {&e, evSize, evGet};
      clap_process_t pr = {(int64_t)k * BLOK, BLOK, nullptr, nullptr, &au, 0, 1, &in, &uitEv};
      if (pl->process(pl, &pr) == CLAP_PROCESS_ERROR) stop("process gaf ERROR");
      alles.insert(alles.end(), l.begin(), l.end());
      alles.insert(alles.end(), r.begin(), r.end());
    }
    return alles;
  }
};

// wat de preset hoort te geven: de waarde uit het JSON-bestand, voor een ontbrekende naam de standaard
std::vector<double> verwacht(const pb_presets::Preset& p) {
  std::vector<double> w;
  for (auto& q : params) {
    double v = q.standaard;
    for (int k = 0; k < p.aantal; ++k)
      if (q.naam == p.waarden[k].naam) v = p.waarden[k].waarde;
    w.push_back(v);
  }
  return w;
}
// een andere stand dan de preset (zo is te zien dat een ontbrekende naam echt naar zijn standaard gaat)
std::vector<double> anders() {
  std::vector<double> w;
  for (auto& q : params) w.push_back(q.min + 0.37 * (q.max - q.min));
  return w;
}
std::string tekst(const std::vector<double>& w) {
  std::string s;
  for (size_t i = 0; i < w.size(); ++i) s += (i ? ", " : "") + params[i].naam + " " + getal(w[i]);
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) stop("gebruik: pb-presettoets <plugin.clap>");
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) stop(std::string("dlopen: ") + dlerror());
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!entry || !entry->init(argv[1])) stop("geen clap_entry");
  fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!fab || fab->get_plugin_count(fab) < 1) stop("geen plugin-factory");
  pluginId = fab->get_plugin_descriptor(fab, 0)->id;

  // ---- preset-discovery -------------------------------------------------------------------------------------------
  const auto* pf = static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  const auto* pfCompat =
      static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT));
  if (!pf) stop("geen preset-discovery-factory");
  toets(pfCompat == pf, std::string("de factory ook onder ") + CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT);
  const uint32_t n = pf->count(pf);
  toets(n == (uint32_t)pb_presets::AANTAL,
        "aantal providers " + std::to_string(n) + " (verwacht " + std::to_string(pb_presets::AANTAL) + ", één per preset)");
  if (n != (uint32_t)pb_presets::AANTAL) stop("verder toetsen heeft geen zin");
  toets(pf->get_descriptor(pf, n) == nullptr, "get_descriptor voorbij het eind geeft nullptr");
  std::vector<std::string> ids;
  for (uint32_t i = 0; i < n; ++i) {
    const auto& p = pb_presets::PRESETS[i];
    const auto* d = pf->get_descriptor(pf, i);
    if (!d || !d->id) stop("descriptor " + std::to_string(i) + " ontbreekt");
    const std::string wie = std::string("provider ") + d->id;
    bool uniek = true;
    for (auto& j : ids) uniek = uniek && j != d->id;
    ids.push_back(d->id);
    toets(uniek && clap_version_is_compatible(d->clap_version) && d->name && p.naam == std::string(d->name),
          wie + ": uniek id, naam \"" + (d->name ? d->name : "") + "\"");
    Indexer ind;
    const clap_preset_discovery_indexer_t indexer = {CLAP_VERSION_INIT, "pb-presettoets", "Pluginbouwer", "", "0.1",
                                                     &ind, ixFiletype, ixLocatie, ixSoundpack, ixExt};
    const auto* prov = pf->create(pf, &indexer, d->id);
    if (!prov) {
      toets(false, wie + ": create gaf nullptr");
      continue;
    }
    toets(prov->desc == d, wie + ": desc is de descriptor van de factory");
    const bool init = prov->init(prov);
    toets(init && ind.locaties.size() == 1 && ind.locaties[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
              ind.locaties[0].zonderPad && (ind.locaties[0].flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) &&
              ind.filetypes == 0,
          wie + ": init declareert één locatie van soort PLUGIN (fabrieksinhoud, location nullptr)");
    Ontvanger o;
    const clap_preset_discovery_metadata_receiver_t ontvanger = {
        &o, rOnError, rBegin, rPluginId, rSoundpack, rFlags, rCreator, rBeschrijving, rTijden, rFeature, rExtra};
    const bool md = prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &ontvanger);
    const bool een = md && o.fouten == 0 && o.presets.size() == 1;
    toets(een && o.presets[0].naam == p.naam && o.presets[0].key == p.sleutel,
          wie + ": get_metadata geeft één preset (container van één), naam \"" + p.naam + "\", load key \"" + p.sleutel + "\"");
    toets(een && o.presets[0].pluginIds.size() == 1 && o.presets[0].pluginIds[0] == std::string("clap:") + pluginId &&
              o.presets[0].flagsGezet && (o.presets[0].flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) &&
              o.presets[0].beschrijving == p.uitleg,
          wie + ": plugin-id clap:" + pluginId + ", fabrieksinhoud, uitleg als beschrijving");
    Ontvanger o2;
    const clap_preset_discovery_metadata_receiver_t ontvanger2 = {
        &o2, rOnError, rBegin, rPluginId, rSoundpack, rFlags, rCreator, rBeschrijving, rTijden, rFeature, rExtra};
    toets(!prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/niets.json", &ontvanger2) &&
              o2.presets.empty(),
          wie + ": get_metadata voor een FILE-locatie geeft false");
    prov->destroy(prov);
  }
  {
    Indexer ind;
    const clap_preset_discovery_indexer_t indexer = {CLAP_VERSION_INIT, "pb-presettoets", "Pluginbouwer", "", "0.1",
                                                     &ind, ixFiletype, ixLocatie, ixSoundpack, ixExt};
    toets(pf->create(pf, &indexer, "bestaat.niet") == nullptr, "create met een onbekend provider-id geeft nullptr");
  }

  // ---- preset-load ------------------------------------------------------------------------------------------------
  {
    Instantie t;
    if (!t.laad) stop("geen preset-load-extensie");
    const auto* pe = static_cast<const clap_plugin_preset_load_t*>(t.pl->get_extension(t.pl, CLAP_EXT_PRESET_LOAD_COMPAT));
    toets(pe == t.laad, std::string("preset-load ook onder ") + CLAP_EXT_PRESET_LOAD_COMPAT);
    for (uint32_t i = 0; i < t.par->count(t.pl); ++i) {
      clap_param_info_t info;
      if (!t.par->get_info(t.pl, i, &info)) stop("get_info mislukt");
      params.push_back({info.id, info.name, info.min_value, info.max_value, info.default_value});
    }
  }
  for (int actief = 0; actief < 2; ++actief) {
    const std::string stand = actief ? " (actief)" : " (inactief)";
    for (const auto& p : pb_presets::PRESETS) {
      const std::string wie = std::string("preset \"") + p.naam + "\"" + stand;
      Instantie t, ref;
      if (actief) {
        t.activeer();
        ref.activeer();
      }
      t.flush(anders());
      ref.flush(anders());
      if (actief) {
        t.render(1);
        ref.render(1);
      }
      const int r0 = rescans, f0 = flushVragen, g0 = geladen, e0 = laadFouten;
      const bool ok = t.laadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, p.sleutel);
      toets(ok && laadFouten == e0, wie + ": from_location geeft true");
      const auto w = t.waarden(), v = verwacht(p);
      toets(w == v, wie + ": get_value " + tekst(w) + (w == v ? "" : " (verwacht " + tekst(v) + ")"));
      toets(rescans == r0 + 1 && geladen == g0 + 1 && laatsteKey == p.sleutel && flushVragen == f0 + actief,
            wie + ": rescan, loaded(PLUGIN, nullptr, \"" + p.sleutel + "\")" + (actief ? ", request_flush" : ""));
      // de render: gelijk aan een instantie die dezelfde waarden via flush kreeg
      ref.flush(v);
      if (!actief) {
        t.activeer();
        ref.activeer();
      }
      const auto a = t.render(40), b = ref.render(40);
      toets(a == b, wie + ": render bit voor bit gelijk aan dezelfde waarden via flush");
    }
  }
  {
    Instantie t;
    t.flush(anders());
    const auto voor = t.waarden();
    const int r0 = rescans, g0 = geladen, e0 = laadFouten;
    const bool a = t.laadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "bestaat-niet");
    const bool b = t.laadPreset(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, nullptr);
    const bool c = t.laadPreset(CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/donker.json", pb_presets::PRESETS[0].sleutel);
    toets(!a && !b && !c && laadFouten == e0 + 3,
          "onbekende load key, geen load key, FILE-locatie: false en on_error (" + std::to_string(laadFouten - e0) + "×)");
    toets(t.waarden() == voor && rescans == r0 && geladen == g0, "na een mislukte load: niets veranderd, geen rescan of loaded");
  }

  std::printf("pb-presettoets: %d toetsen, %d ok, %d fout (%d presets)\n", goed + fouten, goed, fouten,
              pb_presets::AANTAL);
  entry->deinit();
  return fouten ? 1 : 0;
}
