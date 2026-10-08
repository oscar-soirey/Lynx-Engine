#pragma once

/**
 * Fog of the scene. No HRL call needed : set the fields, the component sends
 * them to the renderer (at the next frame, or at once with Refresh()).
 *
 *     // Distance fog of the whole scene (one at a time : the last enabled one wins)
 *     auto& fog = actor->AddComponent<lynx::FogComponent>();
 *     fog.mode = lynx::FogComponent::Mode::Exponential;
 *     fog.color = {0.6f, 0.7f, 0.8f};
 *     fog.density = 0.02f;
 *
 *     // Volumetric fog : a ball of mist at the actor (+ offset), or the whole scene
 *     auto& mist = actor->AddComponent<lynx::VolumetricFogComponent>();
 *     mist.radius = 20.f;
 *     mist.density = 0.4f;
 *     mist.global = false;          // true : fills the whole scene (one at a time)
 *
 * JS : addComponent("Fog", { mode: "exponential", color, density, start, end })
 *      addComponent("VolumetricFog", { radius, density, color, steps, offset, global })
 *
 * Removing the component (or destroying the actor) removes its fog.
 */

#include <cstdint>

#include "Component.h"

namespace lynx
{
	class LYNX_API FogComponent : public Component
	{
	public:
		enum class Mode
		{
			Linear,        // from `start` (no fog) to `end` (full fog)
			Exponential,   // density : thicker with the distance
			ExpSquared     // density : clear near the camera, then thick quickly
		};

		FogComponent() = default;
		~FogComponent() override;

		bool enabled = true;
		Mode mode = Mode::Exponential;
		vec3 color{0.62f, 0.68f, 0.78f};

		/** Exponential / ExpSquared. */
		float density = 0.015f;

		/** Linear : distances from the camera (world units). */
		float start = 40.f;
		float end = 160.f;

		/** Applies the fields now (otherwise : at the next frame). */
		void Refresh();

		/** This component is the one that draws the fog of the scene. */
		bool IsActive() const;

	protected:
		void OnAttach() override;
		void Update(float dt) override;
		void LateUpdate(float dt) override;

	private:
		void Sync(bool force);

		float last_[8] = {};
		bool has_last_ = false;
	};

	class LYNX_API VolumetricFogComponent : public Component
	{
	public:
		VolumetricFogComponent() = default;
		~VolumetricFogComponent() override;

		bool enabled = true;

		/** true : fills the whole scene (one at a time, the last enabled one wins) ; false : a volume. */
		bool global = false;

		vec3 color{0.75f, 0.78f, 0.85f};
		float density = 0.3f;

		/** Volume : radius (world units) around the actor location + offset. */
		float radius = 16.f;
		vec3 offset{0.f};

		/** Ray-march samples (4..64) : more = smoother, slower. */
		int steps = 24;

		void Refresh();

		/** Renderer id of the volume (advanced use) ; 0xFFFFFFFF : none (global, or before the first frame). */
		uint32_t GetFogId() const { return fog_; }

	protected:
		void OnAttach() override;
		void Update(float dt) override;
		void LateUpdate(float dt) override;

	private:
		void Sync(bool force);
		void ReleaseVolume();
		void ReleaseGlobal();

		uint32_t fog_ = 0xFFFFFFFFu;
		float last_[12] = {};
		bool has_last_ = false;
	};
}
