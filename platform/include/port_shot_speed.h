#pragma once

// Dead Space Prime's faster Power Beam (PortDebug::PowerBeamSpeed): a shot
// leaves `scale` times as fast and reaches as far as before. Plain arithmetic,
// so tests/port_shot_speed.cpp checks it alone.

#include <cmath>

namespace PortShotSpeed {

// The lifetime, in 60 Hz frames, of a projectile whose effect has none (no
// PSLT): CProjectileWeapon's "forever".
constexpr int kNoLifetime = 0x7FFFFF;

// CProjectileWeapon moves a projectile once a frame while its frame is below
// its lifetime, so it travels `lifetime` frames' worth. At `scale` times the
// speed it gets as far in lifetime / scale frames: rounded to whole frames, at
// least one. No lifetime stays none, and a scale that isn't above zero changes
// nothing.
inline int ScaledLifetime(int lifetime, float scale) {
  if (lifetime >= kNoLifetime || lifetime <= 0 || !(scale > 0.f) || !std::isfinite(scale)) {
    return lifetime;
  }
  const int frames = static_cast< int >(std::lround(static_cast< double >(lifetime) / scale));
  return frames < 1 ? 1 : frames;
}

} // namespace PortShotSpeed
