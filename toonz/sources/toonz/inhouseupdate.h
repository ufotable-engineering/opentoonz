#pragma once

#ifndef INHOUSEUPDATE_INCLUDED
#define INHOUSEUPDATE_INCLUDED

#include <QString>

class QWidget;

// Self-update of in-house Windows portable builds. The release zip is
// downloaded in the background and applied on the next launch by
// inhouse_update.ps1, which replaces the program files but never overwrites
// anything already in portablestuff.
namespace InhouseUpdate {

// False unless this is a released in-house build running from a writable
// portable folder that ships the updater script
bool isAvailable();

bool isDownloaded(const QString &version);

// Result is only picked up on the next launch; failures are silently retried
// by the next update check
void download(const QString &version);

// Returns true when the updater has been started and the application must
// quit immediately
bool applyPendingUpdate(QWidget *parent);

}  // namespace InhouseUpdate

#endif  // INHOUSEUPDATE_INCLUDED
