#include "nagram/messages/online.h"

#include "nagram/messages/options.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_dialogs.h"

namespace Nagram::Messages {
namespace {

enum class Presence { None, Online, Recently };

Presence ComputePresence(not_null<PeerData*> peer) {
	const auto mode = ForDevice().Get(kSenderOnlineStatus);
	const auto user = peer->asUser();
	if (!mode || !user || user->isSelf() || user->isBot()
		|| user->isServiceUser()) {
		return Presence::None;
	}
	const auto lastseen = user->lastseen();
	if (lastseen.isOnline(base::unixtime::now())) {
		return Presence::Online;
	}
	return (mode == 2 && lastseen.isRecently())
		? Presence::Recently
		: Presence::None;
}

} // namespace

void PaintSenderOnline(
		QPainter &p,
		not_null<PeerData*> peer,
		int x,
		int y,
		int size) {
	const auto presence = ComputePresence(peer);
	if (presence == Presence::None) {
		return;
	}
	const auto badge = st::dialogsOnlineBadgeSize;
	const auto stroke = st::dialogsOnlineBadgeStroke;
	const auto skip = st::dialogsOnlineBadgeSkip;
	auto hq = PainterHighQualityEnabler(p);
	auto pen = QPen(st::windowBg);
	pen.setWidthF(stroke);
	p.setPen(pen);
	p.setBrush((presence == Presence::Online)
		? st::dialogsOnlineBadgeFg
		: st::windowSubTextFg);
	p.drawEllipse(QRectF(
		x + size - skip.x() - badge,
		y + size - skip.y() - badge,
		badge,
		badge));
}

void RepaintOnSenderOnline(
		not_null<Ui::RpWidget*> widget,
		not_null<Main::Session*> session) {
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::OnlineStatus
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return update.peer->isUser()
			&& widget->isVisible()
			&& ForDevice().Get(kSenderOnlineStatus) != 0;
	}) | rpl::on_next([=] {
		widget->update();
	}, widget->lifetime());
}

} // namespace Nagram::Messages
