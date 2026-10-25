#pragma once

// Dead Space Prime's passthrough bridge, the Metroid Prime side (docs/PASSTHROUGH.md in the Dead
// Space Prime repository). While it's on, Samus's state goes into the shared block
// (passthrough_protocol.h) once a game tick, for a plugin in Dead Space to read, and the
// plugin's state is read back, with Dead Space's collision near Isaac, which the F1 overlay can
// draw as a wireframe around Samus, and which Samus can walk in (WalkInHostWorld).
// Off at each start; F1 > Debug > Passthrough turns it on.

#include "passthrough_protocol.h"
#include "passthrough_view.h"

#include "passthrough_collision.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CAreaOctTree;
class CGameArea;
class CStateManager;

namespace PortPassthrough {

bool Enabled();
void SetEnabled(bool enabled);

// Once a tick from CStateManager::Update, after Samus and the camera have moved.
void Tick(CStateManager& mgr, float dt);

enum class Status {
  Off,       // the bridge is off
  Error,     // the shared block couldn't be opened (see Info::message)
  Waiting,   // published; no host has written in the last second
  Connected, // a host is writing
};

// Walking in the host's world (WalkInHostWorld).
enum class Walk {
  Off,     // not asked for
  Waiting, // asked for; waiting for what Info::walkMessage says
  Walking, // Samus walks in the host's world
};

struct Info {
  Status status = Status::Off;
  std::string message;
  uint32_t guestTick = 0;
  Passthrough::HostState host{};
  float ownEyeHeight = 0.f; // Samus's own eyes above her feet, as the game has them
  Walk walk = Walk::Off;
  std::string walkMessage;
  float ownHalfWidth = 0.f;  // Samus's own collision box: half its width, and its height
  float ownHeight = 0.f;
  float halfWidth = 0.f;     // and as it is now
  float height = 0.f;
  float ownBallRadius = 0.f; // her morph ball's radius, her own and as it is now
  float ballRadius = 0.f;
  uint32_t touching = 0;     // the host's body of the wall she's up against (0: none)
  uint32_t frozen = 0;       // the rooms' objects switched off while she walks there
  uint32_t treeGeneration = 0; // the triangle set the collision was last built from
  uint32_t builds = 0;
  float buildMs = 0.f;       // how long the last build took (on its own thread)
  PassthroughCollision::Stats tree{};
};
Info GetInfo();

// While a host is connected with its player in a level, the height above his feet where it puts
// its first-person camera (HostState::playerEyeHeight); else 0. CPlayer::GetEyeHeight puts
// Samus's eyes there instead of her own, so both games look from the same height above the same
// feet. On the game's thread.
float EyeHeight();

// The host's collision near its player, for the overlay's wireframe: the latest triangle set (in
// wire space), where to put it so the host's player stands where Samus does
// (passthrough_view.h; where it is, while she walks in the host's world), and the camera as of
// the last tick. False while there's none, or no host.
struct Wireframe {
  std::shared_ptr< const std::vector< Passthrough::Triangle > > triangles;
  uint32_t generation = 0;
  float radius = 0.f;
  PassthroughView::Placement placement;
  PassthroughView::Camera camera{};
  PassthroughView::Vec3 samus{}; // her feet, in the guest's world
};
bool GetWireframe(Wireframe& out);

// Whether the overlay draws it (F1 > Debug > Passthrough; off at each start).
bool WireframeShown();
void SetWireframeShown(bool shown);

// Walking in the host's world (F1 > Debug > Passthrough; off at each start). While it's on,
// the host is in a level and its triangles have come: the rooms' objects are switched off and
// the rooms aren't drawn (HidesRoom); the host's collision near its player, built into a
// room's collision tree (passthrough_collision.h), stands in for the rooms' (AreaCollision);
// Samus goes to where its player stands, facing his way, made his size; and the guest tells
// the host (kGuestInHostWorld), so it keeps its player where she is. Turned off, or with the
// host gone, the rooms come back and so does she, to where she was.
bool WalkInHostWorld();
void SetWalkInHostWorld(bool on);
// Builds turn the host's triangles to face Samus (PassthroughCollision::Facing::TowardSamus)
// instead of keeping the host's winding. Off at each start.
bool FaceSamus();
void SetFaceSamus(bool on);

// The collision tree the game uses for a room (CGameArea::GetOctTree): the room's own, or
// while Samus walks in the host's world, the host's for the room she's in and an empty one for
// the others. On the game's thread.
const CAreaOctTree& AreaCollision(const CGameArea& area, const CAreaOctTree& own);
// The host's tree while Samus walks in its world, else null. On the game's thread.
const CAreaOctTree* HostCollision();

// Whether the rooms are left undrawn (while Samus walks in the host's world).
bool HidesRoom();

// From CStateManager's destructor: forgets a walk in the host's world that began in that game,
// as the rooms and Samus it would give back are gone.
void GameEnded(const CStateManager& mgr);

// Leaves the shared block. On the game's thread; Tick calls it once the bridge is turned off.
void Shutdown();

} // namespace PortPassthrough
