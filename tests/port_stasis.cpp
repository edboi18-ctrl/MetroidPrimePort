#include "port_stasis.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
int sLine = 0;
void Check(bool condition) {
  if (!condition) {
    std::fprintf(stderr, "stasis regression failed at line %d\n", sLine);
    std::abort();
  }
}
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    sLine = __LINE__;                                                                              \
    Check(cond);                                                                                   \
  } while (0)

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

using PortStasis::EResult;
using PortStasis::State;
using PortStasis::Tuning;

constexpr float kDt = 1.f / 60.f;

// Runs `seconds` of real time at 60 Hz.
void Run(State& s, const Tuning& t, float seconds) {
  const int ticks = static_cast< int >(std::lround(seconds / kDt));
  for (int i = 0; i < ticks; ++i) s.Tick(kDt, t);
}
} // namespace

int main() {
  const Tuning t; // 15% speed, 5 s with a 0.75 s ease-out, 50 a use, 5 a second back

  // A fresh state: a full meter, nothing held, everything at full speed.
  {
    State s;
    CHECK(Near(s.Meter(), PortStasis::kMeterMax));
    CHECK(s.HeldCount() == 0);
    CHECK(Near(s.TimeScale(7, t), 1.f));
    CHECK(Near(s.Strength(7, t), 0.f));
  }

  // No target costs nothing.
  {
    State s;
    CHECK(s.Apply(PortStasis::kNoTarget, t) == EResult::NoTarget);
    CHECK(Near(s.Meter(), 100.f));
    CHECK(s.HeldCount() == 0);
  }

  // A hold slows only its target, and spends one use.
  {
    State s;
    CHECK(s.Apply(5, t) == EResult::Held);
    CHECK(Near(s.Meter(), 50.f));
    CHECK(s.Held(5));
    CHECK(!s.Held(6));
    CHECK(Near(s.TimeScale(5, t), 0.15f));
    CHECK(Near(s.TimeScale(6, t), 1.f));
    CHECK(Near(s.Strength(5, t), 1.f));
    CHECK(Near(s.Remaining(5), 5.f));
  }

  // Holding the same target again starts its time over; a second use empties
  // the meter, and a third is refused without touching anything.
  {
    State s;
    s.Apply(5, t);
    Run(s, t, 2.f);
    CHECK(Near(s.Remaining(5), 3.f, 0.02f));
    CHECK(s.Apply(5, t) == EResult::Refreshed);
    CHECK(Near(s.Remaining(5), 5.f));
    CHECK(s.HeldCount() == 1);
    // 50 spent, 10 regained over the 2 s, 50 spent again.
    CHECK(Near(s.Meter(), 10.f, 0.05f));
    CHECK(s.Apply(6, t) == EResult::NoEnergy);
    CHECK(Near(s.Meter(), 10.f, 0.05f));
    CHECK(!s.Held(6));
  }

  // The ease-out: full strength until its last 0.75 s, then linearly back to
  // normal speed, and the hold ends on time.
  {
    State s;
    s.Apply(5, t);
    Run(s, t, 4.f); // 1 s left: still the body of the hold
    CHECK(Near(s.TimeScale(5, t), 0.15f));
    Run(s, t, 0.625f); // 0.375 s left: half way through the ease-out
    CHECK(Near(s.Strength(5, t), 0.5f, 0.03f));
    CHECK(Near(s.TimeScale(5, t), 1.f + (0.15f - 1.f) * 0.5f, 0.03f));
    Run(s, t, 0.5f);
    CHECK(!s.Held(5));
    CHECK(s.HeldCount() == 0);
    CHECK(Near(s.TimeScale(5, t), 1.f));
  }

  // The meter refills at its rate and stops at the top.
  {
    State s;
    s.SetMeter(0.f);
    Run(s, t, 4.f);
    CHECK(Near(s.Meter(), 20.f, 0.05f));
    Run(s, t, 60.f);
    CHECK(Near(s.Meter(), 100.f));
  }

  // With every slot taken, the hold closest to running out gives way.
  {
    State s;
    for (uint16_t id = 0; id < PortStasis::kMaxHeld; ++id) {
      s.SetMeter(100.f);
      CHECK(s.Apply(id, t) == EResult::Held);
      s.Tick(0.1f, t); // each older hold has less time left
    }
    CHECK(s.HeldCount() == PortStasis::kMaxHeld);
    s.SetMeter(100.f);
    CHECK(s.Apply(100, t) == EResult::Held);
    CHECK(s.HeldCount() == PortStasis::kMaxHeld);
    CHECK(s.Held(100));
    CHECK(!s.Held(0)); // the oldest
    CHECK(s.Held(1));
  }

  // Release drops one hold and keeps the rest; ReleaseAll keeps the meter.
  {
    State s;
    s.SetMeter(100.f);
    s.Apply(1, t);
    s.Apply(2, t);
    s.Release(1);
    CHECK(!s.Held(1));
    CHECK(s.Held(2));
    CHECK(s.HeldCount() == 1);
    s.Release(42); // not held: nothing happens
    CHECK(s.HeldCount() == 1);
    s.SetMeter(30.f);
    s.ReleaseAll();
    CHECK(s.HeldCount() == 0);
    CHECK(Near(s.Meter(), 30.f));
  }

  // Bad numbers don't break it: a time step that isn't a positive number does
  // nothing, a rate is kept above zero, and no ease-out means no division by it.
  {
    State s;
    s.Apply(5, t);
    s.Tick(-1.f, t);
    s.Tick(std::numeric_limits< float >::quiet_NaN(), t);
    CHECK(Near(s.Remaining(5), 5.f));
    Tuning frozen = t;
    frozen.timeScale = 0.f;
    CHECK(Near(s.TimeScale(5, frozen), 0.01f));
    Tuning sharp = t;
    sharp.easeOut = 0.f;
    Run(s, sharp, 4.99f);
    CHECK(Near(s.TimeScale(5, sharp), 0.15f));
    CHECK(Near(s.Strength(5, sharp), 1.f));
    s.SetMeter(std::numeric_limits< float >::infinity());
    CHECK(Near(s.Meter(), 0.f));
    s.SetMeter(250.f);
    CHECK(Near(s.Meter(), 100.f));
  }

  // A throw pays and holds nothing yet; the hold it lands costs nothing, and
  // landing on a held target starts its time over.
  {
    State s;
    CHECK(s.Throw(5, t) == EResult::Thrown);
    CHECK(Near(s.Meter(), 50.f));
    CHECK(!s.Held(5));
    CHECK(s.Hold(5, t) == EResult::Held);
    CHECK(Near(s.Meter(), 50.f));
    CHECK(Near(s.TimeScale(5, t), 0.15f));
    Run(s, t, 1.f);
    CHECK(s.Hold(5, t) == EResult::Refreshed);
    CHECK(Near(s.Remaining(5), 5.f));
    CHECK(s.HeldCount() == 1);
  }

  // A throw that can't go spends nothing; a hold needs a target.
  {
    State s;
    CHECK(s.Throw(PortStasis::kNoTarget, t) == EResult::NoTarget);
    CHECK(Near(s.Meter(), 100.f));
    s.SetMeter(49.f);
    CHECK(s.Throw(5, t) == EResult::NoEnergy);
    CHECK(Near(s.Meter(), 49.f));
    CHECK(s.Hold(PortStasis::kNoTarget, t) == EResult::NoTarget);
    CHECK(s.HeldCount() == 0);
  }

  // Two throws in flight at once: both paid up front, then the meter is empty.
  {
    State s;
    CHECK(s.Throw(1, t) == EResult::Thrown);
    CHECK(s.Throw(2, t) == EResult::Thrown);
    CHECK(s.Throw(3, t) == EResult::NoEnergy);
    CHECK(Near(s.Meter(), 0.f));
  }

  // The flight: a step moves exactly that far along the line, the last one
  // lands on the goal, and a step that isn't positive goes nowhere.
  {
    float pos[3] = {0.f, 0.f, 0.f};
    const float goal[3] = {3.f, 4.f, 0.f}; // 5 away
    CHECK(!PortStasis::StepToward(pos, goal, 2.f));
    CHECK(Near(pos[0], 1.2f) && Near(pos[1], 1.6f) && Near(pos[2], 0.f));
    CHECK(!PortStasis::StepToward(pos, goal, 0.f));
    CHECK(!PortStasis::StepToward(pos, goal, -1.f));
    CHECK(!PortStasis::StepToward(pos, goal, std::numeric_limits< float >::quiet_NaN()));
    CHECK(Near(pos[0], 1.2f) && Near(pos[1], 1.6f));
    CHECK(PortStasis::StepToward(pos, goal, 10.f));
    CHECK(pos[0] == 3.f && pos[1] == 4.f && pos[2] == 0.f);
    CHECK(PortStasis::StepToward(pos, goal, 0.f)); // already there
    const float far[3] = {std::numeric_limits< float >::infinity(), 0.f, 0.f};
    CHECK(!PortStasis::StepToward(pos, far, 1.f));
    CHECK(pos[0] == 3.f);
  }

  std::puts("stasis: all checks passed");
  return 0;
}
