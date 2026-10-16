// Dead Space Prime's Stasis in the game (port_dead_space.h). The meter is drawn by
// CAuiEnergyBarT01, along the player's energy bar in the helmet HUD; this says what
// it shows and draws its label in the game's own font.
#include "port_dead_space.h"

#include "port_debug.h"
#include "port_stasis.h"

#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CResFactory.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/TToken.hpp"
#include "Kyoto/Text/CDrawStringOptions.hpp"
#include "Kyoto/Text/CRasterFont.hpp"
#include "Kyoto/Text/CTextColor.hpp"
#include "Kyoto/Text/CTextRenderBuffer.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/SFX/UI.h"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/TGameTypes.hpp"

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
// The meter's charge, and its empty part's flash after a refused press. The HUD
// draws both additively, tinted and faded with the energy bar.
const CColor kMeterColor(static_cast< uchar >(90), 170, 255, 255);
const CColor kRefusedColor(static_cast< uchar >(255), 64, 48, 255);

TCachedToken< CRasterFont >* sFont = nullptr;
CTextRenderBuffer* sLabel = nullptr;
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

// "STASIS" in the HUD font, laid out once the font has loaded: its rows run down
// from 0 at the top of the line to sLabelHeight.
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
  sLabelHeight = font.GetMonoHeight();
  return true;
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

bool StasisMeterVisible() {
  if (!PortDebug::Stasis()) return false;
  const CStateManager* mgr = PortDebug::StateManager();
  if (mgr == nullptr || mgr != sOwner) return false;
  const CPlayer* player = mgr->GetPlayer();
  return player != nullptr && player->GetDeathTime() <= 0.f;
}

float StasisMeterFraction() {
  return std::clamp(sStasis.Meter() / PortStasis::kMeterMax, 0.f, 1.f);
}

float StasisMeterStep() {
  const float step = CurrentTuning().cost / PortStasis::kMeterMax;
  return step > 0.f ? std::clamp(step, 0.1f, 1.f) : 1.f;
}

float StasisRefusedFlash() { return std::clamp(sRefusedFlash / kRefusedFlashTime, 0.f, 1.f); }

const CColor& StasisMeterColor() { return kMeterColor; }

const CColor& StasisRefusedColor() { return kRefusedColor; }

void DrawStasisLabel(const CTransform4f& topLeft, float height, const CColor& color) {
  if (!BuildLabel() || sLabelHeight <= 0) return;
  const float scale = height / static_cast< float >(sLabelHeight);
  // The rows run down, as CGuiTextSupport::Render flips them for a text pane.
  CGraphics::SetModelMatrix(topLeft * CTransform4f::Scale(scale, 1.f, -scale));
  sLabel->Render(color, 0.f);
}

} // namespace PortDeadSpace
