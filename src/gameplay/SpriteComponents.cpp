#include "SpriteComponents.h"

#include "Actor.h"
#include "../core/Engine.h"
#include "../core/RessourceManager.h"

#include <hrl/hrl.h>

#include <cmath>
#include <iostream>
#include <unordered_map>

namespace lynx
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;
	}


	// ========================================================================
	// SpriteComponent
	// ========================================================================

	SpriteComponent::SpriteComponent()
	{
		EnsureMesh();
	}

	SpriteComponent::~SpriteComponent()
	{
		if (mesh_ != kInvalid)
			HRL_DeleteMesh(mesh_);
	}

	bool SpriteComponent::EnsureMesh()
	{
		if (mesh_ != kInvalid)
			return true;

		const uint32_t scene = Engine::GetScene();

		if (!HRL_IsValidScene(scene))
			return false;

		mesh_ = HRL_CreateMeshSprite(scene);

		if (mesh_ == kInvalid)
			return false;

		// Le mesh suit l'acteur des le premier Update.
		has_last_ = false;

		// Selection dans l'editeur (clic sur le sprite -> l'acteur).
		if (Actor* owner = GetOwner())
			HRL_SetMeshUserHandle(mesh_, owner);

		return true;
	}

	void SpriteComponent::OnAttach()
	{
		if (EnsureMesh())
		{
			if (Actor* owner = GetOwner())
				HRL_SetMeshUserHandle(mesh_, owner);
		}

		SyncTransform(true);
	}

	void SpriteComponent::Update(float)
	{
		SyncTransform(false);
	}

	void SpriteComponent::Refresh()
	{
		// Les classes filles appliquent texture / animation dans Update.
		Update(0.f);
		SyncTransform(true);
	}

	void SpriteComponent::SyncTransform(bool force)
	{
		Actor* owner = GetOwner();

		if (!owner || !EnsureMesh())
			return;

		const transform& t = owner->transform;

		// Le retournement de l'acteur (scale negative) retourne aussi l'offset.
		const float actor_flip_x = t.scale.x < 0.f ? -1.f : 1.f;
		const float actor_flip_y = t.scale.y < 0.f ? -1.f : 1.f;
		const float abs_x = std::fabs(t.scale.x);
		const float abs_y = std::fabs(t.scale.y);

		const float x = t.location.x + offset.x * abs_x * actor_flip_x;
		const float y = t.location.y + offset.y * abs_y * actor_flip_y;
		const float z = t.location.z + offset.z;

		float sx = t.scale.x * size.x * (flip_x ? -1.f : 1.f);
		float sy = t.scale.y * size.y * (flip_y ? -1.f : 1.f);

		// Pas de visibilite dans l'API mesh : un sprite cache a une taille nulle.
		if (!visible)
			sx = sy = 0.f;

		const float state[8] = { x, y, z, sx, sy, 1.f, 0.f, 0.f };

		if (!force && has_last_)
		{
			bool same = true;

			for (int i = 0; i < 8; ++i)
				same = same && state[i] == last_[i];

			if (same)
				return;
		}

		HRL_SetMeshLocation(mesh_, x, y, z);
		HRL_SetMeshScale(mesh_, sx, sy, 1.f);

		for (int i = 0; i < 8; ++i)
			last_[i] = state[i];

		has_last_ = true;
	}

	uint32_t SpriteComponent::MaterialForTexture(const std::string& texture)
	{
		// Un materiau par texture, partage par tous les sprites statiques.
		static std::unordered_map<std::string, uint32_t> materials;

		if (auto it = materials.find(texture); it != materials.end())
			return it->second;

		const uint32_t tex = RessourceTex(texture.c_str());

		if (tex == kInvalid)
			return kInvalid;

		const uint32_t material = HRL_CreateMaterial(HRL_SPRITE_SHADER);
		HRL_MaterialSetTexture(material, HRL_T_ALBEDO, tex);

		materials.emplace(texture, material);
		return material;
	}


	// ========================================================================
	// StaticSpriteComponent
	// ========================================================================

	void StaticSpriteComponent::Update(float dt)
	{
		SpriteComponent::Update(dt);

		if (mesh_ == kInvalid)
			return;

		if (texture != applied_texture_)
		{
			applied_texture_ = texture;

			if (!texture.empty())
			{
				const uint32_t material = MaterialForTexture(texture);

				if (material != kInvalid)
					HRL_SetMeshMaterial(mesh_, material);
				else
					std::cout << "[sprite] texture not found: " << texture << std::endl;
			}
		}

		if (region != applied_region_)
		{
			applied_region_ = region;
			HRL_SetSpriteRegion(mesh_, region.x, region.y, region.z, region.w);
		}
	}


	// ========================================================================
	// AnimationSpriteComponent
	// ========================================================================

	AnimationSpriteComponent::AnimationSpriteComponent() = default;

	// Les animations reference le mesh : detruites avant lui (~SpriteComponent).
	AnimationSpriteComponent::~AnimationSpriteComponent()
	{
		single_.reset();
		blend_spaces_.clear();
		animations_.clear();
	}

	void AnimationSpriteComponent::SetAnimation(const std::string& tex, int frames, float time, bool looping)
	{
		texture = tex;
		frame_count = frames;
		frame_time = time;
		loop = looping;
		mode = Mode::Single;

		RebuildSingle();
		ApplyMode();

		if (auto_play)
			Play();
	}

	void AnimationSpriteComponent::RebuildSingle()
	{
		single_.reset();
		built_texture_ = texture;
		built_frames_ = frame_count;

		if (texture.empty() || frame_count < 1 || !EnsureMesh())
			return;

		single_ = std::make_unique<animation>(mesh_, texture.c_str(), frame_count, loop, frame_time);

		for (const FrameEvent& e : single_events_)
			single_->add_event(e.frame, e.callback);

		single_->on_finished([this]()
		{
			playing_ = false;

			if (single_finished_)
				single_finished_();
		});
	}

	void AnimationSpriteComponent::ApplyMode()
	{
		applied_mode_ = mode;

		if (mode == Mode::Single)
		{
			if (single_)
				single_->play();
		}
		else if (!manager_.current_state().empty())
		{
			// Remet le materiau de l'etat courant.
			manager_.force_state(manager_.current_state());
		}
	}

	void AnimationSpriteComponent::Play()
	{
		if (single_ && single_->is_finished())
			single_->restart();

		playing_ = true;
		started_ = true;
	}

	void AnimationSpriteComponent::Pause()
	{
		playing_ = false;
		started_ = true;
	}

	void AnimationSpriteComponent::Stop()
	{
		playing_ = false;
		started_ = true;

		if (single_)
			single_->restart();
	}

	void AnimationSpriteComponent::Restart()
	{
		if (single_)
			single_->restart();

		playing_ = true;
		started_ = true;
	}

	bool AnimationSpriteComponent::IsFinished() const
	{
		if (mode == Mode::StateMachine)
			return manager_.is_current_finished();

		return single_ && single_->is_finished();
	}

	int AnimationSpriteComponent::GetFrame() const
	{
		return single_ ? single_->current_frame() + 1 : 0;
	}

	void AnimationSpriteComponent::AddFrameEvent(int frame, std::function<void()> callback)
	{
		if (!callback)
			return;

		single_events_.push_back({frame, callback});

		if (single_)
			single_->add_event(frame, std::move(callback));
	}

	void AnimationSpriteComponent::ClearFrameEvents()
	{
		single_events_.clear();

		if (single_)
			single_->clear_events();
	}

	void AnimationSpriteComponent::OnFinished(std::function<void()> callback)
	{
		single_finished_ = std::move(callback);
	}

	animation& AnimationSpriteComponent::AddAnimation(
		const std::string& name, const std::string& tex, int frames, bool looping, float time)
	{
		mode = Mode::StateMachine;

		// Une animation deja utilisee par un etat ne peut pas etre remplacee
		// (l'anim_manager garde son adresse) : on garde l'existante.
		if (auto it = animations_.find(name); it != animations_.end())
		{
			std::cout << "[sprite] animation \"" << name << "\" already exists" << std::endl;
			return *it->second;
		}

		EnsureMesh();

		auto anim = std::make_unique<animation>(mesh_, tex.c_str(), frames < 1 ? 1 : frames, looping, time);
		animation& ref = *anim;
		animations_.emplace(name, std::move(anim));
		return ref;
	}

	blend_space& AnimationSpriteComponent::AddBlendSpace(const std::string& name)
	{
		mode = Mode::StateMachine;

		auto& slot = blend_spaces_[name];

		if (!slot)
			slot = std::make_unique<blend_space>();

		return *slot;
	}

	bool AnimationSpriteComponent::AddBlendSample(const std::string& space, float position, const std::string& anim)
	{
		blend_space* bs = GetBlendSpace(space);
		animation* a = GetAnimation(anim);

		if (!bs || !a)
			return false;

		bs->add(position, *a);
		return true;
	}

	animation* AnimationSpriteComponent::GetAnimation(const std::string& name)
	{
		auto it = animations_.find(name);
		return it == animations_.end() ? nullptr : it->second.get();
	}

	blend_space* AnimationSpriteComponent::GetBlendSpace(const std::string& name)
	{
		auto it = blend_spaces_.find(name);
		return it == blend_spaces_.end() ? nullptr : it->second.get();
	}

	bool AnimationSpriteComponent::AddState(const std::string& state, const std::string& target, anim_state_options options)
	{
		mode = Mode::StateMachine;

		playable* p = GetAnimation(target);

		if (!p)
			p = GetBlendSpace(target);

		if (!p)
		{
			std::cout << "[sprite] AddState(\"" << state << "\") : no animation or blend space \""
			          << target << "\"" << std::endl;
			return false;
		}

		manager_.add_state(state, *p, std::move(options));
		return true;
	}

	void AnimationSpriteComponent::SetDefaultState(const std::string& state)
	{
		mode = Mode::StateMachine;
		manager_.set_default_state(state);
	}

	void AnimationSpriteComponent::OnAttach()
	{
		SpriteComponent::OnAttach();
	}

	void AnimationSpriteComponent::BeginPlay()
	{
		if (mode == Mode::Single && auto_play && !started_)
			Play();
	}

	void AnimationSpriteComponent::Update(float dt)
	{
		SpriteComponent::Update(dt);

		// Champs Single modifies directement (texture, frame_count...).
		if (mode == Mode::Single)
		{
			if (texture != built_texture_ || frame_count != built_frames_)
			{
				RebuildSingle();
				ApplyMode();
			}

			if (single_)
			{
				single_->anim_time = frame_time > 0.f ? frame_time : 0.001f;
				single_->set_loop(loop);
			}
		}

		if (mode != applied_mode_)
			ApplyMode();
	}

	void AnimationSpriteComponent::Tick(float dt)
	{
		if (mode == Mode::Single)
		{
			if (single_ && playing_)
				single_->update(dt);
		}
		else
		{
			manager_.update(dt);
		}
	}
}
