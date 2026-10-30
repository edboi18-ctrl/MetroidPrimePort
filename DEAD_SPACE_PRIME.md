# Dead Space Prime

This branch (`dead-space-prime`) of the Metroid Prime native port adds Dead
Space mechanics to Metroid Prime, keeping Prime's own controls and HUD. Each
addition is off by default except a slightly faster walk (see
[Walk speed](#walk-speed)), the gun's sway (see [Gun sway](#gun-sway)) and
faster Power Beam shots (see [Power Beam speed](#power-beam-speed)), and
**Original experience** turns those off too.

Everything else about the port, including building, running and its other
options, is in [README.md](README.md) and [docs/NATIVE_PORT.md](docs/NATIVE_PORT.md).
Windows build steps are also in the
[Dead Space Prime repository](https://github.com/edboi18-ctrl/Dead-Space-Prime-/blob/main/docs/RESEARCH.md#build-the-metroid-prime-port-on-windows).

## Stasis

From Dead Space: slow an enemy down for a few seconds.

**Turn it on:** F1 > Controls > Options > Dead Space > **Stasis**.

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
3. F1 > Controls > Options > Dead Space shows each as found or none. A file added while the
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

**Tuning:** F1 > Controls > Options > Dead Space has sliders for the speed (5-50% of normal)
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

**Turn it on:** F1 > Controls > Options > Dead Space > **Manual aim**. It is meant for mouse
aim (F1 > Controls > Mouse settings), where the crosshair follows the mouse.

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
  don't seek, and nor do the extra shots **Stack beams** (F1 > Game > Cutscenes) fires
  with each one. `kSeekAngle` and `kSeekRange` in `platform/port_dead_space.cpp`
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

**Turn it on:** F1 > Controls > Options > Dead Space > **Double-tap dash**.

- Tap a movement key twice within 0.25 s (the keys bound to the stick
  directions, W, A, S and D by default) and Samus dashes that way: forward,
  back, left or right, relative to where she faces. Holding a key never
  dashes, and a key of another direction pressed in between starts over.
- Keyboard only: a stick flicked twice doesn't dash.
- The dash is a straight burst lasting 0.3 s. At the default **Dash speed**
  of 130% it goes from 39 m/s easing to 18.2 m/s, about 8.7 m. It is slower in
  water and the like as walking is, but never slower than Samus walks there.
  It has retail's dash sound and rumble. A wall ends it. You can jump out of
  it, dash once in the air per jump, and dash again 0.35 s after the last.
  Locked on, retail's dash works as before.
- **Dash speed** (F1 > Controls > Options > Dead Space, 75-150%, 130% by default) scales
  both speeds, and so the distance, since the dash always lasts 0.3 s. At 100%
  it goes from 30 to 14 m/s, about 6.7 m; 75% covers about 5.1 m and 150%
  about 10.1 m. The first version was 26 to 10 m/s over 0.28 s, about 5.2 m;
  130% is what felt right in play.
- The rest are constants, to be tuned in play: `kPortDashTime`,
  `kPortDashStartSpeed`, `kPortDashEndSpeed` and `kPortDashCooldown` in
  `src/MetroidPrime/Player/CPlayerDynamics.cpp`, and `kWindowMs` in
  `platform/include/port_double_tap.h`.

Both are off by default and under **Original experience**, and saved in
`port_settings.ini` as `manual_aim`, `double_tap_dash` and `dash_speed`.

The distances are per tick at 60 Hz, as `PortUpdateDash` moves Samus: 18
ticks of 1/60 s, each at that tick's eased speed. At 120 or 144 Hz they come
out within 0.1 m of that.

## Walk speed

Samus walks a little faster: **Walk speed** (F1 > Game > Cutscenes) is 115% of
the original game's by default, from 50% to 200%, saved as `walk_speed` in
percent. **Original experience** walks at 100%.

- It's the port's own walk speed, which upstream added in v0.33.0
  (`WalkScale` in `src/MetroidPrime/Player/CPlayerDynamics.cpp`). This branch
  only makes 115% its default: `kWalkSpeedDefault` in
  `platform/include/port_debug.h`, with `kWalkSpeedRetail` for **Original
  experience**. Before the merge of v0.36.0 the branch had a Walk speed of its
  own (100-150%, in the Dead Space section), which this replaces.
- It scales the top speed, the acceleration and the friction of walking and
  strafing together: on every surface (water, lava and the like stay slower
  than dry ground), in the air, under direct aim and Remastered movement, and
  for the orbit strafe. Jumps, dashes, turning, the grapple and the morph ball
  are unchanged.
- Samus reaches the new speed in the same time as she reaches the original
  one, and her steady speed is exactly 115% of retail's. That speed is
  `CPlayer::GetActualFirstPersonMaxVelocity`: V - f·m·V/(a·dt) + f for the
  top speed V, friction f and acceleration a, so scaling all three by k
  scales it by k. (The branch's own version left friction alone, which came
  out a little more than 15% faster.)
- A `walk_speed` the earlier version saved (a multiple, such as `1.15`) isn't
  a percent the setting takes, so it's ignored and 115% applies. Set it again
  in F1 if you had changed it.

## Gun sway

As in Metroid Prime Hunters, the arm cannon leans the way you turn, and the
crosshair leans with it: shots still go to the crosshair. **Gun sway** (F1 >
Controls > Options > Dead Space) is the most it leans, 3 degrees by default, from 0 (off) to
10, saved as `gun_sway`. **Original experience** turns it off.

- It leans left, right, up or down, toward the turn, in proportion to how fast
  the view turns: all the way at 180 degrees a second or faster. When the turn
  stops it settles back to centre.
- It follows the turn smoothly, closing 63% of the gap every 0.08 s, at any
  tick rate. A view that jumps more than about 29 degrees in one tick (a cut
  or a teleport) doesn't move it, and morph ball, cinematics and level
  cameras reset it.
- The gun turns about its own origin. Its aim turns with it, so shots,
  missiles and their seeking (manual aim) and the Stasis throw all go along
  the leaned crosshair.
- For scale: on a screen showing 90 degrees across, a 3 degree lean moves the
  crosshair about 2.6% of the screen's width from centre (tan 3 degrees =
  0.052, over the 2 x tan 45 degrees = 2 the screen spans).
- `kFullTurnRate`, `kFollowTime` and `kCutAngle` are in
  `platform/include/port_gun_sway.h`.

## Power Beam speed

Power Beam shots, plain and charged, fly faster: **Power Beam speed** (F1 >
Controls > Options > Dead Space) is 150% of the original game's by default, from 100% to
300%, saved as `power_beam_speed`. **Original experience** fires at 100%.

- The speed is the shot's own, from its effect file on the disc (the WPSC's
  `IVEC`, a distance per 60 Hz frame); `CGunWeapon::Fire` multiplies it as
  the shot leaves the gun. Homing turns a shot without changing its speed, so
  it stays faster all the way.
- It reaches as far as before. A shot moves once a frame for its lifetime in
  frames (the WPSC's `PSLT`), so its lifetime is divided by the same factor,
  rounded to whole frames: a shot that fades out after a distance fades out
  at that distance, sooner. A shot with no lifetime still has none.
- Hits are checked along the whole path from the shot's last position to its
  new one, so a faster shot can't skip through a wall or an enemy.
- Lock-on leads its target for the new speed (`CPlayerGun::GetBeamVelocity`).
- With **Stack beams** (F1 > Game > Cutscenes) on, the Power Beam shot it adds to another
  beam's is faster too: both come out of `CGunWeapon::SpawnProjectile`.
- The other beams, missiles and the Super Missile are unchanged.

## Passthrough bridge (in progress)

The start of a passthrough mod: Dead Space (2008, PC) running at the same time
as this port, Dead Space supplying the world, its scripting and enemies, and
the port supplying Samus. The plan, the division of work between the two games
and the wire format are in
[docs/PASSTHROUGH.md](https://github.com/edboi18-ctrl/Dead-Space-Prime-/blob/ccr-03a138c3-7l85hc/docs/PASSTHROUGH.md)
in the Dead Space Prime repository. What the port has so far:

- **F1 > Debug > Passthrough bridge** (off at each start) shares Samus's state
  once a tick through a block of named shared memory: her position and
  velocity, the camera's position, axes and field of view, energy, visor,
  beam, morph state and a few flags. A host's state comes back the same way,
  and the F1 line shows whether one is connected.
- **Bridge version 2:** the host also sends its collision near its player
  (Dead Space's triangles within 20 m of Isaac, up to 16,384, in the port's
  coordinates), and **Draw the host's collision**, under the bridge's switch,
  draws them over the picture as a wireframe around Samus, as if Isaac stood
  where she does, facing her way: blue for the level, orange for what moves
  (doors, lifts). The F1 line under it counts them. Off at each start.
- `passthrough_probe.exe`, next to the game in the Windows package, stands in
  for the Dead Space plugin: run it in a console while the bridge is on and
  it prints Samus's state twice a second, and F1 shows it as connected. With
  `--room` it also sends a made-up room (a floor 20 m square, a wall 6 m
  ahead with a doorway, and an orange door in it) for the wireframe, with its
  player's size Isaac's, for walking in it.
- **Eye height (still bridge version 2):** the host also says how high above
  its player's feet it puts its first-person camera (Dead Space: 1.65 m
  above Isaac's feet; a host that doesn't say leaves Samus's own), and
  while it's connected with its player in a level, Samus's eyes go there
  instead of her own height (`CPlayer::GetEyeHeight` and
  `GetUnbiasedEyeHeight`), so both games look from the same height above the
  same feet; her gun comes down with them. The F1 line shows both heights.
- **Walk in the host's world** (under the wireframe's switch; off at each
  start): Samus walks in Dead Space's world, colliding with its collision.
  Once the host is in a level and its triangles have come:
  - the triangles (the level's, and those of doors and lifts, which the
    plugin sends again within a tenth of a second of their moving) are built
    into a room collision tree, as a room's collision is (`CAreaOctTree`, in the
    big-endian layout the game reads; `platform/passthrough_collision.cpp`,
    on a thread of its own, about 13 ms for a full set), within the limits of
    the lists `CMetroidAreaCollider` marks tested vertices, edges and
    triangles in (10,240, 24,576 and 16,384, unchecked by the game), the
    nearest first, and again for each new set the host sends;
  - while she walks there, the room she's in collides through that tree, and
    the other loaded rooms through an empty one (`CGameArea::GetOctTree`, and
    `CGameCollision::BuildAreaCollisionCache`, which now goes through it), so
    walking, the morph ball, the Remastered movers, shots and rays all use it;
    the collision failsafe searches within the tree's bounds;
  - the rooms' objects (what their scripts made, and every enemy) are switched
    off, so they stop thinking, colliding and drawing, and the rooms aren't
    drawn (as the collision view's "only" mode leaves them);
  - she goes to where the host's player stands, facing his way, in its space
    one to one, made his size (`CPlayer::PortSetCollisionSize`; Isaac's
    capsule is 0.38 m in radius and 1.80 m tall in build 45's run), and the
    bridge tells the host (`kGuestInHostWorld`) so it keeps its player where
    she is. Standing, her collision is a box that doesn't turn with her
    (`CCollidableAABox`), so it's the square that fits inside his circle,
    0.54 m across (radius times the square root of 2): build 49's box, as
    wide as his capsule, was 1.07 m from corner to corner and stuck in a
    passage he walks through. It touches nothing his capsule doesn't, and
    face on it lets her 0.11 m nearer a wall than he gets, which Dead Space
    keeps him back from. Her morph ball is his radius too, 0.76 m across
    (`CTweakPlayer::GetPlayerBallHalfExtent`, which everything that sizes the
    ball reads, gives his while she walks there, and `CMorphBall::PortSetRadius`
    remakes its sphere and scales its models, the glow inside it, and how far
    out its speed trails run);
  - each tick she walks there, the port finds the wall or ceiling her box
    touches (grown 3 cm along the ground, lifted 10 cm off the floor) whose
    plane is nearest her, and sends the host's number for its body
    (`GuestState::touching`: the host numbers its bodies in each triangle's
    flags, above `kTriangleBodyShift`); F1 shows it, and the plugin logs
    which of Dead Space's bodies it is, for a wall she meets that he doesn't
    (build 50's run: a fixed box filling an open doorway; the plugin now
    leaves out of what it sends a body Isaac's own collision leaves out);
  - turned off, or with the host gone (out of a level, or quiet for 10 s: a
    stall of a second or two doesn't count), she gets her own size back, the
    rooms' objects come back on, and she goes back to where she was.
  A triangle faces as Dead Space winds it, and Prime's collision is one-sided
  (a box moving into a triangle from behind passes through:
  `CollisionUtil::RayTriangleIntersection_Double` needs the move against its
  normal, (b - a) x (c - a)), so F1 counts the triangles within 3 m of her that
  face away, and **Turn its triangles to face Samus** (off at each start)
  turns each to face her as the tree is built, again each metre she goes.
  Floors are those facing within 45 degrees of up, ceilings of down, the rest
  walls, all solid metal. Walls within 10 m of where a build is centred are
  also taken wound the other way (sharing the wall's corners and edges, and
  its smooth edges), so they stop her from both sides: a host may have a wall
  of one layer that its own collision stops its player at from either side
  (build 55's run: a doorway she could walk through one way and not back, but
  could roll under). F1 counts them; floors and ceilings keep their winding. F1 > Debug > Rendering > Collision view draws the
  tree in the world (floors blue, ceilings red, walls grey).
- Not yet: Necromorphs. Doors and lifts are rebuilt into the tree as they
  move, a tenth of a second or so behind (a lift's floor moves in steps).

The wire format (`platform/include/passthrough_protocol.h`) uses only 4-byte
fields, so a 32-bit plugin and the 64-bit port lay it out alike, and every
read and write goes through a sequence lock so neither side sees half of the
other's update (`tests/port_passthrough.cpp` races a writer against a reader
to check it). The triangle set, at 640 KiB, is copied word by word straight
between the block and the reader's copy, and only when it has changed; the
wireframe's placement and projection (`platform/include/passthrough_view.h`)
are covered by the same test. `tests/port_passthrough_collision.cpp` builds
trees from made-up rooms and checks them through the game's own collision
code: rays onto the floor and walls, a box of Isaac's size falling onto the
floor, meeting the ceiling and walls, a closed door and not an open one,
which body's wall a box is up against, passing a face from behind (and not
a wall made two-sided, near her), the
triangles' winding kept or turned to face her, the limits, and a large floor
triangle gathering only the leaves near a query.

## Fixes

**Water vanishing for a frame (Watery Hall).** Now and then the water would
disappear for a frame or two, leaving a dark hole. In play after the change
below, it happened for a few seconds after starting and then stopped, with
both F1 counters at 0, so the cause is the graphics layer building shaders:
aurora skips a draw whose pipeline isn't built yet rather than waiting
(`extern/aurora/lib/gx/pipeline.cpp`, `render` returns when `bind_pipeline`
fails), and builds missing ones on a worker thread. The pipelines saved in
the cache (`pipeline_cache.db`) are rebuilt in the background after start
(`load_pipeline_cache_entries`, at `Background` priority), and a water
variant seen for the first time (ripples, the camera near the surface) is
built when it's first drawn. Each is missing only until it's built, and once
saved it's ready at the next start. F1 > Debug > Rendering > Frame statistics shows
"pipelines made, waiting": waiting above 0 means some are still being built.
(Aurora's own water renderer, used for mod surfaces, waits for its pipelines
instead: `require_pipeline` at `Blocking` priority.)

**Bigger sorted-draw buckets.** Looking into it turned up a real limit, which
wasn't the cause here. The renderer sorts transparent things (water, glass,
effects) into 50 buckets of at most 128 draws, 512 in all, and leaves out
whatever doesn't fit; these are the original game's numbers, from the
decompiled `Buckets` in `src/MetaRender/CCubeRenderer.cpp`. A water surface
goes into its bucket last, so a full bucket would drop the water first. On PC
every bucket can now hold all the draws there can be (2048, four times
retail's, plus the 8 water surfaces), so none overflows; the drawing order is
unchanged, and it applies under **Original experience** too. F1 > Debug > Rendering >
Frame statistics counts transparent draws still left out (it should stay 0)
and the times retail's buckets would have dropped a water surface.

## Where the code is

| File | What it does |
|---|---|
| `platform/include/port_stasis.h`, `platform/port_stasis.cpp` | The arithmetic: meter, throws and holds, ease-out, the charge's flight step. No game code, so `tests/port_stasis.cpp` tests it alone. |
| `platform/include/port_dead_space.h`, `platform/port_dead_space.cpp` | Ties it to the game: picks the target, flies and draws the charge and its burst, scales the held enemy's time step, its glow, what the meter shows, and its label's text. |
| `platform/include/port_dead_space_sound.h`, `platform/port_dead_space_sound.cpp` | Plays your own WAV files for the throw and the hit through an SDL audio stream of their own (covered by `tests/port_dead_space_sound.cpp`). |
| `platform/include/port_double_tap.h`, `platform/port_double_tap.cpp` | Spots a movement key double-tapped (covered by `tests/port_double_tap.cpp`). |
| `platform/include/port_gun_sway.h`, `platform/port_gun_sway.cpp` | The gun sway's lean from each tick's view direction (covered by `tests/port_gun_sway.cpp`). |
| `platform/include/port_shot_speed.h` | A faster shot's lifetime for the same reach (covered by `tests/port_shot_speed.cpp`). |
| `src/Weapons/CProjectileWeapon.cpp`, `include/Weapons/CProjectileWeapon.hpp`, `src/MetroidPrime/Weapons/CGunWeapon.cpp` | Speeds up Power Beam shots as they're fired (`PortScaleSpeed`). |
| `platform/include/port_input_engine.h` | The `stasis` action in the binding engine. |
| `platform/input/port_input_devices.cpp` | Routes the Stasis action and adds its default bindings, and feeds the keyboard's movement keys to the double-tap check. |
| `platform/include/port_input_settings.h`, `platform/port_input_settings.cpp` | The settings and their `port_settings.ini` lines (covered by `tests/port_input_settings.cpp`). |
| `platform/include/port_debug.h`, `platform/debug_ui/options.cpp` | The settings' getters and setters. |
| `platform/include/port_debug.h` (`kWalkSpeedDefault`, `kWalkSpeedRetail`), `docs/SETTINGS.md` | The faster walk: 115% as the default of the port's own `walk_speed`. |
| `platform/generated/port_entity_schema.inc`, `docs/SAVESTATE_SCHEMA.md` | The save-state field schema with `CPlayer`'s dash and sway fields, made by `tools/gen_entity_schema.py` (clang-19); run it again when a class's fields change, or `port_entity_schema_current` fails. |
| `platform/debug_ui/input.cpp` | Keeps a Stasis press and a double-tap dash for the game's next tick. |
| `platform/debug_ui/controls.cpp` | The F1 overlay's Dead Space section. |
| `platform/debug_ui/cheats.cpp` | Shows the sorted-draw counters under Frame statistics. |
| `platform/debug_ui/tabs.cpp` | **Original experience**'s help, which names what it turns off. |
| `CMakeLists.txt` | Builds the new `platform/` files into the game and registers their tests (`mp_port_test`). |
| `src/MetroidPrime/CStateManager.cpp` | Runs Stasis each tick, scales a held enemy's `PreThink`, `Think` and collision move, and draws the charges after the rooms. |
| `src/MetroidPrime/Player/CGrappleArm.cpp`, `include/MetroidPrime/Player/CGrappleArm.hpp` | The throw: the grapple animations in order, and the charge let go at the thrust's projectile event. |
| `src/MetroidPrime/Player/CPlayerOrbit.cpp` | Points the throwing hand at the enemy; manual aim's limits on lock-on and the aim assist. |
| `src/MetroidPrime/Player/CPlayerDynamics.cpp`, `src/MetroidPrime/Player/CPlayer.cpp`, `include/MetroidPrime/Player/CPlayer.hpp` | The double-tap dash and the gun sway (`PortUpdateGunSway`, `PortGunLean`, applied in `UpdateGunTransform`). |
| `src/MetroidPrime/CTargetReticles.cpp` | Leans the crosshair with the gun. |
| `src/MetaRender/CCubeRenderer.cpp`, `include/MetaRender/CCubeRenderer.hpp` | Bigger sorted-draw buckets, as much room in the per-area stash Remastered's draw order keeps them in (`StashSlot`), and their counters for F1 > Debug > Rendering. |
| `platform/include/passthrough_protocol.h`, `platform/include/passthrough_shm.h`, `platform/passthrough_shm.cpp` | The passthrough bridge's wire format, sequence lock and shared memory, plain C++ for the Dead Space plugin too (covered by `tests/port_passthrough.cpp`). |
| `platform/include/port_passthrough.h`, `platform/port_passthrough.cpp`, `src/MetroidPrime/CStateManager.cpp`, `platform/debug_ui/debug.cpp` | Publishes Samus's state once a tick, reads the host's and its triangle set, the F1 > Debug > Passthrough switches, and the wireframe drawn each frame (called from `platform/debug_ui/window.cpp`). |
| `src/MetroidPrime/Player/CPlayerDynamics.cpp` (`GetEyeHeight`, `GetUnbiasedEyeHeight`, `GetOwnEyeHeight`) | Samus's eyes at the host player's height while the bridge is linked. |
| `platform/include/passthrough_view.h` | The wireframe's math: the host's world put where Samus is, and projected as the port's camera has it. |
| `platform/include/passthrough_collision.h`, `platform/passthrough_collision.cpp` | The host's triangles built into a room's collision tree (covered by `tests/port_passthrough_collision.cpp`). |
| `include/MetroidPrime/CGameArea.hpp` (`GetOctTree`), `src/MetroidPrime/CGameCollision.cpp` (`BuildAreaCollisionCache`, the failsafe's bounds), `src/MetroidPrime/CStateManager.cpp` (`DrawWorld`, the destructor), `src/MetroidPrime/Player/CPlayerDynamics.cpp` (`PortSetCollisionSize`), `include/MetroidPrime/Tweaks/CTweakPlayer.hpp` (`GetPlayerBallHalfExtent`), `src/MetroidPrime/Player/CMorphBall.cpp` (`PortSetRadius`) | Samus walking in the host's world: its collision for the rooms', the rooms undrawn, her size and her morph ball's. |
| `tools/passthrough_probe.cpp` | The stand-in host (`--room`: a made-up room for the wireframe and for walking in). |
| `src/MetroidPrime/Player/CPlayerGun.cpp` | Draws the throwing hand, and keeps the suit's right arm drawn while it throws; under manual aim, has missiles, Super Missiles and the Wave Beam seek the target nearest the crosshair; gives lock-on the faster Power Beam's speed. |
| `src/MetroidPrime/Enemies/CPatterned.cpp` | The glow, in `PreRender`. |
| `src/GuiSys/CAuiEnergyBarT01.cpp`, `include/GuiSys/CAuiEnergyBarT01.hpp` | Draws the meter and its label along the energy bar, inside the bar's own draw. |
| `src/MetroidPrime/HUD/CHudEnergyInterface.cpp` | Turns the meter on for Samus's energy bar in every visor but morph ball, and moves the low-energy warning below it. |
| `src/GuiSys/CGuiTextPane.cpp`, `include/GuiSys/CGuiTextPane.hpp` | A draw-time offset for a text pane, which that move uses; the frame's own layout is left as loaded. |

The new files under `platform/` and `tests/` are this fork's own work, the part
the port's `LICENSE` covers. The edits under `src/` are small hooks in the
decompiled game code, which stays Nintendo's (see `NOTICE`).
