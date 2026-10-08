#include "FogComponent.h"

#include "Actor.h"
#include "../core/Engine.h"

#include <hrl/hrl.h>

#include <algorithm>

namespace lynx
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;

		// The scene has one distance fog and one global volumetric fog : the
		// component that applied it last owns it (it turns it off when it goes).
		const FogComponent* g_scene_fog = nullptr;
		const VolumetricFogComponent* g_global_volumetric = nullptr;

		HRL_EFogType ToHRL(FogComponent::Mode mode)
		{
			switch (mode)
			{
				case FogComponent::Mode::Linear:     return HRL_FOG_LINEAR;
				case FogComponent::Mode::ExpSquared: return HRL_FOG_EXP_SQUARED;
				default:                             return HRL_FOG_EXPONENTIAL;
			}
		}

		bool SceneValid(uint32_t& scene)
		{
			scene = Engine::GetScene();
			return scene != kInvalid && HRL_IsValidScene(scene);
		}

		template <size_t N>
		bool Changed(const float (&state)[N], float (&last)[N], bool force, bool has_last)
		{
			if (force || !has_last)
				return true;
			for (size_t i = 0; i < N; ++i)
				if (state[i] != last[i])
					return true;
			return false;
		}
	}

	// =========================================================================
	// FogComponent
	// =========================================================================

	FogComponent::~FogComponent()
	{
		if (g_scene_fog != this)
			return;
		g_scene_fog = nullptr;
		uint32_t scene;
		if (SceneValid(scene))
			HRL_SetFogEnabled(scene, HRL_FALSE);
	}

	bool FogComponent::IsActive() const
	{
		return g_scene_fog == this;
	}

	void FogComponent::OnAttach() { Sync(true); }
	void FogComponent::Update(float) { Sync(false); }
	void FogComponent::LateUpdate(float) { Sync(false); }
	void FogComponent::Refresh() { Sync(true); }

	void FogComponent::Sync(bool force)
	{
		uint32_t scene;
		if (!SceneValid(scene))
			return;

		if (!enabled)
		{
			if (g_scene_fog == this)
			{
				g_scene_fog = nullptr;
				HRL_SetFogEnabled(scene, HRL_FALSE);
			}
			has_last_ = false;
			return;
		}

		// Another fog took the scene : this one waits (re-applied if it gets it back).
		if (g_scene_fog != this)
		{
			if (g_scene_fog && !force && has_last_)
				return;
			g_scene_fog = this;
			force = true;
		}

		const float state[8] = {
			static_cast<float>(mode), color.x, color.y, color.z,
			std::max(0.f, density), start, std::max(end, start + 0.01f), 1.f,
		};
		if (!Changed(state, last_, force, has_last_))
			return;

		HRL_SetFogEnabled(scene, HRL_TRUE);
		HRL_SetFogMode(scene, ToHRL(mode));
		HRL_SetFogColor(scene, color.x, color.y, color.z);
		HRL_SetFogDensity(scene, state[4]);
		HRL_SetFogLinearRange(scene, state[5], state[6]);

		std::copy(std::begin(state), std::end(state), last_);
		has_last_ = true;
	}

	// =========================================================================
	// VolumetricFogComponent
	// =========================================================================

	VolumetricFogComponent::~VolumetricFogComponent()
	{
		ReleaseVolume();
		ReleaseGlobal();
	}

	void VolumetricFogComponent::ReleaseVolume()
	{
		if (fog_ != kInvalid && HRL_IsValidVolumetricFog(fog_))
			HRL_DeleteVolumetricFog(fog_);
		fog_ = kInvalid;
	}

	void VolumetricFogComponent::ReleaseGlobal()
	{
		if (g_global_volumetric != this)
			return;
		g_global_volumetric = nullptr;
		uint32_t scene;
		if (SceneValid(scene))
			HRL_SetGlobalVolumetricFogEnabled(scene, HRL_FALSE);
	}

	void VolumetricFogComponent::OnAttach() { Sync(true); }
	void VolumetricFogComponent::Update(float) { Sync(false); }
	void VolumetricFogComponent::LateUpdate(float) { Sync(false); }   // final position of the frame
	void VolumetricFogComponent::Refresh() { Sync(true); }

	void VolumetricFogComponent::Sync(bool force)
	{
		uint32_t scene;
		if (!SceneValid(scene))
			return;

		const HRL_uint sample_steps = static_cast<HRL_uint>(std::clamp(steps, 4, 64));

		if (!enabled)
		{
			ReleaseVolume();
			ReleaseGlobal();
			has_last_ = false;
			return;
		}

		if (global)
		{
			ReleaseVolume();
			if (g_global_volumetric != this)
			{
				if (g_global_volumetric && !force && has_last_)
					return;   // another global volumetric fog has the scene
				g_global_volumetric = this;
				force = true;
			}
			const float state[12] = { 1.f, color.x, color.y, color.z, std::max(0.f, density),
			                          static_cast<float>(sample_steps) };
			if (!Changed(state, last_, force, has_last_))
				return;
			HRL_SetGlobalVolumetricFogEnabled(scene, HRL_TRUE);
			HRL_SetGlobalVolumetricFogColor(scene, color.x, color.y, color.z);
			HRL_SetGlobalVolumetricFogDensity(scene, state[4]);
			HRL_SetGlobalVolumetricFogSteps(scene, sample_steps);
			std::copy(std::begin(state), std::end(state), last_);
			has_last_ = true;
			return;
		}

		// A volume at the actor.
		ReleaseGlobal();
		if (fog_ != kInvalid && !HRL_IsValidVolumetricFog(fog_))
			fog_ = kInvalid;   // deleted by the renderer (scene recreated...)
		if (fog_ == kInvalid)
		{
			fog_ = HRL_CreateVolumetricFog(scene);
			has_last_ = false;
			if (fog_ == kInvalid)
				return;   // HRL_MAX_VOLUMETRIC_FOGS reached
		}

		const Actor* owner = GetOwner();
		const vec3 p = owner ? owner->transform.location + offset : offset;
		const float state[12] = { 0.f, color.x, color.y, color.z, std::max(0.f, density),
		                          static_cast<float>(sample_steps), std::max(0.01f, radius), p.x, p.y, p.z };
		if (!Changed(state, last_, force, has_last_))
			return;

		HRL_SetVolumetricFogEnabled(fog_, HRL_TRUE);
		HRL_SetVolumetricFogColor(fog_, color.x, color.y, color.z);
		HRL_SetVolumetricFogDensity(fog_, state[4]);
		HRL_SetVolumetricFogSteps(fog_, sample_steps);
		HRL_SetVolumetricFogRadius(fog_, state[6]);
		HRL_SetVolumetricFogPosition(fog_, p.x, p.y, p.z);
		std::copy(std::begin(state), std::end(state), last_);
		has_last_ = true;
	}
}
