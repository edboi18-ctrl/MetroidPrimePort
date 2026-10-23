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
  bool shown = PortPassthrough::WireframeShown();
  if (ImGui::Checkbox("Draw the host's collision", &shown)) {
    PortPassthrough::SetWireframeShown(shown);
  }
  ImGui::SetItemTooltip("Dead Space's collision near Isaac, as the plugin sends it, drawn as a\n"
                        "wireframe around Samus, as if Isaac stood where she does, facing her\n"
                        "way: blue for the level, orange for what moves (doors, lifts).\n"
                        "Not saved: off at each start.");
  if (shown && info.status == PortPassthrough::Status::Connected) {
    PortPassthrough::Wireframe wireframe;
    if (PortPassthrough::GetWireframe(wireframe)) {
      ImGui::TextDisabled("%zu triangles within %.0f m of its player (set %u)",
                          wireframe.triangles->size(), wireframe.radius, wireframe.generation);
    } else {
      ImGui::TextDisabled("No triangles from the host yet");
    }
  }
}

// Dead Space's collision near Isaac, put where Samus is and drawn over the game's picture, the
// nearest triangles first.
void DrawPassthroughWireframe() {
  // Only those within this of Samus are drawn, at most this many (three lines each).
  constexpr float kDrawnWithin = 25.f;
  constexpr size_t kMostDrawn = 12000;
  PortPassthrough::Wireframe wireframe;
  if (!PortPassthrough::WireframeShown() || !PortPassthrough::GetWireframe(wireframe) ||
      !wireframe.triangles) {
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImDrawList* draw = ImGui::GetBackgroundDrawList(const_cast< ImGuiViewport* >(viewport));
  const PassthroughView::Placement& place = wireframe.placement;
  size_t drawn = 0;
  for (const Passthrough::Triangle& triangle : *wireframe.triangles) {
    const PassthroughView::Vec3 corners[3] = {PassthroughView::ToGuest(place, triangle.a),
                                              PassthroughView::ToGuest(place, triangle.b),
                                              PassthroughView::ToGuest(place, triangle.c)};
    const PassthroughView::Vec3 centre = PassthroughView::Scale(
        PassthroughView::Add(PassthroughView::Add(corners[0], corners[1]), corners[2]), 1.f / 3.f);
    const PassthroughView::Vec3 off = PassthroughView::Sub(centre, place.to);
    if (PassthroughView::Dot(off, off) > kDrawnWithin * kDrawnWithin) {
      continue;
    }
    if (++drawn > kMostDrawn) {
      break;
    }
    const ImU32 colour = (triangle.flags & Passthrough::kTriangleMoves) != 0
                             ? IM_COL32(255, 160, 40, 220)
                             : IM_COL32(60, 210, 255, 170);
    for (int edge = 0; edge < 3; ++edge) {
      float line[4];
      if (PassthroughView::Project(wireframe.camera, viewport->Size.x, viewport->Size.y,
                                   corners[edge], corners[(edge + 1) % 3], line)) {
        draw->AddLine(ImVec2(viewport->Pos.x + line[0], viewport->Pos.y + line[1]),
                      ImVec2(viewport->Pos.x + line[2], viewport->Pos.y + line[3]), colour, 1.f);
      }
    }
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
