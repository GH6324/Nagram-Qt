#pragma once

#include "nagram/menu/model.h"
#include "data/data_types.h"
#include "base/basic_types.h"

class QAction;
class HistoryItem;
namespace Window {
class SessionController;
} // namespace Window
namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Nagram::Menu {

void Tag(QAction *action, ActionId id);
void Apply(
	Ui::PopupMenu *menu,
	HistoryItem *item,
	Window::SessionController *controller,
	MessageIdsList selected,
	Fn<void(HistoryItem*)> select); // nullptr selects everything between

} // namespace Nagram::Menu
