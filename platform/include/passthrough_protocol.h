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
// written for another version. 2: the host's collision near its player (TriangleSet). A field
// taken from `reserved` where 0 means "none" needs no new version, as a side that doesn't know it
// writes 0 there and ignores it: HostState::playerEyeHeight is one.
constexpr uint32_t kVersion = 2;
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
  float playerEyeHeight; // where the host puts its camera in first person, above his feet; the
                         // guest puts Samus's eyes there too, so the two views match (0: none)
  uint32_t reserved[15];
};

// The host's collision near its player (milestone 4 in docs/PASSTHROUGH.md): its world's
// triangles within `radius` of `centre`, in wire space, which the host publishes again as its
// player moves and as its world changes. The guest collides with them, and can draw them.
constexpr uint32_t kMaxTriangles = 16384;

enum TriangleFlags : uint32_t {
  kTriangleMoves = 1u << 0, // on something the host moves itself (a door, a lift): may go stale
};

struct Triangle {
  Vec3 a, b, c;   // its corners, wire space
  uint32_t flags; // TriangleFlags
};

struct TriangleSet {
  uint32_t generation; // raised with each set the host publishes; never 0 once published
  uint32_t count;      // triangles in use, at most kMaxTriangles
  Vec3 centre;         // the host's player as the set was picked
  float radius;        // the triangles are those reaching within this of the centre
  uint32_t reserved[10];
  Triangle triangles[kMaxTriangles];
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
  Slot< TriangleSet > triangles; // written by the host only; WriteDirect and ReadDirect
};

// The layout both sides rely on.
static_assert(sizeof(Vec3) == 12, "Vec3 layout");
static_assert(sizeof(GuestState) == 164, "GuestState layout");
static_assert(offsetof(GuestState, position) == 16 && offsetof(GuestState, fovY) == 76 &&
                  offsetof(GuestState, beam) == 96,
              "GuestState offsets");
static_assert(sizeof(HostState) == 96, "HostState layout");
static_assert(offsetof(HostState, playerPosition) == 8 &&
                  offsetof(HostState, playerMaxHealth) == 28 &&
                  offsetof(HostState, playerEyeHeight) == 32,
              "HostState offsets");
static_assert(sizeof(Triangle) == 40 && sizeof(TriangleSet) == 64 + 40 * kMaxTriangles &&
                  offsetof(TriangleSet, triangles) == 64,
              "TriangleSet layout");
static_assert(offsetof(SharedBlock, guest) == 16 && offsetof(SharedBlock, host) == 188 &&
                  offsetof(SharedBlock, triangles) == 292 &&
                  sizeof(SharedBlock) == 292 + 8 + sizeof(TriangleSet),
              "SharedBlock layout");
static_assert(std::is_trivially_copyable_v< GuestState > &&
                  std::is_trivially_copyable_v< HostState > &&
                  std::is_trivially_copyable_v< TriangleSet >,
              "plain data");
static_assert(sizeof(GuestState) % 4 == 0 && sizeof(HostState) % 4 == 0, "whole words");
static_assert(std::atomic_ref< uint32_t >::is_always_lock_free, "lock-free words across processes");

// The sequence lock, over memory another process writes at the same time. Every access is a
// 32-bit atomic, so a reader never sees half a word, and the sequence tells it whether the
// words it copied belong to one write.
template < typename T >
inline void Write(Slot< T >& slot, const T& value) {
  static_assert(sizeof(T) <= 4096, "a large slot is written with WriteDirect");
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
  static_assert(sizeof(T) <= 4096, "a large slot is read with ReadDirect");
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

// The same lock for a slot too large to copy through the stack (TriangleSet: 640 KiB), word by
// word straight between the slot and the caller's value. A ReadDirect that fails may leave `out`
// part written; a caller keeps its last good copy elsewhere.
template < typename T >
inline void WriteDirect(Slot< T >& slot, const T& value) {
  std::atomic_ref< uint32_t > sequence(slot.sequence);
  const uint32_t start = sequence.load(std::memory_order_relaxed);
  sequence.store(start + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  const unsigned char* in = reinterpret_cast< const unsigned char* >(&value);
  uint32_t* out = reinterpret_cast< uint32_t* >(&slot.value);
  for (size_t i = 0; i < sizeof(T) / 4; ++i) {
    uint32_t word;
    std::memcpy(&word, in + 4 * i, sizeof(word));
    std::atomic_ref< uint32_t >(out[i]).store(word, std::memory_order_relaxed);
  }
  sequence.store(start + 2, std::memory_order_release);
}

template < typename T >
inline bool ReadDirect(Slot< T >& slot, T& out, int tries = 4) {
  std::atomic_ref< uint32_t > sequence(slot.sequence);
  const uint32_t* in = reinterpret_cast< const uint32_t* >(&slot.value);
  unsigned char* to = reinterpret_cast< unsigned char* >(&out);
  for (int attempt = 0; attempt < tries; ++attempt) {
    const uint32_t before = sequence.load(std::memory_order_acquire);
    if (before == 0) {
      return false;
    }
    if (before & 1u) {
      continue;
    }
    for (size_t i = 0; i < sizeof(T) / 4; ++i) {
      const uint32_t word =
          std::atomic_ref< uint32_t >(const_cast< uint32_t& >(in[i])).load(std::memory_order_relaxed);
      std::memcpy(to + 4 * i, &word, sizeof(word));
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    if (sequence.load(std::memory_order_relaxed) == before) {
      return true;
    }
  }
  return false;
}

// A slot's sequence as it is now: a reader compares it with the one it last copied at, to copy a
// large slot only when it has changed.
template < typename T >
inline uint32_t Sequence(Slot< T >& slot) {
  return std::atomic_ref< uint32_t >(slot.sequence).load(std::memory_order_acquire);
}

} // namespace Passthrough
