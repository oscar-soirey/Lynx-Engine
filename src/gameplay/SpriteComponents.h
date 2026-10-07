#pragma once

/**
 * Sprites : un quad texture qui suit l'acteur.
 *
 *     auto& s = actor->AddComponent<lynx::StaticSpriteComponent>();
 *     s.texture = "hero.png";
 *     s.size = {2.f, 3.f};
 *
 *     auto& a = actor->AddComponent<lynx::AnimationSpriteComponent>();
 *     a.SetAnimation("hero_run.png", 8, 0.1f);          // une seule animation
 *
 *     // ou pilote par une machine a etats (AnimationSystem.h) :
 *     a.AddAnimation("idle", "hero_idle.png", 4);
 *     a.AddAnimation("run", "hero_run.png", 8, true, 0.08f);
 *     a.AddState("idle", "idle");
 *     a.AddState("run", "run");
 *     a.SetDefaultState("idle");
 *     a.GetAnimManager().add_transition("idle", "run", a.GetAnimManager().greater("speed", 0.1f));
 *     a.GetAnimManager().add_transition("run", "idle", a.GetAnimManager().less("speed", 0.1f));
 *     a.SetFloat("speed", 3.f);
 *
 * Les champs publics s'appliquent a la frame suivante (Update), ou tout de
 * suite avec Refresh(). Aucun header HRL ici : les ids HRL sont des uint32_t.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Component.h"
#include "AnimationSystem.h"

namespace lynx
{
	/**
	 * Base commune des sprites (quad HRL "mesh sprite").
	 * Le quad est centre sur la position de l'acteur + offset, et suit sa scale
	 * (une scale X negative retourne le sprite, comme flip_x).
	 */
	class LYNX_API SpriteComponent : public Component
	{
	public:
		SpriteComponent();
		~SpriteComponent() override;

		/** Taille du quad (unites monde, avant la scale de l'acteur). */
		vec2 size{1.f, 1.f};

		/** Decalage par rapport a l'acteur (suit sa scale et son retournement). */
		vec3 offset{0.f};

		bool visible = true;
		bool flip_x = false;
		bool flip_y = false;

		/** Applique tout de suite les champs (sinon : a la prochaine frame). */
		void Refresh();

		/** Id HRL du mesh (usage avance : materiau, region...). */
		uint32_t GetMeshId() const { return mesh_; }

	protected:
		void OnAttach() override;
		void Update(float dt) override;
		void LateUpdate(float dt) override;

		/** Materiau sprite de la texture (cache par chemin). 0xFFFFFFFF si introuvable. */
		static uint32_t MaterialForTexture(const std::string& texture);

		bool EnsureMesh();
		void SyncTransform(bool force);

		uint32_t mesh_ = 0xFFFFFFFFu;

	private:
		// Derniere valeur envoyee a HRL (evite les appels inutiles).
		float last_[8] = {};
		bool has_last_ = false;
	};


	/** Sprite avec une texture fixe (ou une region d'une planche). */
	class LYNX_API StaticSpriteComponent : public SpriteComponent
	{
	public:
		/** Chemin dans les assets. */
		std::string texture;

		/** Region de la texture : u0, v0, u1, v1 (0..1). */
		vec4 region{0.f, 0.f, 1.f, 1.f};

	protected:
		void Update(float dt) override;

	private:
		std::string applied_texture_;
		vec4 applied_region_{-1.f};
	};


	/**
	 * Sprite anime (planche horizontale : frame_count images cote a cote).
	 *
	 * Deux modes :
	 *  - Single       : une animation (texture, frame_count, frame_time, loop),
	 *                   controlee par Play / Pause / Stop / Restart.
	 *  - StateMachine : plusieurs animations (AddAnimation, AddBlendSpace),
	 *                   choisies par un anim_manager (etats, transitions,
	 *                   parametres) : voir AnimationSystem.h.
	 *
	 * Les animations avancent pendant le jeu (Tick), au rythme du temps de jeu.
	 */
	class LYNX_API AnimationSpriteComponent : public SpriteComponent
	{
	public:
		enum class Mode
		{
			Single,
			StateMachine
		};

		AnimationSpriteComponent();
		~AnimationSpriteComponent() override;

		Mode mode = Mode::Single;

		// ---------------------------------------------------------------
		// Mode Single (champs modifiables a tout moment)
		// ---------------------------------------------------------------

		std::string texture;
		int frame_count = 1;
		float frame_time = 0.1f;
		bool loop = true;

		/** Faux : l'animation ne demarre pas seule (appeler Play()). */
		bool auto_play = true;

		/** Configure l'animation et passe en mode Single. */
		void SetAnimation(const std::string& texture, int frame_count, float frame_time = 0.1f, bool loop = true);

		void Play();
		void Pause();
		/** Pause + retour a la premiere frame. */
		void Stop();
		void Restart();

		bool IsPlaying() const { return playing_; }
		bool IsFinished() const;

		/** Frame courante, a partir de 1 (0 si aucune animation). */
		int GetFrame() const;

		/** Evenement sur une frame (1 = premiere) de l'animation Single. */
		void AddFrameEvent(int frame, std::function<void()> callback);
		void ClearFrameEvents();

		/** Fin d'une animation Single sans boucle. */
		void OnFinished(std::function<void()> callback);

		// ---------------------------------------------------------------
		// Mode StateMachine (passe en StateMachine automatiquement)
		// ---------------------------------------------------------------

		/** Ajoute (ou remplace) une animation nommee. */
		animation& AddAnimation(const std::string& name, const std::string& texture,
		                        int frame_count, bool loop = true, float frame_time = 0.1f);

		/** Blend space nomme : AddBlendSample(name, position, animation). */
		blend_space& AddBlendSpace(const std::string& name);
		bool AddBlendSample(const std::string& blend_space, float position, const std::string& animation);

		animation* GetAnimation(const std::string& name);
		blend_space* GetBlendSpace(const std::string& name);

		/** Etat qui joue une animation ou un blend space (par son nom). */
		bool AddState(const std::string& state, const std::string& animation_or_blend_space,
		              anim_state_options options = {});

		void SetDefaultState(const std::string& state);

		/** Machine a etats complete (transitions, conditions, parametres...). */
		anim_manager& GetAnimManager() { return manager_; }

		// Raccourcis vers les parametres de la machine a etats
		void SetFloat(const std::string& name, float value) { manager_.set_float(name, value); }
		void SetBool(const std::string& name, bool value) { manager_.set_bool(name, value); }
		void SetInt(const std::string& name, int value) { manager_.set_int(name, value); }
		void SetTrigger(const std::string& name) { manager_.set_trigger(name); }
		float GetFloat(const std::string& name) const { return manager_.get_float(name); }
		bool GetBool(const std::string& name) const { return manager_.get_bool(name); }

		void ForceState(const std::string& state) { manager_.force_state(state); }
		const std::string& GetCurrentState() const { return manager_.current_state(); }

		// ---------------------------------------------------------------
		// Anim Graph (.animgraph fait dans l'editeur, voir AnimGraph.h)
		// ---------------------------------------------------------------

		/** Asset du graphe ("" = aucun). Le changer recharge le graphe. */
		std::string graph;

		/**
		 * Remplace la machine a etats par celle du graphe (parametres,
		 * animations, blend spaces, etats, transitions). false si le
		 * fichier est illisible (la machine a etats est alors vide).
		 */
		bool LoadGraph(const std::string& asset);

		/** Vide la machine a etats (animations, blend spaces, etats, transitions). */
		void ClearStateMachine();

		/** Graphe charge ("" = aucun). */
		const std::string& GetLoadedGraph() const { return loaded_graph_; }

		/** Composants (niveau courant) qui jouent le graphe `asset` : debug de l'editeur. */
		static std::vector<AnimationSpriteComponent*> GetWithGraph(const std::string& asset);

		/**
		 * Evenements du graphe (notifies, entree / sortie d'etat), en plus de
		 * la fonction JS du meme nom sur l'acteur.
		 */
		std::function<void(const std::string& event)> on_graph_event;

	protected:
		void OnAttach() override;
		void BeginPlay() override;
		void Update(float dt) override;
		void Tick(float dt) override;

	private:
		void RebuildSingle();
		void ApplyMode();

		// Single
		std::unique_ptr<animation> single_;
		std::string built_texture_;
		int built_frames_ = 0;
		bool playing_ = false;
		bool started_ = false;

		struct FrameEvent
		{
			int frame;
			std::function<void()> callback;
		};
		std::vector<FrameEvent> single_events_;
		std::function<void()> single_finished_;

		void FireGraphEvent(const std::string& event);
		std::string loaded_graph_;

		// StateMachine (unique_ptr : adresses stables pour l'anim_manager)
		std::map<std::string, std::unique_ptr<animation>> animations_;
		std::map<std::string, std::unique_ptr<blend_space>> blend_spaces_;
		anim_manager manager_;

		Mode applied_mode_ = Mode::Single;
	};
}
