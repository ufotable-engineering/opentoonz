#include "toonz/shifttracestate.h"

#include <Qt>

#include <cassert>

//***************************************************************************
//    ShiftTraceGhost  implementation
//***************************************************************************

bool ShiftTraceGhost::hasTransform() const {
  return !m_aff.isIdentity() || m_pivot != TPointD();
}

//-----------------------------------------------------------------------------

void ShiftTraceGhost::resetTransform() {
  m_aff   = TAffine();
  m_pivot = TPointD();
}

//-----------------------------------------------------------------------------

bool ShiftTraceGhost::operator==(const ShiftTraceGhost &other) const {
  return m_id == other.m_id && m_frameOffset == other.m_frameOffset &&
         m_aff == other.m_aff && m_pivot == other.m_pivot &&
         m_tint == other.m_tint && m_flipKey == other.m_flipKey;
}

//***************************************************************************
//    ShiftTraceCurve  implementation
//***************************************************************************

bool ShiftTraceCurve::operator==(const ShiftTraceCurve &other) const {
  return m_status == other.m_status && m_p0 == other.m_p0 &&
         m_p1 == other.m_p1 && m_p2 == other.m_p2;
}

//***************************************************************************
//    ShiftTraceLayout  implementation
//***************************************************************************

ShiftTraceLayout ShiftTraceLayout::makeDefault() {
  ShiftTraceLayout layout;

  // Ids are assigned in order from 0, so the first two ghosts get
  // kPreviousGhostId and kFollowingGhostId.
  ShiftTraceGhost previous;
  previous.m_tint    = ShiftTraceGhostTint::Back;
  previous.m_flipKey = Qt::Key_F1;
  layout.addGhost(previous);

  ShiftTraceGhost following;
  following.m_tint    = ShiftTraceGhostTint::Front;
  following.m_flipKey = Qt::Key_F3;
  layout.addGhost(following);

  return layout;
}

//-----------------------------------------------------------------------------

const ShiftTraceGhost &ShiftTraceLayout::getGhost(int index) const {
  assert(0 <= index && index < getGhostCount());
  return m_ghosts[index];
}

//-----------------------------------------------------------------------------

ShiftTraceGhost &ShiftTraceLayout::getGhost(int index) {
  assert(0 <= index && index < getGhostCount());
  return m_ghosts[index];
}

//-----------------------------------------------------------------------------

int ShiftTraceLayout::indexOfGhost(int ghostId) const {
  for (int i = 0; i < getGhostCount(); ++i)
    if (m_ghosts[i].m_id == ghostId) return i;
  return -1;
}

//-----------------------------------------------------------------------------

const ShiftTraceGhost *ShiftTraceLayout::findGhost(int ghostId) const {
  int index = indexOfGhost(ghostId);
  return index < 0 ? nullptr : &m_ghosts[index];
}

//-----------------------------------------------------------------------------

ShiftTraceGhost *ShiftTraceLayout::findGhost(int ghostId) {
  int index = indexOfGhost(ghostId);
  return index < 0 ? nullptr : &m_ghosts[index];
}

//-----------------------------------------------------------------------------

const ShiftTraceGhost *ShiftTraceLayout::findGhostByFlipKey(int key) const {
  if (key == 0) return nullptr;
  for (const ShiftTraceGhost &ghost : m_ghosts)
    if (ghost.m_flipKey == key) return &ghost;
  return nullptr;
}

//-----------------------------------------------------------------------------

int ShiftTraceLayout::addGhost(ShiftTraceGhost ghost) {
  ghost.m_id = m_nextGhostId++;
  m_ghosts.push_back(ghost);
  return ghost.m_id;
}

//-----------------------------------------------------------------------------

int ShiftTraceLayout::getGhostOffset(int ghostId) const {
  const ShiftTraceGhost *ghost = findGhost(ghostId);
  return ghost ? ghost->m_frameOffset : 0;
}

//-----------------------------------------------------------------------------

void ShiftTraceLayout::setGhostOffset(int ghostId, int offset) {
  if (ShiftTraceGhost *ghost = findGhost(ghostId))
    ghost->m_frameOffset = offset;
}

//-----------------------------------------------------------------------------

void ShiftTraceLayout::resetTransforms() {
  for (ShiftTraceGhost &ghost : m_ghosts) ghost.resetTransform();
}

//-----------------------------------------------------------------------------

bool ShiftTraceLayout::operator==(const ShiftTraceLayout &other) const {
  // m_nextGhostId is bookkeeping only; two layouts with the same ghosts are
  // interchangeable for undo and change detection.
  return m_ghosts == other.m_ghosts && m_curve == other.m_curve;
}

//***************************************************************************
//    ShiftTraceState  implementation
//***************************************************************************

ShiftTraceState::ShiftTraceState()
    : m_layout(ShiftTraceLayout::makeDefault()) {}

//-----------------------------------------------------------------------------

bool ShiftTraceState::operator==(const ShiftTraceState &other) const {
  return m_layout == other.m_layout && m_activeGhostId == other.m_activeGhostId;
}
