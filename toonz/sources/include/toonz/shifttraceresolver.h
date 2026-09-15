#pragma once

#ifndef SHIFTTRACERESOLVER_H
#define SHIFTTRACERESOLVER_H

#include "toonz/shifttracestate.h"

// TnzCore includes
#include "tfilepath.h"

#include <cstdint>
#include <functional>

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

//    Forward declarations

class TXsheet;
class TXshCell;

//====================================================

//***************************************************************************
//    ShiftTraceCellRef  declaration
//***************************************************************************

//! Identity of a cell, without holding the level alive.
struct DVAPI ShiftTraceCellRef {
  std::uintptr_t m_level       = 0;  //!< 0: empty cell
  std::uintptr_t m_simpleLevel = 0;  //!< 0: not a simple level
  TFrameId m_fid;

  static ShiftTraceCellRef fromCell(const TXshCell &cell);
  bool isEmpty() const { return m_level == 0; }
  // Same semantics as TXshCell::operator==.
  bool operator==(const ShiftTraceCellRef &other) const {
    return m_level == other.m_level && m_fid == other.m_fid;
  }
  bool operator!=(const ShiftTraceCellRef &other) const {
    return !(*this == other);
  }
};

using ShiftTraceCellGetter =
    std::function<ShiftTraceCellRef(int row, int column)>;

//***************************************************************************
//    ShiftTraceResolvedGhost  declaration
//***************************************************************************

struct DVAPI ShiftTraceResolvedGhost {
  int m_ghostIndex        = -1;
  int m_ghostId           = -1;
  bool m_valid            = false;
  int m_row               = -1;  //!< xsheet row, or level frame index
  int m_onionSkinDistance = 0;   //!< -1 back tint, +1 front tint
};

//***************************************************************************
//    ShiftTraceResolver  declaration
//***************************************************************************

//! Decides which cell each ghost shows. Both the stage builder and the tool
//! go through here so that they always agree.
namespace ShiftTraceResolver {

DVAPI ShiftTraceResolvedGhost
resolveInXsheet(const ShiftTraceLayout &layout, int ghostIndex, int currentRow,
                int currentColumn, const ShiftTraceCellGetter &getCell);

DVAPI ShiftTraceResolvedGhost resolveInLevel(const ShiftTraceLayout &layout,
                                             int ghostIndex,
                                             int currentFrameIndex);

DVAPI ShiftTraceCellGetter makeCellGetter(const TXsheet *xsh);

}  // namespace ShiftTraceResolver

#endif  // SHIFTTRACERESOLVER_H
