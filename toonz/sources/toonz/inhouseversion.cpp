#include "inhouseversion.h"

#include "inhouseversion_config.h"

// TnzBase includes
#include "tenv.h"

// Qt includes
#include <QVersionNumber>

namespace {

// Three segments required, so "dev" and the upstream "1.8" both come out null
QVersionNumber calVer(const QString &text) {
  QVersionNumber v = QVersionNumber::fromString(text.trimmed());
  return v.segmentCount() == 3 ? v : QVersionNumber();
}

}  // namespace

//-----------------------------------------------------------------------------

bool InhouseVersion::isEnabled() { return !version().isEmpty(); }

//-----------------------------------------------------------------------------

QString InhouseVersion::version() { return QString::fromUtf8(INHOUSE_VERSION); }

//-----------------------------------------------------------------------------

void InhouseVersion::applyToEnv() {
  if (!isEnabled()) return;
  TEnv::setApplicationFullName(TEnv::getApplicationFullName() +
                               " (integration " INHOUSE_VERSION ")");
}

//-----------------------------------------------------------------------------

QString InhouseVersion::releasePageUrl() {
  return QString::fromUtf8(INHOUSE_RELEASE_URL) + "/latest";
}

//-----------------------------------------------------------------------------

QString InhouseVersion::versionFileUrl() {
  return releasePageUrl() + "/download/inhouse_version.txt";
}

//-----------------------------------------------------------------------------

bool InhouseVersion::isNewer(const QString &latest) {
  QVersionNumber current = calVer(version());
  return !current.isNull() && calVer(latest) > current;
}
