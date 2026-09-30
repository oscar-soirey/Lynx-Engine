#pragma once

#include "../core/Common.h"

namespace lynx
{
	class LYNX_API Audio2D
	{
	public:
		Audio2D(const char* sound_asset);
		~Audio2D();

		Audio2D(const Audio2D&) = delete;
		Audio2D& operator=(const Audio2D&) = delete;

		void Play();
		void Stop();
		bool IsPlaying() const;

		// Play() randomly chooses values in these ranges.
		float volume_min = 0.9f;
		float volume_max = 1.1f;

		float pitch_min = 0.9f;
		float pitch_max = 1.1f;

		bool looping = false;

	private:
		unsigned int source_ = 0;
		unsigned int buffer_ = 0;
	};
}
