#pragma once

class PeerData;
class QSize;
class QString;
struct ClickContext;

namespace Dialogs {
struct SearchState;
} // namespace Dialogs

namespace Nagram::Links {

[[nodiscard]] bool AutoLoginDisabled();

class HashtagClickScope final {
public:
	HashtagClickScope(const ClickContext &context, const QString &tag);
	~HashtagClickScope();

private:
	const bool _wasActive = false;
	PeerData * const _wasPeer = nullptr;

};

void ApplyHashtagSearchPage(Dialogs::SearchState &state);

[[nodiscard]] QSize WebAppPanelSize(QSize base);

} // namespace Nagram::Links
