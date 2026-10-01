#include "nagram/core/exchange.h"
#include "nagram/network/options.h"
#include "base/basic_types.h"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

class MemoryPrefs final : public Nagram::RawPrefs {
public:
	[[nodiscard]] QByteArray read(std::string_view key) override {
		const auto found = values.find(std::string(key));
		return (found == values.end()) ? QByteArray() : found->second;
	}
	void write(std::string_view key, const QByteArray &value) override {
		values[std::string(key)] = value;
	}
	void clear(std::string_view key) override {
		values.erase(std::string(key));
	}

	std::map<std::string, QByteArray> values;
};

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Type>
void CheckDeviceOption(
		const Nagram::Registry &registry,
		const Nagram::Option<Type> &option,
		const Type &changed) {
	using namespace Nagram;
	const auto info = registry.Find(option.key);
	Require(info != nullptr, "network option is not registered");
	Require(info->scope == Scope::Device
		&& info->category == Category::Network
		&& registry.HasFlag(option.key, Flag::Exportable)
		&& !registry.HasFlag(option.key, Flag::RequiresRestart),
		"network option must be an exportable device option");

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(options.Get(option) == option.fallback
		&& prefs.values.empty(), "network option default value");
	Require(options.Set(option, changed)
		&& options.Get(option) == changed, "network option write");
	const auto exported = Exchange::Export(options, registry);
	Require(exported.invalidKeys.empty(), "network option export");

	auto importedPrefs = MemoryPrefs();
	auto imported = Options(importedPrefs);
	const auto plan = Exchange::PlanImport(imported, registry, exported.data);
	Require(plan.error.isEmpty() && plan.changes.size() == 1,
		"network option import preview");
	Require(Exchange::Apply(imported, registry, plan).applied
		&& imported.Get(option) == changed, "network option import");

	importedPrefs.values[std::string(option.key)] = "broken";
	Require(imported.Get(option) == option.fallback
		&& imported.invalidKeys().contains(option.key)
		&& importedPrefs.values[std::string(option.key)] == "broken",
		"invalid network value must fall back and stay stored");
}

void TestConnectionOptions() {
	using namespace Nagram;
	using namespace Nagram::Network;
	auto registry = Registry();
	RegisterOptions(registry);
	CheckDeviceOption(registry, kIpStrategy, 4);
	CheckDeviceOption(registry, kDisableBackupAddresses, true);
	Require(kIpStrategy.fallback == 0 && !kDisableBackupAddresses.fallback,
		"connection options must follow Telegram by default");
	for (auto value = 0; value != kIpStrategyCount; ++value) {
		Require(kIpStrategy.validate(value), "IP strategy rejected");
	}
	Require(!kIpStrategy.validate(-1) && !kIpStrategy.validate(5),
		"IP strategy out of range accepted");

	auto prefs = MemoryPrefs();
	auto options = Options(prefs);
	Require(!options.Set(kIpStrategy, 5) && prefs.values.empty(),
		"IP strategy out of range stored");
}

void TestIpChoice() {
	using namespace Nagram::Network;
	for (const auto tryIPv6 : { false, true }) {
		for (const auto prefer : { false, true }) {
			const auto upstream = IpChoice{ true, tryIPv6, prefer };
			Require(ResolveIpChoice(0, upstream) == upstream,
				"default IP strategy must return the upstream values");
			Require(ResolveIpChoice(-1, upstream) == upstream
				&& ResolveIpChoice(5, upstream) == upstream,
				"unknown IP strategy must return the upstream values");
			Require(ResolveIpChoice(1, upstream)
				== IpChoice{ true, false, false }, "IPv4 only");
			Require(ResolveIpChoice(2, upstream)
				== IpChoice{ true, true, false }, "prefer IPv4");
			Require(ResolveIpChoice(3, upstream)
				== IpChoice{ true, true, true }, "prefer IPv6");
			const auto only6 = ResolveIpChoice(4, upstream);
			Require(!only6.useIPv4 && only6.useIPv6, "IPv6 only");
		}
	}
}

void TestOrderIps() {
	using namespace Nagram::Network;
	const auto mixed = QStringList{
		u"2001:db8::1"_q,
		u"192.0.2.1"_q,
		u"2001:db8::2"_q,
		u"192.0.2.2"_q,
	};
	const auto v4 = QStringList{ u"192.0.2.1"_q, u"192.0.2.2"_q };
	const auto v6 = QStringList{ u"2001:db8::1"_q, u"2001:db8::2"_q };
	Require(OrderIps(0, mixed) == mixed && OrderIps(9, mixed) == mixed,
		"default IP strategy must keep the resolved list");
	Require(OrderIps(1, mixed) == v4, "IPv4 only list");
	Require(OrderIps(2, mixed) == v4 + v6, "prefer IPv4 list");
	Require(OrderIps(3, mixed) == v6 + v4, "prefer IPv6 list");
	Require(OrderIps(4, mixed) == v6, "IPv6 only list");
	Require(OrderIps(1, v6).isEmpty() && OrderIps(4, v4).isEmpty(),
		"a list without the allowed family must become empty");
	for (auto strategy = 0; strategy != kIpStrategyCount; ++strategy) {
		Require(OrderIps(strategy, {}).isEmpty(), "empty list");
	}
}

} // namespace

void TestNetwork() {
	TestConnectionOptions();
	TestIpChoice();
	TestOrderIps();
	std::cout << "PASS: Nagram network options" << std::endl;
}
