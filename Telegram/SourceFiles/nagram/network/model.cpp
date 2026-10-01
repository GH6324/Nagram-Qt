#include "nagram/network/model.h"

namespace Nagram::Network {
namespace {

[[nodiscard]] bool IsIPv6(const QString &ip) {
	return !ip.contains(u'.') && ip.contains(u':');
}

} // namespace

IpChoice ResolveIpChoice(int strategy, IpChoice upstream) {
	if (!ValidIpStrategy(strategy)) {
		return upstream;
	}
	switch (static_cast<IpStrategy>(strategy)) {
	case IpStrategy::Follow: return upstream;
	case IpStrategy::Ipv4Only: return { true, false, false };
	case IpStrategy::PreferIpv4: return { true, true, false };
	case IpStrategy::PreferIpv6: return { true, true, true };
	case IpStrategy::Ipv6Only: return { false, true, true };
	}
	return upstream;
}

QStringList OrderIps(int strategy, const QStringList &ips) {
	if (!ValidIpStrategy(strategy)
		|| static_cast<IpStrategy>(strategy) == IpStrategy::Follow) {
		return ips;
	}
	const auto choice = ResolveIpChoice(strategy, {});
	auto v4 = QStringList();
	auto v6 = QStringList();
	for (const auto &ip : ips) {
		if (IsIPv6(ip)) {
			if (choice.useIPv6) {
				v6.push_back(ip);
			}
		} else if (choice.useIPv4) {
			v4.push_back(ip);
		}
	}
	return choice.preferIPv6 ? (v6 + v4) : (v4 + v6);
}

} // namespace Nagram::Network
