// Dead Space Prime's sound cues from the player's own WAV files (port_dead_space_sound.h).
#include "port_dead_space_sound.h"

#include "port_paths.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace PortDeadSpaceSound {
namespace {

constexpr int kCueCount = static_cast< int >(ECue::Count);
// How long a cue without a usable file waits before looking again.
constexpr uint64_t kRetryMs = 2000;

const char* const kFileNames[kCueCount] = {
    "stasis_fire.wav",
    "stasis_hit.wav",
};

struct Clip {
  bool looked = false;  // tried to load at least once
  bool reported = false; // said so in the log
  bool loaded = false;
  uint64_t retryAtMs = 0;
  SDL_AudioSpec spec{};
  Uint8* data = nullptr;
  Uint32 length = 0;
  SDL_AudioStream* stream = nullptr;
};

Clip sClips[kCueCount];
std::string sFolderOverride;
bool sOverridden = false;
bool sAudioInitialized = false;

Clip& ClipFor(ECue cue) { return sClips[static_cast< int >(cue)]; }

void Free(Clip& clip) {
  if (clip.stream != nullptr) {
    SDL_DestroyAudioStream(clip.stream);
  }
  if (clip.data != nullptr) {
    SDL_free(clip.data);
  }
  clip = Clip();
}

bool Load(ECue cue) {
  Clip& clip = ClipFor(cue);
  if (clip.loaded) {
    return true;
  }
  const uint64_t now = SDL_GetTicks();
  if (clip.looked && now < clip.retryAtMs) {
    return false;
  }
  clip.looked = true;
  clip.retryAtMs = now + kRetryMs;
  const std::string path = Folder() + FileName(cue);
  SDL_AudioSpec spec{};
  Uint8* data = nullptr;
  Uint32 length = 0;
  if (!SDL_LoadWAV(path.c_str(), &spec, &data, &length) || length == 0) {
    if (!clip.reported) {
      clip.reported = true;
      std::fprintf(stderr, "[dead space] %s not used: %s\n", path.c_str(),
                   data == nullptr ? SDL_GetError() : "no samples");
    }
    SDL_free(data);
    return false;
  }
  clip.spec = spec;
  clip.data = data;
  clip.length = length;
  clip.loaded = true;
  std::fprintf(stderr, "[dead space] %s: %d Hz, %d channel(s), %u bytes\n", path.c_str(),
               spec.freq, spec.channels, static_cast< unsigned >(length));
  return true;
}

} // namespace

const char* FileName(ECue cue) {
  const int i = static_cast< int >(cue);
  return i >= 0 && i < kCueCount ? kFileNames[i] : "";
}

std::string Folder() {
  if (sOverridden) {
    return sFolderOverride;
  }
  const std::string& user = PortPaths::UserFolder();
  return (user.empty() ? std::string("./") : user) + "dead_space/";
}

void SetFolder(const std::string& folder) {
  for (Clip& clip : sClips) {
    Free(clip);
  }
  sFolderOverride = folder;
  if (!sFolderOverride.empty() && sFolderOverride.back() != '/' && sFolderOverride.back() != '\\') {
    sFolderOverride += '/';
  }
  sOverridden = true;
}

bool Available(ECue cue) {
  const int i = static_cast< int >(cue);
  return i >= 0 && i < kCueCount && Load(cue);
}

bool Play(ECue cue, float gain) {
  if (!Available(cue)) {
    return false;
  }
  Clip& clip = ClipFor(cue);
  if (clip.stream == nullptr) {
    if (!sAudioInitialized) {
      sAudioInitialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
      if (!sAudioInitialized) {
        return false;
      }
    }
    // Its own logical device: SDL mixes it with the game's streams.
    clip.stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &clip.spec, nullptr, nullptr);
    if (clip.stream == nullptr) {
      return false;
    }
    SDL_ResumeAudioStreamDevice(clip.stream);
  }
  SDL_ClearAudioStream(clip.stream);
  SDL_SetAudioStreamGain(clip.stream, std::isfinite(gain) ? std::clamp(gain, 0.f, 4.f) : 1.f);
  return SDL_PutAudioStreamData(clip.stream, clip.data, static_cast< int >(clip.length));
}

void Shutdown() {
  for (Clip& clip : sClips) {
    Free(clip);
  }
  if (sAudioInitialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sAudioInitialized = false;
  }
}

} // namespace PortDeadSpaceSound
