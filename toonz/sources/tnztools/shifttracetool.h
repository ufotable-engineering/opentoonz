#pragma once

#include "tools/tool.h"
#include "tproperty.h"

class ShiftTraceTool final : public TTool {
public:
  enum CurveStatus {
    NoCurve,
    TwoPointsCurve,  // just during the first click&drag
    ThreePointsCurve
  };

  enum GadgetId {
    NoGadget,
    NoGadget_InBox,
    CurveP0Gadget,
    CurveP1Gadget,
    CurvePmGadget,
    MoveCenterGadget,
    RotateGadget,
    TranslateGadget,
    ScaleGadget
  };
  inline bool isCurveGadget(GadgetId id) const {
    return CurveP0Gadget <= id && id <= CurvePmGadget;
  }

private:
  TPointD m_oldPos, m_startPos;
  int m_ghostIndex;
  TPointD m_p0, m_p1, m_p2;

  CurveStatus m_curveStatus;
  GadgetId m_gadget;
  GadgetId m_highlightedGadget;

  TRectD m_box;
  TAffine m_dpiAff;
  int m_row[2];
  TAffine m_aff[2];
  TPointD m_center[2];

  TAffine m_oldAff;

  TPropertyGroup m_prop;
  TBoolProperty m_straightTrajectory;

  bool getArcCenter(TPointD &center) const;

public:
  ShiftTraceTool();

  ToolType getToolType() const override { return GenericTool; }

  void clearData();
  void updateData();
  void updateBox();
  void updateCurveAffs();
  void updateCurveCenters();
  void updateGhost();

  void reset() override;

  void mouseMove(const TPointD &, const TMouseEvent &e) override;
  void leftButtonDown(const TPointD &, const TMouseEvent &) override;
  void leftButtonDrag(const TPointD &, const TMouseEvent &) override;
  void leftButtonUp(const TPointD &, const TMouseEvent &) override;
  void draw() override;

  TAffine getGhostAff();
  GadgetId getGadget(const TPointD &);
  void drawDot(const TPointD &center, double r,
               const TPixel32 &color = TPixel32::White);
  void drawControlRect();
  void drawCurve();

  void onActivate() override;
  void onDeactivate() override;

  void onLeave() override;

  bool isEventAcceptable(QEvent *e) override;

  int getCursorId() const override;

  TPropertyGroup *getProperties(int targetType) override { return &m_prop; }
  bool onPropertyChanged(std::string propertyName) override;
  void updateTranslation() override;

  int getCurrentGhostIndex() { return m_ghostIndex; }
  void setCurrentGhostIndex(int index);
};