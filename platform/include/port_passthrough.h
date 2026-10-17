#pragma once

// Dead Space Prime's passthrough bridge, the Metroid Prime side (docs/PASSTHROUGH.md in the Dead
// Space Prime repository). While it's on, Samus's state goes into the shared block
// (passthrough_protocol.h) once a game tick, for a plugin in Dead Space to read, and the
// plugin's state is read back. So far that's all: nothing from the host acts on the game yet.
// Off at each start; F1 > Debug > Passthrough turns it on.

#include "passthrough_protocol.h"

#include <cstdint>
#include <string>

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

// Leaves the shared block. On the game's thread; Tick calls it once the bridge is turned off.
void Shutdown();

} // namespace PortPassthrough
