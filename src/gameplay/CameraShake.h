#pragma once

#include <algorithm>
#include <random>

#include "../core/Common.h"

namespace lynx
{
	struct CameraShake
	{
		bool enabled = true;

		float duration = 0.15f;
		float elapsed = 0.f;

		float positionAmplitude = 0.7f;
		float rotationAmplitude = 0.6f;
		float frequency = 20.0f;
		float falloff = 1.25f;

		bool active = false;
		float phaseX = 0.f;
		float phaseY = 0.f;
		float phaseR = 0.f;

		void Trigger()
		{
			if (!enabled)
				return;

			elapsed = 0.f;
			active = true;

			static std::mt19937 rng(std::random_device{}());
			std::uniform_real_distribution<float> phase(0.f, 6.28318530718f);

			phaseX = phase(rng);
			phaseY = phase(rng);
			phaseR = phase(rng);
		}

		void Update(float dt, float& x, float& y, float& rotationZ)
		{
			if (!active)
				return;

			elapsed += dt;

			if (elapsed >= duration)
			{
				active = false;
				return;
			}

			float t = elapsed / std::max(duration, 0.0001f);
			float strength = std::pow(
					1.f - t,
					std::max(falloff, 0.01f)
			);

			float time = elapsed * frequency;

			x += std::sin(time * 1.00f + phaseX)
					 * positionAmplitude * strength;

			y += std::sin(time * 1.37f + phaseY)
					 * positionAmplitude * strength;

			rotationZ += std::sin(time * 0.83f + phaseR)
									 * rotationAmplitude * strength;
		}
	};

	LYNX_API void SetCameraShake(CameraShake& camera_shake);
	LYNX_API CameraShake& GetCameraShake();
}
