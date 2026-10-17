// pb-preset-toets — doen de fabriekspresets wat ze zeggen? Een kleine CLAP-host die de preset-discovery-factory van
// de plugin doorzoekt zoals een DAW (indexer, provider, metadata) en elke gevonden preset laadt met preset-load.
//
//   pb-preset-toets <plugin.clap> <tabel.tsv> [<in.wav>]
//
// Wat hij toetst (exitcode 1 bij een fout, met uitleg):
//  1. De factory (ook onder de _COMPAT-id) heeft providers met een geldige descriptor; elke provider verklaart één
//     locatie van soort PLUGIN (location NULL) met de vlag fabrieksinhoud, en meldt per preset een naam, een load_key
//     (uniek), de CLAP-id van deze plugin en de vlag fabrieksinhoud. Een onbekende provider-id of een
//     bestandslocatie: geweigerd.
//  2. Elke preset laadt (from_location → true); de host hoort `loaded` met dezelfde locatie en sleutel en krijgt een
//     rescan van de waarden. De waarden gaan naar <tabel.tsv> (sleutel, presetnaam, parameter, waarde %.17g) zodat
//     gereedschap/vergelijk-presets.mjs ze tegen presets/*.json legt.
//  3. Een preset laden vóór activate klinkt bit voor bit als dezelfde waarden via params.flush, en een preset laden
//     terwijl de plugin actief is (tussen twee process-blokken) ook: het Ontwerp krijgt de waarden echt.
//  4. Een onbekende sleutel geeft false, meldt on_error en verandert geen waarde.
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
[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-preset-toets: %s\n", t.c_str()); std::exit(2); }
std::string tekst(const char* s) { return s ? s : "(null)"; }

// --- host met params.rescan en preset-load (we onthouden wat de plugin meldt) ----------------------------------------
int g_rescans = 0, g_fouten_plugin = 0;
std::vector<std::string> g_geladen;  // "soort|location|sleutel" per loaded()
void hpRescan(const clap_host_t*, clap_param_rescan_flags f) { if (f & CLAP_PARAM_RESCAN_VALUES) ++g_rescans; }
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpFlush(const clap_host_t*) {}
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpFlush};
void hplFout(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char* msg) {
  ++g_fouten_plugin;
  std::printf("        (on_error van de plugin: %s)\n", tekst(msg).c_str());
}
void hplGeladen(const clap_host_t*, uint32_t soort, const char* plek, const char* sleutel) {
  g_geladen.push_back(std::to_string(soort) + "|" + tekst(plek) + "|" + tekst(sleutel));
}
const clap_host_preset_load_t HOST_PRESET_LOAD = {hplFout, hplGeladen};
const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return &HOST_PRESET_LOAD;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-preset-toets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

struct Events { std::vector<clap_event_param_value_t> ev; };
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t) static_cast<Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return &static_cast<Events*>(l->ctx)->ev[i].header; }
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT { return pl->process(pl, pr); }

// --- de preset-discovery: indexer en metadata-ontvanger ------------------------------------------------------------
struct Plek { uint32_t vlaggen, soort; std::string naam; bool zonderLocatie; };
struct Gevonden {
  std::string naam, sleutel, uitleg;
  std::vector<std::string> pluginIds;  // "abi:id"
  bool vlaggenGezet = false;
  uint32_t vlaggen = 0;
};
struct Index {
  std::vector<Plek> plekken;
  std::vector<Gevonden> presets;
  int fouten = 0;
};
Index* ix(const clap_preset_discovery_indexer_t* i) { return static_cast<Index*>(i->indexer_data); }
Index* ix(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Index*>(r->receiver_data); }
bool ixBestandsoort(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_filetype_t*) { return true; }
bool ixPlek(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
  ix(i)->plekken.push_back({l->flags, l->kind, tekst(l->name), l->location == nullptr});
  return true;
}
bool ixPakket(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; }
const void* ixExt(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }

void mrFout(const clap_preset_discovery_metadata_receiver_t* r, int32_t, const char* msg) {
  ++ix(r)->fouten;
  fout("metadata on_error: " + tekst(msg));
}
bool mrBegin(const clap_preset_discovery_metadata_receiver_t* r, const char* naam, const char* sleutel) {
  Gevonden g;
  g.naam = tekst(naam);
  g.sleutel = tekst(sleutel);
  if (!naam || !sleutel) fout("begin_preset zonder naam of load_key (een container in de plugin heeft beide nodig)");
  ix(r)->presets.push_back(g);
  return true;
}
Gevonden* huidig(const clap_preset_discovery_metadata_receiver_t* r) {
  if (ix(r)->presets.empty()) { fout("metadata vóór begin_preset"); return nullptr; }
  return &ix(r)->presets.back();
}
void mrPluginId(const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
  if (auto* g = huidig(r)) g->pluginIds.push_back(tekst(id->abi) + ":" + tekst(id->id));
}
void mrPakket(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void mrVlaggen(const clap_preset_discovery_metadata_receiver_t* r, uint32_t v) {
  if (auto* g = huidig(r)) { g->vlaggenGezet = true; g->vlaggen = v; }
}
void mrMaker(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void mrUitleg(const clap_preset_discovery_metadata_receiver_t* r, const char* u) {
  if (auto* g = huidig(r)) g->uitleg = tekst(u);
}
void mrTijden(const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
void mrKenmerk(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void mrExtra(const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}

// --- een geladen .clap ---------------------------------------------------------------------------------------------
struct Bib {
  const clap_plugin_entry_t* entry = nullptr;
  const clap_plugin_factory_t* fab = nullptr;
  const char* id = nullptr;
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
  Bib b;
  b.entry = static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry"));
  if (!b.entry || !b.entry->init(pad.c_str())) stop("geen clap_entry in " + pad);
  b.fab = static_cast<const clap_plugin_factory_t*>(b.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!b.fab || b.fab->get_plugin_count(b.fab) < 1) stop("geen plugin in " + pad);
  b.id = b.fab->get_plugin_descriptor(b.fab, 0)->id;
  return b;
}

struct Inst {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_preset_load_t* pre = nullptr;
  explicit Inst(const Bib& b) {
    pl = b.fab->create_plugin(b.fab, &HOST, b.id);
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    pre = static_cast<const clap_plugin_preset_load_t*>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
    if (!pp || !pre) stop("geen params- of preset-load-extensie");
  }
  ~Inst() { pl->destroy(pl); }
  Inst(const Inst&) = delete;
  Inst& operator=(const Inst&) = delete;

  std::map<clap_id, std::string> namen() const {
    std::map<clap_id, std::string> m;
    for (uint32_t k = 0, n = pp->count(pl); k < n; ++k) {
      clap_param_info_t i{};
      if (!pp->get_info(pl, k, &i)) stop("get_info faalt");
      m[i.id] = i.name;
    }
    return m;
  }
  std::map<clap_id, double> waarden() const {
    std::map<clap_id, double> m;
    for (auto& [id, n] : namen()) {
      double v = NAN;
      if (!pp->get_value(pl, id, &v)) stop("get_value faalt");
      m[id] = v;
    }
    return m;
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
  bool laadPreset(const std::string& sleutel) const {
    return pre->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, sleutel.c_str());
  }
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

// render `in` door `inst` (blok 512). `tussendoor(blokNr)` mag tussen twee blokken iets doen (preset laden, flush).
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

void vergelijkAudio(const std::string& wat, const std::vector<std::vector<float>>& a,
                    const std::vector<std::vector<float>>& b) {
  double piek = 0, verschil = 0;
  bool gelijk = a.size() == b.size();
  for (size_t c = 0; gelijk && c < a.size(); ++c) {
    gelijk = a[c].size() == b[c].size() && std::memcmp(a[c].data(), b[c].data(), a[c].size() * 4) == 0;
    for (size_t i = 0; i < a[c].size() && i < b[c].size(); ++i) {
      piek = std::fmax(piek, std::fabs(a[c][i]));
      verschil = std::fmax(verschil, std::fabs((double)a[c][i] - b[c][i]));
    }
  }
  if (!gelijk) fout(wat + ": verschilt, max |verschil| " + std::to_string(verschil));
  if (piek == 0) fout(wat + ": stilte; de toets zegt dan niets");
  ok("%s: bit voor bit gelijk (piek %.3f)", wat.c_str(), piek);
}

bool zelfde(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) stop("gebruik: pb-preset-toets <plugin.clap> <tabel.tsv> [<in.wav>]");
  const Bib bib = laad(argv[1]);
  const Wav in = argc > 3 ? leesWav(argv[3]) : ruis(96000);
  const std::string clapId = std::string("clap:") + bib.id;

  // 1. de discovery zoals een DAW hem doorloopt
  std::printf("1. preset-discovery\n");
  auto* fab = static_cast<const clap_preset_discovery_factory_t*>(bib.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  auto* compat = bib.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT);
  if (!fab) stop("geen preset-discovery-factory");
  if (compat != fab) fout("de _COMPAT-id geeft een andere factory");
  const uint32_t nProv = fab->count(fab);
  if (nProv < 1) stop("de factory heeft geen provider");
  ok("factory met %u provider(s), ook onder de _COMPAT-id", nProv);

  Index index;
  const clap_preset_discovery_indexer_t indexer = {CLAP_VERSION_INIT, "pb-preset-toets", "Pluginbouwer", nullptr, "0.1",
                                                   &index, ixBestandsoort, ixPlek, ixPakket, ixExt};
  const clap_preset_discovery_metadata_receiver_t ontvanger = {&index, mrFout, mrBegin, mrPluginId, mrPakket,
                                                               mrVlaggen, mrMaker, mrUitleg, mrTijden, mrKenmerk,
                                                               mrExtra};
  if (fab->create(fab, &indexer, "bestaat.niet")) fout("create met een onbekende provider-id gaf een provider");
  for (uint32_t k = 0; k < nProv; ++k) {
    const auto* d = fab->get_descriptor(fab, k);
    if (!d || !d->id || !d->name || !clap_version_is_compatible(d->clap_version)) { fout("ongeldige provider-descriptor"); continue; }
    const auto* prov = fab->create(fab, &indexer, d->id);
    if (!prov) { fout(std::string("create gaf geen provider voor ") + d->id); continue; }
    if (prov->desc != d && (!prov->desc || std::strcmp(prov->desc->id, d->id))) fout("provider->desc hoort bij een andere provider");
    index.plekken.clear();
    if (!prov->init(prov)) fout("provider init gaf false");
    if (index.plekken.size() != 1) fout("verwacht één locatie, kreeg " + std::to_string(index.plekken.size()));
    for (auto& p : index.plekken) {
      if (p.soort != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || !p.zonderLocatie) fout("locatie '" + p.naam + "' is niet van soort PLUGIN met location NULL");
      if (!(p.vlaggen & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT)) fout("locatie '" + p.naam + "' is geen fabrieksinhoud");
    }
    ok("provider '%s' (%s): locatie '%s' in de plugin, fabrieksinhoud", d->name, d->id,
       index.plekken.empty() ? "" : index.plekken[0].naam.c_str());
    if (prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/bestaat/niet.json", &ontvanger))
      fout("get_metadata voor een bestandslocatie gaf true");
    if (!prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &ontvanger)) fout("get_metadata gaf false");
    prov->destroy(prov);
  }
  if (index.presets.empty()) stop("geen presets gevonden");
  std::map<std::string, int> sleutels;
  for (auto& g : index.presets) {
    if (++sleutels[g.sleutel] > 1) fout("sleutel '" + g.sleutel + "' komt twee keer voor");
    if (g.pluginIds.size() != 1 || g.pluginIds[0] != clapId) fout("preset '" + g.naam + "' hoort niet (alleen) bij " + clapId);
    if (!g.vlaggenGezet || !(g.vlaggen & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT)) fout("preset '" + g.naam + "' is niet als fabrieksinhoud gemeld");
    ok("preset '%s' (sleutel %s): %s, fabrieksinhoud", g.naam.c_str(), g.sleutel.c_str(), clapId.c_str());
  }

  // 2. elke preset laden; de waarden naar de tabel
  std::printf("2. preset-load\n");
  FILE* tabel = std::fopen(argv[2], "w");
  if (!tabel) stop(std::string("kan niet schrijven: ") + argv[2]);
  for (auto& g : index.presets) {
    Inst i(bib);
    const auto namen = i.namen();
    g_geladen.clear();
    const int r0 = g_rescans;
    if (!i.laadPreset(g.sleutel)) { fout("from_location gaf false voor '" + g.sleutel + "'"); continue; }
    const std::string verwacht = std::to_string(CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) + "|(null)|" + g.sleutel;
    if (g_geladen.size() != 1 || g_geladen[0] != verwacht) fout("host hoorde geen loaded(" + verwacht + ")");
    if (g_rescans == r0) fout("geen params.rescan na het laden van '" + g.sleutel + "'");
    for (auto& [id, v] : i.waarden())
      std::fprintf(tabel, "%s\t%s\t%s\t%.17g\n", g.sleutel.c_str(), g.naam.c_str(), namen.at(id).c_str(), v);
    std::fprintf(tabel, "%s\t%s\t#uitleg\t%s\n", g.sleutel.c_str(), g.naam.c_str(), g.uitleg.c_str());
    ok("'%s' geladen; loaded() en rescan gehoord", g.sleutel.c_str());
  }
  std::fclose(tabel);

  // 3. klinkt de preset als dezelfde waarden via flush, vóór activate en terwijl actief?
  std::printf("3. de preset bereikt de DSP\n");
  for (auto& g : index.presets) {
    Inst a(bib), b(bib);
    if (!a.laadPreset(g.sleutel)) continue;  // al gemeld
    const auto w = a.waarden();
    b.flush(w);
    vergelijkAudio("'" + g.sleutel + "' vóór activate vs flush", render(a, in), render(b, in));

    Inst c(bib), d(bib);
    const int blok = 20;
    auto mc = render(c, in, [&](int nr) { if (nr == blok && !c.laadPreset(g.sleutel)) fout("laden terwijl actief faalt"); });
    auto md = render(d, in, [&](int nr) { if (nr == blok) d.flush(w); });
    vergelijkAudio("'" + g.sleutel + "' geladen terwijl actief vs flush", mc, md);
    for (auto& [id, v] : c.waarden())
      if (!zelfde(v, w.at(id))) fout("na laden terwijl actief: parameter " + std::to_string(id) + " = " + std::to_string(v));
  }

  // 4. een onbekende sleutel
  std::printf("4. onbekende preset\n");
  {
    Inst i(bib);
    if (!i.laadPreset(index.presets.back().sleutel)) stop("laden faalt");
    const auto voor = i.waarden();
    const int f0 = g_fouten_plugin;
    if (i.laadPreset("bestaat-niet")) fout("from_location met een onbekende sleutel gaf true");
    if (i.pre->from_location(i.pl, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/bestaat/niet.json", nullptr))
      fout("from_location met een bestand gaf true");
    if (g_fouten_plugin != f0 + 2) fout("de plugin meldde on_error niet");
    for (auto& [id, v] : i.waarden())
      if (!zelfde(v, voor.at(id))) fout("een mislukte preset veranderde parameter " + std::to_string(id));
    ok("%s", "onbekende sleutel en bestandslocatie: false, on_error, niets veranderd");
  }

  if (g_fouten) { std::fprintf(stderr, "pb-preset-toets: %d fout(en)\n", g_fouten); return 1; }
  std::printf("pb-preset-toets: alles groen (%zu presets)\n", index.presets.size());
  return 0;
}
