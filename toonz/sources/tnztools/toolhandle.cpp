

#include "tools/toolhandle.h"
#include "toonz/stage2.h"
#include "tools/tool.h"
#include "tools/toolcommandids.h"
#include "timage.h"
// #include "tapp.h"
#include "toonzqt/menubarcommand.h"
#include "toonz/preferences.h"
#include <QGuiApplication>
#include <QAction>
#include <QMap>
#include <QDebug>

static bool isViewerNavigationTool(const QString &name) {
  return name == T_RotateView || name == T_ZoomView || name == T_HandView;
}

//=============================================================================
// ToolHandle
//-----------------------------------------------------------------------------

ToolHandle::ToolHandle()
    : m_tool(0)
    , m_toolName("")
    , m_toolTargetType(TTool::NoTarget)
    , m_storedToolName("")
    , m_suspendedTool(0)
    , m_toolIsBusy(false) {}

//-----------------------------------------------------------------------------

ToolHandle::~ToolHandle() {}

//-----------------------------------------------------------------------------

TTool *ToolHandle::getTool() const { return m_tool; }

//-----------------------------------------------------------------------------

void ToolHandle::setTool(QString name) {
  TTool *tool       = TTool::getTool(name.toStdString(),
                                     (TTool::ToolTargetType)m_toolTargetType);
  bool toNavigation = isViewerNavigationTool(name);

  // navigation tools are temporary, so keep the current tool's editing state
  if (toNavigation && !isViewerNavigationToolSelected() &&
      Preferences::instance()->isKeepToolOnViewNavigationEnabled())
    m_suspendedTool = m_tool;
  bool resumed = m_suspendedTool && tool == m_suspendedTool;

  // unsetPseudoTool() must still return to the tool used before the pseudo tool
  if (!m_suspendedTool || (!toNavigation && !resumed)) m_oldToolName = name;
  m_toolName = name;
  if (tool == m_tool) return;

  if (m_tool && m_tool != m_suspendedTool) m_tool->onDeactivate();
  if (m_suspendedTool && !toNavigation) {
    if (!resumed) m_suspendedTool->onDeactivate();
    m_suspendedTool = 0;
  }

  // Camera test uses the automatically activated CameraTestTool
  if (name != "T_CameraTest" && CameraTestCheck::instance()->isEnabled())
    CameraTestCheck::instance()->setIsEnabled(false);

  m_tool = tool;

  if (name != T_Hand && CleanupPreviewCheck::instance()->isEnabled()) {
    // When using a tool, you have to exit from cleanup preview mode
    QAction *act = CommandManager::instance()->getAction("MI_CleanupPreview");
    if (act) CommandManager::instance()->execute(act);
  }

  if (m_tool)  // Should always enter
  {
    if (!resumed) m_tool->onActivate();
    emit toolSwitched();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::storeTool() {
  // navigation tools will not be stored
  if (!isViewerNavigationToolSelected()) {
    m_storedToolName = m_toolName;
    m_storedToolTime.start();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::restoreTool() {
  // qDebug() << m_storedToolTime.elapsed();
  // navigation tools will always return to the stored one even if the key press
  // time is short
  if (m_storedToolName != m_toolName && m_storedToolName != "" &&
      (m_storedToolTime.elapsed() >
           Preferences::instance()->getTempToolSwitchTimer() ||
       isViewerNavigationToolSelected())) {
    setTool(m_storedToolName);
  }
}

//-----------------------------------------------------------------------------

bool ToolHandle::isViewerNavigationToolSelected() {
  return isViewerNavigationTool(m_toolName);
}

//-----------------------------------------------------------------------------

void ToolHandle::setPseudoTool(QString name) {
  QString oldToolName = m_oldToolName;
  setTool(name);
  m_oldToolName = oldToolName;
}

//-----------------------------------------------------------------------------

void ToolHandle::unsetPseudoTool() {
  if (m_toolName != m_oldToolName) setTool(m_oldToolName);
}

//-----------------------------------------------------------------------------

void ToolHandle::setToolBusy(bool value) {
  m_toolIsBusy = value;
  if (!m_toolIsBusy) emit toolEditingFinished();
}

//-----------------------------------------------------------------------------

QIcon currentIcon;

static QIcon getCurrentIcon() { return currentIcon; }

//-----------------------------------------------------------------------------

/*
void ToolHandle::changeTool(QAction* action)
{
}
*/

//-----------------------------------------------------------------------------

void ToolHandle::onImageChanged(TImage::Type imageType) {
  TTool::ToolTargetType targetType = TTool::EmptyTarget;

  switch (imageType) {
  case TImage::RASTER:
    targetType = TTool::RasterImage;
    break;
  case TImage::TOONZ_RASTER:
    targetType = TTool::ToonzImage;
    break;
  case TImage::MESH:
    targetType = TTool::MeshImage;
    break;
  case TImage::META:
    targetType = TTool::MetaImage;
    break;
  case TImage::VECTOR:
    targetType = TTool::VectorImage;
    break;
  default:
    targetType = TTool::EmptyTarget;
    break;
  }

  if (targetType != m_toolTargetType) {
    m_toolTargetType = targetType;
    setTool(m_toolName);
  }

  if (m_tool) {
    m_tool->updateMatrix();
    m_tool->onImageChanged();
  }
  if (m_suspendedTool) {
    m_suspendedTool->updateMatrix();
    m_suspendedTool->onImageChanged();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::updateMatrix() {
  if (m_tool) m_tool->updateMatrix();
}
