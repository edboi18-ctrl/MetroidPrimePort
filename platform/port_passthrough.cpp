#include "port_passthrough.h"

#include "passthrough_shm.h"

#include "MetroidPrime/CGameArea.hpp"
#include "MetroidPrime/CObjectList.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/CWorld.hpp"
#include "MetroidPrime/Cameras/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CGameCamera.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/Tweaks/CTweakPlayer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
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
// The latest triangle set, for the game's thread (sWireframe's is the overlay's).
std::shared_ptr< const std::vector< Passthrough::Triangle > > sSet;
uint32_t sSetGeneration = 0;

// Walking in the host's world. The switches are set from the F1 overlay; the rest is the game's
// thread's.
std::atomic< bool > sWalkWanted{false};
std::atomic< bool > sFaceSamus{false};
std::atomic< bool > sHidesRoom{false};
bool sWalking = false;
const CStateManager* sWalkGame = nullptr; // the game she began walking in
TAreaId sWalkArea = kInvalidAreaId;        // the room the host's collision stands in for
CTransform4f sReturnTo = CTransform4f::Identity(); // where she was, to go back to
std::vector< TUniqueId > sFrozen;          // the rooms' objects switched off meanwhile
std::string sWalkMessage;
// The host's collision as a room's: the tree in use, and one being built on its own thread
// (about 13 ms for a full set), from the set and options recorded with it.
struct Built {
  std::unique_ptr< PassthroughCollision::Tree > tree;
  float ms = 0.f;
};
std::unique_ptr< PassthroughCollision::Tree > sTree;
std::future< Built > sBuilding;
uint32_t sBuiltGeneration = 0;
bool sBuiltFacing = false;
Passthrough::Vec3 sBuiltAt{};
uint32_t sTreeGeneration = 0;
uint32_t sBuilds = 0;
float sBuildMs = 0.f;
// A build facing Samus is made again once she's this far from where the last was centred.
constexpr float kRebuildFacing = 1.f;
// Put where the host's player stands, she goes this far above his feet and drops onto them.
constexpr float kAboveFeet = 0.05f;
// The host's player's size is taken within these (HostState::playerRadius, playerHeight).
constexpr float kLeastRadius = 0.15f;
constexpr float kMostRadius = 1.f;
constexpr float kLeastHeight = 0.8f;
constexpr float kMostHeight = 3.f;

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
  if (sWalking) {
    state.flags |= Passthrough::kGuestInHostWorld;
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
      sSet = triangles;
      sSetGeneration = generation;
    }
  }
  const PassthroughView::Vec3 facing = ToVec(player.GetTransform().GetColumn(kDY));
  std::lock_guard lock(sInfoMutex);
  if (triangles) {
    sWireframe.triangles = std::move(triangles);
    sWireframe.generation = generation;
    sWireframe.radius = radius;
  }
  // Walking in the host's world, she is in its space one to one: its triangles stay where they
  // are.
  sWireframe.placement =
      sWalking ? PassthroughView::Placement{}
               : PassthroughView::Place(sHost.playerPosition, sHost.playerYaw, samus.position,
                                        PassthroughView::Yaw(facing));
  sWireframe.camera = {samus.eye, samus.forward, samus.up, samus.fovY, samus.aspect};
  sWireframe.samus = samus.position;
  sHaveWireframe = connected && (sHost.flags & Passthrough::kHostInGame) != 0 &&
                   sWireframe.triangles != nullptr;
}

// A room with no collision: a branch root with no children (CMetroidAreaCollider and the ray
// tests find nothing under it).
const CAreaOctTree& EmptyTree() {
  static uchar sNodes[36] = {};
  static const CAreaOctTree sTree(CAABox(CVector3f::Zero(), CVector3f::Zero()),
                                  CAreaOctTree::Node::kTT_Branch, sNodes, sNodes, 0, nullptr,
                                  nullptr, nullptr, nullptr, 0, nullptr, 0, nullptr, 0, nullptr);
  return sTree;
}

Passthrough::Vec3 Toward(const Passthrough::Vec3& a, const Passthrough::Vec3& b) {
  return {b.x - a.x, b.y - a.y, b.z - a.z};
}
float Distance(const Passthrough::Vec3& a, const Passthrough::Vec3& b) {
  const Passthrough::Vec3 d = Toward(a, b);
  return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

// The size she's made: the host's player's, if it has said and it's believable, else her own.
void SizeFor(float& halfWidth, float& height) {
  const float radius = sHost.playerRadius;
  const float tall = sHost.playerHeight;
  if (sWalking && radius >= kLeastRadius && radius <= kMostRadius && tall >= kLeastHeight &&
      tall <= kMostHeight && tall >= 2.f * radius) {
    halfWidth = radius;
    height = tall;
  } else {
    halfWidth = gpTweakPlayer->GetPlayerXYHalfExtent();
    height = gpTweakPlayer->GetPlayerHeight();
  }
}

// Switches off the objects the rooms' scripts made (by their editor ids) and every enemy, in the
// rooms that are loaded: they stop thinking, being drawn and colliding (an inactive actor leaves
// the game's sorted lists: CStateManager::UpdateActorInSortedLists). Straight through
// SetActive, not the scripts' Deactivate message, which would set off what's connected to them.
// Samus, the cameras and what she makes (her shots) stay on.
void Freeze(CStateManager& mgr) {
  sFrozen.clear();
  const TUniqueId player = mgr.GetPlayer()->GetUniqueId();
  for (CGameArea::CConstChainIterator it = mgr.GetWorld()->GetChainHead(CWorld::kC_Alive);
       it != CWorld::skGlobalEnd; ++it) {
    if (!(*it).IsPostConstructed() || (*it).GetObjectList() == nullptr) {
      continue;
    }
    const CObjectList& objects = *(*it).GetObjectList();
    for (int i = objects.GetFirstObjectIndex(); i != -1; i = objects.GetNextObjectIndex(i)) {
      const CEntity* entity = objects[i];
      if (entity == nullptr || !entity->GetActive() || entity->GetUniqueId() == player ||
          TCastToConstPtr< CGameCamera >(entity)) {
        continue;
      }
      if (entity->GetEditorId() != kInvalidEditorId || TCastToConstPtr< CPatterned >(entity)) {
        sFrozen.push_back(entity->GetUniqueId());
      }
    }
  }
  for (const TUniqueId id : sFrozen) {
    if (CEntity* entity = mgr.ObjectById(id)) {
      entity->SetActive(false);
    }
  }
}

void Thaw(CStateManager& mgr) {
  for (const TUniqueId id : sFrozen) {
    if (CEntity* entity = mgr.ObjectById(id)) {
      entity->SetActive(true);
    }
  }
  sFrozen.clear();
}

void StartWalking(CStateManager& mgr) {
  CPlayer& player = *mgr.Player();
  sReturnTo = player.GetTransform();
  sWalkGame = &mgr;
  sWalkArea = mgr.GetNextAreaId();
  Freeze(mgr);
  sWalking = true;
  sHidesRoom.store(true, std::memory_order_relaxed);
  float halfWidth, height;
  SizeFor(halfWidth, height);
  player.PortSetCollisionSize(halfWidth, height);
  // Where the host's player stands, facing his way (HostState::playerYaw: about +Z, 0 facing
  // +Y).
  const Passthrough::Vec3& at = sHost.playerPosition;
  const CVector3f feet(at.x, at.y, at.z + kAboveFeet);
  const CVector3f forward(-std::sin(sHost.playerYaw), std::cos(sHost.playerYaw), 0.f);
  player.Teleport(CTransform4f::LookAt(feet, feet + forward, CVector3f::Up()), mgr, true);
}

// Back to the rooms: her own size, their objects on again, and she where she was. A new game
// (a load, or her death) has none of those to give back.
void StopWalking(CStateManager& mgr) {
  if (!sWalking) {
    return;
  }
  sWalking = false;
  sHidesRoom.store(false, std::memory_order_relaxed);
  if (sWalkGame == &mgr && mgr.GetPlayer() != nullptr) {
    CPlayer& player = *mgr.Player();
    player.PortSetCollisionSize(gpTweakPlayer->GetPlayerXYHalfExtent(),
                                gpTweakPlayer->GetPlayerHeight());
    Thaw(mgr);
    player.Teleport(sReturnTo, mgr, true);
  }
  sFrozen.clear();
  sWalkGame = nullptr;
  sWalkArea = kInvalidAreaId;
}

// Starts a build when none is running and the tree is out of date: a new set, the facing
// switched, or (facing her) Samus moved on. Takes a finished one.
void UpdateTree(const Passthrough::Vec3& at) {
  if (sBuilding.valid() && sBuilding.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    Built built = sBuilding.get();
    sTree = std::move(built.tree);
    sTreeGeneration = sBuiltGeneration;
    sBuildMs = built.ms;
    ++sBuilds;
  }
  if (sBuilding.valid() || !sSet) {
    return;
  }
  const bool facing = sFaceSamus.load(std::memory_order_relaxed);
  const bool stale = sBuiltGeneration != sSetGeneration || sBuiltFacing != facing ||
                     (facing && Distance(at, sBuiltAt) > kRebuildFacing);
  if (!stale) {
    return;
  }
  sBuiltGeneration = sSetGeneration;
  sBuiltFacing = facing;
  sBuiltAt = at;
  PassthroughCollision::Options options;
  options.nearest = at;
  options.facing = facing ? PassthroughCollision::Facing::TowardSamus
                          : PassthroughCollision::Facing::AsHost;
  sBuilding = std::async(std::launch::async, [set = sSet, options] {
    const Clock::time_point start = Clock::now();
    Built built;
    built.tree = PassthroughCollision::Build(set->data(), set->size(), options);
    built.ms = std::chrono::duration< float, std::milli >(Clock::now() - start).count();
    return built;
  });
}

// Once a tick: walking in the host's world while it's asked for and there's a host in a level
// with its triangles; back in the rooms once not.
void UpdateWalk(CStateManager& mgr, const Passthrough::GuestState& samus, bool connected) {
  if (sWalking && sWalkGame != &mgr) {
    StopWalking(mgr); // a new game: nothing to give back
  }
  const bool hostInLevel = connected && (sHost.flags & Passthrough::kHostInGame) != 0 &&
                           (sHost.flags & Passthrough::kHostLoading) == 0;
  const bool wanted = sWalkWanted.load(std::memory_order_relaxed);
  if (!wanted || !hostInLevel) {
    StopWalking(mgr);
    sWalkMessage = !wanted ? "" : !connected ? "a host" : "the host's player in a level";
    if (!wanted && !sBuilding.valid()) {
      sTree.reset();
      sBuiltGeneration = 0;
    }
    return;
  }
  // The room she's in can change only by its doors, which are off; a respawn changes it.
  if (sWalking && mgr.GetNextAreaId() != sWalkArea) {
    StopWalking(mgr);
  }
  UpdateTree(sWalking ? samus.position : sHost.playerPosition);
  if (sWalking) {
    sWalkMessage.clear();
    return;
  }
  if (!sSet) {
    sWalkMessage = "the host's triangles";
  } else if (!sTree || sTreeGeneration != sSetGeneration) {
    sWalkMessage = "the host's collision to be built";
  } else if (mgr.GetGameState() != CStateManager::kGS_Running ||
             mgr.GetCameraManager()->IsInCinematicCamera() || !mgr.GetPlayerState()->IsAlive()) {
    sWalkMessage = "Samus in play (not paused, in a cutscene or dead)";
  } else {
    StartWalking(mgr);
    sWalkMessage.clear();
  }
}
} // namespace

bool Enabled() { return sEnabled.load(std::memory_order_relaxed); }

void SetEnabled(bool enabled) { sEnabled.store(enabled, std::memory_order_relaxed); }

void Tick(CStateManager& mgr, float dt) {
  if (!Enabled()) {
    sEyeHeight.store(0.f, std::memory_order_relaxed);
    StopWalking(mgr);
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
  UpdateWireframe(block, samus, *mgr.GetPlayer(), connected);
  UpdateWalk(mgr, samus, connected);
  SetInfo(connected ? Status::Connected : Status::Waiting, {}, &sHost,
          mgr.GetPlayer()->GetOwnEyeHeight());
  std::lock_guard lock(sInfoMutex);
  Info& info = sInfo;
  info.walk = !sWalkWanted.load(std::memory_order_relaxed) ? Walk::Off
              : sWalking                                   ? Walk::Walking
                                                           : Walk::Waiting;
  info.walkMessage = sWalkMessage;
  info.ownHalfWidth = gpTweakPlayer->GetPlayerXYHalfExtent();
  info.ownHeight = gpTweakPlayer->GetPlayerHeight();
  const CAABox& box = mgr.GetPlayer()->GetBaseBoundingBox();
  info.halfWidth = 0.5f * (box.GetMaxPoint().GetX() - box.GetMinPoint().GetX());
  info.height = box.GetMaxPoint().GetZ() - box.GetMinPoint().GetZ();
  info.frozen = uint32_t(sFrozen.size());
  info.treeGeneration = sTree ? sTreeGeneration : 0;
  info.builds = sBuilds;
  info.buildMs = sBuildMs;
  info.tree = sTree ? sTree->GetStats() : PassthroughCollision::Stats{};
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

bool WalkInHostWorld() { return sWalkWanted.load(std::memory_order_relaxed); }

void SetWalkInHostWorld(bool on) { sWalkWanted.store(on, std::memory_order_relaxed); }

bool FaceSamus() { return sFaceSamus.load(std::memory_order_relaxed); }

void SetFaceSamus(bool on) { sFaceSamus.store(on, std::memory_order_relaxed); }

const CAreaOctTree& AreaCollision(const CGameArea& area, const CAreaOctTree& own) {
  if (!sWalking || !sTree) {
    return own;
  }
  return area.GetId() == sWalkArea ? sTree->OctTree() : EmptyTree();
}

const CAreaOctTree* HostCollision() { return sWalking && sTree ? &sTree->OctTree() : nullptr; }

bool HidesRoom() { return sHidesRoom.load(std::memory_order_relaxed); }

void GameEnded(const CStateManager& mgr) {
  if (sWalkGame != &mgr) {
    return;
  }
  sWalking = false;
  sHidesRoom.store(false, std::memory_order_relaxed);
  sFrozen.clear();
  sWalkGame = nullptr;
  sWalkArea = kInvalidAreaId;
}

void Shutdown() {
  sEyeHeight.store(0.f, std::memory_order_relaxed);
  if (sBuilding.valid()) {
    sBuilding.wait();
    sBuilding = {};
  }
  sTree.reset();
  sBuiltGeneration = 0;
  sSet.reset();
  sSetGeneration = 0;
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
