#include "nagram/menu/message_tools.h"

#include "nagram/menu/actions.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Nagram::Menu {
namespace {

constexpr auto kActionIdProperty = "nagramMenuActionId";

QString FormatDate(TimeId date) {
	return base::unixtime::parse(date).toString(Qt::ISODate);
}

QString PeerLine(not_null<PeerData*> peer) {
	return peer->name() + u" ("_q + QString::number(peer->id.value) + u')';
}

QString Details(not_null<HistoryItem*> item) {
	auto lines = QStringList();
	const auto add = [&](QString name, QString value) {
		if (!value.isEmpty()) {
			lines.push_back(name + u": "_q + value);
		}
	};
	add(tr::lng_nagram_details_message_id(tr::now),
		QString::number(item->id.bare));
	add(tr::lng_nagram_details_chat(tr::now), PeerLine(item->history()->peer));
	add(tr::lng_nagram_details_sender(tr::now), PeerLine(item->from()));
	add(tr::lng_nagram_details_date(tr::now), FormatDate(item->date()));
	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		add(tr::lng_nagram_details_edited(tr::now), FormatDate(edited->date));
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (forwarded->originalSender) {
			add(tr::lng_nagram_details_forwarded_from(tr::now),
				PeerLine(forwarded->originalSender));
		} else if (!forwarded->originalPostAuthor.isEmpty()) {
			add(tr::lng_nagram_details_forwarded_from(tr::now),
				forwarded->originalPostAuthor);
		}
		if (forwarded->originalDate) {
			add(tr::lng_nagram_details_forwarded_date(tr::now),
				FormatDate(forwarded->originalDate));
		}
		if (forwarded->originalId) {
			add(tr::lng_nagram_details_forwarded_id(tr::now),
				QString::number(forwarded->originalId.bare));
		}
	}
	if (const auto bot = item->viaBot()) {
		add(tr::lng_nagram_details_via_bot(tr::now), PeerLine(bot));
	}
	if (const auto group = item->groupId()) {
		add(tr::lng_nagram_details_album(tr::now),
			QString::number(group.value));
	}
	if (const auto views = item->viewsCount(); views >= 0) {
		add(tr::lng_nagram_details_views(tr::now), QString::number(views));
	}
	return lines.join(u'\n');
}

void ShowDetails(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	const auto item = controller->session().data().message(itemId);
	if (!item) {
		return;
	}
	const auto text = Details(item);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_nagram_menu_details());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box, text, st::boxLabel))->setSelectable(true);
		box->addButton(tr::lng_nagram_details_copy(), [=] {
			QGuiApplication::clipboard()->setText(text);
			box->showToast(tr::lng_nagram_details_copied(tr::now));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

bool CanSaveToSaved(not_null<HistoryItem*> item) {
	return item->isRegular()
		&& item->allowsForward()
		&& !item->history()->peer->isSelf();
}

void SaveToSaved(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	auto &session = controller->session();
	const auto item = session.data().message(itemId);
	if (!item || !CanSaveToSaved(item)) {
		controller->showToast(tr::lng_nagram_menu_batch_unavailable(tr::now));
		return;
	}
	const auto history = item->history();
	auto resolved = history->resolveForwardDraft(Data::ForwardDraft{
		.ids = history->owner().itemOrItsGroup(item),
		.options = Data::ForwardOptions::PreserveInfo,
	});
	if (resolved.items.empty()) {
		return;
	}
	auto action = Api::SendAction(session.data().history(session.user()));
	action.clearDraft = false;
	action.generateLocal = false;
	session.api().forwardMessages(
		std::move(resolved),
		action,
		crl::guard(controller, [=] {
			controller->showToast(tr::lng_nagram_menu_saved_done(tr::now));
		}));
}

int EndPosition(not_null<Ui::PopupMenu*> menu) {
	for (auto index = 0; index != int(menu->actions().size()); ++index) {
		const auto value = menu->actions()[index]->property(kActionIdProperty);
		if (value.isValid()
			&& value.toInt() == static_cast<int>(ActionId::Delete)) {
			return index;
		}
	}
	return int(menu->actions().size());
}

void Insert(
		not_null<Ui::PopupMenu*> menu,
		int position,
		ActionId id,
		const QString &text,
		const style::icon *icon,
		Fn<void()> callback) {
	const auto action = Ui::Menu::CreateAction(menu, text, std::move(callback));
	auto widget = base::make_unique_q<Ui::Menu::Action>(
		menu->menu(), menu->menu()->st(), action, icon, icon);
	Tag(menu->insertAction(position, std::move(widget)), id);
}

} // namespace

void InsertMessageToolActions(
		Ui::PopupMenu *menu,
		HistoryItem *item,
		Window::SessionController *controller,
		Fn<void(HistoryItem*)> select) {
	if (!menu || !controller || !item) {
		return;
	}
	const auto itemId = item->fullId();
	auto position = EndPosition(menu);
	Insert(menu, position++, ActionId::MessageDetails,
		tr::lng_nagram_menu_details(tr::now), &st::menuIconInfo,
		crl::guard(controller, [=] { ShowDetails(controller, itemId); }));
	if (CanSaveToSaved(item)) {
		Insert(menu, position++, ActionId::SaveToSaved,
			tr::lng_nagram_menu_save_to_saved(tr::now),
			&st::menuIconSavedMessages,
			crl::guard(controller, [=] { SaveToSaved(controller, itemId); }));
	}
	if (select && item->canBeSelected()) {
		Insert(menu, position, ActionId::SelectAll,
			tr::lng_nagram_menu_select_all(tr::now), &st::menuIconSelect,
			crl::guard(controller, [=] { select(nullptr); }));
	}
}

} // namespace Nagram::Menu
