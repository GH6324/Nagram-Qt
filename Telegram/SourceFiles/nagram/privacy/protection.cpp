#include "nagram/privacy/protection.h"

#include "nagram/privacy/options.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "history/history_item.h"

namespace Nagram::Privacy {

bool DoNotSharePhoneByDefault() {
	return ForDevice().Get(kDoNotSharePhone);
}

bool ForceCopy() {
	return ForDevice().Get(kForceCopy);
}

rpl::producer<bool> ForceCopyChanges() {
	return ForDevice().Value(kForceCopy) | rpl::skip(1);
}

bool AllowsCopy(not_null<const PeerData*> peer) {
	return CopyAllowed(ForceCopy(), peer->allowsForwarding());
}

bool ForbidsCopy(not_null<const HistoryItem*> item) {
	return CopyForbidden(ForceCopy(), item->forbidsForward());
}

rpl::producer<bool> AllowsCopyValue(not_null<PeerData*> peer) {
	return rpl::combine(
		ForDevice().Value(kForceCopy),
		Data::AllowsForwardingValue(peer)
	) | rpl::map(CopyAllowed) | rpl::distinct_until_changed();
}

} // namespace Nagram::Privacy
