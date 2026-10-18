// pb-au-render — een kleine AU-host (macOS, AudioToolbox) voor de null-test van fase 5: rendert de geïnstalleerde AU
// op een gekozen samplerate, met een stereo- of mono-uitgang (de stream-indeling op de output-scope; valkuil 17), en
// schrijft de uitvoer als WAV. Zonder host-callbacks (geen tempo, geen transport; valkuil 23) en zonder audio-ingang
// (een instrument; valkuil 20). Dezelfde opties als pb-render, zodat ci-null.sh dezelfde lijst gebruikt:
//
//   pb-au-render <type:subtype:maker> <in.wav> <uit.wav> [--sr 48000] [--kanalen 1|2] [--blok 512 | --blok 3,128,…]
//                [--p id=waarde ...] [--noten f] [--automatie f]
//
//   <in.wav>       alleen voor de lengte (een instrument leest geen invoer)
//   --p            AudioUnitSetParameter vóór Initialize (de CLAP-waarde: clap-wrapper geeft het bereik zo door)
//   --noten f      per regel "sample aan|uit noot": MusicDeviceMIDIEvent 0x90/0x80 met de offset in het blok
//   --automatie f  per regel "sample id waarde", alleen op een blokgrens: id 5 (Bypass) als kAudioUnitProperty_
//                  BypassEffect (zoals een host bypass zet), een ander id als AudioUnitSetParameter
#include <AudioToolbox/AudioToolbox.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fout(const std::string& t) {
  std::fprintf(stderr, "pb-au-render: %s\n", t.c_str());
  std::exit(2);
}
void ok(OSStatus s, const char* wat) {
  if (s != noErr) fout(std::string(wat) + " gaf OSStatus " + std::to_string((int)s));
}

constexpr uint32_t BYPASS_ID = 5;

OSType fourcc(const std::string& s) {
  if (s.size() != 4) fout("een AU-code heeft 4 tekens: " + s);
  return ((OSType)(unsigned char)s[0] << 24) | ((OSType)(unsigned char)s[1] << 16) | ((OSType)(unsigned char)s[2] << 8) |
         (OSType)(unsigned char)s[3];
}

size_t wavLengte(const char* pad) {
  FILE* f = std::fopen(pad, "rb");
  if (!f) fout(std::string("kan niet lezen: ") + pad);
  std::vector<unsigned char> b;
  unsigned char buf[65536];
  size_t r;
  while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + r);
  std::fclose(f);
  auto u16 = [&](size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) fout("geen WAV");
  size_t p = 12, dLen = 0;
  uint32_t kan = 0, bits = 0;
  while (p + 8 <= b.size()) {
    const uint32_t len = u32(p + 4);
    if (!std::memcmp(&b[p], "fmt ", 4)) { kan = u16(p + 10); bits = u16(p + 22); }
    if (!std::memcmp(&b[p], "data", 4)) dLen = len;
    p += 8 + len + (len & 1);
  }
  if (!kan || bits != 32) fout("alleen 32-bit float WAV");
  return dLen / (4 * kan);
}

void schrijfWav(const char* pad, const std::vector<std::vector<float>>& k, uint32_t sr) {
  FILE* f = std::fopen(pad, "wb");
  if (!f) fout(std::string("kan niet schrijven: ") + pad);
  const uint32_t kan = (uint32_t)k.size(), n = (uint32_t)k[0].size(), data = n * kan * 4;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); put32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(16); put16(3); put16((uint16_t)kan); put32(sr); put32(sr * kan * 4); put16((uint16_t)(kan * 4)); put16(32);
  std::fwrite("data", 1, 4, f); put32(data);
  for (uint32_t i = 0; i < n; ++i)
    for (uint32_t c = 0; c < kan; ++c) std::fwrite(&k[c][i], 4, 1, f);
  std::fclose(f);
}

struct Ev {
  uint64_t sample;
  int soort;  // 0 noot aan, 1 noot uit, 2 parameter
  uint32_t id;
  double waarde;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) fout("gebruik: pb-au-render <type:subtype:maker> <in.wav> <uit.wav> [opties]");
  const std::string codes = argv[1];
  if (codes.size() != 14 || codes[4] != ':' || codes[9] != ':') fout("codes als aumu:PTgg:Vrve");
  double sr = 48000;
  uint32_t kanalen = 2;
  std::vector<uint32_t> blokken = {512};
  std::vector<std::pair<uint32_t, double>> params;
  std::vector<Ev> ev;
  for (int i = 4; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--sr" && i + 1 < argc) sr = std::atof(argv[++i]);
    else if (a == "--kanalen" && i + 1 < argc) kanalen = (uint32_t)std::atoi(argv[++i]);
    else if (a == "--blok" && i + 1 < argc) {
      blokken.clear();
      const std::string s = argv[++i];
      size_t q = 0;
      while (q <= s.size()) {
        size_t k = s.find(',', q);
        if (k == std::string::npos) k = s.size();
        blokken.push_back((uint32_t)std::atoi(s.substr(q, k - q).c_str()));
        q = k + 1;
      }
    } else if (a == "--p" && i + 1 < argc) {
      const std::string s = argv[++i];
      const size_t k = s.find('=');
      if (k == std::string::npos) fout("--p id=waarde");
      params.push_back({(uint32_t)std::atoi(s.substr(0, k).c_str()), std::atof(s.substr(k + 1).c_str())});
    } else if (a == "--noten" && i + 1 < argc) {
      std::ifstream f(argv[++i]);
      if (!f) fout("--noten: bestand niet te lezen");
      uint64_t smp;
      std::string wat;
      int noot;
      while (f >> smp >> wat >> noot) ev.push_back({smp, wat == "aan" ? 0 : 1, (uint32_t)noot, 0});
    } else if (a == "--automatie" && i + 1 < argc) {
      std::ifstream f(argv[++i]);
      if (!f) fout("--automatie: bestand niet te lezen");
      uint64_t smp;
      uint32_t id;
      double w;
      while (f >> smp >> id >> w) ev.push_back({smp, 2, id, w});
    } else if (a == "--via" && i + 1 < argc) ++i;  // van pb-render; een AU kiest zijn indeling met de stream-indeling
    else fout("onbekende optie " + a);
  }
  for (uint32_t b : blokken)
    if (b < 1 || b > 4096) fout("blokgrootte 1..4096");
  if (kanalen != 1 && kanalen != 2) fout("--kanalen 1 of 2");
  // stabiel op sample (binnen één sample de volgorde van de bestanden)
  for (size_t a = 1; a < ev.size(); ++a)
    for (size_t b = a; b > 0 && ev[b - 1].sample > ev[b].sample; --b) std::swap(ev[b - 1], ev[b]);

  const size_t n = wavLengte(argv[2]);
  uint32_t maxBlok = 1;
  for (uint32_t b : blokken)
    if (b > maxBlok) maxBlok = b;

  AudioComponentDescription d{};
  d.componentType = fourcc(codes.substr(0, 4));
  d.componentSubType = fourcc(codes.substr(5, 4));
  d.componentManufacturer = fourcc(codes.substr(10, 4));
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) fout("AU " + codes + " niet gevonden (geïnstalleerd? killall -9 AudioComponentRegistrar; valkuil 5)");
  AudioUnit au = nullptr;
  ok(AudioComponentInstanceNew(comp, &au), "AudioComponentInstanceNew");

  UInt32 maxF = maxBlok;
  ok(AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF),
     "MaximumFramesPerSlice");
  AudioStreamBasicDescription f{};
  f.mSampleRate = sr;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
  f.mBytesPerPacket = 4;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = 4;
  f.mChannelsPerFrame = kanalen;
  f.mBitsPerChannel = 32;
  ok(AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f),
     "StreamFormat (output)");
  for (auto& [id, v] : params)
    ok(AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)v, 0), "SetParameter");
  ok(AudioUnitInitialize(au), "AudioUnitInitialize");

  AudioStreamBasicDescription g{};
  UInt32 gs = sizeof g;
  ok(AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &g, &gs), "StreamFormat terug");
  if (g.mChannelsPerFrame != kanalen || g.mSampleRate != sr)
    fout("de AU staat op " + std::to_string(g.mChannelsPerFrame) + " kanalen, " + std::to_string(g.mSampleRate) + " Hz");

  std::vector<std::vector<float>> uit(kanalen, std::vector<float>(n)), buf(kanalen, std::vector<float>(maxBlok));
  std::vector<unsigned char> ablOpslag(offsetof(AudioBufferList, mBuffers) + kanalen * sizeof(AudioBuffer));
  auto* abl = reinterpret_cast<AudioBufferList*>(ablOpslag.data());
  size_t pos = 0, bnr = 0, volgende = 0;
  while (pos < n) {
    uint32_t m = blokken[bnr++ % blokken.size()];
    if (pos + m > n) m = (uint32_t)(n - pos);
    while (volgende < ev.size() && ev[volgende].sample < pos + m) {
      const Ev& e = ev[volgende++];
      const uint32_t t = e.sample < pos ? 0 : (uint32_t)(e.sample - pos);
      if (e.soort == 2) {  // een parameter of bypass: alleen op een blokgrens
        if (t != 0) fout("--automatie: een event op sample " + std::to_string(e.sample) + " valt niet op een blokgrens");
        if (e.id == BYPASS_ID) {
          const UInt32 aan = e.waarde >= 0.5 ? 1 : 0;
          ok(AudioUnitSetProperty(au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &aan, sizeof aan),
             "BypassEffect");
        } else
          ok(AudioUnitSetParameter(au, e.id, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)e.waarde, 0),
             "SetParameter");
      } else {
        ok(MusicDeviceMIDIEvent(au, e.soort == 0 ? 0x90 : 0x80, e.id, e.soort == 0 ? 100 : 0, t), "MIDIEvent");
      }
    }
    abl->mNumberBuffers = kanalen;
    for (uint32_t c = 0; c < kanalen; ++c) {
      abl->mBuffers[c].mNumberChannels = 1;
      abl->mBuffers[c].mDataByteSize = m * 4;
      abl->mBuffers[c].mData = buf[c].data();
    }
    AudioTimeStamp ts{};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    ts.mSampleTime = (Float64)pos;
    AudioUnitRenderActionFlags vlag = 0;
    ok(AudioUnitRender(au, &vlag, &ts, 0, m, abl), "AudioUnitRender");
    for (uint32_t c = 0; c < kanalen; ++c) std::memcpy(&uit[c][pos], abl->mBuffers[c].mData, m * 4);
    pos += m;
  }
  AudioUnitUninitialize(au);
  AudioComponentInstanceDispose(au);
  schrijfWav(argv[3], uit, (uint32_t)sr);
  return 0;
}
