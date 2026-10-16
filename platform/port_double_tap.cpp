#include "port_double_tap.h"

#include <cmath>

namespace PortDoubleTap {

int Detector::Update(const bool held[kDirections], double nowMs, double windowMs) {
  int fired = -1;
  for (int d = 0; d < kDirections; ++d) {
    const bool pressed = held[d] && !mHeld[d];
    mHeld[d] = held[d];
    if (!pressed) continue;
    const bool second = mPending[d] && std::isfinite(nowMs) && nowMs - mFirstPressMs[d] >= 0.0 &&
                        nowMs - mFirstPressMs[d] <= windowMs;
    // Any press starts over every other direction's count.
    for (int other = 0; other < kDirections; ++other) {
      if (other != d) mPending[other] = false;
    }
    if (second) {
      mPending[d] = false;
      if (fired < 0) fired = d;
    } else {
      mPending[d] = true;
      mFirstPressMs[d] = nowMs;
    }
  }
  return fired;
}

void Detector::Reset() { *this = Detector(); }

} // namespace PortDoubleTap
