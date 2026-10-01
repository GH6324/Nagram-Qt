#pragma once

#include "nagram/core/options.h"
#include "nagram/network/model.h"

namespace Nagram::Network {

inline constexpr auto kIpStrategy = Option<int>{
	"nagram.ipStrategy", Scope::Device, 0,
	Category::Network, "lng_nagram_ip_strategy",
	static_cast<unsigned>(Flag::LocalOnly), ValidIpStrategy };
inline constexpr auto kDisableBackupAddresses = Option<bool>{
	"nagram.disableBackupAddresses", Scope::Device, false,
	Category::Network, "lng_nagram_disable_backup_addresses",
	static_cast<unsigned>(Flag::LocalOnly) };
inline constexpr auto kUseSystemDns = Option<bool>{
	"nagram.useSystemDns", Scope::Device, false,
	Category::Network, "lng_nagram_use_system_dns",
	static_cast<unsigned>(Flag::LocalOnly) };
inline const auto kCustomDoh = Option<QString>{
	"nagram.customDoh", Scope::Device, QString(),
	Category::Network, "lng_nagram_custom_doh",
	static_cast<unsigned>(Flag::LocalOnly), ValidCustomDoh };

inline void RegisterOptions(Registry &registry) {
	Expects(registry.Add(kIpStrategy));
	Expects(registry.Add(kDisableBackupAddresses));
	Expects(registry.Add(kUseSystemDns));
	Expects(registry.Add(kCustomDoh));
}

} // namespace Nagram::Network
