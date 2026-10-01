#pragma once

#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QStringList>
#include <QtCore/QUrl>

namespace Nagram::Network {

enum class IpStrategy {
	Follow,
	Ipv4Only,
	PreferIpv4,
	PreferIpv6,
	Ipv6Only,
};

inline constexpr auto kIpStrategyCount = 5;

struct IpChoice {
	bool useIPv4 = true;
	bool useIPv6 = false;
	bool preferIPv6 = false;

	friend bool operator==(const IpChoice &, const IpChoice &) = default;
};

[[nodiscard]] constexpr bool ValidIpStrategy(const int &value) {
	return value >= 0 && value < kIpStrategyCount;
}

[[nodiscard]] IpChoice ResolveIpChoice(int strategy, IpChoice upstream);
[[nodiscard]] QStringList OrderIps(int strategy, const QStringList &ips);

inline constexpr auto kMaxDohAddressLength = 256;

[[nodiscard]] bool ValidDohAddress(const QString &address);
[[nodiscard]] bool ValidCustomDoh(const QString &value);
void SetDohEndpoint(QUrl &url, const QString &data);
[[nodiscard]] bool SameDohEndpoint(const QUrl &request, const QString &custom);
[[nodiscard]] bool IsDnsJson(const QByteArray &bytes);

} // namespace Nagram::Network
