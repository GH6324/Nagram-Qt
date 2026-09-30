#pragma once

namespace style {
struct SettingsSlider;
} // namespace style
namespace Window {
class SessionController;
} // namespace Window

namespace Nagram::Chats {

[[nodiscard]] const style::SettingsSlider &FiltersTabsStyle(
	const style::SettingsSlider &fallback);
[[nodiscard]] bool GlobalSearchDisabled();
void WatchJoinedChats(not_null<Window::SessionController*> controller);

} // namespace Nagram::Chats
