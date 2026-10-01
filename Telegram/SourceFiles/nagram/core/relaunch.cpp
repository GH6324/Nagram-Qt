#include "nagram/core/relaunch.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>

namespace Nagram {

bool RelaunchWithoutUpdater(const QStringList &arguments) {
	auto bundle = QDir(QCoreApplication::applicationDirPath());
	if (!bundle.cdUp() || !bundle.cdUp()) {
		return false;
	} else if (QFile::exists(bundle.filePath(u"Contents/Frameworks/Updater"_q))) {
		return false;
	}
	// The new instance must start after this one releases the single-instance lock.
	const auto script = u"while /bin/kill -0 \"$1\" 2>/dev/null; do /bin/sleep 0.2; done; "
		"bundle=\"$2\"; shift 2; exec /usr/bin/open -n \"$bundle\" --args \"$@\""_q;
	auto full = QStringList{
		u"-c"_q,
		script,
		u"sh"_q,
		QString::number(QCoreApplication::applicationPid()),
		bundle.absolutePath(),
	};
	full.append(arguments);
	return QProcess::startDetached(u"/bin/sh"_q, full);
}

} // namespace Nagram
