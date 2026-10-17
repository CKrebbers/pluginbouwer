// au-render (macOS) — pb-render voor de geïnstalleerde AU: stuurt een WAV door aufx PDca Vrve via AudioToolbox, in
// de gevraagde kanaalindeling, samplerate en blokgrootte, en schrijft de uitvoer als WAV. De null-matrix
// (gereedschap/null-matrix.mjs au) vergelijkt dat met de WASM. Zo is de AU getoetst in mono, mono -> stereo en stereo
// (pluginval test kent alleen de standaardindeling) en met de bypass van de host (kAudioUnitProperty_BypassEffect).
//
//   au-render <in.wav> <uit.wav> [--sr 48000] [--kanalen stereo|mono|mono-stereo] [--blok 512 | --blok 3,128,1,4096]
//             [--p id=waarde ...] [--bypass]
//
// Parameters en bypass worden vóór AudioUnitInitialize gezet (zoals Logic een project opent; valkuil 1).
#include <AudioToolbox/AudioToolbox.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fout(const std::string& t) { std::fprintf(stderr, "au-render: %s\n", t.c_str()); std::exit(2); }
void controle(OSStatus st, const char* wat) {
  if (st != noErr) fout(std::string(wat) + ": OSStatus " + std::to_string((int)st));
}

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

// de invoer per AU-invoerkanaal, en waar de render nu is
struct Invoer {
  const std::vector<std::vector<float>>* k = nullptr;
  uint32_t kanalen = 2;
  size_t pos = 0;
  std::vector<std::vector<float>> reserve;  // als de AU geen eigen buffer meegeeft
};

OSStatus geefInvoer(void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io) {
  auto* in = static_cast<Invoer*>(ctx);
  for (UInt32 b = 0; b < io->mNumberBuffers; ++b) {
    const auto& bron = (*in->k)[b < in->k->size() ? b : 0];
    if (!io->mBuffers[b].mData) io->mBuffers[b].mData = in->reserve[b].data();
    auto* d = static_cast<float*>(io->mBuffers[b].mData);
    for (UInt32 i = 0; i < n; ++i) d[i] = in->pos + i < bron.size() ? bron[in->pos + i] : 0.0f;
    io->mBuffers[b].mDataByteSize = n * sizeof(float);
  }
  return noErr;
}

AudioStreamBasicDescription formaat(double sr, uint32_t kanalen) {
  AudioStreamBasicDescription f{};
  f.mSampleRate = sr;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
  f.mBytesPerPacket = 4;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = 4;
  f.mChannelsPerFrame = kanalen;
  f.mBitsPerChannel = 32;
  return f;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) fout("gebruik: au-render <in.wav> <uit.wav> [--sr N] [--kanalen stereo|mono|mono-stereo] [--blok N[,N…]] [--p id=waarde] [--bypass]");
  double sr = 0; std::vector<uint32_t> blokken = {512}; bool bypass = false;
  std::string indeling = "stereo";
  std::vector<std::pair<AudioUnitParameterID, double>> params;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--sr" && i + 1 < argc) sr = std::atof(argv[++i]);
    else if (a == "--kanalen" && i + 1 < argc) indeling = argv[++i];
    else if (a == "--blok" && i + 1 < argc) {
      blokken.clear(); std::string s = argv[++i]; size_t q = 0;
      while (q <= s.size()) { size_t k = s.find(',', q); if (k == std::string::npos) k = s.size(); blokken.push_back((uint32_t)std::atoi(s.substr(q, k - q).c_str())); q = k + 1; }
    } else if (a == "--p" && i + 1 < argc) {
      std::string s = argv[++i]; size_t k = s.find('=');
      if (k == std::string::npos) fout("--p id=waarde");
      params.push_back({(AudioUnitParameterID)std::atoi(s.substr(0, k).c_str()), std::atof(s.substr(k + 1).c_str())});
    } else if (a == "--bypass") bypass = true;
    else fout("onbekende optie " + a);
  }
  uint32_t kanIn = 2, kanUit = 2;
  if (indeling == "mono") kanIn = kanUit = 1;
  else if (indeling == "mono-stereo") kanIn = 1;
  else if (indeling != "stereo") fout("onbekende indeling " + indeling);

  Wav in = leesWav(argv[1]);
  if (sr <= 0) sr = in.sr;
  uint32_t maxBlok = 1; for (auto b : blokken) if (b > maxBlok) maxBlok = b;

  AudioComponentDescription desc = {kAudioUnitType_Effect, 'PDca', 'Vrve', 0, 0};
  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (!comp) fout("AU aufx PDca Vrve niet gevonden (geïnstalleerd? killall -9 AudioComponentRegistrar)");
  AudioUnit au = nullptr;
  controle(AudioComponentInstanceNew(comp, &au), "AudioComponentInstanceNew");

  // de kanaalindelingen die de AU aanbiedt (moeten mono, mono -> stereo en stereo bevatten)
  {
    UInt32 g = 0;
    Boolean schrijf = false;
    if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, &g, &schrijf) == noErr && g) {
      std::vector<AUChannelInfo> ci(g / sizeof(AUChannelInfo));
      AudioUnitGetProperty(au, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, ci.data(), &g);
      bool gevonden = false;
      for (auto& c : ci) gevonden = gevonden || (c.inChannels == (SInt16)kanIn && c.outChannels == (SInt16)kanUit);
      if (!gevonden) fout("SupportedNumChannels heeft geen " + std::to_string(kanIn) + " -> " + std::to_string(kanUit));
    } else {
      fout("geen SupportedNumChannels");
    }
  }
  const AudioStreamBasicDescription fi = formaat(sr, kanIn), fu = formaat(sr, kanUit);
  controle(AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fi, sizeof fi), "StreamFormat (invoer)");
  controle(AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fu, sizeof fu), "StreamFormat (uitgang)");
  UInt32 mf = maxBlok;
  controle(AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, sizeof mf), "MaximumFramesPerSlice");
  Invoer bron;
  bron.k = &in.k;
  bron.kanalen = kanIn;
  bron.reserve.assign(kanIn, std::vector<float>(maxBlok));
  AURenderCallbackStruct cb = {geefInvoer, &bron};
  controle(AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb), "SetRenderCallback");
  for (auto& [id, v] : params) controle(AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)v, 0), "SetParameter");
  if (bypass) {
    UInt32 b = 1;
    controle(AudioUnitSetProperty(au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &b, sizeof b), "BypassEffect");
  }
  controle(AudioUnitInitialize(au), "AudioUnitInitialize");

  const size_t n = in.k[0].size();
  Wav uit; uit.sr = (uint32_t)sr; uit.k.assign(kanUit, std::vector<float>(n));
  std::vector<std::vector<float>> bu(kanUit, std::vector<float>(maxBlok));
  std::vector<unsigned char> ablGeheugen(offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * kanUit);
  auto* abl = reinterpret_cast<AudioBufferList*>(ablGeheugen.data());
  size_t bnr = 0;
  while (bron.pos < n) {
    uint32_t m = blokken[bnr++ % blokken.size()];
    if (bron.pos + m > n) m = (uint32_t)(n - bron.pos);
    abl->mNumberBuffers = kanUit;
    for (uint32_t c = 0; c < kanUit; ++c) {
      abl->mBuffers[c].mNumberChannels = 1;
      abl->mBuffers[c].mDataByteSize = m * sizeof(float);
      abl->mBuffers[c].mData = bu[c].data();
    }
    AudioTimeStamp ts{};
    ts.mFlags = kAudioTimeStampSampleTimeValid;
    ts.mSampleTime = (Float64)bron.pos;
    AudioUnitRenderActionFlags vl = 0;
    controle(AudioUnitRender(au, &vl, &ts, 0, m, abl), "AudioUnitRender");
    for (uint32_t c = 0; c < kanUit; ++c) std::memcpy(&uit.k[c][bron.pos], abl->mBuffers[c].mData, m * sizeof(float));
    bron.pos += m;
  }
  AudioUnitUninitialize(au);
  AudioComponentInstanceDispose(au);
  schrijfWav(argv[2], uit);
  return 0;
}
