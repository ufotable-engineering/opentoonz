#include "inhouseupdate.h"

#include "inhouseversion.h"

// TnzQt includes
#include "toonzqt/dvdialog.h"

// Qt includes
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
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

// The marker is written right after the updater starts, so a process with the
// same PID that started later is an unrelated one that reused it
bool isUpdaterRunning(qint64 pid, const QDateTime &markerWritten) {
#ifdef _WIN32
  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
  if (!process) return false;
  DWORD code = 0;
  FILETIME created, exited, kernel, user;
  bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE &&
                 GetProcessTimes(process, &created, &exited, &kernel, &user);
  CloseHandle(process);
  if (!running) return false;
  ULARGE_INTEGER ticks;
  ticks.LowPart  = created.dwLowDateTime;
  ticks.HighPart = created.dwHighDateTime;
  // FILETIME counts 100 ns ticks from 1601-01-01
  qint64 msecs = qint64(ticks.QuadPart / 10000) - 11644473600000LL;
  return QDateTime::fromMSecsSinceEpoch(msecs) <= markerWritten;
#else
  return false;
#endif
}

}  // namespace

//-----------------------------------------------------------------------------

bool InhouseUpdate::isAvailable() {
#ifdef _WIN32
  static const bool available = []() {
    if (!isReleaseVersion(InhouseVersion::version())) return false;
    QDir dir(appDir());
    if (!dir.exists("portablestuff") || !dir.exists(scriptName)) return false;
    if (!dir.mkpath("update")) return false;
    // QFileInfo::isWritable() ignores NTFS permissions, so actually try it
    QTemporaryFile probe(updateDir() + "/probe");
    return probe.open();
  }();
  return available;
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
  if (!isReleaseVersion(version)) return;

  QNetworkAccessManager *manager = new QNetworkAccessManager(qApp);
  manager->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

  // An interrupted download must not leave a zip that looks complete
  QSaveFile *file = new QSaveFile(zipPath(version), manager);
  if (!file->open(QIODevice::WriteOnly)) {
    manager->deleteLater();
    return;
  }

  QNetworkReply *reply = manager->get(
      QNetworkRequest(QUrl(InhouseVersion::releaseZipUrl(version))));
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

  // Holds the updater's PID and the version it is installing. Users may start
  // OpenToonz again while the updater is still replacing files.
  QFile inProgress(dir.filePath("in_progress"));
  if (inProgress.open(QIODevice::ReadOnly)) {
    QStringList fields = QString::fromUtf8(inProgress.readAll()).split('\n');
    QDateTime written  = QFileInfo(inProgress).lastModified();
    inProgress.close();
    if (isUpdaterRunning(fields.value(0).toLongLong(), written)) {
      DVGui::MsgBox(DVGui::INFORMATION,
                    QObject::tr("OpenToonz is being updated and will start "
                                "automatically when finished."),
                    {QObject::tr("OK")}, 0, parent);
      return true;
    }
    // The updater was killed or never ran, e.g. when a group policy blocks
    // scripts. Offering the same zip again would repeat that, so drop it and
    // let the update check offer the web site again.
    inProgress.remove();
    QString version = fields.value(1).trimmed();
    if (isReleaseVersion(version)) QFile::remove(zipPath(version));
    DVGui::MsgBox(
        DVGui::WARNING,
        QObject::tr("The last update did not finish. If this happens again, "
                    "download the new version from the web site."),
        {QObject::tr("OK")}, 0, parent);
  }

  if (dir.exists("failed")) {
    DVGui::MsgBox(DVGui::WARNING,
                  QObject::tr("The last update failed. See %1 for details.")
                      .arg(nativePath(dir.filePath("update.log"))),
                  {QObject::tr("OK")}, 0, parent);
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

  int ret =
      DVGui::MsgBox(QObject::tr("Version %1 has been downloaded.\nUpdate "
                                "now? OpenToonz will restart.")
                        .arg(latest),
                    QObject::tr("Update Now"), QObject::tr("Later"), 0, parent);
  if (ret != 1) return false;

  // Run a copy so that the updater does not replace the script it is running
  QString script = dir.filePath(scriptName);
  QFile::remove(script);
  if (!QFile::copy(QDir(appDir()).filePath(scriptName), script)) return false;

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
  if (inProgress.open(QIODevice::WriteOnly))
    inProgress.write(QByteArray::number(pid) + '\n' + latest.toUtf8());
  return true;
}
