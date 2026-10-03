#pragma once

#include "../core/Common.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lynx
{
	class Actor;

	// ---------------------------------------------------------------------
	// ai_state_options
	// ---------------------------------------------------------------------
	struct LYNX_API ai_state_options
	{
		// Appelé une fois à l'entrée de l'état.
		std::function<void()> on_enter;

		// Appelé une fois à la sortie de l'état.
		std::function<void()> on_exit;

		// Appelé à chaque Tick tant que l'état est actif (dt en secondes).
		std::function<void(float)> on_update;

		// Remet tous les axes à 0 quand on quitte l'état
		// (évite qu'un personnage continue à avancer après un changement d'état).
		bool stop_axes_on_exit = true;
	};

	// ---------------------------------------------------------------------
	// AIBehavior
	//
	// Machine à états pilotant un Actor, sur le même principe que anim_manager.
	//
	// - Actions    : std::function<void()> enregistrées par nom
	//                (ex : "jump", "attack").
	// - Axes       : std::function<void(float)> enregistrées par nom
	//                (ex : "move_x" : -1 = gauche, +1 = droite).
	//                Une valeur d'axe est maintenue : le callback est appelé à
	//                chaque Tick tant que la valeur != 0, et une dernière fois
	//                quand elle repasse à 0.
	// - Paramètres : bool / int / float nommés + triggers (valables 1 Tick).
	// - États      : nom + callbacks (on_enter / on_update / on_exit).
	// - Transitions: from -> to (ou "n'importe quel état"), condition, priorité.
	//
	//   ai.register_action("jump", [&] { player.jump(); });
	//   ai.register_axis("move_x", [&](float v) { player.move_horizontal(v); });
	//
	//   ai.add_state("patrol", {
	//     .on_enter  = [&] { ai.set_axis("move_x", 1.0f); },
	//   });
	//   ai.add_state("chase", {
	//     .on_update = [&](float) { ai.set_axis("move_x", dir_to_target()); },
	//   });
	//   ai.add_transition("patrol", "chase", ai.is_true("sees_player"));
	//   ai.add_transition("chase", "patrol", ai.is_false("sees_player"));
	//   ai.set_default_state("patrol");
	// ---------------------------------------------------------------------
	class LYNX_API AIBehavior {
	public:
		using action      = std::function<void()>;
		using axis_action = std::function<void(float)>;
		using condition   = std::function<bool(const AIBehavior&)>;

		explicit AIBehavior(Actor* parent);

		Actor* get_actor() const { return parent_; }

		// ---------- Actions ----------

		// Enregistre (ou remplace) une action.
		void register_action(const std::string& name, action fn);

		bool has_action(const std::string& name) const { return actions_.count(name) > 0; }

		// Exécute l'action immédiatement. Retourne false si elle n'existe pas.
		bool do_action(const std::string& name);

		// ---------- Axes ----------

		// Enregistre (ou remplace) un axe. La valeur courante est conservée.
		void register_axis(const std::string& name, axis_action fn);

		bool has_axis(const std::string& name) const { return axes_.count(name) > 0; }

		// Définit la valeur de l'axe (maintenue jusqu'au prochain set_axis).
		// Ignoré si l'axe n'existe pas.
		void set_axis(const std::string& name, float value);

		void stop_axis(const std::string& name) { set_axis(name, 0.0f); }
		void stop_all_axes();

		float get_axis(const std::string& name) const;

		// ---------- Paramètres ----------

		void set_bool(const std::string& name, bool v)   { params_[name] = v ? 1.0f : 0.0f; }
		void set_int(const std::string& name, int v)     { params_[name] = static_cast<float>(v); }
		void set_float(const std::string& name, float v) { params_[name] = v; }

		// Un trigger reste actif jusqu'à la fin du prochain Tick(), puis est effacé.
		void set_trigger(const std::string& name) { triggers_.insert(name); }

		float get_float(const std::string& name) const;
		bool  get_bool(const std::string& name) const { return get_float(name) != 0.0f; }
		int   get_int(const std::string& name) const  { return static_cast<int>(get_float(name)); }
		bool  has_trigger(const std::string& name) const { return triggers_.count(name) > 0; }

		// ---------- Conditions prêtes à l'emploi ----------
		// À utiliser sur l'instance :
		//   ai.add_transition("a", "b", ai.triggered("go"));

		condition is_true(std::string name);
		condition is_false(std::string name);
		condition greater(std::string name, float v);
		condition less(std::string name, float v);
		condition triggered(std::string name);

		// Vrai quand l'état courant est actif depuis plus de `seconds` secondes.
		condition state_time_greater(float seconds);

		// ---------- États ----------

		void add_state(const std::string& name, ai_state_options opts = {});

		// État joué au démarrage.
		void set_default_state(const std::string& name) { default_state_ = name; }

		// ---------- Transitions ----------

		// from -> to si cond est vraie. Priorité la plus haute gagne
		// (à égalité : la première déclarée).
		void add_transition(
			const std::string& from,
			const std::string& to,
			condition cond,
			int priority = 0
		);

		// Depuis n'importe quel état (ex : "flee", "dead").
		void add_any_transition(const std::string& to, condition cond, int priority = 0);

		// Force un état immédiatement (ignore les règles).
		void force_state(const std::string& name) { switch_to(name); }

		const std::string& current_state() const { return current_name_; }

		// Temps passé dans l'état courant (secondes).
		float state_time() const { return state_time_; }

		// ---------- Update ----------

		void Tick(float dt);

	private:

		struct state
		{
			ai_state_options opts;
		};

		struct transition
		{
			std::string from; // vide = n'importe quel état
			std::string to;
			condition cond;
			int priority;
		};

		struct axis
		{
			axis_action callback;
			float value = 0.0f;
			bool dirty = false; // valeur changée depuis le dernier appel
		};

		void switch_to(const std::string& name);
		void apply_axes();

		Actor* parent_ = nullptr;

		std::unordered_map<std::string, action> actions_;
		std::unordered_map<std::string, axis> axes_;

		std::unordered_map<std::string, state> states_;
		std::vector<transition> transitions_;

		std::unordered_map<std::string, float> params_;
		std::unordered_set<std::string> triggers_;

		// Pointe dans states_ (nodes stables).
		const state* current_ = nullptr;
		std::string current_name_;
		std::string default_state_;
		float state_time_ = 0.0f;

	public:
		AIBehavior(const AIBehavior&) = delete;
		AIBehavior& operator=(const AIBehavior&) = delete;
		AIBehavior(AIBehavior&&) = default;
		AIBehavior& operator=(AIBehavior&&) = default;
	};
}
