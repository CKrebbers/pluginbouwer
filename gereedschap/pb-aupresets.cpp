// pb-aupresets — toetst de fabriekspresets door de geïnstalleerde AU (PROMPTS.md fase 4; alleen macOS).
//
//   pb-aupresets <verwacht.tsv>
//
// verwacht.tsv maakt gereedschap/presets-verwacht.sh uit presets/*.json (sleutel, naam, parameternaam, waarde).
// De AU (aumu PTgg Vrve, uit ~/Library/Audio/Plug-Ins/Components) moet:
//   - kAudioUnitProperty_FactoryPresets geven: precies de presets uit presets/, met hun naam, genummerd 0..n−1;
//   - als verse instantie op "Untitled" (−1) staan, niet op preset 0 (ARCHITECTUUR.md "Presets"), op de standaarden;
//   - bij het zetten van kAudioUnitProperty_PresentPreset, vóór én na Initialize, precies de waarden van die preset
//     tonen (ontbrekende namen hun standaard) en PresentPreset dat nummer en die naam geven;
//   - in ClassInfo de naam van de preset bewaren (ook van een eigen preset van de host, nummer −1), en die ClassInfo laadt in een nieuwe instantie tot dezelfde waarden;
//   - een onbekend presetnummer weigeren zonder iets te veranderen.
// Exitcode 0 = alles groen; elke mislukte controle staat als "FOUT" in de uitvoer.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

int fouten = 0, controles = 0;
void check(bool ok, const std::string& wat) {
  ++controles;
  if (!ok) ++fouten;
  std::printf("%s  %s\n", ok ? "ok  " : "FOUT", wat.c_str());
}
[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-aupresets: %s\n", t.c_str()); std::exit(2); }

std::string utf8(CFStringRef s) {
  if (!s) return "";
  char b[512];
  if (!CFStringGetCString(s, b, sizeof b, kCFStringEncodingUTF8)) return "?";
  return b;
}
std::string getal(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.9g", v);
  return b;
}

struct VerwachtePreset { std::string sleutel, naam; std::map<std::string, double> waarden; };
std::vector<VerwachtePreset> leesVerwacht(const char* bestand) {  // in de volgorde van de sleutels, zoals de tabel
  std::ifstream f(bestand);
  if (!f) stop(std::string("kan ") + bestand + " niet lezen");
  std::map<std::string, VerwachtePreset> v;
  std::string regel;
  while (std::getline(f, regel)) {
    if (regel.empty()) continue;
    std::vector<std::string> d;
    std::stringstream ss(regel);
    std::string veld;
    while (std::getline(ss, veld, '\t')) d.push_back(veld);
    while (d.size() < 4) d.push_back("");
    auto& p = v[d[0]];
    p.sleutel = d[0];
    p.naam = d[1];
    if (!d[2].empty()) p.waarden[d[2]] = std::strtod(d[3].c_str(), nullptr);
  }
  std::vector<VerwachtePreset> uit;
  for (auto& kv : v) uit.push_back(kv.second);
  if (uit.empty()) stop(std::string(bestand) + " bevat geen presets");
  return uit;
}

struct Param { AudioUnitParameterID id; std::string naam; float standaard; };

AudioUnit maak() {
  AudioComponentDescription d = {kAudioUnitType_MusicDevice, 'PTgg', 'Vrve', 0, 0};
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) stop("AU aumu PTgg Vrve niet gevonden (geïnstalleerd? killall -9 AudioComponentRegistrar?)");
  AudioUnit au = nullptr;
  if (AudioComponentInstanceNew(comp, &au) != noErr || !au) stop("AU: geen instantie");
  UInt32 maxF = 512;
  AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF);
  return au;
}

std::vector<Param> leesParams(AudioUnit au) {
  UInt32 sz = 0;
  Boolean schrijfbaar = false;
  if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &schrijfbaar) != noErr)
    stop("AU: geen ParameterList");
  std::vector<AudioUnitParameterID> ids(sz / sizeof(AudioUnitParameterID));
  if (!ids.empty() && AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz) != noErr)
    stop("AU: ParameterList lezen mislukt");
  std::vector<Param> ps;
  for (AudioUnitParameterID id : ids) {
    AudioUnitParameterInfo info;
    std::memset(&info, 0, sizeof info);
    UInt32 isz = sizeof info;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &isz) != noErr)
      stop("AU: ParameterInfo lezen mislukt");
    std::string naam = (info.flags & kAudioUnitParameterFlag_HasCFNameString) ? utf8(info.cfNameString) : info.name;
    if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) && (info.flags & kAudioUnitParameterFlag_CFNameRelease))
      CFRelease(info.cfNameString);
    ps.push_back({id, naam, info.defaultValue});
  }
  return ps;
}

std::map<AudioUnitParameterID, float> waarden(AudioUnit au, const std::vector<Param>& ps) {
  std::map<AudioUnitParameterID, float> w;
  for (auto& p : ps) {
    AudioUnitParameterValue v = -12345;
    if (AudioUnitGetParameter(au, p.id, kAudioUnitScope_Global, 0, &v) != noErr) stop("AU: GetParameter mislukt");
    w[p.id] = v;
  }
  return w;
}
std::string tekst(const std::map<AudioUnitParameterID, float>& w) {
  std::string t;
  for (auto& kv : w) t += (t.empty() ? "" : " ") + std::to_string(kv.first) + "=" + getal(kv.second);
  return t;
}

// wat de preset moet geven: zijn waarden, de rest de standaard (als float: een AU-parameter is een float)
std::map<AudioUnitParameterID, float> verwachtVan(const VerwachtePreset& vp, const std::vector<Param>& ps, bool* namenOk) {
  std::map<AudioUnitParameterID, float> w;
  for (auto& p : ps) w[p.id] = p.standaard;
  *namenOk = true;
  for (auto& kv : vp.waarden) {
    bool gevonden = false;
    for (auto& p : ps)
      if (p.naam == kv.first) { w[p.id] = (float)kv.second; gevonden = true; }
    *namenOk = *namenOk && gevonden;
  }
  return w;
}

// één blok renderen (zo neemt de wrapper de parameter-events van de host over, zoals in een host die speelt)
bool render(AudioUnit au, Float64* tijd) {
  constexpr UInt32 BLOK = 512;
  AudioStreamBasicDescription fmt;
  std::memset(&fmt, 0, sizeof fmt);
  UInt32 fg = sizeof fmt;
  AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, &fg);
  const bool los = (fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
  const UInt32 kan = fmt.mChannelsPerFrame ? fmt.mChannelsPerFrame : 2;
  std::vector<float> data(BLOK * kan);
  std::vector<unsigned char> ablBuf(sizeof(AudioBufferList) + kan * sizeof(AudioBuffer));
  auto* abl = reinterpret_cast<AudioBufferList*>(ablBuf.data());
  abl->mNumberBuffers = los ? kan : 1;
  for (UInt32 c = 0; c < abl->mNumberBuffers; ++c) {
    abl->mBuffers[c].mNumberChannels = los ? 1 : kan;
    abl->mBuffers[c].mDataByteSize = (UInt32)(BLOK * sizeof(float) * (los ? 1 : kan));
    abl->mBuffers[c].mData = data.data() + (los ? c * BLOK : 0);
  }
  AudioUnitRenderActionFlags vl = 0;
  AudioTimeStamp ts;
  std::memset(&ts, 0, sizeof ts);
  ts.mSampleTime = *tijd;
  ts.mFlags = kAudioTimeStampSampleTimeValid;
  *tijd += BLOK;
  return AudioUnitRender(au, &vl, &ts, 0, BLOK, abl) == noErr;
}

bool huidige(AudioUnit au, SInt32* nummer, std::string* naam) {
  AUPreset p{};
  UInt32 sz = sizeof p;
  if (AudioUnitGetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, &sz) != noErr) return false;
  *nummer = p.presetNumber;
  *naam = utf8(p.presetName);
  if (p.presetName) CFRelease(p.presetName);  // de AU geeft hem geretaind
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) stop("gebruik: pb-aupresets <verwacht.tsv>");
  const auto verwacht = leesVerwacht(argv[1]);

  AudioUnit au = maak();
  const auto ps = leesParams(au);
  check(!ps.empty(), "AU: " + std::to_string(ps.size()) + " parameters");
  std::map<AudioUnitParameterID, float> standaard;
  for (auto& p : ps) standaard[p.id] = p.standaard;

  // de lijst
  CFArrayRef lijst = nullptr;
  UInt32 sz = sizeof lijst;
  const OSStatus st = AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &lijst, &sz);
  check(st == noErr && lijst, "AU: kAudioUnitProperty_FactoryPresets (status " + std::to_string(st) + ")");
  std::vector<AUPreset> presets;
  if (lijst) {
    for (CFIndex i = 0; i < CFArrayGetCount(lijst); ++i)
      presets.push_back(*static_cast<const AUPreset*>(CFArrayGetValueAtIndex(lijst, i)));
  }
  check(presets.size() == verwacht.size(),
        "AU: " + std::to_string(presets.size()) + " factory presets = " + std::to_string(verwacht.size()) + " in presets/");
  for (size_t i = 0; i < presets.size() && i < verwacht.size(); ++i) {
    std::printf("     factory preset %d: %s\n", (int)presets[i].presetNumber, utf8(presets[i].presetName).c_str());
    check(presets[i].presetNumber == (SInt32)i && utf8(presets[i].presetName) == verwacht[i].naam,
          "AU: preset " + std::to_string(i) + " heet \"" + utf8(presets[i].presetName) + "\" = presets/" + verwacht[i].sleutel +
              ".json \"" + verwacht[i].naam + "\"");
  }

  // een verse instantie: Untitled (−1), op de standaarden
  SInt32 nr = 0;
  std::string naam;
  check(huidige(au, &nr, &naam) && nr == -1, "AU: een verse instantie staat op preset " + std::to_string(nr) + " \"" + naam +
                                                 "\" (verwacht -1, Untitled), niet op preset 0");
  check(waarden(au, ps) == standaard, "AU: een verse instantie staat op de standaarden " + tekst(standaard));

  // elke preset, de eerste vóór Initialize (dan zet hij ook wat de host eerder zette opzij, valkuil 1), de rest erna
  for (int voorInit = 1; voorInit >= 0; --voorInit) {
    const char* modus = voorInit ? "voor Initialize" : "na Initialize";
    AudioUnit a = maak();
    Float64 tijd = 0;
    if (!voorInit) check(AudioUnitInitialize(a) == noErr, std::string("AU: Initialize [") + modus + "]");
    for (size_t i = 0; i < presets.size() && i < verwacht.size(); ++i) {
      // eerst iets anders zetten: de preset moet het overschrijven, ook de namen die hij niet noemt
      for (auto& p : ps) {
        AudioUnitParameterInfo info{};
        UInt32 isz = sizeof info;
        AudioUnitGetProperty(a, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, p.id, &info, &isz);
        if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) && (info.flags & kAudioUnitParameterFlag_CFNameRelease))
          CFRelease(info.cfNameString);
        AudioUnitSetParameter(a, p.id, kAudioUnitScope_Global, 0, info.minValue + 0.9f * (info.maxValue - info.minValue), 0);
      }
      if (!voorInit) render(a, &tijd);  // de host speelt: zijn waarden zijn binnen vóór de preset komt
      bool namenOk = false;
      const auto w = verwachtVan(verwacht[i], ps, &namenOk);
      const std::string wie = "AU: preset " + std::to_string(i) + " \"" + verwacht[i].naam + "\" [" + modus + "]";
      check(namenOk, wie + ": de parameternamen uit de JSON bestaan in de AU");
      const OSStatus zs = AudioUnitSetProperty(a, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &presets[i], sizeof(AUPreset));
      check(zs == noErr, wie + ": PresentPreset zetten (status " + std::to_string(zs) + ")");
      if (voorInit && i == 0) check(AudioUnitInitialize(a) == noErr, wie + ": Initialize daarna");
      const auto echt = waarden(a, ps);
      check(echt == w, wie + ": waarden " + tekst(echt) + " = de JSON (rest standaard) " + tekst(w));
      check(huidige(a, &nr, &naam) && nr == (SInt32)i && naam == verwacht[i].naam,
            wie + ": PresentPreset geeft " + std::to_string(nr) + " \"" + naam + "\"");
      const bool r1 = render(a, &tijd), r2 = render(a, &tijd);
      const auto naRender = waarden(a, ps);
      check(r1 && r2 && naRender == w, wie + ": na twee keer Render nog steeds " + tekst(naRender));
      if (voorInit) break;  // na Initialize gaat het verder in de lus hieronder
    }
    AudioComponentInstanceDispose(a);
  }

  // ClassInfo: de naam van de preset gaat mee, en laadt in een nieuwe instantie tot dezelfde waarden
  {
    AudioUnit a = maak(), b = maak();
    AudioUnitInitialize(a);
    AudioUnitInitialize(b);
    const size_t i = presets.size() > 1 ? 1 : 0;
    if (!presets.empty()) {
      AudioUnitSetProperty(a, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &presets[i], sizeof(AUPreset));
      CFPropertyListRef plist = nullptr;
      UInt32 psz = sizeof plist;
      const bool ok = AudioUnitGetProperty(a, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &psz) == noErr && plist;
      std::string pnaam = "?";
      if (ok && CFGetTypeID(plist) == CFDictionaryGetTypeID()) {
        const void* n = CFDictionaryGetValue((CFDictionaryRef)plist, CFSTR(kAUPresetNameKey));
        if (n && CFGetTypeID(n) == CFStringGetTypeID()) pnaam = utf8((CFStringRef)n);
      }
      check(ok && pnaam == verwacht[i].naam, "AU: ClassInfo na preset \"" + verwacht[i].naam + "\" heeft de naam \"" + pnaam + "\"");
      const bool geladen = ok && AudioUnitSetProperty(b, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof plist) == noErr;
      check(geladen && waarden(b, ps) == waarden(a, ps), "AU: die ClassInfo laadt in een nieuwe instantie tot " + tekst(waarden(b, ps)));
      if (plist) CFRelease(plist);
    }
    // een eigen preset van de host (nummer < 0): de naam gaat mee in ClassInfo (auval: "Preset name is not retained")
    AUPreset eigen = {-1, CFSTR("Eigen klank")};
    const OSStatus es = AudioUnitSetProperty(a, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &eigen, sizeof eigen);
    CFPropertyListRef plist2 = nullptr;
    UInt32 psz2 = sizeof plist2;
    std::string enaam = "?";
    if (es == noErr && AudioUnitGetProperty(a, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist2, &psz2) == noErr && plist2) {
      if (CFGetTypeID(plist2) == CFDictionaryGetTypeID()) {
        const void* n = CFDictionaryGetValue((CFDictionaryRef)plist2, CFSTR(kAUPresetNameKey));
        if (n && CFGetTypeID(n) == CFStringGetTypeID()) enaam = utf8((CFStringRef)n);
      }
      CFRelease(plist2);
    }
    check(huidige(a, &nr, &naam) && nr == -1 && naam == "Eigen klank" && enaam == "Eigen klank",
          "AU: een eigen preset (-1, \"Eigen klank\") staat in PresentPreset (" + std::to_string(nr) + " \"" + naam +
              "\") en in ClassInfo (\"" + enaam + "\")");

    // een onbekend nummer: geweigerd, niets veranderd
    const auto w0 = waarden(a, ps);
    SInt32 nr0 = 0;
    std::string naam0;
    huidige(a, &nr0, &naam0);
    AUPreset vreemd = {(SInt32)presets.size() + 7, CFSTR("bestaat niet")};
    const OSStatus vs = AudioUnitSetProperty(a, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &vreemd, sizeof vreemd);
    check(huidige(a, &nr, &naam) && waarden(a, ps) == w0 && nr == nr0,
          "AU: een onbekend presetnummer (status " + std::to_string(vs) + ") verandert de waarden en de huidige preset niet");
    AudioComponentInstanceDispose(a);
    AudioComponentInstanceDispose(b);
  }

  if (lijst) CFRelease(lijst);
  AudioComponentInstanceDispose(au);
  std::printf("\n%d controles, %d fout\n", controles, fouten);
  return fouten ? 1 : 0;
}
