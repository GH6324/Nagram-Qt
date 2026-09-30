#pragma once

#include "nagram/chats/options.h"
#include "lang/lang_keys.h"
#include "styles/style_menu_icons.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Nagram::Chats {

void WatchRecentChats(not_null<Window::SessionController*> controller);
void ShowRecentChats(not_null<Window::SessionController*> controller);

template <typename AddAction>
void AddRecentChatsMenuItem(
		AddAction &&addAction,
		not_null<Window::SessionController*> controller) {
	if (!ForDevice().Get(kRecentChats)) {
		return;
	}
	addAction(
		tr::lng_nagram_recent_chats(),
		{ &st::menuIconTimer },
		u"recentChats"_q
	)->setClickedCallback([=] {
		ShowRecentChats(controller);
	});
}

} // namespace Nagram::Chats
