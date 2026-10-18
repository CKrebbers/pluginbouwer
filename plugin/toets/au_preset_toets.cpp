// au_preset_toets — de AU-kant van fase 4 (macOS): de fabriekspresets als AU factory presets, door de geïnstalleerde
// .component (AudioToolbox, zoals Logic hem laadt).
//
//   au_preset_toets
//
//   - een verse instantie staat op "Untitled" (−1), niet op preset 0 (ARCHITECTUUR.md "Presets");
//   - kAudioUnitProperty_FactoryPresets geeft precies de presets uit presets/*.json, in die volgorde, met hun naam;
//   - elke preset laadt via kAudioUnitProperty_PresentPreset (vóór én na AudioUnitInitialize): daarna is hij de
//     huidige preset en staat elke parameter op de waarde uit de JSON, of op zijn standaard als hij er niet in staat.
// De verwachte waarden komen uit presets_tabel.h (door CMake uit dezelfde JSON-bestanden gemaakt).
// Uitvoer: per controle "ok" of "FOUT"; exitcode 0 alleen als alles ok is.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "presets_tabel.h"

namespace {

int fouten = 0, controles = 0;
void controle(bool goed, const std::string& wat) {
  ++controles;
  if (!goed) ++fouten;
  std::printf("%s  %s\n", goed ? "ok  " : "FOUT", wat.c_str());
}

std::string tekst(CFStringRef s) {
  if (!s) return "(null)";
  char buf[256];
  if (!CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) return "(?)";
  return buf;
}

using pb::Ontwerp;

// alle parameters zoals de preset ze zet; mis = de namen die afwijken
bool waardenKloppen(AudioUnit au, const dub::Preset& p, std::string& mis) {
  double verwacht[Ontwerp::N_PARAMS];
  for (int i = 0; i < Ontwerp::N_PARAMS; ++i) verwacht[i] = Ontwerp::PARAMS[i].standaard;
  for (int k = 0; k < p.n; ++k) verwacht[dub::paramBijNaam(p.waarden[k].naam)] = p.waarden[k].waarde;
  bool goed = true;
  for (int i = 0; i < Ontwerp::N_PARAMS; ++i) {
    AudioUnitParameterValue v = NAN;
    const OSStatus s = AudioUnitGetParameter(au, Ontwerp::PARAMS[i].id, kAudioUnitScope_Global, 0, &v);
    const double marge = 1e-6 * (Ontwerp::PARAMS[i].max - Ontwerp::PARAMS[i].min);
    if (s != noErr || !(std::fabs((double)v - verwacht[i]) <= marge)) {
      goed = false;
      char b[128];
      std::snprintf(b, sizeof b, " %s=%g (verwacht %g, status %d)", Ontwerp::PARAMS[i].naam, (double)v, verwacht[i],
                    (int)s);
      mis += b;
    }
  }
  return goed;
}

void laadAlle(AudioUnit au, CFArrayRef lijst, const char* hoe) {
  // achterstevoren, en elk twee keer: wat de vorige zette mag niet blijven hangen
  for (int herhaal = 0; herhaal < 2; ++herhaal) {
    for (CFIndex k = CFArrayGetCount(lijst) - 1; k >= 0; --k) {
      const AUPreset* ap = static_cast<const AUPreset*>(CFArrayGetValueAtIndex(lijst, k));
      if (!ap || ap->presetNumber < 0 || ap->presetNumber >= dub::N_PRESETS) continue;
      const dub::Preset& p = dub::PRESETS[ap->presetNumber];
      const OSStatus s = AudioUnitSetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, ap,
                                              sizeof(AUPreset));
      AUPreset nu = {-2, nullptr};
      UInt32 grootte = sizeof nu;
      const OSStatus s2 =
          AudioUnitGetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &nu, &grootte);
      const bool huidig = s2 == noErr && nu.presetNumber == ap->presetNumber && tekst(nu.presetName) == p.naam;
      if (s2 == noErr && nu.presetName) CFRelease(nu.presetName);
      std::string mis;
      const bool waarden = waardenKloppen(au, p, mis);
      if (herhaal == 0 || s != noErr || !huidig || !waarden)
        controle(s == noErr && huidig && waarden,
                 std::string(hoe) + ": \"" + p.naam + "\" laadt (status " + std::to_string((int)s) +
                     "), is de huidige preset, alle parameters zoals de JSON" + (mis.empty() ? "" : ";" + mis));
    }
  }
}

}  // namespace

int main() {
  AudioComponentDescription d = {};
  auto code = [](const char* t) { return (OSType)((t[0] << 24) | (t[1] << 16) | (t[2] << 8) | t[3]); };
  d.componentType = code("aufx");
  d.componentSubType = code("PDca");
  d.componentManufacturer = code("Vrve");
  AudioComponent c = AudioComponentFindNext(nullptr, &d);
  if (!c) { std::fprintf(stderr, "AU aufx PDca Vrve niet gevonden (geïnstalleerd? AudioComponentRegistrar?)\n"); return 2; }
  AudioUnit au = nullptr;
  if (AudioComponentInstanceNew(c, &au) != noErr || !au) { std::fprintf(stderr, "AudioComponentInstanceNew mislukt\n"); return 2; }

  AUPreset nu = {-2, nullptr};
  UInt32 grootte = sizeof nu;
  OSStatus s = AudioUnitGetProperty(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0, &nu, &grootte);
  controle(s == noErr && nu.presetNumber == -1,
           "een verse instantie staat op preset " + std::to_string((int)nu.presetNumber) + " \"" + tekst(nu.presetName) +
               "\" (verwacht −1, Untitled)");
  if (s == noErr && nu.presetName) CFRelease(nu.presetName);

  CFArrayRef lijst = nullptr;
  grootte = sizeof lijst;
  s = AudioUnitGetProperty(au, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global, 0, &lijst, &grootte);
  const CFIndex n = (s == noErr && lijst) ? CFArrayGetCount(lijst) : 0;
  controle(n == dub::N_PRESETS, "kAudioUnitProperty_FactoryPresets: " + std::to_string((long)n) + " presets (verwacht " +
                                    std::to_string(dub::N_PRESETS) + ", status " + std::to_string((int)s) + ")");
  for (CFIndex k = 0; k < n; ++k) {
    const AUPreset* ap = static_cast<const AUPreset*>(CFArrayGetValueAtIndex(lijst, k));
    const std::string naam = ap ? tekst(ap->presetName) : "(null)";
    const bool goed = ap && k < dub::N_PRESETS && ap->presetNumber == (SInt32)k && naam == dub::PRESETS[k].naam;
    controle(goed, "factory preset " + std::to_string((long)k) + ": \"" + naam + "\"" +
                       (k < dub::N_PRESETS ? std::string(" (presets/") + dub::PRESETS[k].sleutel + ".json)" : ""));
  }

  if (n > 0) {
    laadAlle(au, lijst, "vóór Initialize");
    s = AudioUnitInitialize(au);
    controle(s == noErr, "AudioUnitInitialize (status " + std::to_string((int)s) + ")");
    if (s == noErr) {
      laadAlle(au, lijst, "na Initialize");
      AudioUnitUninitialize(au);
    }
  }
  if (lijst) CFRelease(lijst);
  AudioComponentInstanceDispose(au);
  std::printf("au-preset-toets: %d/%d controles ok, %d fout\n", controles - fouten, controles, fouten);
  return fouten == 0 ? 0 : 1;
}
