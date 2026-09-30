#include "nagram/chats/recent_chats.h"

#include "boxes/peer_list_box.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Nagram::Chats {
namespace {

constexpr auto kMaximumRecentChats = 30; // matches ValidRecentChats

std::vector<PeerId> ReadRecent(not_null<Main::Session*> session) {
	auto result = std::vector<PeerId>();
	const auto value = ForAccount(session).Get(kRecentChatsList);
	for (const auto &part : value.split(u',', Qt::SkipEmptyParts)) {
		result.push_back(PeerId(part.toULongLong()));
	}
	return result;
}

void WriteRecent(
		not_null<Main::Session*> session,
		const std::vector<PeerId> &ids) {
	auto parts = QStringList();
	for (const auto id : ids) {
		parts.push_back(QString::number(id.value));
	}
	Expects(ForAccount(session).Set(kRecentChatsList, parts.join(u',')));
}

void Remember(not_null<PeerData*> peer) {
	const auto session = &peer->session();
	auto ids = ReadRecent(session);
	ids.erase(ranges::remove(ids, peer->id), ids.end());
	ids.insert(ids.begin(), peer->id);
	if (ids.size() > kMaximumRecentChats) {
		ids.resize(kMaximumRecentChats);
	}
	WriteRecent(session, ids);
}

class RecentChatsController final : public PeerListController {
public:
	explicit RecentChatsController(
		not_null<Window::SessionController*> window)
	: _window(window) {
	}

	Main::Session &session() const override {
		return _window->session();
	}

	void prepare() override {
		auto &owner = session().data();
		const auto current = _window->activeChatCurrent().peer();
		for (const auto id : ReadRecent(&session())) {
			const auto peer = owner.peerLoaded(id);
			if (peer && peer != current) {
				delegate()->peerListAppendRow(
					std::make_unique<PeerListRow>(peer));
			}
		}
		if (!delegate()->peerListFullRowsCount()) {
			setDescriptionText(tr::lng_nagram_recent_chats_empty(tr::now));
		}
		delegate()->peerListRefreshRows();
	}

	void rowClicked(not_null<PeerListRow*> row) override {
		const auto peer = row->peer();
		const auto window = _window;
		window->hideLayer();
		window->showPeerHistory(peer, Window::SectionShow::Way::ClearStack);
	}

private:
	const not_null<Window::SessionController*> _window;

};

} // namespace

void WatchRecentChats(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	controller->activeChatChanges(
	) | rpl::filter([](const Dialogs::Key &key) {
		return key.peer() && ForDevice().Get(kRecentChats);
	}) | rpl::on_next([](const Dialogs::Key &key) {
		Remember(key.peer());
	}, controller->lifetime());
	ForDevice().Value(kRecentChats) | rpl::filter([](bool enabled) {
		return !enabled;
	}) | rpl::on_next([=] {
		WriteRecent(session, {});
	}, controller->lifetime());
}

void ShowRecentChats(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	controller->show(Box<PeerListBox>(
		std::make_unique<RecentChatsController>(controller),
		[=](not_null<PeerListBox*> box) {
			box->setTitle(tr::lng_nagram_recent_chats());
			box->addButton(tr::lng_close(), [=] { box->closeBox(); });
			box->addLeftButton(tr::lng_nagram_recent_chats_clear(), [=] {
				WriteRecent(session, {});
				box->closeBox();
			});
		}));
}

} // namespace Nagram::Chats
