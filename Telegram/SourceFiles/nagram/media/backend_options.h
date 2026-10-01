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

inline constexpr auto kGroupCallRawAudio = Option<bool>{
	"nagram.groupCallRawAudio", Scope::Device, false,
	Category::Media, "lng_nagram_group_call_raw_audio" };

[[nodiscard]] constexpr int ResolveVoiceBitrate(int kbps, int fallback) {
	return (kbps > 0 && ValidVoiceBitrate(kbps)) ? (kbps * 1000) : fallback;
}

[[nodiscard]] inline int VoiceRecordBitrate(Options &options, int fallback) {
	return ResolveVoiceBitrate(options.Get(kVoiceRecordBitrate), fallback);
}

inline void RegisterBackendOptions(Registry &registry) {
	Expects(registry.Add(kVoiceRecordBitrate));
	Expects(registry.Add(kGroupCallRawAudio));
}

} // namespace Nagram::Media
