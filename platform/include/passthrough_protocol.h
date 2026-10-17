#pragma once

// Dead Space Prime's passthrough bridge: the wire format between the Metroid Prime port (the
// guest, which owns Samus: movement, camera, weapons and HUD) and a plugin inside Dead Space
// (2008, PC) (the host, which owns the world: level, scripting and enemies). Both games run at
// once and share one block of named shared memory, laid out as SharedBlock below.
//
// The host plugin is a 32-bit DLL and the port a 64-bit program, so every field is 4 bytes
// (float or uint32_t), with no pointers and no 64-bit integers: the layout is the same on both,
// and the static_asserts at the end pin it. Standard C++ only, so the host plugin can include
// this header unchanged. docs/PASSTHROUGH.md in the Dead Space Prime repository is the contract
// these structures implement.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Passthrough {

// "DSPT" read as a little-endian uint32_t.
constexpr uint32_t kMagic = 0x54505344u;
// Raised whenever SharedBlock's layout or a field's meaning changes. Each side refuses a block
// written for another version.
constexpr uint32_t kVersion = 1;
// The shared memory's name. Windows: in the session's Local\ namespace (CreateFileMappingW).
// Elsewhere: a POSIX shared memory object (shm_open), for tests and tools.
constexpr const char* kSharedMemoryName = "DeadSpacePrimePassthrough";

struct Vec3 {
  float x, y, z;
};

// Wire space is the guest's world space: right-handed, +Z up, the units of Metroid Prime's
// world. The host plugin maps its own coordinates to and from it (docs/PASSTHROUGH.md,
// "Coordinates").

enum GuestFlags : uint32_t {
  kGuestInGame = 1u << 0,    // a game is loaded and running
  kGuestPaused = 1u << 1,    // the pause screen or a menu is up
  kGuestOnGround = 1u << 2,  // Samus stands on something
  kGuestCinematic = 1u << 3, // a cutscene camera is in control
  kGuestDead = 1u << 4,      // Samus has died
};

enum GuestMorph : uint32_t {
  kMorphUnmorphed = 0,
  kMorphMorphed = 1,
  kMorphMorphing = 2,
  kMorphUnmorphing = 3,
};

// What the guest publishes once a game tick.
struct GuestState {
  uint32_t tick;   // simulation ticks since the guest attached; never 0 once published
  float dt;        // this tick's length, in seconds
  uint32_t flags;  // GuestFlags
  uint32_t morph;  // GuestMorph
  Vec3 position;   // Samus's feet
  Vec3 velocity;   // in world units a second
  Vec3 eye;        // the camera's position
  Vec3 forward;    // the camera's forward axis, unit length
  Vec3 up;         // the camera's up axis, unit length
  float fovY;      // the camera's vertical field of view, in degrees
  float aspect;    // the picture's width over its height
  float energy;    // Samus's energy
  float maxEnergy; // and its most with her energy tanks
  uint32_t visor;  // Metroid Prime's CPlayerState::EPlayerVisor
  uint32_t beam;   // Metroid Prime's CPlayerState::EBeamId
  uint32_t reserved[16];
};

enum HostFlags : uint32_t {
  kHostInGame = 1u << 0,   // a level is loaded and the player is in control
  kHostCutscene = 1u << 1, // a cutscene or scripted sequence has the player
  kHostLoading = 1u << 2,  // a level is loading
};

// What the host publishes once a frame.
struct HostState {
  uint32_t frame;        // host frames since it attached; never 0 once published
  uint32_t flags;        // HostFlags
  Vec3 playerPosition;   // the host's own player (Isaac), in wire space
  float playerYaw;       // his heading, in radians about +Z, 0 facing +Y
  float playerHealth;    // his health
  float playerMaxHealth; // and its most
  uint32_t reserved[16];
};

// One side's state behind a sequence lock: the writer makes `sequence` odd, writes, then makes
// it even again; a reader copies between two even, equal reads of it.
template < typename T >
struct Slot {
  uint32_t sequence;
  uint32_t padding;
  T value;
};

struct SharedBlock {
  uint32_t magic;   // kMagic once initialised
  uint32_t version; // kVersion
  uint32_t size;    // sizeof(SharedBlock)
  uint32_t reserved;
  Slot< GuestState > guest;
  Slot< HostState > host;
};

// The layout both sides rely on.
static_assert(sizeof(Vec3) == 12, "Vec3 layout");
static_assert(sizeof(GuestState) == 164, "GuestState layout");
static_assert(offsetof(GuestState, position) == 16 && offsetof(GuestState, fovY) == 76 &&
                  offsetof(GuestState, beam) == 96,
              "GuestState offsets");
static_assert(sizeof(HostState) == 96, "HostState layout");
static_assert(offsetof(HostState, playerPosition) == 8 &&
                  offsetof(HostState, playerMaxHealth) == 28,
              "HostState offsets");
static_assert(offsetof(SharedBlock, guest) == 16 && offsetof(SharedBlock, host) == 188 &&
                  sizeof(SharedBlock) == 292,
              "SharedBlock layout");
static_assert(std::is_trivially_copyable_v< GuestState > &&
                  std::is_trivially_copyable_v< HostState >,
              "plain data");
static_assert(sizeof(GuestState) % 4 == 0 && sizeof(HostState) % 4 == 0, "whole words");
static_assert(std::atomic_ref< uint32_t >::is_always_lock_free, "lock-free words across processes");

// The sequence lock, over memory another process writes at the same time. Every access is a
// 32-bit atomic, so a reader never sees half a word, and the sequence tells it whether the
// words it copied belong to one write.
template < typename T >
inline void Write(Slot< T >& slot, const T& value) {
  std::atomic_ref< uint32_t > sequence(slot.sequence);
  const uint32_t start = sequence.load(std::memory_order_relaxed);
  sequence.store(start + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  uint32_t words[sizeof(T) / 4];
  std::memcpy(words, &value, sizeof(T));
  uint32_t* out = reinterpret_cast< uint32_t* >(&slot.value);
  for (size_t i = 0; i < sizeof(T) / 4; ++i) {
    std::atomic_ref< uint32_t >(out[i]).store(words[i], std::memory_order_relaxed);
  }
  sequence.store(start + 2, std::memory_order_release);
}

// Copies the slot's latest complete write into `out`. False when the writer was busy on every
// try, or nothing has been written yet.
template < typename T >
inline bool Read(Slot< T >& slot, T& out, int tries = 8) {
  std::atomic_ref< uint32_t > sequence(slot.sequence);
  uint32_t* in = reinterpret_cast< uint32_t* >(&slot.value);
  for (int attempt = 0; attempt < tries; ++attempt) {
    const uint32_t before = sequence.load(std::memory_order_acquire);
    if (before == 0) {
      return false;
    }
    if (before & 1u) {
      continue;
    }
    uint32_t words[sizeof(T) / 4];
    for (size_t i = 0; i < sizeof(T) / 4; ++i) {
      words[i] = std::atomic_ref< uint32_t >(in[i]).load(std::memory_order_relaxed);
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    if (sequence.load(std::memory_order_relaxed) == before) {
      std::memcpy(&out, words, sizeof(T));
      return true;
    }
  }
  return false;
}

} // namespace Passthrough
