#pragma once

#ifndef INHOUSEVERSION_INCLUDED
#define INHOUSEVERSION_INCLUDED

#include <QString>

// In-house build identity. Everything here is a no-op unless the build was
// configured with INHOUSE_VERSION, so plain upstream builds are unaffected.
namespace InhouseVersion {

bool isEnabled();

// "2026.09.1", "dev", or empty when not configured
QString version();

// Appends " (integration <version>)" to the application full name so the
// window title, About dialog and crash reports show which build is running.
void applyToEnv();

}  // namespace InhouseVersion

#endif  // INHOUSEVERSION_INCLUDED
