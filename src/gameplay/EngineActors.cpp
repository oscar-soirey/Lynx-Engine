#include "EngineActors.h"
#include "Humanoid.h"

#include "SoundSourceComponent.h"
#include "../core/Engine.h"
#include "../core/Factory.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace lynx
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;
		constexpr int kIconCount = static_cast<int>(EngineActorIcon::Count);

#include "resources/actor_icons_png.inl"

		// Atlas texture and its sprite material (created on first use, with
		// the renderer ; recreated if the renderer was restarted).
		uint32_t g_icon_texture = kInvalid;
		uint32_t g_icon_material = kInvalid;

		bool EnsureIconMaterial()
		{
			if (g_icon_texture != kInvalid && HRL_IsValidTexture(g_icon_texture) &&
			    g_icon_material != kInvalid && HRL_IsValidMaterial(g_icon_material))
				return true;

			if (!HRL_IsValidScene(Engine::GetScene()))
				return false;

			g_icon_texture = HRL_CreateTexture(reinterpret_cast<const char*>(kActorIconsPng), sizeof(kActorIconsPng));
			if (g_icon_texture == kInvalid)
				return false;

			g_icon_material = HRL_CreateMaterial(HRL_SPRITE_SHADER);
			if (g_icon_material == kInvalid)
				return false;
			HRL_MaterialSetTexture(g_icon_material, HRL_T_ALBEDO, g_icon_texture);
			return true;
		}

		void IconRegion(EngineActorIcon icon, float uv[4])
		{
			const int index = std::clamp(static_cast<int>(icon), 0, kIconCount - 1);
			uv[0] = static_cast<float>(index) / kIconCount;
			uv[1] = 0.f;
			uv[2] = static_cast<float>(index + 1) / kIconCount;
			uv[3] = 1.f;
		}

		// Editor-only decorations (icons, direction lines).
		bool EditorDecorations()
		{
			return !Engine::IsReleaseMode();
		}

		// Light color for the editor lines (always visible).
		void LineColor(const vec3& color, float out[3])
		{
			const float m = std::max({color.x, color.y, color.z, 0.0001f});
			out[0] = std::clamp(color.x / m, 0.25f, 1.f);
			out[1] = std::clamp(color.y / m, 0.25f, 1.f);
			out[2] = std::clamp(color.z / m, 0.25f, 1.f);
		}

		void DrawArrow(const vec3& from, const vec3& direction, float length, const vec3& color)
		{
			const uint32_t scene = Engine::GetScene();
			if (!HRL_IsValidScene(scene))
				return;

			float c[3];
			LineColor(color, c);

			const vec3 to = from + direction * length;
			HRL_DrawDebugSegment(scene, from.x, from.y, from.z, to.x, to.y, to.z, c[0], c[1], c[2]);

			// Head : two short lines, in a plane that contains the direction.
			vec3 side(-direction.y, direction.x, 0.f);
			if (std::fabs(side.x) + std::fabs(side.y) < 0.001f)
				side = vec3(1.f, 0.f, 0.f);
			const float side_length = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
			side = side / side_length;

			const float head = length * 0.22f;
			const vec3 back = to - direction * head;
			const vec3 a = back + side * (head * 0.6f);
			const vec3 b = back - side * (head * 0.6f);
			HRL_DrawDebugSegment(scene, to.x, to.y, to.z, a.x, a.y, a.z, c[0], c[1], c[2]);
			HRL_DrawDebugSegment(scene, to.x, to.y, to.z, b.x, b.y, b.z, c[0], c[1], c[2]);
		}

		struct ClassInfo
		{
			EngineActorIcon icon;
		};

		const std::unordered_map<std::string, ClassInfo>& EngineClasses()
		{
			static const std::unordered_map<std::string, ClassInfo> classes = {
				{ "Actor", { EngineActorIcon::Actor } },
				{ "PointLightActor", { EngineActorIcon::PointLight } },
				{ "SpotLightActor", { EngineActorIcon::SpotLight } },
				{ "DirectionalLightActor", { EngineActorIcon::DirectionalLight } },
				{ "SkyLightActor", { EngineActorIcon::SkyLight } },
				{ "Light2DActor", { EngineActorIcon::PointLight } },
				{ "SpriteActor", { EngineActorIcon::Sprite } },
				{ "SoundActor", { EngineActorIcon::Sound } },
				{ "ColliderActor", { EngineActorIcon::Collider } },
				{ "Humanoid", { EngineActorIcon::Actor } },
			};
			return classes;
		}
	}


	// ========================================================================
	// EditorIconComponent
	// ========================================================================

	EditorIconComponent::EditorIconComponent(EngineActorIcon icon_type)
		: icon(icon_type)
	{
		size = {3.f, 3.f};   // voxels
	}

	void EditorIconComponent::Update(float dt)
	{
		visible = show && !playing_;

		if (mesh_ != kInvalid && icon != applied_icon_ && EnsureIconMaterial())
		{
			HRL_SetMeshMaterial(mesh_, g_icon_material);
			float uv[4];
			IconRegion(icon, uv);
			HRL_SetSpriteRegion(mesh_, uv[0], uv[1], uv[2], uv[3]);
			applied_icon_ = icon;
		}

		SpriteComponent::Update(dt);
	}

	void EditorIconComponent::BeginPlay()
	{
		playing_ = true;
		visible = false;
		Refresh();
	}

	void EditorIconComponent::EndPlay()
	{
		playing_ = false;
		visible = show;
		Refresh();
	}


	// ========================================================================
	// Lights
	// ========================================================================

	LightActor::LightActor(LightComponent::Type type, EngineActorIcon icon)
		: type_(type), icon_(icon)
	{
		HPROPERTY(color, Exposed);
		HPROPERTY(intensity, Exposed);
		HPROPERTY(enabled, Exposed);
	}

	void LightActor::Init()
	{
		Actor::Init();

		light_ = &AddComponent<LightComponent>(type_);
		// The actor rotation turns the light (rotate gizmo, Details).
		light_->rotation = vec3(0.f);
		light_->use_actor_rotation = true;
		ApplyProperties(*light_);
		light_->Refresh();

		if (EditorDecorations())
			AddComponent<EditorIconComponent>(icon_);
	}

	void LightActor::Update(double dt)
	{
		Actor::Update(dt);

		if (!light_)
			return;

		ApplyProperties(*light_);

		if (!playing_ && EditorDecorations())
			DrawEditorHelpers();
	}

	void LightActor::StartGame()
	{
		Actor::StartGame();
		playing_ = true;
	}

	void LightActor::EndGame()
	{
		Actor::EndGame();
		playing_ = false;
	}

	void LightActor::ApplyProperties(LightComponent& light)
	{
		light.type = type_;
		light.color = color;
		light.intensity = intensity;
		light.enabled = enabled;
	}

	void LightActor::DrawEditorHelpers()
	{
	}


	PointLightActor::PointLightActor()
		: LightActor(LightComponent::Type::Point, EngineActorIcon::PointLight)
	{
		HPROPERTY(attenuation, Exposed);
		HPROPERTY(cast_shadows, Exposed);
		HPROPERTY(shadow_strength, Exposed);

		// In front of the level (the camera looks towards -Z).
		transform.location.z = 10.f;   // voxels in front of the level
	}

	void PointLightActor::ApplyProperties(LightComponent& light)
	{
		LightActor::ApplyProperties(light);
		light.attenuation = attenuation;
		light.cast_shadows = cast_shadows;
		light.shadow_strength = shadow_strength;
	}


	SpotLightActor::SpotLightActor()
		: LightActor(LightComponent::Type::Spot, EngineActorIcon::SpotLight)
	{
		HPROPERTY(attenuation, Exposed);
		HPROPERTY(inner_angle, Exposed);
		HPROPERTY(outer_angle, Exposed);
		HPROPERTY(cast_shadows, Exposed);
		HPROPERTY(shadow_strength, Exposed);

		transform.location.z = 10.f;   // voxels in front of the level
		transform.rotation = vec3(0.f, -90.f, 0.f);   // towards the level
		intensity = 2.f;
	}

	void SpotLightActor::ApplyProperties(LightComponent& light)
	{
		LightActor::ApplyProperties(light);
		light.attenuation = attenuation;
		light.inner_angle = inner_angle;
		light.outer_angle = outer_angle;
		light.cast_shadows = cast_shadows;
		light.shadow_strength = shadow_strength;
	}

	void SpotLightActor::DrawEditorHelpers()
	{
		if (LightComponent* light = GetLight())
			DrawArrow(light->GetWorldLocation(), light->GetDirection(), 8.f, color);
	}


	DirectionalLightActor::DirectionalLightActor()
		: LightActor(LightComponent::Type::Directional, EngineActorIcon::DirectionalLight)
	{
		HPROPERTY(cast_shadows, Exposed);
		HPROPERTY(shadow_strength, Exposed);

		transform.location.z = 10.f;   // voxels in front of the level
		transform.rotation = vec3(-45.f, -60.f, 0.f);   // from the top right, into the level
		color = vec3(1.f, 0.96f, 0.88f);
	}

	void DirectionalLightActor::ApplyProperties(LightComponent& light)
	{
		LightActor::ApplyProperties(light);
		light.cast_shadows = cast_shadows;
		light.shadow_strength = shadow_strength;
	}

	void DirectionalLightActor::DrawEditorHelpers()
	{
		if (LightComponent* light = GetLight())
			DrawArrow(transform.location, light->GetDirection(), 12.f, color);
	}


	// ------------------------------------------------------------------------
	// Light2DActor
	// ------------------------------------------------------------------------

	Light2DActor::Light2DActor()
	{
		HPROPERTY(color, Exposed);
		HPROPERTY(intensity, Exposed);
		HPROPERTY(radius, Exposed);
		HPROPERTY(falloff, Exposed);
		HPROPERTY(enabled, Exposed);
		HPROPERTY(cast_shadows, Exposed);
		HPROPERTY(shadow_strength, Exposed);
		HPROPERTY(source_radius, Exposed);
		HPROPERTY(cone_angle, Exposed);
		HPROPERTY(cone_softness, Exposed);
		HPROPERTY(direction, Exposed);
		HPROPERTY(flicker, Exposed);
	}

	void Light2DActor::Init()
	{
		Actor::Init();
		light_ = &AddComponent<Light2DComponent>();
		ApplyProperties();
		if (EditorDecorations())
			AddComponent<EditorIconComponent>(EngineActorIcon::PointLight);
	}

	void Light2DActor::Update(double dt)
	{
		Actor::Update(dt);
		if (!light_)
			return;
		ApplyProperties();
		if (!playing_ && EditorDecorations())
			DrawEditorHelpers();
	}

	void Light2DActor::StartGame()
	{
		Actor::StartGame();
		playing_ = true;
	}

	void Light2DActor::EndGame()
	{
		Actor::EndGame();
		playing_ = false;
	}

	void Light2DActor::ApplyProperties()
	{
		light_->color = color;
		light_->intensity = intensity;
		light_->radius = radius;
		light_->falloff = falloff;
		light_->enabled = enabled;
		light_->cast_shadows = cast_shadows;
		light_->shadow_strength = shadow_strength;
		light_->source_radius = source_radius;
		light_->cone_angle = cone_angle;
		light_->cone_softness = cone_softness;
		light_->direction = direction;
		light_->flicker = flicker;
	}

	void Light2DActor::DrawEditorHelpers()
	{
		const uint32_t scene = Engine::GetScene();
		if (!HRL_IsValidScene(scene) || !light_)
			return;

		// Reach of the light (radius in voxels -> world units).
		float ox = 0.f, oy = 0.f, sx = 1.f, sy = 0.f;
		HRL_VoxelToWorldCoordinates(scene, 0.f, 0.f, &ox, &oy);
		HRL_VoxelToWorldCoordinates(scene, 1.f, 0.f, &sx, &sy);
		const float r = radius * std::max(1e-5f, sx - ox);

		float c[3];
		LineColor(color, c);
		const vec3 center = light_->GetWorldLocation();
		const bool cone = cone_angle < 359.f;
		const float dir = light_->GetWorldDirection() * 3.14159265f / 180.f;
		const float half = std::clamp(cone_angle, 1.f, 359.f) * 0.5f * 3.14159265f / 180.f;
		const float a0 = cone ? dir - half : 0.f;
		const float a1 = cone ? dir + half : 6.2831853f;
		constexpr int kSegments = 40;
		vec3 previous = cone ? center : center + vec3(std::cos(a0) * r, std::sin(a0) * r, 0.f);
		for (int i = 0; i <= kSegments; ++i)
		{
			const float a = a0 + (a1 - a0) * static_cast<float>(i) / kSegments;
			const vec3 p = center + vec3(std::cos(a) * r, std::sin(a) * r, 0.f);
			HRL_DrawDebugSegment(scene, previous.x, previous.y, previous.z, p.x, p.y, p.z, c[0], c[1], c[2]);
			previous = p;
		}
		if (cone)
			HRL_DrawDebugSegment(scene, previous.x, previous.y, previous.z, center.x, center.y, center.z,
			                     c[0], c[1], c[2]);
	}


	SkyLightActor::SkyLightActor()
		: LightActor(LightComponent::Type::Sky, EngineActorIcon::SkyLight)
	{
		transform.location.z = 10.f;   // voxels in front of the level
		color = vec3(0.75f, 0.85f, 1.f);
		intensity = 0.4f;
	}


	// ========================================================================
	// SpriteActor
	// ========================================================================

	SpriteActor::SpriteActor()
	{
		HPROPERTY(texture, Exposed);
		HPROPERTY(size, Exposed);
		HPROPERTY(flip_x, Exposed);
		HPROPERTY(flip_y, Exposed);
		HPROPERTY(visible, Exposed);
	}

	void SpriteActor::Init()
	{
		Actor::Init();
		sprite_ = &AddComponent<StaticSpriteComponent>();
		if (EditorDecorations())
			icon_ = &AddComponent<EditorIconComponent>(EngineActorIcon::Sprite);
		Update(0.0);
	}

	void SpriteActor::Update(double dt)
	{
		Actor::Update(dt);

		if (sprite_)
		{
			sprite_->texture = texture;
			sprite_->size = size;
			sprite_->flip_x = flip_x;
			sprite_->flip_y = flip_y;
			sprite_->visible = visible && !texture.empty();
		}

		// The icon only while there is no image to click on.
		if (icon_)
			icon_->show = texture.empty() || !visible;
	}


	// ========================================================================
	// SoundActor
	// ========================================================================

	SoundActor::SoundActor()
	{
		HPROPERTY(sound, Exposed);
		HPROPERTY(volume, Exposed);
		HPROPERTY(pitch, Exposed);
		HPROPERTY(loop, Exposed);
		HPROPERTY(spatial, Exposed);
		HPROPERTY(play_on_begin, Exposed);
		HPROPERTY(max_distance, Exposed);

		HFUNCTION(Play);
		HFUNCTION(Stop);
	}

	void SoundActor::Init()
	{
		Actor::Init();
		source_ = &AddComponent<SoundSourceComponent>();
		if (EditorDecorations())
			AddComponent<EditorIconComponent>(EngineActorIcon::Sound);
		Update(0.0);
	}

	void SoundActor::Update(double dt)
	{
		Actor::Update(dt);

		if (!source_)
			return;

		source_->sound = sound;
		source_->volume = volume;
		source_->pitch = pitch;
		source_->loop = loop;
		source_->spatial = spatial;
		source_->play_on_begin = play_on_begin;
		source_->max_distance = std::max(max_distance, source_->reference_distance + 0.01f);
	}

	void SoundActor::Play()
	{
		if (source_)
		{
			Update(0.0);
			source_->Play();
		}
	}

	void SoundActor::Stop()
	{
		if (source_)
			source_->Stop();
	}


	// ========================================================================
	// ColliderActor
	// ========================================================================

	ColliderActor::ColliderActor()
	{
		HPROPERTY(size, Exposed);
		HPROPERTY(trigger, Exposed);
		HPROPERTY(movable, Exposed);
		HPROPERTY(generate_overlap_events, Exposed);
		HPROPERTY(layer, Exposed);
		HPROPERTY(mask, Exposed);
		HPROPERTY(show_in_game, Exposed);
	}

	void ColliderActor::Init()
	{
		Actor::Init();
		collider_ = &AddComponent<ColliderComponent>();
		if (EditorDecorations())
			AddComponent<EditorIconComponent>(EngineActorIcon::Collider);
		Update(0.0);
	}

	void ColliderActor::Update(double dt)
	{
		Actor::Update(dt);

		if (!collider_)
			return;

		collider_->size = size;
		collider_->trigger = trigger;
		collider_->movable = movable;
		collider_->generate_overlap_events = generate_overlap_events;
		collider_->layer = static_cast<uint32_t>(layer);
		collider_->mask = static_cast<uint32_t>(mask);
		// The box : always in the editor, in the game when asked.
		collider_->debug_draw = playing_ ? show_in_game : EditorDecorations();
	}

	void ColliderActor::StartGame()
	{
		Actor::StartGame();
		playing_ = true;
		Update(0.0);
	}

	void ColliderActor::EndGame()
	{
		Actor::EndGame();
		playing_ = false;
		Update(0.0);
	}


	// ========================================================================
	// Factory
	// ========================================================================

	void RegisterEngineActors(FactoryObject& factory)
	{
		factory.RegisterObject("Actor", []() -> Object*
		{
			// A plain actor : the icon makes it visible / clickable in the editor.
			Actor* actor = new Actor();
			if (EditorDecorations())
				actor->AddComponent<EditorIconComponent>(EngineActorIcon::Actor);
			return actor;
		});
		factory.RegisterObject("PointLightActor", []() -> Object* { return new PointLightActor(); });
		factory.RegisterObject("SpotLightActor", []() -> Object* { return new SpotLightActor(); });
		factory.RegisterObject("DirectionalLightActor", []() -> Object* { return new DirectionalLightActor(); });
		factory.RegisterObject("SkyLightActor", []() -> Object* { return new SkyLightActor(); });
		factory.RegisterObject("Light2DActor", []() -> Object* { return new Light2DActor(); });
		factory.RegisterObject("SpriteActor", []() -> Object* { return new SpriteActor(); });
		factory.RegisterObject("SoundActor", []() -> Object* { return new SoundActor(); });
		factory.RegisterObject("ColliderActor", []() -> Object* { return new ColliderActor(); });
		factory.RegisterObject("Humanoid", []() -> Object* { return new Humanoid(); });

		for (const auto& [name, info] : EngineClasses())
			factory.MarkBuiltin(name.c_str());
	}

	bool IsEngineActorClass(const std::string& class_name)
	{
		return EngineClasses().count(class_name) != 0;
	}

	bool GetEngineActorIcon(const std::string& class_name, uint32_t& texture, float uv[4])
	{
		const auto& classes = EngineClasses();
		const auto it = classes.find(class_name);
		if (it == classes.end() || !EnsureIconMaterial())
			return false;

		texture = g_icon_texture;
		IconRegion(it->second.icon, uv);
		return true;
	}
}
