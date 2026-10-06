#pragma once

// =============================================================================
// Anim Graph (.animgraph) : graphe d'animation fait dans l'editeur, comme Unreal
// -----------------------------------------------------------------------------
// On part de la SORTIE : le noeud final "Output Pose" recoit la pose a jouer.
// On y branche :
//
//   Animation       une planche de sprites (texture, frames) + notifies
//   Blend Space     un parametre float (ex : speed) choisit l'animation
//   State Machine   des etats et des transitions (Entry -> Idle <-> Run...)
//
//   [Locomotion (State Machine)] ----> [Output Pose]
//
// Dans une State Machine : Entry (l'etat de depart), Any State, les etats et
// les transitions (from -> to quand TOUTES leurs conditions sont vraies).
// Chaque etat a sa propre pose : [Animation ou Blend Space] -> [Output Pose].
//
//   Parameters   speed (float), airborne (bool), hurt (trigger)...
//
//   sprite.graph = "anim/Hero.animgraph";     // ou LoadGraph()
//   sprite.SetFloat("speed", 3.f);            // les transitions suivent
//
// Evenements (notifies, entree / sortie d'etat) : la fonction JS du meme nom
// sur l'acteur (methode de sa classe ou d'un script attache), et
// AnimationSpriteComponent::on_graph_event en C++.
// Condition "script" : fonction JS de l'acteur qui retourne true / false.
//
// Les fichiers de la version 1 (etats a la racine + Entry) sont lus comme une
// State Machine "Locomotion" branchee sur Output Pose.
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
		std::string motion;     // pose de l'etat : animation ou blend space -> Output Pose
		bool interruptible = true;
		bool restart_on_enter = true;
		std::string next;       // joue a la fin si aucune transition ("" = entree)
		std::string on_enter;   // evenement (fonction JS)
		std::string on_exit;
		float x = 0.f;          // position dans l'editeur
		float y = 0.f;
		// Graphe de pose de l'etat (editeur) : noeud de la pose, Output Pose.
		float pose_x = 0.f, pose_y = 0.f;
		float output_x = 320.f, output_y = 0.f;
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

	/** Une machine a etats (noeud "State Machine" du graphe). */
	struct LYNX_API AnimGraphStateMachine
	{
		int id = 0;
		std::string name;
		std::vector<AnimGraphState> states;
		std::vector<AnimGraphTransition> transitions;
		std::string entry_state;
		float entry_x = 0.f, entry_y = 0.f;     // noeuds de l'editeur
		float any_x = 0.f, any_y = 220.f;

		AnimGraphState* FindState(const std::string& name);
		const AnimGraphState* FindState(const std::string& name) const;
		/** Un etat change de nom : transitions, entree, next suivent. */
		void RenameState(const std::string& from, const std::string& to);
	};

	/** Noeud du graphe racine qui produit une pose. */
	struct LYNX_API AnimGraphPoseNode
	{
		int id = 0;
		/** "animation", "blendspace", "statemachine" */
		std::string type = "animation";
		std::string source;     // nom de l'animation / du blend space / de la state machine
		float x = 0.f;
		float y = 0.f;
	};

	/** Ce que joue le graphe : la state machine branchee sur Output Pose (ou un seul etat). */
	struct LYNX_API AnimGraphCompiled
	{
		std::vector<AnimGraphState> states;
		std::vector<AnimGraphTransition> transitions;
		std::string entry_state;
		std::string state_machine;   // "" : animation / blend space seul (ou rien)
	};

	struct LYNX_API AnimGraphAsset
	{
		std::vector<AnimGraphParameter> parameters;
		std::vector<AnimGraphAnimation> animations;
		std::vector<AnimGraphBlendSpace> blend_spaces;
		std::vector<AnimGraphStateMachine> state_machines;

		// Graphe racine : noeuds de pose, et celui branche sur Output Pose.
		std::vector<AnimGraphPoseNode> pose_nodes;
		int output_source = 0;                  // id d'un pose_node (0 : rien)
		float output_x = 360.f, output_y = 0.f;

		bool LoadFromString(const std::string& xml, std::string* error = nullptr);
		std::string SaveToString() const;
		/** assets/<path> (aussi depuis l'archive d'un jeu livre). */
		bool LoadFromAsset(const std::string& path, std::string* error = nullptr);

		/** Ce qui est branche sur Output Pose, pret pour AnimationSpriteComponent. */
		AnimGraphCompiled Compile() const;

		const AnimGraphParameter* FindParameter(const std::string& name) const;
		bool IsAnimation(const std::string& name) const;
		bool IsBlendSpace(const std::string& name) const;
		bool IsMotion(const std::string& name) const;
		AnimGraphStateMachine* FindStateMachine(const std::string& name);
		AnimGraphStateMachine* FindStateMachine(int id);
		const AnimGraphStateMachine* FindStateMachine(const std::string& name) const;
		AnimGraphPoseNode* FindPoseNode(int id);
		const AnimGraphPoseNode* FindPoseNode(int id) const;
		/** Le noeud branche sur Output Pose (nullptr : rien). */
		const AnimGraphPoseNode* OutputNode() const;

		/** Id libre (state machines, etats, transitions, noeuds). */
		int NewId() const;

		/** Une animation / un blend space change de nom : blend spaces, etats, noeuds suivent. */
		void RenameMotion(const std::string& from, const std::string& to);
		/** Une state machine change de nom : ses noeuds suivent. */
		void RenameStateMachine(const std::string& from, const std::string& to);
	};
}
