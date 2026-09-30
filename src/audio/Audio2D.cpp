#include "Audio2D.h"
#include "AudioCommon.h"

#include <openal/al.h>
#include <dr/dr_mp3.h>
#include <dr/dr_wav.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string.h>
#include <vector>

#include "../core/Filesystem.h"

namespace lynx
{
	namespace
	{
		std::mt19937& AudioRandom()
		{
			static std::mt19937 rng(std::random_device{}());
			return rng;
		}

		float RandomRange(float min_value, float max_value)
		{
			if (min_value > max_value)
				std::swap(min_value, max_value);

			std::uniform_real_distribution<float> dist(min_value, max_value);
			return dist(AudioRandom());
		}

		bool LoadAudioFile(
			const char* path,
			const std::vector<uint8_t>& data,
			std::vector<int16_t>& pcm,
			ALenum& format,
			ALsizei& sample_rate)
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

				const unsigned int channels = wav.channels;
				sample_rate = static_cast<ALsizei>(wav.sampleRate);

				if (channels == 1)
					format = AL_FORMAT_MONO16;
				else if (channels == 2)
					format = AL_FORMAT_STEREO16;
				else
				{
					drwav_uninit(&wav);
					return false;
				}

				const drwav_uint64 frame_count = wav.totalPCMFrameCount;
				pcm.resize(static_cast<size_t>(frame_count * channels));

				const drwav_uint64 frames_read =
					drwav_read_pcm_frames_s16(&wav, frame_count, pcm.data());

				drwav_uninit(&wav);
				return frames_read == frame_count;
			}

			if (!_stricmp(extension, "mp3"))
			{
				drmp3 mp3;
				if (!drmp3_init_memory(&mp3, data.data(), data.size(), nullptr))
					return false;

				const unsigned int channels = mp3.channels;
				sample_rate = static_cast<ALsizei>(mp3.sampleRate);

				if (channels == 1)
					format = AL_FORMAT_MONO16;
				else if (channels == 2)
					format = AL_FORMAT_STEREO16;
				else
				{
					drmp3_uninit(&mp3);
					return false;
				}

				const drmp3_uint64 frame_count =
					drmp3_get_pcm_frame_count(&mp3);

				pcm.resize(static_cast<size_t>(frame_count * channels));

				const drmp3_uint64 frames_read =
					drmp3_read_pcm_frames_s16(&mp3, frame_count, pcm.data());

				drmp3_uninit(&mp3);
				return frames_read == frame_count;
			}

			return false;
		}
	}

	Audio2D::Audio2D(const char* sound_asset)
	{
		if (!InitializeAudio())
			return;

		const std::vector<uint8_t> sound_data = fs::ReadBinary(sound_asset);
		if (sound_data.empty())
			return;

		std::vector<int16_t> pcm;
		ALenum format = 0;
		ALsizei sample_rate = 0;

		if (!LoadAudioFile(sound_asset, sound_data, pcm, format, sample_rate))
			return;

		alGenBuffers(1, &buffer_);
		alBufferData(
			buffer_,
			format,
			pcm.data(),
			static_cast<ALsizei>(pcm.size() * sizeof(int16_t)),
			sample_rate
		);

		if (alGetError() != AL_NO_ERROR)
		{
			alDeleteBuffers(1, &buffer_);
			buffer_ = 0;
			return;
		}

		alGenSources(1, &source_);

		// 2D: source is relative to the listener and has no distance attenuation.
		alSourcei(source_, AL_BUFFER, static_cast<ALint>(buffer_));
		alSourcei(source_, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(source_, AL_POSITION, 0.0f, 0.0f, 0.0f);
		alSourcef(source_, AL_ROLLOFF_FACTOR, 0.0f);
	}

	Audio2D::~Audio2D()
	{
		if (source_)
			alDeleteSources(1, &source_);

		if (buffer_)
			alDeleteBuffers(1, &buffer_);
	}

	void Audio2D::Play()
	{
		if (!source_)
			return;

		alSourcef(source_, AL_GAIN, RandomRange(volume_min, volume_max));
		alSourcef(source_, AL_PITCH, RandomRange(pitch_min, pitch_max));
		alSourcei(source_, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);

		alSourcePlay(source_);
	}

	void Audio2D::Stop()
	{
		if (source_)
			alSourceStop(source_);
	}

	bool Audio2D::IsPlaying() const
	{
		if (!source_)
			return false;

		ALint state = AL_STOPPED;
		alGetSourcei(source_, AL_SOURCE_STATE, &state);
		return state == AL_PLAYING;
	}
}
