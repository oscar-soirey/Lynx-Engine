#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Object.h"
#include "Component.h"
#include "Components.h"
#include "Interface.h"
#include "../core/data/EventDispatcher.h"

namespace lynx
{
	class Engine;
	class PlayerController;

	class LYNX_API Actor : public Object {
		friend class Level;
		friend class Engine;
		friend class PlayerController;
	public:
		transform transform{};

		/**
		 * Scripts attaches, separes par ';' (ex: "scripts/Player.js;scripts/Blink.js").
		 * Propriete reflechie : chargee/sauvegardee avec le niveau. La modifier
		 * ajoute/retire les scripts correspondants (voir ScriptComponent).
		 */
		std::string scripts;

		/**
		 * Auto possess (comme AutoPossessPlayer d'Unreal) : index du joueur
		 * (0 = joueur par defaut) qui possede cet acteur au lancement du jeu,
		 * ou des son spawn pendant le jeu. -1 = desactive.
		 * Propriete reflechie (panneau Details, sauvegardee avec le niveau).
		 */
		int auto_possess_player = -1;

		/**
		 * Folder of the actor in the Outliner of the editor ("Lights/Street",
		 * "" = root). Saved with the level ; no effect in the game.
		 */
		std::string outliner_folder;


		Actor();
		~Actor() override;


		//actor class is not copiable
		Actor(const Actor&) = delete;
		Actor& operator=(const Actor&) = delete;
		// Le deplacement transfere l'entite ECS (et donc les composants).
		Actor(Actor&&);
		Actor& operator=(Actor&&);

		HEventDispatcher<> ED_transform_modified;

		// ====================================================================
		// Possession (voir PlayerController.h)
		// ====================================================================

		/** Appele quand un joueur prend le controle de cet acteur. */
		virtual void OnPossessed(PlayerController* pc);
		/** Appele quand le joueur le relache (Unpossess, autre acteur, fin du jeu...). */
		virtual void OnUnpossessed(PlayerController* pc);

		// ====================================================================
		// Collisions (ColliderComponent, voir BoxColliderComponent.h)
		// ====================================================================

		/** Un collider de cet acteur commence a chevaucher celui de `other`. */
		virtual void OnBeginOverlap(Actor* other);
		/** ... ne le chevauche plus (`other` peut etre en train d'etre detruit). */
		virtual void OnEndOverlap(Actor* other);
		/**
		 * Contact bloquant (l'un des deux avance contre l'autre). `other` =
		 * nullptr : un voxel. `normal` : direction qui repousse cet acteur.
		 */
		virtual void OnHit(Actor* other, const vec3& normal);

		// ====================================================================
		// Interfaces (voir Interface.h)
		// ====================================================================

		/** Cet acteur implemente l'interface `name` (C++). */
		void ImplementInterface(const std::string& name);

		/**
		 * Implementation C++ d'une fonction d'interface (implemente aussi
		 * l'interface). Sans handler, une HFUNCTION du meme nom est appelee.
		 */
		void BindInterfaceFunction(const std::string& name, const std::string& function, InterfaceHandler handler);

		/** C++, classe JS ou script attache (heritage d'interfaces compris). */
		bool Implements(const std::string& name) const;

		/** Interfaces declarees en C++ par cet acteur. */
		const std::vector<std::string>& GetNativeInterfaces() const { return interfaces_; }

		/** lynx::interfaces::Call(this, ...) */
		bool CallInterface(const std::string& name, const std::string& function,
		                   const InterfaceArgs& args = {}, InterfaceArg* result = nullptr);

		/** Handler C++ de `function` (nullptr si aucun). Moteur. */
		const InterfaceHandler* FindInterfaceHandler(const std::string& name, const std::string& function) const;

		/** Joueur qui possede cet acteur (nullptr = aucun). */
		PlayerController* GetController() const { return controller_; }
		bool IsPossessed() const { return controller_ != nullptr; }


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

		// Appele chaque frame de jeu par le PlayerController qui possede
		// l'acteur (avant Tick), si input_enabled_. Un acteur non possede ne
		// le recoit pas : voir auto_possess_player / PlayerController::Possess.
		virtual void ProcessInput(){}

		//backend information, do not modify
		Engine* engine_internal_=nullptr;

		//entt::entity, stocke en entier pour ne pas exposer EnTT
		uint32_t entity_ = 0xFFFFFFFFu;

		// Possession : ecrit par PlayerController uniquement.
		PlayerController* controller_ = nullptr;

		// auto_possess_player deja traite pour cette partie (Engine).
		bool auto_possess_done_ = false;

		// Interfaces C++ (ImplementInterface) et handlers ("Interface.Fonction").
		std::vector<std::string> interfaces_;
		std::vector<std::pair<std::string, InterfaceHandler>> interface_handlers_;
	};
}
