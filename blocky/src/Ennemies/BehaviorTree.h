#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace bt {

enum class Status { Success, Failure, Running };

class Node {
public:
	virtual ~Node() = default;
	virtual Status Tick(float delta_time) = 0;
};

using NodePtr = std::unique_ptr<Node>;

// Noeud composite : possède une liste d'enfants
class Composite : public Node {
public:
	Composite &Add(NodePtr child)
	{
		children_.push_back(std::move(child));
		return *this;
	}

protected:
	std::vector<NodePtr> children_;
};

// Execute les enfants dans l'ordre, s'arrete au premier qui echoue
class Sequence : public Composite {
public:
	Status Tick(float dt) override
	{
		for (auto &child : children_)
		{
			Status s = child->Tick(dt);
			if (s != Status::Success)
				return s;
		}
		return Status::Success;
	}
};

// Execute les enfants dans l'ordre, s'arrete au premier qui reussit
class Selector : public Composite {
public:
	Status Tick(float dt) override
	{
		for (auto &child : children_)
		{
			Status s = child->Tick(dt);
			if (s != Status::Failure)
				return s;
		}
		return Status::Failure;
	}
};

// Inverse Success / Failure de l'enfant
class Inverter : public Node {
public:
	explicit Inverter(NodePtr child) : child_(std::move(child)) {}

	Status Tick(float dt) override
	{
		Status s = child_->Tick(dt);
		if (s == Status::Success) return Status::Failure;
		if (s == Status::Failure) return Status::Success;
		return s;
	}

private:
	NodePtr child_;
};

// Feuille : condition (Success si vrai, Failure sinon)
class Condition : public Node {
public:
	explicit Condition(std::function<bool()> fn) : fn_(std::move(fn)) {}

	Status Tick(float) override
	{
		return fn_() ? Status::Success : Status::Failure;
	}

private:
	std::function<bool()> fn_;
};

// Feuille : action
class Action : public Node {
public:
	explicit Action(std::function<Status(float)> fn) : fn_(std::move(fn)) {}

	Status Tick(float dt) override { return fn_(dt); }

private:
	std::function<Status(float)> fn_;
};

// Helpers de construction
template <typename T, typename... Args>
NodePtr Make(Args &&...args)
{
	return std::make_unique<T>(std::forward<Args>(args)...);
}

} // namespace bt