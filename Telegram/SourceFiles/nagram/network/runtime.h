#pragma once

#include <QtCore/QStringList>

namespace Nagram::Network {

// Main thread.
[[nodiscard]] bool UseIPv4(bool upstream);
[[nodiscard]] bool UseIPv6(bool upstream);
[[nodiscard]] QStringList OrderIps(const QStringList &ips);
[[nodiscard]] bool BackupAddressesDisabled();

// Thread safe.
[[nodiscard]] bool PreferIPv6(bool upstream);

} // namespace Nagram::Network
