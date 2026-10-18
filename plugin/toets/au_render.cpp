// au_render — de AU-kant van de matrix van fase 5 (macOS): rendert elke regel van <map>/au-lijst.txt (uit
// gereedschap/matrix.mjs maak) door de geïnstalleerde .component, zoals een host dat doet (AudioToolbox), en schrijft
// <uitmap>/<naam>.wav. `node gereedschap/matrix.mjs vergelijk <map> <uitmap> AU` legt ze naast graaf.wasm.
//
//   pb-aurender <map>/au-lijst.txt <uitmap>
//
// Per regel: een verse instantie; de stroomformaten van de indeling op ingang en uitgang (element 0; 2 → 2, 1 → 1 of
// 1 → 2: valkuil 17, patches/clap-wrapper-auv2-volgt-presets-mono-stereo.patch); de parameters vóór
// AudioUnitInitialize (valkuil 1); dan AudioUnitRender in wisselende blokken (1, 4096, 77, 3). Met events (de bypass)
// in blokken van 64, en vóór het blok dat op de sample van een event begint kAudioUnitProperty_BypassEffect: zo zet
// een AU-host als Logic de bypass (clap-wrapper maakt er de CLAP-parameter met CLAP_PARAM_IS_BYPASS van).
#include <AudioToolbox/AudioToolbox.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fout(const std::string& t) { std::fprintf(stderr, "pb-aurender: %s\n", t.c_str()); std::exit(2); }

std::vector<std::vector<float>> leesWav(const std::string& pad) {
  std::ifstream f(pad, std::ios::binary);
  if (!f) fout("kan niet lezen: " + pad);
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u16 = [&](size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); };
  auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) fout("geen WAV: " + pad);
  size_t p = 12, dOff = 0, dLen = 0;
  uint32_t kan = 0, bits = 0, tag = 0;
  while (p + 8 <= b.size()) {
    const uint32_t len = u32(p + 4);
    if (!std::memcmp(&b[p], "fmt ", 4)) { tag = u16(p + 8); kan = u16(p + 10); bits = u16(p + 22); }
    if (!std::memcmp(&b[p], "data", 4)) { dOff = p + 8; dLen = len; }
    p += 8 + len + (len & 1);
  }
  if (tag != 3 || bits != 32 || !kan || !dOff) fout("alleen 32-bit float WAV: " + pad);
  const size_t n = dLen / (4 * kan);
  std::vector<std::vector<float>> k(kan, std::vector<float>(n));
  for (size_t i = 0; i < n; ++i)
    for (uint32_t c = 0; c < kan; ++c) std::memcpy(&k[c][i], &b[dOff + (i * kan + c) * 4], 4);
  return k;
}
void schrijfWav(const std::string& pad, const std::vector<std::vector<float>>& k, uint32_t sr) {
  FILE* f = std::fopen(pad.c_str(), "wb");
  if (!f) fout("kan niet schrijven: " + pad);
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

struct Bron {
  const std::vector<std::vector<float>>* in = nullptr;
  size_t pos = 0;
};
OSStatus geefInvoer(void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io) {
  auto* b = static_cast<Bron*>(ctx);
  for (UInt32 c = 0; c < io->mNumberBuffers; ++c) {
    const auto& k = (*b->in)[c < b->in->size() ? c : b->in->size() - 1];
    // geen buffer van de AU: dan mag de bron zijn eigen geheugen geven (de AU leest er alleen uit)
    if (!io->mBuffers[c].mData) io->mBuffers[c].mData = const_cast<float*>(&k[b->pos]);
    else std::memcpy(io->mBuffers[c].mData, &k[b->pos], n * sizeof(float));
    io->mBuffers[c].mDataByteSize = n * sizeof(float);
  }
  return noErr;
}

AudioStreamBasicDescription formaat(double sr, UInt32 kanalen) {
  AudioStreamBasicDescription f = {};
  f.mSampleRate = sr;
  f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
  f.mBytesPerPacket = 4;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = 4;
  f.mChannelsPerFrame = kanalen;
  f.mBitsPerChannel = 32;
  return f;
}

void check(OSStatus s, const std::string& wat) {
  if (s != noErr) fout(wat + " gaf status " + std::to_string((int)s));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) fout("gebruik: pb-aurender <au-lijst.txt> <uitmap>");
  AudioComponentDescription d = {};
  auto code = [](const char* t) { return (OSType)((t[0] << 24) | (t[1] << 16) | (t[2] << 8) | t[3]); };
  d.componentType = code("aufx");
  d.componentSubType = code("PDca");
  d.componentManufacturer = code("Vrve");
  AudioComponent comp = AudioComponentFindNext(nullptr, &d);
  if (!comp) fout("AU aufx PDca Vrve niet gevonden (geïnstalleerd? AudioComponentRegistrar?)");

  std::ifstream lijst(argv[1]);
  if (!lijst) fout(std::string("kan niet lezen: ") + argv[1]);
  std::string regel;
  int renders = 0;
  while (std::getline(lijst, regel)) {
    if (regel.empty()) continue;
    std::istringstream r(regel);
    std::string naam, invoer, events;
    double sr = 0;
    UInt32 kanIn = 0, kanUit = 0;
    r >> naam >> sr >> kanIn >> kanUit >> invoer >> events;
    std::vector<std::pair<AudioUnitParameterID, double>> params;
    for (std::string p; r >> p;) {
      const size_t k = p.find('=');
      if (k == std::string::npos) fout("parameter zonder '=': " + p);
      params.push_back({(AudioUnitParameterID)std::atoi(p.substr(0, k).c_str()), std::atof(p.substr(k + 1).c_str())});
    }
    std::vector<std::pair<size_t, UInt32>> bypass;  // sample, aan
    if (events != "-") {
      std::ifstream ef(events);
      if (!ef) fout("kan niet lezen: " + events);
      size_t s; int id; double v;
      while (ef >> s >> id >> v) bypass.push_back({s, v >= 0.5 ? 1u : 0u});
    }
    const auto in = leesWav(invoer);
    const size_t n = in[0].size();

    AudioUnit au = nullptr;
    check(AudioComponentInstanceNew(comp, &au), naam + ": AudioComponentInstanceNew");
    const UInt32 maxBlok = 4096;
    check(AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxBlok, sizeof maxBlok),
          naam + ": MaximumFramesPerSlice");
    const AudioStreamBasicDescription fi = formaat(sr, kanIn), fu = formaat(sr, kanUit);
    check(AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fi, sizeof fi),
          naam + ": StreamFormat ingang (" + std::to_string(kanIn) + " kanalen)");
    check(AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fu, sizeof fu),
          naam + ": StreamFormat uitgang (" + std::to_string(kanUit) + " kanalen)");
    for (const auto& [id, v] : params)
      check(AudioUnitSetParameter(au, id, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)v, 0),
            naam + ": parameter " + std::to_string(id));
    Bron bron;
    bron.in = &in;
    AURenderCallbackStruct cb = {geefInvoer, &bron};
    check(AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb),
          naam + ": SetRenderCallback");
    check(AudioUnitInitialize(au), naam + ": AudioUnitInitialize");

    std::vector<std::vector<float>> uit(kanUit, std::vector<float>(n));
    std::vector<unsigned char> ablGeheugen(sizeof(AudioBufferList) + kanUit * sizeof(AudioBuffer));
    auto* abl = reinterpret_cast<AudioBufferList*>(ablGeheugen.data());
    const UInt32 wissel[] = {1, 4096, 77, 3};
    size_t pos = 0, bnr = 0, e = 0;
    while (pos < n) {
      UInt32 m = bypass.empty() ? wissel[bnr++ % 4] : 64;
      if (pos + m > n) m = (UInt32)(n - pos);
      while (e < bypass.size() && bypass[e].first <= pos) {
        if (bypass[e].first != pos) fout(naam + ": event niet op een blokgrens");
        check(AudioUnitSetProperty(au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0, &bypass[e].second,
                                   sizeof(UInt32)),
              naam + ": BypassEffect");
        ++e;
      }
      abl->mNumberBuffers = kanUit;
      for (UInt32 c = 0; c < kanUit; ++c) {
        abl->mBuffers[c].mNumberChannels = 1;
        abl->mBuffers[c].mDataByteSize = m * sizeof(float);
        abl->mBuffers[c].mData = &uit[c][pos];
      }
      AudioUnitRenderActionFlags vlag = 0;
      AudioTimeStamp ts = {};
      ts.mSampleTime = (Float64)pos;
      ts.mFlags = kAudioTimeStampSampleTimeValid;
      bron.pos = pos;
      check(AudioUnitRender(au, &vlag, &ts, 0, m, abl), naam + ": AudioUnitRender");
      // een AU mag eigen buffers teruggeven: dan daaruit kopiëren
      for (UInt32 c = 0; c < kanUit; ++c)
        if (abl->mBuffers[c].mData != &uit[c][pos]) std::memcpy(&uit[c][pos], abl->mBuffers[c].mData, m * sizeof(float));
      pos += m;
    }
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
    schrijfWav(std::string(argv[2]) + "/" + naam + ".wav", uit, (uint32_t)sr);
    std::printf("pb-aurender: %s (%u → %u kanalen, %zu samples)\n", naam.c_str(), (unsigned)kanIn, (unsigned)kanUit, n);
    ++renders;
  }
  std::printf("pb-aurender: %d renders\n", renders);
  return 0;
}
