#pragma once

// The input and controls settings (mouse aim, twin stick, touch layout, bindings,
// gyro, gameplay toggles): their values, the port_settings.ini lines that carry
// them, and the MP_* overrides. Plain data without SDL, so tests/port_input_settings.cpp
// links it alone. PortDebug:: keeps the getters and setters (Original experience and
// touch gating live there) and forwards to Get() / MutableSettings().

#include "port_input_map.h"

#include <atomic>
#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>

namespace PortInput {

// A second controller button per PAD button, indexed by the PAD bit's position.
constexpr int kPadAltCount = 16;
// Mouse crosshair size, percent.
constexpr int kCrosshairSizeMin = 25;
constexpr int kCrosshairSizeMax = 100;
constexpr int kCrosshairSizeDefault = 50;
// The Android touch overlay's margins, in dp.
constexpr float kTouchMarginMaxDp = 300.f;
constexpr float kTouchSideMarginDefault = 16.f;
constexpr float kTouchStickInsetDefault = 32.f;
constexpr float kTouchButtonInsetDefault = 0.f;
constexpr std::size_t kTouchLayoutMaxLen = 4096;
// SDL_SCANCODE_LSHIFT (SDL3), the default beam-shift key; debug_ui.cpp checks it.
constexpr int kScancodeLShift = 225;
// Dead Space Prime's Stasis (port_dead_space.h): how slow a held enemy runs, as a
// fraction of normal speed, and how long a hold lasts, in seconds.
constexpr float kStasisSlowMin = 0.05f;
constexpr float kStasisSlowMax = 0.5f;
constexpr float kStasisSlowDefault = 0.15f;
constexpr float kStasisDurationMin = 1.f;
constexpr float kStasisDurationMax = 15.f;
constexpr float kStasisDurationDefault = 5.f;
// Dead Space Prime's movement: Samus's top walking speed (and acceleration, so she
// reaches it as quickly) as a multiple of retail's, and the double-tap dash's
// speed, and so its length, as a multiple of its own.
constexpr float kWalkSpeedMin = 1.f;
constexpr float kWalkSpeedMax = 1.5f;
constexpr float kWalkSpeedDefault = 1.15f;
constexpr float kDashSpeedMin = 0.75f;
constexpr float kDashSpeedMax = 1.5f;
constexpr float kDashSpeedDefault = 1.3f;
// Dead Space Prime's gun sway: the most the arm cannon leans toward a turn, in
// degrees (port_gun_sway.h); 0 keeps it still.
constexpr float kGunSwayMin = 0.f;
constexpr float kGunSwayMax = 10.f;
constexpr float kGunSwayDefault = 3.f;
// Dead Space Prime's faster Power Beam: its shots' speed as a multiple of
// retail's, with the same reach (port_shot_speed.h).
constexpr float kPowerBeamSpeedMin = 1.f;
constexpr float kPowerBeamSpeedMax = 3.f;
constexpr float kPowerBeamSpeedDefault = 1.5f;

struct Settings {
  bool mouseAim = false;
  bool twinStick = false;
  bool springBall = false;
  bool swapScanXray = false;
  bool fastMorph = false;
  bool lockOnToggle = false;
  bool stickyCharge = false;
  bool rapidCharge = false;
  bool remasteredMovement = true;
  // Dead Space Prime: Stasis, off by default.
  bool stasis = false;
  float stasisSlow = kStasisSlowDefault;
  float stasisDuration = kStasisDurationDefault;
  // Dead Space Prime: manual aim (lock-on only for the scan visor and grapple
  // points, no aim assist) and dashing on a double-tapped movement key; off.
  bool manualAim = false;
  bool doubleTapDash = false;
  // Dead Space Prime: a faster walk, the gun's sway and faster Power Beam shots, on by default,
  // and the dash's speed.
  float walkSpeed = kWalkSpeedDefault;
  float dashSpeed = kDashSpeedDefault;
  float gunSway = kGunSwayDefault;
  float powerBeamSpeed = kPowerBeamSpeedDefault;
  bool springFlick = false;
  float springFlickRate = 6.f;
  float stickAimRate = 900.f;
  // Gyro aiming: off / hold / always, auto / controller / phone, and how fast a
  // rotation turns into aim travel.
  int gyroMode = 0;
  int gyroSource = 0;
  float gyroRate = 600.f;
  // Android touch overlay.
  bool touchColors = false;        // the GameCube pad's colours
  bool touchLabels = true;         // each button's function under its letter
  bool touchTurbo = false;         // a Turbo button beside Fire
  bool touchFloatingStick = true;  // the left stick appears where the left half is touched
  bool touchAim = true;
  float touchAimSpeed = 2.25f;
  bool touchMapTap = true;
  bool touchWheels = true;
  bool touchClassic = false;       // classic GameCube layout
  bool touchTwinStick = false;     // exclusive with touchClassic; classic wins on load
  bool touchVisorTapScan = false;
  // Read from the Android UI thread.
  std::atomic< float > touchSideMargin{kTouchSideMarginDefault};
  std::atomic< float > touchStickInset{kTouchStickInsetDefault};
  std::atomic< float > touchButtonInset{kTouchButtonInsetDefault};
  bool mouseInvertX = false;
  bool mouseInvertY = false;
  bool mouseButtons = true;
  bool mouseCrosshair = true;
  int crosshairSize = kCrosshairSizeDefault;
  float mouseSensitivity = 0.0035f;
  // What each mouse button does under mouse aim (PortInputMap::EMouseAction).
  int mouseActions[PortInputMap::kMouseButtonCount] = {
      PortInputMap::DefaultMouseAction(0), PortInputMap::DefaultMouseAction(1),
      PortInputMap::DefaultMouseAction(2), PortInputMap::DefaultMouseAction(3),
      PortInputMap::DefaultMouseAction(4)};
  // The beam shift: two keys or mouse buttons (scancode or PAD_KEY_MOUSE_*) and a
  // controller button (an SDL gamepad button or PAD_NATIVE_BUTTON_TRIGGER_*), -1 for none.
  int shiftBindings[3] = {kScancodeLShift, -1, -1};
  int turboBindings[3] = {-1, -1, -1}; // turbo fire: two keys, then a controller button
  int padAltButtons[kPadAltCount] = {-1, -1, -1, -1, -1, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1};
};

Settings& MutableSettings();
const Settings& Get();

// Applies one settings-file line; false if the key isn't an input key.
bool ApplySetting(std::string_view key, std::string_view value);
// After a whole file is read: classic and twin-stick touch layouts are exclusive.
void PostLoad();
// The input keys' lines, in three runs that sit at their old places in the file:
// bindings and toggles, gameplay options, then aim/touch/mouse. WriteSettings is all three.
void WriteControlSettings(std::ostream& out);
void WriteGameplaySettings(std::ostream& out);
void WriteAimSettings(std::ostream& out);
void WriteSettings(std::ostream& out);
// MP_* variables for a single run; they win over the file.
void ApplyEnvOverrides();

// The touch overlay's per-control offsets (an opaque string the Java view parses),
// shared between the UI thread and the game thread. Set refuses an over-long layout;
// the settings file also drops one that isn't printable ASCII.
std::string TouchLayout();
bool SetTouchLayout(std::string_view layout);

} // namespace PortInput
