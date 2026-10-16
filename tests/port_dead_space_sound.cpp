// PortDeadSpaceSound on SDL's dummy audio driver: cues load only from a real WAV
// file in the folder, play once loaded, and stay silent (Play says false) without.
#include "port_dead_space_sound.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
int sLine = 0;
void Check(bool condition) {
  if (!condition) {
    std::fprintf(stderr, "dead space sound regression failed at line %d\n", sLine);
    std::abort();
  }
}
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    sLine = __LINE__;                                                                              \
    Check(cond);                                                                                   \
  } while (0)

void Put16(std::ofstream& out, uint16_t v) {
  const char b[2] = {static_cast< char >(v & 0xFF), static_cast< char >(v >> 8)};
  out.write(b, 2);
}
void Put32(std::ofstream& out, uint32_t v) {
  Put16(out, static_cast< uint16_t >(v & 0xFFFF));
  Put16(out, static_cast< uint16_t >(v >> 16));
}

// A 0.1 s, 440 Hz, 16-bit mono PCM WAV at 22050 Hz.
void WriteWav(const std::filesystem::path& path) {
  const uint32_t rate = 22050;
  std::vector< int16_t > samples(rate / 10);
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast< int16_t >(8000.0 * std::sin(6.283185307 * 440.0 * i / rate));
  }
  const uint32_t dataBytes = static_cast< uint32_t >(samples.size() * 2);
  std::ofstream out(path, std::ios::binary);
  out.write("RIFF", 4);
  Put32(out, 36 + dataBytes);
  out.write("WAVEfmt ", 8);
  Put32(out, 16);       // fmt chunk size
  Put16(out, 1);        // PCM
  Put16(out, 1);        // mono
  Put32(out, rate);     // sample rate
  Put32(out, rate * 2); // byte rate
  Put16(out, 2);        // block align
  Put16(out, 16);       // bits per sample
  out.write("data", 4);
  Put32(out, dataBytes);
  for (int16_t s : samples) Put16(out, static_cast< uint16_t >(s));
}
} // namespace

int main() {
  using PortDeadSpaceSound::ECue;
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIO_DRIVER", "dummy", true);

  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() /
                       ("port_dead_space_sound_" + std::to_string(SDL_GetTicksNS()));
  std::error_code ec;
  fs::remove_all(dir, ec); // a leftover from a run that stopped short
  fs::create_directories(dir);

  CHECK(std::string(PortDeadSpaceSound::FileName(ECue::StasisFire)) == "stasis_fire.wav");
  CHECK(std::string(PortDeadSpaceSound::FileName(ECue::StasisHit)) == "stasis_hit.wav");

  // An empty folder: nothing to play, and Play says so.
  PortDeadSpaceSound::SetFolder(dir.string());
  CHECK(PortDeadSpaceSound::Folder().back() == '/');
  CHECK(!PortDeadSpaceSound::Available(ECue::StasisFire));
  CHECK(!PortDeadSpaceSound::Play(ECue::StasisFire, 1.f));

  // A real WAV for the fire cue and a broken one for the hit cue. Pointing at
  // the folder again forgets the misses, as a later retry would.
  WriteWav(dir / "stasis_fire.wav");
  {
    std::ofstream junk(dir / "stasis_hit.wav", std::ios::binary);
    junk << "not a wave file";
  }
  PortDeadSpaceSound::SetFolder(dir.string());
  CHECK(PortDeadSpaceSound::Available(ECue::StasisFire));
  CHECK(!PortDeadSpaceSound::Available(ECue::StasisHit));
  CHECK(PortDeadSpaceSound::Play(ECue::StasisFire, 0.5f));
  CHECK(PortDeadSpaceSound::Play(ECue::StasisFire, 0.5f)); // again, from the start
  CHECK(PortDeadSpaceSound::Play(ECue::StasisFire, std::nanf("")));
  CHECK(!PortDeadSpaceSound::Play(ECue::StasisHit, 1.f));
  CHECK(!PortDeadSpaceSound::Available(ECue::Count));

  PortDeadSpaceSound::Shutdown();
  fs::remove_all(dir, ec);
  std::puts("dead space sound: all checks passed");
  return 0;
}
