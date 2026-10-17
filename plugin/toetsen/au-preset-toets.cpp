// au-preset-toets — de AU-kant van fase 4 (macOS): de fabriekspresets als AU factory presets, zoals Logic ze ziet.
//
//   au-preset-toets <verwacht.tsv>        (uit `node gereedschap/presets-plat.mjs presets`)
//
// Opent de geïnstalleerde AU (aufx PDca Vrve) via AudioToolbox en eist:
//   - kAudioUnitProperty_FactoryPresets: precies de presets uit verwacht.tsv, in die volgorde, nummer 0..n-1;
//   - kAudioUnitProperty_PresentPreset zetten, vóór en na AudioUnitInitialize: daarna geeft PresentPreset dat nummer en
//     die naam terug, staat elke AU-parameter op de waarde uit de JSON (of zijn standaard), en draagt de ClassInfo
//     (de state die Logic bewaart) de presetnaam en de waarden van de preset in de CLAP-state;
//   - een preset gezet ná een SetParameter vóór Initialize wint (de wrapper bewaart zulke waarden tot Initialize);
//   - een onbekend presetnummer geeft een fout.
// Exitcode 0 = geslaagd; elke fout staat in de uitvoer met "FOUT".
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

namespace {

int g_fouten = 0;
void fout(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); ++g_fouten; }
[[noreturn]] void stop(const std::string& t) { std::printf("FOUT: %s\n", t.c_str()); std::exit(2); }
void ok(const std::string& t) { std::printf("ok: %s\n", t.c_str()); }

std::string tekst(CFStringRef s) {
  if (!s) return "(null)";
  char b[512];
  if (!CFStringGetCString(s, b, sizeof b, kCFStringEncodingUTF8)) return "(?)";
  return b;
}

struct Verwacht {
  std::string sleutel, naam;
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
    if (v.size() < 3) stop("onleesbare regel: " + regel);
    Verwacht p{v[0], v[1], {}};
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

struct Param { AudioUnitParameterID id; std::string naam; float standaard; };

std::vector<Param> params(AudioUnit au) {
  UInt32 grootte = 0;
  if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &grootte, nullptr))
    stop("geen ParameterList");
  std::vector<AudioUnitParameterID> ids(grootte / sizeof(AudioUnitParameterID));
  if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &grootte))
    stop("ParameterList lezen mislukt");
  std::vector<Param> r;
  for (AudioUnitParameterID id : ids) {
    AudioUnitParameterInfo info{};
    UInt32 g = sizeof info;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &g))
      stop("ParameterInfo mislukt voor id " + std::to_string(id));
    std::string naam = (info.flags & kAudioUnitParameterFlag_HasCFNameString) ? tekst(info.cfNameString) : info.name;
    if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) && (info.flags & kAudioUnitParameterFlag_CFNameRelease))
      CFRelease(info.cfNameString);
    r.push_back({id, naam, info.defaultValue});
  }
  return r;
}

// De CLAP-state uit de ClassInfo ("PBST", versie, aantal, dan per parameter id u32 + waarde f64; little-endian)
std::map<uint32_t, double> clapState(CFDictionaryRef d) {
  std::map<uint32_t, double> r;
  auto data = static_cast<CFDataRef>(CFDictionaryGetValue(d, CFSTR(kAUPresetDataKey)));
  if (!data || CFGetTypeID(data) != CFDataGetTypeID()) { fout("ClassInfo zonder data"); return r; }
  const UInt8* b = CFDataGetBytePtr(data);
  const CFIndex n = CFDataGetLength(data);
  auto u32 = [&](CFIndex o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; };
  if (n < 12 || std::memcmp(b, "PBST", 4)) { fout("ClassInfo-data is geen PBST-state"); return r; }
  const uint32_t aantal = u32(8);
  if (n < 12 + 12 * (CFIndex)aantal) { fout("ClassInfo-data te kort"); return r; }
  for (uint32_t k = 0; k < aantal; ++k) {
    const CFIndex o = 12 + 12 * (CFIndex)k;
    const uint64_t bits = (uint64_t)u32(o + 4) | ((uint64_t)u32(o + 8) << 32);
    double v;
    std::memcpy(&v, &bits, 8);
    r[u32(o)] = v;
  }
  return r;
}

void zetPreset(AudioUnit au, SInt32 nr, CFStringRef naam, OSStatus& st) {
  AUPreset p = {nr, naam};
  st = AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, sizeof p);
}

// Na het zetten van preset `nr`: PresentPreset, de AU-parameters en (als de AU geïnitialiseerd is) de ClassInfo.
void controleer(AudioUnit au, const std::vector<Param>& ps, const Verwacht& v, SInt32 nr, bool geinit, const char* wanneer) {
  const int voor = g_fouten;
  const std::string wie = std::string("preset ") + std::to_string(nr) + " '" + v.naam + "' (" + wanneer + ")";
  AUPreset nu{};
  UInt32 g = sizeof nu;
  if (AudioUnitGetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &nu, &g)) {
    fout(wie + ": PresentPreset lezen mislukt");
  } else {
    if (nu.presetNumber != nr || tekst(nu.presetName) != v.naam)
      fout(wie + ": PresentPreset is " + std::to_string(nu.presetNumber) + " '" + tekst(nu.presetName) + "'");
    if (nu.presetName) CFRelease(nu.presetName);
  }
  std::map<std::string, double> moet;
  for (const Param& p : ps) {
    auto it = v.waarden.find(p.naam);
    moet[p.naam] = it != v.waarden.end() ? it->second : p.standaard;
    AudioUnitParameterValue w = 0;
    if (AudioUnitGetParameter(au, p.id, kAudioUnitScope_Global, 0, &w)) { fout(wie + ": GetParameter " + p.naam); continue; }
    if (w != (float)moet[p.naam]) {
      char t[200];
      std::snprintf(t, sizeof t, "%s: AU-parameter %s = %.9g, verwacht %.9g", wie.c_str(), p.naam.c_str(), w, moet[p.naam]);
      fout(t);
    }
  }
  if (geinit) {
    CFPropertyListRef pl = nullptr;
    g = sizeof pl;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &g) || !pl) {
      fout(wie + ": ClassInfo lezen mislukt");
    } else {
      auto d = static_cast<CFDictionaryRef>(pl);
      const std::string naam = tekst(static_cast<CFStringRef>(CFDictionaryGetValue(d, CFSTR(kAUPresetNameKey))));
      if (naam != v.naam) fout(wie + ": ClassInfo-naam '" + naam + "'");
      const auto st = clapState(d);
      for (const Param& p : ps) {
        auto it = st.find(p.id);
        if (it == st.end()) fout(wie + ": " + p.naam + " ontbreekt in de CLAP-state");
        else if (it->second != moet[p.naam]) {
          char t[200];
          std::snprintf(t, sizeof t, "%s: CLAP-state %s = %.17g, verwacht %.17g", wie.c_str(), p.naam.c_str(), it->second,
                        moet[p.naam]);
          fout(t);
        }
      }
      CFRelease(pl);
    }
  }
  if (g_fouten == voor) ok(wie + ": PresentPreset, alle " + std::to_string(ps.size()) + " parameters" +
                           (geinit ? " en de ClassInfo (naam en CLAP-state)" : "") + " kloppen");
}

int toets(const char* verwachtPad) {
  const std::vector<Verwacht> verwacht = leesVerwacht(verwachtPad);
  AudioComponentDescription cd = {kAudioUnitType_Effect, 'PDca', 'Vrve', 0, 0};
  AudioComponent c = AudioComponentFindNext(nullptr, &cd);
  if (!c) stop("AU aufx PDca Vrve niet gevonden (geïnstalleerd? killall -9 AudioComponentRegistrar?)");
  AudioUnit au = nullptr;
  if (AudioComponentInstanceNew(c, &au) || !au) stop("AudioComponentInstanceNew mislukt");

  // 1. de lijst
  CFArrayRef lijst = nullptr;
  UInt32 g = sizeof lijst;
  if (AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &lijst, &g) || !lijst)
    stop("geen kAudioUnitProperty_FactoryPresets");
  const CFIndex n = CFArrayGetCount(lijst);
  std::vector<std::pair<SInt32, std::string>> presets;
  std::vector<CFStringRef> namen;
  for (CFIndex i = 0; i < n; ++i) {
    auto* p = static_cast<const AUPreset*>(CFArrayGetValueAtIndex(lijst, i));
    presets.push_back({p->presetNumber, tekst(p->presetName)});
    namen.push_back(static_cast<CFStringRef>(CFRetain(p->presetName)));
  }
  CFRelease(lijst);
  if ((size_t)n != verwacht.size()) fout("verwacht " + std::to_string(verwacht.size()) + " factory presets, kreeg " + std::to_string(n));
  for (size_t i = 0; i < presets.size() && i < verwacht.size(); ++i) {
    std::printf("  factory preset %d: %s\n", (int)presets[i].first, presets[i].second.c_str());
    if (presets[i].first != (SInt32)i || presets[i].second != verwacht[i].naam)
      fout("factory preset " + std::to_string(i) + " is " + std::to_string(presets[i].first) + " '" + presets[i].second +
           "', verwacht '" + verwacht[i].naam + "'");
  }
  if (!g_fouten) ok("FactoryPresets: " + std::to_string(n) + " presets, in de volgorde en met de namen van presets/*.json");

  const std::vector<Param> ps = params(au);
  for (const Verwacht& v : verwacht)
    for (auto& [naam, w] : v.waarden) {
      bool bekend = false;
      for (const Param& p : ps) bekend = bekend || p.naam == naam;
      if (!bekend) fout("presets/" + v.sleutel + ".json: '" + naam + "' is geen AU-parameter");
    }

  // 2. vóór Initialize (zo zet Logic een preset bij het laden van een project of de presetlijst)
  const size_t m = std::min(namen.size(), verwacht.size());
  OSStatus st;
  for (size_t i = 0; i < m; ++i) {
    zetPreset(au, (SInt32)i, namen[i], st);
    if (st) { fout("PresentPreset " + std::to_string(i) + " zetten vóór Initialize: status " + std::to_string(st)); continue; }
    controleer(au, ps, verwacht[i], (SInt32)i, false, "vóór Initialize");
  }
  // een parameter vóór Initialize, daarna een preset: de preset wint, ook in de CLAP
  if (m >= 2) {
    AudioUnitSetParameter(au, ps[0].id, kAudioUnitScope_Global, 0, (float)(ps[0].standaard * 0.5 + 1), 0);
    zetPreset(au, 1, namen[1], st);
    if (st) fout("PresentPreset 1 na SetParameter: status " + std::to_string(st));
  }
  // 3. Initialize, en daarna elke preset nog eens (zo kiest de gebruiker er een uit de lijst in Logic)
  {
    AudioStreamBasicDescription f{};
    f.mSampleRate = 48000; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
  }
  if (AudioUnitInitialize(au)) stop("AudioUnitInitialize mislukt");
  if (m >= 2) controleer(au, ps, verwacht[1], 1, true, "na SetParameter vóór Initialize, dan Initialize");
  for (size_t i = m; i-- > 0;) {  // achterstevoren, zodat elke preset iets verandert
    zetPreset(au, (SInt32)i, namen[i], st);
    if (st) { fout("PresentPreset " + std::to_string(i) + " zetten na Initialize: status " + std::to_string(st)); continue; }
    controleer(au, ps, verwacht[i], (SInt32)i, true, "na Initialize");
  }
  zetPreset(au, (SInt32)verwacht.size() + 10, namen.empty() ? CFSTR("x") : namen[0], st);
  if (st == noErr) fout("een onbekend presetnummer gaf geen fout");
  else ok("onbekend presetnummer: fout " + std::to_string(st));

  for (CFStringRef s : namen) CFRelease(s);
  AudioUnitUninitialize(au);
  AudioComponentInstanceDispose(au);
  return g_fouten ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "gebruik: au-preset-toets <verwacht.tsv>\n");
    return 2;
  }
  const int r = toets(argv[1]);
  std::printf(r == 0 ? "GESLAAGD\n" : "MISLUKT (%d fouten)\n", g_fouten);
  return r;
}
