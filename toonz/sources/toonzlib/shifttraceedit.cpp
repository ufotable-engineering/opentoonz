#include "toonz/shifttraceedit.h"

// TnzLib includes
#include "toonz/onionskinmask.h"
#include "toonz/tapplication.h"
#include "toonz/tonionskinmaskhandle.h"

//***************************************************************************
//    ShiftTraceEdit  implementation
//***************************************************************************

ShiftTraceLayoutView ShiftTraceEdit::currentLayout(const TApplication *app) {
  ShiftTraceLayoutView view;
  view.m_state = app->getCurrentOnionSkin()
                     ->getOnionSkinMask()
                     .getShiftTraceStateSnapshot();
  view.m_layout = &view.m_state->getLayout();
  return view;
}

//-----------------------------------------------------------------------------

bool ShiftTraceEdit::editState(
    TOnionSkinMaskHandle *handle,
    const std::function<void(ShiftTraceState &)> &edit, Notify notify) {
  OnionSkinMask osm    = handle->getOnionSkinMask();
  ShiftTraceState next = osm.getShiftTraceState();
  edit(next);
  if (next == osm.getShiftTraceState()) return false;

  osm.setShiftTraceState(std::move(next));
  handle->setOnionSkinMask(osm);
  if (notify == Notify::MaskChanged) handle->notifyOnionSkinMaskChanged();
  return true;
}

//-----------------------------------------------------------------------------

bool ShiftTraceEdit::editCurrentLayout(
    const TApplication *app,
    const std::function<void(ShiftTraceLayout &)> &edit, Notify notify) {
  return editState(
      app->getCurrentOnionSkin(),
      [&](ShiftTraceState &state) { edit(state.getLayout()); }, notify);
}

//***************************************************************************
//    ShiftTraceDragSession  implementation
//***************************************************************************

void ShiftTraceDragSession::begin(const TApplication *app) {
  m_handle      = app->getCurrentOnionSkin();
  m_before      = m_handle->getOnionSkinMask().getShiftTraceStateSnapshot();
  m_lastWritten = m_before;
  m_working     = *m_before;
}

//-----------------------------------------------------------------------------

bool ShiftTraceDragSession::store() {
  if (!m_handle) return false;

  // Another writer (a scene switch, for instance) replaced the state while
  // dragging: drop the session rather than overwrite their state.
  if (m_handle->getOnionSkinMask().getShiftTraceStateSnapshot() !=
      m_lastWritten) {
    end();
    return false;
  }

  if (m_working == *m_lastWritten) return true;

  OnionSkinMask osm = m_handle->getOnionSkinMask();
  osm.setShiftTraceState(m_working);
  m_handle->setOnionSkinMask(osm);
  m_lastWritten = m_handle->getOnionSkinMask().getShiftTraceStateSnapshot();
  return true;
}

//-----------------------------------------------------------------------------

bool ShiftTraceDragSession::preview() { return store(); }

//-----------------------------------------------------------------------------

bool ShiftTraceDragSession::commit() {
  if (!m_handle) return false;
  if (!store()) return false;
  bool changed = *m_lastWritten != *m_before;
  end();
  return changed;
}

//-----------------------------------------------------------------------------

void ShiftTraceDragSession::end() {
  m_handle = nullptr;
  m_before.reset();
  m_lastWritten.reset();
}
