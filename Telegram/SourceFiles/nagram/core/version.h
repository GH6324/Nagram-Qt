#pragma once

#include <QtCore/QString>

namespace Nagram {

// The upstream version followed by the Nagram revision, like "7.2.10.3".
[[nodiscard]] QString VersionString();

[[nodiscard]] bool VersionIsBeta();

} // namespace Nagram
