#include "nagram/chats/folders.h"

#include "nagram/chats/options.h"
#include "boxes/choose_filter_box.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_nagram_compose.h"

namespace Nagram::Chats {
namespace {

void ChooseFoldersBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<History*> history) {
	box->setTitle(tr::lng_nagram_join_folders_title());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_nagram_join_folders_about(),
		st::boxLabel));
	struct Row {
		FilterId id = 0;
		bool was = false;
		not_null<Ui::Checkbox*> checkbox;
	};
	auto rows = std::vector<Row>();
	for (const auto &filter : history->owner().chatsFilters().list()) {
		if (!filter.id()) {
			continue;
		}
		const auto was = filter.contains(history);
		rows.push_back({
			.id = filter.id(),
			.was = was,
			.checkbox = box->addRow(object_ptr<Ui::Checkbox>(
				box,
				filter.titleText().text,
				was)),
		});
	}
	box->addButton(tr::lng_settings_save(), [=] {
		const auto validator = ChooseFilterValidator(history);
		auto failed = false;
		for (const auto &row : rows) {
			const auto now = row.checkbox->checked();
			if (now == row.was) {
				continue;
			} else if (now && validator.canAdd(row.id)) {
				validator.add(row.id);
			} else if (!now && validator.canRemove(row.id)) {
				validator.remove(row.id);
			} else {
				failed = true;
			}
		}
		if (failed) {
			controller->showToast(tr::lng_nagram_join_folders_failed(tr::now));
		}
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

const style::SettingsSlider &FiltersTabsStyle(
		const style::SettingsSlider &fallback) {
	return ForDevice().Get(kCompactFolderTabs)
		? st::nagramCompactFiltersTabs
		: fallback;
}

bool GlobalSearchDisabled() {
	return ForDevice().Get(kDisableGlobalSearch);
}

void WatchJoinedChats(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::ChannelAmIn
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		const auto channel = update.peer->asChannel();
		return channel
			&& channel->amIn()
			&& ForDevice().Get(kChooseFolderAfterJoin)
			&& (controller->activeChatCurrent().peer() == update.peer)
			&& session->data().chatsFilters().has();
	}) | rpl::on_next([=](const Data::PeerUpdate &update) {
		const auto history = session->data().history(update.peer);
		controller->show(Box(ChooseFoldersBox, controller, history));
	}, controller->lifetime());
}

} // namespace Nagram::Chats
