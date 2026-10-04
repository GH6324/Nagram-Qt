#pragma once

#include <QtCore/QString>

namespace Nagram::Updates {

[[nodiscard]] QString ReleasesUrl();
[[nodiscard]] QString DownloadPrefix();
[[nodiscard]] QString FeedUrl();

// A feed entry is newer when its "released" number is above this one.
[[nodiscard]] quint64 FeedVersion();

[[nodiscard]] bool SkipTelegramFeed();

} // namespace Nagram::Updates
