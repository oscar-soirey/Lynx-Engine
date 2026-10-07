#pragma once

/**
 * Son attache a l'acteur.
 *
 *     auto& s = actor->AddComponent<lynx::SoundSourceComponent>();
 *     s.sound = "footstep1.wav";
 *     s.Play();
 *
 * spatial = true  : son 3D qui suit l'acteur (attenuation avec la distance).
 * spatial = false : son 2D (musique, interface).
 * Les reglages sont appliques a chaque Play().
 */

#include <memory>
#include <string>

#include "Component.h"

namespace lynx
{
	class AudioSource;
	class Audio2D;

	class LYNX_API SoundSourceComponent : public Component
	{
	public:
		SoundSourceComponent();
		~SoundSourceComponent() override;

		/** Chemin dans les assets (wav / mp3). */
		std::string sound;

		bool spatial = true;
		bool loop = false;

		/** Joue au lancement du jeu. */
		bool play_on_begin = false;

		float volume = 1.f;
		float pitch = 1.f;

		/** Variation aleatoire a chaque Play() (+/-). */
		float volume_variation = 0.f;
		float pitch_variation = 0.f;

		// Son spatial : gain 1 a reference_distance, 0 a max_distance (rolloff 1).
		float reference_distance = 1.f;
		float max_distance = 100.f;
		float rolloff = 1.f;

		void Play();
		void Stop();
		bool IsPlaying() const;

		/** Joue le son a une position (son spatial seulement). */
		void PlayAtLocation(const vec3& location);

	protected:
		void BeginPlay() override;
		void Update(float dt) override;
		void LateUpdate(float dt) override;
		void EndPlay() override;

	private:
		bool EnsureSource();
		void ApplySettings();

		std::unique_ptr<AudioSource> source_3d_;
		std::unique_ptr<Audio2D> source_2d_;
		std::string loaded_sound_;
		bool loaded_spatial_ = true;
	};
}
