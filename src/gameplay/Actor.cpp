#include "Actor.h"
#include "../core/Profiler.h"

#include "../core/Engine.h"
#include "../core/Level.h"
#include "PlayerController.h"
#include "Private/ECS.h"
#include "../scripting/Private/ScriptSystem.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lynx
{
	// Acces moteur aux membres prives / proteges de Component.
	struct ComponentAccess
	{
		static void SetEntity(Component& c, uint32_t e) { c.entity_ = e; }
		static bool& Begun(Component& c) { return c.begun_play_; }
		static bool& PendingDestroy(Component& c) { return c.pending_destroy_; }

		static void OnAttach(Component& c) { c.OnAttach(); }
		static void BeginPlay(Component& c) { c.BeginPlay(); }
		static void Update(Component& c, float dt) { c.Update(dt); }
		static void Tick(Component& c, float dt) { c.Tick(dt); }
		static void LateUpdate(Component& c, float dt) { c.LateUpdate(dt); }
		static void EndPlay(Component& c) { c.EndPlay(); }
	};

	// ========================================================================
	// Registre ECS (EnTT)
	//
	// - une entite par Actor, avec un composant interne ActorRef -> Actor*
	// - un storage EnTT par type de composant, identifie par un hash du nom
	//   du type (storage nomme). Chaque element possede le composant
	//   (unique_ptr) : les adresses restent stables meme si EnTT deplace les
	//   elements, et les composants du jeu (alloues dans la dll du jeu) sont
	//   detruits par leur destructeur virtuel.
	// ========================================================================

	namespace
	{
		struct ActorRef
		{
			Actor* actor = nullptr;
		};

		struct ComponentSlot
		{
			std::unique_ptr<Component> ptr;
		};

		entt::registry& Registry()
		{
			static entt::registry registry;
			return registry;
		}

		using SlotStorage = std::remove_reference_t<decltype(Registry().storage<ComponentSlot>(entt::id_type{}))>;

		struct TypeInfo
		{
			std::string name;
			SlotStorage* storage = nullptr;
		};

		// Types de composants, dans l'ordre de premiere utilisation (ordre des
		// appels BeginPlay/Tick... pour un meme acteur).
		std::vector<uint32_t>& TypeOrder()
		{
			static std::vector<uint32_t> order;
			return order;
		}

		std::unordered_map<uint32_t, TypeInfo>& Types()
		{
			static std::unordered_map<uint32_t, TypeInfo> types;
			return types;
		}

		SlotStorage* FindStorage(uint32_t type_id)
		{
			auto& types = Types();
			auto it = types.find(type_id);
			return it == types.end() ? nullptr : it->second.storage;
		}

		SlotStorage& GetOrCreateStorage(uint32_t type_id, const char* type_name)
		{
			auto& types = Types();
			auto it = types.find(type_id);
			if (it != types.end())
			{
				if (it->second.name != type_name)
				{
					std::cout << "[ecs] component type id collision: " << it->second.name
					          << " / " << type_name << std::endl;
				}
				return *it->second.storage;
			}

			SlotStorage& storage = Registry().storage<ComponentSlot>(static_cast<entt::id_type>(type_id));
			types.emplace(type_id, TypeInfo{type_name, &storage});
			TypeOrder().push_back(type_id);
			return storage;
		}

		entt::entity ToEntity(uint32_t e)
		{
			return static_cast<entt::entity>(e);
		}

		bool IsValid(uint32_t e)
		{
			return e != ecs::kNullEntity && Registry().valid(ToEntity(e));
		}

		// --- Destruction differee --------------------------------------------
		// Pendant un parcours (BeginPlay/Tick...), un composant retire ne peut
		// pas etre detruit tout de suite : on le met de cote jusqu'a la fin.

		int g_iteration_depth = 0;
		bool g_playing = false;

		std::vector<std::unique_ptr<Component>>& Graveyard()
		{
			static std::vector<std::unique_ptr<Component>> graveyard;
			return graveyard;
		}

		struct IterationGuard
		{
			IterationGuard() { ++g_iteration_depth; }
			~IterationGuard()
			{
				if (--g_iteration_depth > 0)
					return;
				// un destructeur peut en retirer d'autres : on boucle
				while (!Graveyard().empty())
				{
					auto dead = std::move(Graveyard());
					Graveyard().clear();
					dead.clear();
				}
			}
		};

		void Bury(std::unique_ptr<Component> c)
		{
			ComponentAccess::PendingDestroy(*c) = true;
			if (g_iteration_depth > 0)
				Graveyard().push_back(std::move(c));
			// sinon detruit ici
		}

		// Retire le composant `type_id` de l'entite et rend sa propriete.
		std::unique_ptr<Component> TakeComponent(uint32_t entity, uint32_t type_id)
		{
			SlotStorage* storage = FindStorage(type_id);
			const entt::entity e = ToEntity(entity);
			if (!storage || !storage->contains(e))
				return nullptr;

			std::unique_ptr<Component> owned = std::move(storage->get(e).ptr);
			storage->erase(e);
			return owned;
		}

		/** Acteurs : ordre du niveau, puis les autres (spawns en attente...). */
		// Bumped when an actor entity is created or destroyed (see the cache
		// of OrderedActors).
		uint64_t g_entity_version = 0;

		/**
		 * Every actor : the level order, then the ones not in the level yet
		 * (spawned this frame). Called by every physics query, collider and
		 * component loop : the list is cached and rebuilt only when it can
		 * have changed (another level, its actor list resized or moved, an
		 * actor entity created or destroyed). Before : a copy and a hash set
		 * of every actor per call (~0.1 ms with 500 actors, x hundreds).
		 */
		std::vector<Actor*> OrderedActors()
		{
			struct Cache
			{
				bool valid = false;
				const Level* level = nullptr;
				const void* data = nullptr;
				size_t size = 0;
				uint64_t version = 0;
				std::vector<Actor*> actors;
			};
			static Cache cache;

			Level* level = Engine::Get() ? Engine::Get()->GetCurrentLevel() : nullptr;
			const std::vector<Actor*>* list = level ? &level->GetActors() : nullptr;
			const void* data = list ? static_cast<const void*>(list->data()) : nullptr;
			const size_t size = list ? list->size() : 0;

			if (cache.valid && cache.level == level && cache.data == data && cache.size == size &&
			    cache.version == g_entity_version)
				return cache.actors;

			std::vector<Actor*>& out = cache.actors;
			out.clear();
			std::unordered_set<Actor*> seen;
			seen.reserve(size + 16);

			if (list)
			{
				for (Actor* a : *list)
				{
					if (a && seen.insert(a).second)
						out.push_back(a);
				}
			}

			for (auto [e, ref] : Registry().view<ActorRef>().each())
			{
				if (ref.actor && seen.insert(ref.actor).second)
					out.push_back(ref.actor);
			}

			cache.valid = true;
			cache.level = level;
			cache.data = data;
			cache.size = size;
			cache.version = g_entity_version;
			return out;
		}

		/** Profiler zone of a component : its C++ type (nullptr : fine zones off). */
		const char* ProfileName(const Component& c)
		{
			return profiler::TypeName(typeid(c));
		}

		/** Tous les composants vivants, acteur par acteur. */
		std::vector<Component*> CollectAllComponents()
		{
			std::vector<Component*> out;
			for (Actor* a : OrderedActors())
			{
				for (Component* c : a->GetComponents())
					out.push_back(c);
			}
			return out;
		}

		void StartComponent(Component& c)
		{
			bool& begun = ComponentAccess::Begun(c);
			if (begun || ComponentAccess::PendingDestroy(c))
				return;
			begun = true;
			ComponentAccess::BeginPlay(c);
		}

		void StopComponent(Component& c)
		{
			bool& begun = ComponentAccess::Begun(c);
			if (!begun)
				return;
			begun = false;
			ComponentAccess::EndPlay(c);
		}
	}


	// ========================================================================
	// Component
	// ========================================================================

	Actor* Component::GetOwner() const
	{
		return ecs::GetActor(entity_);
	}

	namespace detail
	{
		uint32_t HashComponentType(const char* type_name)
		{
			// FNV-1a 32 bits : stable entre le moteur et le jeu
			uint32_t h = 2166136261u;
			for (const char* c = type_name; c && *c; ++c)
			{
				h ^= static_cast<uint8_t>(*c);
				h *= 16777619u;
			}
			return h;
		}
	}


	// ========================================================================
	// ecs::
	// ========================================================================

	namespace ecs
	{
		uint32_t CreateEntity(Actor* owner)
		{
			auto& reg = Registry();
			const entt::entity e = reg.create();
			reg.emplace<ActorRef>(e, owner);
			++g_entity_version;
			return static_cast<uint32_t>(entt::to_integral(e));
		}

		void DestroyEntity(uint32_t entity)
		{
			if (!IsValid(entity))
				return;

			// L'acteur est en cours de destruction : pas d'EndPlay ici (fait
			// avant par Level via OnActorDestroyed), on detruit les composants.
			IterationGuard guard;
			const auto order = TypeOrder();
			for (auto it = order.rbegin(); it != order.rend(); ++it)
			{
				if (auto owned = TakeComponent(entity, *it))
				{
					ComponentAccess::Begun(*owned) = false;
					ComponentAccess::SetEntity(*owned, kNullEntity);
					Bury(std::move(owned));
				}
			}

			Registry().destroy(ToEntity(entity));
			++g_entity_version;
		}

		void RebindEntity(uint32_t entity, Actor* owner)
		{
			if (!IsValid(entity))
				return;
			if (auto* ref = Registry().try_get<ActorRef>(ToEntity(entity)))
				ref->actor = owner;
		}

		Actor* GetActor(uint32_t entity)
		{
			if (!IsValid(entity))
				return nullptr;
			const auto* ref = Registry().try_get<ActorRef>(ToEntity(entity));
			return ref ? ref->actor : nullptr;
		}

		void CollectActorsWith(uint32_t type_id, std::vector<Actor*>& out)
		{
			out.clear();
			SlotStorage* storage = FindStorage(type_id);
			if (!storage || storage->empty())
				return;

			for (Actor* a : OrderedActors())
			{
				if (storage->contains(ToEntity(a->GetEntity())))
					out.push_back(a);
			}
		}

		void BeginPlay()
		{
			g_playing = true;
			IterationGuard guard;
			for (Component* c : CollectAllComponents())
				StartComponent(*c);
		}

		void EndPlay()
		{
			{
				IterationGuard guard;
				for (Component* c : CollectAllComponents())
					StopComponent(*c);
			}
			g_playing = false;
		}

		void Update(float dt)
		{
			IterationGuard guard;
			for (Component* c : CollectAllComponents())
			{
				if (!ComponentAccess::PendingDestroy(*c))
				{
					LYNX_PROFILE_SCOPE_PTR(ProfileName(*c));
					ComponentAccess::Update(*c, dt);
				}
			}
		}

		void LateUpdate(float dt)
		{
			IterationGuard guard;
			for (Component* c : CollectAllComponents())
			{
				if (!ComponentAccess::PendingDestroy(*c))
				{
					LYNX_PROFILE_SCOPE_PTR(ProfileName(*c));
					ComponentAccess::LateUpdate(*c, dt);
				}
			}
		}

		void Tick(float dt)
		{
			g_playing = true;
			IterationGuard guard;
			const std::vector<Component*> components = CollectAllComponents();
			LYNX_PROFILE_COUNTER("Components", components.size());
			for (Component* c : components)
			{
				LYNX_PROFILE_SCOPE_PTR(ProfileName(*c));

				// composant ajoute pendant le jeu : BeginPlay au premier tick
				StartComponent(*c);

				if (!ComponentAccess::PendingDestroy(*c) && c->tick_enabled)
					ComponentAccess::Tick(*c, dt);
			}
		}

		void OnActorDestroyed(Actor* actor)
		{
			if (!actor)
				return;
			IterationGuard guard;
			for (Component* c : actor->GetComponents())
				StopComponent(*c);
		}

		bool IsPlaying()
		{
			return g_playing;
		}
	}


	// ========================================================================
	// Actor
	// ========================================================================

	void Actor::OnTransformChanged()
	{
		ED_transform_modified.Call();
	}


	Actor::Actor()
	{
		entity_ = ecs::CreateEntity(this);

		HPROPERTY(transform, Exposed, OnTransformChanged());
		HPROPERTY(scripts, Exposed, OnScriptsPropertyChanged());
		HPROPERTY(auto_possess_player, Exposed);
		HPROPERTY(outliner_folder, Exposed);
	}

	Actor::~Actor()
	{
		// Normalement deja relache par le niveau (avec OnUnpossessed). Sinon
		// (acteur supprime a la main) le joueur oublie simplement l'acteur.
		if (controller_)
			controller_->ForgetActor();
		controller_ = nullptr;

		// Detruit l'entite et tous ses composants (dont les scripts).
		scripting::OnEntityDestroyed(entity_);
		ecs::DestroyEntity(entity_);
		entity_ = ecs::kNullEntity;
	}

	// Note : on ne copie PAS la table de proprietes d'Object (elle contient des
	// pointeurs vers les membres de `other` et des Observable possedes : la
	// copier menait a un double delete). Les proprietes d'Actor sont
	// re-enregistrees sur les membres de `this`.
	Actor::Actor(Actor&& other)
		: transform(other.transform),
		  scripts(std::move(other.scripts)),
		  auto_possess_player(other.auto_possess_player),
		  ED_transform_modified(std::move(other.ED_transform_modified)),
		  input_enabled_(other.input_enabled_),
		  engine_internal_(other.engine_internal_),
		  entity_(other.entity_),
		  controller_(other.controller_),
		  auto_possess_done_(other.auto_possess_done_),
		  interfaces_(std::move(other.interfaces_)),
		  interface_handlers_(std::move(other.interface_handlers_))
	{
		object_id_ = other.object_id_;
		outliner_folder = other.outliner_folder;
		other.entity_ = ecs::kNullEntity;
		ecs::RebindEntity(entity_, this);

		// Le joueur suit l'acteur deplace.
		other.controller_ = nullptr;
		if (controller_)
			controller_->possessed_actor_ = this;

		HPROPERTY(transform, Exposed, OnTransformChanged());
		HPROPERTY(scripts, Exposed, OnScriptsPropertyChanged());
		HPROPERTY(auto_possess_player, Exposed);
		HPROPERTY(outliner_folder, Exposed);
	}

	Actor& Actor::operator=(Actor&& other)
	{
		if (this == &other)
			return *this;

		// les proprietes de `this` pointent deja sur ses propres membres
		object_id_ = other.object_id_;
		outliner_folder = other.outliner_folder;
		transform = other.transform;
		scripts = std::move(other.scripts);
		auto_possess_player = other.auto_possess_player;
		ED_transform_modified = std::move(other.ED_transform_modified);
		input_enabled_ = other.input_enabled_;
		engine_internal_ = other.engine_internal_;
		auto_possess_done_ = other.auto_possess_done_;
		interfaces_ = std::move(other.interfaces_);
		interface_handlers_ = std::move(other.interface_handlers_);

		// Possession : `this` prend celle de `other` (l'ancienne est relachee).
		if (controller_ && controller_ != other.controller_)
			controller_->Unpossess();
		controller_ = other.controller_;
		other.controller_ = nullptr;
		if (controller_)
			controller_->possessed_actor_ = this;

		// echange : les composants de `this` seront detruits avec `other`
		std::swap(entity_, other.entity_);
		ecs::RebindEntity(entity_, this);
		ecs::RebindEntity(other.entity_, &other);
		return *this;
	}

	void Actor::OnPossessed(PlayerController*)
	{
	}

	void Actor::OnUnpossessed(PlayerController*)
	{
	}

	void Actor::OnBeginOverlap(Actor*)
	{
	}

	void Actor::OnEndOverlap(Actor*)
	{
	}

	void Actor::OnHit(Actor*, const vec3&)
	{
	}


	// ---- Composants --------------------------------------------------------

	Component& Actor::AddComponentInternal(uint32_t type_id, Component* component)
	{
		std::unique_ptr<Component> owned(component);

		if (!IsValid(entity_))
		{
			// acteur deplace / detruit : on garde l'objet en vie pour que la
			// reference retournee reste valide, mais il n'est attache a rien.
			std::cout << "[ecs] AddComponent on an actor without entity" << std::endl;
			static std::vector<std::unique_ptr<Component>> orphans;
			orphans.push_back(std::move(owned));
			return *orphans.back();
		}

		if (Component* existing = GetComponentInternal(type_id))
		{
			// deja present (AddComponentInternal appele directement)
			return *existing;
		}

		SlotStorage& storage = GetOrCreateStorage(type_id, typeid(*component).name());
		storage.emplace(ToEntity(entity_), ComponentSlot{std::move(owned)});

		ComponentAccess::SetEntity(*component, entity_);
		ComponentAccess::OnAttach(*component);
		// BeginPlay : au prochain ecs::Tick si le jeu tourne, sinon au lancement.
		return *component;
	}

	Component* Actor::GetComponentInternal(uint32_t type_id) const
	{
		if (!IsValid(entity_))
			return nullptr;
		SlotStorage* storage = FindStorage(type_id);
		const entt::entity e = ToEntity(entity_);
		if (!storage || !storage->contains(e))
			return nullptr;
		return storage->get(e).ptr.get();
	}

	void Actor::RemoveComponentInternal(uint32_t type_id)
	{
		std::unique_ptr<Component> owned = TakeComponent(entity_, type_id);
		if (!owned)
			return;

		{
			IterationGuard guard; // EndPlay peut retirer d'autres composants
			StopComponent(*owned);
			Bury(std::move(owned));
		}

		if (type_id == detail::ComponentTypeId<ScriptComponent>())
			scripts.clear();
	}

	std::vector<Component*> Actor::GetComponents() const
	{
		std::vector<Component*> out;
		if (!IsValid(entity_))
			return out;

		const entt::entity e = ToEntity(entity_);
		for (uint32_t type_id : TypeOrder())
		{
			SlotStorage* storage = FindStorage(type_id);
			if (storage && storage->contains(e))
				out.push_back(storage->get(e).ptr.get());
		}
		return out;
	}


	// ---- Scripts -----------------------------------------------------------

	bool Actor::AddScript(const char* path)
	{
		if (!path || !*path)
			return false;
		return AddComponent<ScriptComponent>().AddScript(path);
	}

	void Actor::RemoveScript(const char* path)
	{
		if (auto* sc = GetComponent<ScriptComponent>())
			sc->RemoveScript(path ? path : "");
	}

	void Actor::OnScriptsPropertyChanged()
	{
		// Decoupe "a.js;b.js" en liste
		std::vector<std::string> wanted;
		size_t start = 0;
		while (start <= scripts.size())
		{
			size_t end = scripts.find(';', start);
			if (end == std::string::npos)
				end = scripts.size();

			std::string path = scripts.substr(start, end - start);
			// trim
			path.erase(0, path.find_first_not_of(" \t"));
			path.erase(path.find_last_not_of(" \t") + 1);
			if (!path.empty() && std::find(wanted.begin(), wanted.end(), path) == wanted.end())
				wanted.push_back(path);

			start = end + 1;
		}

		auto* sc = GetComponent<ScriptComponent>();
		if (!sc && wanted.empty())
			return;
		if (!sc)
			sc = &AddComponent<ScriptComponent>();

		// Retire ce qui n'est plus dans la liste, ajoute ce qui manque.
		for (const auto& current : sc->GetScripts())
		{
			if (std::find(wanted.begin(), wanted.end(), current) == wanted.end())
				sc->RemoveScript(current);
		}
		for (const auto& path : wanted)
		{
			if (!sc->HasScript(path))
				sc->AddScript(path);
		}
	}
}
