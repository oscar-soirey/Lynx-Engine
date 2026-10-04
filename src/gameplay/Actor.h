#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Object.h"
#include "Component.h"
#include "Components.h"
#include "../core/data/EventDispatcher.h"

namespace lynx
{
	class Engine;

	class LYNX_API Actor : public Object {
		friend class Level;
		friend class Engine;
	public:
		transform transform{};

		/**
		 * Scripts attaches, separes par ';' (ex: "scripts/Player.js;scripts/Blink.js").
		 * Propriete reflechie : chargee/sauvegardee avec le niveau. La modifier
		 * ajoute/retire les scripts correspondants (voir ScriptComponent).
		 */
		std::string scripts;


		Actor();
		~Actor() override;


		//actor class is not copiable
		Actor(const Actor&) = delete;
		Actor& operator=(const Actor&) = delete;
		// Le deplacement transfere l'entite ECS (et donc les composants).
		Actor(Actor&&);
		Actor& operator=(Actor&&);

		HEventDispatcher<> ED_transform_modified;

		virtual void OnPossessed(int pc);
		virtual void OnUnpossessed(int pc);


		// ====================================================================
		// Composants (ECS, EnTT cote moteur)
		// T doit heriter de lynx::Component (voir Component.h). Les composants
		// du jeu fonctionnent exactement comme ceux du moteur.
		// ====================================================================

		/**
		 * Ajoute un composant T construit avec `args`, ou retourne celui deja
		 * present (un seul composant de chaque type par acteur).
		 */
		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			static_assert(std::is_base_of_v<Component, T>, "T must inherit from lynx::Component");
			if (T* existing = GetComponent<T>())
				return *existing;
			return static_cast<T&>(AddComponentInternal(detail::ComponentTypeId<T>(), new T(std::forward<Args>(args)...)));
		}

		/** @return nullptr si l'acteur n'a pas ce composant. */
		template<typename T>
		T* GetComponent() const
		{
			static_assert(std::is_base_of_v<Component, T>, "T must inherit from lynx::Component");
			return static_cast<T*>(GetComponentInternal(detail::ComponentTypeId<T>()));
		}

		template<typename T>
		bool HasComponent() const
			{ return GetComponent<T>() != nullptr; }

		/** EndPlay est appele si besoin, puis le composant est detruit. */
		template<typename T>
		void RemoveComponent()
			{ RemoveComponentInternal(detail::ComponentTypeId<T>()); }

		/** Tous les composants de l'acteur (ordre stable). */
		std::vector<Component*> GetComponents() const;

		// Utilises par les templates ci-dessus. AddComponentInternal prend
		// possession du composant.
		Component& AddComponentInternal(uint32_t type_id, Component* component);
		Component* GetComponentInternal(uint32_t type_id) const;
		void RemoveComponentInternal(uint32_t type_id);

		// Raccourcis vers ScriptComponent
		bool AddScript(const char* path);
		void RemoveScript(const char* path);

		/** Identifiant de l'entite EnTT (entt::entity) de cet acteur. */
		uint32_t GetEntity() const { return entity_; }

	protected:
		virtual void OnTransformChanged();

		bool input_enabled_=true;

	private:
		//ne met jamais les headers third-party dans les headers du moteur

		void OnScriptsPropertyChanged();

		virtual void ProcessInput(){}

		//backend information, do not modify
		Engine* engine_internal_=nullptr;

		//entt::entity, stocke en entier pour ne pas exposer EnTT
		uint32_t entity_ = 0xFFFFFFFFu;
	};
}
