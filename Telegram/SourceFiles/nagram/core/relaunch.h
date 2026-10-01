#pragma once

#include <QtCore/QStringList>

namespace Nagram {

// Returns false when the bundled Updater should handle the relaunch.
[[nodiscard]] bool RelaunchWithoutUpdater(
	const QString &bundlePath,
	const QStringList &arguments);

} // namespace Nagram
