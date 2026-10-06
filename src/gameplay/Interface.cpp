#include "Interface.h"

#include "Actor.h"
#include "../core/Engine.h"
#include "../core/Level.h"
#include "../scripting/Private/ScriptSystem.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

namespace lynx
{
	namespace
	{
		std::map<std::string, InterfaceInfo>& Registry()
		{
			static std::map<std::string, InterfaceInfo> registry;
			static bool builtins = false;

			if (!builtins)
			{
				builtins = true;
				registry["Damageable"] = { "Damageable", { "TakeDamage" }, {}, false };
				registry["Interactable"] = { "Interactable", { "Interact", "CanInteract" }, {}, false };
			}
			return registry;
		}

		void CollectFunctions(const std::string& name, std::vector<std::string>& out, std::set<std::string>& seen)
		{
			if (!seen.insert(name).second)
				return;

			const auto& registry = Registry();
			const auto it = registry.find(name);
			if (it == registry.end())
				return;

			for (const std::string& parent : it->second.parents)
				CollectFunctions(parent, out, seen);

			for (const std::string& fn : it->second.functions)
				if (std::find(out.begin(), out.end(), fn) == out.end())
					out.push_back(fn);
		}

		bool IsA(const std::string& name, const std::string& base, int depth)
		{
			if (name == base)
				return true;
			if (depth > 32)
				return false;

			const auto& registry = Registry();
			const auto it = registry.find(name);
			if (it == registry.end())
				return false;

			for (const std::string& parent : it->second.parents)
				if (IsA(parent, base, depth + 1))
					return true;
			return false;
		}

		std::string Key(const std::string& name, const std::string& function)
		{
			return name + "." + function;
		}

		// InterfaceArg -> argument d'une HFUNCTION (un acteur devient son id).
		PropVariantType ToProp(const InterfaceArg& arg)
		{
			return std::visit([](const auto& v) -> PropVariantType
			{
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, std::monostate>)
					return 0;
				else if constexpr (std::is_same_v<T, Actor*>)
					return v ? v->object_id_ : std::string();
				else
					return v;
			}, arg);
		}

		InterfaceArg FromProp(const PropVariantType& prop)
		{
			return std::visit([](const auto& v) -> InterfaceArg
			{
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, transform>)
					return v.location;
				else
					return v;
			}, prop);
		}
	}


	// ========================================================================
	// Registre
	// ========================================================================

	void DefineInterface(const std::string& name, const std::vector<std::string>& functions,
	                     const std::vector<std::string>& parents, bool from_script)
	{
		if (name.empty())
			return;

		InterfaceInfo& info = Registry()[name];

		// Une interface C++ n'est jamais remplacee par une classe JS du meme nom.
		if (from_script && !info.name.empty() && !info.from_script)
		{
			std::cout << "[interface] " << name << " : already declared in C++, the JavaScript one is ignored\n";
			return;
		}

		info.name = name;
		info.functions = functions;
		info.parents = parents;
		info.from_script = from_script;
	}

	const InterfaceInfo* FindInterface(const std::string& name)
	{
		const auto& registry = Registry();
		const auto it = registry.find(name);
		return it != registry.end() ? &it->second : nullptr;
	}

	std::vector<std::string> GetInterfaceNames()
	{
		std::vector<std::string> names;
		for (const auto& [name, info] : Registry())
			names.push_back(name);
		return names;
	}

	bool InterfaceIsA(const std::string& name, const std::string& base)
	{
		return IsA(name, base, 0);
	}

	std::vector<std::string> GetInterfaceFunctions(const std::string& name)
	{
		std::vector<std::string> out;
		std::set<std::string> seen;
		CollectFunctions(name, out, seen);
		return out;
	}

	bool InterfaceHasFunction(const std::string& name, const std::string& function)
	{
		if (!FindInterface(name))
			return true;

		const std::vector<std::string> functions = GetInterfaceFunctions(name);
		return std::find(functions.begin(), functions.end(), function) != functions.end();
	}

	void ForgetScriptInterfaces()
	{
		auto& registry = Registry();
		for (auto it = registry.begin(); it != registry.end();)
		{
			if (it->second.from_script)
				it = registry.erase(it);
			else
				++it;
		}
	}


	// ========================================================================
	// Actor
	// ========================================================================

	void Actor::ImplementInterface(const std::string& name)
	{
		if (!name.empty() && std::find(interfaces_.begin(), interfaces_.end(), name) == interfaces_.end())
			interfaces_.push_back(name);
	}

	void Actor::BindInterfaceFunction(const std::string& name, const std::string& function, InterfaceHandler handler)
	{
		ImplementInterface(name);

		const std::string key = Key(name, function);
		for (auto& [k, h] : interface_handlers_)
		{
			if (k == key)
			{
				h = std::move(handler);
				return;
			}
		}
		interface_handlers_.emplace_back(key, std::move(handler));
	}

	const InterfaceHandler* Actor::FindInterfaceHandler(const std::string& name, const std::string& function) const
	{
		// Exact d'abord, puis une interface fille de `name` (Pickup : Collectible).
		const std::string exact = Key(name, function);
		for (const auto& [k, h] : interface_handlers_)
			if (k == exact && h)
				return &h;

		const std::string suffix = "." + function;
		for (const auto& [k, h] : interface_handlers_)
		{
			if (!h || k.size() <= suffix.size() || k.compare(k.size() - suffix.size(), suffix.size(), suffix) != 0)
				continue;
			const std::string iface = k.substr(0, k.size() - suffix.size());
			if (InterfaceIsA(iface, name) || InterfaceIsA(name, iface))
				return &h;
		}
		return nullptr;
	}

	bool Actor::Implements(const std::string& name) const
	{
		for (const std::string& iface : interfaces_)
			if (InterfaceIsA(iface, name))
				return true;

		return scripting::ScriptImplements(this, name);
	}

	bool Actor::CallInterface(const std::string& name, const std::string& function,
	                          const InterfaceArgs& args, InterfaceArg* result)
	{
		return interfaces::Call(this, name, function, args, result);
	}


	// ========================================================================
	// Messages
	// ========================================================================

	namespace interfaces
	{
		bool Implements(const Actor* actor, const std::string& name)
		{
			return actor && actor->Implements(name);
		}

		bool Call(Actor* actor, const std::string& name, const std::string& function,
		          const InterfaceArgs& args, InterfaceArg* result)
		{
			if (result)
				*result = std::monostate{};

			if (!actor || function.empty() || !actor->Implements(name))
				return false;

			if (!InterfaceHasFunction(name, function))
			{
				std::cout << "[interface] " << function << " is not a function of " << name << "\n";
				return false;
			}

			bool handled = false;
			InterfaceArg last;

			// 1. Handler C++ (BindInterfaceFunction).
			const InterfaceHandler* handler = actor->FindInterfaceHandler(name, function);
			const bool has_hfunction = actor->GetFunctions().contains(function);

			if (handler)
			{
				const InterfaceHandler copy = *handler;   // l'acteur peut changer pendant l'appel
				last = copy(args);
				handled = true;
			}

			// 2. JavaScript : methode de la classe JS, scripts attaches (la
			//    HFUNCTION est appelee plus bas, avec les arguments C++), puis
			//    implementation par defaut si personne d'autre ne l'implemente.
			InterfaceArg js_result;
			const bool js = scripting::CallScriptInterface(
				actor, name, function, args, &js_result,
				/*skip_native*/ true, /*use_default*/ !handler && !has_hfunction);

			if (js)
			{
				handled = true;
				if (!std::holds_alternative<std::monostate>(js_result))
					last = js_result;
			}

			// 3. HFUNCTION du meme nom (si le JS ne l'a pas remplacee).
			if (!handler && !js && has_hfunction)
			{
				std::vector<PropVariantType> props;
				props.reserve(args.size());
				for (const InterfaceArg& a : args)
					props.push_back(ToProp(a));

				std::optional<PropVariantType> r;
				if (actor->CallFunctionByName(function, props, &r))
				{
					handled = true;
					if (r)
						last = FromProp(*r);
				}
			}

			if (result)
				*result = last;
			return handled;
		}

		std::vector<Actor*> FindImplementing(const std::string& name)
		{
			std::vector<Actor*> out;
			Engine* engine = Engine::Get();
			Level* level = engine ? engine->GetCurrentLevel() : nullptr;
			if (!level)
				return out;

			for (Actor* actor : level->GetActors())
				if (actor && actor->Implements(name))
					out.push_back(actor);
			return out;
		}

		int Broadcast(const std::string& name, const std::string& function, const InterfaceArgs& args)
		{
			int count = 0;
			// Liste copiee : un message peut detruire / creer des acteurs.
			for (Actor* actor : FindImplementing(name))
			{
				Engine* engine = Engine::Get();
				Level* level = engine ? engine->GetCurrentLevel() : nullptr;
				if (!level)
					break;
				const auto& actors = level->GetActors();
				if (std::find(actors.begin(), actors.end(), actor) == actors.end())
					continue;

				if (Call(actor, name, function, args))
					++count;
			}
			return count;
		}
	}


	// ========================================================================
	// Interfaces du moteur
	// ========================================================================

	bool ApplyDamage(Actor* target, float amount, Actor* instigator)
	{
		return interfaces::Call(target, "Damageable", "TakeDamage", { amount, instigator });
	}

	bool Interact(Actor* target, Actor* instigator)
	{
		if (!target || !target->Implements("Interactable"))
			return false;

		InterfaceArg can = true;
		if (interfaces::Call(target, "Interactable", "CanInteract", { instigator }, &can))
		{
			const bool* allowed = std::get_if<bool>(&can);
			if (allowed && !*allowed)
				return false;
		}

		return interfaces::Call(target, "Interactable", "Interact", { instigator });
	}


	// ========================================================================
	// Arguments
	// ========================================================================

	float InterfaceArgFloat(const InterfaceArgs& args, size_t index, float fallback)
	{
		if (index >= args.size())
			return fallback;
		const InterfaceArg& a = args[index];
		if (const float* f = std::get_if<float>(&a)) return *f;
		if (const int* i = std::get_if<int>(&a)) return static_cast<float>(*i);
		if (const bool* b = std::get_if<bool>(&a)) return *b ? 1.f : 0.f;
		return fallback;
	}

	int InterfaceArgInt(const InterfaceArgs& args, size_t index, int fallback)
	{
		if (index >= args.size())
			return fallback;
		const InterfaceArg& a = args[index];
		if (const int* i = std::get_if<int>(&a)) return *i;
		if (const float* f = std::get_if<float>(&a)) return static_cast<int>(*f);
		if (const bool* b = std::get_if<bool>(&a)) return *b ? 1 : 0;
		return fallback;
	}

	bool InterfaceArgBool(const InterfaceArgs& args, size_t index, bool fallback)
	{
		if (index >= args.size())
			return fallback;
		const InterfaceArg& a = args[index];
		if (const bool* b = std::get_if<bool>(&a)) return *b;
		if (const int* i = std::get_if<int>(&a)) return *i != 0;
		if (const float* f = std::get_if<float>(&a)) return *f != 0.f;
		if (Actor* const* p = std::get_if<Actor*>(&a)) return *p != nullptr;
		return fallback;
	}

	std::string InterfaceArgString(const InterfaceArgs& args, size_t index, const std::string& fallback)
	{
		if (index >= args.size())
			return fallback;
		if (const std::string* s = std::get_if<std::string>(&args[index]))
			return *s;
		return fallback;
	}

	Actor* InterfaceArgActor(const InterfaceArgs& args, size_t index)
	{
		if (index >= args.size())
			return nullptr;
		Actor* const* p = std::get_if<Actor*>(&args[index]);
		return p ? *p : nullptr;
	}
}
