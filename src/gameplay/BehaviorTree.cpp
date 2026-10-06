#include "BehaviorTree.h"

#include "Actor.h"
#include "BoxColliderComponent.h"
#include "Components.h"
#include "SpriteComponents.h"
#include "Private/ECS.h"
#include "../core/Engine.h"
#include "../core/Filesystem.h"
#include "../core/Level.h"
#include "../core/Profiler.h"
#include "../scripting/Private/ScriptSystem.h"

#include <xml/tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>

namespace lynx
{
	namespace
	{
		std::vector<float> ParseNumbers(std::string text)
		{
			std::replace(text.begin(), text.end(), ',', ' ');
			std::istringstream in(text);
			std::vector<float> out;
			float v = 0.f;
			while (in >> v)
				out.push_back(v);
			return out;
		}

		bool ParseFloat(const std::string& text, float& out)
		{
			char* end = nullptr;
			out = std::strtof(text.c_str(), &end);
			return end && end != text.c_str() && *end == '\0';
		}

		float ToFloat(const std::string& text, float fallback)
		{
			float v = fallback;
			return ParseFloat(text, v) ? v : fallback;
		}

		bool ToBool(const std::string& text)
		{
			return text == "true" || text == "1" || text == "yes";
		}

		std::string Num(float v)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(v));
			return buffer;
		}

		std::mt19937& Random()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}
	}


	// =========================================================================
	// Blackboard
	// =========================================================================

	float BlackboardValue::AsNumber() const
	{
		switch (type)
		{
		case Type::Bool: return b ? 1.f : 0.f;
		case Type::Int: return static_cast<float>(i);
		case Type::Float: return f;
		default: return 0.f;
		}
	}

	std::string BlackboardValue::ToString() const
	{
		switch (type)
		{
		case Type::Bool: return b ? "true" : "false";
		case Type::Int: return std::to_string(i);
		case Type::Float: return Num(f);
		case Type::String: return s;
		case Type::Vector: return Num(v.x) + " " + Num(v.y) + " " + Num(v.z);
		case Type::Actor:
		{
			Actor* a = ecs::GetActor(entity);
			return a ? "actor " + a->object_id_ : "actor (destroyed)";
		}
		default: return "(not set)";
		}
	}

	void Blackboard::SetBool(const std::string& key, bool value)
	{
		BlackboardValue v; v.type = BlackboardValue::Type::Bool; v.b = value; values_[key] = v;
	}
	void Blackboard::SetInt(const std::string& key, int value)
	{
		BlackboardValue v; v.type = BlackboardValue::Type::Int; v.i = value; values_[key] = v;
	}
	void Blackboard::SetFloat(const std::string& key, float value)
	{
		BlackboardValue v; v.type = BlackboardValue::Type::Float; v.f = value; values_[key] = v;
	}
	void Blackboard::SetString(const std::string& key, const std::string& value)
	{
		BlackboardValue v; v.type = BlackboardValue::Type::String; v.s = value; values_[key] = v;
	}
	void Blackboard::SetVector(const std::string& key, const vec3& value)
	{
		BlackboardValue v; v.type = BlackboardValue::Type::Vector; v.v = value; values_[key] = v;
	}
	void Blackboard::SetActor(const std::string& key, Actor* actor)
	{
		if (!actor)
		{
			Clear(key);
			return;
		}
		BlackboardValue v; v.type = BlackboardValue::Type::Actor; v.entity = actor->GetEntity(); values_[key] = v;
	}
	void Blackboard::Set(const std::string& key, const BlackboardValue& value)
	{
		if (value.type == BlackboardValue::Type::None)
			Clear(key);
		else
			values_[key] = value;
	}

	void Blackboard::SetFromText(const std::string& key, const std::string& text)
	{
		std::string type = GetDeclaredType(key);
		if (type.empty())
		{
			// Devine : nombre, booleen, vecteur, sinon texte.
			float f = 0.f;
			if (text == "true" || text == "false")
				type = "bool";
			else if (ParseFloat(text, f))
				type = "float";
			else if (ParseNumbers(text).size() >= 2 && text.find_first_not_of("0123456789.-+eE ,") == std::string::npos)
				type = "vector";
			else
				type = "string";
		}

		if (type == "bool")
			SetBool(key, ToBool(text));
		else if (type == "int")
			SetInt(key, static_cast<int>(ToFloat(text, 0.f)));
		else if (type == "float")
			SetFloat(key, ToFloat(text, 0.f));
		else if (type == "vector")
		{
			const auto n = ParseNumbers(text);
			SetVector(key, vec3(n.size() > 0 ? n[0] : 0.f, n.size() > 1 ? n[1] : 0.f, n.size() > 2 ? n[2] : 0.f));
		}
		else if (type == "actor")
		{
			// Un acteur par son id dans le niveau ("" : vide).
			Actor* a = nullptr;
			if (Engine* engine = Engine::Get(); engine && engine->GetCurrentLevel() && !text.empty())
				a = engine->GetCurrentLevel()->GetActorFromID(text.c_str());
			SetActor(key, a);
		}
		else
			SetString(key, text);
	}

	void Blackboard::Clear(const std::string& key)
	{
		values_.erase(key);
	}

	void Blackboard::ClearAll()
	{
		values_.clear();
	}

	bool Blackboard::IsSet(const std::string& key) const
	{
		const BlackboardValue* v = Get(key);
		if (!v)
			return false;
		if (v->type == BlackboardValue::Type::Actor)
			return ecs::GetActor(v->entity) != nullptr;
		return true;
	}

	const BlackboardValue* Blackboard::Get(const std::string& key) const
	{
		auto it = values_.find(key);
		return it == values_.end() ? nullptr : &it->second;
	}

	bool Blackboard::GetBool(const std::string& key, bool fallback) const
	{
		const BlackboardValue* v = Get(key);
		return v ? v->AsNumber() != 0.f : fallback;
	}

	int Blackboard::GetInt(const std::string& key, int fallback) const
	{
		const BlackboardValue* v = Get(key);
		return v ? static_cast<int>(v->AsNumber()) : fallback;
	}

	float Blackboard::GetFloat(const std::string& key, float fallback) const
	{
		const BlackboardValue* v = Get(key);
		return v ? v->AsNumber() : fallback;
	}

	std::string Blackboard::GetString(const std::string& key) const
	{
		const BlackboardValue* v = Get(key);
		return v ? (v->type == BlackboardValue::Type::String ? v->s : v->ToString()) : std::string();
	}

	bool Blackboard::GetVector(const std::string& key, vec3& out) const
	{
		const BlackboardValue* v = Get(key);
		if (!v)
			return false;
		if (v->type == BlackboardValue::Type::Vector)
		{
			out = v->v;
			return true;
		}
		if (v->type == BlackboardValue::Type::Actor)
		{
			if (Actor* a = ecs::GetActor(v->entity))
			{
				out = a->transform.location;
				return true;
			}
		}
		return false;
	}

	Actor* Blackboard::GetActor(const std::string& key) const
	{
		const BlackboardValue* v = Get(key);
		return v && v->type == BlackboardValue::Type::Actor ? ecs::GetActor(v->entity) : nullptr;
	}

	void Blackboard::DeclareKey(const std::string& key, const std::string& type)
	{
		declared_[key] = type;
	}

	std::string Blackboard::GetDeclaredType(const std::string& key) const
	{
		auto it = declared_.find(key);
		return it == declared_.end() ? std::string() : it->second;
	}

	std::vector<std::string> Blackboard::GetKeys() const
	{
		std::vector<std::string> keys;
		for (const auto& [k, t] : declared_)
			keys.push_back(k);
		for (const auto& [k, v] : values_)
			if (!declared_.count(k))
				keys.push_back(k);
		std::sort(keys.begin(), keys.end());
		return keys;
	}


	// =========================================================================
	// Asset
	// =========================================================================

	std::string BTAux::Get(const std::string& name, const std::string& fallback) const
	{
		for (const auto& p : params)
			if (p.name == name)
				return p.value;
		return fallback;
	}

	void BTAux::Set(const std::string& name, const std::string& value)
	{
		for (auto& p : params)
			if (p.name == name)
			{
				p.value = value;
				return;
			}
		params.push_back({ name, value });
	}

	std::string BTNodeDesc::Get(const std::string& name, const std::string& fallback) const
	{
		for (const auto& p : params)
			if (p.name == name)
				return p.value;
		return fallback;
	}

	void BTNodeDesc::Set(const std::string& name, const std::string& value)
	{
		for (auto& p : params)
			if (p.name == name)
			{
				p.value = value;
				return;
			}
		params.push_back({ name, value });
	}

	namespace
	{
		// Parametres d'un noeud / aux : les attributs inconnus de la balise.
		void ReadParams(const tinyxml2::XMLElement* e, std::vector<BTParam>& out, std::initializer_list<const char*> skip)
		{
			for (const tinyxml2::XMLAttribute* a = e->FirstAttribute(); a; a = a->Next())
			{
				bool skipped = false;
				for (const char* s : skip)
					if (std::string(a->Name()) == s)
						skipped = true;
				if (!skipped)
					out.push_back({ a->Name(), a->Value() });
			}
		}

		void WriteParams(tinyxml2::XMLElement* e, const std::vector<BTParam>& params)
		{
			for (const auto& p : params)
				e->SetAttribute(p.name.c_str(), p.value.c_str());
		}

		// Parametres manquants : valeurs par defaut du type.
		void FillDefaults(const std::string& type, std::vector<BTParam>& params, const char* category = nullptr)
		{
			const BTNodeTypeDef* def = category ? FindBTNodeType(type, category) : FindBTNodeType(type);
			if (!def)
				return;
			for (const BTParamDef& p : def->params)
			{
				bool present = false;
				for (const auto& existing : params)
					if (existing.name == p.name)
						present = true;
				if (!present)
					params.push_back({ p.name, p.default_value });
			}
		}
	}

	bool BehaviorTreeAsset::LoadFromString(const std::string& xml, std::string* error)
	{
		using namespace tinyxml2;

		XMLDocument doc;
		if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
		{
			if (error)
				*error = std::string("invalid XML : ") + doc.ErrorStr();
			return false;
		}

		const XMLElement* root = doc.FirstChildElement("BehaviorTree");
		if (!root)
		{
			if (error)
				*error = "not a behavior tree (no <BehaviorTree>)";
			return false;
		}

		keys.clear();
		nodes.clear();

		if (const XMLElement* bb = root->FirstChildElement("Blackboard"))
		{
			for (const XMLElement* k = bb->FirstChildElement("Key"); k; k = k->NextSiblingElement("Key"))
			{
				BTKeyDesc key;
				key.name = k->Attribute("name") ? k->Attribute("name") : "";
				key.type = k->Attribute("type") ? k->Attribute("type") : "float";
				key.default_value = k->Attribute("default") ? k->Attribute("default") : "";
				keys.push_back(std::move(key));
			}
		}

		for (const XMLElement* n = root->FirstChildElement("Node"); n; n = n->NextSiblingElement("Node"))
		{
			BTNodeDesc node;
			node.id = n->IntAttribute("id", 0);
			node.parent = n->IntAttribute("parent", 0);
			node.type = n->Attribute("type") ? n->Attribute("type") : "";
			node.name = n->Attribute("name") ? n->Attribute("name") : "";
			node.x = n->FloatAttribute("x", 0.f);
			node.y = n->FloatAttribute("y", 0.f);
			ReadParams(n, node.params, { "id", "parent", "type", "name", "x", "y" });
			FillDefaults(node.type, node.params);

			for (const XMLElement* a = n->FirstChildElement(); a; a = a->NextSiblingElement())
			{
				const std::string tag = a->Name();
				if (tag != "Decorator" && tag != "Service")
					continue;
				BTAux aux;
				aux.id = a->IntAttribute("id", 0);
				aux.type = a->Attribute("type") ? a->Attribute("type") : "";
				ReadParams(a, aux.params, { "id", "type" });
				FillDefaults(aux.type, aux.params, tag == "Decorator" ? "decorator" : "service");
				(tag == "Decorator" ? node.decorators : node.services).push_back(std::move(aux));
			}
			nodes.push_back(std::move(node));
		}

		// Ids manquants (fichier ecrit a la main).
		for (auto& node : nodes)
		{
			if (node.id <= 0)
				node.id = NewId();
			for (auto& d : node.decorators)
				if (d.id <= 0)
					d.id = NewId();
			for (auto& s : node.services)
				if (s.id <= 0)
					s.id = NewId();
		}

		if (!Root())
		{
			BTNodeDesc r;
			r.id = NewId();
			r.type = "Root";
			nodes.insert(nodes.begin(), r);
		}
		return true;
	}

	std::string BehaviorTreeAsset::SaveToString() const
	{
		using namespace tinyxml2;

		XMLDocument doc;
		doc.InsertFirstChild(doc.NewDeclaration());
		XMLElement* root = doc.NewElement("BehaviorTree");
		root->SetAttribute("version", 1);
		doc.InsertEndChild(root);

		XMLElement* bb = doc.NewElement("Blackboard");
		for (const auto& k : keys)
		{
			XMLElement* e = doc.NewElement("Key");
			e->SetAttribute("name", k.name.c_str());
			e->SetAttribute("type", k.type.c_str());
			if (!k.default_value.empty())
				e->SetAttribute("default", k.default_value.c_str());
			bb->InsertEndChild(e);
		}
		root->InsertEndChild(bb);

		for (const auto& n : nodes)
		{
			XMLElement* e = doc.NewElement("Node");
			e->SetAttribute("id", n.id);
			e->SetAttribute("parent", n.parent);
			e->SetAttribute("type", n.type.c_str());
			if (!n.name.empty())
				e->SetAttribute("name", n.name.c_str());
			e->SetAttribute("x", Num(n.x).c_str());
			e->SetAttribute("y", Num(n.y).c_str());
			WriteParams(e, n.params);

			for (const auto& d : n.decorators)
			{
				XMLElement* de = doc.NewElement("Decorator");
				de->SetAttribute("id", d.id);
				de->SetAttribute("type", d.type.c_str());
				WriteParams(de, d.params);
				e->InsertEndChild(de);
			}
			for (const auto& s : n.services)
			{
				XMLElement* se = doc.NewElement("Service");
				se->SetAttribute("id", s.id);
				se->SetAttribute("type", s.type.c_str());
				WriteParams(se, s.params);
				e->InsertEndChild(se);
			}
			root->InsertEndChild(e);
		}

		XMLPrinter printer;
		doc.Print(&printer);
		return printer.CStr();
	}

	bool BehaviorTreeAsset::LoadFromAsset(const std::string& path, std::string* error)
	{
		const auto data = fs::ReadBinary(path);
		if (data.empty())
		{
			if (error)
				*error = "could not read assets/" + path;
			return false;
		}
		return LoadFromString(std::string(reinterpret_cast<const char*>(data.data()), data.size()), error);
	}

	BTNodeDesc* BehaviorTreeAsset::Find(int id)
	{
		for (auto& n : nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	const BTNodeDesc* BehaviorTreeAsset::Find(int id) const
	{
		for (const auto& n : nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	BTNodeDesc* BehaviorTreeAsset::Root()
	{
		for (auto& n : nodes)
			if (n.type == "Root")
				return &n;
		return nullptr;
	}

	const BTNodeDesc* BehaviorTreeAsset::Root() const
	{
		for (const auto& n : nodes)
			if (n.type == "Root")
				return &n;
		return nullptr;
	}

	std::vector<const BTNodeDesc*> BehaviorTreeAsset::ChildrenOf(int id) const
	{
		std::vector<const BTNodeDesc*> out;
		for (const auto& n : nodes)
			if (n.parent == id && n.id != id)
				out.push_back(&n);
		std::stable_sort(out.begin(), out.end(), [](const BTNodeDesc* a, const BTNodeDesc* b)
		{
			return a->y != b->y ? a->y < b->y : a->x < b->x;
		});
		return out;
	}

	int BehaviorTreeAsset::NewId() const
	{
		int id = 1;
		for (const auto& n : nodes)
		{
			id = std::max(id, n.id + 1);
			for (const auto& d : n.decorators)
				id = std::max(id, d.id + 1);
			for (const auto& s : n.services)
				id = std::max(id, s.id + 1);
		}
		return id;
	}

	BehaviorTreeAsset BehaviorTreeAsset::MakeDefault()
	{
		BehaviorTreeAsset asset;
		BTNodeDesc root;
		root.id = 1;
		root.type = "Root";
		asset.nodes.push_back(root);
		return asset;
	}


	// =========================================================================
	// Types de noeuds
	// =========================================================================

	const std::vector<BTNodeTypeDef>& GetBTNodeTypes()
	{
		static const char* kAbort = "none|self|lowerPriority|both";
		static const char* kAbortHelp =
			"self : stops this node when the condition becomes false.\n"
			"lowerPriority : interrupts what runs below (in a Selector) when it becomes true.";

		static const std::vector<BTNodeTypeDef> types = {
			{ "Root", "root", "Start of the tree : one child, run again when it ends.", {} },

			// ---- Composites ------------------------------------------------------
			{ "Selector", "composite", "Runs its children from the top until one SUCCEEDS (a fallback list).", {} },
			{ "Sequence", "composite", "Runs its children from the top until one FAILS (steps in order).", {} },

			// ---- Tasks -----------------------------------------------------------------
			{ "Wait", "task", "Waits, then succeeds.", {
				{ "time", "float", "1", "", "Seconds." },
				{ "random", "float", "0", "", "+/- random seconds." } } },
			{ "MoveTo", "task", "Moves the actor to a Blackboard key (vector or actor). Succeeds when close enough.", {
				{ "target", "key", "target", "", "Vector or actor key." },
				{ "speed", "float", "5", "", "Units per second." },
				{ "acceptance", "float", "0.5", "", "Distance that counts as arrived." },
				{ "use_z", "bool", "false", "", "Also move on Z (default : X / Y only)." },
				{ "face", "bool", "true", "", "Flips the actor (scale X) toward the movement." },
				{ "anim_speed_param", "string", "", "", "AnimationSprite float parameter set to the speed (ex : speed)." },
				{ "timeout", "float", "0", "", "Fails after this many seconds (0 : never)." } } },
			{ "SetValue", "task", "Writes a Blackboard value.", {
				{ "key", "key", "", "", "" },
				{ "value", "string", "", "", "Text converted to the type of the key (3, true, 1 2 0, an actor id...)." } } },
			{ "ClearValue", "task", "Clears a Blackboard value.", {
				{ "key", "key", "", "", "" } } },
			{ "SetAnimParam", "task", "Sets a parameter of the AnimationSprite (Anim Graph).", {
				{ "param", "string", "", "", "" },
				{ "kind", "enum", "trigger", "trigger|float|bool|int", "" },
				{ "value", "string", "1", "", "" } } },
			{ "Log", "task", "Prints a line (console), then succeeds.", {
				{ "text", "string", "Hello", "", "{key} : value of a Blackboard key." } } },
			{ "CallFunction", "task", "Calls a JavaScript function of the actor every tick : true / undefined = success, false = failure, \"running\" = not finished.", {
				{ "function", "string", "", "", "Method of its JS class or function of an attached script." } } },
			{ "Script", "task", "A JavaScript class : class X extends BTTask { Execute(dt) { } Tick(dt) { } Abort() { } }.", {
				{ "class", "class", "", "BTTask", "" } } },
			{ "Finish", "task", "Ends at once with a result.", {
				{ "result", "enum", "success", "success|failure", "" } } },

			// ---- Decorators ------------------------------------------------------------
			{ "Blackboard", "decorator", "Condition on a Blackboard key.", {
				{ "key", "key", "", "", "" },
				{ "op", "enum", "isSet", "isSet|isNotSet|==|!=|>|>=|<|<=", "" },
				{ "value", "string", "", "", "Compared value (number, true / false, text)." },
				{ "abort", "enum", "none", kAbort, kAbortHelp },
				{ "inverse", "bool", "false", "", "Inverts the condition." } } },
			{ "CallFunction", "decorator", "Condition : a JavaScript function of the actor returns true.", {
				{ "function", "string", "", "", "" },
				{ "abort", "enum", "none", kAbort, kAbortHelp },
				{ "inverse", "bool", "false", "", "" } } },
			{ "Script", "decorator", "Condition : class X extends BTDecorator { Check() { return true; } }.", {
				{ "class", "class", "", "BTDecorator", "" },
				{ "abort", "enum", "none", kAbort, kAbortHelp },
				{ "inverse", "bool", "false", "", "" } } },
			{ "Cooldown", "decorator", "After the node ends, it fails for some time.", {
				{ "time", "float", "2", "", "Seconds." } } },
			{ "Loop", "decorator", "Runs the node again while it succeeds.", {
				{ "count", "int", "3", "", "Number of runs (0 : forever)." } } },
			{ "TimeLimit", "decorator", "The node fails if it runs too long.", {
				{ "time", "float", "5", "", "Seconds." } } },
			{ "ForceSuccess", "decorator", "The node always succeeds.", {} },

			// ---- Services --------------------------------------------------------------
			{ "CallFunction", "service", "Calls a JavaScript function of the actor at an interval while the node runs.", {
				{ "function", "string", "", "", "" },
				{ "interval", "float", "0.5", "", "Seconds." } } },
			{ "Script", "service", "class X extends BTService { Activated() { } Tick(dt) { } Deactivated() { } }.", {
				{ "class", "class", "", "BTService", "" },
				{ "interval", "float", "0.5", "", "Seconds." } } },
			{ "DistanceTo", "service", "Writes the distance to a target (vector or actor key) into a float key.", {
				{ "target", "key", "target", "", "" },
				{ "result", "key", "distance", "", "" },
				{ "interval", "float", "0.2", "", "" } } },
			{ "FindNearest", "service", "Writes the nearest actor with a tag (within a radius) into an actor key, or clears it.", {
				{ "tag", "string", "player", "", "TagsComponent tag." },
				{ "radius", "float", "20", "", "" },
				{ "result", "key", "target", "", "" },
				{ "interval", "float", "0.5", "", "" } } },
		};
		return types;
	}

	const BTNodeTypeDef* FindBTNodeType(const std::string& type, const char* category)
	{
		for (const auto& t : GetBTNodeTypes())
			if (type == t.type && category && std::string(t.category) == category)
				return &t;
		return nullptr;
	}

	const BTNodeTypeDef* FindBTNodeType(const std::string& type)
	{
		for (const auto& t : GetBTNodeTypes())
			if (type == t.type && std::string(t.category) != "decorator" && std::string(t.category) != "service")
				return &t;
		return nullptr;
	}
}


// =============================================================================
// Execution
// =============================================================================

namespace lynx
{
	namespace
	{
		enum class Status { Success, Failure, Running };

		Status FromScript(int s)
		{
			return s == 2 ? Status::Running : (s == 1 ? Status::Failure : Status::Success);
		}

		// Type de decorateur / service : la recherche tient compte de la categorie
		// (CallFunction / Script existent en tache, decorateur et service).
		const BTNodeTypeDef* FindAuxType(const std::string& type, const char* category)
		{
			for (const auto& t : GetBTNodeTypes())
				if (type == t.type && std::string(t.category) == category)
					return &t;
			return nullptr;
		}

		enum class Abort { None, Self, Lower, Both };

		struct RAux
		{
			const BTAux* desc = nullptr;
			std::string type;
			uint32_t script = 0;
			Abort abort = Abort::None;
			bool inverse = false;
			float interval = 0.5f;
			float timer = 0.f;
			int loops_left = 0;
			double cooldown_until = -1.0;

			std::string Get(const std::string& n, const std::string& f = "") const { return desc->Get(n, f); }
			float GetFloat(const std::string& n, float f) const { return ToFloat(desc->Get(n, ""), f); }
		};

		struct RNode
		{
			const BTNodeDesc* desc = nullptr;
			bool composite = false;
			bool selector = false;
			std::vector<RNode*> children;
			std::vector<RAux> decorators;
			std::vector<RAux> services;
			uint32_t script = 0;

			// Etat
			bool active = false;
			bool started = false;       // tache demarree
			bool abort_requested = false;
			bool loop_pending = false;
			int current = 0;
			float elapsed = 0.f;
			float wait = 0.f;
			float stuck = 0.f;

			std::string Get(const std::string& n, const std::string& f = "") const { return desc->Get(n, f); }
			float GetFloat(const std::string& n, float f) const { return ToFloat(desc->Get(n, ""), f); }
		};

		std::vector<BehaviorTreeComponent*>& RunningList()
		{
			static std::vector<BehaviorTreeComponent*> list;
			return list;
		}

		Abort ParseAbort(const std::string& text)
		{
			if (text == "self") return Abort::Self;
			if (text == "lowerPriority") return Abort::Lower;
			if (text == "both") return Abort::Both;
			return Abort::None;
		}

		std::vector<std::pair<std::string, std::string>> ParamPairs(const std::vector<BTParam>& params)
		{
			std::vector<std::pair<std::string, std::string>> out;
			for (const auto& p : params)
				out.emplace_back(p.name, p.value);
			return out;
		}
	}

	struct BehaviorTreeComponent::Impl
	{
		BehaviorTreeComponent* self = nullptr;
		BehaviorTreeAsset asset;
		std::string loaded;
		std::vector<std::unique_ptr<RNode>> nodes;
		RNode* root = nullptr;
		Blackboard blackboard;
		bool running = false;
		double time = 0.0;

		Actor* Owner() const { return self->GetOwner(); }

		// ---- Construction -----------------------------------------------------

		void Build()
		{
			Destroy();

			const BTNodeDesc* root_desc = asset.Root();
			if (!root_desc)
				return;

			std::function<RNode*(const BTNodeDesc&)> make = [&](const BTNodeDesc& d) -> RNode*
			{
				auto node = std::make_unique<RNode>();
				RNode* raw = node.get();
				raw->desc = &d;
				raw->composite = d.type == "Root" || d.type == "Selector" || d.type == "Sequence";
				raw->selector = d.type == "Selector";

				if (d.type == "Script")
					raw->script = scripting::CreateAINode(scripting::AINodeKind::Task, d.Get("class"), Owner(), ParamPairs(d.params));

				for (const BTAux& a : d.decorators)
				{
					RAux aux;
					aux.desc = &a;
					aux.type = a.type;
					aux.abort = ParseAbort(a.Get("abort", "none"));
					aux.inverse = ToBool(a.Get("inverse", "false"));
					if (a.type == "Script")
						aux.script = scripting::CreateAINode(scripting::AINodeKind::Decorator, a.Get("class"), Owner(), ParamPairs(a.params));
					raw->decorators.push_back(aux);
				}
				for (const BTAux& a : d.services)
				{
					RAux aux;
					aux.desc = &a;
					aux.type = a.type;
					aux.interval = std::max(0.f, ToFloat(a.Get("interval", "0.5"), 0.5f));
					if (a.type == "Script")
						aux.script = scripting::CreateAINode(scripting::AINodeKind::Service, a.Get("class"), Owner(), ParamPairs(a.params));
					raw->services.push_back(aux);
				}

				nodes.push_back(std::move(node));

				if (raw->composite)
				{
					auto children = asset.ChildrenOf(d.id);
					if (d.type == "Root" && children.size() > 1)
						children.resize(1);   // la racine n'a qu'un enfant
					for (const BTNodeDesc* c : children)
						raw->children.push_back(make(*c));
				}
				return raw;
			};

			root = make(*root_desc);
		}

		void Destroy()
		{
			for (auto& n : nodes)
			{
				if (n->script)
					scripting::DestroyAINode(n->script);
				for (auto& a : n->decorators)
					if (a.script)
						scripting::DestroyAINode(a.script);
				for (auto& a : n->services)
					if (a.script)
						scripting::DestroyAINode(a.script);
			}
			nodes.clear();
			root = nullptr;
		}

		// ---- Conditions ---------------------------------------------------------

		bool EvalBlackboard(const RAux& d) const
		{
			const std::string key = d.Get("key");
			const std::string op = d.Get("op", "isSet");
			const std::string text = d.Get("value");

			if (op == "isSet")
				return blackboard.IsSet(key);
			if (op == "isNotSet")
				return !blackboard.IsSet(key);

			const BlackboardValue* v = blackboard.Get(key);
			if (!v)
				return op == "!=";

			if (v->type == BlackboardValue::Type::String || v->type == BlackboardValue::Type::Actor ||
			    v->type == BlackboardValue::Type::Vector)
			{
				const std::string s = v->type == BlackboardValue::Type::String ? v->s
				                    : (v->type == BlackboardValue::Type::Actor && ecs::GetActor(v->entity)
				                           ? ecs::GetActor(v->entity)->object_id_ : v->ToString());
				if (op == "==") return s == text;
				if (op == "!=") return s != text;
				return false;
			}

			const float a = v->AsNumber();
			const float b = text == "true" ? 1.f : (text == "false" ? 0.f : ToFloat(text, 0.f));
			if (op == "==") return a == b;
			if (op == "!=") return a != b;
			if (op == ">") return a > b;
			if (op == ">=") return a >= b;
			if (op == "<") return a < b;
			if (op == "<=") return a <= b;
			return false;
		}

		// Decorateur conditionnel (sinon : nullopt-like -> true, conditional = false).
		bool EvalDecorator(const RAux& d, bool& conditional) const
		{
			conditional = true;
			bool r = true;
			if (d.type == "Blackboard")
				r = EvalBlackboard(d);
			else if (d.type == "CallFunction")
				r = scripting::CallActorPredicate(Owner(), d.Get("function").c_str());
			else if (d.type == "Script")
				r = d.script && scripting::CallAINode(d.script, "Check", 0.f) == 0;
			else
			{
				conditional = false;
				return true;
			}
			return d.inverse ? !r : r;
		}

		bool CheckConditions(const RNode& n) const
		{
			for (const RAux& d : n.decorators)
			{
				bool conditional = false;
				if (!EvalDecorator(d, conditional))
					return false;
				if (d.type == "Cooldown" && time < d.cooldown_until)
					return false;
			}
			return true;
		}

		bool HasAbort(const RNode& n, bool self_mode) const
		{
			for (const RAux& d : n.decorators)
			{
				if (d.abort == Abort::Both)
					return true;
				if (self_mode ? d.abort == Abort::Self : d.abort == Abort::Lower)
					return true;
			}
			return false;
		}

		// Decorateurs "self" : encore vrais ?
		bool SelfConditionsHold(const RNode& n) const
		{
			for (const RAux& d : n.decorators)
			{
				if (d.abort != Abort::Self && d.abort != Abort::Both)
					continue;
				bool conditional = false;
				if (!EvalDecorator(d, conditional))
					return false;
			}
			return true;
		}

		// ---- Services -------------------------------------------------------------

		void RunService(RAux& s, float dt)
		{
			Actor* owner = Owner();
			if (s.type == "CallFunction")
			{
				scripting::CallActorTask(owner, s.Get("function").c_str(), dt);
			}
			else if (s.type == "Script")
			{
				if (s.script)
					scripting::CallAINode(s.script, "Tick", dt);
			}
			else if (s.type == "DistanceTo")
			{
				vec3 target;
				if (owner && blackboard.GetVector(s.Get("target"), target))
				{
					const vec3 d = target - owner->transform.location;
					blackboard.SetFloat(s.Get("result", "distance"), std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
				}
				else
				{
					blackboard.Clear(s.Get("result", "distance"));
				}
			}
			else if (s.type == "FindNearest")
			{
				Actor* best = nullptr;
				float best_d = s.GetFloat("radius", 20.f);
				const std::string tag = s.Get("tag");
				Engine* engine = Engine::Get();
				Level* level = engine ? engine->GetCurrentLevel() : nullptr;
				if (owner && level)
				{
					for (Actor* a : level->GetActors())
					{
						if (!a || a == owner)
							continue;
						auto* tags = a->GetComponent<TagsComponent>();
						if (!tags || !tags->Has(tag))
							continue;
						const vec3 d = a->transform.location - owner->transform.location;
						const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
						if (dist <= best_d)
						{
							best_d = dist;
							best = a;
						}
					}
				}
				blackboard.SetActor(s.Get("result", "target"), best);
			}
		}

		void TickServices(RNode& n, float dt)
		{
			for (RAux& s : n.services)
			{
				s.timer -= dt;
				if (s.timer <= 0.f)
				{
					s.timer = s.interval;
					RunService(s, dt);
				}
			}
		}

		// ---- Entree / sortie des noeuds -----------------------------------------------

		void Enter(RNode& n, bool keep_loops)
		{
			n.active = true;
			n.started = false;
			n.abort_requested = false;
			n.current = 0;
			n.elapsed = 0.f;
			n.stuck = 0.f;

			for (RAux& d : n.decorators)
				if (d.type == "Loop" && !keep_loops)
					d.loops_left = std::max(0, static_cast<int>(d.GetFloat("count", 3.f)));

			for (RAux& s : n.services)
			{
				s.timer = 0.f;     // premier passage tout de suite
				if (s.type == "Script" && s.script)
					scripting::CallAINode(s.script, "Activated", 0.f);
			}
		}

		void Exit(RNode& n)
		{
			if (!n.active)
				return;
			n.active = false;
			n.started = false;
			for (RAux& s : n.services)
				if (s.type == "Script" && s.script)
					scripting::CallAINode(s.script, "Deactivated", 0.f);

			// MoveTo : la vitesse de l'animation retombe.
			if (n.desc->type == "MoveTo")
				SetAnimSpeed(n, 0.f);
		}

		void AbortSubtree(RNode& n)
		{
			if (!n.active)
				return;
			if (n.composite)
			{
				for (RNode* c : n.children)
					AbortSubtree(*c);
			}
			else if (n.started && n.script)
			{
				scripting::CallAINode(n.script, "Abort", 0.f);
			}
			n.loop_pending = false;
			Exit(n);
		}

		// ---- Taches -------------------------------------------------------------------

		void SetAnimSpeed(RNode& n, float speed)
		{
			const std::string param = n.Get("anim_speed_param");
			if (param.empty() || !Owner())
				return;
			if (auto* sprite = Owner()->GetComponent<AnimationSpriteComponent>())
				sprite->SetFloat(param, speed);
		}

		Status MoveTo(RNode& n, float dt)
		{
			Actor* owner = Owner();
			vec3 target;
			if (!owner || !blackboard.GetVector(n.Get("target", "target"), target))
				return Status::Failure;

			const bool use_z = ToBool(n.Get("use_z", "false"));
			vec3 delta = target - owner->transform.location;
			if (!use_z)
				delta.z = 0.f;

			const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
			const float acceptance = n.GetFloat("acceptance", 0.5f);
			if (dist <= acceptance)
			{
				SetAnimSpeed(n, 0.f);
				return Status::Success;
			}

			const float timeout = n.GetFloat("timeout", 0.f);
			if (timeout > 0.f && n.elapsed > timeout)
				return Status::Failure;

			const float speed = n.GetFloat("speed", 5.f);
			const float step = std::min(speed * dt, dist - acceptance * 0.5f);
			const vec3 move = delta * (step / dist);

			const vec3 before = owner->transform.location;
			if (auto* box = owner->GetComponent<BoxColliderComponent>())
				box->MoveAndCollide(move);
			else
				owner->transform.location += move;

			if (ToBool(n.Get("face", "true")) && std::fabs(move.x) > 1e-4f)
			{
				const float sx = std::fabs(owner->transform.scale.x);
				owner->transform.scale.x = move.x < 0.f ? -sx : sx;
			}

			const vec3 moved = owner->transform.location - before;
			const float moved_len = std::sqrt(moved.x * moved.x + moved.y * moved.y + moved.z * moved.z);
			SetAnimSpeed(n, dt > 0.f ? moved_len / dt : 0.f);

			// Bloque contre un mur : echec apres 1 s.
			if (step > 0.f && moved_len < step * 0.1f)
			{
				n.stuck += dt;
				if (n.stuck > 1.f)
					return Status::Failure;
			}
			else
			{
				n.stuck = 0.f;
			}
			return Status::Running;
		}

		std::string Expand(const std::string& text) const
		{
			// "{key}" -> valeur du Blackboard
			std::string out;
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == '{')
				{
					const size_t end = text.find('}', i);
					if (end != std::string::npos)
					{
						const BlackboardValue* v = blackboard.Get(text.substr(i + 1, end - i - 1));
						out += v ? v->ToString() : "(not set)";
						i = end;
						continue;
					}
				}
				out += text[i];
			}
			return out;
		}

		Status RunTask(RNode& n, float dt, bool first)
		{
			const std::string& type = n.desc->type;
			Actor* owner = Owner();

			if (type == "Wait")
			{
				if (first)
				{
					const float random = n.GetFloat("random", 0.f);
					std::uniform_real_distribution<float> dist(-random, random);
					n.wait = std::max(0.f, n.GetFloat("time", 1.f) + (random > 0.f ? dist(Random()) : 0.f));
				}
				return n.elapsed >= n.wait ? Status::Success : Status::Running;
			}
			if (type == "MoveTo")
				return MoveTo(n, dt);
			if (type == "SetValue")
			{
				blackboard.SetFromText(n.Get("key"), n.Get("value"));
				return Status::Success;
			}
			if (type == "ClearValue")
			{
				blackboard.Clear(n.Get("key"));
				return Status::Success;
			}
			if (type == "SetAnimParam")
			{
				auto* sprite = owner ? owner->GetComponent<AnimationSpriteComponent>() : nullptr;
				if (!sprite)
					return Status::Failure;
				const std::string param = n.Get("param");
				const std::string kind = n.Get("kind", "trigger");
				const std::string value = n.Get("value", "1");
				if (kind == "trigger") sprite->SetTrigger(param);
				else if (kind == "bool") sprite->SetBool(param, ToBool(value));
				else if (kind == "int") sprite->SetInt(param, static_cast<int>(ToFloat(value, 0.f)));
				else sprite->SetFloat(param, ToFloat(value, 0.f));
				return Status::Success;
			}
			if (type == "Log")
			{
				std::cout << "[AI] " << (owner ? owner->object_id_ : std::string("?")) << " : "
				          << Expand(n.Get("text")) << std::endl;
				return Status::Success;
			}
			if (type == "CallFunction")
			{
				bool found = false;
				const int s = scripting::CallActorTask(owner, n.Get("function").c_str(), dt, &found);
				return found ? FromScript(s) : Status::Failure;
			}
			if (type == "Script")
			{
				if (!n.script)
					return Status::Failure;
				bool found = false;
				const int s = scripting::CallAINode(n.script, first ? "Execute" : "Tick", dt, &found);
				// Pas de Tick : la tache qui a dit "running" reste en cours.
				if (!found)
					return first ? Status::Success : Status::Running;
				return FromScript(s);
			}
			if (type == "Finish")
				return n.Get("result", "success") == "failure" ? Status::Failure : Status::Success;

			std::cout << "[AI] unknown task " << type << std::endl;
			return Status::Failure;
		}

		// ---- Execution ------------------------------------------------------------------

		Status Exec(RNode& n, float dt)
		{
			bool keep_loops = false;
			if (n.loop_pending)
			{
				n.loop_pending = false;
				keep_loops = true;
			}
			else if (!n.active)
			{
				if (!CheckConditions(n))
					return Status::Failure;
			}

			if (!n.active)
				Enter(n, keep_loops);
			else if (n.abort_requested)
			{
				n.abort_requested = false;
				AbortSubtree(n);
				return Finish(n, Status::Failure, true);
			}

			n.elapsed += dt;

			for (RAux& d : n.decorators)
			{
				if (d.type == "TimeLimit" && n.elapsed > d.GetFloat("time", 5.f))
				{
					AbortSubtree(n);
					return Finish(n, Status::Failure, true);
				}
			}

			TickServices(n, dt);

			Status s;
			if (n.composite)
			{
				s = n.selector ? Status::Failure : Status::Success;
				bool done = false;
				while (n.current < static_cast<int>(n.children.size()) && !done)
				{
					const Status c = Exec(*n.children[static_cast<size_t>(n.current)], dt);
					if (c == Status::Running)
						return Status::Running;
					if (n.desc->type == "Root")
					{
						s = c;
						done = true;
					}
					else if (n.selector && c == Status::Success)
					{
						s = Status::Success;
						done = true;
					}
					else if (!n.selector && c == Status::Failure)
					{
						s = Status::Failure;
						done = true;
					}
					else
					{
						++n.current;
					}
				}
			}
			else
			{
				const bool first = !n.started;
				n.started = true;
				s = RunTask(n, dt, first);
				if (s == Status::Running)
					return Status::Running;
			}

			return Finish(n, s, false);
		}

		// Fin d'un noeud : Loop, Cooldown, ForceSuccess.
		Status Finish(RNode& n, Status s, bool aborted)
		{
			if (!aborted && s == Status::Success)
			{
				for (RAux& d : n.decorators)
				{
					if (d.type != "Loop")
						continue;
					const bool forever = d.GetFloat("count", 3.f) <= 0.f;
					if (forever || --d.loops_left > 0)
					{
						Exit(n);
						n.loop_pending = true;
						return Status::Running;    // relance au prochain tick
					}
				}
			}

			Exit(n);

			for (RAux& d : n.decorators)
			{
				if (d.type == "Cooldown")
					d.cooldown_until = time + d.GetFloat("time", 2.f);
				if (d.type == "ForceSuccess")
					s = Status::Success;
			}
			return s;
		}

		// Observer aborts, avant l'execution du tick.
		void CheckAborts(RNode& n)
		{
			if (!n.active)
				return;

			if (&n != root && HasAbort(n, true) && !SelfConditionsHold(n))
			{
				n.abort_requested = true;
				return;
			}

			if (!n.composite)
				return;

			// Selector : un enfant plus prioritaire redevient possible.
			if (n.selector)
			{
				for (int j = 0; j < n.current && j < static_cast<int>(n.children.size()); ++j)
				{
					RNode& c = *n.children[static_cast<size_t>(j)];
					if (HasAbort(c, false) && CheckConditions(c))
					{
						if (n.current < static_cast<int>(n.children.size()))
							AbortSubtree(*n.children[static_cast<size_t>(n.current)]);
						n.current = j;
						return;
					}
				}
			}

			if (n.current < static_cast<int>(n.children.size()))
				CheckAborts(*n.children[static_cast<size_t>(n.current)]);
		}

		void Tick(float dt)
		{
			if (!running || !root)
				return;

			time += dt;
			CheckAborts(*root);
			const Status s = Exec(*root, dt);
			if (s != Status::Running)
				Exit(*root);    // la racine repart au prochain tick
		}

		void ActivePath(std::vector<const RNode*>& out) const
		{
			const RNode* n = root;
			while (n && (n->active || n->loop_pending))
			{
				out.push_back(n);
				if (!n->composite || n->current >= static_cast<int>(n->children.size()))
					break;
				n = n->children[static_cast<size_t>(n->current)];
			}
		}
	};


	// =========================================================================
	// BehaviorTreeComponent
	// =========================================================================

	BehaviorTreeComponent::BehaviorTreeComponent()
		: impl_(std::make_unique<Impl>())
	{
		impl_->self = this;
	}

	BehaviorTreeComponent::~BehaviorTreeComponent()
	{
		Stop();
		impl_->Destroy();
	}

	bool BehaviorTreeComponent::Load(const std::string& asset)
	{
		const bool was_running = impl_->running;
		Stop();

		behavior_tree = asset;
		impl_->loaded = asset;
		impl_->asset = BehaviorTreeAsset::MakeDefault();
		impl_->Destroy();

		if (asset.empty())
			return true;

		std::string error;
		if (!impl_->asset.LoadFromAsset(asset, &error))
		{
			std::cout << "[AI] " << asset << " : " << error << std::endl;
			return false;
		}

		for (const BTKeyDesc& k : impl_->asset.keys)
		{
			impl_->blackboard.DeclareKey(k.name, k.type);
			if (!k.default_value.empty() && !impl_->blackboard.IsSet(k.name))
				impl_->blackboard.SetFromText(k.name, k.default_value);
		}

		if (was_running)
			Start();
		return true;
	}

	void BehaviorTreeComponent::Start()
	{
		if (behavior_tree != impl_->loaded)
			Load(behavior_tree);
		if (impl_->running)
			return;

		impl_->Build();
		impl_->running = impl_->root != nullptr;
		if (impl_->running)
			RunningList().push_back(this);
	}

	void BehaviorTreeComponent::Stop()
	{
		if (!impl_->running)
			return;
		if (impl_->root)
			impl_->AbortSubtree(*impl_->root);
		impl_->running = false;
		impl_->Destroy();
		auto& list = RunningList();
		list.erase(std::remove(list.begin(), list.end(), this), list.end());
	}

	void BehaviorTreeComponent::Restart()
	{
		Stop();
		Start();
	}

	bool BehaviorTreeComponent::IsRunning() const
	{
		return impl_->running;
	}

	Blackboard& BehaviorTreeComponent::GetBlackboard()
	{
		return impl_->blackboard;
	}

	const Blackboard& BehaviorTreeComponent::GetBlackboard() const
	{
		return impl_->blackboard;
	}

	std::vector<int> BehaviorTreeComponent::GetActiveNodeIds() const
	{
		std::vector<const RNode*> path;
		impl_->ActivePath(path);
		std::vector<int> ids;
		for (const RNode* n : path)
			ids.push_back(n->desc->id);
		return ids;
	}

	std::vector<std::string> BehaviorTreeComponent::GetActiveNodeNames() const
	{
		std::vector<const RNode*> path;
		impl_->ActivePath(path);
		std::vector<std::string> names;
		for (const RNode* n : path)
			names.push_back(n->desc->name.empty() ? n->desc->type : n->desc->name);
		return names;
	}

	const std::string& BehaviorTreeComponent::GetLoadedTree() const
	{
		return impl_->loaded;
	}

	std::vector<BehaviorTreeComponent*> BehaviorTreeComponent::GetRunning(const std::string& asset)
	{
		std::vector<BehaviorTreeComponent*> out;
		for (BehaviorTreeComponent* c : RunningList())
			if (asset.empty() || c->GetLoadedTree() == asset)
				out.push_back(c);
		return out;
	}

	void BehaviorTreeComponent::BeginPlay()
	{
		if (behavior_tree != impl_->loaded)
			Load(behavior_tree);
		if (start_on_begin)
			Start();
	}

	void BehaviorTreeComponent::Update(float)
	{
		// Champ change (JS, C++) : rechargement.
		if (behavior_tree != impl_->loaded)
			Load(behavior_tree);
	}

	void BehaviorTreeComponent::Tick(float dt)
	{
		LYNX_PROFILE_SCOPE("Behavior Tree");
		impl_->Tick(dt);
	}

	void BehaviorTreeComponent::EndPlay()
	{
		Stop();
	}
}
