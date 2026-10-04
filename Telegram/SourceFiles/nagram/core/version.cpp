#include "nagram/core/version.h"

#include "nagram_version_data.h"

namespace Nagram {

QString VersionString() {
	return QString::fromLatin1(kVersionStr);
}

bool VersionIsBeta() {
	return kVersionBeta;
}

quint32 UpdateRevision() {
	return kVersionRevision;
}

} // namespace Nagram
