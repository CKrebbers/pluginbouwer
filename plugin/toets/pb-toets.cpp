// pb-toets — de eigen toetsen van fase 2, een kleine CLAP-host naast gereedschap/pb-render.
//
//   pb-toets state <oud.clap> <nieuw.clap> <id>
//       <oud.clap> is een build die parameter <id> niet kent (build/zonder/zonder-<id>.clap), <nieuw.clap> de echte.
//       Vooruit-compatibel: state van oud laadt in nieuw; de waarden die oud kende komen exact terug, <id> krijgt zijn
//       standaard, en nieuw klinkt daarna bit voor bit als nieuw met dezelfde waarden via flush (ook als de state
//       komt terwijl nieuw al actief is). Ook achteruit (state van nieuw in oud: onbekend id overgeslagen), een
//       toekomstige versie met een onbekend id, en kapotte state (geweigerd, niets veranderd).
//
//   pb-toets render <plugin.clap> <in.wav> <uit.wav> [--p id=waarde ...]
//       Zoals pb-render, maar de waarden komen als automatisering: CLAP-events midden in de blokken (wisselende
//       blokgroottes), steeds opnieuw met dezelfde waarde, zoals een DAW die een automatiseringsspoor afspeelt. De
//       uitvoer moet de null-test halen (gereedschap/vergelijk.mjs), net als die van pb-render.
//
//   pb-toets tekst <plugin.clap>
//       Tekst ↔ waarde in gewone eenheden: bekende paren (375 → "375 ms", "1,2 s" → 1200, "3k" → 3000 Hz, ...),
//       tekst → waarde → tekst geeft dezelfde tekst over het hele bereik, en dat alles ook onder een locale met een
//       decimale komma (zoals een host die setlocale doet).
//
//   pb-toets automatisering <plugin.clap>
//       Echte automatisering (Tijd glijdt, Toon, Terugkoppeling, Mix springen, waarden buiten het bereik) als events
//       op willekeurige samples: de uitvoer is bit voor bit gelijk bij blok 512, wisselende blokken, blok 1, en bij
//       blokken die precies op elk event gesplitst zijn. Een event werkt dus op zijn sample, los van de blokindeling.
//
//   pb-toets presets <plugin.clap> <verwacht.tsv>
//       Fabriekspresets (fase 4): de preset-discovery-factory geeft één locatie (FILE, fabrieksinhoud, een map die
//       bestaat) met precies de presets uit <verwacht.tsv> (per regel: bestand, naam, dan param=waarde; gemaakt uit
//       presets/*.json door toets-fase4.sh), elk met de juiste naam, het plugin-id en zonder laadsleutel.
//       preset-load laadt elk bestand: elke waarde exact zoals in de JSON, wat de preset niet noemt op zijn standaard,
//       de host krijgt rescan en loaded, en de plugin klinkt daarna bit voor bit als dezelfde waarden via flush, ook als
//       de preset komt terwijl hij actief is. Ook onder een locale met een decimale komma. Een onbekend bestand of
//       soort locatie geeft false en on_error, en verandert niets.
//
// Exit 0 = geslaagd; anders een melding op stderr en exit 1 (toets mislukt) of 2 (gebruik/laden).
#include <clap/clap.h>
#include <clocale>
#include <dlfcn.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
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

[[noreturn]] void fout(const std::string& t) { std::fprintf(stderr, "pb-toets: %s\n", t.c_str()); std::exit(2); }
int mislukt = 0;
void eis(bool goed, const std::string& wat) {
  std::printf("  %s %s\n", goed ? "ok  " : "FOUT", wat.c_str());
  if (!goed) ++mislukt;
}

// --- WAV (32-bit float), zoals pb-render ---------------------------------------------------------------------
struct Wav { uint32_t sr = 48000; std::vector<std::vector<float>> k; };

Wav leesWav(const char* pad) {
  FILE* f = std::fopen(pad, "rb");
  if (!f) fout(std::string("kan niet lezen: ") + pad);
  std::vector<unsigned char> b;
  unsigned char buf[65536]; size_t r;
  while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + r);
  std::fclose(f);
  auto u16 = [&](size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) fout("geen WAV");
  size_t p = 12; uint32_t tag = 0, kan = 0, bits = 0, sr = 0; size_t dOff = 0, dLen = 0;
  while (p + 8 <= b.size()) {
    uint32_t len = u32(p + 4);
    if (!std::memcmp(&b[p], "fmt ", 4)) { tag = u16(p + 8); kan = u16(p + 10); sr = u32(p + 12); bits = u16(p + 22); }
    if (!std::memcmp(&b[p], "data", 4)) { dOff = p + 8; dLen = len; }
    p += 8 + len + (len & 1);
  }
  if (tag == 0xfffe) tag = 3;
  if (tag != 3 || bits != 32 || !kan || !dOff) fout("alleen 32-bit float WAV");
  Wav w; w.sr = sr; w.k.assign(kan, {});
  size_t n = dLen / (4 * kan);
  for (auto& c : w.k) c.resize(n);
  for (size_t i = 0; i < n; ++i)
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(&w.k[c][i], &b[dOff + (i * kan + c) * 4], 4);
  return w;
}

void schrijfWav(const char* pad, const Wav& w) {
  FILE* f = std::fopen(pad, "wb");
  if (!f) fout(std::string("kan niet schrijven: ") + pad);
  uint32_t kan = (uint32_t)w.k.size(), n = (uint32_t)w.k[0].size(), data = n * kan * 4;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); put32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(16); put16(3); put16((uint16_t)kan); put32(w.sr); put32(w.sr * kan * 4); put16((uint16_t)(kan * 4)); put16(32);
  std::fwrite("data", 1, 4, f); put32(data);
  for (uint32_t i = 0; i < n; ++i) for (uint32_t c = 0; c < kan; ++c) std::fwrite(&w.k[c][i], 4, 1, f);
  std::fclose(f);
}

// --- host: met params-extensie, zodat de plugin rescan en request_flush kan vragen ----------------------------
int rescans = 0, flushVragen = 0;
void hostRescan(const clap_host_t*, clap_param_rescan_flags) { ++rescans; }
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostRequestFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hostRescan, hostClear, hostRequestFlush};
int presetGeladen = 0, presetFouten = 0;
void hostPresetFout(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char* msg) {
  ++presetFouten;
  std::printf("    (on_error van de plugin: %s)\n", msg ? msg : "");
}
void hostPresetGeladen(const clap_host_t*, uint32_t, const char*, const char*) { ++presetGeladen; }
const clap_host_preset_load_t HOST_PRESET_LOAD = {hostPresetFout, hostPresetGeladen};
const void* hostExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return &HOST_PRESET_LOAD;
  return nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-toets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

struct Events { std::vector<clap_event_param_value_t> ev; };
uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)static_cast<const Events*>(l->ctx)->ev.size(); }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return &static_cast<const Events*>(l->ctx)->ev[i].header; }
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }
clap_event_param_value_t maakEvent(uint32_t tijd, clap_id id, double v) {
  clap_event_param_value_t e{};
  e.header = {sizeof(e), tijd, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.param_id = id; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = v;
  return e;
}

struct Buf { std::vector<unsigned char> d; size_t pos = 0; };
int64_t ostWrite(const clap_ostream_t* s, const void* p, uint64_t n) {
  auto* b = static_cast<Buf*>(s->ctx); auto* c = static_cast<const unsigned char*>(p); b->d.insert(b->d.end(), c, c + n); return (int64_t)n;
}
int64_t istRead(const clap_istream_t* s, void* p, uint64_t n) {
  auto* b = static_cast<Buf*>(s->ctx); uint64_t m = b->d.size() - b->pos; if (n < m) m = n;
  std::memcpy(p, b->d.data() + b->pos, m); b->pos += m; return (int64_t)m;
}

// noinline + nonblocking: met -fsanitize=realtime loopt elke process in een real-time context (zie pb-render)
__attribute__((noinline)) clap_process_status doeProcess(const clap_plugin_t* pl, const clap_process_t* pr) PB_RT { return pl->process(pl, pr); }

// --- een geladen .clap -------------------------------------------------------------------------------------------
struct Bib {
  void* h = nullptr;
  const clap_plugin_entry_t* entry = nullptr;
  const clap_plugin_factory_t* fab = nullptr;
  std::string id, pad;
};
Bib laad(const std::string& pad) {
  Bib b; b.pad = pad;
  std::string lib = pad;
  struct stat st{};
  if (stat(pad.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {  // macOS-bundle: Contents/MacOS/<naam zonder .clap>
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    if (naam.size() > 5 && naam.substr(naam.size() - 5) == ".clap") naam.resize(naam.size() - 5);
    lib = pad + "/Contents/MacOS/" + naam;
  }
  b.h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!b.h) fout(std::string("dlopen: ") + dlerror());
  b.entry = static_cast<const clap_plugin_entry_t*>(dlsym(b.h, "clap_entry"));
  if (!b.entry || !b.entry->init(pad.c_str())) fout("geen clap_entry in " + pad);
  b.fab = static_cast<const clap_plugin_factory_t*>(b.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!b.fab || b.fab->get_plugin_count(b.fab) < 1) fout("geen plugin in de factory van " + pad);
  b.id = b.fab->get_plugin_descriptor(b.fab, 0)->id;
  return b;
}
const clap_plugin_t* maak(const Bib& b) {
  const clap_plugin_t* pl = b.fab->create_plugin(b.fab, &HOST, b.id.c_str());
  if (!pl || !pl->init(pl)) fout("plugin start niet: " + b.pad);
  return pl;
}
const clap_plugin_params_t* params(const clap_plugin_t* pl) {
  auto* p = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
  if (!p) fout("geen params-extensie");
  return p;
}
const clap_plugin_state_t* stateExt(const clap_plugin_t* pl) {
  auto* s = static_cast<const clap_plugin_state_t*>(pl->get_extension(pl, CLAP_EXT_STATE));
  if (!s) fout("geen state-extensie");
  return s;
}
std::map<clap_id, clap_param_info_t> infos(const clap_plugin_t* pl) {
  std::map<clap_id, clap_param_info_t> m;
  auto* p = params(pl);
  for (uint32_t i = 0; i < p->count(pl); ++i) {
    clap_param_info_t info{};
    if (!p->get_info(pl, i, &info)) fout("get_info mislukt");
    m[info.id] = info;
  }
  return m;
}
void flush(const clap_plugin_t* pl, const std::vector<std::pair<clap_id, double>>& w) {
  Events ev;
  for (auto& [id, v] : w) ev.ev.push_back(maakEvent(0, id, v));
  clap_input_events_t in = {&ev, evSize, evGet};
  clap_output_events_t uit = {nullptr, evPush};
  params(pl)->flush(pl, &in, &uit);
}
std::vector<unsigned char> bewaar(const clap_plugin_t* pl) {
  Buf b; clap_ostream_t os = {&b, ostWrite};
  if (!stateExt(pl)->save(pl, &os)) fout("state save mislukt");
  return b.d;
}
bool zetTerug(const clap_plugin_t* pl, const std::vector<unsigned char>& d) {
  Buf b; b.d = d; clap_istream_t is = {&b, istRead};
  return stateExt(pl)->load(pl, &is);
}
std::map<clap_id, double> waarden(const clap_plugin_t* pl) {
  std::map<clap_id, double> m;
  auto* p = params(pl);
  for (auto& [id, info] : infos(pl)) {
    double v = NAN;
    if (!p->get_value(pl, id, &v)) fout("get_value mislukt");
    m[id] = v;
  }
  return m;
}
// de ids in een PBST-blob (kop: magie, versie, aantal; dan per parameter id u32 + waarde f64)
std::vector<clap_id> idsInState(const std::vector<unsigned char>& d) {
  std::vector<clap_id> ids;
  if (d.size() < 12) return ids;
  uint32_t n; std::memcpy(&n, &d[8], 4);
  for (uint32_t k = 0; k < n && 12 + k * 12 + 12 <= d.size(); ++k) { uint32_t id; std::memcpy(&id, &d[12 + k * 12], 4); ids.push_back(id); }
  return ids;
}

// testsignaal: tikken en ruisstoten, stereo, deterministisch
Wav testsignaal(uint32_t sr, size_t n) {
  Wav w; w.sr = sr; w.k.assign(2, std::vector<float>(n, 0.0f));
  uint32_t x = 0x9e3779b9u;
  for (size_t i = 0; i < n; ++i) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    const float ruis = ((float)(x >> 8) / 8388608.0f - 1.0f) * 0.25f;
    const bool stoot = (i % 24000) < 2400;
    w.k[0][i] = (i % 12000 == 0 ? 0.8f : 0.0f) + (stoot ? ruis : 0.0f);
    w.k[1][i] = (i % 12000 == 6000 ? 0.6f : 0.0f) + (stoot ? -ruis * 0.5f : 0.0f);
  }
  return w;
}

// render: blokken uit de lijst (rondgaand); events met een absolute sampletijd gaan in het blok waar ze vallen.
// flushNaActivate: waarden via params.flush na activate en vóór start_processing (actief, maar geen process).
// splitsOpEvents: elk blok eindigt vóór het volgende event, zodat elk event op tijd 0 van zijn blok staat.
// naActivate: een state die geladen wordt na activate en vóór de eerste process (de host laadt een project terwijl
// de plugin al actief is).
struct AbsEvent { uint64_t tijd; clap_id id; double v; };
Wav render(const clap_plugin_t* pl, const Wav& in, std::vector<uint32_t> blokken, std::vector<AbsEvent> events,
           bool splitsOpEvents = false, const std::vector<unsigned char>* naActivate = nullptr,
           const std::vector<std::pair<clap_id, double>>* flushNaActivate = nullptr,
           const std::function<void(const clap_plugin_t*)>& doeNaActivate = nullptr) {
  std::stable_sort(events.begin(), events.end(), [](const AbsEvent& a, const AbsEvent& b) { return a.tijd < b.tijd; });
  uint32_t maxBlok = 1; for (auto b : blokken) maxBlok = std::max(maxBlok, b);
  if (!pl->activate(pl, in.sr, 1, maxBlok)) fout("activate mislukt");
  if (naActivate && !zetTerug(pl, *naActivate)) fout("state load na activate mislukt");
  if (flushNaActivate) flush(pl, *flushNaActivate);  // params.flush terwijl actief, zonder process
  if (doeNaActivate) doeNaActivate(pl);
  if (!pl->start_processing(pl)) fout("start_processing mislukt");
  const uint32_t kan = 2;
  const size_t n = in.k[0].size();
  Wav uit; uit.sr = in.sr; uit.k.assign(kan, std::vector<float>(n));
  std::vector<std::vector<float>> bi(kan, std::vector<float>(maxBlok)), bu(kan, std::vector<float>(maxBlok));
  std::vector<float*> pi(kan), pu(kan);
  for (uint32_t c = 0; c < kan; ++c) { pi[c] = bi[c].data(); pu[c] = bu[c].data(); }
  clap_output_events_t uitEv = {nullptr, evPush};
  Events blokEv; blokEv.ev.reserve(events.size());
  size_t pos = 0, bnr = 0, e = 0;
  while (pos < n) {
    uint32_t m = blokken[bnr++ % blokken.size()];
    if (pos + m > n) m = (uint32_t)(n - pos);
    if (splitsOpEvents) {
      while (e < events.size() && events[e].tijd < pos) ++e;  // (komt niet voor)
      size_t v = e;
      while (v < events.size() && events[v].tijd == pos) ++v;
      if (v < events.size() && events[v].tijd < pos + m) m = (uint32_t)(events[v].tijd - pos);
    }
    blokEv.ev.clear();
    while (e < events.size() && events[e].tijd < pos + m) {
      blokEv.ev.push_back(maakEvent((uint32_t)(events[e].tijd - pos), events[e].id, events[e].v));
      ++e;
    }
    clap_input_events_t inEv = {&blokEv, evSize, evGet};
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(pi[c], &in.k[c][pos], m * 4);
    clap_audio_buffer_t ai = {pi.data(), nullptr, kan, 0, 0}, au = {pu.data(), nullptr, kan, 0, 0};
    clap_process_t pr = {(int64_t)pos, m, nullptr, &ai, &au, 1, 1, &inEv, &uitEv};
    if (doeProcess(pl, &pr) == CLAP_PROCESS_ERROR) fout("process gaf een fout");
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(&uit.k[c][pos], pu[c], m * 4);
    pos += m;
  }
  pl->stop_processing(pl);
  pl->deactivate(pl);
  return uit;
}
// eerste sample waar a en b verschillen (bitgewijs), of -1
long verschil(const Wav& a, const Wav& b) {
  for (size_t c = 0; c < a.k.size(); ++c)
    for (size_t i = 0; i < a.k[c].size(); ++i)
      if (std::memcmp(&a.k[c][i], &b.k[c][i], 4)) return (long)i;
  return -1;
}
bool eindig(const Wav& a) {
  for (auto& c : a.k) for (float v : c) if (!std::isfinite(v)) return false;
  return true;
}
std::string getal(double v) { char t[64]; std::snprintf(t, sizeof t, "%.17g", v); return t; }

// een waarde die niet de standaard is, binnen het bereik: per id een andere fractie
double andereWaarde(const clap_param_info_t& info, int nr) {
  const double f = 0.15 + 0.1 * (nr % 8);
  double v = info.min_value + (info.max_value - info.min_value) * f;
  v = (double)(float)v;  // exact in float32, zoals SPEC.md voor de grenzen en standaarden
  if (v == info.default_value) v = (double)(float)(info.min_value + (info.max_value - info.min_value) * (f + 0.05));
  return v;
}

// --- state ------------------------------------------------------------------------------------------------------
int toetsState(const std::string& oudPad, const std::string& nieuwPad, clap_id weg) {
  std::printf("state-toets: oud = %s (zonder parameter %u), nieuw = %s\n", oudPad.c_str(), weg, nieuwPad.c_str());
  Bib oud = laad(oudPad), nieuw = laad(nieuwPad);
  if (oud.id != nieuw.id) fout("oud en nieuw hebben een ander plugin-id");
  const Wav in = testsignaal(48000, 48000 * 3 / 2);
  const std::vector<uint32_t> blok = {512};

  const clap_plugin_t* o = maak(oud);
  const clap_plugin_t* nw = maak(nieuw);
  const auto infoOud = infos(o), infoNieuw = infos(nw);
  eis(infoNieuw.count(weg) == 1, "nieuw kent parameter " + std::to_string(weg));
  eis(infoOud.count(weg) == 0 && infoOud.size() + 1 == infoNieuw.size(),
      "oud kent parameter " + std::to_string(weg) + " niet (" + std::to_string(infoOud.size()) + " tegen " +
          std::to_string(infoNieuw.size()) + " parameters)");
  if (!infoNieuw.count(weg)) return 1;
  const double standaardWeg = infoNieuw.at(weg).default_value;
  o->destroy(o); nw->destroy(nw);

  // waarden voor oud: alles wat oud kent, niet de standaard
  std::vector<std::pair<clap_id, double>> wOud;
  int nr = 0;
  for (auto& [id, info] : infoOud) wOud.push_back({id, andereWaarde(info, nr++)});

  // 1. state van oud bewaren
  o = maak(oud);
  flush(o, wOud);
  const auto blobOud = bewaar(o);
  o->destroy(o);
  const auto idsOud = idsInState(blobOud);
  eis(std::find(idsOud.begin(), idsOud.end(), weg) == idsOud.end() && idsOud.size() == infoOud.size(),
      "state van oud bevat " + std::to_string(idsOud.size()) + " parameters, niet " + std::to_string(weg));

  // 2. vooruit: laden in nieuw (dat parameter weg eerst op een andere waarde had)
  nw = maak(nieuw);
  flush(nw, {{weg, andereWaarde(infoNieuw.at(weg), 3)}});
  rescans = 0;
  eis(zetTerug(nw, blobOud), "nieuw laadt de state van oud");
  eis(rescans > 0, "nieuw vraagt de host na het laden om een rescan");
  auto w = waarden(nw);
  bool allesGelijk = true;
  for (auto& [id, v] : wOud)
    if (w[id] != v) { allesGelijk = false; std::printf("    id %u: %s, verwacht %s\n", id, getal(w[id]).c_str(), getal(v).c_str()); }
  eis(allesGelijk, "elke waarde die oud kende komt exact terug in nieuw");
  eis(w[weg] == standaardWeg, "parameter " + std::to_string(weg) + " (ontbrak) staat op zijn standaard " + getal(standaardWeg) +
                                  " (is " + getal(w[weg]) + ")");
  const Wav naLaden = render(nw, in, blok, {});
  nw->destroy(nw);

  // 3. hetzelfde geluid als nieuw met die waarden via flush (parameter weg op de standaard)
  nw = maak(nieuw);
  flush(nw, wOud);
  const Wav viaFlush = render(nw, in, blok, {});
  nw->destroy(nw);
  eis(eindig(viaFlush), "render is eindig (geen NaN/Inf)");
  long d = verschil(naLaden, viaFlush);
  eis(d < 0, "nieuw na het laden klinkt bit voor bit als nieuw met dezelfde waarden" + (d < 0 ? std::string() : " (verschil vanaf sample " + std::to_string(d) + ")"));

  // 3b. dezelfde waarden via params.flush terwijl nieuw actief is maar er geen process loopt
  nw = maak(nieuw);
  const Wav flushActief = render(nw, in, blok, {}, false, nullptr, &wOud);
  w = waarden(nw);
  nw->destroy(nw);
  allesGelijk = true;
  for (auto& [id, v] : wOud) if (w[id] != v) allesGelijk = false;
  eis(allesGelijk, "params.flush buiten process (actief): get_value geeft de nieuwe waarden");
  d = verschil(flushActief, viaFlush);
  eis(d < 0, "params.flush buiten process (actief): bit voor bit gelijk aan flush vóór activate" + (d < 0 ? std::string() : " (verschil vanaf sample " + std::to_string(d) + ")"));

  // 4. de state komt terwijl nieuw al actief is (na activate, vóór process): process neemt hem over
  nw = maak(nieuw);
  flushVragen = 0;
  const Wav actiefGeladen = render(nw, in, blok, {}, false, &blobOud);
  w = waarden(nw);
  nw->destroy(nw);
  eis(flushVragen > 0, "nieuw vraagt request_flush bij een state-load terwijl hij actief is");
  d = verschil(actiefGeladen, viaFlush);
  eis(d < 0, "state geladen terwijl actief: bit voor bit gelijk" + (d < 0 ? std::string() : " (verschil vanaf sample " + std::to_string(d) + ")"));
  eis(w[weg] == standaardWeg, "ook dan staat parameter " + std::to_string(weg) + " op zijn standaard");

  // 5. de state van nieuw daarna opnieuw bewaren en laden: nu met alle parameters, weg op zijn standaard
  nw = maak(nieuw);
  zetTerug(nw, blobOud);
  const auto blobNieuw = bewaar(nw);
  nw->destroy(nw);
  eis(idsInState(blobNieuw).size() == infoNieuw.size(), "nieuw bewaart daarna alle " + std::to_string(infoNieuw.size()) + " parameters");

  // 6. achteruit: state van nieuw (weg niet op de standaard) laadt in oud; het onbekende id wordt overgeslagen
  std::vector<std::pair<clap_id, double>> wNieuw;
  nr = 0;
  for (auto& [id, info] : infoNieuw) wNieuw.push_back({id, andereWaarde(info, nr++ + 2)});
  nw = maak(nieuw);
  flush(nw, wNieuw);
  const auto blobAlles = bewaar(nw);
  nw->destroy(nw);
  o = maak(oud);
  eis(zetTerug(o, blobAlles), "oud laadt de state van nieuw (onbekend id " + std::to_string(weg) + " overgeslagen)");
  w = waarden(o);
  allesGelijk = w.count(weg) == 0;
  std::vector<std::pair<clap_id, double>> wZonderWeg;
  for (auto& [id, v] : wNieuw)
    if (id != weg) { wZonderWeg.push_back({id, v}); if (w[id] != v) allesGelijk = false; }
  eis(allesGelijk, "oud heeft daarna exact de waarden die hij kent");
  const Wav oudNaLaden = render(o, in, blok, {});
  o->destroy(o);
  o = maak(oud);
  flush(o, wZonderWeg);
  const Wav oudViaFlush = render(o, in, blok, {});
  o->destroy(o);
  d = verschil(oudNaLaden, oudViaFlush);
  eis(d < 0, "oud klinkt daarna als oud met dezelfde waarden" + (d < 0 ? std::string() : " (verschil vanaf sample " + std::to_string(d) + ")"));

  // 7. een toekomstige versie: versie 7, een onbekend id 9999 tussen de bekende, met een rare waarde
  {
    std::vector<unsigned char> blob;
    auto put32 = [&](uint32_t v) { unsigned char b[4]; std::memcpy(b, &v, 4); blob.insert(blob.end(), b, b + 4); };
    auto put64 = [&](double v) { unsigned char b[8]; std::memcpy(b, &v, 8); blob.insert(blob.end(), b, b + 8); };
    put32(0x54534250u); put32(7); put32((uint32_t)wNieuw.size() + 1);
    size_t k = 0;
    for (auto& [id, v] : wNieuw) {
      put32(id); put64(v);
      if (++k == 2) { put32(9999); put64(1e300); }
    }
    nw = maak(nieuw);
    eis(zetTerug(nw, blob), "nieuw laadt state van een toekomstige versie met een onbekend id");
    w = waarden(nw);
    allesGelijk = w.count(9999) == 0;
    for (auto& [id, v] : wNieuw) if (w[id] != v) allesGelijk = false;
    eis(allesGelijk, "daarna exact de bekende waarden; het onbekende id doet niets");
    nw->destroy(nw);
  }

  // 8. kapotte state wordt geweigerd en verandert niets
  {
    nw = maak(nieuw);
    flush(nw, wNieuw);
    const auto voor = waarden(nw);
    auto fouteMagie = blobAlles; fouteMagie[0] ^= 0xff;
    auto afgekapt = blobAlles; afgekapt.resize(afgekapt.size() - 5);
    std::vector<unsigned char> leeg;
    eis(!zetTerug(nw, fouteMagie), "state met een verkeerde kop wordt geweigerd");
    eis(!zetTerug(nw, afgekapt), "afgekapte state wordt geweigerd");
    eis(!zetTerug(nw, leeg), "lege state wordt geweigerd");
    eis(waarden(nw) == voor, "na geweigerde state zijn alle waarden onveranderd");
    nw->destroy(nw);
  }

  oud.entry->deinit(); nieuw.entry->deinit();
  std::printf("state-toets zonder parameter %u: %s\n", weg, mislukt ? "MISLUKT" : "geslaagd");
  return mislukt ? 1 : 0;
}

// --- render met de waarden als automatisering ----------------------------------------------------------------------
int toetsRender(int argc, char** argv) {
  if (argc < 5) fout("gebruik: pb-toets render <plugin.clap> <in.wav> <uit.wav> [--p id=waarde ...]");
  std::vector<std::pair<clap_id, double>> w;
  for (int i = 5; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--p" && i + 1 < argc) {
      std::string s = argv[++i]; size_t k = s.find('=');
      if (k == std::string::npos) fout("--p id=waarde");
      w.push_back({(clap_id)std::atoi(s.substr(0, k).c_str()), std::atof(s.substr(k + 1).c_str())});
    } else fout("onbekende optie " + a);
  }
  Bib b = laad(argv[2]);
  const clap_plugin_t* pl = maak(b);
  Wav in = leesWav(argv[3]);
  if (in.k.size() == 1) in.k.push_back(in.k[0]);
  // de waarden op sample 0 (vóór alle audio, zoals het begin van een automatiseringsspoor), en daarna steeds opnieuw
  // midden in de blokken: elke parameter om de 61 + 7·nr samples, met dezelfde waarde
  std::vector<AbsEvent> ev;
  const size_t n = in.k[0].size();
  int nr = 0;
  for (auto& [id, v] : w) {
    const uint64_t stap = 61 + 7 * (uint64_t)nr++;
    for (uint64_t t = 0; t < n; t += stap) ev.push_back({t, id, v});
  }
  Wav uit = render(pl, in, {512, 3, 128, 1, 4096, 77, 333}, ev);
  pl->destroy(pl);
  b.entry->deinit();
  schrijfWav(argv[4], uit);
  return 0;
}

// --- echte automatisering: onafhankelijk van de blokindeling ---------------------------------------------------------
int toetsAutomatisering(const std::string& pad) {
  std::printf("automatisering: %s\n", pad.c_str());
  Bib b = laad(pad);
  const Wav in = testsignaal(48000, 48000 * 2);
  const size_t n = in.k[0].size();
  const clap_plugin_t* pl = maak(b);
  const auto info = infos(pl);
  pl->destroy(pl);
  // per parameter een reeks events op "willekeurige" samples (deterministisch), soms meer op dezelfde sample, soms
  // buiten het bereik (de plugin klemt)
  std::vector<AbsEvent> ev;
  uint32_t x = 12345;
  auto rnd = [&]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
  for (auto& [id, pi] : info) {
    const double bereik = pi.max_value - pi.min_value;
    uint64_t t = rnd() % 700;
    while (t < n) {
      double v = pi.min_value + bereik * (double)(rnd() % 10001) / 10000.0;
      if (rnd() % 23 == 0) v = (rnd() & 1) ? pi.max_value + bereik : pi.min_value - bereik;
      ev.push_back({t, id, v});
      if (rnd() % 5 == 0) ev.push_back({t, id, pi.min_value + bereik * 0.5});  // tweede event op dezelfde sample
      t += 1 + rnd() % (id == 1 ? 3000 : 900);
    }
  }
  std::printf("  %zu events op %zu samples\n", ev.size(), n);
  auto doe = [&](std::vector<uint32_t> blokken, bool splits) {
    const clap_plugin_t* p = maak(b);
    Wav w = render(p, in, std::move(blokken), ev, splits);
    p->destroy(p);
    return w;
  };
  const Wav ref = doe({512}, false);
  eis(eindig(ref), "uitvoer is eindig (geen NaN/Inf)");
  const Wav zonder = [&] { const clap_plugin_t* p = maak(b); Wav w = render(p, in, {512}, {}); p->destroy(p); return w; }();
  eis(verschil(ref, zonder) >= 0, "de automatisering doet hoorbaar iets (anders toetst dit niets)");
  struct Geval { const char* naam; std::vector<uint32_t> blokken; bool splits; };
  const Geval gevallen[] = {
      {"wisselende blokken 3,128,1,4096,77", {3, 128, 1, 4096, 77}, false},
      {"blok 1", {1}, false},
      {"blok 4096", {4096}, false},
      {"blokken gesplitst op elk event (event op tijd 0)", {4096}, true},
  };
  for (auto& g : gevallen) {
    const long d = verschil(doe(g.blokken, g.splits), ref);
    eis(d < 0, std::string(g.naam) + ": bit voor bit gelijk aan blok 512" + (d < 0 ? std::string() : " (verschil vanaf sample " + std::to_string(d) + ")"));
  }
  b.entry->deinit();
  std::printf("automatisering: %s\n", mislukt ? "MISLUKT" : "geslaagd");
  return mislukt ? 1 : 0;
}

// --- tekst <-> waarde ----------------------------------------------------------------------------------------
int toetsTekst(const std::string& pad) {
  std::printf("tekst: %s\n", pad.c_str());
  Bib b = laad(pad);
  const clap_plugin_t* pl = maak(b);
  auto* p = params(pl);
  const auto info = infos(pl);
  auto naar = [&](clap_id id, double v) {
    char t[256] = {0};
    return p->value_to_text(pl, id, v, t, sizeof t) ? std::string(t) : std::string("<geen>");
  };
  auto uit = [&](clap_id id, const char* t, double* v) { return p->text_to_value(pl, id, t, v); };
  auto rondgang = [&](const char* wanneer) {
    int fouten = 0;
    for (auto& [id, pi] : info)
      for (int k = 0; k <= 2000; ++k) {
        const double v = pi.min_value + (pi.max_value - pi.min_value) * k / 2000.0;
        const std::string t1 = naar(id, v);
        double w1 = NAN, w2 = NAN;
        if (!uit(id, t1.c_str(), &w1)) { if (fouten++ < 5) std::printf("    id %u: '%s' niet terug te lezen\n", id, t1.c_str()); continue; }
        const std::string t2 = naar(id, w1);
        if (t2 != t1 || !uit(id, t2.c_str(), &w2) || w2 != w1 || w1 < pi.min_value || w1 > pi.max_value) {
          if (fouten++ < 5) std::printf("    id %u: %s -> '%s' -> %s -> '%s'\n", id, getal(v).c_str(), t1.c_str(), getal(w1).c_str(), t2.c_str());
        }
      }
    eis(fouten == 0, std::string("tekst -> waarde -> tekst is stabiel over het hele bereik van elke parameter (") + wanneer + ")");
  };
  // id's uit SPEC.md: 1 Tijd (ms), 2 Terugkoppeling, 3 Toon (Hz), 4 Laag weg (Hz), 5 Resonantie (Q),
  // 6 Verzadiging (dB), 7 Haperen, 8 Mix
  struct Naar { clap_id id; double v; const char* tekst; };
  const Naar naarGevallen[] = {
      {1, 375, "375 ms"}, {1, 20, "20.0 ms"}, {1, 99.94, "99.9 ms"}, {1, 99.96, "100 ms"}, {1, 999.6, "1.000 s"},
      {1, 1200, "1.200 s"}, {2, 0.5, "50.0 %"}, {2, 1.25, "125.0 %"}, {3, 3000, "3.00 kHz"}, {3, 300, "300 Hz"},
      {3, 12000, "12.00 kHz"}, {4, 100, "100 Hz"}, {4, 20, "20.0 Hz"}, {4, 999.4, "999 Hz"}, {5, 0.70703125, "0.707"},
      {6, 6, "6.0 dB"}, {6, 0, "0.0 dB"}, {7, 0.25, "25.0 %"}, {8, 0.375, "37.5 %"}, {8, 0, "0.0 %"},
  };
  struct Uit { clap_id id; const char* tekst; bool kan; double v; };
  const Uit uitGevallen[] = {
      {1, "375 ms", true, 375}, {1, "375", true, 375}, {1, "1,2 s", true, 1200}, {1, "1.2s", true, 1200},
      {1, " 250 MS ", true, 250}, {1, "5 s", true, 1200}, {1, "3 ms", true, 20}, {1, "10 Hz", false, 0},
      {2, "50 %", true, 0.5}, {2, "50", true, 0.5}, {2, "110%", true, 1.1}, {2, "200 %", true, 1.25},
      {3, "3k", true, 3000}, {3, "3.00 kHz", true, 3000}, {3, "2500", true, 2500}, {3, "1,5 kHz", true, 1500},
      {4, "100 Hz", true, 100}, {5, "0.707", true, 0.707}, {5, "Q 2", false, 0}, {5, "2 q", true, 2},
      {6, "6.0 dB", true, 6}, {6, "-3 dB", true, 0}, {6, "12db", true, 12}, {8, "37.5 %", true, 0.375},
      {8, "", false, 0}, {8, "veel", false, 0}, {3, "kHz", false, 0}, {1, "nan", false, 0}, {1, "1e3", false, 0},
  };
  auto bekendeParen = [&](const char* wanneer) {
    int fouten = 0;
    for (auto& g : naarGevallen) {
      const std::string t = naar(g.id, g.v);
      if (t != g.tekst) { ++fouten; std::printf("    id %u, %s: '%s', verwacht '%s'\n", g.id, getal(g.v).c_str(), t.c_str(), g.tekst); }
    }
    for (auto& g : uitGevallen) {
      double v = NAN;
      const bool kan = uit(g.id, g.tekst, &v);
      if (kan != g.kan || (kan && v != g.v)) {
        ++fouten;
        std::printf("    id %u, '%s': %s %s, verwacht %s %s\n", g.id, g.tekst, kan ? "ja" : "nee", getal(v).c_str(), g.kan ? "ja" : "nee", getal(g.v).c_str());
      }
    }
    eis(fouten == 0, std::string("bekende paren waarde -> tekst en tekst -> waarde (") + wanneer + ")");
  };
  bekendeParen("C-locale");
  rondgang("C-locale");
  const char* kommaLocales[] = {"nl_NL.UTF-8", "nl_NL.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "nl_NL", "de_DE"};
  const char* gezet = nullptr;
  for (const char* l : kommaLocales)
    if (std::setlocale(LC_ALL, l)) { gezet = l; break; }
  if (gezet && std::localeconv()->decimal_point[0] == ',') {
    bekendeParen(gezet);
    rondgang(gezet);
  } else if (std::getenv("PB_EIS_KOMMA")) {
    eis(false, "een locale met een decimale komma (PB_EIS_KOMMA is gezet, maar er is er geen)");
  } else {
    std::printf("  (geen locale met een decimale komma op dit systeem; die toets is overgeslagen)\n");
  }
  std::setlocale(LC_ALL, "C");
  pl->destroy(pl);
  b.entry->deinit();
  std::printf("tekst: %s\n", mislukt ? "MISLUKT" : "geslaagd");
  return mislukt ? 1 : 0;
}

// --- presets (fase 4) ---------------------------------------------------------------------------------------------
struct GevondenPlek { uint32_t vlaggen, soort; std::string naam, plek; };
struct Indexer {
  std::vector<GevondenPlek> plekken;
  std::vector<std::string> extensies;
  static Indexer* zelf(const clap_preset_discovery_indexer_t* i) { return static_cast<Indexer*>(i->indexer_data); }
  static bool type(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_filetype_t* t) {
    zelf(i)->extensies.push_back(t && t->file_extension ? t->file_extension : "");
    return true;
  }
  static bool plek(const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
    zelf(i)->plekken.push_back({l->flags, l->kind, l->name ? l->name : "", l->location ? l->location : ""});
    return true;
  }
  static bool pakket(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; }
  static const void* ext(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }
};
struct GevondenPreset { std::string naam; bool heeftSleutel = false; uint32_t vlaggen = 0; std::vector<std::string> ids; };
struct Ontvanger {
  std::vector<GevondenPreset> presets;
  int fouten = 0;
  static Ontvanger* zelf(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Ontvanger*>(r->receiver_data); }
  static void fout(const clap_preset_discovery_metadata_receiver_t* r, int32_t, const char* m) {
    ++zelf(r)->fouten;
    std::printf("    (metadata on_error: %s)\n", m ? m : "");
  }
  static bool begin(const clap_preset_discovery_metadata_receiver_t* r, const char* naam, const char* sleutel) {
    GevondenPreset g; g.naam = naam ? naam : ""; g.heeftSleutel = sleutel != nullptr;
    zelf(r)->presets.push_back(g);
    return true;
  }
  static void id(const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* i) {
    if (!zelf(r)->presets.empty()) zelf(r)->presets.back().ids.push_back(std::string(i->abi) + ":" + i->id);
  }
  static void vlaggen(const clap_preset_discovery_metadata_receiver_t* r, uint32_t v) {
    if (!zelf(r)->presets.empty()) zelf(r)->presets.back().vlaggen = v;
  }
  static void tekst(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
  static void tijden(const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
  static void extra(const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}
};

struct VerwachtePreset { std::string bestand, naam; std::vector<std::pair<std::string, double>> waarden; };
std::vector<VerwachtePreset> leesVerwacht(const std::string& pad) {
  std::ifstream f(pad);
  if (!f) fout("kan niet lezen: " + pad);
  std::vector<VerwachtePreset> v;
  std::string regel;
  while (std::getline(f, regel)) {
    if (regel.empty()) continue;
    std::vector<std::string> velden;
    std::stringstream ss(regel);
    std::string veld;
    while (std::getline(ss, veld, '\t')) velden.push_back(veld);
    if (velden.size() < 2) fout("verwacht.tsv: regel zonder bestand en naam");
    VerwachtePreset p; p.bestand = velden[0]; p.naam = velden[1];
    for (size_t i = 2; i < velden.size(); ++i) {
      const size_t k = velden[i].find('=');
      if (k == std::string::npos) fout("verwacht.tsv: geen param=waarde: " + velden[i]);
      p.waarden.push_back({velden[i].substr(0, k), std::strtod(velden[i].c_str() + k + 1, nullptr)});
    }
    v.push_back(p);
  }
  return v;
}

int toetsPresets(const std::string& pad, const std::string& verwachtPad) {
  const auto verwacht = leesVerwacht(verwachtPad);
  std::printf("presets: %s, %zu verwacht\n", pad.c_str(), verwacht.size());
  Bib b = laad(pad);
  auto* fab = static_cast<const clap_preset_discovery_factory_t*>(b.entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  eis(fab != nullptr, "de plugin heeft een preset-discovery-factory");
  if (!fab) return 1;
  eis(fab->count(fab) == 1, "één preset-provider");
  const auto* desc = fab->get_descriptor(fab, 0);
  if (!desc) fout("geen provider-beschrijving");

  // als een host: de provider maken, zijn locaties ophalen, elk bestand met de juiste extensie indexeren
  Indexer ix;
  clap_preset_discovery_indexer_t indexer = {CLAP_VERSION_INIT, "pb-toets", "Pluginbouwer", "", "0.1", &ix,
                                             Indexer::type, Indexer::plek, Indexer::pakket, Indexer::ext};
  const auto* prov = fab->create(fab, &indexer, desc->id);
  eis(prov != nullptr && prov->init(prov), std::string("provider '") + desc->id + "' start");
  if (!prov) return 1;
  eis(ix.plekken.size() == 1, "precies één locatie (" + std::to_string(ix.plekken.size()) + ")");
  if (ix.plekken.size() != 1) return 1;
  const GevondenPlek plek = ix.plekken[0];
  struct stat st{};
  eis(plek.soort == CLAP_PRESET_DISCOVERY_LOCATION_FILE && stat(plek.plek.c_str(), &st) == 0 && S_ISDIR(st.st_mode),
      "de locatie is een map die bestaat: " + plek.plek);
  eis(plek.vlaggen == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "de locatie is fabrieksinhoud");
  eis(ix.extensies.size() == 1 && ix.extensies[0] == "json", "bestandstype: json");

  std::map<std::string, std::string> namen;  // bestand -> naam zoals de plugin hem meldt
  for (const auto& v : verwacht) {
    Ontvanger o;
    clap_preset_discovery_metadata_receiver_t r = {&o, Ontvanger::fout, Ontvanger::begin, Ontvanger::id,
                                                   Ontvanger::tekst, Ontvanger::vlaggen, Ontvanger::tekst,
                                                   Ontvanger::tekst, Ontvanger::tijden, Ontvanger::tekst,
                                                   Ontvanger::extra};
    const std::string bestand = plek.plek + "/" + v.bestand;
    const bool goed = prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_FILE, bestand.c_str(), &r);
    const bool een = goed && o.fouten == 0 && o.presets.size() == 1;
    eis(een && o.presets[0].naam == v.naam && !o.presets[0].heeftSleutel &&
            o.presets[0].vlaggen == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT &&
            std::find(o.presets[0].ids.begin(), o.presets[0].ids.end(), "clap:" + b.id) != o.presets[0].ids.end(),
        v.bestand + ": één preset '" + v.naam + "' voor clap:" + b.id + ", zonder laadsleutel" +
            (een ? " (gemeld: '" + o.presets[0].naam + "')" : ""));
  }
  {  // geen andere presets in de map dan de verwachte
    int json = 0;
    std::string lijst = "ls -1 '" + plek.plek + "' | grep -c '\\.json$'";
    if (FILE* p = popen(lijst.c_str(), "r")) { if (std::fscanf(p, "%d", &json) != 1) json = -1; pclose(p); }
    eis(json == (int)verwacht.size(), "de map bevat precies de " + std::to_string(verwacht.size()) + " verwachte presets");
  }
  prov->destroy(prov);

  // laden: elke preset in een eigen instantie; waarden exact, het geluid als dezelfde waarden via flush
  const Wav in = testsignaal(48000, 48000);
  const std::vector<uint32_t> blok = {512};
  const clap_plugin_t* pl = maak(b);
  const auto info = infos(pl);
  auto* pload = static_cast<const clap_plugin_preset_load_t*>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
  eis(pload != nullptr, "de plugin heeft de preset-load-extensie");
  pl->destroy(pl);
  if (!pload) return 1;
  auto laadPreset = [&](const clap_plugin_t* p, const std::string& bestand) {
    auto* l = static_cast<const clap_plugin_preset_load_t*>(p->get_extension(p, CLAP_EXT_PRESET_LOAD));
    return l->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_FILE, bestand.c_str(), nullptr);
  };

  const char* kommaLocales[] = {"nl_NL.UTF-8", "nl_NL.utf8", "de_DE.UTF-8", "de_DE.utf8", "nl_NL", "de_DE"};
  const char* komma = nullptr;
  for (const char* l : kommaLocales)
    if (std::setlocale(LC_ALL, l)) { komma = std::localeconv()->decimal_point[0] == ',' ? l : nullptr; if (komma) break; }
  std::setlocale(LC_ALL, "C");
  if (!komma && std::getenv("PB_EIS_KOMMA")) eis(false, "een locale met een decimale komma (PB_EIS_KOMMA is gezet, maar er is er geen)");

  for (const auto& v : verwacht) {
    const std::string bestand = plek.plek + "/" + v.bestand;
    std::vector<std::pair<clap_id, double>> doel;
    bool namenBekend = true;
    for (auto& [id, inf] : info) {
      double w = inf.default_value;
      for (auto& [naam, x] : v.waarden) if (naam == inf.name) w = x;
      doel.push_back({id, w});
    }
    for (auto& [naam, x] : v.waarden) {
      bool bekend = false;
      for (auto& [id, inf] : info) bekend |= naam == inf.name;
      if (!bekend) { namenBekend = false; std::printf("    onbekende parameter in %s: '%s'\n", v.bestand.c_str(), naam.c_str()); }
    }
    eis(namenBekend, v.bestand + ": elke parameternaam bestaat");

    for (const char* locale : {"C", komma}) {
      if (!locale) continue;
      std::setlocale(LC_ALL, locale);
      const std::string waar = v.bestand + (std::strcmp(locale, "C") ? std::string(" (") + locale + ")" : "");
      pl = maak(b);
      flush(pl, {{info.begin()->first, info.begin()->second.max_value}});  // eerst een andere waarde
      rescans = 0; presetGeladen = 0; presetFouten = 0;
      eis(laadPreset(pl, bestand) && presetFouten == 0, waar + ": preset-load geslaagd");
      eis(rescans > 0 && presetGeladen == 1, waar + ": de host krijgt rescan en loaded");
      auto w = waarden(pl);
      bool gelijk = true;
      for (auto& [id, x] : doel)
        if (w[id] != x) { gelijk = false; std::printf("    id %u: %s, verwacht %s\n", id, getal(w[id]).c_str(), getal(x).c_str()); }
      eis(gelijk, waar + ": elke waarde exact zoals in de JSON, de rest op zijn standaard");
      const Wav naLaden = render(pl, in, blok, {});
      pl->destroy(pl);
      std::setlocale(LC_ALL, "C");
      if (std::strcmp(locale, "C")) continue;

      pl = maak(b);
      flush(pl, doel);
      const Wav viaFlush = render(pl, in, blok, {});
      pl->destroy(pl);
      long d = verschil(naLaden, viaFlush);
      eis(eindig(viaFlush) && d < 0, waar + ": klinkt bit voor bit als dezelfde waarden via flush" +
                                         (d < 0 ? std::string() : " (verschil vanaf sample " + std::to_string(d) + ")"));

      pl = maak(b);
      flushVragen = 0;
      const Wav actief = render(pl, in, blok, {}, false, nullptr, nullptr,
                                [&](const clap_plugin_t* p) { if (!laadPreset(p, bestand)) fout("preset-load (actief) mislukt"); });
      pl->destroy(pl);
      d = verschil(actief, viaFlush);
      eis(flushVragen > 0 && d < 0, waar + ": geladen terwijl actief (request_flush, dan process): bit voor bit gelijk" +
                                        (d < 0 ? std::string() : " (verschil vanaf sample " + std::to_string(d) + ")"));
    }
    if (v.bestand == "standaard.json") {
      bool standaard = true;
      for (auto& [id, x] : doel) standaard &= x == info.at(id).default_value;
      eis(standaard, "standaard.json is de beginstand van de plugin (alle standaardwaarden)");
    }
  }

  // fouten: een bestand dat niet bestaat, een ander soort locatie; de waarden blijven staan
  pl = maak(b);
  flush(pl, {{info.begin()->first, info.begin()->second.max_value}});
  const auto voor = waarden(pl);
  presetFouten = 0;
  auto* l = static_cast<const clap_plugin_preset_load_t*>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
  const bool weg = l->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_FILE, (plek.plek + "/bestaat-niet.json").c_str(), nullptr);
  const bool soort = l->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "standaard");
  eis(!weg && !soort && presetFouten == 2, "een onbekend bestand en een ander soort locatie: false en on_error");
  eis(waarden(pl) == voor, "na een mislukte preset-load staan de waarden er nog");
  pl->destroy(pl);

  b.entry->deinit();
  std::printf("presets: %s (%zu presets)\n", mislukt ? "MISLUKT" : "geslaagd", verwacht.size());
  return mislukt ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string wat = argc > 1 ? argv[1] : "";
  if (wat == "state" && argc == 5) return toetsState(argv[2], argv[3], (clap_id)std::atoi(argv[4]));
  if (wat == "render") return toetsRender(argc, argv);
  if (wat == "tekst" && argc == 3) return toetsTekst(argv[2]);
  if (wat == "automatisering" && argc == 3) return toetsAutomatisering(argv[2]);
  if (wat == "presets" && argc == 4) return toetsPresets(argv[2], argv[3]);
  fout("gebruik: pb-toets state <oud.clap> <nieuw.clap> <id> | render <plugin.clap> <in.wav> <uit.wav> [--p id=waarde] | "
       "tekst <plugin.clap> | automatisering <plugin.clap> | presets <plugin.clap> <verwacht.tsv>");
}
