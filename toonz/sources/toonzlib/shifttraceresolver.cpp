#include "toonz/shifttraceresolver.h"

// TnzLib includes
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"

#include <climits>

//***************************************************************************
//    Local namespace
//***************************************************************************

namespace {

int tintDistance(ShiftTraceGhostTint tint, int ghostRow, int currentRow) {
  switch (tint) {
  case ShiftTraceGhostTint::Back:
    return -1;
  case ShiftTraceGhostTint::Front:
    return 1;
  default:
    return ghostRow < currentRow ? -1 : 1;
  }
}

//-----------------------------------------------------------------------------

// Frame offsets come from the UI, so keep the row arithmetic in range.
bool resolveRow(const ShiftTraceGhost &ghost, int current, int &row) {
  long long value = static_cast<long long>(current) + ghost.m_frameOffset;
  if (value < INT_MIN || value > INT_MAX) return false;
  row = static_cast<int>(value);
  return true;
}

}  // namespace

//***************************************************************************
//    ShiftTraceCellRef  implementation
//***************************************************************************

ShiftTraceCellRef ShiftTraceCellRef::fromCell(const TXshCell &cell) {
  ShiftTraceCellRef ref;
  ref.m_level = reinterpret_cast<std::uintptr_t>(cell.m_level.getPointer());
  ref.m_simpleLevel = reinterpret_cast<std::uintptr_t>(cell.getSimpleLevel());
  ref.m_fid         = cell.m_frameId;
  return ref;
}

//***************************************************************************
//    ShiftTraceResolver  implementation
//***************************************************************************

ShiftTraceResolvedGhost ShiftTraceResolver::resolveInXsheet(
    const ShiftTraceLayout &layout, int ghostIndex, int currentRow,
    int currentColumn, const ShiftTraceCellGetter &getCell) {
  ShiftTraceResolvedGhost result;
  if (ghostIndex < 0 || ghostIndex >= layout.getGhostCount()) return result;

  const ShiftTraceGhost &ghost = layout.getGhost(ghostIndex);
  result.m_ghostIndex          = ghostIndex;
  result.m_ghostId             = ghost.m_id;

  int row;
  if (!resolveRow(ghost, currentRow, row)) return result;
  if (row < 0) return result;

  result.m_row               = row;
  result.m_onionSkinDistance = tintDistance(ghost.m_tint, row, currentRow);

  // A different cell of the same simple level, or any different cell when
  // the current one is not a simple level (empty or sub-xsheet).
  ShiftTraceCellRef current   = getCell(currentRow, currentColumn);
  ShiftTraceCellRef candidate = getCell(row, currentColumn);
  result.m_valid              = candidate != current &&
                   (current.m_simpleLevel == 0 ||
                    candidate.m_simpleLevel == current.m_simpleLevel);
  return result;
}

//-----------------------------------------------------------------------------

ShiftTraceResolvedGhost ShiftTraceResolver::resolveInLevel(
    const ShiftTraceLayout &layout, int ghostIndex, int currentFrameIndex) {
  ShiftTraceResolvedGhost result;
  if (ghostIndex < 0 || ghostIndex >= layout.getGhostCount()) return result;

  const ShiftTraceGhost &ghost = layout.getGhost(ghostIndex);
  result.m_ghostIndex          = ghostIndex;
  result.m_ghostId             = ghost.m_id;

  int row;
  if (!resolveRow(ghost, currentFrameIndex, row)) return result;

  // Negative indices are passed through: the stage builder hides them and
  // the tool skips their control box.
  result.m_row = row;
  result.m_onionSkinDistance =
      tintDistance(ghost.m_tint, row, currentFrameIndex);
  result.m_valid = ghost.m_frameOffset != 0;
  return result;
}

//-----------------------------------------------------------------------------

ShiftTraceCellGetter ShiftTraceResolver::makeCellGetter(const TXsheet *xsh) {
  return [xsh](int row, int column) {
    return xsh ? ShiftTraceCellRef::fromCell(xsh->getCell(row, column))
               : ShiftTraceCellRef();
  };
}
