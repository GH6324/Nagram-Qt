#include "nagram/core/updates.h"

#include "nagram/core/version.h"
#include "core/version.h"

namespace Nagram::Updates {

QString ReleasesUrl() {
	return u"https://github.com/NextAlone/Nagram-qt/releases"_q;
}

QString DownloadPrefix() {
	return ReleasesUrl() + u"/download"_q;
}

QString FeedUrl() {
	return DownloadPrefix() + u"/updates/nagram-updates.json"_q;
}

quint64 FeedVersion() {
	return quint64(AppVersion) * 1000 + UpdateRevision();
}

bool SkipTelegramFeed() {
	return true;
}

} // namespace Nagram::Updates
