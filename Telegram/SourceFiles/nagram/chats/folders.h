#pragma once

#include "base/unique_qptr.h"

namespace style {
struct SettingsSlider;
namespace internal {
class Icon;
} // namespace internal
} // namespace style
namespace Data {
class ChatFilter;
} // namespace Data
namespace Ui {
class PopupMenu;
class VerticalLayout;
} // namespace Ui
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

[[nodiscard]] rpl::producer<bool> SavedInFolderListValue();
[[nodiscard]] std::vector<Data::ChatFilter> FolderTabs(
	std::vector<Data::ChatFilter> list,
	bool main);
[[nodiscard]] std::vector<const style::internal::Icon*> FolderTabIcons(
	const std::vector<Data::ChatFilter> &tabs,
	std::vector<const style::internal::Icon*> icons);
[[nodiscard]] int ActiveFolderTab(
	not_null<Window::SessionController*> controller,
	const std::vector<Data::ChatFilter> &list,
	int fallback);
void OpenSavedFromFolderList(
	not_null<Window::SessionController*> controller);
[[nodiscard]] base::unique_qptr<Ui::PopupMenu> SavedFolderMenu(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller);
void SetupSavedFolderButton(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller);

} // namespace Nagram::Chats
