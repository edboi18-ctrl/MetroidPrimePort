#include "passthrough_shm.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    samus.flags = kGuestInGame | kGuestOnGround;
    Write(guest.Block()->guest, samus);
    GuestState seen{};
    CHECK(Read(host.Block()->guest, seen) && seen.position.x == 10.f && seen.position.y == -4.f &&
          seen.flags == (kGuestInGame | kGuestOnGround));

    HostState isaac{};
    isaac.frame = 42;
    isaac.playerHealth = 100.f;
    Write(host.Block()->host, isaac);
    HostState back{};
    CHECK(Read(guest.Block()->host, back) && back.frame == 42 && back.playerHealth == 100.f);

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
        s.reserved[15] = n;
        Write(writer.Block()->guest, s);
      }
      done = true;
    });
    int reads = 0;
    uint32_t last = 0;
    while (!done) {
      GuestState s{};
      if (Read(reader.Block()->guest, s)) {
        CHECK(s.reserved[15] == s.tick && s.dt == static_cast< float >(s.tick) &&
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
