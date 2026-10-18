// pb-presettoets — de toets van fase 4 (PROMPTS.md), door het gebouwde .clap: de fabriekspresets.
//
//   python3 gereedschap/presets-lijst.py presets > presets.txt
//   pb-presettoets <plugin.clap> presets.txt
//
// presets.txt (uit presets/*.json, op sleutel): per regel sleutel<TAB>naam<TAB>parameternaam=waarde;...
//
// preset-discovery (ARCHITECTUUR.md "Presets"): de factory heeft precies één provider per preset, in de volgorde van
// presets.txt (de AU nummert zijn factory presets zo); elke provider meldt in init één plek van soort
// CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN (zonder pad, fabrieksinhoud) en geeft daar in get_metadata precies één preset:
// de naam uit de JSON, de sleutel als load key, het plugin-id van deze plugin.
// preset-load: elke preset laadt (niet actief en actief), de host hoort `loaded` met dezelfde plek en sleutel en een
// rescan; elke parameter staat daarna op de waarde uit de JSON of, als de JSON hem niet noemt, op zijn standaard (ook
// als hij eerst ergens anders stond); en de DSP klinkt bit voor bit als een verse instantie die die waarden via
// params.flush kreeg. Een onbekende sleutel of een andere soort plek: false, on_error, en niets veranderd.
// clap-validator 0.4.1 laadt van al die presets er maar één: hij bewaart de gevonden presets per plek, en alle
// providers melden dezelfde plek (PLUGIN, zonder pad). Daarom deze toets.
//
// Exitcode 0 = alles groen; anders 1, met per mislukte controle een regel "MISLUKT".
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
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
  std::fprintf(stderr, "pb-presettoets: %s\n", t.c_str());
  std::exit(2);
}
std::string tekst(const char* t) { return t ? t : "(null)"; }
std::string getal(double v) {
  char t[64];
  std::snprintf(t, sizeof t, "%.17g", v);
  return t;
}

// --- host: params (rescan tellen) en preset-load (loaded en on_error onthouden) ----------------------------------
int rescans = 0;
void hpRescan(const clap_host_t*, clap_param_rescan_flags f) {
  if (f & CLAP_PARAM_RESCAN_VALUES) ++rescans;
}
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpFlush(const clap_host_t*) {}
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpFlush};

struct Melding {
  int keer = 0;
  uint32_t soort = 0;
  std::string plek, sleutel;
};
Melding geladen, foutmelding;
void hlFout(const clap_host_t*, uint32_t soort, const char* plek, const char* sleutel, int32_t, const char*) {
  ++foutmelding.keer;
  foutmelding.soort = soort;
  foutmelding.plek = tekst(plek);
  foutmelding.sleutel = tekst(sleutel);
}
void hlGeladen(const clap_host_t*, uint32_t soort, const char* plek, const char* sleutel) {
  ++geladen.keer;
  geladen.soort = soort;
  geladen.plek = tekst(plek);
  geladen.sleutel = tekst(sleutel);
}
const clap_host_preset_load_t HOST_PRESET_LOAD = {hlFout, hlGeladen};

const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return &HOST_PRESET_LOAD;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-presettoets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

// --- events ------------------------------------------------------------------------------------------------------
union Ev { clap_event_header_t h; clap_event_param_value_t p; clap_event_note_t n; };
struct Events { std::vector<Ev> ev; };
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<Events*>(l->ctx)->ev[i].h;
}
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

Ev paramEv(clap_id id, double v) {
  Ev e{};
  e.p.header = {sizeof(e.p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
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
  const clap_plugin_preset_load_t* laad = nullptr;
  bool actief = false;
  Inst() {
    pl = fab->create_plugin(fab, &HOST, pluginId);
    if (!pl || !pl->init(pl)) fout("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    laad = static_cast<const clap_plugin_preset_load_t*>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
    if (!pp) fout("geen params-extensie");
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

struct Preset {
  std::string sleutel, naam;
  std::map<std::string, double> waarden;  // parameternaam → waarde
};
std::vector<Preset> verwacht;

void leesVerwacht(const char* pad) {
  std::ifstream f(pad);
  if (!f) fout(std::string("kan ") + pad + " niet lezen");
  std::string regel;
  while (std::getline(f, regel)) {
    if (regel.empty()) continue;
    const size_t t1 = regel.find('\t'), t2 = regel.find('\t', t1 + 1);
    if (t1 == std::string::npos || t2 == std::string::npos) fout("regel zonder twee tabs: " + regel);
    Preset p;
    p.sleutel = regel.substr(0, t1);
    p.naam = regel.substr(t1 + 1, t2 - t1 - 1);
    std::string rest = regel.substr(t2 + 1);
    size_t pos = 0;
    while (pos < rest.size()) {
      size_t eind = rest.find(';', pos);
      if (eind == std::string::npos) eind = rest.size();
      const std::string paar = rest.substr(pos, eind - pos);
      const size_t is = paar.find('=');
      if (is == std::string::npos) fout("geen naam=waarde: " + paar);
      p.waarden[paar.substr(0, is)] = std::strtod(paar.c_str() + is + 1, nullptr);
      pos = eind + 1;
    }
    verwacht.push_back(p);
  }
  if (verwacht.empty()) fout("geen presets in de lijst");
}

// wat een preset van elke parameter maakt: de waarde uit de JSON, anders de standaard
std::vector<double> doelVan(const Preset& p) {
  std::vector<double> d;
  for (auto& q : params) {
    auto it = p.waarden.find(q.naam);
    d.push_back(it != p.waarden.end() ? it->second : q.standaard);
  }
  return d;
}

// een waarde binnen het bereik die niet de standaard is en niet wat een preset zet (exact in float32)
// Getrapt (Bypass, fase 5): een hele waarde, zoals een host hem stuurt (het maximum, of het minimum als dat de
// standaard is).
double anders(const Param& q) {
  if (q.getrapt) return q.max != q.standaard ? q.max : q.min;
  return (double)(float)(q.min + (q.max - q.min) * 0.8125);
}

// zet de plugin op `w` (via params.flush, buiten process) en geef de render van een verse instantie daarmee
std::vector<float> renderMet(const std::vector<double>& w) {
  Inst x;
  Events ev;
  for (size_t i = 0; i < params.size(); ++i) ev.ev.push_back(paramEv(params[i].id, w[i]));
  x.flush(ev);
  return x.render();
}

// --- preset-discovery: wat een host bij het indexeren ziet -------------------------------------------------------
struct Plek { uint32_t flags, soort; std::string naam; bool zonderPad; };
struct Index { std::vector<Plek> plekken; int bestandstypes = 0, soundpacks = 0; };
bool ixType(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_filetype_t*) {
  ++static_cast<Index*>(i->indexer_data)->bestandstypes;
  return true;
}
bool ixPlek(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
  static_cast<Index*>(i->indexer_data)->plekken.push_back({l->flags, l->kind, tekst(l->name), l->location == nullptr});
  return true;
}
bool ixPack(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_soundpack_t*) {
  ++static_cast<Index*>(i->indexer_data)->soundpacks;
  return true;
}
const void* ixExt(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }

struct Gevonden { std::string naam, sleutel; std::vector<std::string> pluginIds; uint32_t flags = 0; bool flagsGezet = false; };
struct Ontvangst { std::vector<Gevonden> presets; int fouten = 0; };
Ontvangst* van(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Ontvangst*>(r->receiver_data); }
void rFout(const clap_preset_discovery_metadata_receiver_t* r, int32_t, const char*) { ++van(r)->fouten; }
bool rBegin(const clap_preset_discovery_metadata_receiver_t* r, const char* naam, const char* sleutel) {
  van(r)->presets.push_back({tekst(naam), tekst(sleutel), {}, 0, false});
  return true;
}
void rPlugin(const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
  if (!van(r)->presets.empty()) van(r)->presets.back().pluginIds.push_back(tekst(id->abi) + ":" + tekst(id->id));
}
void rPack(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rFlags(const clap_preset_discovery_metadata_receiver_t* r, uint32_t f) {
  if (van(r)->presets.empty()) return;
  van(r)->presets.back().flags = f;
  van(r)->presets.back().flagsGezet = true;
}
void rTekst(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rTijd(const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
void rExtra(const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}

void discoveryToetsen(const clap_plugin_entry_t* entry) {
  const auto* pf = static_cast<const clap_preset_discovery_factory_t*>(
      entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  controle(pf != nullptr, "de entry geeft een preset-discovery-factory");
  if (!pf) return;
  const uint32_t n = pf->count(pf);
  controle(n == verwacht.size(), "de factory heeft " + std::to_string(n) + " providers, één per preset (" +
                                     std::to_string(verwacht.size()) + ")");
  std::vector<std::string> ids;
  for (uint32_t k = 0; k < n && k < verwacht.size(); ++k) {
    const Preset& p = verwacht[k];
    const auto* d = pf->get_descriptor(pf, k);
    if (!d || !d->id) {
      controle(false, "provider " + std::to_string(k) + " heeft een descriptor met een id");
      continue;
    }
    const std::string wie = "provider " + std::to_string(k) + " (" + d->id + "): ";
    for (auto& i : ids) controle(i != d->id, wie + "het id komt maar één keer voor");
    ids.push_back(d->id);

    Index ix;
    const clap_preset_discovery_indexer_t indexer = {CLAP_VERSION_INIT, "pb-presettoets", "Pluginbouwer",
                                                     "https://varve.nl", "0.1", &ix, ixType, ixPlek, ixPack, ixExt};
    const auto* pr = pf->create(pf, &indexer, d->id);
    controle(pr != nullptr, wie + "create");
    if (!pr) continue;
    controle(pr->desc == d, wie + "de provider draagt de descriptor van de factory");
    controle(pr->init(pr), wie + "init");
    controle(ix.plekken.size() == 1, wie + "meldt één plek (" + std::to_string(ix.plekken.size()) + ")");
    if (ix.plekken.size() == 1) {
      const Plek& l = ix.plekken[0];
      controle(l.soort == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && l.zonderPad,
               wie + "de plek is van soort PLUGIN, zonder pad");
      controle(l.flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, wie + "de plek is fabrieksinhoud");
    }
    Ontvangst o;
    const clap_preset_discovery_metadata_receiver_t ontvanger = {&o,     rFout,  rBegin, rPlugin, rPack, rFlags,
                                                                 rTekst, rTekst, rTijd,  rTekst,  rExtra};
    controle(pr->get_metadata(pr, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &ontvanger), wie + "get_metadata");
    controle(o.fouten == 0, wie + "geen on_error");
    controle(o.presets.size() == 1, wie + "precies één preset in de plek (valkuil 15; " +
                                        std::to_string(o.presets.size()) + ")");
    if (o.presets.size() == 1) {
      const Gevonden& g = o.presets[0];
      controle(g.naam == p.naam, wie + "naam '" + g.naam + "' (verwacht '" + p.naam + "')");
      controle(g.sleutel == p.sleutel, wie + "load key '" + g.sleutel + "' (verwacht '" + p.sleutel + "')");
      controle(g.pluginIds.size() == 1 && g.pluginIds[0] == std::string("clap:") + pluginId,
               wie + "voor deze plugin (" + (g.pluginIds.empty() ? std::string("geen id") : g.pluginIds[0]) + ")");
      controle(g.flagsGezet && (g.flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT), wie + "fabrieksinhoud");
    }
    Ontvangst o2;
    const clap_preset_discovery_metadata_receiver_t ontvanger2 = {&o2,    rFout,  rBegin, rPlugin, rPack, rFlags,
                                                                  rTekst, rTekst, rTijd,  rTekst,  rExtra};
    controle(!pr->get_metadata(pr, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/bestaat-niet.json", &ontvanger2) &&
                 o2.presets.empty(),
             wie + "een plek van soort FILE: false, geen preset");
    pr->destroy(pr);
  }
}

// --- preset-load ---------------------------------------------------------------------------------------------------
void laadToetsen() {
  for (bool actief : {false, true}) {
    for (const Preset& p : verwacht) {
      const std::string wie = "'" + p.naam + "'" + (actief ? " [actief]" : " [niet actief]") + ": ";
      Inst x;
      if (!x.laad) {
        controle(false, "de plugin heeft de preset-load-extensie");
        return;
      }
      if (actief) x.activeer();
      // eerst alles ergens anders: wat de preset niet noemt moet terug naar de standaard
      Events ev;
      for (auto& q : params) ev.ev.push_back(paramEv(q.id, anders(q)));
      x.flush(ev);
      const int rescansVoor = rescans, geladenVoor = geladen.keer, foutenVoor = foutmelding.keer;
      const bool ok = x.laad->from_location(x.pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, p.sleutel.c_str());
      controle(ok, wie + "from_location(PLUGIN, " + p.sleutel + ") geeft true");
      controle(geladen.keer == geladenVoor + 1 && geladen.soort == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
                   geladen.plek == "(null)" && geladen.sleutel == p.sleutel,
               wie + "de host hoort loaded(PLUGIN, null, " + p.sleutel + ")");
      controle(foutmelding.keer == foutenVoor, wie + "geen on_error");
      controle(rescans > rescansVoor, wie + "rescan(CLAP_PARAM_RESCAN_VALUES)");
      if (actief) x.flushLeeg();  // params.flush buiten process neemt de geladen waarden over
      const std::vector<double> doel = doelVan(p);
      for (size_t i = 0; i < params.size(); ++i) {
        const double v = x.waarde(params[i].id);
        const bool genoemd = p.waarden.count(params[i].naam) > 0;
        controle(v == doel[i], wie + params[i].naam + " = " + getal(v) + " (verwacht " + getal(doel[i]) +
                                   (genoemd ? ", uit de preset)" : ", de standaard)"));
      }
      controle(x.render() == renderMet(doel), wie + "de DSP klinkt bit voor bit als een verse instantie met die waarden");
    }
  }
  // een onbekende sleutel en een andere soort plek: false, on_error, niets veranderd
  for (bool actief : {false, true}) {
    const std::string wie = std::string(actief ? "[actief]" : "[niet actief]") + " ";
    Inst x;
    if (!x.laad) return;
    if (actief) x.activeer();
    Events ev;
    for (auto& q : params) ev.ev.push_back(paramEv(q.id, anders(q)));
    x.flush(ev);
    struct Geval { uint32_t soort; const char* plek; const char* sleutel; const char* wat; };
    const Geval gevallen[] = {
        {CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "bestaat-niet", "onbekende sleutel"},
        {CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, nullptr, "geen sleutel"},
        {CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/bestaat-niet.json", verwacht[0].sleutel.c_str(), "plek van soort FILE"},
    };
    for (const Geval& g : gevallen) {
      const int foutenVoor = foutmelding.keer, geladenVoor = geladen.keer;
      const bool ok = x.laad->from_location(x.pl, g.soort, g.plek, g.sleutel);
      controle(!ok, wie + g.wat + ": from_location geeft false");
      controle(foutmelding.keer == foutenVoor + 1, wie + g.wat + ": de host hoort on_error");
      controle(geladen.keer == geladenVoor, wie + g.wat + ": geen loaded");
      if (actief) x.flushLeeg();
      bool zelfde = true;
      for (auto& q : params) zelfde = zelfde && x.waarde(q.id) == anders(q);
      controle(zelfde, wie + g.wat + ": alle parameters onveranderd");
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) fout("gebruik: pb-presettoets <plugin.clap> <presets.txt>");
  leesVerwacht(argv[2]);
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
    }
    // elke naam in de JSON is een parameter (de bouw eist het al; hier door het binary)
    for (auto& p : verwacht)
      for (auto& [naam, v] : p.waarden) {
        bool gevonden = false;
        for (auto& q : params)
          if (q.naam == naam) gevonden = v >= q.min && v <= q.max;
        controle(gevonden, "'" + p.naam + "': " + naam + " = " + getal(v) + " is een parameter, binnen het bereik");
      }
    // een verse plugin staat op de standaarden (de AU begint daarom op "Untitled", niet op preset 0)
    bool standaard = true;
    for (auto& q : params) standaard = standaard && x.waarde(q.id) == q.standaard;
    controle(standaard, "een verse instantie staat op de standaarden, niet op een preset");
  }

  std::printf("--- preset-discovery (%zu presets)\n", verwacht.size());
  discoveryToetsen(entry);
  std::printf("--- preset-load\n");
  laadToetsen();

  entry->deinit();
  std::printf("pb-presettoets: %d controles, %d geslaagd, %d mislukt\n", goed + fouten, goed, fouten);
  return fouten ? 1 : 0;
}
