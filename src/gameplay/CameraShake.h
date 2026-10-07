#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

#include "../core/Common.h"

namespace lynx
{
	/**
	 * Camera shake of the game : one global shake, applied by the cameras whose
	 * use_camera_shake is on (CameraComponent). Settings : Windows > Camera
	 * Shake in the editor, saved in assets/camera_shake.json and loaded at
	 * start (editor and shipped game).
	 *
	 * C++ : lynx::GetCameraShake().Trigger();   JS : Engine.cameraShake();
	 */
	struct CameraShake
	{
		bool enabled = true;

		float duration = 0.15f;
		float elapsed = 0.f;

		float positionAmplitude = 2.3f;   // voxels
		float rotationAmplitude = 0.6f;   // degrees
		float frequency = 20.0f;
		float falloff = 1.25f;

		// Axes shaken (horizontal, vertical, roll).
		bool shake_x = true;
		bool shake_y = true;
		bool shake_roll = true;

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

		void Stop()
		{
			active = false;
			elapsed = 0.f;
		}

		/** Offset at `time` seconds after the trigger (0 after `duration`). */
		void Sample(float time, float& x, float& y, float& rotationZ) const
		{
			x = y = rotationZ = 0.f;
			if (time < 0.f || time >= duration)
				return;

			const float t = time / std::max(duration, 0.0001f);
			const float strength = std::pow(1.f - t, std::max(falloff, 0.01f));
			const float w = time * frequency;

			if (shake_x)
				x = std::sin(w * 1.00f + phaseX) * positionAmplitude * strength;
			if (shake_y)
				y = std::sin(w * 1.37f + phaseY) * positionAmplitude * strength;
			if (shake_roll)
				rotationZ = std::sin(w * 0.83f + phaseR) * rotationAmplitude * strength;
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

			float sx = 0.f, sy = 0.f, sr = 0.f;
			Sample(elapsed, sx, sy, sr);
			x += sx;
			y += sy;
			rotationZ += sr;
		}
	};

	LYNX_API void SetCameraShake(CameraShake& camera_shake);
	LYNX_API CameraShake& GetCameraShake();

	/** Settings (not the running state) from / to an asset file. */
	LYNX_API bool LoadCameraShakeSettings(const std::string& path = "camera_shake.json");
	LYNX_API bool SaveCameraShakeSettings(const std::string& path = "camera_shake.json");
}
