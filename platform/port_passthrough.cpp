#include "port_passthrough.h"

#include "passthrough_shm.h"

#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Cameras/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CGameCamera.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"

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

// GetInfo reads from the F1 overlay, which may draw on another thread than the game ticks.
std::mutex sInfoMutex;
Info sInfo;

void SetInfo(Status status, std::string message, const Passthrough::HostState* host) {
  std::lock_guard lock(sInfoMutex);
  sInfo.status = status;
  sInfo.message = std::move(message);
  sInfo.guestTick = sTick;
  if (host != nullptr) {
    sInfo.host = *host;
  }
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
} // namespace

bool Enabled() { return sEnabled.load(std::memory_order_relaxed); }

void SetEnabled(bool enabled) { sEnabled.store(enabled, std::memory_order_relaxed); }

void Tick(const CStateManager& mgr, float dt) {
  if (!Enabled()) {
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
  Passthrough::Write(block.guest, Gather(mgr, dt));

  Passthrough::HostState host{};
  if (Passthrough::Read(block.host, host)) {
    sHost = host;
    if (host.frame != sLastHostFrame) {
      sLastHostFrame = host.frame;
      sHostSeenAt = now;
    }
  }
  const bool connected = sLastHostFrame != 0 && now - sHostSeenAt < kHostTimeout;
  SetInfo(connected ? Status::Connected : Status::Waiting, {}, &sHost);
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

void Shutdown() {
  sMapping.Close();
  sTick = 0;
  sLastHostFrame = 0;
  sHost = {};
  SetInfo(Status::Off, {}, &sHost);
}

} // namespace PortPassthrough
