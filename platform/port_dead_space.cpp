// Dead Space Prime's Stasis in the game (port_dead_space.h). The meter is drawn
// the way PortSpeedrunTimer draws the in-game time: after the frame's IOWins,
// with the game's own font, here in the HUD's missile-bar colours.
#include "port_dead_space.h"

#include "port_debug.h"
#include "port_stasis.h"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CResFactory.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/TToken.hpp"
#include "Kyoto/Text/CDrawStringOptions.hpp"
#include "Kyoto/Text/CRasterFont.hpp"
#include "Kyoto/Text/CTextColor.hpp"
#include "Kyoto/Text/CTextRenderBuffer.hpp"
#include "MetaRender/CCubeRenderer.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Cameras/CCameraManager.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/SFX/UI.h"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/TGameTypes.hpp"
#include "MetroidPrime/Tweaks/CTweakGuiColors.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PortDeadSpace {
namespace {

PortStasis::State sStasis;
// The game the holds belong to: another one (a new game, a reload) starts clean.
const CStateManager* sOwner = nullptr;
// Seconds left of the meter's flash after a press that held nothing.
float sRefusedFlash = 0.f;
const float kRefusedFlashTime = 0.4f;
// The glow at full hold, added to the lit model.
const uchar kGlowRed = 24;
const uchar kGlowGreen = 96;
const uchar kGlowBlue = 220;
const CColor kRefusedColor(static_cast< uchar >(255), 64, 48, 255);

// The meter, as fractions of the viewport: its width, height, and how far its
// bottom edge sits above the screen's.
const float kBarWidth = 0.2f;
const float kBarHeight = 0.011f;
const float kBarBottom = 0.07f;
const float kLabelGap = 4.f;

TCachedToken< CRasterFont >* sFont = nullptr;
CTextRenderBuffer* sLabel = nullptr;
int sLabelWidth = 0;
int sLabelHeight = 0;

PortStasis::Tuning CurrentTuning() {
  PortStasis::Tuning tuning;
  tuning.timeScale = PortDebug::StasisSlow();
  tuning.duration = PortDebug::StasisDuration();
  return tuning;
}

TUniqueId FromRaw(uint16_t raw) {
  TUniqueId id;
  id.value = raw;
  return id;
}

// An enemy Stasis can hold: loaded, active and alive.
CPatterned* LiveEnemy(CStateManager& mgr, TUniqueId id) {
  if (id == kInvalidUniqueId) return nullptr;
  CEntity* ent = mgr.ObjectById(id);
  if (ent == nullptr) return nullptr;
  CPatterned* enemy = TCastToPtr< CPatterned >(ent);
  if (enemy == nullptr || !enemy->GetActive() || !enemy->IsAlive()) return nullptr;
  return enemy;
}

// What a press holds: the enemy under lock-on, else the one the shots would
// home in on. Nothing in morph ball.
TUniqueId PressTarget(CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer();
  if (player == nullptr || player->GetMorphballTransitionState() != CPlayer::kMS_Unmorphed) {
    return kInvalidUniqueId;
  }
  if (LiveEnemy(mgr, player->GetOrbitTargetId()) != nullptr) return player->GetOrbitTargetId();
  if (LiveEnemy(mgr, player->GetAimTargetId()) != nullptr) return player->GetAimTargetId();
  return kInvalidUniqueId;
}

// The HUD's own blips, loaded whenever the HUD is.
void Blip(ushort sfx) {
  CSfxManager::SfxStart(sfx, 127, 64, false, CSfxManager::kMedPriority, false,
                        CSfxManager::kAllAreas);
}

int Advance(const CRasterFont& font, wchar_t c) {
  const CGlyph* glyph = font.GetGlyph(c);
  return glyph != nullptr ? glyph->GetA() + glyph->GetB() + glyph->GetC() : 0;
}

// "STASIS" in the HUD font, laid out once the font has loaded.
bool BuildLabel() {
  if (sLabel != nullptr) return true;
  if (gpResourceFactory == nullptr || gpSimplePool == nullptr) return false;
  if (sFont == nullptr) {
    const SObjectTag* tag = gpResourceFactory->GetResourceIdByName("FONT_Deface14B");
    if (tag == nullptr) return false;
    sFont = rs_new TCachedToken< CRasterFont >(gpSimplePool->GetObj(*tag));
    sFont->Lock();
  }
  if (!sFont->TryCache()) return false;
  CRasterFont& font = *sFont->GetObject();
  if (!font.IsFinishedLoading()) return false;

  static const wchar_t kText[] = L"STASIS";
  const int length = static_cast< int >(sizeof(kText) / sizeof(kText[0])) - 1;
  CDrawStringOptions options;
  options.SetTextDirection(kTD_Horizontal);
  options.SetPaletteEntry(0, CTextColor(255, 255, 255, 255).GetRGBA());
  options.SetPaletteEntry(1, CTextColor(0, 0, 0, 204).GetRGBA());
  options.SetPaletteEntry(2, CTextColor(255, 255, 255, 255).GetRGBA());
  sLabel = rs_new CTextRenderBuffer(CTextRenderBuffer::kM_AllocTally);
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1) sLabel->SetMode(CTextRenderBuffer::kM_BufferFill);
    sLabel->AddFontChange(*sFont);
    int xOut = 0;
    int yOut = 0;
    font.DrawString(options, 0, font.GetBaseLine(), xOut, yOut, sLabel, kText, length);
  }
  sLabelWidth = 0;
  for (int i = 0; i < length; ++i) sLabelWidth += Advance(font, kText[i]);
  sLabelHeight = font.GetMonoHeight();
  return true;
}

// An axis-aligned box in the z-up ortho view SetViewportOrtho(false, ...) gives.
void Box(float left, float bottom, float right, float top, const CColor& color) {
  gpRender->BeginTriangleStrip(4);
  gpRender->PrimColor(color);
  gpRender->PrimVertex(CVector3f(left, 0.f, top));
  gpRender->PrimVertex(CVector3f(left, 0.f, bottom));
  gpRender->PrimVertex(CVector3f(right, 0.f, top));
  gpRender->PrimVertex(CVector3f(right, 0.f, bottom));
  gpRender->EndPrimitive();
}

// The meter shows in plain gameplay: not on the pause or map screens, in a
// cinematic, in morph ball or once Samus is dead.
bool HudVisible(const CStateManager& mgr) {
  if (&mgr != sOwner || mgr.GetGameState() != CStateManager::kGS_Running) return false;
  if (PortDebug::PauseScreenOpen() || PortDebug::MapScreenOpen()) return false;
  const CCameraManager* cameras = mgr.GetCameraManager();
  if (cameras == nullptr || cameras->IsInCinematicCamera()) return false;
  const CPlayer* player = mgr.GetPlayer();
  return player != nullptr && player->GetDeathTime() <= 0.f &&
         player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed;
}

} // namespace

void Tick(CStateManager& mgr, float dt) {
  const bool pressed = PortDebug::ConsumeStasisRequest();
  if (&mgr != sOwner) {
    sStasis = PortStasis::State();
    sRefusedFlash = 0.f;
    sOwner = &mgr;
  }
  if (!PortDebug::Stasis()) {
    sStasis.ReleaseAll();
    sRefusedFlash = 0.f;
    return;
  }

  const PortStasis::Tuning tuning = CurrentTuning();
  // Backwards: Release closes the gap over the holds already looked at.
  for (int i = sStasis.HeldCount() - 1; i >= 0; --i) {
    const uint16_t held = sStasis.HeldTarget(i);
    if (LiveEnemy(mgr, FromRaw(held)) == nullptr) sStasis.Release(held);
  }
  sStasis.Tick(dt, tuning);
  sRefusedFlash = std::max(0.f, sRefusedFlash - dt);
  if (!pressed) return;

  const TUniqueId target = PressTarget(mgr);
  const uint16_t raw = target == kInvalidUniqueId ? PortStasis::kNoTarget : target.value;
  switch (sStasis.Apply(raw, tuning)) {
  case PortStasis::EResult::Held:
  case PortStasis::EResult::Refreshed:
    Blip(SFXui_x_freeon_00);
    break;
  case PortStasis::EResult::NoTarget:
  case PortStasis::EResult::NoEnergy:
    sRefusedFlash = kRefusedFlashTime;
    Blip(SFXui_x_freeoff_00);
    break;
  }
}

float TimeScale(TUniqueId id) {
  if (sStasis.HeldCount() == 0) return 1.f;
  return sStasis.TimeScale(id.value, CurrentTuning());
}

void Tint(TUniqueId id, CColor& color) {
  if (sStasis.HeldCount() == 0) return;
  const float strength = sStasis.Strength(id.value, CurrentTuning());
  if (strength <= 0.f) return;
  const auto channel = [strength](uchar current, uchar glow) {
    const long scaled = std::lround(static_cast< float >(glow) * strength);
    return static_cast< uchar >(std::max< long >(current, scaled));
  };
  color.Set(channel(color.GetRedu8(), kGlowRed), channel(color.GetGreenu8(), kGlowGreen),
            channel(color.GetBlueu8(), kGlowBlue), color.GetAlphau8());
}

void Reset() {
  sStasis.ReleaseAll();
  sRefusedFlash = 0.f;
}

void DrawHud() {
  if (!PortDebug::Stasis() || gpRender == nullptr || gpTweakGuiColors == nullptr) return;
  const CStateManager* mgr = PortDebug::StateManager();
  if (mgr == nullptr || !HudVisible(*mgr)) return;

  const float width = static_cast< float >(CGraphics::GetViewportWidth());
  const float height = static_cast< float >(CGraphics::GetViewportHeight());
  const float barWidth = std::round(width * kBarWidth);
  const float barHeight = std::max(4.f, std::round(height * kBarHeight));
  const float left = std::round((width - barWidth) * 0.5f);
  const float bottom = std::round(height * kBarBottom);
  const float top = bottom + barHeight;
  const float edge = std::max(1.f, std::round(barHeight * 0.25f));
  const CColor& filled = gpTweakGuiColors->GetMissileBarFilled();
  const CColor& shadow = gpTweakGuiColors->GetMissileBarShadow();
  const CColor empty = CColor::Lerp(gpTweakGuiColors->GetMissileBarEmpty(), kRefusedColor,
                                    sRefusedFlash / kRefusedFlashTime);

  gpRender->SetViewportOrtho(false, -4096.f, 4096.f);
  CGraphics::SetCullMode(kCM_None);
  gpRender->SetDepthReadWrite(false, false);

  if (BuildLabel()) {
    gpRender->SetModelMatrix(
        CTransform4f::Translate(std::round((width - static_cast< float >(sLabelWidth)) * 0.5f), 0.f,
                                top + edge + kLabelGap + static_cast< float >(sLabelHeight)) *
        CTransform4f::Scale(CVector3f(1.f, 1.f, -1.f)));
    sLabel->Render(filled, 0.f);
  }

  gpRender->SetModelMatrix(CTransform4f::Identity());
  CGraphics::SetBlendMode(kBM_Blend, kBF_SrcAlpha, kBF_InvSrcAlpha, kLO_Clear);
  Box(left - edge, bottom - edge, left + barWidth + edge, top + edge, shadow);
  Box(left, bottom, left + barWidth, top, empty);
  const float fill = sStasis.Meter() / PortStasis::kMeterMax;
  if (fill > 0.f) {
    Box(left, bottom, left + std::round(barWidth * fill), top, filled);
  }
  // A notch at each use's worth of meter.
  const float cost = PortStasis::Tuning().cost;
  if (cost > 0.f) {
    for (float at = cost; at < PortStasis::kMeterMax; at += cost) {
      const float x = std::round(left + barWidth * at / PortStasis::kMeterMax);
      Box(x - std::max(1.f, edge * 0.5f), bottom, x + std::max(1.f, edge * 0.5f), top, shadow);
    }
  }
}

} // namespace PortDeadSpace
