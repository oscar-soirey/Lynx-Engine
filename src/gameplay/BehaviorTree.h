#pragma once

// =============================================================================
// Behavior Tree (.bt) + Blackboard : IA faite dans l'editeur (type Unreal)
// -----------------------------------------------------------------------------
//   Root
//    └ Selector                      essaie ses enfants (de haut en bas) jusqu'a un succes
//       ├ [Blackboard: target isSet, abort lowerPriority]
//       │ Sequence                   enfants dans l'ordre jusqu'a un echec
//       │  ├ MoveTo target
//       │  └ Script Attack           tache ecrite en JS (class Attack extends BTTask)
//       └ Wait 2                     sinon : attendre
//
// - Taches : Wait, MoveTo, SetValue, ClearValue, SetAnimParam, Log, CallFunction
//   (fonction JS de l'acteur), Script (classe JS BTTask), Finish.
// - Decorateurs (conditions / modificateurs d'un noeud) : Blackboard, CallFunction,
//   Script (classe JS BTDecorator), Cooldown, Loop, TimeLimit, ForceSuccess.
//   "abort" (observer aborts d'Unreal) : self = le noeud s'arrete quand sa
//   condition devient fausse ; lowerPriority = il interrompt ce qui tourne plus
//   bas quand elle devient vraie ; both.
// - Services (tournent tant que leur noeud est actif, a intervalle) : CallFunction,
//   Script (classe JS BTService), DistanceTo, FindNearest.
//
// L'ordre des enfants est leur position verticale dans l'editeur (haut d'abord).
// Composant "AI" : actor->AddComponent<lynx::BehaviorTreeComponent>().behavior_tree = "ai/Guard.bt";
// =============================================================================

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Component.h"
#include "../core/Common.h"

namespace lynx
{
	class Actor;

	// =========================================================================
	// Blackboard : memoire de l'IA (valeurs nommees)
	// =========================================================================

	struct LYNX_API BlackboardValue
	{
		enum class Type { None, Bool, Int, Float, String, Vector, Actor };

		Type type = Type::None;
		bool b = false;
		int i = 0;
		float f = 0.f;
		std::string s;
		vec3 v{ 0.f };
		uint32_t entity = 0xFFFFFFFFu;    // Actor : id de l'entite

		/** Nombre (bool / int / float), 0 sinon. */
		float AsNumber() const;
		std::string ToString() const;
	};

	class LYNX_API Blackboard
	{
	public:
		void SetBool(const std::string& key, bool value);
		void SetInt(const std::string& key, int value);
		void SetFloat(const std::string& key, float value);
		void SetString(const std::string& key, const std::string& value);
		void SetVector(const std::string& key, const vec3& value);
		void SetActor(const std::string& key, Actor* actor);   // nullptr : Clear
		void Set(const std::string& key, const BlackboardValue& value);

		/**
		 * Valeur ecrite en texte ("3", "true", "1 2 0", "hello") : convertie
		 * selon le type declare de la cle (sinon devine).
		 */
		void SetFromText(const std::string& key, const std::string& text);

		void Clear(const std::string& key);
		void ClearAll();

		/** Une valeur est presente (un acteur : encore vivant). */
		bool IsSet(const std::string& key) const;

		/** nullptr si absente. */
		const BlackboardValue* Get(const std::string& key) const;

		bool GetBool(const std::string& key, bool fallback = false) const;
		int GetInt(const std::string& key, int fallback = 0) const;
		float GetFloat(const std::string& key, float fallback = 0.f) const;
		std::string GetString(const std::string& key) const;
		/** Vecteur, ou position de l'acteur d'une cle Actor. */
		bool GetVector(const std::string& key, vec3& out) const;
		/** nullptr si absent ou detruit. */
		Actor* GetActor(const std::string& key) const;

		/** Type d'une cle declaree dans l'editeur ("float", "actor"...). "" sinon. */
		void DeclareKey(const std::string& key, const std::string& type);
		std::string GetDeclaredType(const std::string& key) const;

		std::vector<std::string> GetKeys() const;

	private:
		std::unordered_map<std::string, BlackboardValue> values_;
		std::unordered_map<std::string, std::string> declared_;
	};


	// =========================================================================
	// Asset (.bt) : noeuds a plat, chaque noeud connait son parent
	// =========================================================================

	struct LYNX_API BTParam
	{
		std::string name;
		std::string value;
	};

	/** Decorateur ou service, attache a un noeud. */
	struct LYNX_API BTAux
	{
		int id = 0;
		std::string type;
		std::vector<BTParam> params;

		std::string Get(const std::string& name, const std::string& fallback = "") const;
		void Set(const std::string& name, const std::string& value);
	};

	struct LYNX_API BTNodeDesc
	{
		int id = 0;
		int parent = 0;         // 0 : non relie (l'editeur le garde, le jeu l'ignore)
		std::string type;       // "Root", "Selector", "Wait"...
		std::string name;       // titre libre (vide : le type)
		float x = 0.f;          // position dans l'editeur (y : ordre des enfants)
		float y = 0.f;
		std::vector<BTParam> params;
		std::vector<BTAux> decorators;
		std::vector<BTAux> services;

		std::string Get(const std::string& name, const std::string& fallback = "") const;
		void Set(const std::string& name, const std::string& value);
	};

	struct LYNX_API BTKeyDesc
	{
		std::string name;
		/** "bool", "int", "float", "string", "vector", "actor" */
		std::string type = "float";
		std::string default_value;
	};

	struct LYNX_API BehaviorTreeAsset
	{
		std::vector<BTKeyDesc> keys;
		std::vector<BTNodeDesc> nodes;    // nodes[?].type == "Root" : la racine

		bool LoadFromString(const std::string& xml, std::string* error = nullptr);
		std::string SaveToString() const;
		bool LoadFromAsset(const std::string& path, std::string* error = nullptr);

		BTNodeDesc* Find(int id);
		const BTNodeDesc* Find(int id) const;
		BTNodeDesc* Root();
		const BTNodeDesc* Root() const;

		/** Enfants de `id`, dans l'ordre d'execution (haut en bas, puis gauche a droite). */
		std::vector<const BTNodeDesc*> ChildrenOf(int id) const;

		/** Id libre (noeuds, decorateurs, services). */
		int NewId() const;

		/** Un arbre avec une racine (fichier neuf). */
		static BehaviorTreeAsset MakeDefault();
	};


	// =========================================================================
	// Types de noeuds (editeur : palette, Details ; jeu : execution)
	// =========================================================================

	struct LYNX_API BTParamDef
	{
		const char* name;
		/** "float", "int", "bool", "string", "key", "enum", "class" */
		const char* type;
		const char* default_value;
		const char* options;     // enum : "a|b|c"
		const char* help;
	};

	struct LYNX_API BTNodeTypeDef
	{
		const char* type;
		/** "root", "composite", "task", "decorator", "service" */
		const char* category;
		const char* description;
		std::vector<BTParamDef> params;
	};

	LYNX_API const std::vector<BTNodeTypeDef>& GetBTNodeTypes();
	/** Noeud (racine, composite, tache) de ce type. */
	LYNX_API const BTNodeTypeDef* FindBTNodeType(const std::string& type);
	/** Type d'une categorie ("decorator", "service"...) : "Script" existe dans trois. */
	LYNX_API const BTNodeTypeDef* FindBTNodeType(const std::string& type, const char* category);


	// =========================================================================
	// Composant "AI" : fait tourner un Behavior Tree sur son acteur
	// =========================================================================

	class LYNX_API BehaviorTreeComponent : public Component
	{
	public:
		/** Asset .bt (assets/). Le changer recharge l'arbre. */
		std::string behavior_tree;
		/** Demarre au lancement du jeu. */
		bool start_on_begin = true;

		BehaviorTreeComponent();
		~BehaviorTreeComponent() override;

		/** Charge l'arbre (et declare les cles du Blackboard avec leurs valeurs par defaut). */
		bool Load(const std::string& asset);
		void Start();
		/** Arrete (les taches en cours recoivent Abort). */
		void Stop();
		void Restart();
		bool IsRunning() const;

		Blackboard& GetBlackboard();
		const Blackboard& GetBlackboard() const;

		/** Noeuds actifs (racine -> tache en cours) : ids de l'asset, noms. */
		std::vector<int> GetActiveNodeIds() const;
		std::vector<std::string> GetActiveNodeNames() const;

		const std::string& GetLoadedTree() const;

		/** Composants qui font tourner `asset` (debug de l'editeur). */
		static std::vector<BehaviorTreeComponent*> GetRunning(const std::string& asset);

		struct Impl;

	protected:
		void BeginPlay() override;
		void Update(float dt) override;
		void Tick(float dt) override;
		void EndPlay() override;

	private:
		std::unique_ptr<Impl> impl_;
	};
}
