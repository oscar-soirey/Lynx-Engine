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
	 * Trigger(intensity, length) scales the amplitudes and the duration of the
	 * settings for this shake only (a big impact : Engine.cameraShake(2.5, 1.6)).
	 * A weaker shake does not cut a stronger one still running.
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
		float scale = 1.f;          // running shake : amplitude multiplier
		float length_scale = 1.f;   // running shake : duration multiplier
		float phaseX = 0.f;
		float phaseY = 0.f;
		float phaseR = 0.f;

		/** Duration of the running shake (settings x length of the trigger). */
		float Duration() const
		{
			return duration * length_scale;
		}

		/** Amplitude multiplier left in the running shake (0 : none). */
		float CurrentStrength() const
		{
			if (!active)
				return 0.f;
			const float t = std::clamp(elapsed / std::max(Duration(), 0.0001f), 0.f, 1.f);
			return scale * std::pow(1.f - t, std::max(falloff, 0.01f));
		}

		void Trigger(float intensity = 1.f, float length = 1.f)
		{
			if (!enabled)
				return;

			intensity = std::clamp(intensity, 0.f, 20.f);
			if (active && CurrentStrength() > intensity)
				return;

			scale = intensity;
			length_scale = std::clamp(length, 0.05f, 20.f);
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
			scale = 1.f;
			length_scale = 1.f;
			elapsed = 0.f;
		}

		/** Offset at `time` seconds after the trigger (0 after `duration`). */
		void Sample(float time, float& x, float& y, float& rotationZ) const
		{
			x = y = rotationZ = 0.f;
			const float total = Duration();
			if (time < 0.f || time >= total)
				return;

			const float t = time / std::max(total, 0.0001f);
			const float strength = scale * std::pow(1.f - t, std::max(falloff, 0.01f));
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

			if (elapsed >= Duration())
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
