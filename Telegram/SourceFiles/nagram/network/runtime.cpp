#include "nagram/network/runtime.h"

#include "nagram/network/options.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"

#include <atomic>

namespace Nagram::Network {
namespace {

std::atomic<int> Strategy = 0;

void Reconnect() {
	if (!Core::App().domain().started()) {
		return;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		account->mtp().restart();
	}
}

void EnsureStarted() {
	static auto started = false;
	if (started) {
		return;
	}
	started = true;
	// WHY: session threads can't read Core::Settings, so the main thread
	// mirrors the value here before the first session is configured.
	auto &options = ForDevice();
	Strategy = options.Get(kIpStrategy);
	static auto lifetime = rpl::lifetime();
	options.Value(
		kIpStrategy
	) | rpl::skip(1) | rpl::on_next([](int value) {
		Strategy = value;
		Reconnect();
	}, lifetime);
}

} // namespace

bool UseIPv4(bool upstream) {
	EnsureStarted();
	return ResolveIpChoice(Strategy, { .useIPv4 = upstream }).useIPv4;
}

bool UseIPv6(bool upstream) {
	EnsureStarted();
	return ResolveIpChoice(Strategy, { .useIPv6 = upstream }).useIPv6;
}

QStringList OrderIps(const QStringList &ips) {
	EnsureStarted();
	return OrderIps(Strategy, ips);
}

bool BackupAddressesDisabled() {
	return ForDevice().Get(kDisableBackupAddresses);
}

bool PreferIPv6(bool upstream) {
	return ResolveIpChoice(Strategy, { .preferIPv6 = upstream }).preferIPv6;
}

} // namespace Nagram::Network
