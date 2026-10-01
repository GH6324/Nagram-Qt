#include "nagram/media/audio.h"

#include "nagram/media/backend_options.h"

namespace Nagram::Media {

int VoiceRecordBitrate(int fallback) {
	return VoiceRecordBitrate(ForDevice(), fallback);
}

bool GroupCallRawAudio() {
	return ForDevice().Get(kGroupCallRawAudio);
}

} // namespace Nagram::Media
