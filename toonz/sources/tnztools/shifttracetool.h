#pragma once

#include "tools/tool.h"

#include "toonz/shifttraceedit.h"
#include "toonz/shifttraceresolver.h"

#include <vector>

class ShiftTraceTool final : public TTool {
public:
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
  GadgetId m_gadget;
  GadgetId m_highlightedGadget;

  // Derived from the layout on every draw
  TRectD m_box;
  TAffine m_dpiAff;
  std::vector<ShiftTraceResolvedGhost> m_resolved;

  TAffine m_oldAff;
  ShiftTraceDragSession m_session;

public:
  ShiftTraceTool();

  ToolType getToolType() const override { return GenericTool; }

  void clearDerivedData();
  void updateData(const ShiftTraceLayout &layout, int activeGhostId);
  void updateBox(int ghostId);
  void applyCurve(ShiftTraceLayout &layout);

  void reset() override;

  void mouseMove(const TPointD &, const TMouseEvent &e) override;
  void leftButtonDown(const TPointD &, const TMouseEvent &) override;
  void leftButtonDrag(const TPointD &, const TMouseEvent &) override;
  void leftButtonUp(const TPointD &, const TMouseEvent &) override;
  void draw() override;

  GadgetId getGadget(const TPointD &);
  void drawDot(const TPointD &center, double r,
               const TPixel32 &color = TPixel32::White);
  void drawControlRect(const ShiftTraceLayout &layout, int activeGhostId);
  void drawCurve(const ShiftTraceLayout &layout);

  void onActivate() override;
  void onDeactivate() override;

  void onLeave() override;

  bool isEventAcceptable(QEvent *e) override;

  int getCursorId() const override;

  int getActiveGhostId() const;
  void setActiveGhostId(int ghostId);

private:
  const ShiftTraceResolvedGhost *findResolved(int ghostId) const;
  void storeActiveGhostId(int ghostId);
};
