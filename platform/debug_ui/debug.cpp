// F1 overlay: The Debug tab and log section. State and helpers shared with the other parts are in
// debug_ui_internal.h.

#include "debug_ui_internal.h"

#include "port_debug.h"
#include "port_log.h"
#include "port_log_file.h"
#include "port_passthrough.h"

#include <aurora/imgui.h>
#include <imgui_internal.h>

#include <string>

namespace PortDebug {

void DrawLogSection() {
  ImGui::SeparatorText("Log");
  bool logFile = sLogFile || PortLogFile::Active();
  if (ImGui::Checkbox("Write the log to a file", &logFile)) {
    SetLogFile(logFile);
    if (logFile) {
      PortLogFile::Start();
    }
  }
  const std::string logPath = PortLogFile::Path();
  if (!logPath.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Copy log path")) {
      ImGui::SetClipboardText(logPath.c_str());
    }
  }
  if (PortLogFile::Active()) {
    ImGui::TextWrapped("Writing to %s (last run's: metroid_prime_port.old.log).", logPath.c_str());
    if (!sLogFile) {
      ImGui::TextDisabled("Stops at the next start.");
    }
  } else {
    ImGui::TextWrapped("Everything the game logs, including the reason for a crash, goes to %s.",
                       logPath.empty() ? "(no user folder)" : logPath.c_str());
  }
  if (const std::string shared = PortLogFile::SharedPath(); !shared.empty()) {
    ImGui::TextWrapped("A copy goes to %s, which the phone's file manager can open.", shared.c_str());
  }

}

// Dead Space Prime's passthrough bridge (port_passthrough.h): the switch and what it sees.
static void DrawPassthrough() {
  bool enabled = PortPassthrough::Enabled();
  if (ImGui::Checkbox("Passthrough bridge", &enabled)) {
    PortPassthrough::SetEnabled(enabled);
  }
  ImGui::SetItemTooltip("Dead Space Prime's passthrough bridge, in progress: shares Samus's state\n"
                        "once a tick, through shared memory, with a plugin in Dead Space (2008).\n"
                        "passthrough_probe, next to the game, stands in for the plugin.\n"
                        "Not saved: off at each start.");
  const PortPassthrough::Info info = PortPassthrough::GetInfo();
  switch (info.status) {
  case PortPassthrough::Status::Off:
    break;
  case PortPassthrough::Status::Error:
    ImGui::TextDisabled("Not running: %s", info.message.c_str());
    break;
  case PortPassthrough::Status::Waiting:
    ImGui::TextDisabled("Publishing (tick %u); no host connected", info.guestTick);
    break;
  case PortPassthrough::Status::Connected:
    ImGui::TextDisabled("Host connected: frame %u; its player at %.2f %.2f %.2f", info.host.frame,
                        info.host.playerPosition.x, info.host.playerPosition.y,
                        info.host.playerPosition.z);
    break;
  }
}

void DrawDebugTab() {
  ImGui::SeparatorText("Cheats");
  bool cheats = sCheats;
  if (ImGui::Checkbox("Show cheats (items, health, teleport)", &cheats)) {
    sCheats = cheats;
    MarkDirty();
  }
  if (sCheats) {
    DrawCheats();
  }

  ImGui::SeparatorText("Camera");
  DrawFreeCam();

  ImGui::SeparatorText("Rendering");
  DrawRendering();

  ImGui::SeparatorText("Passthrough");
  DrawPassthrough();

  ImGui::SeparatorText("Audio backends");
  DrawAudio();
  if (ImGui::CollapsingHeader("Sounds playing")) {
    DrawVoices();
  }

  DrawRemasteredDebug();
}

} // namespace PortDebug
