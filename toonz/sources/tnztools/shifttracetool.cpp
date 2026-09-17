

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
#include "tenv.h"

#include "tgl.h"
#include <math.h>
#include <QKeyEvent>

//=============================================================================

TEnv::IntVar ShiftTraceStraightTrajectory("ShiftTraceToolStraightTrajectory",
                                          0);
TEnv::IntVar ShiftTraceRotateAlongArc("ShiftTraceToolRotateAlongArc", 0);

namespace {

// Every division draws a dot on the trajectory each frame, so an unbounded
// denominator would stall the viewer
const int kMaxSnapDivision = 64;

// getGadget() prefers p0/p1 on overlap, so a p2 placed exactly on an end
// point could no longer be grabbed
const double kMinTrajectoryRatio = 0.05;

}  // namespace

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
    : TTool("T_ShiftTrace")
    , m_ghostIndex(0)
    , m_curveStatus(NoCurve)
    , m_gadget(NoGadget)
    , m_highlightedGadget(NoGadget)
    , m_straightTrajectory("Straight Trajectory", false)
    , m_rotateAlongArc("Rotate Along Arc", false)
    , m_snapRatio("Snap Ratio", L"")
    , m_snapDivision(0)
    , m_snapNumerator(0)
    , m_bend(0)
    , m_ratio(0.5) {
  bind(TTool::AllTargets);  // Deals with tool deactivation internally
  m_prop.bind(m_straightTrajectory);
  m_prop.bind(m_rotateAlongArc);
  m_prop.bind(m_snapRatio);
  m_straightTrajectory.setId("StraightTrajectory");
  m_rotateAlongArc.setId("RotateAlongArc");
  m_snapRatio.setId("SnapRatio");
}

void ShiftTraceTool::updateTranslation() {
  m_straightTrajectory.setQStringName(QObject::tr("Straight Trajectory"));
  m_rotateAlongArc.setQStringName(QObject::tr("Rotate Along Arc"));
  m_snapRatio.setQStringName(QObject::tr("Snap Ratio"));
}

void ShiftTraceTool::clearData() {
  m_ghostIndex        = 0;
  m_curveStatus       = NoCurve;
  m_gadget            = NoGadget;
  m_highlightedGadget = NoGadget;
  m_bend              = 0;
  m_ratio             = 0.5;

  m_box = TRectD();
  for (int i = 0; i < 2; i++) {
    m_row[i]    = -1;
    m_aff[i]    = TAffine();
    m_center[i] = TPointD();
  }
}

void ShiftTraceTool::updateBox() {
  if (m_ghostIndex < 0 || 2 <= m_ghostIndex || m_row[m_ghostIndex] < 0) return;

  TImageP img;

  TApplication *app = TTool::getApplication();
  if (app->getCurrentFrame()->isEditingScene()) {
    int col      = app->getCurrentColumn()->getColumnIndex();
    int row      = m_row[m_ghostIndex];
    TXsheet *xsh = app->getCurrentXsheet()->getXsheet();

    TXshCell cell       = xsh->getCell(row, col);
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

    const TFrameId &ghostFid = sl->index2fid(m_row[m_ghostIndex]);
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

void ShiftTraceTool::updateData() {
  m_box = TRectD();
  for (int i = 0; i < 2; i++) m_row[i] = -1;
  m_dpiAff          = TAffine();
  TApplication *app = TTool::getApplication();

  OnionSkinMask osm  = app->getCurrentOnionSkin()->getOnionSkinMask();
  int previousOffset = osm.getShiftTraceGhostFrameOffset(0);
  int forwardOffset  = osm.getShiftTraceGhostFrameOffset(1);
  // we must find the prev (m_row[0]) and next (m_row[1]) reference images
  // (either might not exist)
  // see also stage.cpp, StageBuilder::addCellWithOnionSkin
  if (app->getCurrentFrame()->isEditingScene()) {
    TXsheet *xsh  = app->getCurrentXsheet()->getXsheet();
    int row       = app->getCurrentFrame()->getFrame();
    int col       = app->getCurrentColumn()->getColumnIndex();
    TXshCell cell = xsh->getCell(row, col);
    int r;
    r = row + previousOffset;
    if (r >= 0 && xsh->getCell(r, col) != cell &&
        (cell.getSimpleLevel() == 0 ||
         xsh->getCell(r, col).getSimpleLevel() == cell.getSimpleLevel())) {
      m_row[0] = r;
    }

    r = row + forwardOffset;
    if (r >= 0 && xsh->getCell(r, col) != cell &&
        (cell.getSimpleLevel() == 0 ||
         xsh->getCell(r, col).getSimpleLevel() == cell.getSimpleLevel())) {
      m_row[1] = r;
    }
  }
  // on editing level
  else {
    TXshLevel *level = app->getCurrentLevel()->getLevel();
    if (level) {
      TXshSimpleLevel *sl = level->getSimpleLevel();
      if (sl) {
        TFrameId fid = app->getCurrentFrame()->getFid();
        int row      = sl->guessIndex(fid);
        m_row[0]     = row + previousOffset;
        m_row[1]     = row + forwardOffset;
      }
    }
  }
  updateBox();
}

TPointD ShiftTraceTool::chordNormal() const {
  TPointD d = m_p1 - m_p0;
  if (norm2(d) <= 0) return TPointD(0, 1);
  return normalize(rotate90(d));
}

TPointD ShiftTraceTool::arcApex() const {
  return (m_p0 + m_p1) * 0.5 + chordNormal() * m_bend;
}

// Handles sit off the trajectory so they never overlap the midpoint gadget
double ShiftTraceTool::bendHandleOffset() const { return 50 * getPixelSize(); }

// A straight trajectory is treated like collinear points: no arc, so the
// ghosts are translated without rotation. The sweep sign picks the side of
// the circle that contains the apex
ShiftTraceTool::Trajectory ShiftTraceTool::getTrajectory() const {
  Trajectory t;
  TPointD apex = arcApex();
  t.isArc      = !m_straightTrajectory.getValue() &&
            circumCenter(t.center, m_p0, m_p1, apex);
  if (!t.isArc) return t;
  t.radius        = norm(m_p0 - t.center);
  t.angle0        = atan(m_p0 - t.center);
  double angle1   = atan(m_p1 - t.center);
  double angleRef = atan(apex - t.center);
  double ccw1     = fmod(angle1 - t.angle0 + M_2PI, M_2PI);
  double ccwRef   = fmod(angleRef - t.angle0 + M_2PI, M_2PI);
  t.sweep         = ccwRef < ccw1 ? ccw1 : ccw1 - M_2PI;
  return t;
}

TPointD ShiftTraceTool::trajectoryPoint(const Trajectory &t,
                                        double ratio) const {
  if (!t.isArc) return m_p0 + (m_p1 - m_p0) * ratio;
  double angle = t.angle0 + t.sweep * ratio;
  return t.center + t.radius * TPointD(cos(angle), sin(angle));
}

TPointD ShiftTraceTool::trajectoryPoint(double ratio) const {
  return trajectoryPoint(getTrajectory(), ratio);
}

double ShiftTraceTool::trajectoryRatio(const TPointD &pos) const {
  Trajectory t = getTrajectory();
  if (!t.isArc) {
    TPointD d   = m_p1 - m_p0;
    double len2 = d * d;
    if (len2 <= 0) return 0.5;
    return tcrop(((pos - m_p0) * d) / len2, kMinTrajectoryRatio,
                 1.0 - kMinTrajectoryRatio);
  }
  double angle     = atan(pos - t.center);
  double travelled = t.sweep > 0 ? fmod(angle - t.angle0 + M_2PI, M_2PI)
                                 : fmod(t.angle0 - angle + M_2PI, M_2PI);
  double ratio     = travelled / fabs(t.sweep);
  // Beyond p1 the position is on the complementary arc: pick the nearer end
  if (ratio > 1.0)
    ratio = (ratio - 1.0 < M_2PI / fabs(t.sweep) - ratio) ? 1 : 0;
  return tcrop(ratio, kMinTrajectoryRatio, 1.0 - kMinTrajectoryRatio);
}

// End points are excluded: they are the keys themselves, not inbetweens
double ShiftTraceTool::snapRatio(double ratio) const {
  if (m_snapDivision < 2) return ratio;
  int k = tcrop(tround(ratio * m_snapDivision), 1, m_snapDivision - 1);
  return (double)k / m_snapDivision;
}

double ShiftTraceTool::snapOrJump(double ratio) const {
  return m_snapNumerator > 0 ? (double)m_snapNumerator / m_snapDivision
                             : snapRatio(ratio);
}

// Accepts "a/b" (jump to a/b and snap to b-ths) or "b" (snap only)
void ShiftTraceTool::parseSnapRatio() {
  QString text      = QString::fromStdWString(m_snapRatio.getValue()).trimmed();
  QStringList parts = text.split(QChar('/'));
  bool okNum = false, okDen = false;
  int num = 0, den = 0;
  if (parts.size() == 1) {
    den   = parts[0].trimmed().toInt(&okDen);
    okNum = true;
  } else if (parts.size() == 2) {
    num = parts[0].trimmed().toInt(&okNum);
    den = parts[1].trimmed().toInt(&okDen);
  }
  if (!okNum || !okDen || den < 2 || den > kMaxSnapDivision || num < 0 ||
      num >= den) {
    m_snapDivision  = 0;
    m_snapNumerator = 0;
    m_snapRatio.setValue(L"");
    return;
  }
  m_snapDivision  = den;
  m_snapNumerator = num;
  m_snapRatio.setValue(
      (num > 0 ? QString("%1/%2").arg(num).arg(den) : QString::number(den))
          .toStdWString());
}

//
// Compute m_aff[0] and m_aff[1] according to the current curve
//
void ShiftTraceTool::updateCurveAffs() {
  if (m_curveStatus != ThreePointsCurve) {
    m_aff[0] = m_aff[1] = TAffine();
  } else {
    double phi0 = 0, phi1 = 0;
    Trajectory t = getTrajectory();
    if (m_rotateAlongArc.getValue() && t.isArc) {
      TPointD v0 = normalize(m_p0 - t.center);
      TPointD v1 = normalize(m_p1 - t.center);
      TPointD v2 = normalize(m_p2 - t.center);
      TPointD u0(-v0.y, v0.x);
      TPointD u1(-v1.y, v1.x);
      phi0 = atan2((v2 * u0), (v2 * v0)) * 180.0 / 3.1415;
      phi1 = atan2((v2 * u1), (v2 * v1)) * 180.0 / 3.1415;
    }
    m_aff[0] = TTranslation(m_p2 - m_p0) * TRotation(m_p0, phi0);
    m_aff[1] = TTranslation(m_p2 - m_p1) * TRotation(m_p1, phi1);
  }
}

void ShiftTraceTool::updateCurveCenters() {
  m_center[0] = (m_aff[0] * m_dpiAff).inv() * m_p2;
  m_center[1] = (m_aff[1] * m_dpiAff).inv() * m_p2;
}

void ShiftTraceTool::updateGhost() {
  OnionSkinMask osm =
      TTool::getApplication()->getCurrentOnionSkin()->getOnionSkinMask();
  osm.setShiftTraceGhostAff(0, m_aff[0]);
  osm.setShiftTraceGhostAff(1, m_aff[1]);
  osm.setShiftTraceGhostCenter(0, m_center[0]);
  osm.setShiftTraceGhostCenter(1, m_center[1]);
  TTool::getApplication()->getCurrentOnionSkin()->setOnionSkinMask(osm);
}

void ShiftTraceTool::reset() {
  int ghostIndex = m_ghostIndex;
  onActivate();
  invalidate();
  m_ghostIndex = ghostIndex;

  TTool::getApplication()
      ->getCurrentTool()
      ->notifyToolChanged();  // Refreshes toolbar values
}

TAffine ShiftTraceTool::getGhostAff() {
  if (0 <= m_ghostIndex && m_ghostIndex < 2)
    return m_aff[m_ghostIndex] * m_dpiAff;
  else
    return TAffine();
}

void ShiftTraceTool::drawDot(const TPointD &center, double r,
                             const TPixel32 &color) {
  tglColor(color);
  tglDrawDisk(center, r);
  glColor3d(0.2, 0.2, 0.2);
  tglDrawCircle(center, r);
}

void ShiftTraceTool::drawControlRect() {  // TODO
  if (m_ghostIndex < 0 || m_ghostIndex > 1) return;
  int row = m_row[m_ghostIndex];
  if (row < 0) return;

  TRectD box = m_box;
  if (box.isEmpty()) return;
  glPushMatrix();
  tglMultMatrix(getGhostAff());

  TPixel32 color;

  // draw onion-colored rectangle to indicate which ghost is grabbed
  {
    TPixel32 frontOniColor, backOniColor;
    bool inksOnly;
    Preferences::instance()->getOnionData(frontOniColor, backOniColor,
                                          inksOnly);
    color       = (m_ghostIndex == 0) ? backOniColor : frontOniColor;
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
  if (m_curveStatus == NoCurve) {
    color = m_highlightedGadget == MoveCenterGadget ? TPixel32(200, 100, 100)
                                                    : TPixel32::White;
    TPointD c = m_center[m_ghostIndex];
    drawDot(c, r, color);
  }
  glPopMatrix();
}

void ShiftTraceTool::drawCurve() {
  if (m_curveStatus == NoCurve) return;
  double r = 4 * sqrt(tglGetPixelSize2());
  double u = getPixelSize();
  if (m_curveStatus == TwoPointsCurve) {
    TPixel32 color = m_highlightedGadget == CurveP0Gadget
                         ? TPixel32(200, 100, 100)
                         : TPixel32::White;
    drawDot(m_p0, r, color);
    glColor3d(0.2, 0.2, 0.2);
    tglDrawSegment(m_p0, m_p1);
    drawDot(m_p1, r, TPixel32::Red);
  } else if (m_curveStatus == ThreePointsCurve) {
    TPixel32 color = m_highlightedGadget == CurveP0Gadget
                         ? TPixel32(200, 100, 100)
                         : TPixel32::White;
    drawDot(m_p0, r, color);
    color = m_highlightedGadget == CurveP1Gadget ? TPixel32(200, 100, 100)
                                                 : TPixel32::White;
    drawDot(m_p1, r, color);

    glColor3d(0.2, 0.2, 0.2);

    Trajectory t = getTrajectory();
    glBegin(GL_LINE_STRIP);
    int n = 100;
    for (int i = 0; i <= n; i++) tglVertex(trajectoryPoint(t, (double)i / n));
    glEnd();
    for (int k = 1; k < m_snapDivision; k++)
      drawDot(trajectoryPoint(t, (double)k / m_snapDivision), r * 0.5,
              TPixel32(180, 180, 180));
    if (!m_straightTrajectory.getValue()) {
      TPointD apex = arcApex();
      TPointD off  = chordNormal() * bendHandleOffset();
      glLineStipple(1, 0xAAAA);
      glEnable(GL_LINE_STIPPLE);
      tglDrawSegment(apex - off, apex + off);
      glDisable(GL_LINE_STIPPLE);
      color = m_highlightedGadget == CurveBendPlusGadget
                  ? TPixel32(200, 100, 100)
                  : TPixel32(120, 180, 240);
      drawDot(apex + off, r * 0.75, color);
      color = m_highlightedGadget == CurveBendMinusGadget
                  ? TPixel32(200, 100, 100)
                  : TPixel32(120, 180, 240);
      drawDot(apex - off, r * 0.75, color);
    }
    color = m_highlightedGadget == CurvePmGadget ? TPixel32(200, 100, 100)
                                                 : TPixel32::White;
    drawDot(m_p2, r, color);
  }
}

void ShiftTraceTool::onActivate() {
  m_ghostIndex  = 0;
  m_curveStatus = NoCurve;
  clearData();
  m_straightTrajectory.setValue(ShiftTraceStraightTrajectory ? 1 : 0);
  m_rotateAlongArc.setValue(ShiftTraceRotateAlongArc ? 1 : 0);
  OnionSkinMask osm =
      TTool::getApplication()->getCurrentOnionSkin()->getOnionSkinMask();
  m_aff[0]    = osm.getShiftTraceGhostAff(0);
  m_aff[1]    = osm.getShiftTraceGhostAff(1);
  m_center[0] = osm.getShiftTraceGhostCenter(0);
  m_center[1] = osm.getShiftTraceGhostCenter(1);
}

void ShiftTraceTool::onDeactivate() {
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
  std::vector<std::pair<TPointD, GadgetId>> gadgets;
  gadgets.push_back(std::make_pair(m_p0, CurveP0Gadget));
  gadgets.push_back(std::make_pair(m_p1, CurveP1Gadget));
  gadgets.push_back(std::make_pair(m_p2, CurvePmGadget));
  if (m_curveStatus == ThreePointsCurve && !m_straightTrajectory.getValue()) {
    TPointD apex = arcApex();
    TPointD off  = chordNormal() * bendHandleOffset();
    gadgets.push_back(std::make_pair(apex + off, CurveBendPlusGadget));
    gadgets.push_back(std::make_pair(apex - off, CurveBendMinusGadget));
  }
  TAffine aff      = getGhostAff();
  double pixelSize = getPixelSize();
  double d         = 15 * pixelSize;  // offset for rotation handle
  if (0 <= m_ghostIndex && m_ghostIndex < 2) {
    gadgets.push_back(std::make_pair(aff * m_box.getP00(), ScaleGadget));
    gadgets.push_back(std::make_pair(aff * m_box.getP01(), ScaleGadget));
    gadgets.push_back(std::make_pair(aff * m_box.getP10(), ScaleGadget));
    gadgets.push_back(std::make_pair(aff * m_box.getP11(), ScaleGadget));
    gadgets.push_back(
        std::make_pair(aff * m_center[m_ghostIndex], MoveCenterGadget));
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
  if (0 <= m_ghostIndex && m_ghostIndex < 2) {
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
  m_gadget = m_highlightedGadget;
  m_oldPos = m_startPos = pos;

  bool notify = false;

  if (!e.isCtrlPressed() &&
      (m_gadget == NoGadget || m_gadget == NoGadget_InBox)) {
    if (m_gadget == NoGadget_InBox) {
      m_gadget = TranslateGadget;
    } else {
      m_gadget = RotateGadget;
    }

    int row = getViewer()->posToRow(e.m_pos, 5.0, false, true);
    if (row >= 0) {
      int index         = -1;
      TApplication *app = TTool::getApplication();
      if (app->getCurrentFrame()->isEditingScene()) {
        int currentRow = getFrame();
        if (m_row[0] >= 0 && row < currentRow)
          index = 0;
        else if (m_row[1] >= 0 && row > currentRow)
          index = 1;
      } else {
        if (m_row[0] == row)
          index = 0;
        else if (m_row[1] == row)
          index = 1;
      }

      if (index >= 0) {
        m_ghostIndex = index;
        updateBox();
        m_gadget            = TranslateGadget;
        m_highlightedGadget = TranslateGadget;
        notify              = true;
      }
    }
  } else if (e.isCtrlPressed()) {
    m_gadget = NoGadget_InBox;
  }

  m_oldAff = m_aff[m_ghostIndex];
  invalidate();

  if (notify) {
    TTool::getApplication()
        ->getCurrentTool()
        ->notifyToolChanged();  // Refreshes toolbar values
  }
}

void ShiftTraceTool::leftButtonDrag(const TPointD &pos, const TMouseEvent &e) {
  if (m_gadget == NoGadget || m_gadget == NoGadget_InBox) {
    if (norm(pos - m_oldPos) > 10 * getPixelSize()) {
      m_curveStatus = TwoPointsCurve;
      m_p0          = m_oldPos;
      m_gadget      = CurveP1Gadget;
    }
  }

  if (isCurveGadget(m_gadget)) {
    if (m_gadget == CurveP0Gadget)
      m_p0 = pos;
    else if (m_gadget == CurveP1Gadget)
      m_p1 = pos;
    else if (m_gadget == CurvePmGadget)
      m_ratio = snapRatio(trajectoryRatio(pos));
    else if (m_gadget == CurveBendPlusGadget ||
             m_gadget == CurveBendMinusGadget) {
      // Keeps the grabbed handle under the cursor while the apex follows
      int sign = m_gadget == CurveBendPlusGadget ? 1 : -1;
      m_bend   = (pos - (m_p0 + m_p1) * 0.5) * chordNormal() -
               sign * bendHandleOffset();
    }
    if (m_curveStatus == ThreePointsCurve) m_p2 = trajectoryPoint(m_ratio);
    updateCurveAffs();
  } else if (m_gadget == RotateGadget) {
    TAffine aff = getGhostAff();
    TPointD c   = aff * m_center[m_ghostIndex];
    TPointD a   = m_oldPos - c;
    TPointD b   = pos - c;
    m_oldPos    = pos;
    TPointD u   = normalize(a);
    double phi =
        atan2(-u.y * b.x + u.x * b.y, u.x * b.x + u.y * b.y) * 180.0 / 3.14153;

    TPointD imgC = aff * m_center[m_ghostIndex];

    m_aff[m_ghostIndex] = TRotation(imgC, phi) * m_aff[m_ghostIndex];
  } else if (m_gadget == MoveCenterGadget) {
    TAffine aff   = getGhostAff().inv();
    TPointD delta = aff * pos - aff * m_oldPos;
    m_oldPos      = pos;
    m_center[m_ghostIndex] += delta;
  } else if (m_gadget == TranslateGadget) {
    TPointD delta       = pos - m_oldPos;
    m_oldPos            = pos;
    m_aff[m_ghostIndex] = TTranslation(delta) * m_aff[m_ghostIndex];
  } else if (m_gadget == ScaleGadget) {
    TAffine aff  = getGhostAff();
    TPointD c    = m_center[m_ghostIndex];
    TPointD a    = aff.inv() * m_oldPos - c;
    TPointD b    = aff.inv() * pos - c;
    TPointD imgC = aff * m_center[m_ghostIndex];

    if (e.isShiftPressed())
      m_aff[m_ghostIndex] = TScale(imgC, b.x / a.x, b.y / a.y) * m_oldAff;
    else {
      double scale        = std::max(b.x / a.x, b.y / a.y);
      m_aff[m_ghostIndex] = TScale(imgC, scale) * m_oldAff;
    }
  }

  updateGhost();
  invalidate();
}

void ShiftTraceTool::leftButtonUp(const TPointD &pos, const TMouseEvent &) {
  if (CurveP0Gadget <= m_gadget && m_gadget <= CurvePmGadget) {
    if (m_curveStatus == TwoPointsCurve) {
      m_bend        = 0;
      m_ratio       = snapOrJump(0.5);
      m_p2          = trajectoryPoint(m_ratio);
      m_curveStatus = ThreePointsCurve;
      updateCurveAffs();
      updateGhost();
      updateCurveCenters();
    }
  }
  m_gadget = NoGadget;
  invalidate();

  TTool::getApplication()
      ->getCurrentTool()
      ->notifyToolChanged();  // Refreshes toolbar values
}

void ShiftTraceTool::draw() {
  updateData();
  drawControlRect();
  drawCurve();
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

void ShiftTraceTool::setCurrentGhostIndex(int index) {
  m_ghostIndex = index;
  updateBox();
  invalidate();
}

bool ShiftTraceTool::onPropertyChanged(std::string propertyName) {
  ShiftTraceStraightTrajectory = (int)m_straightTrajectory.getValue();
  ShiftTraceRotateAlongArc     = (int)m_rotateAlongArc.getValue();

  bool ratioEntered = propertyName == m_snapRatio.getName();
  if (ratioEntered) parseSnapRatio();

  // Ghosts moved by hand without a trajectory must survive a mode switch
  if (m_curveStatus == ThreePointsCurve) {
    m_ratio = ratioEntered ? snapOrJump(m_ratio) : snapRatio(m_ratio);
    m_p2    = trajectoryPoint(m_ratio);
    updateCurveAffs();
    updateCurveCenters();
    updateGhost();
  }
  invalidate();
  return true;
}

ShiftTraceTool shiftTraceTool;
