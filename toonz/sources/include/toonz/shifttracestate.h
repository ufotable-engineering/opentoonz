#pragma once

#ifndef SHIFTTRACESTATE_H
#define SHIFTTRACESTATE_H

// TnzCore includes
#include "tcommon.h"
#include "tgeometry.h"

#include <vector>

#undef DVAPI
#undef DVVAR
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//====================================================

// Namespace-scope constexpr has internal linkage, which avoids MSVC issues with
// static data members of dllimport classes.
namespace ShiftTrace {
constexpr int kPreviousGhostId  = 0;
constexpr int kFollowingGhostId = 1;
}  // namespace ShiftTrace

enum class ShiftTraceGhostTint : int { Auto = 0, Back = 1, Front = 2 };

//***************************************************************************
//    ShiftTraceGhost  declaration
//***************************************************************************

struct DVAPI ShiftTraceGhost {
  int m_id = -1;
  //! Offset from the current row (xsheet) or frame index (level). 0 hides
  //! the ghost.
  int m_frameOffset = 0;
  //! Stage-space transform, multiplied on the left of the ghost placement.
  TAffine m_aff;
  //! Rotation / scale center, in the input space of m_aff.
  TPointD m_pivot;
  ShiftTraceGhostTint m_tint = ShiftTraceGhostTint::Auto;
  int m_flipKey              = 0;  //!< Qt::Key value, 0 = none

  bool hasTransform() const;
  void resetTransform();  //!< identity m_aff, origin m_pivot
  bool operator==(const ShiftTraceGhost &other) const;
  bool operator!=(const ShiftTraceGhost &other) const {
    return !(*this == other);
  }
};

//***************************************************************************
//    ShiftTraceCurve  declaration
//***************************************************************************

struct DVAPI ShiftTraceCurve {
  enum class Status : int { None = 0, TwoPoints = 1, ThreePoints = 2 };

  Status m_status = Status::None;
  TPointD m_p0, m_p1, m_p2;

  bool operator==(const ShiftTraceCurve &other) const;
  bool operator!=(const ShiftTraceCurve &other) const {
    return !(*this == other);
  }
};

//***************************************************************************
//    ShiftTraceLayout  declaration
//***************************************************************************

//! Ordered ghost list plus the curve.
class DVAPI ShiftTraceLayout {
public:
  //! Previous ghost (F1, back tint) followed by the following ghost (F3,
  //! front tint).
  static ShiftTraceLayout makeDefault();

  int getGhostCount() const { return static_cast<int>(m_ghosts.size()); }
  const ShiftTraceGhost &getGhost(int index) const;
  ShiftTraceGhost &getGhost(int index);

  int indexOfGhost(int ghostId) const;  //!< -1 if absent
  const ShiftTraceGhost *findGhost(int ghostId) const;
  ShiftTraceGhost *findGhost(int ghostId);
  const ShiftTraceGhost *findGhostByFlipKey(int key) const;  //!< first match

  //! Appends the ghost and returns its id. The id in \p ghost is overwritten.
  int addGhost(ShiftTraceGhost ghost);

  //! Frame offset of a ghost, 0 for unknown ids.
  int getGhostOffset(int ghostId) const;
  void setGhostOffset(int ghostId, int offset);

  void resetTransforms();

  const ShiftTraceCurve &getCurve() const { return m_curve; }
  void setCurve(const ShiftTraceCurve &curve) { m_curve = curve; }

  bool operator==(const ShiftTraceLayout &other) const;
  bool operator!=(const ShiftTraceLayout &other) const {
    return !(*this == other);
  }

private:
  std::vector<ShiftTraceGhost> m_ghosts;
  ShiftTraceCurve m_curve;
  int m_nextGhostId = 0;
};

//***************************************************************************
//    ShiftTraceState  declaration
//***************************************************************************

//! Snapshot of every Shift and Trace value that can be undone.
class DVAPI ShiftTraceState {
public:
  ShiftTraceState();

  const ShiftTraceLayout &getLayout() const { return m_layout; }
  ShiftTraceLayout &getLayout() { return m_layout; }

  int getActiveGhostId() const { return m_activeGhostId; }
  void setActiveGhostId(int ghostId) { m_activeGhostId = ghostId; }

  bool operator==(const ShiftTraceState &other) const;
  bool operator!=(const ShiftTraceState &other) const {
    return !(*this == other);
  }

private:
  ShiftTraceLayout m_layout;
  int m_activeGhostId = ShiftTrace::kPreviousGhostId;
};

#endif  // SHIFTTRACESTATE_H
