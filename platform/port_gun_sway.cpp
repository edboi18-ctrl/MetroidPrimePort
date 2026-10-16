#include "port_gun_sway.h"

#include <algorithm>
#include <cmath>

namespace PortGunSway {

namespace {
constexpr float kPi = 3.14159265f;

// a - b, wrapped into [-pi, pi].
float AngleDelta(float a, float b) {
  float d = std::fmod(a - b, 2.f * kPi);
  if (d > kPi)
    d -= 2.f * kPi;
  if (d < -kPi)
    d += 2.f * kPi;
  return d;
}
} // namespace

void State::Update(float forwardX, float forwardY, float forwardZ, float dt, float maxRadians) {
  const float flat = std::hypot(forwardX, forwardY);
  if (!(maxRadians > 0.f) || !(dt > 0.f) || !std::isfinite(flat + forwardZ) ||
      (flat == 0.f && forwardZ == 0.f)) {
    Reset();
    return;
  }
  // Prime's forward is +y and its yaw turns counterclockwise, toward -x.
  const float yaw = std::atan2(-forwardX, forwardY);
  const float pitch = std::atan2(forwardZ, flat);
  float yawTurn = 0.f;
  float pitchTurn = 0.f;
  if (mHasLast) {
    yawTurn = AngleDelta(yaw, mLastYaw);
    pitchTurn = pitch - mLastPitch;
    if (std::fabs(yawTurn) > kCutAngle || std::fabs(pitchTurn) > kCutAngle) {
      yawTurn = pitchTurn = 0.f;
    }
  }
  mLastYaw = yaw;
  mLastPitch = pitch;
  mHasLast = true;

  // Lean toward the turn, in proportion to its rate up to kFullTurnRate.
  const float yawGoal = std::clamp(yawTurn / (dt * kFullTurnRate), -1.f, 1.f) * maxRadians;
  const float pitchGoal = std::clamp(pitchTurn / (dt * kFullTurnRate), -1.f, 1.f) * maxRadians;
  const float follow = 1.f - std::exp(-dt / kFollowTime);
  mYaw += (yawGoal - mYaw) * follow;
  mPitch += (pitchGoal - mPitch) * follow;
}

void State::Reset() {
  mYaw = mPitch = 0.f;
  mHasLast = false;
}

} // namespace PortGunSway
