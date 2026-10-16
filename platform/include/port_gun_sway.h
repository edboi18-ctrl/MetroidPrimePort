#pragma once

// Dead Space Prime's gun sway (PortDebug::GunSway): as in Metroid Prime Hunters,
// the arm cannon leans a few degrees the way the view is turning, left, right, up
// or down, and settles back when it stops. The crosshair leans with it and shots
// go to the crosshair (CPlayer::PortGunLean). Plain data over each tick's view
// direction, so tests/port_gun_sway.cpp checks it alone.

namespace PortGunSway {

// The turn rate, in radians a second, that leans the gun all the way: 180
// degrees a second. Slower turns lean it in proportion.
constexpr float kFullTurnRate = 3.14159265f;
// How quickly the lean follows the turn: it closes 63% of the gap in this many
// seconds, at any tick rate.
constexpr float kFollowTime = 0.08f;
// A view that jumps further than this in one tick (radians) was moved, not
// turned: a cut or a teleport, which doesn't lean the gun.
constexpr float kCutAngle = 0.5f;

class State {
public:
  // One tick: the view's forward direction (any length) and the tick's length.
  // `maxRadians` is the most the gun leans; 0 or less keeps it straight.
  void Update(float forwardX, float forwardY, float forwardZ, float dt, float maxRadians);
  // Straight, and the next Update only takes the view's direction.
  void Reset();

  // The lean, in radians: yaw about the view's up axis, positive to the left
  // (counterclockwise, as Prime's yaw), and pitch about the view's right axis,
  // positive up.
  float Yaw() const { return mYaw; }
  float Pitch() const { return mPitch; }

private:
  float mYaw = 0.f;
  float mPitch = 0.f;
  float mLastYaw = 0.f;
  float mLastPitch = 0.f;
  bool mHasLast = false;
};

} // namespace PortGunSway
