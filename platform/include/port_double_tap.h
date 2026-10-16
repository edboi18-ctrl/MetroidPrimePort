#pragma once

// Dead Space Prime's double-tap dash: spots a movement key pressed, let go and
// pressed again within a short window (PortDebug::DoubleTapDash). Plain data
// over each poll's key states, so tests/port_double_tap.cpp checks it alone.

namespace PortDoubleTap {

enum Direction : int { kForward, kBack, kLeft, kRight, kDirections };

// The most time from the first press to the second that still counts.
constexpr double kWindowMs = 250.0;

class Detector {
public:
  // One poll: whether a key for each direction is down, and the poll's clock in
  // milliseconds. Gives the direction pressed for the second time within
  // `windowMs` of its first press, or -1. A press of another direction in
  // between, or a first press held past the window, starts over.
  int Update(const bool held[kDirections], double nowMs, double windowMs = kWindowMs);
  void Reset();

private:
  bool mHeld[kDirections] = {};
  bool mPending[kDirections] = {};
  double mFirstPressMs[kDirections] = {};
};

} // namespace PortDoubleTap
