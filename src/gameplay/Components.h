#pragma once

/**
 * Composants fournis par le moteur. Tous heritent de lynx::Component
 * (voir Component.h pour creer les votres, dans le moteur ou dans le jeu).
 *
 * Ce header ne doit JAMAIS inclure de header third-party (entt, quickjs...).
 *
 *     auto& vel = actor->AddComponent<lynx::VelocityComponent>();
 *     vel.linear = {0, 0, 5};
 *
 *     if (auto* tags = actor->GetComponent<lynx::TagsComponent>())
 *         tags->Add("enemy");
 *
 *     actor->AddScript("scripts/Player.js");   // raccourci vers ScriptComponent
 */

#include <string>
#include <vector>

#include "Component.h"

namespace lynx
{
	/**
	 * Liste de tags texte ("enemy", "pickup"...). Permet de retrouver des
	 * acteurs sans connaitre leur classe (JS : Level.findWithTag).
	 */
	struct LYNX_API TagsComponent : Component
	{
		std::vector<std::string> tags;

		bool Has(const std::string& tag) const;
		//ne fait rien si le tag est deja present
		void Add(const std::string& tag);
		void Remove(const std::string& tag);
	};

	/**
	 * Mouvement simple, applique a chaque tick de jeu :
	 *     transform.location += linear  * dt
	 *     transform.rotation += angular * dt
	 * damping : freinage par seconde (0 = aucun, 1 = perd ~100% en 1s).
	 */
	struct LYNX_API VelocityComponent : Component
	{
		vec3 linear{0.f};
		vec3 angular{0.f};
		float damping = 0.f;

	protected:
		void Tick(float dt) override;
	};

	/**
	 * Detruit automatiquement l'acteur quand `remaining` (en secondes de jeu)
	 * arrive a 0. Pratique pour les projectiles, particules, etc.
	 */
	struct LYNX_API LifetimeComponent : Component
	{
		float remaining = 0.f;

	protected:
		void Tick(float dt) override;

	private:
		bool destroy_requested_ = false;
	};

	/**
	 * Scripts JavaScript (QuickJS) attaches a un acteur.
	 *
	 * Modele "leger" : un fichier de script n'est pas une classe, c'est un
	 * comportement. Chaque script attache recoit une variable `parent` (l'acteur)
	 * et le moteur appelle ses fonctions si elles existent :
	 *     BeginPlay()      au lancement du jeu (ou au tick suivant si ajoute en jeu)
	 *     Update(dt)       a chaque tick de jeu
	 *     EndPlay()        arret du jeu, retrait du script ou destruction de l'acteur
	 *
	 * Le meme fichier peut etre attache a plusieurs acteurs : chaque attache a
	 * son propre `parent` et ses propres variables (le fichier est compile une
	 * seule fois puis instancie).
	 *
	 * La liste des scripts est aussi exposee comme propriete "scripts" de
	 * l'Actor ("a.js;b.js"), donc chargee / sauvegardee avec les niveaux.
	 */
	struct LYNX_API ScriptComponent : Component
	{
		ScriptComponent();
		~ScriptComponent() override;

		/**
		 * Charge et instancie un script (chemin dans les assets, ex: "scripts/Player.js").
		 * @return false si le fichier est introuvable ou contient une erreur
		 * (il reste quand meme dans la liste, et sera reessaye par Reload()).
		 */
		bool AddScript(const std::string& path);
		void RemoveScript(const std::string& path);
		bool HasScript(const std::string& path) const;
		std::vector<std::string> GetScripts() const;

		/**
		 * Appelle une fonction du script (dans tous les scripts de l'acteur
		 * qui la definissent). Les arguments sont des nombres.
		 * @return true si au moins un script definissait la fonction.
		 */
		bool CallFunction(const char* name, const std::vector<double>& args = {});

		/** Recharge les fichiers depuis le disque (hot reload). */
		void Reload();

		struct Impl;

	protected:
		void OnAttach() override;
		void BeginPlay() override;
		void Tick(float dt) override;
		void EndPlay() override;

	private:
		friend struct ScriptAccess;

		Impl* impl_ = nullptr;
	};
}
