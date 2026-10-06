#pragma once

// =============================================================================
// Anim Graph (.animgraph) : machine a etats d'animation, faite dans l'editeur
// -----------------------------------------------------------------------------
// Version legere de l'AnimGraph d'Unreal, pour AnimationSpriteComponent :
//
//   Parameters   speed (float), airborne (bool), hurt (trigger)...
//   Animations   planches de sprites (texture, frames) + notifies (frame -> evenement)
//   Blend spaces une variable (ex : speed) choisit l'animation (idle / walk / run)
//   States       jouent une animation ou un blend space
//   Transitions  from -> to quand TOUTES leurs conditions sont vraies
//                (any state -> to : depuis n'importe quel etat)
//
//   sprite.graph = "anim/Hero.animgraph";     // ou LoadGraph()
//   sprite.SetFloat("speed", 3.f);            // les transitions suivent
//
// Evenements (notifies, entree / sortie d'etat) : la fonction JS du meme nom
// sur l'acteur (methode de sa classe ou d'un script attache), et
// AnimationSpriteComponent::on_graph_event en C++.
// Condition "script" : fonction JS de l'acteur qui retourne true / false.
// =============================================================================

#include <string>
#include <vector>

#include "../core/Common.h"

namespace lynx
{
	struct LYNX_API AnimGraphParameter
	{
		std::string name;
		/** "float", "bool", "int", "trigger" */
		std::string type = "float";
		float default_value = 0.f;
	};

	struct LYNX_API AnimGraphNotify
	{
		int frame = 1;          // 1 = premiere frame
		std::string event;      // fonction JS appelee sur l'acteur
	};

	struct LYNX_API AnimGraphAnimation
	{
		std::string name;
		std::string texture;    // planche horizontale (assets/)
		int frames = 1;
		float frame_time = 0.1f;
		bool loop = true;
		std::vector<AnimGraphNotify> notifies;
	};

	struct LYNX_API AnimGraphSample
	{
		float position = 0.f;
		std::string animation;
	};

	struct LYNX_API AnimGraphBlendSpace
	{
		std::string name;
		std::string variable;   // parametre float qui choisit l'animation
		std::vector<AnimGraphSample> samples;
	};

	struct LYNX_API AnimGraphState
	{
		int id = 0;
		std::string name;
		std::string motion;     // animation ou blend space
		bool interruptible = true;
		bool restart_on_enter = true;
		std::string next;       // joue a la fin si aucune transition ("" = entree)
		std::string on_enter;   // evenement (fonction JS)
		std::string on_exit;
		float x = 0.f;          // position dans l'editeur
		float y = 0.f;
	};

	struct LYNX_API AnimGraphCondition
	{
		/** "compare", "isTrue", "isFalse", "trigger", "finished", "script" */
		std::string kind = "compare";
		std::string param;
		/** > >= < <= == != */
		std::string op = ">";
		float value = 0.f;
		std::string function;   // "script" : fonction JS de l'acteur -> bool
	};

	struct LYNX_API AnimGraphTransition
	{
		int id = 0;
		std::string from;       // "" : depuis n'importe quel etat (Any State)
		std::string to;
		int priority = 0;
		bool wait_finished = false;
		std::vector<AnimGraphCondition> conditions;   // toutes vraies (aucune : toujours)
	};

	struct LYNX_API AnimGraphAsset
	{
		std::vector<AnimGraphParameter> parameters;
		std::vector<AnimGraphAnimation> animations;
		std::vector<AnimGraphBlendSpace> blend_spaces;
		std::vector<AnimGraphState> states;
		std::vector<AnimGraphTransition> transitions;

		std::string entry_state;
		float entry_x = 0.f, entry_y = 0.f;     // noeuds de l'editeur
		float any_x = 0.f, any_y = 200.f;

		bool LoadFromString(const std::string& xml, std::string* error = nullptr);
		std::string SaveToString() const;
		/** assets/<path> (aussi depuis l'archive d'un jeu livre). */
		bool LoadFromAsset(const std::string& path, std::string* error = nullptr);

		AnimGraphState* FindState(const std::string& name);
		const AnimGraphParameter* FindParameter(const std::string& name) const;
		bool IsMotion(const std::string& name) const;
		/** Id libre pour un etat / une transition. */
		int NewId() const;

		/** Un etat change de nom : transitions, entree, next suivent. */
		void RenameState(const std::string& from, const std::string& to);
	};
}
