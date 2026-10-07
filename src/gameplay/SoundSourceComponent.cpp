#include "SoundSourceComponent.h"

#include "Actor.h"
#include "../audio/Audio2D.h"
#include "../audio/AudioSource.h"

#include <algorithm>

namespace lynx
{
	SoundSourceComponent::SoundSourceComponent() = default;
	SoundSourceComponent::~SoundSourceComponent() = default;

	bool SoundSourceComponent::EnsureSource()
	{
		if (sound.empty())
		{
			source_3d_.reset();
			source_2d_.reset();
			loaded_sound_.clear();
			return false;
		}

		// Nouveau son ou nouveau mode : on recree la source.
		if (sound != loaded_sound_ || spatial != loaded_spatial_ || (!source_3d_ && !source_2d_))
		{
			source_3d_.reset();
			source_2d_.reset();

			if (spatial)
			{
				source_3d_ = std::make_unique<AudioSource>(sound.c_str());
				source_3d_->AttachToActor(GetOwner());
			}
			else
			{
				source_2d_ = std::make_unique<Audio2D>(sound.c_str());
			}

			loaded_sound_ = sound;
			loaded_spatial_ = spatial;
		}

		return true;
	}

	void SoundSourceComponent::ApplySettings()
	{
		const float vmin = std::max(0.f, volume - volume_variation);
		const float vmax = std::max(vmin, volume + volume_variation);
		const float pmin = std::max(0.01f, pitch - pitch_variation);
		const float pmax = std::max(pmin, pitch + pitch_variation);

		if (source_3d_)
		{
			source_3d_->volume_min = vmin;
			source_3d_->volume_max = vmax;
			source_3d_->pitch_min = pmin;
			source_3d_->pitch_max = pmax;
			source_3d_->looping = loop;
			source_3d_->reference_distance = reference_distance;
			source_3d_->max_distance = max_distance;
			source_3d_->rolloff = rolloff;
		}

		if (source_2d_)
		{
			source_2d_->volume_min = vmin;
			source_2d_->volume_max = vmax;
			source_2d_->pitch_min = pmin;
			source_2d_->pitch_max = pmax;
			source_2d_->looping = loop;
		}
	}

	void SoundSourceComponent::Play()
	{
		if (!EnsureSource())
			return;

		ApplySettings();

		if (source_3d_)
			source_3d_->Play();
		else if (source_2d_)
			source_2d_->Play();
	}

	void SoundSourceComponent::Stop()
	{
		if (source_3d_)
			source_3d_->Stop();

		if (source_2d_)
			source_2d_->Stop();
	}

	bool SoundSourceComponent::IsPlaying() const
	{
		if (source_3d_)
			return source_3d_->IsPlaying();

		return source_2d_ && source_2d_->IsPlaying();
	}

	void SoundSourceComponent::PlayAtLocation(const vec3& location)
	{
		if (!spatial || !EnsureSource() || !source_3d_)
			return;

		ApplySettings();
		source_3d_->PlayAtLocation(location);
	}

	void SoundSourceComponent::BeginPlay()
	{
		if (play_on_begin)
			Play();
	}

	void SoundSourceComponent::Update(float)
	{
		// Un son en boucle suit l'acteur qui bouge.
		if (source_3d_ && source_3d_->IsPlaying())
			source_3d_->SyncWithActor();
	}

	void SoundSourceComponent::LateUpdate(float dt)
	{
		Update(dt);
	}

	void SoundSourceComponent::EndPlay()
	{
		Stop();
	}
}
