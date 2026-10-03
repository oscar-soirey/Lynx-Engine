#include "AudioCommon.h"

#include <openal/al.h>
#include <openal/alc.h>
#include <dr/dr_mp3.h>
#include <dr/dr_wav.h>

#include <string.h>

namespace lynx
{
	namespace
	{
		ALCdevice* g_device = nullptr;
		ALCcontext* g_context = nullptr;
		float g_master_volume = 1.0f;
		bool g_planar = true;
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

		// Attenuation lineaire : le gain vaut 1 a reference_distance et 0 a
		// max_distance (le modele par defaut, inverse-distance, ne atteint jamais 0).
		alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
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

	void SetAudioPlanar(bool planar)
	{
		g_planar = planar;
	}

	bool IsAudioPlanar()
	{
		return g_planar;
	}

	namespace audio_detail
	{
		float Depth(float z)
		{
			return g_planar ? 0.0f : z;
		}

		namespace
		{
			// Mixe un buffer stereo entrelace (L R L R ...) en mono, sur place.
			void DownmixToMono(std::vector<int16_t>& pcm)
			{
				const size_t frames = pcm.size() / 2;

				for (size_t i = 0; i < frames; ++i)
				{
					const int32_t l = pcm[i * 2];
					const int32_t r = pcm[i * 2 + 1];
					pcm[i] = static_cast<int16_t>((l + r) / 2);
				}

				pcm.resize(frames);
			}
		}

		bool LoadSoundFile(
			const char* path,
			const std::vector<uint8_t>& data,
			bool force_mono,
			std::vector<int16_t>& pcm,
			int& channels,
			int& sample_rate)
		{
			const char* extension = path;
			for (const char* p = path; *p; ++p)
				if (*p == '.')
					extension = p + 1;

			if (!_stricmp(extension, "wav"))
			{
				drwav wav;
				if (!drwav_init_memory(&wav, data.data(), data.size(), nullptr))
					return false;

				channels = static_cast<int>(wav.channels);
				sample_rate = static_cast<int>(wav.sampleRate);

				if (channels != 1 && channels != 2)
				{
					drwav_uninit(&wav);
					return false;
				}

				const drwav_uint64 frame_count = wav.totalPCMFrameCount;
				pcm.resize(static_cast<size_t>(frame_count * channels));

				const drwav_uint64 frames_read =
					drwav_read_pcm_frames_s16(&wav, frame_count, pcm.data());

				drwav_uninit(&wav);

				if (frames_read != frame_count)
					return false;
			}
			else if (!_stricmp(extension, "mp3"))
			{
				drmp3 mp3;
				if (!drmp3_init_memory(&mp3, data.data(), data.size(), nullptr))
					return false;

				channels = static_cast<int>(mp3.channels);
				sample_rate = static_cast<int>(mp3.sampleRate);

				if (channels != 1 && channels != 2)
				{
					drmp3_uninit(&mp3);
					return false;
				}

				const drmp3_uint64 frame_count = drmp3_get_pcm_frame_count(&mp3);
				pcm.resize(static_cast<size_t>(frame_count * channels));

				const drmp3_uint64 frames_read =
					drmp3_read_pcm_frames_s16(&mp3, frame_count, pcm.data());

				drmp3_uninit(&mp3);

				if (frames_read != frame_count)
					return false;
			}
			else
			{
				return false;
			}

			if (force_mono && channels == 2)
			{
				DownmixToMono(pcm);
				channels = 1;
			}

			return true;
		}
	}
}
