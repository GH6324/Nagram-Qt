#include "nagram/chats/folders.h"

#include "nagram/chats/options.h"
#include "boxes/choose_filter_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/chat_filters_tabs_mode.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/side_bar_button.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_nagram_compose.h"
#include "styles/style_window.h"

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

constexpr auto kSavedTabId = FilterId(-1);

// WHY: openFolder resets the filter to "All chats" before it marks the
// folder opened, so the redirect away from a hidden "All chats" has to
// be told that this reset is on purpose.
Window::SessionController *EnteringFolder = nullptr;

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

void WatchFolderReturn(not_null<Window::SessionController*> controller) {
	const auto origin = controller->lifetime().make_state<FilterId>(0);
	controller->activeChatsFilter(
	) | rpl::combine_previous(
	) | rpl::on_next([=](FilterId was, FilterId now) {
		*origin = (EnteringFolder == controller && !now) ? was : FilterId();
	}, controller->lifetime());

	controller->openedFolder().changes(
	) | rpl::filter([=](Data::Folder *folder) {
		return !folder && !controller->activeChatsFilterCurrent();
	}) | rpl::on_next([=] {
		const auto filters = &controller->session().data().chatsFilters();
		const auto wanted = base::take(*origin);
		const auto exists = wanted
			&& ranges::contains(
				filters->list(),
				wanted,
				&Data::ChatFilter::id);
		const auto id = exists
			? wanted
			: filters->allChatsHidden()
			? filters->displayList().front().id()
			: FilterId();
		if (id) {
			controller->setActiveChatsFilter(id);
		}
	}, controller->lifetime());
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

void WatchFolders(not_null<Window::SessionController*> controller) {
	WatchJoinedChats(controller);
	WatchFolderReturn(controller);
}

void ResetFilterForFolder(not_null<Window::SessionController*> controller) {
	const auto was = std::exchange(EnteringFolder, controller.get());
	const auto guard = gsl::finally([=] { EnteringFolder = was; });
	controller->setActiveChatsFilter(0);
}

bool RedirectFromAllChats(not_null<Window::SessionController*> controller) {
	return (EnteringFolder != controller)
		&& !controller->openedFolder().current()
		&& controller->session().data().chatsFilters().allChatsHidden();
}

rpl::producer<bool> SavedInFolderListValue() {
	return ForDevice().Value(kSavedInFolderList);
}

std::vector<Data::ChatFilter> FolderTabs(
		std::vector<Data::ChatFilter> list,
		bool main) {
	if (main && ForDevice().Get(kSavedInFolderList)) {
		list.push_back(Data::ChatFilter(
			kSavedTabId,
			{ TextWithEntities{ tr::lng_saved_messages(tr::now) } },
			QString(),
			std::nullopt,
			Data::ChatFilter::Flags(),
			{},
			{},
			{}));
	}
	return list;
}

std::vector<const style::internal::Icon*> FolderTabIcons(
		const std::vector<Data::ChatFilter> &tabs,
		std::vector<const style::internal::Icon*> icons) {
	for (auto i = 0, count = int(tabs.size()); i != count; ++i) {
		if (tabs[i].id() == kSavedTabId) {
			icons[i] = &st::nagramFoldersTabsSaved;
		}
	}
	return icons;
}

int ActiveFolderTab(
		not_null<Window::SessionController*> controller,
		const std::vector<Data::ChatFilter> &list,
		int fallback) {
	const auto i = ranges::find(
		list,
		controller->activeChatsFilterCurrent(),
		&Data::ChatFilter::id);
	return (i != end(list)) ? int(i - begin(list)) : std::max(fallback, 0);
}

void OpenSavedFromFolderList(
		not_null<Window::SessionController*> controller) {
	controller->showPeerHistory(controller->session().userPeerId());
}

base::unique_qptr<Ui::PopupMenu> SavedFolderMenu(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto history = controller->session().data().history(
		controller->session().userPeerId());
	const auto addAction = Ui::Menu::CreateAddActionCallback(result.get());
	addAction(tr::lng_context_new_window(tr::now), crl::guard(controller, [=] {
		controller->showInNewWindow(Window::SeparateId(
			Window::SeparateType::Chat,
			history));
	}), &st::menuIconNewWindow);
	addAction(tr::lng_dlg_filter(tr::now), crl::guard(controller, [=] {
		controller->searchInChat(history);
	}), &st::menuIconSearch);
	addAction(tr::lng_nagram_hide_folder_entry(tr::now), [] {
		Expects(ForDevice().Set(kSavedInFolderList, false));
	}, &st::menuIconCancel);
	return result;
}

void SetupSavedFolderButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	const auto holder = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	const auto menu = holder->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	rpl::combine(
		SavedInFolderListValue(),
		Core::App().settings().chatFiltersTabsModeValue()
	) | rpl::on_next([=](bool shown, Ui::ChatsFiltersTabsMode value) {
		using Mode = Ui::ChatsFiltersTabsMode;
		holder->clear();
		if (shown) {
			const auto mode = Ui::VerticalChatsFiltersTabsMode(value);
			const auto button = holder->add(object_ptr<Ui::SideBarButton>(
				holder,
				TextWithEntities{ tr::lng_saved_messages(tr::now) },
				((mode == Mode::TextOnly)
					? st::windowFiltersButtonTextOnly
					: (mode == Mode::IconsOnly)
					? st::windowFiltersButtonIconsOnly
					: st::windowFiltersButton)));
			button->setIconOverride(
				&st::nagramFoldersSaved,
				&st::nagramFoldersSavedActive);
			button->setShowIcon(mode != Mode::TextOnly);
			button->setShowText(mode != Mode::IconsOnly);
			button->setClickedCallback([=] {
				OpenSavedFromFolderList(controller);
			});
			button->events(
			) | rpl::filter([](not_null<QEvent*> e) {
				return (e->type() == QEvent::ContextMenu);
			}) | rpl::on_next([=](not_null<QEvent*> e) {
				*menu = SavedFolderMenu(button, controller);
				(*menu)->popup(QCursor::pos());
				e->accept();
			}, button->lifetime());
		}
		holder->resizeToWidth(st::windowFiltersWidth);
	}, holder->lifetime());
}

} // namespace Nagram::Chats
