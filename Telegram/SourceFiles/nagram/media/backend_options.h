#pragma once

#include "nagram/core/options.h"

namespace Nagram::Media {

[[nodiscard]] constexpr bool ValidVoiceBitrate(const int &value) {
	return value == 0 || value == 16 || value == 24 || value == 48
		|| value == 64 || value == 96 || value == 128;
}

inline constexpr auto kVoiceRecordBitrate = Option<int>{
	"nagram.voiceRecordBitrate", Scope::Device, 0,
	Category::Media, "lng_nagram_voice_record_bitrate", 0,
	ValidVoiceBitrate };

[[nodiscard]] constexpr int ResolveVoiceBitrate(int kbps, int fallback) {
	return (kbps > 0 && ValidVoiceBitrate(kbps)) ? (kbps * 1000) : fallback;
}

[[nodiscard]] inline int VoiceRecordBitrate(Options &options, int fallback) {
	return ResolveVoiceBitrate(options.Get(kVoiceRecordBitrate), fallback);
}

inline void RegisterBackendOptions(Registry &registry) {
	Expects(registry.Add(kVoiceRecordBitrate));
}

} // namespace Nagram::Media
