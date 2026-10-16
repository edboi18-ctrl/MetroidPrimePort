#pragma once

// Dead Space's Stasis as plain data: a meter that pays for each use, and the
// enemies it holds, each slowed for a while and then eased back to full speed.
// The game side (port_dead_space.cpp) picks the target and applies the rate;
// this is arithmetic over real time, so tests/port_stasis.cpp links it alone.
//
// A target is a game object id's raw value (TUniqueId::value); kNoTarget is
// kInvalidUniqueId's.

#include <cstdint>

namespace PortStasis {

constexpr uint16_t kNoTarget = 0xFFFF;
constexpr int kMaxHeld = 8;
constexpr float kMeterMax = 100.f;

struct Tuning {
  // The fraction of normal speed a held enemy runs at.
  float timeScale = 0.15f;
  // How long a hold lasts, in real seconds, ease-out included.
  float duration = 5.f;
  // The end of the hold over which the enemy speeds back up to normal.
  float easeOut = 0.75f;
  // The meter one use spends, and what it regains per real second.
  float cost = 50.f;
  float rechargePerSecond = 5.f;
};

enum class EResult : uint8_t {
  Held,      // a new hold
  Refreshed, // already held: its time starts over
  NoTarget,
  NoEnergy,
  Thrown, // paid for: the charge is on its way, and Hold lands it
};

class State {
public:
  // Pays for a charge thrown at `target`; nothing is spent unless it goes.
  EResult Throw(uint16_t target, const Tuning& t);
  // Pays for a charge thrown at no one (manual aim, at whatever the crosshair
  // is on): Thrown, or NoEnergy with nothing spent.
  EResult Spend(const Tuning& t);
  // Holds `target` without paying: a thrown charge arriving. With every slot
  // taken, the hold closest to running out gives way.
  EResult Hold(uint16_t target, const Tuning& t);
  // Throw and Hold at once.
  EResult Apply(uint16_t target, const Tuning& t);
  // Advances real time: holds run down and end, the meter refills.
  void Tick(float dt, const Tuning& t);
  // The rate `target` runs at now: 1 when it isn't held.
  float TimeScale(uint16_t target, const Tuning& t) const;
  // How strongly `target` is held, 0..1: 1 until the ease-out, then down to 0.
  float Strength(uint16_t target, const Tuning& t) const;
  bool Held(uint16_t target) const { return Find(target) >= 0; }
  // Seconds of hold left; 0 when not held.
  float Remaining(uint16_t target) const;
  void Release(uint16_t target);
  // Drops every hold; the meter keeps its charge.
  void ReleaseAll() { mCount = 0; }
  int HeldCount() const { return mCount; }
  uint16_t HeldTarget(int i) const { return mHolds[i].target; }
  float Meter() const { return mMeter; }
  void SetMeter(float meter);

private:
  struct Slot {
    uint16_t target = kNoTarget;
    float remaining = 0.f;
  };
  int Find(uint16_t target) const;

  Slot mHolds[kMaxHeld];
  int mCount = 0;
  float mMeter = kMeterMax;
};

// A charge's flight: moves `pos` straight toward `goal` by up to `step`, and
// says whether it got there, in which case `pos` is `goal`. A step that isn't a
// positive number leaves `pos` where it is.
bool StepToward(float pos[3], const float goal[3], float step);

// Aiming by hand: how far along the ray from `origin` in the unit direction
// `dir` it first meets the box `lo`..`hi` grown by `pad` on every side, or a
// negative number when it misses (or the box is behind). 0 when `origin` is
// inside it.
float RayBoxDistance(const float origin[3], const float dir[3], const float lo[3],
                     const float hi[3], float pad);

} // namespace PortStasis
