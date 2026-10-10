#pragma once

// Dead Space Prime: the Dead Space mechanics this fork adds to Metroid Prime.
// So far Stasis: PortStasis keeps the arithmetic, and this ties it to the game -
// the enemy under lock-on, its time step while held, its blue glow, and the
// meter at the bottom of the screen. Everything is off unless
// PortDebug::Stasis() is on.

class CColor;
class CStateManager;
struct TUniqueId;

namespace PortDeadSpace {

// Once per running tick from CStateManager::Update, before anything thinks or
// moves: takes a pending Stasis press, runs the holds down, and lets go of
// enemies that died, were unloaded or deactivated.
void Tick(CStateManager& mgr, float dt);
// The time step multiplier for `id` this tick: 1 unless Stasis holds it.
// CStateManager scales the entity's PreThink, Think and collision move by it.
float TimeScale(TUniqueId id);
// A held enemy's glow, merged into `color` channel by channel. CPatterned's
// PreRender draws a non-black color through the damage flash's blend mode, which
// adds it to the lit model; anything not held is left alone.
void Tint(TUniqueId id, CColor& color);
// Lets go of everything (a save state loading).
void Reset();
// The meter. Call after the frame's IOWins, as PortSpeedrunTimer::Draw is.
void DrawHud();

} // namespace PortDeadSpace
