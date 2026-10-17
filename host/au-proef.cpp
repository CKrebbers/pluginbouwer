// au-proef.cpp — een kleine AU-host (macOS, AudioToolbox) die de plugin gebruikt zoals een DAW: laden, initialiseren,
// renderen met een invoer-callback, parameters per blok automatiseren, state (ClassInfo) bewaren en terugzetten in een
// nieuwe instantie, TailTime en bypass. Hij oordeelt niet: hij schrijft WAV's en info.json, tools/au-proef.mjs toetst.
//   au-proef <type> <subtype> <maker> <map met testsignalen/<ontwerp>/au> <uitmap>
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

struct Plug {
  AudioComponentInstance au = nullptr; Bron bron; uint32_t sr; uint32_t maxBlok = 512; Float64 tijd = 0;
  map<string, AudioUnitParameterID> idVan; map<string, AudioUnitParameterInfo> infoVan;
  Plug(AudioComponent comp, uint32_t sr_) : sr(sr_) {
    fout("instantiëren", AudioComponentInstanceNew(comp, &au));
    AudioStreamBasicDescription f{}; f.mSampleRate = sr; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    fout("formaat in", AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f));
    fout("formaat uit", AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f));
    fout("max blok", AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxBlok, sizeof maxBlok));
    AURenderCallbackStruct cb{invoer, &bron};
    fout("invoer-callback", AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb));
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

int main(int argc, char** argv) {
  if (argc < 6) { fprintf(stderr, "gebruik: au-proef <type> <subtype> <maker> <testmap> <uitmap>\n"); return 2; }
  AudioComponentDescription d{code(argv[1]), code(argv[2]), code(argv[3]), 0, 0};
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) { fprintf(stderr, "FOUT: AU %s %s %s niet gevonden (geïnstalleerd en AudioComponentRegistrar herstart?)\n", argv[1], argv[2], argv[3]); return 2; }
  string tmap = argv[4], uit = argv[5];
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
    Plug b(comp, sr);
    fout("ClassInfo zetten", AudioUnitSetProperty(b.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof plist));
    b.init();
    string verschil; bool pg = true;
    for (auto& [k, v] : stateWaarden) { float va = a.lees(k), vb = b.lees(k); if (va != vb) { pg = false; char t[160]; snprintf(t, sizeof t, "%s %.9g→%.9g; ", k.c_str(), va, vb); verschil += t; } }
    auto ya = a.render(autoIn.k, 512, [](size_t) {}), yb = b.render(autoIn.k, 512, [](size_t) {});
    bool rg = ya == yb;
    CFRelease(plist);
    info += string("\"state\":{\"parametersGelijk\":") + (pg ? "true" : "false") + ",\"renderGelijk\":" + (rg ? "true" : "false") + ",\"verschil\":" + jsonStr(verschil) + "},";
    printf("state: parameters %s, render %s %s\n", pg ? "gelijk" : "ANDERS", rg ? "gelijk" : "ANDERS", verschil.c_str());
  }
  // 6. bypass (gemeld)
  {
    Plug p(comp, sr); p.init();
    UInt32 aan = 1; OSStatus s = AudioUnitSetProperty(p.au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &aan, sizeof aan);
    string wat;
    if (s != noErr) { char t[64]; snprintf(t, sizeof t, "niet ondersteund (OSStatus %d)", (int)s); wat = t; }
    else { auto y = p.render(autoIn.k, 512, [](size_t) {}); wat = y == autoIn.k ? "uitgang = ingang (bit voor bit)" : "aan, maar de uitgang is niet de ingang"; }
    info += "\"bypass\":" + jsonStr(wat) + "}";
    printf("bypass: %s\n", wat.c_str());
  }
  ofstream(uit + "/info.json") << info << "\n";
  return 0;
}
