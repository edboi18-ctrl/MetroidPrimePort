#include "passthrough_shm.h"
#include "passthrough_view.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

namespace {
int sLine = 0;
void Check(bool condition) {
  if (!condition) {
    std::fprintf(stderr, "passthrough regression failed at line %d\n", sLine);
    std::abort();
  }
}
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    sLine = __LINE__;                                                                              \
    Check(cond);                                                                                   \
  } while (0)

using namespace Passthrough;

// A name of the test's own, so a running game's bridge is left alone.
std::string TestName() { return "DeadSpacePrimePassthroughTest" + std::to_string(std::rand()); }
} // namespace

int main() {
  std::srand(static_cast< unsigned >(std::chrono::steady_clock::now().time_since_epoch().count()));

  // The slot's sequence lock alone: nothing written reads as nothing, a write reads back whole.
  {
    Slot< GuestState > slot{};
    GuestState out{};
    CHECK(!Read(slot, out));
    GuestState in{};
    in.tick = 7;
    in.position = {1.f, 2.f, 3.f};
    in.fovY = 55.f;
    Write(slot, in);
    CHECK(slot.sequence == 2);
    CHECK(Read(slot, out) && out.tick == 7 && out.position.z == 3.f && out.fovY == 55.f);
    // A writer caught midway (an odd sequence) isn't read.
    slot.sequence = 3;
    CHECK(!Read(slot, out, 2));
  }

  // The triangle set, too large for the stack: written and read straight, and its sequence
  // tells a reader whether there's anything new.
  {
    auto slot = std::make_unique< Slot< TriangleSet > >();
    auto in = std::make_unique< TriangleSet >();
    auto out = std::make_unique< TriangleSet >();
    CHECK(Sequence(*slot) == 0 && !ReadDirect(*slot, *out));
    in->generation = 3;
    in->count = 2;
    in->centre = {1.f, 2.f, 3.f};
    in->radius = 20.f;
    in->triangles[0] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0};
    in->triangles[kMaxTriangles - 1] = {{5.f, 5.f, 5.f}, {6.f, 5.f, 5.f}, {5.f, 6.f, 5.f},
                                        kTriangleMoves};
    WriteDirect(*slot, *in);
    CHECK(Sequence(*slot) == 2);
    CHECK(ReadDirect(*slot, *out) && out->generation == 3 && out->count == 2 &&
          out->centre.z == 3.f && out->radius == 20.f && out->triangles[0].b.x == 1.f &&
          out->triangles[kMaxTriangles - 1].c.y == 6.f &&
          out->triangles[kMaxTriangles - 1].flags == kTriangleMoves);
    slot->sequence = 5;
    CHECK(!ReadDirect(*slot, *out, 2));
  }

  // The wireframe's view: the host's world put where Samus is, and onto the screen.
  {
    using namespace PassthroughView;
    const auto same = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    // Headings: facing +Y is 0, facing -X a quarter turn.
    CHECK(same(Yaw({0.f, 1.f, 0.f}), 0.f) && same(Yaw({-1.f, 0.f, 0.f}), 1.5707963f));
    // He stands at (10, 0, 5) facing +Y; she at (0, 0, 1) facing -X: a point 2 m ahead of him
    // and 1 m up is 2 m ahead of her and 1 m up.
    const Placement place = Place({10.f, 0.f, 5.f}, 0.f, {0.f, 0.f, 1.f}, 1.5707963f);
    const Vec3 ahead = ToGuest(place, {10.f, 2.f, 6.f});
    CHECK(same(ahead.x, -2.f) && same(ahead.y, 0.f) && same(ahead.z, 2.f));
    // A camera at the origin looking along +Y, +Z up, 90 degrees high on a square screen: a point
    // straight ahead is mid-screen, one up and ahead at 45 degrees the top edge, one to the right
    // at 45 degrees the right edge.
    const Camera camera{{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, 90.f, 1.f};
    float line[4];
    CHECK(Project(camera, 100.f, 100.f, {0.f, 5.f, 0.f}, {0.f, 5.f, 5.f}, line));
    CHECK(same(line[0], 50.f) && same(line[1], 50.f) && same(line[2], 50.f) &&
          same(line[3], 0.f));
    CHECK(Project(camera, 100.f, 100.f, {0.f, 5.f, 0.f}, {5.f, 5.f, 0.f}, line));
    CHECK(same(line[2], 100.f) && same(line[3], 50.f));
    // Behind the camera: nothing; half behind: cut where it passes the near distance.
    CHECK(!Project(camera, 100.f, 100.f, {0.f, -1.f, 0.f}, {1.f, -2.f, 0.f}, line));
    CHECK(Project(camera, 100.f, 100.f, {0.f, -1.f, 1.f}, {0.f, 1.f, 1.f}, line, 0.5f));
    CHECK(same(line[1], -50.f) && same(line[3], 0.f));
  }

#ifndef __ANDROID__
  const std::string name = TestName();
  Mapping::Unlink(name.c_str());

  // Two openers of one name see one block: the second finds the first's header, and what one
  // side writes the other reads.
  {
    Mapping guest;
    Mapping host;
    CHECK(guest.Open(name.c_str()) == OpenResult::Ok);
    CHECK(host.Open(name.c_str()) == OpenResult::Ok);
    CHECK(guest.Block()->magic == kMagic && guest.Block()->version == kVersion &&
          guest.Block()->size == sizeof(SharedBlock));

    GuestState samus{};
    samus.tick = 1;
    samus.position = {10.f, -4.f, 2.5f};
    samus.flags = kGuestInGame | kGuestOnGround | kGuestInHostWorld;
    Write(guest.Block()->guest, samus);
    GuestState seen{};
    CHECK(Read(host.Block()->guest, seen) && seen.position.x == 10.f && seen.position.y == -4.f &&
          seen.flags == (kGuestInGame | kGuestOnGround | kGuestInHostWorld));

    HostState isaac{};
    isaac.frame = 42;
    isaac.playerHealth = 100.f;
    isaac.playerEyeHeight = 1.65f;
    isaac.playerRadius = 0.38f;
    isaac.playerHeight = 1.8f;
    Write(host.Block()->host, isaac);
    HostState back{};
    CHECK(Read(guest.Block()->host, back) && back.frame == 42 && back.playerHealth == 100.f &&
          back.playerEyeHeight == 1.65f && back.playerRadius == 0.38f &&
          back.playerHeight == 1.8f);

    auto set = std::make_unique< TriangleSet >();
    set->generation = 1;
    set->count = 1;
    set->triangles[0] = {{1.f, 2.f, 3.f}, {4.f, 5.f, 6.f}, {7.f, 8.f, 9.f}, 0};
    WriteDirect(host.Block()->triangles, *set);
    auto got = std::make_unique< TriangleSet >();
    CHECK(ReadDirect(guest.Block()->triangles, *got) && got->generation == 1 &&
          got->triangles[0].c.z == 9.f);

    // A block made by another version is refused.
    host.Block()->version = kVersion + 1;
    Mapping third;
    CHECK(third.Open(name.c_str()) == OpenResult::VersionMismatch && !third.IsOpen());
    host.Block()->version = kVersion;
  }
  Mapping::Unlink(name.c_str());

  // A reader racing a writer only ever sees whole writes: every field of a write carries the
  // same number, so a mix of two writes would show.
  {
    Mapping writer;
    Mapping reader;
    CHECK(writer.Open(name.c_str()) == OpenResult::Ok &&
          reader.Open(name.c_str()) == OpenResult::Ok);
    std::atomic< bool > done{false};
    std::thread thread([&] {
      for (uint32_t n = 1; n <= 200000; ++n) {
        GuestState s{};
        s.tick = n;
        s.dt = static_cast< float >(n);
        s.position = {s.dt, s.dt, s.dt};
        s.touching = n;
        s.reserved[14] = n;
        Write(writer.Block()->guest, s);
      }
      done = true;
    });
    int reads = 0;
    uint32_t last = 0;
    while (!done) {
      GuestState s{};
      if (Read(reader.Block()->guest, s)) {
        CHECK(s.reserved[14] == s.tick && s.touching == s.tick &&
              s.dt == static_cast< float >(s.tick) &&
              s.position.z == s.dt);
        CHECK(s.tick >= last);
        last = s.tick;
        ++reads;
      }
    }
    thread.join();
    CHECK(reads > 0);
  }
  Mapping::Unlink(name.c_str());
#endif

  std::puts("passthrough ok");
  return 0;
}
