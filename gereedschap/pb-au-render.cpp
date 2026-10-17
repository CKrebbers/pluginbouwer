// pb-au-render — de null-test door de geïnstalleerde AU (macOS, AudioToolbox), in elke kanaalindeling (fase 5).
// Een kleine AU-host zoals Logic: streamformaten op de bussen zetten (zo kiest een AU-host mono of stereo),
// parameters vóór Initialize, dan Render in blokken op een eigen (audio)thread, met de invoer via een render-callback.
//
//   pb-au-render <in.wav> <uit.wav> [--indeling stereo|mono|mono-stereo] [--blok 512 | --blok 3,128,1,4096]
//                [--p id=waarde ...] [--bypass] [type subtype maker]          (standaard aufx PDca Vrve)
//   pb-au-render --weiger <in> <uit>   Initialize met deze kanalen (bv. 2 1) moet kAudioUnitErr_FormatNotSupported geven
//
// --p zet een AU-parameter (id = het CLAP-id; de waarde in eenheden, zoals clap-wrapper ze doorgeeft).
// --bypass zet kAudioUnitProperty_BypassEffect na Initialize, zoals een host met zijn bypassknop.
// Mono leest kanaal 0 van de invoer. De uitvoer gaat als 32-bit float WAV naar <uit.wav>.
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void stop(const std::string& t) { std::fprintf(stderr, "pb-au-render: %s\n", t.c_str()); std::exit(2); }

OSType code(const char* c) {
  if (std::strlen(c) != 4) stop(std::string("geen vier tekens: ") + c);
  return ((OSType)(unsigned char)c[0] << 24) | ((OSType)(unsigned char)c[1] << 16) |
         ((OSType)(unsigned char)c[2] << 8) | (OSType)(unsigned char)c[3];
}

struct Wav { uint32_t sr = 48000; std::vector<std::vector<float>> k; };

Wav leesWav(const char* pad) {
  FILE* f = std::fopen(pad, "rb");
  if (!f) stop(std::string("kan niet lezen: ") + pad);
  std::vector<unsigned char> b;
  unsigned char buf[65536]; size_t r;
  while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + r);
  std::fclose(f);
  auto u16 = [&](size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) stop("geen WAV");
  size_t p = 12; uint32_t tag = 0, kan = 0, bits = 0, sr = 0; size_t dOff = 0, dLen = 0;
  while (p + 8 <= b.size()) {
    uint32_t len = u32(p + 4);
    if (!std::memcmp(&b[p], "fmt ", 4)) { tag = u16(p + 8); kan = u16(p + 10); sr = u32(p + 12); bits = u16(p + 22); }
    if (!std::memcmp(&b[p], "data", 4)) { dOff = p + 8; dLen = len; }
    p += 8 + len + (len & 1);
  }
  if (tag == 0xfffe) tag = 3;
  if (tag != 3 || bits != 32 || !kan || !dOff) stop("alleen 32-bit float WAV");
  Wav w; w.sr = sr; w.k.assign(kan, {});
  size_t n = dLen / (4 * kan);
  for (auto& c : w.k) c.resize(n);
  for (size_t i = 0; i < n; ++i)
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(&w.k[c][i], &b[dOff + (i * kan + c) * 4], 4);
  return w;
}

void schrijfWav(const char* pad, const Wav& w) {
  FILE* f = std::fopen(pad, "wb");
  if (!f) stop(std::string("kan niet schrijven: ") + pad);
  uint32_t kan = (uint32_t)w.k.size(), n = (uint32_t)w.k[0].size(), data = n * kan * 4;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); put32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(16); put16(3); put16((uint16_t)kan); put32(w.sr); put32(w.sr * kan * 4); put16((uint16_t)(kan * 4)); put16(32);
  std::fwrite("data", 1, 4, f); put32(data);
  for (uint32_t i = 0; i < n; ++i) for (uint32_t c = 0; c < kan; ++c) std::fwrite(&w.k[c][i], 4, 1, f);
  std::fclose(f);
}

AudioStreamBasicDescription formaat(double sr, uint32_t kanalen) {
  AudioStreamBasicDescription f{};
  f.mSampleRate = sr;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
  f.mBytesPerPacket = f.mBytesPerFrame = 4;
  f.mFramesPerPacket = 1;
  f.mChannelsPerFrame = kanalen;
  f.mBitsPerChannel = 32;
  return f;
}

AudioUnit maakAU(const AudioComponentDescription& d) {
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) stop("AU niet gevonden (geïnstalleerd, en AudioComponentRegistrar opnieuw gestart?)");
  AudioUnit au = nullptr;
  if (AudioComponentInstanceNew(comp, &au) != noErr || !au) stop("AudioComponentInstanceNew faalt");
  return au;
}

OSStatus zetFormaten(AudioUnit au, double sr, uint32_t kIn, uint32_t kUit) {
  AudioStreamBasicDescription fi = formaat(sr, kIn), fu = formaat(sr, kUit);
  OSStatus e = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fi, sizeof fi);
  if (e != noErr) return e;
  return AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fu, sizeof fu);
}

// de invoer voor de render-callback
struct Bron {
  const Wav* in = nullptr;
  uint32_t kanalen = 0;
  size_t pos = 0;  // eerste sample van het blok dat nu gerenderd wordt
};

OSStatus geefInvoer(void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n,
                    AudioBufferList* io) {
  auto* b = static_cast<Bron*>(ctx);
  for (UInt32 c = 0; c < io->mNumberBuffers; ++c) {
    if (!io->mBuffers[c].mData) return kAudio_ParamError;
    const auto& bron = b->in->k[c < b->kanalen ? c : b->kanalen - 1];
    std::memcpy(io->mBuffers[c].mData, &bron[b->pos], n * sizeof(float));
  }
  return noErr;
}

}  // namespace

int main(int argc, char** argv) {
  AudioComponentDescription d{};
  d.componentType = code("aufx");
  d.componentSubType = code("PDca");
  d.componentManufacturer = code("Vrve");

  if (argc >= 4 && !std::strcmp(argv[1], "--weiger")) {
    const uint32_t kIn = (uint32_t)std::atoi(argv[2]), kUit = (uint32_t)std::atoi(argv[3]);
    AudioUnit au = maakAU(d);
    OSStatus e = zetFormaten(au, 48000, kIn, kUit);
    if (e == noErr) e = AudioUnitInitialize(au);
    AudioComponentInstanceDispose(au);
    if (e == noErr) stop("Initialize met " + std::to_string(kIn) + " in, " + std::to_string(kUit) + " uit lukte; verwacht geweigerd");
    std::printf("ok %u in, %u uit: geweigerd (fout %d)\n", kIn, kUit, (int)e);
    return 0;
  }
  if (argc < 3) stop("gebruik: pb-au-render <in.wav> <uit.wav> [--indeling …] [--blok …] [--p id=waarde] [--bypass]");

  std::string indeling = "stereo";
  std::vector<uint32_t> blokken = {512};
  std::vector<std::pair<AudioUnitParameterID, double>> params;
  bool bypass = false;
  std::vector<const char*> codes;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--indeling" && i + 1 < argc) indeling = argv[++i];
    else if (a == "--blok" && i + 1 < argc) {
      blokken.clear(); std::string s = argv[++i]; size_t q = 0;
      while (q <= s.size()) { size_t k = s.find(',', q); if (k == std::string::npos) k = s.size(); blokken.push_back((uint32_t)std::atoi(s.substr(q, k - q).c_str())); q = k + 1; }
    } else if (a == "--p" && i + 1 < argc) {
      std::string s = argv[++i]; size_t k = s.find('=');
      if (k == std::string::npos) stop("--p id=waarde");
      params.push_back({(AudioUnitParameterID)std::atoi(s.substr(0, k).c_str()), std::atof(s.substr(k + 1).c_str())});
    } else if (a == "--bypass") bypass = true;
    else if (a.size() == 4 && a[0] != '-') codes.push_back(argv[i]);
    else stop("onbekende optie " + a);
  }
  if (codes.size() == 3) {
    d.componentType = code(codes[0]); d.componentSubType = code(codes[1]); d.componentManufacturer = code(codes[2]);
  } else if (!codes.empty()) stop("type subtype maker: alle drie");
  uint32_t kIn = 2, kUit = 2;
  if (indeling == "mono") kIn = kUit = 1;
  else if (indeling == "mono-stereo") { kIn = 1; kUit = 2; }
  else if (indeling != "stereo") stop("--indeling stereo|mono|mono-stereo");
  uint32_t maxBlok = 1;
  for (auto b : blokken) { if (b < 1) stop("blok ≥ 1"); if (b > maxBlok) maxBlok = b; }

  Wav in = leesWav(argv[1]);
  const double sr = in.sr;
  const size_t n = in.k[0].size();

  AudioUnit au = maakAU(d);
  if (OSStatus e = zetFormaten(au, sr, kIn, kUit); e != noErr) stop("StreamFormat " + indeling + ": fout " + std::to_string(e));
  UInt32 mf = maxBlok;
  if (AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, sizeof mf) != noErr)
    stop("MaximumFramesPerSlice faalt");
  Bron bron;
  bron.in = &in;
  bron.kanalen = (uint32_t)in.k.size();
  AURenderCallbackStruct cb = {geefInvoer, &bron};
  if (AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr)
    stop("SetRenderCallback faalt");
  for (auto& [id, v] : params)  // vóór Initialize, zoals een host die een project opent (valkuil 1)
    if (AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)v, 0) != noErr)
      stop("parameter " + std::to_string(id) + " zetten faalt");
  if (OSStatus e = AudioUnitInitialize(au); e != noErr) stop("AudioUnitInitialize (" + indeling + "): fout " + std::to_string(e));
  if (bypass) {
    UInt32 aan = 1;
    if (AudioUnitSetProperty(au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &aan, sizeof aan) != noErr)
      stop("BypassEffect zetten faalt");
    UInt32 terug = 0, tm = sizeof terug;
    if (AudioUnitGetProperty(au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &terug, &tm) != noErr || terug != 1)
      stop("BypassEffect leest niet terug als 1");
  }

  Wav uit; uit.sr = in.sr; uit.k.assign(kUit, std::vector<float>(n));
  std::string renderFout;
  std::thread audio([&] {  // Render op een eigen thread, zoals de audiothread van een host
    std::vector<unsigned char> ablGeheugen(offsetof(AudioBufferList, mBuffers) + kUit * sizeof(AudioBuffer));
    auto* abl = reinterpret_cast<AudioBufferList*>(ablGeheugen.data());
    AudioTimeStamp ts{};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    size_t bnr = 0;
    while (bron.pos < n) {
      uint32_t m = blokken[bnr++ % blokken.size()];
      if (bron.pos + m > n) m = (uint32_t)(n - bron.pos);
      abl->mNumberBuffers = kUit;
      for (uint32_t c = 0; c < kUit; ++c) {
        abl->mBuffers[c].mNumberChannels = 1;
        abl->mBuffers[c].mDataByteSize = m * sizeof(float);
        abl->mBuffers[c].mData = &uit.k[c][bron.pos];
      }
      AudioUnitRenderActionFlags vlag = 0;
      ts.mSampleTime = (Float64)bron.pos;
      OSStatus e = AudioUnitRender(au, &vlag, &ts, 0, m, abl);
      if (e != noErr) { renderFout = "AudioUnitRender: fout " + std::to_string(e); return; }
      for (uint32_t c = 0; c < kUit; ++c)  // de AU mag zijn eigen buffers teruggeven
        if (abl->mBuffers[c].mData != &uit.k[c][bron.pos]) std::memcpy(&uit.k[c][bron.pos], abl->mBuffers[c].mData, m * sizeof(float));
      bron.pos += m;
    }
  });
  audio.join();
  if (!renderFout.empty()) stop(renderFout);
  AudioUnitUninitialize(au);
  AudioComponentInstanceDispose(au);
  schrijfWav(argv[2], uit);
  return 0;
}
