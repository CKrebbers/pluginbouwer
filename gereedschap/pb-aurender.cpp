// pb-aurender — rendert de geïnstalleerde AU (aumu PTgg Vrve) in stereo of mono, op elke samplerate (PROMPTS.md fase 5;
// alleen macOS). Een eigen AU-host zonder host-callbacks (geen tempo, valkuil 23) en zonder audio-ingang (valkuil 20).
//
//   pb-aurender <uit.wav> --sr <Hz> --kanalen 1|2 --samples <n> [--blok 512 | --blok 3,128,1,4096,77]
//               [--noten f]         per regel "sample aan|uit noot": als MIDI (0x90/0x80) met hun offset in het blok
//               [--p id=waarde ...] parameters vóór Initialize (AudioUnitSetParameter, in hun eigen eenheid)
//               [--bypass aan uit]  de bypass-parameter (id 5) aan op sample "aan" en uit op "uit"; het blok wordt
//                                   daar gesplitst, de parameter gaat op offset 0 van het nieuwe blok
//
// Valkuil 17: de AU van clap-wrapper v0.16.0 kent zonder patch alleen de vaste poort; met
// patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch worden de indelingen van audio-ports-config de
// AU-kanaalindelingen. Dit programma eist dat SupportedNumChannels 0 → 1 en 0 → 2 meldt, zet het uitgangsformaat op het
// gevraagde aantal kanalen (vóór Initialize, zoals een host), en eist dat Initialize het aanneemt.
// De uitvoer gaat naar <uit.wav> (32-bit float); gereedschap/vergelijk.mjs --paar legt hem naast de WASM.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-aurender: %s\n", t.c_str()); std::exit(2); }

void schrijfWav(const char* pad, const std::vector<std::vector<float>>& k, uint32_t sr) {
  FILE* f = std::fopen(pad, "wb");
  if (!f) stop(std::string("kan niet schrijven: ") + pad);
  const uint32_t kan = (uint32_t)k.size(), n = (uint32_t)k[0].size(), data = n * kan * 4;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); put32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(16); put16(3); put16((uint16_t)kan); put32(sr); put32(sr * kan * 4); put16((uint16_t)(kan * 4)); put16(32);
  std::fwrite("data", 1, 4, f); put32(data);
  for (uint32_t i = 0; i < n; ++i) for (uint32_t c = 0; c < kan; ++c) std::fwrite(&k[c][i], 4, 1, f);
  std::fclose(f);
}

struct Ev { uint64_t sample; int soort; int noot; };  // soort: 1 aan, 0 uit (MIDI), 2 bypass aan, 3 bypass uit

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) stop("gebruik: pb-aurender <uit.wav> --sr N --kanalen 1|2 --samples N [--blok …] [--noten f] [--p id=w] [--bypass aan uit]");
  double sr = 0; uint32_t kanalen = 0; uint64_t samples = 0;
  std::vector<uint32_t> blokken = {512};
  std::vector<Ev> ev;
  std::vector<std::pair<AudioUnitParameterID, float>> params;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--sr" && i + 1 < argc) sr = std::atof(argv[++i]);
    else if (a == "--kanalen" && i + 1 < argc) kanalen = (uint32_t)std::atoi(argv[++i]);
    else if (a == "--samples" && i + 1 < argc) samples = std::strtoull(argv[++i], nullptr, 10);
    else if (a == "--blok" && i + 1 < argc) {
      blokken.clear();
      std::string s = argv[++i];
      size_t q = 0;
      while (q <= s.size()) {
        size_t k = s.find(',', q);
        if (k == std::string::npos) k = s.size();
        blokken.push_back((uint32_t)std::atoi(s.substr(q, k - q).c_str()));
        q = k + 1;
      }
    } else if (a == "--noten" && i + 1 < argc) {
      std::ifstream f(argv[++i]);
      if (!f) stop("--noten: bestand niet te lezen");
      uint64_t s; std::string w; int n;
      while (f >> s >> w >> n) ev.push_back({s, w == "aan" ? 1 : 0, n});
    } else if (a == "--p" && i + 1 < argc) {
      const std::string s = argv[++i];
      const size_t k = s.find('=');
      if (k == std::string::npos) stop("--p id=waarde");
      params.push_back({(AudioUnitParameterID)std::atoi(s.substr(0, k).c_str()), (float)std::atof(s.substr(k + 1).c_str())});
    } else if (a == "--bypass" && i + 2 < argc) {
      ev.push_back({std::strtoull(argv[i + 1], nullptr, 10), 2, 0});
      ev.push_back({std::strtoull(argv[i + 2], nullptr, 10), 3, 0});
      i += 2;
    } else stop("onbekende optie " + a);
  }
  if (sr <= 0 || (kanalen != 1 && kanalen != 2) || samples == 0) stop("--sr, --kanalen 1|2 en --samples zijn nodig");
  for (auto b : blokken) if (b == 0 || b > 4096) stop("blokgrootte 1..4096");
  for (size_t a = 1; a < ev.size(); ++a)  // stabiel op sample
    for (size_t b = a; b > 0 && ev[b - 1].sample > ev[b].sample; --b) std::swap(ev[b - 1], ev[b]);

  AudioComponentDescription d = {kAudioUnitType_MusicDevice, 'PTgg', 'Vrve', 0, 0};
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) stop("AU aumu PTgg Vrve niet gevonden (geïnstalleerd? killall -9 AudioComponentRegistrar?)");
  AudioUnit au = nullptr;
  if (AudioComponentInstanceNew(comp, &au) != noErr || !au) stop("AU: geen instantie");

  // de kanaalindelingen die de AU aanbiedt (valkuil 17): 0 → 1 en 0 → 2
  {
    UInt32 sz = 0; Boolean w = false;
    if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, &sz, &w) != noErr || !sz)
      stop("AU: geen kAudioUnitProperty_SupportedNumChannels");
    std::vector<AUChannelInfo> ci(sz / sizeof(AUChannelInfo));
    AudioUnitGetProperty(au, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, ci.data(), &sz);
    bool mono = false, stereo = false;
    std::printf("AU SupportedNumChannels:");
    for (auto& c : ci) {
      std::printf(" %d→%d", c.inChannels, c.outChannels);
      mono = mono || (c.inChannels == 0 && c.outChannels == 1);
      stereo = stereo || (c.inChannels == 0 && c.outChannels == 2);
    }
    std::printf("\n");
    if (!mono || !stereo) stop("AU: SupportedNumChannels mist 0→1 of 0→2 (valkuil 17)");
  }

  uint32_t maxBlok = 1;
  for (auto b : blokken) if (b > maxBlok) maxBlok = b;
  UInt32 maxF = maxBlok;
  if (AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF) != noErr)
    stop("AU: MaximumFramesPerSlice");
  AudioStreamBasicDescription fmt;
  std::memset(&fmt, 0, sizeof fmt);
  fmt.mSampleRate = sr;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
  fmt.mBytesPerPacket = 4; fmt.mFramesPerPacket = 1; fmt.mBytesPerFrame = 4; fmt.mChannelsPerFrame = kanalen; fmt.mBitsPerChannel = 32;
  OSStatus st = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof fmt);
  if (st != noErr) stop("AU: uitgangsformaat " + std::to_string(kanalen) + " kanalen, " + std::to_string((int)sr) + " Hz geweigerd (" + std::to_string(st) + ")");
  for (auto& [id, v] : params)
    if (AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, v, 0) != noErr) stop("AU: SetParameter " + std::to_string(id));
  st = AudioUnitInitialize(au);
  if (st != noErr) stop("AU: Initialize met " + std::to_string(kanalen) + " kanalen geweigerd (" + std::to_string(st) + ")");
  {
    AudioStreamBasicDescription f2;
    UInt32 sz = sizeof f2;
    AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f2, &sz);
    if (f2.mChannelsPerFrame != kanalen || f2.mSampleRate != sr) stop("AU: het uitgangsformaat veranderde bij Initialize");
    std::printf("AU geïnitialiseerd: %u kanalen, %.0f Hz, blok %s\n", (unsigned)f2.mChannelsPerFrame, f2.mSampleRate,
                blokken.size() == 1 ? std::to_string(blokken[0]).c_str() : "wisselend");
  }

  std::vector<std::vector<float>> uit(kanalen, std::vector<float>(samples));
  std::vector<float> buf(maxBlok * kanalen);
  std::vector<unsigned char> ablBuf(sizeof(AudioBufferList) + kanalen * sizeof(AudioBuffer));
  auto* abl = reinterpret_cast<AudioBufferList*>(ablBuf.data());
  size_t volgende = 0, bnr = 0;
  uint64_t pos = 0;
  while (pos < samples) {
    uint64_t m = blokken[bnr++ % blokken.size()];
    if (pos + m > samples) m = samples - pos;
    // een bypass-event midden in het blok: het blok daar splitsen (de parameter gaat op offset 0)
    for (size_t e = volgende; e < ev.size() && ev[e].sample < pos + m; ++e)
      if (ev[e].soort >= 2 && ev[e].sample > pos) { m = ev[e].sample - pos; break; }
    while (volgende < ev.size() && ev[volgende].sample < pos + m) {
      const Ev& x = ev[volgende++];
      const UInt32 off = (UInt32)(x.sample < pos ? 0 : x.sample - pos);
      if (x.soort >= 2) {
        if (AudioUnitSetParameter(au, 5, kAudioUnitScope_Global, 0, x.soort == 2 ? 1.0f : 0.0f, 0) != noErr) stop("AU: bypass zetten");
      } else if (MusicDeviceMIDIEvent(au, x.soort ? 0x90 : 0x80, (UInt32)x.noot, x.soort ? 100 : 0, off) != noErr) {
        stop("AU: MusicDeviceMIDIEvent");
      }
    }
    abl->mNumberBuffers = kanalen;
    for (uint32_t c = 0; c < kanalen; ++c) {
      abl->mBuffers[c].mNumberChannels = 1;
      abl->mBuffers[c].mDataByteSize = (UInt32)(m * sizeof(float));
      abl->mBuffers[c].mData = buf.data() + c * maxBlok;
    }
    AudioUnitRenderActionFlags vl = 0;
    AudioTimeStamp ts;
    std::memset(&ts, 0, sizeof ts);
    ts.mSampleTime = (Float64)pos;
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    st = AudioUnitRender(au, &vl, &ts, 0, (UInt32)m, abl);
    if (st != noErr) stop("AU: Render gaf " + std::to_string(st));
    for (uint32_t c = 0; c < kanalen; ++c)
      std::memcpy(&uit[c][pos], abl->mBuffers[c].mData, (size_t)m * sizeof(float));
    pos += m;
  }
  AudioUnitUninitialize(au);
  AudioComponentInstanceDispose(au);
  schrijfWav(argv[1], uit, (uint32_t)sr);
  std::printf("geschreven: %s (%llu samples, %u kanalen)\n", argv[1], (unsigned long long)samples, (unsigned)kanalen);
  return 0;
}
