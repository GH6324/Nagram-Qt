#pragma once

#include "nagram/core/options.h"

namespace Nagram::Links {

enum class HashtagPage { Follow, ThisChat, MyMessages };

inline constexpr auto kDisableOfficialAutoLogin = Option<bool>{
	"nagram.disableOfficialAutoLogin", Scope::Device, false,
	Category::Rules, "lng_nagram_disable_official_auto_login" };
inline constexpr auto kHashtagSearchPageChannel = Option<int>{
	"nagram.hashtagSearchPageChannel", Scope::Device, 0,
	Category::Rules, "lng_nagram_hashtag_page_channel", 0,
	[](const int &value) { return value >= 0 && value <= 2; } };
inline constexpr auto kHashtagSearchPageChat = Option<int>{
	"nagram.hashtagSearchPageChat", Scope::Device, 0,
	Category::Rules, "lng_nagram_hashtag_page_chat", 0,
	[](const int &value) { return value >= 0 && value <= 2; } };

[[nodiscard]] inline HashtagPage ResolveHashtagPage(
		bool clicked,
		bool broadcast,
		int channelValue,
		int chatValue) {
	const auto value = broadcast ? channelValue : chatValue;
	return !clicked
		? HashtagPage::Follow
		: (value == 1)
		? HashtagPage::ThisChat
		: (value == 2)
		? HashtagPage::MyMessages
		: HashtagPage::Follow;
}

inline void RegisterBehaviorOptions(Registry &registry) {
	Expects(registry.Add(kDisableOfficialAutoLogin));
	Expects(registry.Add(kHashtagSearchPageChannel));
	Expects(registry.Add(kHashtagSearchPageChat));
}

} // namespace Nagram::Links
