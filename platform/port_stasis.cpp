#include "port_stasis.h"

#include <algorithm>
#include <cmath>

namespace PortStasis {
namespace {

float Clamp(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

// The tuning as used: a rate in (0, 1], a duration that isn't negative, an
// ease-out no longer than the hold, and no negative cost or recharge.
Tuning Sane(const Tuning& t) {
  Tuning s = t;
  s.timeScale = std::isfinite(s.timeScale) ? Clamp(s.timeScale, 0.01f, 1.f) : 1.f;
  s.duration = std::isfinite(s.duration) ? std::max(s.duration, 0.f) : 0.f;
  s.easeOut = std::isfinite(s.easeOut) ? Clamp(s.easeOut, 0.f, s.duration) : 0.f;
  s.cost = std::isfinite(s.cost) ? Clamp(s.cost, 0.f, kMeterMax) : kMeterMax;
  s.rechargePerSecond =
      std::isfinite(s.rechargePerSecond) ? std::max(s.rechargePerSecond, 0.f) : 0.f;
  return s;
}

// 1 for the body of the hold, falling linearly to 0 over the ease-out.
float StrengthAt(float remaining, const Tuning& t) {
  if (remaining <= 0.f) return 0.f;
  if (t.easeOut <= 0.f || remaining >= t.easeOut) return 1.f;
  return remaining / t.easeOut;
}

} // namespace

int State::Find(uint16_t target) const {
  for (int i = 0; i < mCount; ++i) {
    if (mHolds[i].target == target) return i;
  }
  return -1;
}

EResult State::Throw(uint16_t target, const Tuning& tuning) {
  if (target == kNoTarget) return EResult::NoTarget;
  const Tuning t = Sane(tuning);
  if (mMeter < t.cost) return EResult::NoEnergy;
  mMeter -= t.cost;
  return EResult::Thrown;
}

EResult State::Apply(uint16_t target, const Tuning& tuning) {
  const EResult paid = Throw(target, tuning);
  return paid == EResult::Thrown ? Hold(target, tuning) : paid;
}

EResult State::Hold(uint16_t target, const Tuning& tuning) {
  if (target == kNoTarget) return EResult::NoTarget;
  const Tuning t = Sane(tuning);
  int i = Find(target);
  const bool refreshed = i >= 0;
  if (!refreshed) {
    if (mCount < kMaxHeld) {
      i = mCount++;
    } else {
      i = 0;
      for (int j = 1; j < mCount; ++j) {
        if (mHolds[j].remaining < mHolds[i].remaining) i = j;
      }
    }
    mHolds[i].target = target;
  }
  mHolds[i].remaining = t.duration;
  return refreshed ? EResult::Refreshed : EResult::Held;
}

void State::Tick(float dt, const Tuning& tuning) {
  if (!std::isfinite(dt) || dt <= 0.f) return;
  const Tuning t = Sane(tuning);
  int kept = 0;
  for (int i = 0; i < mCount; ++i) {
    Slot hold = mHolds[i];
    hold.remaining -= dt;
    if (hold.remaining > 0.f) mHolds[kept++] = hold;
  }
  mCount = kept;
  mMeter = std::min(kMeterMax, mMeter + t.rechargePerSecond * dt);
}

float State::TimeScale(uint16_t target, const Tuning& tuning) const {
  const int i = Find(target);
  if (i < 0) return 1.f;
  const Tuning t = Sane(tuning);
  return 1.f + (t.timeScale - 1.f) * StrengthAt(mHolds[i].remaining, t);
}

float State::Strength(uint16_t target, const Tuning& tuning) const {
  const int i = Find(target);
  return i < 0 ? 0.f : StrengthAt(mHolds[i].remaining, Sane(tuning));
}

float State::Remaining(uint16_t target) const {
  const int i = Find(target);
  return i < 0 ? 0.f : std::max(mHolds[i].remaining, 0.f);
}

void State::Release(uint16_t target) {
  const int i = Find(target);
  if (i < 0) return;
  for (int j = i + 1; j < mCount; ++j) mHolds[j - 1] = mHolds[j];
  --mCount;
}

void State::SetMeter(float meter) {
  mMeter = std::isfinite(meter) ? Clamp(meter, 0.f, kMeterMax) : 0.f;
}

bool StepToward(float pos[3], const float goal[3], float step) {
  const float d[3] = {goal[0] - pos[0], goal[1] - pos[1], goal[2] - pos[2]};
  const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (!std::isfinite(dist)) return false;
  if (!std::isfinite(step) || step <= 0.f) return dist == 0.f;
  if (dist <= step) {
    for (int i = 0; i < 3; ++i) pos[i] = goal[i];
    return true;
  }
  const float k = step / dist;
  for (int i = 0; i < 3; ++i) pos[i] += d[i] * k;
  return false;
}

} // namespace PortStasis
