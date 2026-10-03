#pragma once

#include <cstdint>
#include <vector>

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

	// Jeu 2D : si true (defaut), l'axe Z est ignore pour le calcul de la distance
	// (listener et sources sont places a Z = 0). Evite qu'un sprite a Z=5 et une
	// camera a Z=80 faussent l'attenuation.
	LYNX_API void SetAudioPlanar(bool planar);
	LYNX_API bool IsAudioPlanar();

	// ------------------------------------------------------------------
	// Usage interne (non exporte) : partage entre Audio2D et AudioSource.
	// ------------------------------------------------------------------
	namespace audio_detail
	{
		// Decode un .wav / .mp3 en PCM 16 bits.
		// force_mono : si le fichier est stereo, les 2 canaux sont mixes en mono
		// ((L + R) / 2). OpenAL ne spatialise QUE les buffers mono.
		bool LoadSoundFile(
			const char* path,
			const std::vector<uint8_t>& data,
			bool force_mono,
			std::vector<int16_t>& pcm,
			int& channels,
			int& sample_rate);

		// Retourne 0 si le mode planaire est actif, sinon z.
		float Depth(float z);
	}
}
