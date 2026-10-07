#include "AudioSource.h"
#include "AudioCommon.h"

#include <openal/al.h>

#include <algorithm>
#include <iostream>
#include <cstdint>
#include <random>
#include <vector>

#include "../core/Filesystem.h"
#include "../gameplay/Actor.h"

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

	AudioSource::AudioSource(const char* sound_asset)
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

		// force_mono = true : OpenAL ne spatialise pas (ni attenuation, ni panning)
		// les buffers stereo. Un son stereo est donc mixe en mono ici.
		if (!audio_detail::LoadSoundFile(sound_asset, sound_data, true, pcm, channels, sample_rate))
		{
			std::cout << "[Audio] Son illisible (wav / mp3 attendu) : " << sound_asset << std::endl;
			return;
		}

		alGenBuffers(1, &buffer_);
		alBufferData(
			buffer_,
			AL_FORMAT_MONO16,
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

		alSourcei(source_, AL_BUFFER, static_cast<ALint>(buffer_));
		alSourcei(source_, AL_SOURCE_RELATIVE, AL_FALSE);
		alSource3f(source_, AL_POSITION, 0.0f, 0.0f, 0.0f);
	}

	AudioSource::~AudioSource()
	{
		if (source_)
			alDeleteSources(1, &source_);

		if (buffer_)
			alDeleteBuffers(1, &buffer_);
	}

	void AudioSource::AttachToActor(Actor* actor)
	{
		attached_actor_ = actor;
	}

	void AudioSource::DetachFromActor()
	{
		attached_actor_ = nullptr;
	}

	void AudioSource::SetLocation(vec3 loc)
	{
		location_ = loc;

		if (source_)
			alSource3f(
				source_,
				AL_POSITION,
				loc.x,
				loc.y,
				audio_detail::Depth(loc.z)
			);
	}

	vec3 AudioSource::GetLocation() const
	{
		return location_;
	}

	void AudioSource::SyncWithActor()
	{
		if (attached_actor_)
			SetLocation(attached_actor_->transform.location);
	}

	void AudioSource::Play()
	{
		if (!source_)
			return;

		SyncWithActor();

		alSourcef(source_, AL_GAIN, RandomRange(volume_min, volume_max));
		alSourcef(source_, AL_PITCH, RandomRange(pitch_min, pitch_max));
		alSourcei(source_, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);

		alSourcef(source_, AL_ROLLOFF_FACTOR, std::max(0.0f, rolloff));
		alSourcef(
			source_,
			AL_REFERENCE_DISTANCE,
			std::max(0.001f, reference_distance)
		);
		alSourcef(
			source_,
			AL_MAX_DISTANCE,
			std::max(0.001f, max_distance)
		);

		alSourcePlay(source_);
	}

	void AudioSource::Stop()
	{
		if (source_)
			alSourceStop(source_);
	}

	bool AudioSource::IsPlaying() const
	{
		if (!source_)
			return false;

		ALint state = AL_STOPPED;
		alGetSourcei(source_, AL_SOURCE_STATE, &state);
		return state == AL_PLAYING;
	}

	// Position explicite (impacts, etc.) : prioritaire sur l'acteur attache.
	void AudioSource::PlayAtLocation(vec3 loc)
	{
		if (!source_)
			return;

		// On joue sans resynchroniser sur l'acteur, sinon Play() ecraserait loc.
		Actor* attached = attached_actor_;
		attached_actor_ = nullptr;

		SetLocation(loc);
		Play();

		attached_actor_ = attached;
	}
}
