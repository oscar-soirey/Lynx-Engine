#pragma once

#include "../core/Common.h"

namespace lynx
{
	// OpenAL global audio device/context.
	// Call this once during engine startup before creating Audio2D/AudioSource.
	LYNX_API bool InitializeAudio();
	LYNX_API void ShutdownAudio();
	LYNX_API bool IsAudioInitialized();

	// Master volume applied to all OpenAL sources.
	LYNX_API void SetMasterVolume(float volume);
	LYNX_API float GetMasterVolume();
}
