

#include "shifttracetool.h"
#include "toonz/onionskinmask.h"
#include "toonz/tonionskinmaskhandle.h"
#include "tools/cursors.h"
#include "timage.h"
#include "trasterimage.h"
#include "ttoonzimage.h"
#include "tvectorimage.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txsheethandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/txshlevelhandle.h"
#include "tools/toolhandle.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/dpiscale.h"
#include "toonz/stage.h"
#include "tpixel.h"
#include "toonzqt/menubarcommand.h"

#include "toonz/preferences.h"
#include "toonzqt/gutil.h"

#include "tgl.h"
#include <math.h>
#include <QKeyEvent>

//=============================================================================

static bool circumCenter(TPointD &out, const TPointD &a, const TPointD &b,
                         const TPointD &c) {
  double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
  if (fabs(d) < 0.0001) {
    out = TPointD();
    return false;
  }
  out.x = ((a.y * a.y + a.x * a.x) * (b.y - c.y) +
           (b.y * b.y + b.x * b.x) * (c.y - a.y) +
           (c.y * c.y + c.x * c.x) * (a.y - b.y)) /
          d;
  out.y = ((a.y * a.y + a.x * a.x) * (c.x - b.x) +
           (b.y * b.y + b.x * b.x) * (a.x - c.x) +
           (c.y * c.y + c.x * c.x) * (b.x - a.x)) /
          d;
  return true;
}

//=============================================================================

ShiftTraceTool::ShiftTraceTool()
    : TTool("T_ShiftTrace"), m_gadget(NoGadget), m_highlightedGadget(NoGadget) {
  bind(TTool::AllTargets);  // Deals with tool deactivation internally
}

void ShiftTraceTool::clearDerivedData() {
  m_gadget            = NoGadget;
  m_highlightedGadget = NoGadget;

  m_box = TRectD();
  m_resolved.clear();
}

const ShiftTraceResolvedGhost *ShiftTraceTool::findResolved(int ghostId) const {
  for (const ShiftTraceResolvedGhost &rg : m_resolved)
    if (rg.m_ghostId == ghostId) return &rg;
  return nullptr;
}

void ShiftTraceTool::storeActiveGhostId(int ghostId) {
  ShiftTraceEdit::editState(
      TTool::getApplication()->getCurrentOnionSkin(),
      [ghostId](ShiftTraceState &state) { state.setActiveGhostId(ghostId); },
      ShiftTraceEdit::Notify::None);
}

void ShiftTraceTool::updateBox(int ghostId) {
  const ShiftTraceResolvedGhost *rg = findResolved(ghostId);
  if (!rg || !rg->m_valid || rg->m_row < 0) return;

  TImageP img;

  TApplication *app = TTool::getApplication();
  if (app->getCurrentFrame()->isEditingScene()) {
    TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

    int col             = app->getCurrentColumn()->getColumnIndex();
    TXshCell cell       = xsh->getCell(rg->m_row, col);
    TXshSimpleLevel *sl = cell.getSimpleLevel();
    if (sl) {
      m_dpiAff = getDpiAffine(sl, cell.m_frameId);
      img      = cell.getImage(false);
    }
  }
  // on editing level
  else {
    TXshLevel *level = app->getCurrentLevel()->getLevel();
    if (!level) return;
    TXshSimpleLevel *sl = level->getSimpleLevel();
    if (!sl) return;

    const TFrameId &ghostFid = sl->index2fid(rg->m_row);
    m_dpiAff                 = getDpiAffine(sl, ghostFid);
    img                      = sl->getFrame(ghostFid, false);
  }

  if (img) {
    if (TRasterImageP ri = img) {
      TRasterP ras = ri->getRaster();
      m_box        = (convert(ras->getBounds()) - ras->getCenterD()) *
              ri->getSubsampling();
    } else if (TToonzImageP ti = img) {
      TRasterP ras = ti->getRaster();
      m_box        = (convert(ras->getBounds()) - ras->getCenterD()) *
              ti->getSubsampling();
    } else if (TVectorImageP vi = img) {
      m_box = vi->getBBox();
    }
  }
}

void ShiftTraceTool::updateData(const ShiftTraceLayout &layout,
                                int activeGhostId) {
  m_box = TRectD();
  m_resolved.clear();
  m_dpiAff          = TAffine();
  TApplication *app = TTool::getApplication();

  if (app->getCurrentFrame()->isEditingScene()) {
    TXsheet *xsh                 = app->getCurrentXsheet()->getXsheet();
    int row                      = app->getCurrentFrame()->getFrame();
    int col                      = app->getCurrentColumn()->getColumnIndex();
    ShiftTraceCellGetter getCell = ShiftTraceResolver::makeCellGetter(xsh);
    for (int i = 0; i < layout.getGhostCount(); ++i)
      m_resolved.push_back(
          ShiftTraceResolver::resolveInXsheet(layout, i, row, col, getCell));
  }
  // on editing level
  else {
    TXshLevel *level = app->getCurrentLevel()->getLevel();
    if (level) {
      TXshSimpleLevel *sl = level->getSimpleLevel();
      if (sl) {
        TFrameId fid = app->getCurrentFrame()->getFid();
        int row      = sl->guessIndex(fid);
        for (int i = 0; i < layout.getGhostCount(); ++i)
          m_resolved.push_back(
              ShiftTraceResolver::resolveInLevel(layout, i, row));
      }
    }
  }
  updateBox(activeGhostId);
}

//
// Compute the affines of the previous and following ghosts according to the
// current curve
//
void ShiftTraceTool::applyCurve(ShiftTraceLayout &layout) {
  ShiftTraceGhost *previous  = layout.findGhost(ShiftTrace::kPreviousGhostId);
  ShiftTraceGhost *following = layout.findGhost(ShiftTrace::kFollowingGhostId);
  const ShiftTraceCurve &curve = layout.getCurve();

  if (curve.m_status != ShiftTraceCurve::Status::ThreePoints) {
    if (previous) previous->m_aff = TAffine();
    if (following) following->m_aff = TAffine();
    return;
  }

  double phi0 = 0, phi1 = 0;
  TPointD center;
  if (circumCenter(center, curve.m_p0, curve.m_p1, curve.m_p2)) {
    TPointD v0 = normalize(curve.m_p0 - center);
    TPointD v1 = normalize(curve.m_p1 - center);
    TPointD v2 = normalize(curve.m_p2 - center);
    TPointD u0(-v0.y, v0.x);
    TPointD u1(-v1.y, v1.x);
    phi0 = atan2((v2 * u0), (v2 * v0)) * 180.0 / 3.1415;
    phi1 = atan2((v2 * u1), (v2 * v1)) * 180.0 / 3.1415;
  }
  if (previous)
    previous->m_aff =
        TTranslation(curve.m_p2 - curve.m_p0) * TRotation(curve.m_p0, phi0);
  if (following)
    following->m_aff =
        TTranslation(curve.m_p2 - curve.m_p1) * TRotation(curve.m_p1, phi1);
}

void ShiftTraceTool::reset() {
  int ghostId = getActiveGhostId();
  onActivate();
  invalidate();
  storeActiveGhostId(ghostId);

  TTool::getApplication()
      ->getCurrentTool()
      ->notifyToolChanged();  // Refreshes toolbar values
}

void ShiftTraceTool::drawDot(const TPointD &center, double r,
                             const TPixel32 &color) {
  tglColor(color);
  tglDrawDisk(center, r);
  glColor3d(0.2, 0.2, 0.2);
  tglDrawCircle(center, r);
}

void ShiftTraceTool::drawControlRect(const ShiftTraceLayout &layout,
                                     int activeGhostId) {
  const ShiftTraceGhost *ghost = layout.findGhost(activeGhostId);
  if (!ghost) return;
  const ShiftTraceResolvedGhost *rg = findResolved(activeGhostId);
  if (!rg || !rg->m_valid || rg->m_row < 0) return;

  TRectD box = m_box;
  if (box.isEmpty()) return;
  glPushMatrix();
  tglMultMatrix(ghost->m_aff * m_dpiAff);

  TPixel32 color;

  // draw onion-colored rectangle to indicate which ghost is grabbed
  {
    TPixel32 frontOniColor, backOniColor;
    bool inksOnly;
    Preferences::instance()->getOnionData(frontOniColor, backOniColor,
                                          inksOnly);
    color       = (rg->m_onionSkinDistance < 0) ? backOniColor : frontOniColor;
    double unit = sqrt(tglGetPixelSize2());
    unit *= getDevicePixelRatio(m_viewer->viewerWidget());
    TRectD coloredBox = box.enlarge(3.0 * unit);
    tglColor(color);
    glBegin(GL_LINE_STRIP);
    glVertex2d(coloredBox.x0, coloredBox.y0);
    glVertex2d(coloredBox.x1, coloredBox.y0);
    glVertex2d(coloredBox.x1, coloredBox.y1);
    glVertex2d(coloredBox.x0, coloredBox.y1);
    glVertex2d(coloredBox.x0, coloredBox.y0);
    glEnd();
  }

  color = m_highlightedGadget == TranslateGadget ? TPixel32(200, 100, 100)
          : m_highlightedGadget == RotateGadget  ? TPixel32(100, 200, 100)
                                                 : TPixel32(120, 120, 120);
  tglColor(color);
  glBegin(GL_LINE_STRIP);
  glVertex2d(box.x0, box.y0);
  glVertex2d(box.x1, box.y0);
  glVertex2d(box.x1, box.y1);
  glVertex2d(box.x0, box.y1);
  glVertex2d(box.x0, box.y0);
  glEnd();
  color    = m_highlightedGadget == ScaleGadget ? TPixel32(200, 100, 100)
                                                : TPixel32::White;
  double r = 4 * sqrt(tglGetPixelSize2());
  drawDot(box.getP00(), r, color);
  drawDot(box.getP01(), r, color);
  drawDot(box.getP10(), r, color);
  drawDot(box.getP11(), r, color);
  if (layout.getCurve().m_status == ShiftTraceCurve::Status::None) {
    color = m_highlightedGadget == MoveCenterGadget ? TPixel32(200, 100, 100)
                                                    : TPixel32::White;
    // The pivot lives in the input space of m_aff; bring it back to image
    // pixels, which is the space of the matrix pushed above.
    TPointD c = m_dpiAff.inv() * ghost->m_pivot;
    drawDot(c, r, color);
  }
  glPopMatrix();
}

void ShiftTraceTool::drawCurve(const ShiftTraceLayout &layout) {
  const ShiftTraceCurve &curve = layout.getCurve();
  if (curve.m_status == ShiftTraceCurve::Status::None) return;
  double r = 4 * sqrt(tglGetPixelSize2());
  double u = getPixelSize();
  if (curve.m_status == ShiftTraceCurve::Status::TwoPoints) {
    TPixel32 color = m_highlightedGadget == CurveP0Gadget
                         ? TPixel32(200, 100, 100)
                         : TPixel32::White;
    drawDot(curve.m_p0, r, color);
    glColor3d(0.2, 0.2, 0.2);
    tglDrawSegment(curve.m_p0, curve.m_p1);
    drawDot(curve.m_p1, r, TPixel32::Red);
  } else if (curve.m_status == ShiftTraceCurve::Status::ThreePoints) {
    TPixel32 color = m_highlightedGadget == CurveP0Gadget
                         ? TPixel32(200, 100, 100)
                         : TPixel32::White;
    drawDot(curve.m_p0, r, color);
    color = m_highlightedGadget == CurveP1Gadget ? TPixel32(200, 100, 100)
                                                 : TPixel32::White;
    drawDot(curve.m_p1, r, color);

    glColor3d(0.2, 0.2, 0.2);

    TPointD center;
    if (circumCenter(center, curve.m_p0, curve.m_p1, curve.m_p2)) {
      double radius = norm(center - curve.m_p1);
      glBegin(GL_LINE_STRIP);
      int n = 100;
      for (int i = 0; i < n; i++) {
        double t  = (double)i / n;
        TPointD p = (1 - t) * curve.m_p0 + t * curve.m_p2;
        p         = center + radius * normalize(p - center);
        tglVertex(p);
      }
      for (int i = 0; i < n; i++) {
        double t  = (double)i / n;
        TPointD p = (1 - t) * curve.m_p2 + t * curve.m_p1;
        p         = center + radius * normalize(p - center);
        tglVertex(p);
      }
      glEnd();
    } else {
      tglDrawSegment(curve.m_p0, curve.m_p1);
    }
    color = m_highlightedGadget == CurvePmGadget ? TPixel32(200, 100, 100)
                                                 : TPixel32::White;
    drawDot(curve.m_p2, r, color);
  }
}

void ShiftTraceTool::onActivate() {
  clearDerivedData();

  TApplication *app = TTool::getApplication();
  storeActiveGhostId(ShiftTrace::kPreviousGhostId);

  // The curve is dropped on activation, but its points and the ghost
  // transforms it produced are kept.
  ShiftTraceLayoutView view = ShiftTraceEdit::currentLayout(app);
  if (view->getCurve().m_status != ShiftTraceCurve::Status::None)
    ShiftTraceEdit::editCurrentLayout(
        app,
        [](ShiftTraceLayout &layout) {
          ShiftTraceCurve curve = layout.getCurve();
          curve.m_status        = ShiftTraceCurve::Status::None;
          layout.setCurve(curve);
        },
        ShiftTraceEdit::Notify::None);
}

void ShiftTraceTool::onDeactivate() {
  if (m_session.isActive()) m_session.commit();

  // Deactivating Shift and Trace mode resets the pseudo tool with keeping the
  // Edit Shift checkbox unchanged
  QAction *shiftTrace = CommandManager::instance()->getAction("MI_ShiftTrace");
  if (!shiftTrace->isChecked()) return;
  QAction *action = CommandManager::instance()->getAction("MI_EditShift");
  action->setChecked(false);
  action = CommandManager::instance()->getAction("MI_NoShift");
  action->setEnabled(true);

  TApplication *app = TTool::getApplication();
  OnionSkinMask osm = app->getCurrentOnionSkin()->getOnionSkinMask();
  if (osm.isEditingShift()) {
    osm.setShiftTraceStatus(OnionSkinMask::ENABLED);
    TTool::getApplication()->getCurrentOnionSkin()->setOnionSkinMask(osm);
  }
}

ShiftTraceTool::GadgetId ShiftTraceTool::getGadget(const TPointD &p) {
  ShiftTraceLayoutView view =
      ShiftTraceEdit::currentLayout(TTool::getApplication());
  const ShiftTraceCurve &curve = view->getCurve();
  const ShiftTraceGhost *ghost =
      view->findGhost(view.m_state->getActiveGhostId());

  std::vector<std::pair<TPointD, GadgetId>> gadgets;
  gadgets.push_back(std::make_pair(curve.m_p0, CurveP0Gadget));
  gadgets.push_back(std::make_pair(curve.m_p1, CurveP1Gadget));
  gadgets.push_back(std::make_pair(curve.m_p2, CurvePmGadget));
  TAffine aff      = ghost ? ghost->m_aff * m_dpiAff : TAffine();
  double pixelSize = getPixelSize();
  double d         = 15 * pixelSize;  // offset for rotation handle
  if (ghost) {
    gadgets.push_back(std::make_pair(aff * m_box.getP00(), ScaleGadget));
    gadgets.push_back(std::make_pair(aff * m_box.getP01(), ScaleGadget));
    gadgets.push_back(std::make_pair(aff * m_box.getP10(), ScaleGadget));
    gadgets.push_back(std::make_pair(aff * m_box.getP11(), ScaleGadget));
    gadgets.push_back(
        std::make_pair(ghost->m_aff * ghost->m_pivot, MoveCenterGadget));
  }
  int k           = -1;
  double minDist2 = pow(10 * pixelSize, 2);
  for (int i = 0; i < (int)gadgets.size(); i++) {
    double d2 = norm2(gadgets[i].first - p);
    if (d2 < minDist2) {
      minDist2 = d2;
      k        = i;
    }
  }
  if (k >= 0) return gadgets[k].second;

  // rect-point
  if (ghost) {
    TPointD q  = aff.inv() * p;
    double big = 1.0e6;
    double d = big, x = 0, y = 0;
    if (m_box.x0 < q.x && q.x < m_box.x1) {
      x         = q.x;
      double d0 = fabs(m_box.y0 - q.y);
      double d1 = fabs(m_box.y1 - q.y);
      if (d0 < d1) {
        d = d0;
        y = m_box.y0;
      } else {
        d = d1;
        y = m_box.y1;
      }
    }
    if (m_box.y0 < q.y && q.y < m_box.y1) {
      double d0 = fabs(m_box.x0 - q.x);
      double d1 = fabs(m_box.x1 - q.x);
      if (d0 < d) {
        d = d0;
        y = q.y;
        x = m_box.x0;
      }
      if (d1 < d) {
        d = d1;
        y = q.y;
        x = m_box.x1;
      }
    }
    if (d < big) {
      TPointD pp = aff * TPointD(x, y);
      double d   = norm(p - pp);
      if (d < 10 * getPixelSize()) {
        if (m_box.contains(q))
          return TranslateGadget;
        else
          return RotateGadget;
      }
    }
    if (m_box.contains(q))
      return NoGadget_InBox;
    else
      return NoGadget;
  }
  return NoGadget;
}

void ShiftTraceTool::mouseMove(const TPointD &pos, const TMouseEvent &e) {
  GadgetId highlightedGadget = getGadget(pos);
  if (highlightedGadget != m_highlightedGadget) {
    m_highlightedGadget = highlightedGadget;
    invalidate();
  }
}

void ShiftTraceTool::leftButtonDown(const TPointD &pos, const TMouseEvent &e) {
  TApplication *app = TTool::getApplication();
  m_session.begin(app);

  m_gadget = m_highlightedGadget;
  m_oldPos = m_startPos = pos;

  bool notify = false;

  ShiftTraceLayout &layout = m_session.workingLayout();

  if (!e.isCtrlPressed() &&
      (m_gadget == NoGadget || m_gadget == NoGadget_InBox)) {
    if (m_gadget == NoGadget_InBox) {
      m_gadget = TranslateGadget;
    } else {
      m_gadget = RotateGadget;
    }

    int row = getViewer()->posToRow(e.m_pos, 5.0, false, true);
    if (row >= 0) {
      int ghostId = -1;
      if (app->getCurrentFrame()->isEditingScene()) {
        int currentRow = getFrame();
        const ShiftTraceResolvedGhost *previous =
            findResolved(ShiftTrace::kPreviousGhostId);
        const ShiftTraceResolvedGhost *following =
            findResolved(ShiftTrace::kFollowingGhostId);
        if (previous && previous->m_valid && row < currentRow)
          ghostId = ShiftTrace::kPreviousGhostId;
        else if (following && following->m_valid && row > currentRow)
          ghostId = ShiftTrace::kFollowingGhostId;
      } else {
        for (const ShiftTraceResolvedGhost &rg : m_resolved) {
          if (rg.m_valid && rg.m_row == row) {
            ghostId = rg.m_ghostId;
            break;
          }
        }
      }

      if (ghostId >= 0) {
        m_session.working().setActiveGhostId(ghostId);
        updateBox(ghostId);
        m_session.preview();
        m_gadget            = TranslateGadget;
        m_highlightedGadget = TranslateGadget;
        notify              = true;
      }
    }
  } else if (e.isCtrlPressed()) {
    m_gadget = NoGadget_InBox;
  }

  const ShiftTraceGhost *ghost =
      layout.findGhost(m_session.working().getActiveGhostId());
  m_oldAff = ghost ? ghost->m_aff : TAffine();
  invalidate();

  if (notify) {
    TTool::getApplication()
        ->getCurrentTool()
        ->notifyToolChanged();  // Refreshes toolbar values
  }
}

void ShiftTraceTool::leftButtonDrag(const TPointD &pos, const TMouseEvent &e) {
  if (!m_session.isActive()) return;

  ShiftTraceLayout &layout = m_session.workingLayout();
  ShiftTraceGhost *ghost =
      layout.findGhost(m_session.working().getActiveGhostId());
  ShiftTraceCurve curve = layout.getCurve();

  if (m_gadget == NoGadget || m_gadget == NoGadget_InBox) {
    if (norm(pos - m_oldPos) > 10 * getPixelSize()) {
      curve.m_status = ShiftTraceCurve::Status::TwoPoints;
      curve.m_p0     = m_oldPos;
      m_gadget       = CurveP1Gadget;
    }
  }

  if (isCurveGadget(m_gadget)) {
    if (m_gadget == CurveP0Gadget)
      curve.m_p0 = pos;
    else if (m_gadget == CurveP1Gadget)
      curve.m_p1 = pos;
    else
      curve.m_p2 = pos;
    layout.setCurve(curve);
    applyCurve(layout);
  } else if (!ghost) {
    // nothing to drag
  } else if (m_gadget == RotateGadget) {
    TPointD c = ghost->m_aff * ghost->m_pivot;
    TPointD a = m_oldPos - c;
    TPointD b = pos - c;
    m_oldPos  = pos;
    TPointD u = normalize(a);
    double phi =
        atan2(-u.y * b.x + u.x * b.y, u.x * b.x + u.y * b.y) * 180.0 / 3.14153;

    ghost->m_aff = TRotation(c, phi) * ghost->m_aff;
  } else if (m_gadget == MoveCenterGadget) {
    TAffine inv   = ghost->m_aff.inv();
    TPointD delta = inv * pos - inv * m_oldPos;
    m_oldPos      = pos;
    ghost->m_pivot += delta;
  } else if (m_gadget == TranslateGadget) {
    TPointD delta = pos - m_oldPos;
    m_oldPos      = pos;
    ghost->m_aff  = TTranslation(delta) * ghost->m_aff;
  } else if (m_gadget == ScaleGadget) {
    TAffine inv  = ghost->m_aff.inv();
    TPointD c    = ghost->m_pivot;
    TPointD a    = inv * m_oldPos - c;
    TPointD b    = inv * pos - c;
    TPointD imgC = ghost->m_aff * ghost->m_pivot;

    if (e.isShiftPressed())
      ghost->m_aff = TScale(imgC, b.x / a.x, b.y / a.y) * m_oldAff;
    else {
      double scale = std::max(b.x / a.x, b.y / a.y);
      ghost->m_aff = TScale(imgC, scale) * m_oldAff;
    }
  }

  m_session.preview();
  invalidate();
}

void ShiftTraceTool::leftButtonUp(const TPointD &pos, const TMouseEvent &) {
  if (m_session.isActive()) {
    ShiftTraceLayout &layout = m_session.workingLayout();
    if (isCurveGadget(m_gadget)) {
      ShiftTraceCurve curve = layout.getCurve();
      if (curve.m_status == ShiftTraceCurve::Status::TwoPoints) {
        curve.m_p2     = (curve.m_p0 + curve.m_p1) * 0.5;
        curve.m_status = ShiftTraceCurve::Status::ThreePoints;
        layout.setCurve(curve);
        applyCurve(layout);

        // Each ghost rotates about the curve midpoint from now on.
        const int ids[] = {ShiftTrace::kPreviousGhostId,
                           ShiftTrace::kFollowingGhostId};
        for (int id : ids)
          if (ShiftTraceGhost *ghost = layout.findGhost(id))
            ghost->m_pivot = ghost->m_aff.inv() * curve.m_p2;
      }
    }
    m_session.commit();
  }
  m_gadget = NoGadget;
  invalidate();

  TTool::getApplication()
      ->getCurrentTool()
      ->notifyToolChanged();  // Refreshes toolbar values
}

void ShiftTraceTool::draw() {
  ShiftTraceLayoutView view =
      ShiftTraceEdit::currentLayout(TTool::getApplication());
  int activeGhostId = view.m_state->getActiveGhostId();
  updateData(*view, activeGhostId);
  drawControlRect(*view, activeGhostId);
  drawCurve(*view);
}

int ShiftTraceTool::getCursorId() const {
  if (m_highlightedGadget == RotateGadget || m_highlightedGadget == NoGadget)
    return ToolCursor::RotateCursor;
  else if (m_highlightedGadget == ScaleGadget)
    return ToolCursor::ScaleCursor;
  else if (isCurveGadget(m_highlightedGadget))
    return ToolCursor::PinchCursor;
  else  // Curve Points, TranslateGadget, NoGadget_InBox
    return ToolCursor::MoveCursor;
}

bool ShiftTraceTool::isEventAcceptable(QEvent *e) {
  // F1, F2 and F3 keys are used for flipping
  QKeyEvent *keyEvent = static_cast<QKeyEvent *>(e);
  int key             = keyEvent->key();
  return (Qt::Key_F1 <= key && key <= Qt::Key_F3);
}

void ShiftTraceTool::onLeave() {
  OnionSkinMask osm =
      TTool::getApplication()->getCurrentOnionSkin()->getOnionSkinMask();
  osm.clearGhostFlipKey();
  TTool::getApplication()->getCurrentOnionSkin()->setOnionSkinMask(osm);
}

int ShiftTraceTool::getActiveGhostId() const {
  return TTool::getApplication()
      ->getCurrentOnionSkin()
      ->getOnionSkinMask()
      .getShiftTraceState()
      .getActiveGhostId();
}

void ShiftTraceTool::setActiveGhostId(int ghostId) {
  storeActiveGhostId(ghostId);
  updateBox(ghostId);
  invalidate();
}

ShiftTraceTool shiftTraceTool;
