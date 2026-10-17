// pb-au-presets — zijn de AU factory presets echt? Een kleine AU-host (macOS, AudioToolbox) die de geïnstalleerde AU
// opent zoals Logic: Initialize, kAudioUnitProperty_FactoryPresets lezen, elke preset zetten met
// kAudioUnitProperty_PresentPreset en dan de parameters teruglezen.
//
//   pb-au-presets <tabel.tsv> [type subtype maker]      (standaard aufx PDca Vrve)
//
// Wat hij toetst (exitcode 1 bij een fout): er zijn presets; elke preset laat zich zetten (noErr), PresentPreset geeft
// daarna hetzelfde nummer en dezelfde naam, en de ClassInfo (wat Logic in het project bewaart) draagt die naam. Een
// nummer buiten de lijst wordt geweigerd. De parameterwaarden gaan naar <tabel.tsv> (naam, naam, parameter, waarde);
// `node gereedschap/vergelijk-presets.mjs --au <tabel.tsv>` legt ze tegen presets/*.json.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_fouten = 0;
void fout(const std::string& t) { std::fprintf(stderr, "FOUT: %s\n", t.c_str()); ++g_fouten; }
[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-au-presets: %s\n", t.c_str()); std::exit(2); }

std::string tekst(CFStringRef s) {
  if (!s) return "(geen)";
  char b[512];
  if (!CFStringGetCString(s, b, sizeof b, kCFStringEncodingUTF8)) return "(?)";
  return b;
}
OSType code(const char* c) {
  if (std::strlen(c) != 4) stop(std::string("geen vier tekens: ") + c);
  return ((OSType)(unsigned char)c[0] << 24) | ((OSType)(unsigned char)c[1] << 16) |
         ((OSType)(unsigned char)c[2] << 8) | (OSType)(unsigned char)c[3];
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 5) stop("gebruik: pb-au-presets <tabel.tsv> [type subtype maker]");
  AudioComponentDescription d{};
  d.componentType = code(argc == 5 ? argv[2] : "aufx");
  d.componentSubType = code(argc == 5 ? argv[3] : "PDca");
  d.componentManufacturer = code(argc == 5 ? argv[4] : "Vrve");
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) stop("AU niet gevonden (geïnstalleerd, en AudioComponentRegistrar opnieuw gestart?)");
  AudioUnit au = nullptr;
  if (AudioComponentInstanceNew(comp, &au) != noErr || !au) stop("AudioComponentInstanceNew faalt");
  if (AudioUnitInitialize(au) != noErr) stop("AudioUnitInitialize faalt");

  CFArrayRef lijst = nullptr;
  UInt32 maat = sizeof lijst;
  OSStatus e = AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &lijst, &maat);
  if (e != noErr || !lijst) stop("kAudioUnitProperty_FactoryPresets: fout " + std::to_string(e));
  const CFIndex n = CFArrayGetCount(lijst);
  std::printf("%ld factory presets\n", (long)n);
  if (n < 1) stop("geen factory presets");

  // de parameters (ids en namen) één keer
  UInt32 pm = 0;
  if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &pm, nullptr) != noErr)
    stop("ParameterList faalt");
  std::vector<AudioUnitParameterID> ids(pm / sizeof(AudioUnitParameterID));
  if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &pm) != noErr)
    stop("ParameterList faalt");
  std::vector<std::string> namen;
  for (auto id : ids) {
    AudioUnitParameterInfo info{};
    UInt32 im = sizeof info;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &im) != noErr)
      stop("ParameterInfo faalt");
    if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) && info.cfNameString) {
      namen.push_back(tekst(info.cfNameString));
      if (info.flags & kAudioUnitParameterFlag_CFNameRelease) CFRelease(info.cfNameString);
    } else {
      namen.push_back(info.name);
    }
  }

  FILE* tabel = std::fopen(argv[1], "w");
  if (!tabel) stop(std::string("kan niet schrijven: ") + argv[1]);
  for (CFIndex k = 0; k < n; ++k) {
    const AUPreset p = *static_cast<const AUPreset*>(CFArrayGetValueAtIndex(lijst, k));
    const std::string naam = tekst(p.presetName);
    if (p.presetNumber != (SInt32)k) fout(naam + ": nummer " + std::to_string(p.presetNumber) + " op plek " + std::to_string(k));
    e = AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &p, sizeof p);
    if (e != noErr) { fout(naam + ": PresentPreset zetten gaf fout " + std::to_string(e)); continue; }

    AUPreset nu{};
    UInt32 nm = sizeof nu;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &nu, &nm) != noErr) {
      fout(naam + ": PresentPreset lezen faalt");
    } else {
      if (nu.presetNumber != p.presetNumber || tekst(nu.presetName) != naam)
        fout(naam + ": PresentPreset geeft " + std::to_string(nu.presetNumber) + " '" + tekst(nu.presetName) + "'");
      if (nu.presetName) CFRelease(nu.presetName);
    }

    CFPropertyListRef klasse = nullptr;
    UInt32 km = sizeof klasse;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &klasse, &km) != noErr || !klasse) {
      fout(naam + ": ClassInfo lezen faalt");
    } else {
      auto* kn = static_cast<CFStringRef>(CFDictionaryGetValue(static_cast<CFDictionaryRef>(klasse), CFSTR(kAUPresetNameKey)));
      if (tekst(kn) != naam) fout(naam + ": ClassInfo draagt de naam '" + tekst(kn) + "'");
      CFRelease(klasse);
    }

    for (size_t i = 0; i < ids.size(); ++i) {
      AudioUnitParameterValue v = 0;
      if (AudioUnitGetParameter(au, ids[i], kAudioUnitScope_Global, 0, &v) != noErr) fout(naam + ": GetParameter faalt");
      std::fprintf(tabel, "%s\t%s\t%s\t%.9g\n", naam.c_str(), naam.c_str(), namen[i].c_str(), (double)v);
    }
    std::printf("  ok  '%s' (nummer %d) gezet; PresentPreset en ClassInfo dragen de naam\n", naam.c_str(), (int)p.presetNumber);
  }
  std::fclose(tabel);

  AUPreset buiten{(SInt32)n, CFSTR("bestaat niet")};
  if (AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &buiten, sizeof buiten) == noErr)
    fout("een presetnummer buiten de lijst werd geaccepteerd");
  else
    std::printf("  ok  presetnummer %ld (buiten de lijst) geweigerd\n", (long)n);

  CFRelease(lijst);
  AudioUnitUninitialize(au);
  AudioComponentInstanceDispose(au);
  if (g_fouten) { std::fprintf(stderr, "pb-au-presets: %d fout(en)\n", g_fouten); return 1; }
  std::printf("pb-au-presets: alles groen (%ld presets)\n", (long)n);
  return 0;
}
