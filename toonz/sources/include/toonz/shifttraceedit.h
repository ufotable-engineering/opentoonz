#pragma once

#ifndef SHIFTTRACEEDIT_H
#define SHIFTTRACEEDIT_H

#include "toonz/shifttracestate.h"

#include <functional>
#include <memory>

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

class TApplication;
class TOnionSkinMaskHandle;

//====================================================

//***************************************************************************
//    ShiftTraceLayoutView  declaration
//***************************************************************************

// Holds the snapshot so that the layout pointer stays valid after the mask
// in the handle is replaced.
struct DVAPI ShiftTraceLayoutView {
  std::shared_ptr<const ShiftTraceState> m_state;
  const ShiftTraceLayout *m_layout = nullptr;

  const ShiftTraceLayout &operator*() const { return *m_layout; }
  const ShiftTraceLayout *operator->() const { return m_layout; }
};

//***************************************************************************
//    ShiftTraceEdit  declaration
//***************************************************************************

//! Single write path for immediate Shift and Trace edits.
namespace ShiftTraceEdit {

enum class Notify { None, MaskChanged };

//! Layout of the application's current onion skin mask.
DVAPI ShiftTraceLayoutView currentLayout(const TApplication *app);

//! Applies \p edit to a copy of the state and stores it if it changed.
//! Returns whether the state changed.
DVAPI bool editState(TOnionSkinMaskHandle *handle,
                     const std::function<void(ShiftTraceState &)> &edit,
                     Notify notify);

DVAPI bool editCurrentLayout(
    const TApplication *app,
    const std::function<void(ShiftTraceLayout &)> &edit, Notify notify);

}  // namespace ShiftTraceEdit

//***************************************************************************
//    ShiftTraceDragSession  declaration
//***************************************************************************

//! Working copy of the state for one drag: begin, preview while dragging,
//! then commit.
class DVAPI ShiftTraceDragSession {
public:
  void begin(const TApplication *app);

  bool isActive() const { return m_handle != nullptr; }
  ShiftTraceState &working() { return m_working; }
  ShiftTraceLayout &workingLayout() { return m_working.getLayout(); }

  //! Stores the working copy without notification. Returns false and ends
  //! the session if the state was replaced from outside meanwhile.
  bool preview();
  //! Stores, ends the session and returns whether the state changed.
  bool commit();

private:
  bool store();
  void end();

  TOnionSkinMaskHandle *m_handle = nullptr;
  std::shared_ptr<const ShiftTraceState> m_before;
  std::shared_ptr<const ShiftTraceState> m_lastWritten;
  ShiftTraceState m_working;
};

#endif  // SHIFTTRACEEDIT_H
