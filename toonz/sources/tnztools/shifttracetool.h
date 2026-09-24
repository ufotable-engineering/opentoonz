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
    CurveBendPlusGadget,
    CurveBendMinusGadget,
    MoveCenterGadget,
    RotateGadget,
    TranslateGadget,
    ScaleGadget
  };
  inline bool isCurveGadget(GadgetId id) const {
    return CurveP0Gadget <= id && id <= CurveBendMinusGadget;
  }

private:
  struct Trajectory {
    bool isArc;
    TPointD center;
    double radius, angle0, sweep;
  };

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
  TBoolProperty m_rotateAlongArc;
  TStringProperty m_snapRatio;
  int m_snapDivision, m_snapNumerator;
  // Trajectory shape: signed distance of the arc apex from the p0-p1 chord
  double m_bend;
  // Position of p2 along the trajectory (0 = p0, 1 = p1)
  double m_ratio;

  TPointD chordNormal() const;
  TPointD arcApex() const;
  double bendHandleOffset() const;
  Trajectory getTrajectory() const;
  TPointD trajectoryPoint(const Trajectory &t, double ratio) const;
  TPointD trajectoryPoint(double ratio) const;
  double trajectoryRatio(const TPointD &pos) const;
  double snapRatio(double ratio) const;
  double snapOrJump(double ratio) const;
  void parseSnapRatio();

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