// au-proef.cpp — een kleine AU-host (macOS, AudioToolbox) die de plugin gebruikt zoals een DAW: laden, initialiseren,
// renderen met een invoer-callback, parameters per blok automatiseren, state (ClassInfo) bewaren en terugzetten in een
// nieuwe instantie, TailTime en bypass. Hij oordeelt niet: hij schrijft WAV's en info.json, tools/au-proef.mjs toetst.
//   au-proef <type> <subtype> <maker> <map met testsignalen/<ontwerp>/au> <uitmap>
//   au-proef aumu <subtype> <maker> <testsignalen/<ontwerp>> <uitmap>     een instrument (18 okt): geen ingang; de noten
//     van noten/noten.txt als MIDI (MusicDeviceMIDIEvent, op hun sample in het blok), het tempo van hosttempo/tempo.txt
//     via de host-callbacks (beat en tempo, transport speelt), state als hierboven
// Bouwen: clang++ -std=c++17 -O2 host/au-proef.cpp -framework AudioToolbox -framework CoreFoundation -o au-proef
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static void fout(const char* wat, OSStatus s) {
  if (s != noErr) { fprintf(stderr, "FOUT: %s (OSStatus %d)\n", wat, (int)s); exit(2); }
}
static OSType code(const char* s) { return ((OSType)s[0] << 24) | ((OSType)s[1] << 16) | ((OSType)s[2] << 8) | (OSType)s[3]; }

// ---- WAV (float32 of 16/24/32-bit PCM in, float32 uit) --------------------------------------------------------------
struct Wav { vector<vector<float>> k; uint32_t sr = 48000; };
static Wav leesWav(const string& pad) {
  ifstream f(pad, ios::binary); if (!f) { fprintf(stderr, "kan %s niet lezen\n", pad.c_str()); exit(2); }
  vector<char> b((istreambuf_iterator<char>(f)), {});
  auto u16 = [&](size_t o) { uint16_t v; memcpy(&v, &b[o], 2); return v; };
  auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, &b[o], 4); return v; };
  size_t p = 12; uint16_t fmt = 0, kan = 0, bits = 0; uint32_t sr = 0; size_t dOff = 0, dLen = 0;
  while (p + 8 <= b.size()) {
    string id(&b[p], 4); uint32_t len = u32(p + 4);
    if (id == "fmt ") { fmt = u16(p + 8); kan = u16(p + 10); sr = u32(p + 12); bits = u16(p + 22); if (fmt == 0xFFFE) fmt = bits == 32 ? 3 : 1; }
    if (id == "data") { dOff = p + 8; dLen = len; }
    p += 8 + len + (len & 1);
  }
  Wav w; w.sr = sr; size_t bps = bits / 8, n = dLen / (bps * kan); w.k.assign(kan, vector<float>(n));
  for (size_t i = 0; i < n; ++i) for (size_t c = 0; c < kan; ++c) {
    const char* q = &b[dOff + (i * kan + c) * bps];
    float v;
    if (fmt == 3) memcpy(&v, q, 4);
    else if (bps == 2) { int16_t s; memcpy(&s, q, 2); v = s / 32768.f; }
    else if (bps == 3) { int32_t s = (int32_t)((uint32_t)(uint8_t)q[0] << 8 | (uint32_t)(uint8_t)q[1] << 16 | (uint32_t)(uint8_t)q[2] << 24) >> 8; v = s / 8388608.f; }
    else { int32_t s; memcpy(&s, q, 4); v = s / 2147483648.f; }
    w.k[c][i] = v;
  }
  return w;
}
static void schrijfWav(const string& pad, const vector<vector<float>>& k, uint32_t sr) {
  uint32_t n = (uint32_t)k[0].size(), kan = (uint32_t)k.size(), data = n * kan * 4;
  ofstream f(pad, ios::binary);
  auto w32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
  auto w16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
  f.write("RIFF", 4); w32(36 + data); f.write("WAVEfmt ", 8); w32(16); w16(3); w16((uint16_t)kan); w32(sr); w32(sr * kan * 4); w16((uint16_t)(kan * 4)); w16(32);
  f.write("data", 4); w32(data);
  for (uint32_t i = 0; i < n; ++i) for (uint32_t c = 0; c < kan; ++c) f.write((const char*)&k[c][i], 4);
}

// ---- de AU ------------------------------------------------------------------------------------------------------------
struct Bron { const vector<vector<float>>* in = nullptr; size_t pos = 0; };
static OSStatus invoer(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io) {
  auto* b = (Bron*)ref;
  for (UInt32 c = 0; c < io->mNumberBuffers; ++c) {
    float* d = (float*)io->mBuffers[c].mData;
    const auto& k = (*b->in)[min<size_t>(c, b->in->size() - 1)];
    for (UInt32 i = 0; i < n; ++i) d[i] = b->pos + i < k.size() ? k[b->pos + i] : 0.f;
  }
  return noErr;
}

// host-callbacks van een DAW die speelt op een vast tempo vanaf tel 0 (zoals pb-render --tempo)
struct HostKlok { double tempo = 0; uint32_t sr = 48000; Float64 tijd = 0; };
static OSStatus beatEnTempo(void* ref, Float64* beat, Float64* tempo) {
  auto* k = (HostKlok*)ref;
  if (beat) *beat = k->tijd / k->sr * k->tempo / 60.0;
  if (tempo) *tempo = k->tempo;
  return noErr;
}
static OSStatus transportStand(void*, Boolean* speelt, Boolean* neemtOp, Boolean* veranderd, Float64* pos, Boolean* lus, Float64* a, Float64* b) {
  if (speelt) *speelt = true;
  if (neemtOp) *neemtOp = false;
  if (veranderd) *veranderd = false;
  if (pos) *pos = 0;
  if (lus) *lus = false;
  if (a) *a = 0;
  if (b) *b = 0;
  return noErr;
}

struct Plug {
  AudioComponentInstance au = nullptr; Bron bron; uint32_t sr; uint32_t maxBlok = 512; Float64 tijd = 0; bool instrument;
  HostKlok klok;
  map<string, AudioUnitParameterID> idVan; map<string, AudioUnitParameterInfo> infoVan;
  Plug(AudioComponent comp, uint32_t sr_, bool instr = false) : sr(sr_), instrument(instr) {
    fout("instantiëren", AudioComponentInstanceNew(comp, &au));
    AudioStreamBasicDescription f{}; f.mSampleRate = sr; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    if (!instrument) fout("formaat in", AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f));
    fout("formaat uit", AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f));
    fout("max blok", AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxBlok, sizeof maxBlok));
    AURenderCallbackStruct cb{invoer, &bron};
    if (!instrument) fout("invoer-callback", AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb));
    UInt32 sz = 0; AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, nullptr);
    vector<AudioUnitParameterID> ids(sz / sizeof(AudioUnitParameterID));
    if (sz) fout("parameterlijst", AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz));
    for (auto id : ids) {
      AudioUnitParameterInfo in{}; UInt32 s2 = sizeof in;
      fout("parameterinfo", AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &in, &s2));
      char naam[256] = {0};
      if ((in.flags & kAudioUnitParameterFlag_HasCFNameString) && in.cfNameString) CFStringGetCString(in.cfNameString, naam, sizeof naam, kCFStringEncodingUTF8);
      else strncpy(naam, in.name, sizeof naam - 1);
      idVan[naam] = id; infoVan[naam] = in;
    }
  }
  void init() { fout("initialiseren", AudioUnitInitialize(au)); }
  // een host met een transport dat speelt op `tempo` bpm (vóór Initialize, zoals een DAW de callbacks zet)
  void hostTempo(double tempo) {
    klok.tempo = tempo; klok.sr = sr;
    HostCallbackInfo h{}; h.hostUserData = &klok; h.beatAndTempoProc = beatEnTempo; h.transportStateProc2 = transportStand;
    fout("host-callbacks", AudioUnitSetProperty(au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &h, sizeof h));
  }
  ~Plug() { if (au) { AudioUnitUninitialize(au); AudioComponentInstanceDispose(au); } }
  void zet(const string& naam, float v) {
    auto it = idVan.find(naam); if (it == idVan.end()) { fprintf(stderr, "FOUT: parameter '%s' niet in de AU\n", naam.c_str()); exit(2); }
    fout(("zet " + naam).c_str(), AudioUnitSetParameter(au, it->second, kAudioUnitScope_Global, 0, v, 0));
  }
  float lees(const string& naam) { AudioUnitParameterValue v = 0; AudioUnitGetParameter(au, idVan.at(naam), kAudioUnitScope_Global, 0, &v); return v; }
  // render n samples van `in` in blokken van `blok`; voorBlok(b) wordt vóór elk blok aangeroepen (automatisering)
  template <class F> vector<vector<float>> render(const vector<vector<float>>& in, uint32_t blok, F voorBlok) {
    size_t n = in[0].size(); vector<vector<float>> uit(2, vector<float>(n)); bron.in = &in; bron.pos = 0;
    vector<float> l(blok), r(blok);
    for (size_t pos = 0, b = 0; pos < n; pos += blok, ++b) {
      voorBlok(b);
      UInt32 m = (UInt32)min<size_t>(blok, n - pos);
      AudioBufferList* abl = (AudioBufferList*)alloca(sizeof(AudioBufferList) + sizeof(AudioBuffer));
      abl->mNumberBuffers = 2;
      abl->mBuffers[0] = {1, m * 4, l.data()}; abl->mBuffers[1] = {1, m * 4, r.data()};
      klok.tijd = tijd;
      AudioTimeStamp ts{}; ts.mSampleTime = tijd; ts.mFlags = kAudioTimeStampSampleTimeValid;
      AudioUnitRenderActionFlags fl = 0; bron.pos = pos;
      fout("renderen", AudioUnitRender(au, &fl, &ts, 0, m, abl));
      memcpy(&uit[0][pos], abl->mBuffers[0].mData, m * 4); memcpy(&uit[1][pos], abl->mBuffers[1].mData, m * 4);
      tijd += m;
    }
    return uit;
  }
};

static string jsonStr(const string& s) { string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\""; }

static int instrumentProef(AudioComponent comp, const string& tmap, const string& uit);

int main(int argc, char** argv) {
  if (argc < 6) { fprintf(stderr, "gebruik: au-proef <type> <subtype> <maker> <testmap> <uitmap>\n"); return 2; }
  AudioComponentDescription d{code(argv[1]), code(argv[2]), code(argv[3]), 0, 0};
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) { fprintf(stderr, "FOUT: AU %s %s %s niet gevonden (geïnstalleerd en AudioComponentRegistrar herstart?)\n", argv[1], argv[2], argv[3]); return 2; }
  string tmap = argv[4], uit = argv[5];
  if (string(argv[1]) == "aumu") return instrumentProef(comp, tmap, uit);
  Wav autoIn = leesWav(tmap + "/automatie.in.wav"), stoot = leesWav(tmap + "/stoot.in.wav");
  uint32_t sr = autoIn.sr;

  // parameterstappen
  ifstream sf(tmap + "/stappen.txt"); string regel; getline(sf, regel);
  vector<string> namen; { stringstream ss(regel); string t; while (getline(ss, t, '\t')) namen.push_back(t); }
  vector<vector<float>> rijen; while (getline(sf, regel)) { if (regel.empty()) continue; stringstream ss(regel); vector<float> r; string t; while (getline(ss, t, '\t')) r.push_back(strtof(t.c_str(), nullptr)); rijen.push_back(r); }
  // instellingen.json: alleen de twee platte objecten die we nodig hebben ("zelfosc" en "state"), met de hand gelezen
  ifstream jf(tmap + "/instellingen.json"); string json((istreambuf_iterator<char>(jf)), {});
  auto object = [&](const string& sleutel) {
    map<string, float> m; size_t a = json.find("\"" + sleutel + "\""); a = json.find('{', a); size_t e = json.find('}', a);
    string s = json.substr(a + 1, e - a - 1); size_t p = 0;
    while ((p = s.find('"', p)) != string::npos) { size_t q = s.find('"', p + 1); string k = s.substr(p + 1, q - p - 1); size_t c = s.find(':', q); m[k] = strtof(s.c_str() + c + 1, nullptr); p = s.find_first_of(",}", c); if (p == string::npos) break; }
    return m;
  };
  auto zelfosc = object("zelfosc"), stateWaarden = object("state");

  string info = "{";
  // 1. eigenschappen en parameters
  {
    Plug p(comp, sr); p.init();
    Float64 lat = 0, tail = 0; UInt32 s = sizeof(Float64);
    AudioUnitGetProperty(p.au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &lat, &s); s = sizeof(Float64);
    AudioUnitGetProperty(p.au, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail, &s);
    char buf[128]; snprintf(buf, sizeof buf, "\"latencySec\":%.9g,\"tailSec\":%.9g,\"params\":[", lat, tail); info += buf;
    bool eerst = true;
    for (auto& [naam, in] : p.infoVan) { snprintf(buf, sizeof buf, "%s{\"naam\":%s,\"min\":%.9g,\"max\":%.9g,\"standaard\":%.9g}", eerst ? "" : ",", jsonStr(naam).c_str(), in.minValue, in.maxValue, in.defaultValue); info += buf; eerst = false; }
    info += "],";
    printf("AU geladen: latency %.6f s, TailTime %.3f s, %zu parameters\n", lat, tail, p.infoVan.size());
  }
  // 2. automatisering: elke parameter vóór elk blok van 64 samples, zoals een host automatiseert
  {
    Plug p(comp, sr);
    for (size_t i = 0; i < namen.size(); ++i) p.zet(namen[i], rijen[0][i]);
    p.init();
    auto y = p.render(autoIn.k, 64, [&](size_t b) { for (size_t i = 0; i < namen.size(); ++i) p.zet(namen[i], rijen[min(b, rijen.size() - 1)][i]); });
    schrijfWav(uit + "/automatie.wav", y, sr);
  }
  // 3. zelfoscillatie
  {
    Plug p(comp, sr); for (auto& [k, v] : zelfosc) p.zet(k, v); p.init();
    schrijfWav(uit + "/zelfosc.wav", p.render(stoot.k, 512, [](size_t) {}), sr);
  }
  // 4. staart bij de standaardwaarden
  {
    Plug p(comp, sr); p.init();
    schrijfWav(uit + "/staart.wav", p.render(stoot.k, 512, [](size_t) {}), sr);
  }
  // 5. state: ClassInfo van A in een nieuwe instantie B; parameters en render moeten gelijk zijn
  {
    Plug a(comp, sr); for (auto& [k, v] : stateWaarden) a.zet(k, v); a.init();
    CFPropertyListRef plist = nullptr; UInt32 s = sizeof plist;
    fout("ClassInfo lezen", AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &s));
    // eerst vóór Initialize (zo doen sommige hosts het); weigert de AU dat, dan erna (zoals de meeste hosts)
    Plug b(comp, sr);
    OSStatus voor = AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof plist);
    b.init();
    if (voor != noErr) fout("ClassInfo zetten na Initialize", AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof plist));
    char vt[96]; snprintf(vt, sizeof vt, "%s", voor == noErr ? "ja" : "nee (OSStatus ");
    string voorTekst = voor == noErr ? "ja" : string(vt) + to_string((int)voor) + "), wel na Initialize";
    string verschil; bool pg = true;
    for (auto& [k, v] : stateWaarden) { float va = a.lees(k), vb = b.lees(k); if (va != vb) { pg = false; char t[160]; snprintf(t, sizeof t, "%s %.9g→%.9g; ", k.c_str(), va, vb); verschil += t; } }
    auto ya = a.render(autoIn.k, 512, [](size_t) {}), yb = b.render(autoIn.k, 512, [](size_t) {});
    bool rg = ya == yb;
    CFRelease(plist);
    info += string("\"state\":{\"parametersGelijk\":") + (pg ? "true" : "false") + ",\"renderGelijk\":" + (rg ? "true" : "false") + ",\"verschil\":" + jsonStr(verschil) + ",\"voorInitialize\":" + jsonStr(voorTekst) + "},";
    printf("state: parameters %s, render %s %s\n", pg ? "gelijk" : "ANDERS", rg ? "gelijk" : "ANDERS", verschil.c_str());
  }
  // 6. bypass (gemeld)
  {
    Plug p(comp, sr); p.init();
    UInt32 aan = 1; OSStatus s = AudioUnitSetProperty(p.au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &aan, sizeof aan);
    string wat;
    if (s != noErr) { char t[64]; snprintf(t, sizeof t, "niet ondersteund (OSStatus %d)", (int)s); wat = t; }
    else {
      // de plugin vloeit 10 ms over: vergelijk vanaf 20 ms
      auto y = p.render(autoIn.k, 512, [](size_t) {});
      size_t van = (size_t)(0.020 * sr); bool gelijk = true;
      for (size_t c = 0; c < y.size() && gelijk; ++c) for (size_t i = van; i < y[c].size(); ++i) if (y[c][i] != autoIn.k[c][i]) { gelijk = false; break; }
      wat = gelijk ? "uitgang = ingang (bit voor bit, vanaf 20 ms)" : "aan, maar de uitgang is niet de ingang";
    }
    info += "\"bypass\":" + jsonStr(wat) + "}";
    printf("bypass: %s\n", wat.c_str());
  }
  ofstream(uit + "/info.json") << info << "\n";
  return 0;
}

// ---- instrument ---------------------------------------------------------------------------------------------------------
static bool bestaat(const string& pad) { return (bool)ifstream(pad); }
static string parametersJson(Plug& p) {
  string o = "["; bool eerst = true; char buf[256];
  for (auto& [naam, in] : p.infoVan) { snprintf(buf, sizeof buf, "%s{\"naam\":%s,\"min\":%.9g,\"max\":%.9g,\"standaard\":%.9g}", eerst ? "" : ",", jsonStr(naam).c_str(), in.minValue, in.maxValue, in.defaultValue); o += buf; eerst = false; }
  return o + "]";
}

static int instrumentProef(AudioComponent comp, const string& tmap, const string& uit) {
  const uint32_t sr = 48000;
  string info = "{\"soort\":\"instrument\",";
  {
    Plug p(comp, sr, true); p.init();
    Float64 lat = 0, tail = 0; UInt32 s = sizeof(Float64);
    AudioUnitGetProperty(p.au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &lat, &s); s = sizeof(Float64);
    AudioUnitGetProperty(p.au, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &tail, &s);
    UInt32 nIn = 0; s = sizeof nIn;
    AudioUnitGetProperty(p.au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &nIn, &s);
    char buf[160]; snprintf(buf, sizeof buf, "\"latencySec\":%.9g,\"tailSec\":%.9g,\"ingangen\":%u,\"params\":", lat, tail, (unsigned)nIn);
    info += buf + parametersJson(p) + ",";
    printf("AU (instrument) geladen: latency %.6f s, TailTime %.3f s, %u ingangen, %zu parameters\n", lat, tail, (unsigned)nIn, p.infoVan.size());
  }
  // de gewone gevallen (au-gevallen.txt: geval, sr, dan per parameter de waarde), zonder host-callbacks: pluginval's
  // AU-host meldt een tempo, en een sequencer volgt dat; hier speelt hij op zijn eigen Tempo-knop zoals de WASM
  if (bestaat(tmap + "/au-gevallen.txt")) {
    ifstream gf(tmap + "/au-gevallen.txt"); string regel; getline(gf, regel);
    vector<string> kop; { stringstream ss(regel); string t; while (getline(ss, t, '\t')) kop.push_back(t); }
    size_t aantal = 0;
    while (getline(gf, regel)) {
      if (regel.empty()) continue;
      vector<string> v; { stringstream ss(regel); string t; while (getline(ss, t, '\t')) v.push_back(t); }
      const string naam = v[0]; const uint32_t gsr = (uint32_t)strtoul(v[1].c_str(), nullptr, 10);
      Wav in = leesWav(tmap + "/" + naam + ".in.wav");
      Plug p(comp, gsr, true);   // waarden vóór Initialize, zoals de effectproef (patch params-voor-init)
      for (size_t i = 2; i < kop.size() && i < v.size(); ++i) p.zet(kop[i], strtof(v[i].c_str(), nullptr));
      p.init();
      schrijfWav(uit + "/geval-" + naam + ".wav", p.render(in.k, 512, [](size_t) {}), gsr);
      ++aantal;
    }
    printf("gevallen: %zu zonder host-callbacks\n", aantal);
  }
  // noten als MIDI, op hun sample in het blok; blok 512 en 64
  struct Noot { uint64_t smp; bool aan; int n; };
  vector<Noot> noten;
  if (bestaat(tmap + "/noten/noten.txt")) {
    ifstream f(tmap + "/noten/noten.txt"); uint64_t smp; string wat; int n;
    while (f >> smp >> wat >> n) noten.push_back({smp, wat == "aan", n});
    Wav stil = leesWav(tmap + "/noten/in.wav");
    for (uint32_t blok : {512u, 64u}) {
      Plug p(comp, sr, true); p.init();
      size_t i = 0;
      auto y = p.render(stil.k, blok, [&](size_t b) {
        uint64_t van = (uint64_t)b * blok, tot = van + blok;
        for (; i < noten.size() && noten[i].smp < tot; ++i)
          fout("MIDI", MusicDeviceMIDIEvent(p.au, noten[i].aan ? 0x90 : 0x80, (UInt32)noten[i].n, noten[i].aan ? 100 : 0, (UInt32)(noten[i].smp - van)));
      });
      schrijfWav(uit + "/noten-" + to_string(blok) + ".wav", y, sr);
    }
    printf("noten: %zu als MIDI gespeeld (blok 512 en 64)\n", noten.size());
  }
  // het tempo van de host
  if (bestaat(tmap + "/hosttempo/tempo.txt")) {
    double tempo = 0; ifstream(tmap + "/hosttempo/tempo.txt") >> tempo;
    Wav stil = leesWav(tmap + "/hosttempo/in.wav");
    Plug p(comp, sr, true); p.hostTempo(tempo); p.init();
    schrijfWav(uit + "/hosttempo.wav", p.render(stil.k, 512, [](size_t) {}), sr);
    printf("hosttempo: %.1f bpm via de host-callbacks\n", tempo);
  }
  // state: ClassInfo van A in een nieuwe instantie B (na Initialize); dezelfde waarden en dezelfde render
  {
    map<string, float> waarden;
    { Plug p(comp, sr, true); size_t k = 0;
      for (auto& [naam, in] : p.infoVan) {
        if (naam == "Bypass") continue;
        const float f[] = {0.75f, 0.25f, 0.625f, 0.375f};
        waarden[naam] = in.minValue + f[k++ % 4] * (in.maxValue - in.minValue);
      } }
    Plug a(comp, sr, true); a.init(); for (auto& [k, v] : waarden) a.zet(k, v);
    CFPropertyListRef plist = nullptr; UInt32 s = sizeof plist;
    fout("ClassInfo lezen", AudioUnitGetProperty(a.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &s));
    Plug b(comp, sr, true); b.init();
    fout("ClassInfo zetten", AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof plist));
    string verschil; bool pg = true;
    for (auto& [k, v] : waarden) { float va = a.lees(k), vb = b.lees(k); if (va != vb) { pg = false; char t[160]; snprintf(t, sizeof t, "%s %.9g→%.9g; ", k.c_str(), va, vb); verschil += t; } }
    vector<vector<float>> stil(2, vector<float>(sr * 2));
    bool rg = a.render(stil, 512, [](size_t) {}) == b.render(stil, 512, [](size_t) {});
    CFRelease(plist);
    info += string("\"state\":{\"parametersGelijk\":") + (pg ? "true" : "false") + ",\"renderGelijk\":" + (rg ? "true" : "false") + ",\"verschil\":" + jsonStr(verschil) + "}}";
    printf("state: parameters %s, render %s %s\n", pg ? "gelijk" : "ANDERS", rg ? "gelijk" : "ANDERS", verschil.c_str());
  }
  ofstream(uit + "/info.json") << info << "\n";
  return 0;
}
