#include "nagram/privacy/protection.h"

#include "nagram/privacy/options.h"

namespace Nagram::Privacy {

bool DoNotSharePhoneByDefault() {
	return ForDevice().Get(kDoNotSharePhone);
}

} // namespace Nagram::Privacy
