// Dead Space Prime's Stasis in the game (port_dead_space.h). The meter is drawn by
// CAuiEnergyBarT01, along the player's energy bar in the helmet HUD; this says what
// it shows and draws its label in the game's own font. The throwing hand is
// CGrappleArm's; the charge and its burst are drawn here, as additive light.
#include "port_dead_space.h"

#include "port_dead_space_sound.h"
#include "port_debug.h"
#include "port_stasis.h"

#include "Collision/CMaterialFilter.hpp"
#include "Collision/CMaterialList.hpp"
#include "Collision/CRayCastResult.hpp"
#include "Kyoto/Audio/CSfxManager.hpp"
#include "Kyoto/CResFactory.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CColor.hpp"
#include "Kyoto/Graphics/CGX.hpp"
#include "Kyoto/Graphics/CGraphics.hpp"
#include "Kyoto/Math/CAABox.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/Math/CVector3f.hpp"
#include "Kyoto/TToken.hpp"
#include "Kyoto/Text/CDrawStringOptions.hpp"
#include "Kyoto/Text/CRasterFont.hpp"
#include "Kyoto/Text/CTextColor.hpp"
#include "Kyoto/Text/CTextRenderBuffer.hpp"
#include "MetroidPrime/CStateManager.hpp"
#include "MetroidPrime/Cameras/CCameraManager.hpp"
#include "MetroidPrime/Cameras/CGameCamera.hpp"
#include "MetroidPrime/Enemies/CPatterned.hpp"
#include "MetroidPrime/Player/CGameOptions.hpp"
#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CGrappleArm.hpp"
#include "MetroidPrime/Player/CPlayer.hpp"
#include "MetroidPrime/Player/CPlayerGun.hpp"
#include "MetroidPrime/SFX/UI.h"
#include "MetroidPrime/TCastTo.hpp"
#include "MetroidPrime/TGameTypes.hpp"
#include "dolphin/gx/GXVert.h"
#include "rstl/optional_object.hpp"

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

// The throw: a charge's speed in metres a second, how long the hand may take to
// let go before the charge leaves from beside the view, and how long one may fly
// before it fizzles short of its target.
const float kThrowSpeed = 30.f;
const float kWindUpMax = 1.f;
const float kFlightMax = 2.f;
const int kMaxThrows = 4;
const int kTrail = 6;
// Manual aim: how far a throw reaches, and how far beside the crosshair's line an
// enemy's body may be and still be what it's thrown at.
const float kAimRange = 60.f;
const float kAimTolerance = 0.5f;
// A charge thrown at no one still catches an enemy whose body, grown by this, it
// passes through.
const float kCatchTolerance = 0.3f;

// A paid press, from the throw until its charge lands.
struct Throw {
  bool active = false;
  bool flying = false; // false while the hand winds up
  bool onArm = false;  // Samus's left hand throws it
  uint16_t target = PortStasis::kNoTarget;
  float pos[3] = {};
  float goal[3] = {};
  // Where it was on the last few ticks, newest first: the tail is drawn along them.
  float trail[kTrail][3] = {};
  int trailCount = 0;
  float age = 0.f; // since the press, then since it left the hand
};
Throw sThrows[kMaxThrows];
// Where the throwing hand aims, kept while the arm finishes its throw.
float sArmGoal[3] = {};
bool sArmGoalSet = false;

// Where a charge lands: a ring of light swelling out and fading.
const int kMaxBursts = 4;
const float kBurstTime = 0.45f;
struct Burst {
  bool active = false;
  bool held = false; // it caught its target; else it fizzled
  float pos[3] = {};
  float radius = 1.f;
  float age = 0.f;
};
Burst sBursts[kMaxBursts];

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

CVector3f ToVec(const float v[3]) { return CVector3f(v[0], v[1], v[2]); }

void SetVec(float v[3], const CVector3f& c) {
  v[0] = c.GetX();
  v[1] = c.GetY();
  v[2] = c.GetZ();
}

bool Unmorphed(const CStateManager& mgr) {
  const CPlayer* player = mgr.GetPlayer();
  return player != nullptr && player->GetMorphballTransitionState() == CPlayer::kMS_Unmorphed;
}

const CMaterialFilter& SightFilter() {
  static const CMaterialFilter kFilter = CMaterialFilter::MakeIncludeExclude(
      CMaterialList(kMT_Solid),
      CMaterialList(kMT_ProjectilePassthrough, kMT_ScanPassthrough, kMT_Player));
  return kFilter;
}

// An enemy's body for aiming: its touch bounds, else a metre around where shots aim.
void BodyOf(const CStateManager& mgr, const CActor& enemy, float lo[3], float hi[3]) {
  const rstl::optional_object< CAABox > bounds = enemy.GetTouchBounds();
  if (bounds) {
    SetVec(lo, bounds->GetMinPoint());
    SetVec(hi, bounds->GetMaxPoint());
    return;
  }
  SetVec(lo, enemy.GetAimPosition(mgr, 0.f) - CVector3f(0.5f, 0.5f, 0.5f));
  SetVec(hi, enemy.GetAimPosition(mgr, 0.f) + CVector3f(0.5f, 0.5f, 0.5f));
}

// The live enemies whose bounds come near the box from `a` to `b` grown by `pad`.
void NearEnemies(CStateManager& mgr, const CVector3f& a, const CVector3f& b, float pad,
                 TEntityList& out) {
  const CVector3f lo(std::min(a.GetX(), b.GetX()) - pad, std::min(a.GetY(), b.GetY()) - pad,
                     std::min(a.GetZ(), b.GetZ()) - pad);
  const CVector3f hi(std::max(a.GetX(), b.GetX()) + pad, std::max(a.GetY(), b.GetY()) + pad,
                     std::max(a.GetZ(), b.GetZ()) + pad);
  mgr.BuildNearList(out, CAABox(lo, hi), CMaterialFilter::skPassEverything, mgr.GetPlayer());
}

// The first live enemy on the line from `from` along the unit `dir` within `length`, its body
// grown by `pad`, and how far along it is; kInvalidUniqueId when there is none.
TUniqueId FirstEnemyOnLine(CStateManager& mgr, const CVector3f& from, const CVector3f& dir,
                           float length, float pad, float& along) {
  TEntityList nearList;
  // Grown further than `pad`: an enemy's bounds can reach past what the near list is built on.
  NearEnemies(mgr, from, from + dir * length, pad + 3.f, nearList);
  float origin[3];
  float direction[3];
  SetVec(origin, from);
  SetVec(direction, dir);
  TUniqueId best = kInvalidUniqueId;
  along = length;
  for (const TUniqueId id : nearList) {
    const CPatterned* enemy = LiveEnemy(mgr, id);
    if (enemy == nullptr) continue;
    float lo[3];
    float hi[3];
    BodyOf(mgr, *enemy, lo, hi);
    const float hit = PortStasis::RayBoxDistance(origin, direction, lo, hi, pad);
    if (hit >= 0.f && hit <= along) {
      along = hit;
      best = id;
    }
  }
  return best;
}

// Manual aim: the live enemy the crosshair is on, if any - the nearest the crosshair's line
// meets, before a wall, within kAimTolerance of its body - and where a throw lands: on it,
// else on the first wall along the line, or kAimRange out.
TUniqueId CrosshairTarget(CStateManager& mgr, CVector3f& aimPoint) {
  const CTransform4f& cam = mgr.GetCameraManager()->GetCurrentCamera(mgr).GetTransform();
  const CVector3f eye = cam.GetTranslation();
  const CVector3f dir = cam.GetForward().AsNormalized();
  const CRayCastResult wall = mgr.RayStaticIntersection(eye, dir, kAimRange, SightFilter());
  const float reach = wall.IsInvalid() ? kAimRange : wall.GetTime();
  aimPoint = eye + dir * reach;
  float along = reach;
  const TUniqueId target = FirstEnemyOnLine(mgr, eye, dir, reach, kAimTolerance, along);
  if (const CPatterned* enemy = LiveEnemy(mgr, target)) {
    aimPoint = enemy->GetAimPosition(mgr, 0.f);
  }
  return target;
}

// Low and to the left of the view, where the throwing hand would be.
CVector3f BesideView(const CStateManager& mgr) {
  const CTransform4f& cam = mgr.GetCameraManager()->GetCurrentCamera(mgr).GetTransform();
  return cam.GetTranslation() + cam.GetForward() * 0.6f - cam.GetRight() * 0.3f -
         cam.GetUp() * 0.25f;
}

void ClearThrows() {
  for (Throw& t : sThrows) t = Throw();
  for (Burst& b : sBursts) b = Burst();
  sArmGoalSet = false;
}

// The game's sound-effect volume (Options > Sound), 0..1, for the player's own
// Dead Space sounds (port_dead_space_sound.h).
float SfxGain() {
  if (gpGameState == nullptr) return 1.f;
  return std::clamp(static_cast< float >(gpGameState->GameOptions().GetSfxVolume()) / 127.f, 0.f,
                    1.f);
}

// Quieter with distance from the camera: full within 10 m, down to a third by 35 m.
float DistanceGain(const CStateManager& mgr, const float at[3]) {
  const CVector3f eye = mgr.GetCameraManager()->GetCurrentCamera(mgr).GetTranslation();
  const float distance = (ToVec(at) - eye).Magnitude();
  return std::clamp(1.4f - distance / 25.f, 0.35f, 1.f);
}

void Launch(Throw& t, const CVector3f& from) {
  t.flying = true;
  t.age = 0.f;
  SetVec(t.pos, from);
  t.trailCount = 0;
  // Without a file of the player's own, the throwing arm's grapple sounds are what's heard.
  PortDeadSpaceSound::Play(PortDeadSpaceSound::ECue::StasisFire, SfxGain());
}

// A burst about the size of `enemy`.
float BurstRadius(const CActor& enemy) {
  const rstl::optional_object< CAABox > bounds = enemy.GetTouchBounds();
  if (!bounds) return 1.f;
  const CVector3f size = bounds->GetMaxPoint() - bounds->GetMinPoint();
  const float largest = std::max(size.GetX(), std::max(size.GetY(), size.GetZ()));
  return std::clamp(0.6f * largest, 0.6f, 3.f);
}

void AddBurst(const float pos[3], float radius, bool held) {
  // A free slot, else the oldest burst gives way.
  Burst* slot = nullptr;
  for (Burst& b : sBursts) {
    if (!b.active) {
      slot = &b;
      break;
    }
    if (slot == nullptr || b.age > slot->age) slot = &b;
  }
  *slot = Burst();
  slot->active = true;
  slot->held = held;
  std::copy(pos, pos + 3, slot->pos);
  slot->radius = radius;
}

// A throw at `target`, or with no target at `aimPoint` (manual aim, the crosshair on no one).
void StartThrow(CStateManager& mgr, TUniqueId target, const CVector3f* aimPoint) {
  Throw* slot = &sThrows[0];
  for (Throw& t : sThrows) {
    if (!t.active) {
      slot = &t;
      break;
    }
  }
  *slot = Throw();
  slot->active = true;
  slot->target = target.value;
  if (aimPoint != nullptr) {
    SetVec(slot->goal, *aimPoint);
  } else if (const CPatterned* enemy = LiveEnemy(mgr, target)) {
    SetVec(slot->goal, enemy->GetAimPosition(mgr, 0.f));
  }
  CPlayer* player = mgr.Player();
  CPlayerGun* gun = player != nullptr ? player->PlayerGun() : nullptr;
  if (gun != nullptr && gun->GrappleArm().PortThrow(mgr)) {
    slot->onArm = true;
    std::copy(slot->goal, slot->goal + 3, sArmGoal);
    sArmGoalSet = true;
  } else {
    Launch(*slot, BesideView(mgr));
  }
}

// Homes each charge in on its target and lands it: the hold, a blip and a burst.
// One whose target is gone flies on to where it was and fizzles.
void UpdateThrows(CStateManager& mgr, float dt, const PortStasis::Tuning& tuning) {
  for (Throw& t : sThrows) {
    if (!t.active) continue;
    t.age += dt;
    const CPatterned* enemy = LiveEnemy(mgr, FromRaw(t.target));
    if (enemy != nullptr) SetVec(t.goal, enemy->GetAimPosition(mgr, 0.f));
    if (t.onArm) {
      std::copy(t.goal, t.goal + 3, sArmGoal);
      sArmGoalSet = true;
    }
    if (!t.flying) {
      if (t.age > kWindUpMax) Launch(t, BesideView(mgr));
      continue;
    }
    const int kept = std::min(t.trailCount, kTrail - 1);
    for (int k = kept; k > 0; --k) std::copy(t.trail[k - 1], t.trail[k - 1] + 3, t.trail[k]);
    std::copy(t.pos, t.pos + 3, t.trail[0]);
    t.trailCount = kept + 1;
    bool arrived = PortStasis::StepToward(t.pos, t.goal, kThrowSpeed * dt);
    if (t.target == PortStasis::kNoTarget) {
      // Thrown at no one: the first enemy it passes through this tick catches it.
      const CVector3f from = ToVec(t.trail[0]);
      const CVector3f step = ToVec(t.pos) - from;
      if (step.CanBeNormalized()) {
        float along = 0.f;
        const TUniqueId struck = FirstEnemyOnLine(mgr, from, step.AsNormalized(), step.Magnitude(),
                                                  kCatchTolerance, along);
        if (struck != kInvalidUniqueId) {
          t.target = struck.value;
          enemy = LiveEnemy(mgr, struck);
          SetVec(t.pos, from + step.AsNormalized() * along);
          std::copy(t.pos, t.pos + 3, t.goal);
          arrived = true;
        }
      }
    }
    if (arrived) {
      if (enemy != nullptr) {
        sStasis.Hold(t.target, tuning);
        if (!PortDeadSpaceSound::Play(PortDeadSpaceSound::ECue::StasisHit,
                                      SfxGain() * DistanceGain(mgr, t.goal))) {
          Blip(SFXui_x_freeon_00);
        }
        AddBurst(t.goal, BurstRadius(*enemy), true);
      } else {
        AddBurst(t.goal, 0.5f, false);
      }
      t.active = false;
    } else if (t.age > kFlightMax) {
      AddBurst(t.pos, 0.5f, false);
      t.active = false;
    }
  }
  for (Burst& b : sBursts) {
    if (!b.active) continue;
    b.age += dt;
    if (b.age >= kBurstTime) b.active = false;
  }
}

// Drawing: additive light, so the world shows through. Colours are 0xRRGGBBAA.
struct Rgb {
  uint r, g, b;
};
const Rgb kCoreRgb = {215, 240, 255};
const Rgb kGlowRgb = {70, 150, 255};
const float kGlowRadius = 0.32f;
const float kCoreRadius = 0.11f;
// The tail: glows every kTailStep metres back along the charge's path, to kTailLength.
const float kTailLength = 1.2f;
const float kTailStep = 0.12f;
const int kSides = 16;

uint Pack(const Rgb& c, float alpha) {
  const uint a = static_cast< uint >(std::clamp(alpha, 0.f, 1.f) * 255.f + 0.5f);
  return (c.r << 24) | (c.g << 16) | (c.b << 8) | a;
}

// The unit circle, in kSides steps.
const float* Circle(int axis) {
  static float sCos[kSides + 1];
  static float sSin[kSides + 1];
  static bool sBuilt = false;
  if (!sBuilt) {
    for (int i = 0; i <= kSides; ++i) {
      const float a = 6.28318531f * static_cast< float >(i) / static_cast< float >(kSides);
      sCos[i] = std::cos(a);
      sSin[i] = std::sin(a);
    }
    sBuilt = true;
  }
  return axis == 0 ? sCos : sSin;
}

void Vertex(const CVector3f& p, uint rgba) {
  GXPosition3f32(p.GetX(), p.GetY(), p.GetZ());
  GXColor1u32(rgba);
}

// A disc of light facing the camera: `alpha` at its centre, nothing at `radius`.
void Glow(const CVector3f& center, float radius, const Rgb& rgb, float alpha,
          const CVector3f& right, const CVector3f& up) {
  if (radius <= 0.f || alpha <= 0.f) return;
  const float* cosines = Circle(0);
  const float* sines = Circle(1);
  const uint rim = Pack(rgb, 0.f);
  CGX::Begin(GX_TRIANGLEFAN, GX_VTXFMT0, kSides + 2);
  Vertex(center, Pack(rgb, alpha));
  for (int i = 0; i <= kSides; ++i) {
    Vertex(center + (right * cosines[i] + up * sines[i]) * radius, rim);
  }
  CGX::End();
}

// A band of light around `center`, facing the camera: brightest at `radius`,
// fading to nothing `width` inside it and half that outside.
void Ring(const CVector3f& center, float radius, float width, const Rgb& rgb, float alpha,
          const CVector3f& right, const CVector3f& up) {
  if (radius <= 0.f || alpha <= 0.f) return;
  const float* cosines = Circle(0);
  const float* sines = Circle(1);
  const float edges[3] = {std::max(radius - width, 0.f), radius, radius + 0.5f * width};
  const uint colors[3] = {Pack(rgb, 0.f), Pack(rgb, alpha), Pack(rgb, 0.f)};
  for (int band = 0; band < 2; ++band) {
    CGX::Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (kSides + 1));
    for (int i = 0; i <= kSides; ++i) {
      const CVector3f dir = right * cosines[i] + up * sines[i];
      Vertex(center + dir * edges[band], colors[band]);
      Vertex(center + dir * edges[band + 1], colors[band + 1]);
    }
    CGX::End();
  }
}

void DrawCharge(const Throw& t, const CVector3f& right, const CVector3f& up) {
  const CVector3f pos = ToVec(t.pos);
  // The tail, back along the ticks' positions.
  CVector3f prev = pos;
  float walked = 0.f;
  float nextAt = kTailStep;
  for (int k = 0; k < t.trailCount && nextAt <= kTailLength; ++k) {
    const CVector3f point = ToVec(t.trail[k]);
    const CVector3f segment = point - prev;
    const float length = segment.Magnitude();
    while (length > 0.f && nextAt <= walked + length && nextAt <= kTailLength) {
      const CVector3f at = prev + segment * ((nextAt - walked) / length);
      const float fade = 1.f - nextAt / kTailLength;
      Glow(at, kGlowRadius * (0.35f + 0.55f * fade), kGlowRgb, 0.45f * fade, right, up);
      nextAt += kTailStep;
    }
    walked += length;
    prev = point;
  }
  // The charge, pulsing.
  const float pulse = 1.f + 0.12f * std::sin(t.age * 40.f);
  Glow(pos, kGlowRadius * pulse, kGlowRgb, 0.85f, right, up);
  Glow(pos, kCoreRadius * pulse, kCoreRgb, 1.f, right, up);
}

void DrawBurst(const Burst& b, const CVector3f& right, const CVector3f& up) {
  const CVector3f pos = ToVec(b.pos);
  const float t = std::clamp(b.age / kBurstTime, 0.f, 1.f);
  const float grow = 1.f - (1.f - t) * (1.f - t);
  const float strength = b.held ? 1.f : 0.5f;
  const float fade = 1.f - t;
  Ring(pos, b.radius * (0.3f + 0.7f * grow), 0.35f * b.radius, kGlowRgb, 0.9f * strength * fade,
       right, up);
  // The flash where it hit, over the first part of the burst.
  const float flash = t / 0.4f;
  if (flash < 1.f) {
    Glow(pos, b.radius * (0.4f + 0.6f * flash), kCoreRgb, 0.7f * strength * (1.f - flash), right,
         up);
  }
}

} // namespace

void Tick(CStateManager& mgr, float dt) {
  const bool pressed = PortDebug::ConsumeStasisRequest();
  if (&mgr != sOwner) {
    sStasis = PortStasis::State();
    sRefusedFlash = 0.f;
    ClearThrows();
    sOwner = &mgr;
  }
  if (!PortDebug::Stasis()) {
    sStasis.ReleaseAll();
    sRefusedFlash = 0.f;
    ClearThrows();
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
  UpdateThrows(mgr, dt, tuning);
  if (!pressed) return;

  TUniqueId target = PressTarget(mgr);
  // Manual aim: thrown along the crosshair, at the enemy it is on or else at where it points.
  CVector3f aimPoint(0.f, 0.f, 0.f);
  bool atCrosshair = false;
  if (target == kInvalidUniqueId && PortDebug::ManualAim() && Unmorphed(mgr)) {
    target = CrosshairTarget(mgr, aimPoint);
    atCrosshair = target == kInvalidUniqueId;
  }
  const uint16_t raw = target == kInvalidUniqueId ? PortStasis::kNoTarget : target.value;
  switch (atCrosshair ? sStasis.Spend(tuning) : sStasis.Throw(raw, tuning)) {
  case PortStasis::EResult::Thrown:
    StartThrow(mgr, target, atCrosshair ? &aimPoint : nullptr);
    break;
  case PortStasis::EResult::NoTarget:
  case PortStasis::EResult::NoEnergy:
    sRefusedFlash = kRefusedFlashTime;
    Blip(SFXui_x_freeoff_00);
    break;
  default:
    break;
  }
}

void ReleaseThrow(CStateManager& mgr, const CVector3f* hand) {
  for (Throw& t : sThrows) {
    if (t.active && t.onArm && !t.flying) {
      Launch(t, hand != nullptr ? *hand : BesideView(mgr));
      return;
    }
  }
}

bool ThrowSoundReplacesArm() {
  return PortDeadSpaceSound::Available(PortDeadSpaceSound::ECue::StasisFire);
}

CVector3f ThrowDirection(const CStateManager& mgr, const CVector3f& from) {
  if (sArmGoalSet) {
    const CVector3f toGoal = ToVec(sArmGoal) - from;
    if (toGoal.CanBeNormalized()) return toGoal.AsNormalized();
  }
  return mgr.GetCameraManager()->GetCurrentCamera(mgr).GetTransform().GetForward();
}

void DrawWorldEffects(const CStateManager& mgr) {
  bool any = false;
  for (const Throw& t : sThrows) any = any || (t.active && t.flying);
  for (const Burst& b : sBursts) any = any || b.active;
  if (!any || &mgr != sOwner) return;

  // Unlit vertex colour added to what's drawn, depth-tested against the rooms
  // without writing depth, as CRainSplashGenerator draws its splashes.
  static const GXVtxDescList kVertex[] = {
      {GX_VA_POS, GX_DIRECT},
      {GX_VA_CLR0, GX_DIRECT},
      {GX_VA_NULL, GX_NONE},
  };
  CGX::SetVtxDescv(kVertex);
  CGX::SetNumChans(1);
  CGX::SetNumTexGens(0);
  CGX::SetNumTevStages(1);
  CGX::SetChanCtrl(CGX::Channel0, false, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE,
                   GX_AF_NONE);
  CGX::SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  CGraphics::SetTevOp(kTS_Stage0, CGraphics::kEnvPassthru);
  CGraphics::SetTevOp(kTS_Stage1, CGraphics::kEnvPassthru);
  CGX::SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
  CGX::SetZMode(true, GX_LEQUAL, false);
  CGraphics::SetCullMode(kCM_None);
  CGraphics::SetModelMatrix(CTransform4f::Identity());

  // Facing the camera: its right and up.
  const CTransform4f view = CGraphics::GetViewMatrix();
  const CVector3f right = view.GetRight();
  const CVector3f up = view.GetUp();
  for (const Throw& t : sThrows) {
    if (t.active && t.flying) DrawCharge(t, right, up);
  }
  for (const Burst& b : sBursts) {
    if (b.active) DrawBurst(b, right, up);
  }
  CGraphics::SetCullMode(kCM_Front);
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
  ClearThrows();
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
