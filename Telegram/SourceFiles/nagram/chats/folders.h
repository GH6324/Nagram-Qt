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
void WatchFolders(not_null<Window::SessionController*> controller);
void ResetFilterForFolder(not_null<Window::SessionController*> controller);
[[nodiscard]] bool RedirectFromAllChats(
	not_null<Window::SessionController*> controller);

} // namespace Nagram::Chats
