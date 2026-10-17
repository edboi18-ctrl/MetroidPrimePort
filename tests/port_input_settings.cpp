#include "port_input_settings.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {
void Check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "input settings regression failed: %s\n", what);
    std::abort();
  }
}

std::string Text() {
  std::ostringstream out;
  PortInput::WriteSettings(out);
  return out.str();
}
} // namespace

int main() {
  using namespace PortInput;

  // Defaults, and the file they write.
  const std::string defaults = Text();
  Check(!Get().mouseAim && !Get().twinStick && Get().remasteredMovement, "defaults");
  Check(Get().shiftBindings[0] == kScancodeLShift && Get().shiftBindings[1] == -1, "default shift");
  Check(defaults.rfind("mouse_aim=0\ntwin_stick=0\n", 0) == 0, "first lines");
  Check(defaults.find("pad_alt=-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1\n") != std::string::npos, "pad_alt");
  Check(defaults.find("mouse_left=") != std::string::npos && defaults.find("mouse_x2=") != std::string::npos,
        "mouse action keys");

  // Keys that aren't input keys are left to the caller.
  Check(!ApplySetting("fov", "90") && !ApplySetting("game_options", "00") && !ApplySetting("nonsense", "1"),
        "foreign keys");

  // Toggles, ranges and rejects.
  Check(ApplySetting("mouse_aim", "1") && Get().mouseAim, "mouse_aim");
  Check(ApplySetting("stick_aim_rate", "2000") && Get().stickAimRate == 2000.f, "stick_aim_rate");
  ApplySetting("stick_aim_rate", "10");
  Check(Get().stickAimRate == 2000.f, "stick_aim_rate below range");
  ApplySetting("stick_aim_rate", "20000");
  Check(Get().stickAimRate == 2000.f, "stick_aim_rate above range");
  ApplySetting("crosshair_size", "80");
  Check(Get().crosshairSize == 80, "crosshair_size");
  ApplySetting("crosshair_size", "5");
  Check(Get().crosshairSize == 80, "crosshair_size range");
  ApplySetting("touch_side_margin", "40");
  Check(Get().touchSideMargin.load() == 40.f, "touch_side_margin");
  ApplySetting("touch_side_margin", "400");
  Check(Get().touchSideMargin.load() == 40.f, "touch_side_margin range");
  ApplySetting("gyro_mode", "2");
  ApplySetting("gyro_mode", "3");
  Check(Get().gyroMode == 2, "gyro_mode range");
  ApplySetting("shift_key_alt", "44");
  ApplySetting("turbo_pad", "3");
  Check(Get().shiftBindings[1] == 44 && Get().turboBindings[2] == 3, "bindings");
  ApplySetting("mouse_right", "lock_on");
  const std::string before = Text();
  ApplySetting("mouse_right", "not_an_action");
  Check(Text() == before, "unknown mouse action ignored");

  // Dead Space Prime's Stasis: off by default, its two numbers held to their ranges.
  Check(!Get().stasis && Get().stasisSlow == kStasisSlowDefault &&
            Get().stasisDuration == kStasisDurationDefault,
        "stasis defaults");
  Check(defaults.find("stasis=0\nstasis_slow=0.15\nstasis_duration=5\n") != std::string::npos,
        "stasis lines");
  Check(ApplySetting("stasis", "1") && Get().stasis, "stasis");
  Check(ApplySetting("stasis_slow", "0.25") && Get().stasisSlow == 0.25f, "stasis_slow");
  ApplySetting("stasis_slow", "0.9");
  Check(Get().stasisSlow == 0.25f, "stasis_slow above range");
  ApplySetting("stasis_slow", "0");
  Check(Get().stasisSlow == 0.25f, "stasis_slow below range");
  Check(ApplySetting("stasis_duration", "8") && Get().stasisDuration == 8.f, "stasis_duration");
  ApplySetting("stasis_duration", "0.5");
  Check(Get().stasisDuration == 8.f, "stasis_duration below range");
  ApplySetting("stasis_duration", "nan");
  Check(Get().stasisDuration == 8.f, "stasis_duration not a number");

  // Dead Space Prime's manual aim and double-tap dash: off by default, saved after Stasis.
  Check(!Get().manualAim && !Get().doubleTapDash, "manual aim and dash defaults");
  Check(defaults.find("stasis_duration=5\nmanual_aim=0\ndouble_tap_dash=0\n") !=
            std::string::npos,
        "manual aim and dash lines");
  Check(ApplySetting("manual_aim", "1") && Get().manualAim, "manual_aim");
  Check(ApplySetting("double_tap_dash", "1") && Get().doubleTapDash, "double_tap_dash");
  Check(ApplySetting("manual_aim", "0") && !Get().manualAim, "manual_aim off");

  // Dead Space Prime's walk and dash speeds and gun sway: a faster walk, a 130% dash and a 3
  // degree sway by default, each held to its range.
  Check(Get().walkSpeed == kWalkSpeedDefault && Get().dashSpeed == kDashSpeedDefault &&
            Get().gunSway == kGunSwayDefault,
        "walk, dash and sway defaults");
  Check(defaults.find("double_tap_dash=0\nwalk_speed=1.15\ndash_speed=1.3\ngun_sway=3\n") !=
            std::string::npos,
        "walk, dash and sway lines");
  Check(ApplySetting("walk_speed", "1.3") && Get().walkSpeed == 1.3f, "walk_speed");
  ApplySetting("walk_speed", "0.5");
  Check(Get().walkSpeed == 1.3f, "walk_speed below range");
  ApplySetting("walk_speed", "inf");
  Check(Get().walkSpeed == 1.3f, "walk_speed not finite");
  Check(ApplySetting("dash_speed", "1.25") && Get().dashSpeed == 1.25f, "dash_speed");
  ApplySetting("dash_speed", "2");
  Check(Get().dashSpeed == 1.25f, "dash_speed above range");
  Check(ApplySetting("gun_sway", "0") && Get().gunSway == 0.f, "gun_sway off");
  Check(ApplySetting("gun_sway", "5.5") && Get().gunSway == 5.5f, "gun_sway");
  ApplySetting("gun_sway", "11");
  Check(Get().gunSway == 5.5f, "gun_sway above range");
  ApplySetting("gun_sway", "-1");
  Check(Get().gunSway == 5.5f, "gun_sway below range");

  // Dead Space Prime's faster Power Beam: 150% by default, held to 100-300%.
  Check(Get().powerBeamSpeed == kPowerBeamSpeedDefault, "power beam speed default");
  Check(defaults.find("gun_sway=3\npower_beam_speed=1.5\n") != std::string::npos,
        "power beam speed line");
  Check(ApplySetting("power_beam_speed", "2.25") && Get().powerBeamSpeed == 2.25f,
        "power_beam_speed");
  ApplySetting("power_beam_speed", "0.5");
  Check(Get().powerBeamSpeed == 2.25f, "power_beam_speed below range");
  ApplySetting("power_beam_speed", "4");
  Check(Get().powerBeamSpeed == 2.25f, "power_beam_speed above range");

  // Touch layout.
  Check(ApplySetting("touch_layout", "1:0.5,0.5,1") && TouchLayout() == "1:0.5,0.5,1", "touch_layout");
  Check(!SetTouchLayout(std::string(kTouchLayoutMaxLen + 1, 'x')), "touch layout length");
  Check(TouchLayout() == "1:0.5,0.5,1", "touch layout kept");

  // Classic GameCube wins over twin stick on load.
  ApplySetting("touch_twin_stick", "1");
  ApplySetting("touch_classic_gc", "1");
  PostLoad();
  Check(Get().touchClassic && !Get().touchTwinStick, "classic wins");

  // Write then read back reproduces the same text.
  const std::string saved = Text();
  ApplySetting("mouse_aim", "0");
  ApplySetting("stick_aim_rate", "300");
  ApplySetting("touch_side_margin", "10");
  ApplySetting("shift_key", "5");
  ApplySetting("touch_classic_gc", "0");
  SetTouchLayout("");
  Check(Text() != saved, "state changed before the read-back");
  std::istringstream in(saved);
  for (std::string line; std::getline(in, line);) {
    const size_t eq = line.find('=');
    Check(eq != std::string::npos, "line has =");
    Check(ApplySetting(line.substr(0, eq), line.substr(eq + 1)) || line.compare(0, 12, "touch_layout") == 0,
          "every written key is read");
  }
  PostLoad();
  Check(Text() == saved, "round trip");
  return 0;
}
