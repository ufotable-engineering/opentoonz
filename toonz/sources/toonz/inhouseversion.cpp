#include "inhouseversion.h"

#include "inhouseversion_config.h"

// TnzBase includes
#include "tenv.h"

//-----------------------------------------------------------------------------

bool InhouseVersion::isEnabled() { return *INHOUSE_VERSION != '\0'; }

//-----------------------------------------------------------------------------

QString InhouseVersion::version() { return QString::fromUtf8(INHOUSE_VERSION); }

//-----------------------------------------------------------------------------

void InhouseVersion::applyToEnv() {
  if (!isEnabled()) return;
  TEnv::setApplicationFullName(TEnv::getApplicationFullName() +
                               " (integration " INHOUSE_VERSION ")");
}
