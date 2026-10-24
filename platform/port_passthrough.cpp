#include "port_passthrough.h"

#include "passthrough_shm.h"

#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Cameras/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CGameCamera.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>

namespace PortPassthrough {

namespace {
using Clock = std::chrono::steady_clock;

// How long a host may go without writing before it counts as gone, and how often a failed
// open is retried.
constexpr auto kHostTimeout = std::chrono::seconds(1);
constexpr auto kRetryEvery = std::chrono::seconds(2);

// Set from the F1 overlay; the block is opened and closed on the game's thread, in Tick.
std::atomic< bool > sEnabled{false};
Passthrough::Mapping sMapping;
uint32_t sTick = 0;
Clock::time_point sNextTry{};
Clock::time_point sHostSeenAt{};
uint32_t sLastHostFrame = 0;
Passthrough::HostState sHost{};
// The host's triangle set: copied out of the block (640 KiB, so on the heap) only when its
// sequence has moved on.
std::unique_ptr< Passthrough::TriangleSet > sSetBuffer;
uint32_t sSetSequence = 0;
std::atomic< bool > sWireframeShown{false};
// The host's eye height while it applies (EyeHeight); heights outside this aren't taken.
std::atomic< float > sEyeHeight{0.f};
constexpr float kLowestEyes = 0.5f;
constexpr float kHighestEyes = 3.f;

// GetInfo reads from the F1 overlay, which may draw on another thread than the game ticks.
std::mutex sInfoMutex;
Info sInfo;
Wireframe sWireframe; // under sInfoMutex
bool sHaveWireframe = false;

void SetInfo(Status status, std::string message, const Passthrough::HostState* host,
             float ownEyeHeight = 0.f) {
  std::lock_guard lock(sInfoMutex);
  sInfo.status = status;
  sInfo.message = std::move(message);
  sInfo.guestTick = sTick;
  if (host != nullptr) {
    sInfo.host = *host;
  }
  sInfo.ownEyeHeight = ownEyeHeight;
}

Passthrough::Vec3 ToVec(const CVector3f& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

Passthrough::GuestState Gather(const CStateManager& mgr, float dt) {
  Passthrough::GuestState state{};
  state.tick = sTick;
  state.dt = dt;

  const CPlayer& player = *mgr.GetPlayer();
  CPlayerState& playerState = *mgr.GetPlayerState();
  const CCameraManager& cameras = *mgr.GetCameraManager();

  state.flags = Passthrough::kGuestInGame;
  if (mgr.GetGameState() != CStateManager::kGS_Running) {
    state.flags |= Passthrough::kGuestPaused;
  }
  if (player.GetPlayerMovementState() == NPlayer::kMS_OnGround) {
    state.flags |= Passthrough::kGuestOnGround;
  }
  if (cameras.IsInCinematicCamera()) {
    state.flags |= Passthrough::kGuestCinematic;
  }
  if (!playerState.IsAlive()) {
    state.flags |= Passthrough::kGuestDead;
  }
  // CPlayer::EPlayerMorphBallState and GuestMorph count the same way.
  state.morph = static_cast< uint32_t >(player.GetMorphballTransitionState());

  state.position = ToVec(player.GetTranslation());
  state.velocity = ToVec(player.GetVelocityWR());

  const CTransform4f camera = cameras.GetCurrentCameraTransform(mgr);
  state.eye = ToVec(camera.GetTranslation());
  state.forward = ToVec(camera.GetColumn(kDY).AsNormalized());
  state.up = ToVec(camera.GetColumn(kDZ).AsNormalized());
  state.fovY = cameras.GetCurrentCamera(mgr).GetRenderFov();
  state.aspect = CCameraManager::GetDefaultAspectRatio();

  state.energy = playerState.GetHealthInfo().GetHP();
  state.maxEnergy = playerState.CalculateHealth();
  state.visor = static_cast< uint32_t >(playerState.GetCurrentVisor());
  state.beam = static_cast< uint32_t >(playerState.GetCurrentBeam());
  return state;
}
// The host's triangle set, if it has published a new one, and where the overlay puts it: the host's
// player where Samus stands, turned to her heading.
void UpdateWireframe(Passthrough::SharedBlock& block, const Passthrough::GuestState& samus,
                     const CPlayer& player, bool connected) {
  std::shared_ptr< const std::vector< Passthrough::Triangle > > triangles;
  uint32_t generation = 0;
  float radius = 0.f;
  const uint32_t sequence = Passthrough::Sequence(block.triangles);
  if (sequence != sSetSequence && (sequence & 1u) == 0) {
    if (!sSetBuffer) {
      sSetBuffer = std::make_unique< Passthrough::TriangleSet >();
    }
    if (Passthrough::ReadDirect(block.triangles, *sSetBuffer) && sSetBuffer->generation != 0) {
      sSetSequence = sequence;
      const uint32_t count = (std::min)(sSetBuffer->count, Passthrough::kMaxTriangles);
      triangles = std::make_shared< const std::vector< Passthrough::Triangle > >(
          sSetBuffer->triangles, sSetBuffer->triangles + count);
      generation = sSetBuffer->generation;
      radius = sSetBuffer->radius;
    }
  }
  const PassthroughView::Vec3 facing = ToVec(player.GetTransform().GetColumn(kDY));
  std::lock_guard lock(sInfoMutex);
  if (triangles) {
    sWireframe.triangles = std::move(triangles);
    sWireframe.generation = generation;
    sWireframe.radius = radius;
  }
  sWireframe.placement = PassthroughView::Place(sHost.playerPosition, sHost.playerYaw,
                                                samus.position, PassthroughView::Yaw(facing));
  sWireframe.camera = {samus.eye, samus.forward, samus.up, samus.fovY, samus.aspect};
  sHaveWireframe = connected && (sHost.flags & Passthrough::kHostInGame) != 0 &&
                   sWireframe.triangles != nullptr;
}
} // namespace

bool Enabled() { return sEnabled.load(std::memory_order_relaxed); }

void SetEnabled(bool enabled) { sEnabled.store(enabled, std::memory_order_relaxed); }

void Tick(const CStateManager& mgr, float dt) {
  if (!Enabled()) {
    sEyeHeight.store(0.f, std::memory_order_relaxed);
    if (sMapping.IsOpen()) {
      Shutdown();
    }
    sNextTry = {};
    return;
  }
  const Clock::time_point now = Clock::now();
  if (!sMapping.IsOpen()) {
    if (now < sNextTry) {
      return;
    }
    sNextTry = now + kRetryEvery;
    switch (sMapping.Open()) {
    case Passthrough::OpenResult::Ok:
      sTick = 0;
      sLastHostFrame = 0;
      sHost = {};
      break;
    case Passthrough::OpenResult::Unsupported:
      SetInfo(Status::Error, "not available on this system", nullptr);
      return;
    case Passthrough::OpenResult::VersionMismatch:
      SetInfo(Status::Error, "the host was built for another bridge version", nullptr);
      return;
    case Passthrough::OpenResult::Failed:
      SetInfo(Status::Error, sMapping.Error(), nullptr);
      return;
    }
  }
  if (mgr.GetPlayer() == nullptr) {
    return;
  }

  ++sTick;
  if (sTick == 0) {
    sTick = 1;
  }
  Passthrough::SharedBlock& block = *sMapping.Block();
  const Passthrough::GuestState samus = Gather(mgr, dt);
  Passthrough::Write(block.guest, samus);

  Passthrough::HostState host{};
  if (Passthrough::Read(block.host, host)) {
    sHost = host;
    if (host.frame != sLastHostFrame) {
      sLastHostFrame = host.frame;
      sHostSeenAt = now;
    }
  }
  const bool connected = sLastHostFrame != 0 && now - sHostSeenAt < kHostTimeout;
  const float eyes = sHost.playerEyeHeight;
  sEyeHeight.store(connected && (sHost.flags & Passthrough::kHostInGame) != 0 &&
                           eyes >= kLowestEyes && eyes <= kHighestEyes
                       ? eyes
                       : 0.f,
                   std::memory_order_relaxed);
  SetInfo(connected ? Status::Connected : Status::Waiting, {}, &sHost,
          mgr.GetPlayer()->GetOwnEyeHeight());
  UpdateWireframe(block, samus, *mgr.GetPlayer(), connected);
}

Info GetInfo() {
  std::lock_guard lock(sInfoMutex);
  if (!Enabled()) {
    Info off;
    off.status = Status::Off;
    return off;
  }
  return sInfo;
}

bool GetWireframe(Wireframe& out) {
  std::lock_guard lock(sInfoMutex);
  if (!Enabled() || !sHaveWireframe) {
    return false;
  }
  out = sWireframe;
  return true;
}

float EyeHeight() { return sEyeHeight.load(std::memory_order_relaxed); }

bool WireframeShown() { return sWireframeShown.load(std::memory_order_relaxed); }

void SetWireframeShown(bool shown) { sWireframeShown.store(shown, std::memory_order_relaxed); }

void Shutdown() {
  sEyeHeight.store(0.f, std::memory_order_relaxed);
  sMapping.Close();
  sTick = 0;
  sLastHostFrame = 0;
  sHost = {};
  sSetSequence = 0;
  sSetBuffer.reset();
  SetInfo(Status::Off, {}, &sHost);
  std::lock_guard lock(sInfoMutex);
  sWireframe = {};
  sHaveWireframe = false;
}

} // namespace PortPassthrough
