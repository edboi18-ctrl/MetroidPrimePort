// Dead Space Prime's passthrough probe: a stand-in host for the bridge (docs/PASSTHROUGH.md in
// the Dead Space Prime repository). Run it beside the port with F1 > Debug > Passthrough bridge
// on: it prints Samus's state twice a second and writes a host heartbeat, so the port's F1
// status reads "connected". With --still it only reads and writes nothing. With --room its
// player stands at the origin facing +Y, in a made-up room it publishes as the host's collision
// (a floor 20 m square, a wall 6 m ahead of him with a doorway, and a door in it that moves), for
// F1 > Debug > Passthrough's "Draw the host's collision" and "Walk in the host's world", with
// his eyes 1.65 m up, where Samus's go too, and his size Isaac's (a capsule 0.38 m in radius,
// 1.8 m tall), which Samus's becomes while she walks there.
//
//   passthrough_probe [--still] [--room] [--seconds N]

#include "passthrough_shm.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

namespace {
// Two triangles for a quad, corners in order around it.
void Quad(Passthrough::TriangleSet& set, Passthrough::Vec3 a, Passthrough::Vec3 b,
          Passthrough::Vec3 c, Passthrough::Vec3 d, uint32_t flags = 0) {
  if (set.count + 2 > Passthrough::kMaxTriangles) {
    return;
  }
  set.triangles[set.count++] = {a, b, c, flags};
  set.triangles[set.count++] = {a, c, d, flags};
}

// The made-up room, in wire space (+Z up): its floor in 2 m squares, the wall across +Y at 6 m
// with a 2 m doorway in it, and the door, 2.5 m high.
void MakeRoom(Passthrough::TriangleSet& set) {
  set = {};
  set.generation = 1;
  set.radius = 20.f;
  for (int i = -5; i < 5; ++i) {
    for (int j = -5; j < 5; ++j) {
      const float x = 2.f * i, y = 2.f * j;
      Quad(set, {x, y, 0.f}, {x + 2.f, y, 0.f}, {x + 2.f, y + 2.f, 0.f}, {x, y + 2.f, 0.f});
    }
  }
  Quad(set, {-10.f, 6.f, 0.f}, {-1.f, 6.f, 0.f}, {-1.f, 6.f, 4.f}, {-10.f, 6.f, 4.f});
  Quad(set, {1.f, 6.f, 0.f}, {10.f, 6.f, 0.f}, {10.f, 6.f, 4.f}, {1.f, 6.f, 4.f});
  Quad(set, {-1.f, 6.f, 2.5f}, {1.f, 6.f, 2.5f}, {1.f, 6.f, 4.f}, {-1.f, 6.f, 4.f});
  Quad(set, {-1.f, 6.f, 0.f}, {1.f, 6.f, 0.f}, {1.f, 6.f, 2.5f}, {-1.f, 6.f, 2.5f},
       Passthrough::kTriangleMoves);
}
} // namespace

int main(int argc, char** argv) {
  bool still = false;
  bool room = false;
  double seconds = 0.0; // 0: until stopped
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--still") == 0) {
      still = true;
    } else if (std::strcmp(argv[i], "--room") == 0) {
      room = true;
    } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      seconds = std::atof(argv[++i]);
    } else {
      std::fprintf(stderr, "usage: %s [--still] [--room] [--seconds N]\n", argv[0]);
      return 2;
    }
  }

  Passthrough::Mapping mapping;
  switch (mapping.Open()) {
  case Passthrough::OpenResult::Ok:
    break;
  case Passthrough::OpenResult::VersionMismatch:
    std::fprintf(stderr,
                 "The shared block was made by another bridge version (this probe is v%u).\n",
                 Passthrough::kVersion);
    return 1;
  default:
    std::fprintf(stderr, "Can't open the shared block: %s\n", mapping.Error().c_str());
    return 1;
  }
  std::printf(
      "Passthrough probe, bridge v%u. Waiting for the port (F1 > Debug > Passthrough bridge).\n",
      Passthrough::kVersion);
  if (room && !still) {
    auto set = std::make_unique< Passthrough::TriangleSet >();
    MakeRoom(*set);
    Passthrough::WriteDirect(mapping.Block()->triangles, *set);
    std::printf("Published a made-up room: %u triangles.\n", set->count);
  }

  using Clock = std::chrono::steady_clock;
  const Clock::time_point start = Clock::now();
  Clock::time_point nextPrint = start;
  uint32_t frame = 0;
  uint32_t lastTick = 0;
  while (seconds <= 0.0 ||
         std::chrono::duration< double >(Clock::now() - start).count() < seconds) {
    if (!still) {
      Passthrough::HostState host{};
      if (++frame == 0) {
        frame = 1;
      }
      host.frame = frame;
      if (room) {
        host.flags = Passthrough::kHostInGame;
        host.playerEyeHeight = 1.65f; // as the Dead Space plugin puts Isaac's
        host.playerRadius = 0.38f;    // and his size, as Dead Space has it
        host.playerHeight = 1.8f;
      }
      Passthrough::Write(mapping.Block()->host, host);
    }
    if (Clock::now() >= nextPrint) {
      nextPrint += std::chrono::milliseconds(500);
      Passthrough::GuestState samus{};
      if (!Passthrough::Read(mapping.Block()->guest, samus)) {
        std::printf("no Samus yet\n");
      } else if (samus.tick == lastTick) {
        std::printf("tick %u (not moving: the port is paused, closed, or the bridge is off)\n",
                    samus.tick);
      } else {
        lastTick = samus.tick;
        std::printf("tick %u  pos %.2f %.2f %.2f  vel %.2f %.2f %.2f  look %.2f %.2f %.2f  "
                    "energy %.0f/%.0f  flags 0x%x morph %u%s\n",
                    samus.tick, samus.position.x, samus.position.y, samus.position.z,
                    samus.velocity.x, samus.velocity.y, samus.velocity.z, samus.forward.x,
                    samus.forward.y, samus.forward.z, samus.energy, samus.maxEnergy, samus.flags,
                    samus.morph,
                    (samus.flags & Passthrough::kGuestInHostWorld) != 0 ? "  (in the host's world)"
                                                                        : "");
      }
      std::fflush(stdout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  return 0;
}
