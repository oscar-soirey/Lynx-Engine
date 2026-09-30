#pragma once

#include "../core/Common.h"

namespace lynx
{
	class Actor;

	class LYNX_API AudioSource
	{
	public:
		AudioSource(const char* sound_asset);
		~AudioSource();

		AudioSource(const AudioSource&) = delete;
		AudioSource& operator=(const AudioSource&) = delete;

		void AttachToActor(Actor* actor);
		void DetachFromActor();

		void SetLocation(vec3 loc);
		vec3 GetLocation() const;

		void Play();
		void Stop();
		bool IsPlaying() const;

		void PlayAtLocation(vec3 loc);

		// Play() randomly chooses volume/pitch inside these ranges.
		float volume_min = 0.9f;
		float volume_max = 1.1f;

		float pitch_min = 0.9f;
		float pitch_max = 1.1f;

		bool looping = false;

		// Spatial audio parameters.
		float rolloff = 1.0f;
		float reference_distance = 1.0f;
		float max_distance = 100.0f;

	private:
		unsigned int source_ = 0;
		unsigned int buffer_ = 0;

		Actor* attached_actor_ = nullptr;
		vec3 location_{};
	};
}
