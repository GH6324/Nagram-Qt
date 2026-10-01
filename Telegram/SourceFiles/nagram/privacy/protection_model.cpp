#include "nagram/privacy/protection.h"

namespace Nagram::Privacy {

bool CopyAllowed(bool force, bool allowsForwarding) {
	return force || allowsForwarding;
}

bool CopyForbidden(bool force, bool forbidsForward) {
	return !force && forbidsForward;
}

} // namespace Nagram::Privacy
