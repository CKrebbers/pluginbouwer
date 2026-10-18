// formaattoets — de eigen toets van fase 5, door het gebouwde .clap (zoals pb-render: dlopen, geen code van de plugin).
//
//   pb-formaattoets <plugin.clap>
//
// Kanaalindelingen (valkuil 17): audio-ports-config heeft precies 0 → 2 (stereo) en 0 → 1 (mono), geen ingang;
// select en configurable-audio-ports (de weg van de VST3) zetten de uitgangspoort om, alleen als de plugin niet actief
// is; een ingang of 3 kanalen vragen kan niet. audio-ports-config-info zegt welke config gekozen is.
// Samplerates 44,1–192 kHz × blokgrootte 1, 4096 en wisselend (3,128,1,4096,77), stereo en mono: de render hangt niet
// af van de blokgrootte (bit voor bit), mono is bit voor bit het linkerkanaal van stereo, niets is NaN, oneindig of
// subnormaal, en het klinkt.
// Bypass (CLAP_PARAM_IS_BYPASS): de parameter (id 1000, 0..1, standaard 0, stepped), tekst "Uit"/"Aan". Een render met
// bypass aan en weer uit (events midden in een blok) is bit voor bit: vóór de bypass gelijk aan de render zonder
// bypass; 10 ms (round(0,01·sr) samples) lineair overvloeien naar de droge invoer, door pb::uitvoer; dan exact stilte;
// 10 ms terug; daarna weer bit voor bit gelijk aan de render zonder bypass (de DSP rekende door). Bypass vóór activate:
// stil vanaf de eerste sample (geen overgang bij de start). State bewaart de bypass; een state zonder bypass laadt als
// "niet omzeild".
//
// Uitvoer: één regel per toets ("ok …" of "FOUT …"), aan het eind een samenvatting; exit 0 alleen als alles ok is.
#include <clap/clap.h>
#include <dlfcn.h>

#include <algorithm>
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
  std::fprintf(stderr, "pb-formaattoets: %s\n", t.c_str());
  std::exit(2);
}

const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-formaattoets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

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

using Blob = std::vector<unsigned char>;
int64_t ostWrite(const clap_ostream_t* s, const void* p, uint64_t n) {
  auto* b = static_cast<Blob*>(s->ctx);
  auto* c = static_cast<const unsigned char*>(p);
  b->insert(b->end(), c, c + n);
  return (int64_t)n;
}
struct Lezer {
  const Blob* b;
  size_t pos = 0;
};
int64_t istRead(const clap_istream_t* s, void* p, uint64_t n) {
  auto* l = static_cast<Lezer*>(s->ctx);
  uint64_t m = l->b->size() - l->pos;
  if (n < m) m = n;
  std::memcpy(p, l->b->data() + l->pos, m);
  l->pos += m;
  return (int64_t)m;
}

constexpr clap_id BYPASS_ID = 1000;

// een event in een render, op zijn sample: een noot (aan/uit) of een parameter
struct Gebeurtenis {
  uint64_t sample;
  bool noot;
  bool aan;
  int key;
  clap_id id;
  double waarde;
};
Gebeurtenis noot(uint64_t s, bool aan, int key) { return {s, true, aan, key, 0, 0}; }
Gebeurtenis param(uint64_t s, clap_id id, double v) { return {s, false, false, 0, id, v}; }

const clap_plugin_factory_t* fab = nullptr;
const char* pluginId = nullptr;

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* params = nullptr;
  const clap_plugin_state_t* state = nullptr;
  const clap_plugin_audio_ports_t* poorten = nullptr;
  const clap_plugin_audio_ports_config_t* configs = nullptr;
  const clap_plugin_audio_ports_config_info_t* configInfo = nullptr;
  const clap_plugin_configurable_audio_ports_t* configureerbaar = nullptr;
  bool actief = false;

  Instantie() {
    pl = fab->create_plugin(fab, &HOST, pluginId);
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    params = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    state = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    poorten = static_cast<const clap_plugin_audio_ports_t*>(pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS));
    configs = static_cast<const clap_plugin_audio_ports_config_t*>(pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS_CONFIG));
    configInfo =
        static_cast<const clap_plugin_audio_ports_config_info_t*>(pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO));
    configureerbaar = static_cast<const clap_plugin_configurable_audio_ports_t*>(
        pl->get_extension(pl, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS));
    if (!params || !state || !poorten) stop("geen params-, state- of audio-ports-extensie");
  }
  ~Instantie() {
    deactiveer();
    pl->destroy(pl);
  }
  void activeer(double sr, uint32_t maxBlok) {
    if (!pl->activate(pl, sr, 1, maxBlok) || !pl->start_processing(pl)) stop("activate mislukt");
    actief = true;
  }
  void deactiveer() {
    if (!actief) return;
    pl->stop_processing(pl);
    pl->deactivate(pl);
    actief = false;
  }
  uint32_t uitKanalen() {
    clap_audio_port_info_t info{};
    if (poorten->count(pl, true) != 0 || poorten->count(pl, false) != 1 || !poorten->get(pl, 0, false, &info)) return 0;
    return info.channel_count;
  }
  // kies de config met zoveel uitgangskanalen (audio-ports-config, zoals een CLAP-host)
  bool kies(uint32_t kanalen) {
    if (!configs) return false;
    for (uint32_t i = 0; i < configs->count(pl); ++i) {
      clap_audio_ports_config_t c{};
      if (configs->get(pl, i, &c) && c.main_output_channel_count == kanalen) return configs->select(pl, c.id);
    }
    return false;
  }
  void flush(clap_id id, double v) {
    Events e;
    Ev x{};
    x.p.header = {sizeof(x.p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    x.p.param_id = id;
    x.p.note_id = -1;
    x.p.port_index = -1;
    x.p.channel = -1;
    x.p.key = -1;
    x.p.value = v;
    e.ev.push_back(x);
    clap_input_events_t in = {&e, evSize, evGet};
    clap_output_events_t uit = {nullptr, evPush};
    params->flush(pl, &in, &uit);
  }
  double waarde(clap_id id) {
    double v = NAN;
    if (!params->get_value(pl, id, &v)) return NAN;
    return v;
  }
  // render n samples in blokken (het patroon herhaald), met de events op hun sample; kanalen = de uitgangspoort
  std::vector<std::vector<float>> render(uint64_t n, const std::vector<uint32_t>& blokken,
                                         const std::vector<Gebeurtenis>& gb) {
    const uint32_t kan = uitKanalen();
    if (kan < 1 || kan > 2) stop("uitgangspoort heeft geen 1 of 2 kanalen");
    std::vector<std::vector<float>> uit(kan, std::vector<float>(n));
    std::vector<std::vector<float>> buf(kan, std::vector<float>(4096));
    float* p[2] = {buf[0].data(), kan > 1 ? buf[1].data() : nullptr};
    clap_audio_buffer_t au = {p, nullptr, kan, 0, 0};
    clap_output_events_t uitEv = {nullptr, evPush};
    size_t volgende = 0, bnr = 0;
    for (uint64_t pos = 0; pos < n;) {
      uint32_t m = blokken[bnr++ % blokken.size()];
      if (pos + m > n) m = (uint32_t)(n - pos);
      for (auto& b : buf) std::fill(b.begin(), b.begin() + m, 12345.0f);  // de plugin moet alles schrijven
      Events e;
      while (volgende < gb.size() && gb[volgende].sample < pos + m) {
        const auto& g = gb[volgende++];
        const uint32_t t = (uint32_t)(g.sample - pos);
        Ev x{};
        if (g.noot) {
          x.n.header = {sizeof(x.n), t, CLAP_CORE_EVENT_SPACE_ID, (uint16_t)(g.aan ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
          x.n.note_id = -1;
          x.n.port_index = 0;
          x.n.channel = 0;
          x.n.key = (int16_t)g.key;
          x.n.velocity = g.aan ? 0.8 : 0.0;
        } else {
          x.p.header = {sizeof(x.p), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
          x.p.param_id = g.id;
          x.p.note_id = -1;
          x.p.port_index = -1;
          x.p.channel = -1;
          x.p.key = -1;
          x.p.value = g.waarde;
        }
        e.ev.push_back(x);
      }
      clap_input_events_t in = {&e, evSize, evGet};
      clap_process_t pr = {(int64_t)pos, m, nullptr, nullptr, &au, 0, 1, &in, &uitEv};
      if (pl->process(pl, &pr) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
      for (uint32_t c = 0; c < kan; ++c) std::memcpy(&uit[c][pos], buf[c].data(), m * sizeof(float));
      pos += m;
    }
    return uit;
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
};

bool bitGelijk(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && !std::memcmp(a.data(), b.data(), a.size() * sizeof(float));
}
bool bitGelijk(const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b) {
  if (a.size() != b.size()) return false;
  for (size_t c = 0; c < a.size(); ++c)
    if (!bitGelijk(a[c], b[c])) return false;
  return true;
}
// geen NaN, oneindig of subnormaal (pb::uitvoer), en niet alleen stilte
std::string gezond(const std::vector<std::vector<float>>& u) {
  bool klinkt = false;
  for (auto& k : u)
    for (float x : k) {
      if (!std::isfinite(x)) return "NaN of oneindig";
      if (x != 0 && std::fpclassify(x) == FP_SUBNORMAL) return "subnormale float";
      if (x != 0) klinkt = true;
    }
  return klinkt ? "" : "alleen stilte";
}
// dsp/pb_core.h pb::uitvoer: subnormaal wordt 0
float uitvoer(double y) { return (y > -1.1754943508222875e-38 && y < 1.1754943508222875e-38) ? 0.0f : (float)y; }

// een stukje spel: noot 57 aan, 64 erbij (legato), 64 los, 57 los (het loslaten klinkt uit)
std::vector<Gebeurtenis> spel(uint64_t n) {
  return {noot(100, true, 57), noot(n / 3 + 1, true, 64), noot(n / 2 + 2, false, 64), noot(n * 3 / 4 + 3, false, 57)};
}
std::vector<Gebeurtenis> metBypass(std::vector<Gebeurtenis> g, uint64_t a, uint64_t b) {
  g.push_back(param(a, BYPASS_ID, 1.0));
  g.push_back(param(b, BYPASS_ID, 0.0));
  for (size_t i = 1; i < g.size(); ++i)
    for (size_t j = i; j > 0 && g[j - 1].sample > g[j].sample; --j) std::swap(g[j - 1], g[j]);
  return g;
}

const char* naamVan(uint32_t k) { return k == 1 ? "mono" : "stereo"; }

// --- kanaalindelingen ------------------------------------------------------------------------------------------------
void indelingToetsen() {
  std::printf("\n# Kanaalindelingen (audio-ports-config, configurable-audio-ports; valkuil 17)\n");
  Instantie a;
  toets(a.configs != nullptr, "audio-ports-config bestaat");
  toets(a.configInfo != nullptr, "audio-ports-config-info bestaat");
  toets(a.configureerbaar != nullptr, "configurable-audio-ports bestaat (de VST3 van clap-wrapper: setBusArrangements)");
  if (!a.configs || !a.configInfo || !a.configureerbaar) return;
  toets(a.poorten->count(a.pl, true) == 0, "audio-ports: geen ingang (een instrument)");
  toets(a.uitKanalen() == 2, "audio-ports: een verse instantie heeft een stereo-uitgang");
  toets(a.configs->count(a.pl) == 2, "audio-ports-config: twee indelingen");
  bool stereo = false, mono = false;
  clap_id idStereo = CLAP_INVALID_ID, idMono = CLAP_INVALID_ID;
  for (uint32_t i = 0; i < a.configs->count(a.pl); ++i) {
    clap_audio_ports_config_t c{};
    if (!a.configs->get(a.pl, i, &c)) continue;
    const bool vorm = c.input_port_count == 0 && !c.has_main_input && c.output_port_count == 1 && c.has_main_output;
    if (vorm && c.main_output_channel_count == 2 && c.main_output_port_type &&
        !std::strcmp(c.main_output_port_type, CLAP_PORT_STEREO)) {
      stereo = true;
      idStereo = c.id;
    }
    if (vorm && c.main_output_channel_count == 1 && c.main_output_port_type &&
        !std::strcmp(c.main_output_port_type, CLAP_PORT_MONO)) {
      mono = true;
      idMono = c.id;
    }
  }
  clap_audio_ports_config_t leeg{};
  toets(!a.configs->get(a.pl, 2, &leeg), "audio-ports-config: get(2) geeft false");
  toets(stereo, "audio-ports-config: 0 → 2 (stereo, CLAP_PORT_STEREO), zonder ingang");
  toets(mono, "audio-ports-config: 0 → 1 (mono, CLAP_PORT_MONO), zonder ingang");
  toets(a.configInfo->current_config(a.pl) == idStereo, "audio-ports-config-info: een verse instantie staat op stereo");

  toets(a.configs->select(a.pl, idMono) && a.uitKanalen() == 1, "select(mono): de uitgangspoort heeft 1 kanaal");
  clap_audio_port_info_t info{};
  toets(a.poorten->get(a.pl, 0, false, &info) && info.port_type && !std::strcmp(info.port_type, CLAP_PORT_MONO),
        "select(mono): poorttype CLAP_PORT_MONO");
  toets(a.configInfo->current_config(a.pl) == idMono, "audio-ports-config-info: current_config is mono");
  toets(a.configInfo->get(a.pl, idStereo, 0, false, &info) && info.channel_count == 2,
        "audio-ports-config-info: get(stereo) geeft 2 kanalen, ook als mono gekozen is");
  toets(!a.configInfo->get(a.pl, idStereo, 0, true, &info), "audio-ports-config-info: geen ingang");
  toets(!a.configs->select(a.pl, 77), "select(onbekend id) geeft false");
  toets(a.uitKanalen() == 1, "na een mislukte select blijft mono gekozen");
  toets(a.configs->select(a.pl, idStereo) && a.uitKanalen() == 2, "select(stereo): weer 2 kanalen");

  // configurable-audio-ports, zoals clap-wrapper hem uit setBusArrangements aanroept
  clap_audio_port_configuration_request r{};
  r.is_input = false;
  r.port_index = 0;
  r.channel_count = 1;
  r.port_type = CLAP_PORT_MONO;
  toets(a.configureerbaar->can_apply_configuration(a.pl, &r, 1), "configurable: uitgang mono kan");
  toets(a.uitKanalen() == 2, "configurable: can_apply verandert niets");
  toets(a.configureerbaar->apply_configuration(a.pl, &r, 1) && a.uitKanalen() == 1, "configurable: uitgang mono toegepast");
  r.channel_count = 2;
  r.port_type = nullptr;  // clap-wrapper geeft bij kStereo CLAP_PORT_STEREO, maar een host mag nullptr geven
  toets(a.configureerbaar->apply_configuration(a.pl, &r, 1) && a.uitKanalen() == 2, "configurable: uitgang stereo toegepast");
  r.channel_count = 3;
  toets(!a.configureerbaar->can_apply_configuration(a.pl, &r, 1) && !a.configureerbaar->apply_configuration(a.pl, &r, 1),
        "configurable: 3 kanalen kan niet");
  r.channel_count = 1;
  r.port_type = CLAP_PORT_STEREO;
  toets(!a.configureerbaar->apply_configuration(a.pl, &r, 1), "configurable: 1 kanaal met poorttype stereo kan niet");
  r.port_type = CLAP_PORT_MONO;
  r.is_input = true;
  toets(!a.configureerbaar->apply_configuration(a.pl, &r, 1), "configurable: een ingang vragen kan niet (instrument)");
  r.is_input = false;
  r.port_index = 1;
  toets(!a.configureerbaar->apply_configuration(a.pl, &r, 1), "configurable: uitgang 1 bestaat niet");
  toets(a.uitKanalen() == 2, "na de mislukte verzoeken blijft stereo gekozen");

  // alleen als de plugin niet actief is
  a.activeer(48000, 512);
  r.port_index = 0;
  toets(!a.configs->select(a.pl, idMono) && a.uitKanalen() == 2, "select terwijl actief geeft false (niets veranderd)");
  toets(!a.configureerbaar->can_apply_configuration(a.pl, &r, 1) && !a.configureerbaar->apply_configuration(a.pl, &r, 1) &&
            a.uitKanalen() == 2,
        "configurable terwijl actief geeft false (niets veranderd)");
  a.deactiveer();
  toets(a.configs->select(a.pl, idMono) && a.uitKanalen() == 1, "na deactivate kan select weer");
}

// --- samplerates × blokgroottes, stereo en mono ---------------------------------------------------------------------
const double SRS[] = {44100, 48000, 88200, 96000, 176400, 192000};
const std::vector<uint32_t> WISSELEND = {3, 128, 1, 4096, 77};

void samplerateToetsen() {
  std::printf("\n# Samplerates 44,1–192 kHz × blokgrootte 1, 4096 en wisselend, stereo en mono\n");
  for (double sr : SRS) {
    const uint64_t n = (uint64_t)(0.6 * sr);
    std::vector<std::vector<float>> stereo;
    for (uint32_t kan : {2u, 1u}) {
      std::vector<std::vector<float>> eerste;
      for (const auto& blokken : {std::vector<uint32_t>{1}, std::vector<uint32_t>{4096}, WISSELEND}) {
        Instantie a;
        if (!a.kies(kan)) stop("config kiezen mislukt");
        a.activeer(sr, *std::max_element(blokken.begin(), blokken.end()));
        auto u = a.render(n, blokken, spel(n));
        char b[160];
        const std::string g = gezond(u);
        const std::string blok = blokken.size() > 1 ? "wisselend" : std::to_string(blokken[0]);
        std::snprintf(b, sizeof b, "%.0f Hz, %s, blok %s: %u kanaal/kanalen, %s", sr, naamVan(kan), blok.c_str(),
                      (unsigned)u.size(), g.empty() ? "klinkt, eindig, geen subnormale float" : g.c_str());
        toets(u.size() == kan && g.empty(), b);
        if (eerste.empty()) {
          eerste = u;
        } else {
          std::snprintf(b, sizeof b, "%.0f Hz, %s, blok %s: bit voor bit gelijk aan blok 1", sr, naamVan(kan), blok.c_str());
          toets(bitGelijk(u, eerste), b);
        }
      }
      if (kan == 2) {
        stereo = eerste;
      } else {
        char b[160];
        std::snprintf(b, sizeof b, "%.0f Hz: mono is bit voor bit het linkerkanaal van stereo", sr);
        toets(eerste.size() == 1 && stereo.size() == 2 && bitGelijk(eerste[0], stereo[0]), b);
      }
    }
  }
}

// --- bypass ------------------------------------------------------------------------------------------------------------
void bypassToetsen() {
  std::printf("\n# Bypass (CLAP_PARAM_IS_BYPASS: de DSP rekent door, 10 ms overvloeien naar stilte, pb::uitvoer)\n");
  {
    Instantie a;
    bool gevonden = false;
    for (uint32_t i = 0; i < a.params->count(a.pl); ++i) {
      clap_param_info_t info{};
      if (!a.params->get_info(a.pl, i, &info) || !(info.flags & CLAP_PARAM_IS_BYPASS)) continue;
      gevonden = true;
      toets(info.id == BYPASS_ID, "de bypass-parameter heeft id 1000");
      toets(info.min_value == 0 && info.max_value == 1 && info.default_value == 0, "bypass: 0..1, standaard 0");
      toets((info.flags & CLAP_PARAM_IS_STEPPED) && (info.flags & CLAP_PARAM_IS_AUTOMATABLE),
            "bypass: CLAP_PARAM_IS_STEPPED en CLAP_PARAM_IS_AUTOMATABLE");
    }
    toets(gevonden, "er is een parameter met CLAP_PARAM_IS_BYPASS");
    char t[64];
    toets(a.params->value_to_text(a.pl, BYPASS_ID, 0, t, sizeof t) && !std::strcmp(t, "Uit"), "bypass: 0 is \"Uit\"");
    toets(a.params->value_to_text(a.pl, BYPASS_ID, 1, t, sizeof t) && !std::strcmp(t, "Aan"), "bypass: 1 is \"Aan\"");
    double v = -1;
    toets(a.params->text_to_value(a.pl, BYPASS_ID, "Aan", &v) && v == 1, "bypass: \"Aan\" is 1");
    toets(a.params->text_to_value(a.pl, BYPASS_ID, "uit", &v) && v == 0, "bypass: \"uit\" is 0");
    toets(a.params->text_to_value(a.pl, BYPASS_ID, "1", &v) && v == 1, "bypass: \"1\" is 1");
    toets(!a.params->text_to_value(a.pl, BYPASS_ID, "misschien", &v), "bypass: onleesbaar geeft false");
  }

  // de render: vóór de bypass gelijk, 10 ms overvloeien, stilte, 10 ms terug, daarna weer gelijk (de DSP rekende door)
  for (double sr : SRS) {
    for (uint32_t kan : {2u, 1u}) {
      const uint64_t n = (uint64_t)(0.6 * sr), A = n / 5 + 3, B = n / 2 + 5;
      Instantie ref, a;
      ref.kies(kan);
      a.kies(kan);
      ref.activeer(sr, 4096);
      a.activeer(sr, 4096);
      const auto r = ref.render(n, WISSELEND, spel(n));
      const auto u = a.render(n, WISSELEND, metBypass(spel(n), A, B));
      // verwacht: dezelfde regel als de schil, uit de render zonder bypass
      const uint32_t L = (uint32_t)std::floor(0.010 * sr + 0.5);
      std::vector<std::vector<float>> v = r;
      uint32_t pos = 0;
      uint64_t stilVan = 0, stilTot = 0, terugTot = 0;
      for (uint64_t s = 0; s < n; ++s) {
        const bool doel = s >= A && s < B;
        if (!doel && pos == 0) continue;
        if (doel) {
          if (pos < L) ++pos;
        } else {
          --pos;
        }
        if (pos == L && !stilVan) stilVan = s;
        if (pos == L) stilTot = s + 1;
        if (!doel && pos == 0 && !terugTot) terugTot = s + 1;
        const double droog = (double)pos / (double)L, nat = 1.0 - droog;
        for (uint32_t c = 0; c < kan; ++c) v[c][s] = uitvoer((double)r[c][s] * nat + 0.0 * droog);
      }
      char b[200];
      std::snprintf(b, sizeof b, "%.0f Hz, %s: bypass aan op sample %llu, uit op %llu: bit voor bit volgens de regel",
                    sr, naamVan(kan), (unsigned long long)A, (unsigned long long)B);
      toets(u.size() == kan && bitGelijk(u, v), b);
      bool voor = true, stil = true, na = true, klinktVoor = false, klinktNa = false;
      for (uint32_t c = 0; c < u.size() && c < kan; ++c)
        for (uint64_t s = 0; s < n; ++s) {
          if (s < A) { voor = voor && u[c][s] == r[c][s]; klinktVoor = klinktVoor || r[c][s] != 0; }
          if (s >= stilVan && s < stilTot) stil = stil && u[c][s] == 0.0f;
          if (s >= terugTot) { na = na && u[c][s] == r[c][s]; klinktNa = klinktNa || r[c][s] != 0; }
        }
      std::snprintf(b, sizeof b, "%.0f Hz, %s: overvloeien in %u samples (round(0,01 · sr)), dan exact stil tot de bypass "
                    "uitgaat; vóór de bypass en %u samples erna bit voor bit gelijk aan zonder bypass",
                    sr, naamVan(kan), L, L);
      toets(voor && klinktVoor && stil && na && klinktNa && stilVan == A + L - 1 && terugTot == B + L, b);
      const std::string g = gezond(u);
      std::snprintf(b, sizeof b, "%.0f Hz, %s: met bypass eindig en geen subnormale float%s", sr, naamVan(kan),
                    g.empty() || g == "alleen stilte" ? "" : (" (" + g + ")").c_str());
      toets(g.empty(), b);
    }
  }

  {  // bypass vóór activate: stil vanaf de eerste sample; get_value; state bewaart hem
    Instantie a;
    a.flush(BYPASS_ID, 1.0);
    toets(a.waarde(BYPASS_ID) == 1.0, "bypass via flush (inactief): get_value geeft 1");
    const Blob s = a.bewaar();
    a.activeer(48000, 512);
    const auto u = a.render(48000, {512}, spel(48000));
    bool stil = true;
    for (auto& k : u)
      for (float x : k) stil = stil && x == 0.0f;
    toets(stil, "bypass vóór activate: stil vanaf de eerste sample (geen overgang bij de start)");
    Instantie b;
    toets(b.laad(s) && b.waarde(BYPASS_ID) == 1.0, "state: save → load houdt de bypass (1)");
    // een state zonder bypass (zoals fase 4 hem schreef): niet omzeild
    Blob oud(s.begin(), s.end());
    // "PBST", versie, aantal; de bypass staat als laatste: weghalen en het aantal één minder
    uint32_t aantal = 0;
    for (int k = 0; k < 4; ++k) aantal |= (uint32_t)oud[8 + k] << (8 * k);
    bool laatsteIsBypass = false;
    if (oud.size() == 12 + 12 * (size_t)aantal && aantal > 0) {
      uint32_t id = 0;
      for (int k = 0; k < 4; ++k) id |= (uint32_t)oud[12 + 12 * (aantal - 1) + k] << (8 * k);
      laatsteIsBypass = id == BYPASS_ID;
      oud.resize(oud.size() - 12);
      --aantal;
      for (int k = 0; k < 4; ++k) oud[8 + k] = (unsigned char)(aantal >> (8 * k));
    }
    toets(laatsteIsBypass, "state: de bypass staat als laatste id in de blob");
    Instantie c;
    c.flush(BYPASS_ID, 1.0);
    toets(c.laad(oud) && c.waarde(BYPASS_ID) == 0.0, "state zonder bypass-id: laadt als niet omzeild (0)");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) stop("gebruik: pb-formaattoets <plugin.clap>");
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

  indelingToetsen();
  samplerateToetsen();
  bypassToetsen();

  entry->deinit();
  std::printf("\npb-formaattoets: %d toetsen, %d ok, %d fout\n", goed + fouten, goed, fouten);
  return fouten == 0 ? 0 : 1;
}
