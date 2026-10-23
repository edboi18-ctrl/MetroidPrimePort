#pragma once

// Dead Space Prime's passthrough bridge, the Metroid Prime side (docs/PASSTHROUGH.md in the Dead
// Space Prime repository). While it's on, Samus's state goes into the shared block
// (passthrough_protocol.h) once a game tick, for a plugin in Dead Space to read, and the
// plugin's state is read back, with Dead Space's collision near Isaac, which the F1 overlay can
// draw as a wireframe around Samus. Nothing from the host acts on the game yet.
// Off at each start; F1 > Debug > Passthrough turns it on.

#include "passthrough_protocol.h"
#include "passthrough_view.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CStateManager;

namespace PortPassthrough {

bool Enabled();
void SetEnabled(bool enabled);

// Once a tick from CStateManager::Update, after Samus and the camera have moved.
void Tick(const CStateManager& mgr, float dt);

enum class Status {
  Off,       // the bridge is off
  Error,     // the shared block couldn't be opened (see Info::message)
  Waiting,   // published; no host has written in the last second
  Connected, // a host is writing
};

struct Info {
  Status status = Status::Off;
  std::string message;
  uint32_t guestTick = 0;
  Passthrough::HostState host{};
};
Info GetInfo();

// The host's collision near its player, for the overlay's wireframe: the latest triangle set (in
// wire space), where to put it so the host's player stands where Samus does
// (passthrough_view.h), and the camera as of the last tick. False while there's none, or no host.
struct Wireframe {
  std::shared_ptr< const std::vector< Passthrough::Triangle > > triangles;
  uint32_t generation = 0;
  float radius = 0.f;
  PassthroughView::Placement placement;
  PassthroughView::Camera camera{};
};
bool GetWireframe(Wireframe& out);

// Whether the overlay draws it (F1 > Debug > Passthrough; off at each start).
bool WireframeShown();
void SetWireframeShown(bool shown);

// Leaves the shared block. On the game's thread; Tick calls it once the bridge is turned off.
void Shutdown();

} // namespace PortPassthrough
