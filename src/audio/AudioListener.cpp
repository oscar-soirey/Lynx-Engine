#include "AudioListener.h"
#include "AudioCommon.h"

#include <openal/al.h>

namespace lynx
{
	namespace
	{
		Actor* g_listener_actor = nullptr;
		vec3 g_listener_location{};
		vec3 g_listener_velocity{};
		vec3 g_listener_forward{0.0f, 0.0f, -1.0f};
		vec3 g_listener_up{0.0f, 1.0f, 0.0f};
	}

	void AttachAudioListener(Actor* target)
	{
		g_listener_actor = target;
	}

	void UnattachAudioListener()
	{
		g_listener_actor = nullptr;
	}

	void SetListenerLocation(vec3 loc)
	{
		g_listener_location = loc;

		if (!IsAudioInitialized())
			return;

		alListener3f(AL_POSITION, loc.x, loc.y, loc.z);
	}

	void SetListenerVelocity(vec3 velocity)
	{
		g_listener_velocity = velocity;

		if (!IsAudioInitialized())
			return;

		alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
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
		// Actor access is intentionally not assumed here because the exact
		// Lynx Actor transform API is engine-specific. When you have a transform
		// getter, call SetListenerLocation(actor_position) from your game tick.
		//
		// This function still reapplies the last explicitly supplied state.
		if (!IsAudioInitialized())
			return;

		alListener3f(
			AL_POSITION,
			g_listener_location.x,
			g_listener_location.y,
			g_listener_location.z
		);

		alListener3f(
			AL_VELOCITY,
			g_listener_velocity.x,
			g_listener_velocity.y,
			g_listener_velocity.z
		);

		const ALfloat orientation[6] = {
			g_listener_forward.x, g_listener_forward.y, g_listener_forward.z,
			g_listener_up.x, g_listener_up.y, g_listener_up.z
		};

		alListenerfv(AL_ORIENTATION, orientation);
	}
}
