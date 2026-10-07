#include "Audio2D.h"
#include "AudioCommon.h"

#include <openal/al.h>

#include <algorithm>
#include <iostream>
#include <cstdint>
#include <random>
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
	}

	Audio2D::Audio2D(const char* sound_asset)
	{
		if (!InitializeAudio())
			return;

		const std::vector<uint8_t> sound_data = fs::ReadBinary(sound_asset);
		if (sound_data.empty())
		{
			std::cout << "[Audio] Son introuvable : assets/" << sound_asset << std::endl;
			return;
		}

		std::vector<int16_t> pcm;
		int channels = 0;
		int sample_rate = 0;

		// 2D : on garde le stereo tel quel (pas de downmix).
		if (!audio_detail::LoadSoundFile(sound_asset, sound_data, false, pcm, channels, sample_rate))
		{
			std::cout << "[Audio] Son illisible (wav / mp3 attendu) : " << sound_asset << std::endl;
			return;
		}

		const ALenum format = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

		alGenBuffers(1, &buffer_);
		alBufferData(
			buffer_,
			format,
			pcm.data(),
			static_cast<ALsizei>(pcm.size() * sizeof(int16_t)),
			static_cast<ALsizei>(sample_rate)
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
