// Dead Space Prime's passthrough probe: a stand-in host for the bridge (docs/PASSTHROUGH.md in
// the Dead Space Prime repository). Run it beside the port with F1 > Debug > Passthrough bridge
// on: it prints Samus's state twice a second and writes a host heartbeat, so the port's F1
// status reads "connected". With --still it only reads and writes nothing.
//
//   passthrough_probe [--still] [--seconds N]

#include "passthrough_shm.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char** argv) {
  bool still = false;
  double seconds = 0.0; // 0: until stopped
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--still") == 0) {
      still = true;
    } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      seconds = std::atof(argv[++i]);
    } else {
      std::fprintf(stderr, "usage: %s [--still] [--seconds N]\n", argv[0]);
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
                    "energy %.0f/%.0f  flags 0x%x morph %u\n",
                    samus.tick, samus.position.x, samus.position.y, samus.position.z,
                    samus.velocity.x, samus.velocity.y, samus.velocity.z, samus.forward.x,
                    samus.forward.y, samus.forward.z, samus.energy, samus.maxEnergy, samus.flags,
                    samus.morph);
      }
      std::fflush(stdout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  return 0;
}
