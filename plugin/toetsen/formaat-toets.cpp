// formaat-toets — de eigen toets van fase 5, als kleine CLAP-host (zoals param-toets): poortindelingen en bypass.
//
//   formaat-toets <plugin.clap>
//
// 1. audio-ports-config (+ -info) en configurable-audio-ports: drie indelingen (stereo, mono, mono -> stereo), kiezen
//    alleen als de plugin niet actief is, audio-ports volgt de keuze, een onmogelijke vraag wordt geweigerd.
// 2. bypass (id 9, CLAP_PARAM_IS_BYPASS) in elke indeling (mono -> stereo in-place: de invoer is ook uitgang 0):
//    aan vanaf het begin (vóór activate, of als event in het eerste blok) = de invoer bit voor bit, staart 0; aan en
//    uit midden in het renderen (als event in process
//    en via params.flush): ervoor bit voor bit de DSP, na de overgang bit voor bit de invoer, na het uitzetten weer bit
//    voor bit de DSP (die doorrekende); tijdens de overgang ligt elke sample tussen nat en droog.
// 3. bypass in de state; een state zonder id 9 (fase 4) zet hem uit; een preset laat hem staan.
// 4. activate bij 44,1..192 kHz met blokken van 1 tot 4096 samples: eindige uitvoer, staart in seconden gelijk.
// Exitcode 0 = geslaagd; elke fout staat in de uitvoer met "FOUT". Met -fsanitize=realtime lopen process en flush in
// een real-time context.
#include <clap/clap.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
void eis(bool b, const std::string& t) { b ? ok(t) : fout(t); }

const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "formaat-toets", "Pluginbouwer", "https://varve.nl", "0.1",
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

constexpr clap_id BYPASS = 9;

struct Indeling { const char* naam; uint32_t in, uit; bool inPlace; };
// mono -> stereo in-place: het ene invoerkanaal is ook uitgang 0 (de valkuil uit PROMPTS.md fase 5)
const Indeling INDELINGEN[] = {{"stereo", 2, 2, false}, {"stereo in-place", 2, 2, true}, {"mono", 1, 1, true},
                               {"mono naar stereo, in-place", 1, 2, true}, {"mono naar stereo", 1, 2, false}};

using Kanalen = std::vector<std::vector<float>>;

struct Instantie {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  const clap_plugin_state_t* st = nullptr;
  const clap_plugin_audio_ports_t* ap = nullptr;
  const clap_plugin_audio_ports_config_t* cfg = nullptr;
  const clap_plugin_audio_ports_config_info_t* cfgInfo = nullptr;
  const clap_plugin_configurable_audio_ports_t* conf = nullptr;
  const clap_plugin_tail_t* tail = nullptr;
  bool actief = false;
  double sr = 48000;
  explicit Instantie(const Bibliotheek& b) {
    pl = b.fab->create_plugin(b.fab, &HOST, b.id.c_str());
    if (!pl || !pl->init(pl)) stop("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    st = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
    ap = static_cast<const clap_plugin_audio_ports_t*>(pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS));
    cfg = static_cast<const clap_plugin_audio_ports_config_t*>(pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS_CONFIG));
    cfgInfo = static_cast<const clap_plugin_audio_ports_config_info_t*>(
        pl->get_extension(pl, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO));
    conf = static_cast<const clap_plugin_configurable_audio_ports_t*>(
        pl->get_extension(pl, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS));
    tail = static_cast<const clap_plugin_tail_t*>(pl->get_extension(pl, CLAP_EXT_TAIL));
    if (!pp || !st || !ap || !tail) stop("geen params-, state-, audio-ports- of tail-extensie");
    if (!cfg || !cfgInfo || !conf) stop("geen audio-ports-config, audio-ports-config-info of configurable-audio-ports");
  }
  ~Instantie() {
    if (actief) { pl->stop_processing(pl); pl->deactivate(pl); }
    pl->destroy(pl);
  }
  bool kies(uint32_t in, uint32_t uit) {
    for (uint32_t k = 0; k < cfg->count(pl); ++k) {
      clap_audio_ports_config_t c{};
      if (cfg->get(pl, k, &c) && c.main_input_channel_count == in && c.main_output_channel_count == uit)
        return cfg->select(pl, c.id);
    }
    return false;
  }
  void flush(clap_id id, double v) {
    Events ev;
    ev.ev.push_back(maakEvent(0, id, v));
    clap_input_events_t in = {&ev, evSize, evGet};
    clap_output_events_t uit = {nullptr, evPush};
    if (actief) doeFlushAudio(pp, pl, &in, &uit);
    else pp->flush(pl, &in, &uit);
  }
  double waarde(clap_id id) const { double v = -1; pp->get_value(pl, id, &v); return v; }
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
  void activeer(double s, uint32_t maxBlok) {
    sr = s;
    if (!pl->activate(pl, s, 1, maxBlok)) stop("activate mislukt");
    if (!pl->start_processing(pl)) stop("start_processing mislukt");
    actief = true;
  }
  void deactiveer() {
    if (!actief) return;
    pl->stop_processing(pl);
    pl->deactivate(pl);
    actief = false;
  }
};

// vast testsignaal: tikken, ruisstoten en een glijdende sinus, twee verschillende kanalen
Kanalen testsignaal(size_t n, double sr) {
  Kanalen s(2, std::vector<float>(n, 0.0f));
  uint32_t r = 4242;
  double fase = 0;
  for (size_t i = 0; i < n; ++i) {
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    const double ruis = ((double)(r >> 8) / 8388608.0) - 1.0;
    const size_t p = i % 24000;
    const double stoot = p < 2400 ? 0.5 * ruis * std::exp(-(double)p / 600.0) : 0.0;
    fase += 2 * M_PI * (220.0 + 200.0 * (double)i / sr) / sr;
    s[0][i] = (float)(stoot + 0.25 * std::sin(fase) + ((i % 9600) == 0 ? 0.8 : 0.0));
    s[1][i] = (float)(0.7 * stoot + 0.2 * std::sin(1.5 * fase));
  }
  return s;
}

struct Auto { uint32_t sample; clap_id id; double waarde; };
enum class Route { InProcess, Flush };

// Rendert `in` (kanaal 0 en, bij 2 invoerkanalen, kanaal 1) door een actieve instantie in de gekozen indeling.
Kanalen render(Instantie& ins, const Indeling& d, const Kanalen& in, const std::vector<uint32_t>& blokken,
               std::vector<Auto> aut, Route r) {
  uint32_t maxBlok = 1;
  for (auto b : blokken) maxBlok = b > maxBlok ? b : maxBlok;
  if (!ins.actief) ins.activeer(ins.sr, maxBlok);
  const size_t n = in[0].size();
  Kanalen uit(d.uit, std::vector<float>(n));
  std::vector<std::vector<float>> bi(d.in, std::vector<float>(maxBlok)), bu(d.uit, std::vector<float>(maxBlok));
  std::vector<float*> pi(d.in), pu(d.uit);
  for (uint32_t c = 0; c < d.uit; ++c) pu[c] = bu[c].data();
  for (uint32_t c = 0; c < d.in; ++c) pi[c] = d.inPlace ? pu[c] : bi[c].data();
  clap_output_events_t uitEv = {nullptr, evPush};
  size_t pos = 0, bnr = 0, a = 0;
  int64_t steady = 0;
  while (pos < n) {
    uint32_t m = blokken[bnr++ % blokken.size()];
    if (pos + m > n) m = (uint32_t)(n - pos);
    Events ev;
    if (r == Route::Flush) {
      while (a < aut.size() && aut[a].sample <= pos) { ins.flush(aut[a].id, aut[a].waarde); ++a; }
      if (a < aut.size() && aut[a].sample < pos + m) m = (uint32_t)(aut[a].sample - pos);
    } else {
      while (a < aut.size() && aut[a].sample < pos + m) {
        ev.ev.push_back(maakEvent(aut[a].sample > pos ? (uint32_t)(aut[a].sample - pos) : 0, aut[a].id, aut[a].waarde));
        ++a;
      }
    }
    clap_input_events_t inEv = {&ev, evSize, evGet};
    for (uint32_t c = 0; c < d.in; ++c) std::memcpy(pi[c], &in[c][pos], m * 4);
    if (!d.inPlace) for (uint32_t c = 0; c < d.uit; ++c) std::memset(pu[c], 0x7f, m * 4);  // rommel: moet overschreven
    clap_audio_buffer_t ai = {pi.data(), nullptr, d.in, 0, 0}, au = {pu.data(), nullptr, d.uit, 0, 0};
    clap_process_t pr = {steady, m, nullptr, &ai, &au, 1, 1, &inEv, &uitEv};
    if (doeProcess(ins.pl, &pr) == CLAP_PROCESS_ERROR) stop("process gaf een fout");
    for (uint32_t c = 0; c < d.uit; ++c) std::memcpy(&uit[c][pos], pu[c], m * 4);
    pos += m;
    steady += m;
  }
  return uit;
}

// het droge signaal per uitgang: bij mono -> stereo het ene invoerkanaal op beide
Kanalen droog(const Indeling& d, const Kanalen& in) {
  Kanalen r(d.uit);
  for (uint32_t c = 0; c < d.uit; ++c) r[c] = in[c < d.in ? c : 0];
  return r;
}
bool gelijk(float a, float b) { return !std::memcmp(&a, &b, 4); }
// aantal samples in [van, tot) dat niet bit voor bit gelijk is
size_t verschil(const Kanalen& a, const Kanalen& b, size_t van, size_t tot) {
  size_t n = 0;
  for (size_t c = 0; c < a.size(); ++c)
    for (size_t i = van; i < tot && i < a[c].size(); ++i) n += gelijk(a[c][i], b[c][i]) ? 0 : 1;
  return n;
}
bool eindig(const Kanalen& a) {
  for (auto& k : a) for (float x : k) if (!std::isfinite(x)) return false;
  return true;
}

// --- 1. indelingen -------------------------------------------------------------------------------------------------
void toetsIndelingen(const Bibliotheek& b) {
  std::printf("== poortindelingen (audio-ports-config, configurable-audio-ports)\n");
  Instantie ins(b);
  const uint32_t n = ins.cfg->count(ins.pl);
  bool goed = n == 3;
  const uint32_t verwachtIn[] = {2, 1, 1}, verwachtUit[] = {2, 1, 2};
  for (uint32_t k = 0; goed && k < n; ++k) {
    clap_audio_ports_config_t c{};
    goed = ins.cfg->get(ins.pl, k, &c) && c.id == k && c.input_port_count == 1 && c.output_port_count == 1 &&
           c.has_main_input && c.has_main_output && c.main_input_channel_count == verwachtIn[k] &&
           c.main_output_channel_count == verwachtUit[k] &&
           !std::strcmp(c.main_input_port_type, verwachtIn[k] == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO) &&
           !std::strcmp(c.main_output_port_type, verwachtUit[k] == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO);
    if (goed) std::printf("   indeling %u: %s (%u in, %u uit)\n", c.id, c.name, c.main_input_channel_count, c.main_output_channel_count);
    for (int io = 0; goed && io < 2; ++io) {
      clap_audio_port_info_t pi{};
      goed = ins.cfgInfo->get(ins.pl, c.id, 0, io == 0, &pi) &&
             pi.channel_count == (io == 0 ? c.main_input_channel_count : c.main_output_channel_count) &&
             (pi.flags & CLAP_AUDIO_PORT_IS_MAIN);
    }
  }
  eis(goed, "drie indelingen: stereo (2 -> 2), mono (1 -> 1), mono naar stereo (1 -> 2); config-info klopt");
  clap_audio_port_info_t i0{}, u0{};
  ins.ap->get(ins.pl, 0, true, &i0);
  ins.ap->get(ins.pl, 0, false, &u0);
  eis(ins.cfgInfo->current_config(ins.pl) == 0 && i0.channel_count == 2 && u0.channel_count == 2 && i0.in_place_pair == 0,
      "standaard: stereo, in-place met elkaar");
  for (uint32_t k : {1u, 2u, 0u}) {
    const bool gekozen = ins.cfg->select(ins.pl, k);
    ins.ap->get(ins.pl, 0, true, &i0);
    ins.ap->get(ins.pl, 0, false, &u0);
    eis(gekozen && ins.cfgInfo->current_config(ins.pl) == k && i0.channel_count == verwachtIn[k] &&
            u0.channel_count == verwachtUit[k] && i0.in_place_pair == (verwachtIn[k] == verwachtUit[k] ? 0u : CLAP_INVALID_ID),
        "select(" + std::to_string(k) + "): audio-ports volgt (" + std::to_string(i0.channel_count) + " -> " +
            std::to_string(u0.channel_count) + ")");
  }
  eis(!ins.cfg->select(ins.pl, 3) && !ins.cfg->select(ins.pl, CLAP_INVALID_ID), "select met een onbekend id: false");
  // configurable-audio-ports
  auto vraag = [](bool in, uint32_t k, const char* soort) {
    return clap_audio_port_configuration_request_t{in, 0, k, soort, nullptr};
  };
  {
    clap_audio_port_configuration_request_t mono[] = {vraag(true, 1, CLAP_PORT_MONO), vraag(false, 1, CLAP_PORT_MONO)};
    clap_audio_port_configuration_request_t ms[] = {vraag(true, 1, nullptr), vraag(false, 2, nullptr)};
    clap_audio_port_configuration_request_t sm[] = {vraag(true, 2, CLAP_PORT_STEREO), vraag(false, 1, CLAP_PORT_MONO)};
    clap_audio_port_configuration_request_t drie[] = {vraag(true, 3, nullptr), vraag(false, 3, nullptr)};
    clap_audio_port_configuration_request_t fout_type[] = {vraag(true, 2, CLAP_PORT_MONO)};
    clap_audio_port_configuration_request_t poort1[] = {{true, 1, 2, nullptr, nullptr}};
    clap_audio_port_configuration_request_t st[] = {vraag(true, 2, CLAP_PORT_STEREO), vraag(false, 2, CLAP_PORT_STEREO)};
    eis(ins.conf->can_apply_configuration(ins.pl, mono, 2) && ins.conf->apply_configuration(ins.pl, mono, 2) &&
            ins.cfgInfo->current_config(ins.pl) == 1,
        "configurable-audio-ports: mono -> mono");
    eis(ins.conf->apply_configuration(ins.pl, ms, 2) && ins.cfgInfo->current_config(ins.pl) == 2,
        "configurable-audio-ports: mono -> stereo");
    eis(!ins.conf->can_apply_configuration(ins.pl, sm, 2) && !ins.conf->apply_configuration(ins.pl, sm, 2) &&
            !ins.conf->apply_configuration(ins.pl, drie, 2) && !ins.conf->apply_configuration(ins.pl, fout_type, 1) &&
            !ins.conf->apply_configuration(ins.pl, poort1, 1) && ins.cfgInfo->current_config(ins.pl) == 2,
        "configurable-audio-ports weigert stereo -> mono, 3 kanalen, een verkeerd port_type en poort 1 (indeling blijft)");
    eis(ins.conf->apply_configuration(ins.pl, st, 2) && ins.cfgInfo->current_config(ins.pl) == 0,
        "configurable-audio-ports: terug naar stereo");
    ins.activeer(48000, 512);
    eis(!ins.cfg->select(ins.pl, 1) && !ins.conf->can_apply_configuration(ins.pl, mono, 2) &&
            !ins.conf->apply_configuration(ins.pl, mono, 2) && ins.cfgInfo->current_config(ins.pl) == 0,
        "terwijl de plugin actief is: kiezen geweigerd (CLAP: alleen als hij niet actief is)");
  }
}

// --- 2. bypass ---------------------------------------------------------------------------------------------------
void toetsBypass(const Bibliotheek& b) {
  std::printf("== bypass\n");
  {
    Instantie ins(b);
    clap_param_info_t info{};
    bool gevonden = false;
    for (uint32_t k = 0; k < ins.pp->count(ins.pl) && !gevonden; ++k)
      if (ins.pp->get_info(ins.pl, k, &info) && (info.flags & CLAP_PARAM_IS_BYPASS)) gevonden = true;
    eis(gevonden && info.id == BYPASS && info.min_value == 0 && info.max_value == 1 && info.default_value == 0 &&
            (info.flags & CLAP_PARAM_IS_STEPPED) && (info.flags & CLAP_PARAM_IS_AUTOMATABLE),
        "een parameter met CLAP_PARAM_IS_BYPASS: id 9, 0..1, standaard 0 (uit), getrapt, automatiseerbaar");
  }
  const double SR = 48000;
  const size_t N = (size_t)SR * 3;
  const Kanalen in = testsignaal(N, SR);
  const uint32_t AAN = 30011, UIT = 90007, OVERGANG = (uint32_t)std::ceil(0.010 * SR) + 2;
  const std::vector<uint32_t> blok = {512}, wisselend = {3, 128, 1, 4096, 77};
  for (const Indeling& d : INDELINGEN) {
    const Kanalen dr = droog(d, in);
    Kanalen nat;
    {
      Instantie ins(b);
      if (!ins.kies(d.in, d.uit)) stop(std::string("indeling ") + d.naam + " niet te kiezen");
      nat = render(ins, d, in, blok, {}, Route::InProcess);
    }
    if (!eindig(nat) || verschil(nat, dr, 0, N) < N / 2) fout(std::string(d.naam) + ": de DSP doet niets (de toets toetst dan niets)");
    // aan vanaf het begin (vóór activate gezet)
    {
      Instantie ins(b);
      ins.kies(d.in, d.uit);
      ins.flush(BYPASS, 1);
      const Kanalen u = render(ins, d, in, wisselend, {}, Route::InProcess);
      const uint32_t staart = ins.tail->get(ins.pl);
      const size_t v = verschil(u, dr, 0, N);
      eis(v == 0 && staart == 0, std::string(d.naam) + ": bypass aan vóór activate = de invoer, bit voor bit (" +
                                     std::to_string(v) + " verschillend), staart " + std::to_string(staart));
    }
    // aan als event op sample 0 van het eerste blok (zoals JUCE een parameter uit de state doorgeeft): geen overgang
    {
      Instantie ins(b);
      ins.kies(d.in, d.uit);
      const Kanalen u = render(ins, d, in, blok, {{0, BYPASS, 1}}, Route::InProcess);
      const size_t v = verschil(u, dr, 0, N);
      eis(v == 0, std::string(d.naam) + ": bypass als event op sample 0 van het eerste blok = de invoer vanaf sample 0 (" +
                      std::to_string(v) + " verschillend)");
    }
    // aan en weer uit midden in het renderen; als event in process en via flush
    Kanalen ref;
    for (Route r : {Route::InProcess, Route::Flush}) {
      Instantie ins(b);
      ins.kies(d.in, d.uit);
      const Kanalen u = render(ins, d, in, r == Route::InProcess ? wisselend : blok, {{AAN, BYPASS, 1}, {UIT, BYPASS, 0}}, r);
      const std::string wie = std::string(d.naam) + (r == Route::InProcess ? ", event in process" : ", via flush");
      const size_t voor = verschil(u, nat, 0, AAN), tijdens = verschil(u, dr, AAN + OVERGANG, UIT),
                   na = verschil(u, nat, UIT + OVERGANG, N);
      bool tussen = true, glijdt = false;
      for (size_t c = 0; c < u.size(); ++c)
        for (size_t i = AAN; i < AAN + OVERGANG; ++i) {
          const float lo = std::fmin(nat[c][i], dr[c][i]), hi = std::fmax(nat[c][i], dr[c][i]);
          const float marge = 1e-6f * (1 + std::fabs(hi));
          if (u[c][i] < lo - marge || u[c][i] > hi + marge) tussen = false;
          if (!gelijk(u[c][i], nat[c][i]) && !gelijk(u[c][i], dr[c][i])) glijdt = true;
        }
      eis(voor == 0 && tijdens == 0 && na == 0 && tussen && glijdt,
          wie + ": vóór sample " + std::to_string(AAN) + " de DSP, na " + std::to_string(OVERGANG) +
              " samples overgang de invoer, na het uitzetten weer de DSP; alles bit voor bit (" + std::to_string(voor) +
              "/" + std::to_string(tijdens) + "/" + std::to_string(na) + " verschillend); de overgang glijdt tussen nat en droog");
      if (r == Route::InProcess) ref = u;
      else eis(verschil(u, ref, 0, N) == 0, wie + ": gelijk aan dezelfde bypass als event in process");
      eis(ins.waarde(BYPASS) == 0, wie + ": get_value volgt (weer uit)");
    }
  }
}

// --- 3. state en presets -----------------------------------------------------------------------------------------
void toetsState(const Bibliotheek& b) {
  std::printf("== bypass in de state\n");
  const Indeling& d = INDELINGEN[0];
  const Kanalen in = testsignaal(48000, 48000);
  Buf st;
  {
    Instantie ins(b);
    ins.flush(BYPASS, 1);
    st = ins.bewaar();
  }
  {
    Instantie ins(b);
    eis(ins.laad(st) && ins.waarde(BYPASS) == 1, "bypass aan bewaard en geladen: get_value 1");
    eis(verschil(render(ins, d, in, {512}, {}, Route::InProcess), droog(d, in), 0, in[0].size()) == 0,
        "na laden klinkt hij als de invoer");
  }
  {
    Instantie ins(b);
    ins.activeer(48000, 512);
    ins.flush(BYPASS, 0);
    const uint32_t overgang = (uint32_t)std::ceil(0.010 * 48000) + 2;
    eis(ins.laad(st) && verschil(render(ins, d, in, {512}, {}, Route::InProcess), droog(d, in), overgang, in[0].size()) == 0,
        "laden terwijl actief: de audiothread neemt de bypass over (na de overgang van 10 ms de invoer)");
  }
  {  // een state van fase 4 (zonder id 9): "PBST", versie 1, 1 parameter (Mix = 0,5)
    Buf oud;
    const unsigned char kop[] = {'P', 'B', 'S', 'T', 1, 0, 0, 0, 1, 0, 0, 0, 8, 0, 0, 0};
    oud.d.assign(kop, kop + sizeof kop);
    const double mix = 0.5;
    unsigned char w[8];
    std::memcpy(w, &mix, 8);
    oud.d.insert(oud.d.end(), w, w + 8);
    Instantie ins(b);
    ins.flush(BYPASS, 1);
    eis(ins.laad(oud) && ins.waarde(BYPASS) == 0 && ins.waarde(8) == 0.5, "een state zonder id 9 (fase 4): bypass uit");
  }
  {
    Instantie ins(b);
    auto* pr = static_cast<const clap_plugin_preset_load_t*>(ins.pl->get_extension(ins.pl, CLAP_EXT_PRESET_LOAD));
    ins.flush(BYPASS, 1);
    eis(pr && pr->from_location(ins.pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "slapback") &&
            ins.waarde(BYPASS) == 1,
        "een fabriekspreset laat de bypass staan");
  }
}

// --- 4. samplerates en blokgroottes -----------------------------------------------------------------------------
void toetsSamplerates(const Bibliotheek& b) {
  std::printf("== samplerates 44,1..192 kHz, blokken 1..4096\n");
  double staartSec = -1;
  bool goed = true;
  for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
    for (uint32_t maxBlok : {1u, 64u, 4096u}) {
      for (const Indeling& d : {INDELINGEN[1], INDELINGEN[3]}) {
        Instantie ins(b);
        ins.kies(d.in, d.uit);
        ins.flush(2, 0.7);  // terugkoppeling 0,7: een eindige staart
        ins.sr = sr;
        const size_t n = (size_t)(sr / 4);
        std::vector<uint32_t> blokken = {maxBlok};
        if (maxBlok > 1) blokken = {maxBlok, 1, maxBlok / 2 + 1, 3};
        const Kanalen u = render(ins, d, testsignaal(n, sr), blokken, {}, Route::InProcess);
        double e = 0;
        for (auto& k : u) for (float x : k) e += (double)x * x;
        const double sec = ins.tail->get(ins.pl) / sr;
        if (staartSec < 0) staartSec = sec;
        if (!eindig(u) || e <= 0 || std::fabs(sec - staartSec) > 2.0 / sr + 1e-9) {
          goed = false;
          std::printf("   %g Hz, blok tot %u, %s: eindig %d, energie %g, staart %.6f s\n", sr, maxBlok, d.naam, eindig(u), e, sec);
        }
      }
    }
  }
  eis(goed, "activate bij 44,1/48/88,2/96/176,4/192 kHz met maxblok 1, 64 en 4096 (stereo in-place en mono naar stereo "
            "in-place): eindige uitvoer, staart in seconden overal gelijk (" + std::to_string(staartSec) + " s)");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "gebruik: formaat-toets <plugin.clap>\n");
    return 2;
  }
  Bibliotheek b(argv[1]);
  toetsIndelingen(b);
  toetsBypass(b);
  toetsState(b);
  toetsSamplerates(b);
  std::printf(g_fouten == 0 ? "GESLAAGD\n" : "MISLUKT (%d fouten)\n", g_fouten);
  return g_fouten ? 1 : 0;
}
