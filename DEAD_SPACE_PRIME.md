# Dead Space Prime

This branch (`dead-space-prime`) of the Metroid Prime native port adds Dead
Space mechanics to Metroid Prime, keeping Prime's own controls and HUD. Each
addition is off by default, so with nothing switched on the game plays as the
port does.

Everything else about the port, including building, running and its other
options, is in [README.md](README.md) and [docs/NATIVE_PORT.md](docs/NATIVE_PORT.md).
Windows build steps are also in the
[Dead Space Prime repository](https://github.com/edboi18-ctrl/Dead-Space-Prime-/blob/main/docs/RESEARCH.md#build-the-metroid-prime-port-on-windows).

## Stasis

From Dead Space: slow an enemy down for a few seconds.

**Turn it on:** F1 > Game > Dead Space > **Stasis**.

**Use it:** lock on to an enemy (L, or the right mouse button under mouse aim)
and press **Stasis**. Without a lock, Stasis takes the enemy your shots would
home in on.

| Input | Default |
|---|---|
| Keyboard | **R** |
| Controller | **LB**, or **L3** when LB already does something |

A default is only added on an input nothing else uses, and not at all once
you bind Stasis yourself under F1 > Controls > Remap. It works in normal play
and with the scan visor, not in morph ball, menus or the map.

**What it does:**

- The enemy runs at 15% speed for 5 seconds, then eases back to full speed over
  the last 0.75 seconds. Its animation, AI and timers slow down, and so do its
  movement and its fall: both its thinking and its physics step are scaled.
- It glows blue while held, through the same additive colour the game uses to
  flash an enemy when it is hit.
- Each use spends half of the **STASIS** meter. The meter refills by itself
  in 20 seconds from empty, as Dead Space 2's does; Dead Space 1 refilled it
  only with packs and stations, which Prime doesn't have. A press with no enemy
  to hold, or without enough meter, flashes the meter red and costs nothing.
- The meter is part of the helmet HUD: a thin blue strip, notched between its
  two uses, just under the energy bar (inside its arc in the X-ray visor), with
  **STASIS** written past the bar's right end. Both are drawn with the energy
  bar, so they sway with the helmet, fade with the HUD, follow its scale and
  widescreen layout, and go away in morph ball, cutscenes and menus as the
  bar does. The **Energy Low** warning, which the game places just under the
  bar, moves down by the strip's height while the meter shows, so it keeps
  its usual spacing below it.
- Up to 8 enemies can be held at once. Holding a held enemy again starts its
  time over.

**Tuning:** F1 > Game > Dead Space has sliders for the speed (5-50% of normal)
and the duration (1-15 s). They are saved in `port_settings.ini`:

```ini
stasis=1
stasis_slow=0.15
stasis_duration=5
```

The 15% and 5 s defaults are chosen for feel. They are not measured from
Dead Space.

**Original experience** turns Stasis off along with the port's other gameplay
extras.

### Checked in game, and not yet

Played on a USA v1.00 disc: Stasis holds and slows enemies. That first build
drew the meter flat on the screen, outside the helmet, where it covered the
countdown during the escape from the frigate; it is now drawn in the helmet
HUD as above. In the combat visor the meter and label sit well, but the
**Energy Low** warning covered the strip, so the warning now moves down while
the meter shows. Still to be checked in play:

- that the warning clears the strip, and where the meter and its label land
  in the scan, thermal and X-ray visors. They hang off the energy bar, whose
  place comes from your disc. Four constants at the top of the meter code in
  `src/GuiSys/CAuiEnergyBarT01.cpp` set the spacing:
  `kStasisGap` (bar to strip), `kStasisThickness` (strip),
  `kStasisLabelHeight` and `kStasisLabelGap` (label size, and its distance
  past the strip), all in bar thicknesses;
- how strong the blue glow looks;
- that enemies with their own render code (some bosses) glow at all; slowing
  doesn't depend on it;
- how bosses behave when held. Their scripted phases run on the same timers,
  so they should simply take longer, but that is unverified.

A HUD mod that gives the energy bar its own shape hides the meter, since
there is no curve to follow.

The sounds are the HUD's free-look blips (on for a hold, off for a refused
press), because they are always loaded during play.

## Where the code is

| File | What it does |
|---|---|
| `platform/include/port_stasis.h`, `platform/port_stasis.cpp` | The arithmetic: meter, holds, ease-out. No game code, so `tests/port_stasis.cpp` tests it alone. |
| `platform/include/port_dead_space.h`, `platform/port_dead_space.cpp` | Ties it to the game: picks the target, scales the held enemy's time step, its glow, what the meter shows, and its label's text. |
| `platform/include/port_input_engine.h` | The `stasis` action in the binding engine. |
| `platform/input/port_input_devices.cpp` | Routes the action and adds the default bindings. |
| `platform/include/port_input_settings.h`, `platform/port_input_settings.cpp` | The three settings and their `port_settings.ini` lines (covered by `tests/port_input_settings.cpp`). |
| `platform/include/port_debug.h`, `platform/debug_ui.cpp` | Getters, setters and the F1 > Game > Dead Space section. |
| `src/MetroidPrime/CStateManager.cpp` | Runs Stasis each tick and scales a held enemy's `PreThink`, `Think` and collision move. |
| `src/MetroidPrime/Enemies/CPatterned.cpp` | The glow, in `PreRender`. |
| `src/GuiSys/CAuiEnergyBarT01.cpp`, `include/GuiSys/CAuiEnergyBarT01.hpp` | Draws the meter and its label along the energy bar, inside the bar's own draw. |
| `src/MetroidPrime/HUD/CHudEnergyInterface.cpp` | Turns the meter on for Samus's energy bar in every visor but morph ball, and moves the low-energy warning below it. |
| `src/GuiSys/CGuiTextPane.cpp`, `include/GuiSys/CGuiTextPane.hpp` | A draw-time offset for a text pane, which that move uses; the frame's own layout is left as loaded. |

The new files under `platform/` and `tests/` are this fork's own work, the part
the port's `LICENSE` covers. The edits under `src/` are small hooks in the
decompiled game code, which stays Nintendo's (see `NOTICE`).
