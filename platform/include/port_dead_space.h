#pragma once

// Dead Space Prime: the Dead Space mechanics this fork adds to Metroid Prime.
// So far Stasis: PortStasis keeps the arithmetic, and this ties it to the game -
// the enemy under lock-on, its time step while held, its blue glow, and the
// meter in the helmet HUD. Everything is off unless PortDebug::Stasis() is on.

class CColor;
class CStateManager;
class CTransform4f;
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

// The meter. CAuiEnergyBarT01 draws it along the player's energy bar, so it is part
// of the helmet HUD: it moves, fades and scales with it, in every visor but morph
// ball. These are what it shows.
bool StasisMeterVisible();
// The meter's charge, 0..1.
float StasisMeterFraction();
// The share of the meter one use costs: the strip is drawn in segments that long.
float StasisMeterStep();
// 1 just after a press that held nothing, fading to 0.
float StasisRefusedFlash();
const CColor& StasisMeterColor();
const CColor& StasisRefusedColor();
// "STASIS" in the HUD font: `topLeft` places the line's top-left corner, in the
// space the caller is drawing in, and `height` is its height in that space.
void DrawStasisLabel(const CTransform4f& topLeft, float height, const CColor& color);

} // namespace PortDeadSpace
