#pragma once

#include <QtCore/QString>

namespace Nagram {

// The upstream version followed by the Nagram revision, like "7.2.10.3".
[[nodiscard]] QString VersionString();

[[nodiscard]] bool VersionIsBeta();

// The Nagram revision, the low half of the 64-bit update version.
[[nodiscard]] quint32 UpdateRevision();

} // namespace Nagram
