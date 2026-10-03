#include "AudioListener.h"
#include "AudioCommon.h"

#include <openal/al.h>

#include "../gameplay/Actor.h"

namespace lynx
{
	namespace
	{
		Actor* g_listener_actor = nullptr;
		vec3 g_listener_location{};
		vec3 g_listener_velocity{};
		vec3 g_listener_forward{0.0f, 0.0f, -1.0f};
		vec3 g_listener_up{0.0f, 1.0f, 0.0f};

		bool g_use_doppler = true;

		void ApplyDoppler()
		{
			alDopplerFactor(g_use_doppler ? 1.0f : 0.0f);
		}

		void ApplyLocation()
		{
			alListener3f(
				AL_POSITION,
				g_listener_location.x,
				g_listener_location.y,
				audio_detail::Depth(g_listener_location.z)
			);
		}
	}

	void AttachAudioListener(Actor* target)
	{
		g_listener_actor = target;
		alDopplerFactor(0);
	}

	void UnattachAudioListener()
	{
		g_listener_actor = nullptr;
	}

	void SetUseDopplerEffect(bool use)
	{
		g_use_doppler = use;

		if (IsAudioInitialized())
			ApplyDoppler();
	}

	void SetListenerVelocity(vec3 velocity)
	{
		g_listener_velocity = velocity;   // plus de early return

		if (!IsAudioInitialized())
			return;

		alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
	}

	void SetListenerLocation(vec3 loc)
	{
		g_listener_location = loc;

		if (!IsAudioInitialized())
			return;

		ApplyLocation();
	}

	void SetListenerOrientation(vec3 forward, vec3 up)
	{
		g_listener_forward = forward;
		g_listener_up = up;

		if (!IsAudioInitialized())
			return;

		const ALfloat orientation[6] = {
			forward.x, forward.y, forward.z,
			up.x, up.y, up.z
		};

		alListenerfv(AL_ORIENTATION, orientation);
	}

	void UpdateAudioListener()
	{
		if (!IsAudioInitialized())
			return;

		// Suit l'acteur attache (ex : le joueur).
		if (g_listener_actor)
			g_listener_location = g_listener_actor->transform.location;

		ApplyLocation();

		if (g_use_doppler)
		{
			alListener3f(
				AL_VELOCITY,
				g_listener_velocity.x,
				g_listener_velocity.y,
				g_listener_velocity.z
			);
		}

		const ALfloat orientation[6] = {
			g_listener_forward.x, g_listener_forward.y, g_listener_forward.z,
			g_listener_up.x, g_listener_up.y, g_listener_up.z
		};

		alListenerfv(AL_ORIENTATION, orientation);
	}
}
