#include "AIBehavior.h"

#include <utility>

namespace lynx
{
	AIBehavior::AIBehavior(Actor* parent)
		: parent_(parent)
	{
	}

	// ---------- Actions ----------

	void AIBehavior::register_action(const std::string& name, action fn)
	{
		actions_[name] = std::move(fn);
	}

	bool AIBehavior::do_action(const std::string& name)
	{
		auto it = actions_.find(name);
		if (it == actions_.end() || !it->second)
			return false;

		it->second();
		return true;
	}

	// ---------- Axes ----------

	void AIBehavior::register_axis(const std::string& name, axis_action fn)
	{
		axes_[name].callback = std::move(fn);
	}

	void AIBehavior::set_axis(const std::string& name, float value)
	{
		auto it = axes_.find(name);
		if (it == axes_.end())
			return;

		if (it->second.value != value)
		{
			it->second.value = value;
			it->second.dirty = true;
		}
	}

	void AIBehavior::stop_all_axes()
	{
		for (auto& [name, ax] : axes_)
		{
			if (ax.value != 0.0f)
			{
				ax.value = 0.0f;
				ax.dirty = true;
			}
		}
	}

	float AIBehavior::get_axis(const std::string& name) const
	{
		auto it = axes_.find(name);
		return it != axes_.end() ? it->second.value : 0.0f;
	}

	void AIBehavior::apply_axes()
	{
		for (auto& [name, ax] : axes_)
		{
			// Appelé tant que != 0, et une dernière fois au retour à 0.
			if (ax.callback && (ax.value != 0.0f || ax.dirty))
				ax.callback(ax.value);

			ax.dirty = false;
		}
	}

	// ---------- Paramètres ----------

	float AIBehavior::get_float(const std::string& name) const
	{
		auto it = params_.find(name);
		return it != params_.end() ? it->second : 0.0f;
	}

	// ---------- Conditions ----------

	AIBehavior::condition AIBehavior::is_true(std::string name)
	{
		return [name = std::move(name)](const AIBehavior& ai) { return ai.get_bool(name); };
	}

	AIBehavior::condition AIBehavior::is_false(std::string name)
	{
		return [name = std::move(name)](const AIBehavior& ai) { return !ai.get_bool(name); };
	}

	AIBehavior::condition AIBehavior::greater(std::string name, float v)
	{
		return [name = std::move(name), v](const AIBehavior& ai) { return ai.get_float(name) > v; };
	}

	AIBehavior::condition AIBehavior::less(std::string name, float v)
	{
		return [name = std::move(name), v](const AIBehavior& ai) { return ai.get_float(name) < v; };
	}

	AIBehavior::condition AIBehavior::triggered(std::string name)
	{
		return [name = std::move(name)](const AIBehavior& ai) { return ai.has_trigger(name); };
	}

	AIBehavior::condition AIBehavior::state_time_greater(float seconds)
	{
		return [seconds](const AIBehavior& ai) { return ai.state_time() > seconds; };
	}

	// ---------- États ----------

	void AIBehavior::add_state(const std::string& name, ai_state_options opts)
	{
		states_[name].opts = std::move(opts);
	}

	void AIBehavior::switch_to(const std::string& name)
	{
		auto it = states_.find(name);
		if (it == states_.end())
			return;

		if (current_)
		{
			if (current_->opts.on_exit)
				current_->opts.on_exit();

			if (current_->opts.stop_axes_on_exit)
				stop_all_axes();
		}

		current_ = &it->second;
		current_name_ = name;
		state_time_ = 0.0f;

		if (current_->opts.on_enter)
			current_->opts.on_enter();
	}

	// ---------- Transitions ----------

	void AIBehavior::add_transition(
		const std::string& from,
		const std::string& to,
		condition cond,
		int priority)
	{
		transitions_.push_back({ from, to, std::move(cond), priority });
	}

	void AIBehavior::add_any_transition(const std::string& to, condition cond, int priority)
	{
		transitions_.push_back({ "", to, std::move(cond), priority });
	}

	// ---------- Update ----------

	void AIBehavior::Tick(float dt)
	{
		if (!current_ && !default_state_.empty())
			switch_to(default_state_);

		if (!current_)
		{
			triggers_.clear();
			return;
		}

		state_time_ += dt;

		// Évalue les transitions : la priorité la plus haute gagne,
		// à égalité la première déclarée.
		const transition* best = nullptr;

		for (const transition& t : transitions_)
		{
			const bool is_any = t.from.empty();

			if (!is_any && t.from != current_name_)
				continue;

			// Évite de relancer l'état courant à chaque Tick.
			if (is_any && t.to == current_name_)
				continue;

			if (!t.cond || !t.cond(*this))
				continue;

			if (!best || t.priority > best->priority)
				best = &t;
		}

		if (best)
		{
			const std::string to = best->to; // copie : les callbacks peuvent modifier transitions_
			switch_to(to);
		}

		if (current_ && current_->opts.on_update)
			current_->opts.on_update(dt);

		apply_axes();

		triggers_.clear();
	}
}
