#include "nagram/links/behavior.h"

#include "nagram/links/options.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/ui/chat_search_in.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Nagram::Links {
namespace {

auto ClickActive = false;
PeerData *ClickPeer = nullptr;

[[nodiscard]] PeerData *PeerFromContext(const ClickContext &context) {
	const auto my = context.other.value<ClickHandlerContext>();
	if (my.peer) {
		return my.peer;
	}
	const auto controller = my.sessionWindow.get();
	const auto item = (controller && my.itemId)
		? controller->session().data().message(my.itemId)
		: nullptr;
	return item ? item->history()->peer.get() : nullptr;
}

} // namespace

bool AutoLoginDisabled() {
	return ForDevice().Get(kDisableOfficialAutoLogin);
}

HashtagClickScope::HashtagClickScope(
	const ClickContext &context,
	const QString &tag)
: _wasActive(ClickActive)
, _wasPeer(ClickPeer) {
	ClickActive = !tag.contains(u'@');
	ClickPeer = ClickActive ? PeerFromContext(context) : nullptr;
}

HashtagClickScope::~HashtagClickScope() {
	ClickActive = _wasActive;
	ClickPeer = _wasPeer;
}

void ApplyHashtagSearchPage(Dialogs::SearchState &state) {
	const auto inChat = state.inChat.peer();
	const auto peer = inChat ? inChat : ClickPeer;
	if (!peer || !state.tags.empty()) {
		return;
	}
	const auto page = ResolveHashtagPage(
		ClickActive,
		peer->isBroadcast(),
		ForDevice().Get(kHashtagSearchPageChannel),
		ForDevice().Get(kHashtagSearchPageChat));
	if (page == HashtagPage::MyMessages) {
		state.inChat = Dialogs::Key();
		state.fromPeer = nullptr;
		state.tab = Dialogs::ChatSearchTab::MyMessages;
	} else if (page == HashtagPage::ThisChat && !inChat) {
		state.inChat = peer->owner().history(peer).get();
		state.tab = state.defaultTabForMe();
	}
}

} // namespace Nagram::Links
