#pragma once

#include "nagram/core/options.h"

namespace Nagram {

inline constexpr auto kShowRpcErrors = Option<bool>{
	"nagram.showRpcErrors", Scope::Device, false,
	Category::Services, "lng_nagram_show_rpc_errors" };

inline void RegisterDiagnosticsOptions(Registry &registry) {
	Expects(registry.Add(kShowRpcErrors));
}

void InstallRpcErrorObserver();

} // namespace Nagram
