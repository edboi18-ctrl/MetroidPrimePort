#ifndef _CGUITEXTPANE
#define _CGUITEXTPANE

#include "GuiSys/CGuiPane.hpp"

#include "GuiSys/CGuiTextSupport.hpp"

#include "Kyoto/SObjectTag.hpp"

class IObjectStore;
class CSimplePool;
class CColor;
class CGuiTextProperties;

class CGuiTextPane : public CGuiPane {
public:
#if VERSION >= VERSION_GM8P_00 && VERSION != VERSION_GM8E_02
  static CGuiWidget* Create(CGuiFrame* frame, CInputStream& in, CSimplePool* sp, uint version = 0);
  CGuiTextPane(const CGuiWidgetParms& parms, CSimplePool* sp, const float dimX, const float dimY,
              const CVector3f& vec, const CAssetId fontId, const CGuiTextProperties& props,
              const CColor& col1, const CColor& col2, const int padX, const int padY,
              CAssetId jpFontId, int jpExtentX, int jpExtentY);
#else
  static CGuiWidget* Create(CGuiFrame* frame, CInputStream& in, CSimplePool* sp);
#ifdef TARGET_PC
  // The header version of the frame being loaded. PAL frames (version 1)
  // add a Japanese font and extents to each text pane, which 1.00 skips.
  static uint sPortFrameVersion;
#endif
  CGuiTextPane(const CGuiWidgetParms& parms, CSimplePool* sp, const float dimX, const float dimY,
              const CVector3f& vec, const CAssetId fontId, const CGuiTextProperties& props,
              const CColor& col1, const CColor& col2, const int padX, const int padY);
#endif
  ~CGuiTextPane();

  FourCC GetWidgetTypeID() const override { return 'TXPN'; }
  virtual rstl::vector< CAssetId > GetFontAssets() const;

  CGuiTextSupport& TextSupport() { return xd4_textSupport; }
  const CGuiTextSupport& GetTextSupport() const { return xd4_textSupport; }
  void Update(float dt) override;
  bool GetIsFinishedLoadingWidgetSpecific() const override;
  void SetDimensions(const CVector2f& dim, bool initVBO) override;
  void ScaleDimensions(const CVector3f& scale) override;
  void Draw(const CGuiWidgetDrawParms& parms) const override;
  // Port: moves the text by `offset`, in the pane's own space, when it is drawn, leaving the
  // frame's layout as loaded. CHudEnergyInterface keeps the low-energy warning clear of Dead
  // Space Prime's Stasis meter with it.
  void PortSetDrawOffset(const CVector3f& offset) { mPortDrawOffset = offset; }

private:
#if VERSION >= VERSION_GM8P_00 && VERSION != VERSION_GM8E_02
  static bool sDrawPaneRects;
#endif
  mutable CGuiTextSupport xd4_textSupport;
#if VERSION >= VERSION_GM8P_00 && VERSION != VERSION_GM8E_02
  bool xd00_drawShadow;
#endif
  CVector3f mPortDrawOffset;
};

CHECK_SIZEOF(CGuiTextPane, (VERSION >= VERSION_GM8P_00 && VERSION != VERSION_GM8E_02 ? 0xd04 : 0x3e0))

#endif // _CGUITEXTPANE
