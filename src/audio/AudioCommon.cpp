#include "AudioCommon.h"

#include <openal/al.h>
#include <openal/alc.h>

namespace lynx
{
	namespace
	{
		ALCdevice* g_device = nullptr;
		ALCcontext* g_context = nullptr;
		float g_master_volume = 1.0f;
	}

	bool InitializeAudio()
	{
		if (g_context)
			return true;

		g_device = alcOpenDevice(nullptr);
		if (!g_device)
			return false;

		g_context = alcCreateContext(g_device, nullptr);
		if (!g_context)
		{
			alcCloseDevice(g_device);
			g_device = nullptr;
			return false;
		}

		if (!alcMakeContextCurrent(g_context))
		{
			alcDestroyContext(g_context);
			alcCloseDevice(g_device);
			g_context = nullptr;
			g_device = nullptr;
			return false;
		}

		alListenerf(AL_GAIN, g_master_volume);
		return true;
	}

	void ShutdownAudio()
	{
		if (!g_context)
			return;

		alcMakeContextCurrent(nullptr);
		alcDestroyContext(g_context);
		alcCloseDevice(g_device);

		g_context = nullptr;
		g_device = nullptr;
	}

	bool IsAudioInitialized()
	{
		return g_context != nullptr;
	}

	void SetMasterVolume(float volume)
	{
		g_master_volume = volume < 0.0f ? 0.0f : volume;
		if (g_context)
			alListenerf(AL_GAIN, g_master_volume);
	}

	float GetMasterVolume()
	{
		return g_master_volume;
	}
}
