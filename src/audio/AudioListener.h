#pragma once

#include "../core/Common.h"

namespace lynx
{
	class Actor;

	LYNX_API void AttachAudioListener(Actor* target);
	LYNX_API void UnattachAudioListener();
	LYNX_API void SetUseDopplerEffect(bool use);

	LYNX_API void SetListenerLocation(vec3 loc);
	LYNX_API void SetListenerVelocity(vec3 velocity);

	// Forward/up vectors used by OpenAL for stereo orientation.
	LYNX_API void SetListenerOrientation(vec3 forward, vec3 up);

	// Call once per frame if the listener is attached to an Actor.
	LYNX_API void UpdateAudioListener();
}
