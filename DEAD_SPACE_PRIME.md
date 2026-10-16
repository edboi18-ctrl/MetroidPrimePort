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

- Samus throws a Stasis charge with her left hand: the hand comes up, thrusts
  toward the enemy and drops away again. These are the Grapple Beam arm's own
  animations (raise, fire, lower), played 1.75 times as fast, so the grapple
  gear shows on the hand once you have the Grapple Beam. The charge leaves the
  palm at the moment the grapple beam would fire: a pulsing ball of blue light
  with a short tail, flying at 30 m/s and homing in on the enemy. It is drawn
  into the world, so walls hide it, but it doesn't collide with them on the
  way. Where it lands, a ring of blue light swells out and fades.
- The hold starts when the charge arrives, not when you press. The meter is
  paid at the throw, so a charge whose enemy dies or disappears on the way
  fizzles out and is spent, as in Dead Space. While the arm is busy (being
  hit, a charge combo, an idle animation, free look, a second throw before the
  first is done) the charge leaves at once from the bottom left of the view
  instead.
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

**Sounds from Dead Space:** the throw and the hit can play Dead Space's own
Stasis sounds, from WAV files you take from your own copy of the game. None
come with the port: they are EA's, so they can't be shared, and they stay on
your PC.

1. Next to `metroid_prime_port.exe` there is a `user` folder (it holds
   `port_settings.ini`). Make a folder `dead_space` inside it.
2. Put the two sounds there as `stasis_fire.wav` (the charge leaving the hand)
   and `stasis_hit.wav` (the charge catching its enemy). Any WAV that SDL reads
   works: PCM or ADPCM, any sample rate, mono or stereo.
3. F1 > Game > Dead Space shows each as found or none. A file added while the
   game runs is picked up within a couple of seconds.

Dead Space keeps its audio in EA's own formats. [vgmstream](https://github.com/vgmstream/vgmstream)
reads many of them ([its format list](https://github.com/vgmstream/vgmstream/blob/master/doc/FORMATS.md)
has EA's SCHl, SNR/SNS, SPS, ABK and MPF/MUS containers) and converts to WAV
from the command line, as in its [usage notes](https://github.com/vgmstream/vgmstream/blob/master/doc/USAGE.md):
`vgmstream-cli -o stasis_fire.wav <file>`. Which of your copy's files holds the
Stasis sounds depends on the version of Dead Space and isn't covered here; an
editor such as Audacity can cut the right part out to its own WAV.

They play at the game's sound effect volume (Options > Sound), and the hit is
quieter the farther away it lands. Without a file, the throw keeps the grapple
arm's own sound and the hit keeps the HUD blip; with a fire sound, the grapple
arm's is left out.

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

- the throw, which was built without seeing it: how the hand looks and how
  quickly it moves (`kPortThrowSpeed` in
  `src/MetroidPrime/Player/CGrappleArm.cpp`), that the charge leaves from the
  palm, and the charge's size, colour, speed and burst (the constants after
  `kThrowSpeed` in `platform/port_dead_space.cpp`);
- where the meter and its label land in the scan, thermal and X-ray visors
  (the combat visor's are checked, and the Energy Low warning clears them).
  They hang off the energy bar, whose place comes from your disc. Four
  constants at the top of the meter code in `src/GuiSys/CAuiEnergyBarT01.cpp`
  set the spacing:
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

## Manual aim

For keyboard and mouse: aim by hand, as in Dead Space, instead of locking on.

**Turn it on:** F1 > Game > Dead Space > **Manual aim**. It is meant for mouse
aim (F1 > Controls), where the crosshair follows the mouse.

- Lock-on reaches only what the Scan Visor scans and grapple points. In the
  combat, thermal and X-ray visors enemies can't be locked onto, and an empty
  lock-on press does nothing (it would otherwise hold a point to strafe
  around). A scan lock on an enemy lets go when you leave the Scan Visor.
- Shots go where the crosshair is: the game's aim assist, which turns shots
  toward a nearby enemy even without a lock-on, is off.
- Missiles, Super Missiles and Wave Beam shots (the Wavebuster included) still
  home in, on the targetable thing nearest the crosshair by angle: within 15
  degrees of it and 60 m, in sight, picked as each is fired (and kept up to
  date while the Wavebuster fires). Locked on in the Scan Visor, they home on
  the lock as before. The Ice Spreader, the Flamethrower and the other beams
  don't seek. `kSeekAngle` and `kSeekRange` in `platform/port_dead_space.cpp`
  set the cone.
- Stasis is thrown along the crosshair at the nearest enemy it points at, before
  any wall, allowing 0.5 m beside its body. Pointed at none, the charge flies to
  where the crosshair is (the first wall, or 60 m out) and is spent, as in Dead
  Space; an enemy it passes through on the way still catches it.

Prime's enemies and bosses were designed around lock-on, so some fights will
need playtesting and changes to be fair this way; Stasis may help with some.

## Double-tap dash

Retail's dash needs a lock-on: jump while strafing around the locked target.
With manual aim there is no lock-on, so this dashes on a double-tapped movement
key instead.

**Turn it on:** F1 > Game > Dead Space > **Double-tap dash**.

- Tap a movement key twice within 0.25 s (the keys bound to the stick
  directions, W, A, S and D by default) and Samus dashes that way: forward,
  back, left or right, relative to where she faces. Holding a key never
  dashes, and a key of another direction pressed in between starts over.
- Keyboard only: a stick flicked twice doesn't dash.
- The dash is a straight burst, 26 m/s easing to 10 m/s over 0.28 s (about
  5 m), slower in water and the like as walking is, with retail's dash sound
  and rumble. A wall ends it. You can jump out of it, dash once in the air per
  jump, and dash again 0.35 s after the last. Locked on, retail's dash works as
  before.
- These numbers are first guesses, to be tuned in play: `kPortDashTime`,
  `kPortDashStartSpeed`, `kPortDashEndSpeed` and `kPortDashCooldown` in
  `src/MetroidPrime/Player/CPlayerDynamics.cpp`, and `kWindowMs` in
  `platform/include/port_double_tap.h`.

Both are off by default and under **Original experience**, and saved in
`port_settings.ini` as `manual_aim` and `double_tap_dash`.

## Where the code is

| File | What it does |
|---|---|
| `platform/include/port_stasis.h`, `platform/port_stasis.cpp` | The arithmetic: meter, throws and holds, ease-out, the charge's flight step. No game code, so `tests/port_stasis.cpp` tests it alone. |
| `platform/include/port_dead_space.h`, `platform/port_dead_space.cpp` | Ties it to the game: picks the target, flies and draws the charge and its burst, scales the held enemy's time step, its glow, what the meter shows, and its label's text. |
| `platform/include/port_dead_space_sound.h`, `platform/port_dead_space_sound.cpp` | Plays your own WAV files for the throw and the hit through an SDL audio stream of their own (covered by `tests/port_dead_space_sound.cpp`). |
| `platform/include/port_double_tap.h`, `platform/port_double_tap.cpp` | Spots a movement key double-tapped (covered by `tests/port_double_tap.cpp`). |
| `platform/include/port_input_engine.h` | The `stasis` action in the binding engine. |
| `platform/input/port_input_devices.cpp` | Routes the Stasis action and adds its default bindings, and feeds the keyboard's movement keys to the double-tap check. |
| `platform/include/port_input_settings.h`, `platform/port_input_settings.cpp` | The settings and their `port_settings.ini` lines (covered by `tests/port_input_settings.cpp`). |
| `platform/include/port_debug.h`, `platform/debug_ui.cpp` | Getters, setters and the F1 > Game > Dead Space section. |
| `src/MetroidPrime/CStateManager.cpp` | Runs Stasis each tick, scales a held enemy's `PreThink`, `Think` and collision move, and draws the charges after the rooms. |
| `src/MetroidPrime/Player/CGrappleArm.cpp`, `include/MetroidPrime/Player/CGrappleArm.hpp` | The throw: the grapple animations in order, and the charge let go at the thrust's projectile event. |
| `src/MetroidPrime/Player/CPlayerOrbit.cpp` | Points the throwing hand at the enemy; manual aim's limits on lock-on and the aim assist. |
| `src/MetroidPrime/Player/CPlayerDynamics.cpp`, `src/MetroidPrime/Player/CPlayer.cpp`, `include/MetroidPrime/Player/CPlayer.hpp` | The double-tap dash. |
| `src/MetroidPrime/Player/CPlayerGun.cpp` | Draws the throwing hand, and keeps the suit's right arm drawn while it throws; under manual aim, has missiles, Super Missiles and the Wave Beam seek the target nearest the crosshair. |
| `src/MetroidPrime/Enemies/CPatterned.cpp` | The glow, in `PreRender`. |
| `src/GuiSys/CAuiEnergyBarT01.cpp`, `include/GuiSys/CAuiEnergyBarT01.hpp` | Draws the meter and its label along the energy bar, inside the bar's own draw. |
| `src/MetroidPrime/HUD/CHudEnergyInterface.cpp` | Turns the meter on for Samus's energy bar in every visor but morph ball, and moves the low-energy warning below it. |
| `src/GuiSys/CGuiTextPane.cpp`, `include/GuiSys/CGuiTextPane.hpp` | A draw-time offset for a text pane, which that move uses; the frame's own layout is left as loaded. |

The new files under `platform/` and `tests/` are this fork's own work, the part
the port's `LICENSE` covers. The edits under `src/` are small hooks in the
decompiled game code, which stays Nintendo's (see `NOTICE`).
