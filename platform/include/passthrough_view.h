#pragma once

// Dead Space Prime's passthrough bridge: putting the host's triangles (passthrough_protocol.h's
// TriangleSet, in wire space) where Samus is, and onto the screen, for the F1 overlay's
// wireframe of the host's collision. Plain C++, so the tests can check it without the game.

#include "passthrough_protocol.h"

#include <cmath>

namespace PassthroughView {

using Passthrough::Vec3;

inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// A heading as the bridge has them: radians about +Z, 0 facing +Y (HostState::playerYaw). From a
// forward direction: facing +Y is 0, facing -X (a quarter turn anticlockwise seen from above) is
// +pi/2.
inline float Yaw(Vec3 forward) { return std::atan2(-forward.x, forward.y); }

// Where the host's player stands in wire space and where Samus stands in the guest's world, with
// their headings: wire space turned about +Z by her heading less his, and moved so his place
// lands on hers. The host's world is drawn as if he stood where she does, facing her way (as the
// puppet keeps him while he follows her).
struct Placement {
  Vec3 from;   // the host's player
  Vec3 to;     // Samus
  float cosTurn = 1.f;
  float sinTurn = 0.f;
};

inline Placement Place(Vec3 hostPlayer, float hostYaw, Vec3 samus, float samusYaw) {
  Placement p;
  p.from = hostPlayer;
  p.to = samus;
  p.cosTurn = std::cos(samusYaw - hostYaw);
  p.sinTurn = std::sin(samusYaw - hostYaw);
  return p;
}

inline Vec3 ToGuest(const Placement& p, Vec3 wire) {
  const Vec3 d = Sub(wire, p.from);
  return {p.to.x + p.cosTurn * d.x - p.sinTurn * d.y, p.to.y + p.sinTurn * d.x + p.cosTurn * d.y,
          p.to.z + d.z};
}

// The guest's camera: where it is, its forward and up axes (unit length, at right angles), its
// vertical field of view in degrees and its picture's width over height, as GuestState has them.
// Its right axis is forward x up (the guest's world is right-handed, +Z up).
struct Camera {
  Vec3 eye;
  Vec3 forward;
  Vec3 up;
  float fovY;
  float aspect;
};

// A segment on a screen `width` by `height` pixels (0, 0 its top left): the part in front of the
// camera (at least `nearest` ahead of it), projected as the camera's perspective has it. False
// when none of it is in front.
inline bool Project(const Camera& camera, float width, float height, Vec3 a, Vec3 b,
                    float out[4], float nearest = 0.05f) {
  const Vec3 right = Cross(camera.forward, camera.up);
  const Vec3 da = Sub(a, camera.eye);
  const Vec3 db = Sub(b, camera.eye);
  float ax = Dot(da, right), ay = Dot(da, camera.forward), az = Dot(da, camera.up);
  float bx = Dot(db, right), by = Dot(db, camera.forward), bz = Dot(db, camera.up);
  if (ay < nearest && by < nearest) {
    return false;
  }
  // Cut at the near distance: the part behind it goes.
  if (ay < nearest || by < nearest) {
    const float t = (nearest - ay) / (by - ay);
    const float cx = ax + t * (bx - ax), cz = az + t * (bz - az);
    if (ay < nearest) {
      ax = cx, ay = nearest, az = cz;
    } else {
      bx = cx, by = nearest, bz = cz;
    }
  }
  const float halfHeight = std::tan(0.5f * camera.fovY * 3.14159265f / 180.f);
  const float halfWidth = halfHeight * camera.aspect;
  if (!(halfHeight > 0.f) || !(halfWidth > 0.f)) {
    return false;
  }
  out[0] = 0.5f * width * (1.f + ax / (ay * halfWidth));
  out[1] = 0.5f * height * (1.f - az / (ay * halfHeight));
  out[2] = 0.5f * width * (1.f + bx / (by * halfWidth));
  out[3] = 0.5f * height * (1.f - bz / (by * halfHeight));
  return true;
}

} // namespace PassthroughView
