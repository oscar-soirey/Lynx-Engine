#pragma once

/**
 * Lumiere attachee a l'acteur.
 *
 *     auto& light = actor->AddComponent<lynx::LightComponent>();
 *     light.color = {1.f, 0.8f, 0.6f};
 *     light.intensity = 5.f;
 *
 * type : Point (suit l'acteur + offset) ou Sky (eclairage global).
 * Les champs s'appliquent a la frame suivante.
 */

#include <cstdint>

#include "Component.h"

namespace lynx
{
	class LYNX_API LightComponent : public Component
	{
	public:
		enum class Type
		{
			Point,
			Sky
		};

		~LightComponent() override;

		Type type = Type::Point;
		vec3 color{1.f, 1.f, 1.f};
		float intensity = 1.f;
		vec3 offset{0.f};

		/** Faux : intensite 0 (la lumiere existe toujours). */
		bool enabled = true;

		uint32_t GetLightId() const { return light_; }

	protected:
		void OnAttach() override;
		void Update(float dt) override;

	private:
		void Sync();

		uint32_t light_ = 0xFFFFFFFFu;
		Type created_type_ = Type::Point;
	};
}
