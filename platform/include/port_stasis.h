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
};

class State {
public:
  // Holds `target`, paying the cost; nothing is spent unless it holds. With
  // every slot taken, the hold closest to running out gives way.
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
  struct Hold {
    uint16_t target = kNoTarget;
    float remaining = 0.f;
  };
  int Find(uint16_t target) const;

  Hold mHolds[kMaxHeld];
  int mCount = 0;
  float mMeter = kMeterMax;
};

} // namespace PortStasis
