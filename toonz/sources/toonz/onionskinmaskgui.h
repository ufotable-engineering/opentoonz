#pragma once

#ifndef ONIONSKINMASKGUI
#define ONIONSKINMASKGUI

#include <QObject>

class OnionSkinMask;
class QMenu;

//=============================================================================
namespace OnioniSkinMaskGUI {
//-----------------------------------------------------------------------------

// Da fare per la filmstrip!!
void addOnionSkinCommand(QMenu *, bool isFilmStrip = false);

void resetShiftTraceFrameOffset();

//! Handles a click on the Shift and Trace marker of \p frame: hides the ghost
//! shown there, or moves the previous / following ghost to that frame.
void toggleShiftTraceGhostAt(int currentFrame, int frame);

//=============================================================================
// OnionSkinSwitcher
//-----------------------------------------------------------------------------

class OnionSkinSwitcher final : public QObject {
  Q_OBJECT

public:
  OnionSkinSwitcher() {}

  OnionSkinMask getMask() const;

  void setMask(const OnionSkinMask &mask);

  bool isActive() const;
  bool isWholeScene() const;

public slots:
  void activate();
  void deactivate();
  void setWholeScene();
  void setSingleLevel();
  void clearFOS();
  void clearMOS();
  void clearOS();
};

//-----------------------------------------------------------------------------
}  // namespace OnioniSkinMaskGUI
//-----------------------------------------------------------------------------

#endif  // ONIONSKINMASKGUI
