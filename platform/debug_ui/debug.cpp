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
    if (const float eyes = PortPassthrough::EyeHeight(); eyes > 0.f) {
      ImGui::TextDisabled("Samus's eyes %.2f m above her feet, as its player's (hers are %.2f m)",
                          eyes, info.ownEyeHeight);
    }
    break;
  }
  bool shown = PortPassthrough::WireframeShown();
  if (ImGui::Checkbox("Draw the host's collision", &shown)) {
    PortPassthrough::SetWireframeShown(shown);
  }
  ImGui::SetItemTooltip("Dead Space's collision near Isaac, as the plugin sends it, drawn as a\n"
                        "wireframe around Samus, as if Isaac stood where she does, facing her\n"
                        "way (where it is, while she walks in Dead Space's world): blue for the\n"
                        "level, orange for what moves (doors, lifts).\n"
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
  bool walk = PortPassthrough::WalkInHostWorld();
  if (ImGui::Checkbox("Walk in the host's world", &walk)) {
    PortPassthrough::SetWalkInHostWorld(walk);
  }
  ImGui::SetItemTooltip("Samus walks in Dead Space's world: she goes to where Isaac stands,\n"
                        "facing his way and made his size, and collides with Dead Space's\n"
                        "collision near him (the level, doors and lifts; not Necromorphs yet).\n"
                        "Meanwhile the room's objects are switched off and the room isn't drawn\n"
                        "(F1 > Debug > Rendering > Collision view draws what she collides with).\n"
                        "Turned off, she goes back to where she was and the room comes back.\n"
                        "Not saved: off at each start.");
  if (walk) {
    switch (info.walk) {
    case PortPassthrough::Walk::Off:
      break;
    case PortPassthrough::Walk::Waiting:
      ImGui::TextDisabled("Waiting for %s", info.walkMessage.empty() ? "the bridge" : info.walkMessage.c_str());
      break;
    case PortPassthrough::Walk::Walking:
      ImGui::TextDisabled("Walking in the host's world; %u of the rooms' objects switched off",
                          info.frozen);
      break;
    }
    ImGui::TextDisabled("Samus %.2f m across and %.2f m tall (her own size: %.2f by %.2f)",
                        2.f * info.halfWidth, info.height, 2.f * info.ownHalfWidth, info.ownHeight);
    if (info.treeGeneration != 0) {
      const PassthroughCollision::Stats& t = info.tree;
      ImGui::TextDisabled("Collision from set %u: %u triangles (%u floors, %u walls, %u ceilings),",
                          info.treeGeneration, t.taken, t.floors, t.walls, t.ceilings);
      ImGui::TextDisabled("  %u of them on doors and lifts; %u vertices, %u edges, %u leaves;",
                          t.moving, t.vertices, t.edges, t.leaves);
      ImGui::TextDisabled("  left out: %u past the limits, %u with no area; built in %.1f ms (%u "
                          "builds)",
                          t.overLimits, t.degenerate, info.buildMs, info.builds);
      ImGui::TextDisabled("  %u of the %u triangles within 3 m of her face away from her",
                          t.facingAway, t.nearby);
    }
    bool face = PortPassthrough::FaceSamus();
    if (ImGui::Checkbox("Turn its triangles to face Samus", &face)) {
      PortPassthrough::SetFaceSamus(face);
    }
    ImGui::SetItemTooltip("Prime's collision stops Samus only coming at a triangle's front.\n"
                          "Off: the triangles face as Dead Space has them. On: each is turned to\n"
                          "face her as the collision is built (again each metre she goes), for if\n"
                          "Dead Space's face away from her (she'd fall through a floor, walk\n"
                          "through a wall). Not saved: off at each start.");
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
    const PassthroughView::Vec3 off = PassthroughView::Sub(centre, wireframe.samus);
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
