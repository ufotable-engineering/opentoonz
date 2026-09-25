#include "inhouseupdate.h"

#include "inhouseversion.h"
#include "inhouseversion_config.h"

// Qt includes
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QVersionNumber>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

const QString scriptName("inhouse_update.ps1");

// The version text comes from the network and ends up in file names and URLs
bool isReleaseVersion(const QString &version) {
  static const QRegularExpression re("^\\d{4}\\.\\d{2}\\.\\d+$");
  return re.match(version).hasMatch();
}

QString appDir() { return QCoreApplication::applicationDirPath(); }

QString updateDir() { return appDir() + "/update"; }

QString zipPath(const QString &version) {
  return updateDir() + "/" + version + ".zip";
}

QString nativePath(const QString &path) {
  return QDir::toNativeSeparators(path);
}

bool isProcessRunning(qint64 pid) {
#ifdef _WIN32
  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
  if (!process) return false;
  DWORD code   = 0;
  bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
  CloseHandle(process);
  return running;
#else
  return false;
#endif
}

}  // namespace

//-----------------------------------------------------------------------------

bool InhouseUpdate::isAvailable() {
#ifdef _WIN32
  if (!isReleaseVersion(InhouseVersion::version())) return false;
  QDir dir(appDir());
  if (!dir.exists("portablestuff") || !dir.exists(scriptName)) return false;
  if (!dir.mkpath("update")) return false;
  // QFileInfo::isWritable() ignores NTFS permissions, so actually try it
  QTemporaryFile probe(updateDir() + "/probe");
  return probe.open();
#else
  return false;
#endif
}

//-----------------------------------------------------------------------------

bool InhouseUpdate::isDownloaded(const QString &version) {
  return isReleaseVersion(version) && QFile::exists(zipPath(version));
}

//-----------------------------------------------------------------------------

void InhouseUpdate::download(const QString &version) {
  if (!isReleaseVersion(version) || !QDir().mkpath(updateDir())) return;

  QUrl url(QString::fromUtf8(INHOUSE_RELEASE_URL) + "/download/integration-" +
           version + "/Opentoonz-Windows-" + version + ".zip");

  QNetworkAccessManager *manager = new QNetworkAccessManager(qApp);
  manager->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

  // An interrupted download must not leave a zip that looks complete
  QSaveFile *file = new QSaveFile(zipPath(version), manager);
  if (!file->open(QIODevice::WriteOnly)) {
    manager->deleteLater();
    return;
  }

  QNetworkReply *reply = manager->get(QNetworkRequest(url));
  QObject::connect(reply, &QNetworkReply::readyRead,
                   [reply, file]() { file->write(reply->readAll()); });
  QObject::connect(reply, &QNetworkReply::finished, [reply, file, manager]() {
    file->write(reply->readAll());
    if (reply->error() == QNetworkReply::NoError)
      file->commit();
    else
      file->cancelWriting();
    reply->deleteLater();
    manager->deleteLater();
  });
}

//-----------------------------------------------------------------------------

bool InhouseUpdate::applyPendingUpdate(QWidget *parent) {
  if (!isAvailable()) return false;

  QDir dir(updateDir());

  // Users may start OpenToonz again while the updater is still replacing files
  QFile inProgress(dir.filePath("in_progress"));
  if (inProgress.open(QIODevice::ReadOnly)) {
    qint64 pid        = inProgress.readAll().trimmed().toLongLong();
    QDateTime started = QFileInfo(inProgress).lastModified();
    inProgress.close();
    if (isProcessRunning(pid) &&
        started.secsTo(QDateTime::currentDateTime()) < 600) {
      QMessageBox::information(
          parent, QObject::tr("Update"),
          QObject::tr("OpenToonz is being updated and will start "
                      "automatically when finished."));
      return true;
    }
    // The updater was killed or never ran, e.g. when a group policy blocks
    // scripts. Offering the same zip again would repeat that, so drop it and
    // let the update check offer the web site again.
    dir.remove("in_progress");
    for (const QString &name : dir.entryList({"*.zip"}, QDir::Files))
      dir.remove(name);
    QMessageBox::warning(
        parent, QObject::tr("Update"),
        QObject::tr("The last update did not finish. If this happens again, "
                    "download the new version from the web site."));
  }

  if (dir.exists("failed")) {
    QMessageBox::warning(
        parent, QObject::tr("Update"),
        QObject::tr("The last update failed. See %1 for details.")
            .arg(nativePath(dir.filePath("update.log"))));
    dir.remove("failed");
  }

  QString latest;
  for (const QString &name : dir.entryList({"*.zip"}, QDir::Files)) {
    QString version = QFileInfo(name).completeBaseName();
    if (!isReleaseVersion(version)) continue;
    if (!InhouseVersion::isNewer(version)) {
      dir.remove(name);
      continue;
    }
    if (latest.isEmpty() || QVersionNumber::fromString(version) >
                                QVersionNumber::fromString(latest))
      latest = version;
  }
  if (latest.isEmpty()) return false;

  QMessageBox box(QMessageBox::Question, QObject::tr("Update"),
                  QObject::tr("Version %1 has been downloaded.\nUpdate now? "
                              "OpenToonz will restart.")
                      .arg(latest),
                  QMessageBox::NoButton, parent);
  QPushButton *updateButton =
      box.addButton(QObject::tr("Update Now"), QMessageBox::AcceptRole);
  box.addButton(QObject::tr("Later"), QMessageBox::RejectRole);
  box.exec();
  if (box.clickedButton() != updateButton) return false;

  // Run a copy so that the updater does not replace the script it is running
  QString script = updateDir() + "/" + scriptName;
  QFile::remove(script);
  if (!QFile::copy(appDir() + "/" + scriptName, script)) return false;

  QStringList args = {"-NoProfile",
                      "-ExecutionPolicy",
                      "Bypass",
                      "-WindowStyle",
                      "Hidden",
                      "-File",
                      nativePath(script),
                      "-Zip",
                      nativePath(zipPath(latest)),
                      "-InstallDir",
                      nativePath(appDir()),
                      "-ProcessId",
                      QString::number(QCoreApplication::applicationPid()),
                      "-Exe",
                      nativePath(QCoreApplication::applicationFilePath())};

  qint64 pid = 0;
  if (!QProcess::startDetached("powershell.exe", args, QString(), &pid))
    return false;
  // Written here rather than by the script so that it already exists by the
  // time this process has exited. The script leaves it alone until then.
  QFile marker(dir.filePath("in_progress"));
  if (marker.open(QIODevice::WriteOnly)) marker.write(QByteArray::number(pid));
  return true;
}
