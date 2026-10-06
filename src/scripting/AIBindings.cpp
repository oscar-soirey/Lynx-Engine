/*
 * Behavior Trees cote JavaScript : noeuds ecrits en JS et Blackboard.
 *
 *     class Attack extends BTTask {              // noeud "Script", class = Attack
 *         Execute() {                            // au demarrage de la tache
 *             this.owner.getComponent("AnimationSprite").setTrigger("attack");
 *             this.t = 0;
 *             return BT.Running;                 // ou true / false / BT.Success...
 *         }
 *         Tick(dt) { this.t += dt; return this.t > this.params.duration ? BT.Success : BT.Running; }
 *         Abort() { }                            // interrompue (abort, Stop)
 *     }
 *
 *     class CanSeePlayer extends BTDecorator {
 *         Check() { return this.blackboard.has("target"); }
 *     }
 *
 *     class Perception extends BTService {       // tourne tant que son noeud est actif
 *         Activated() { }
 *         Tick(dt) { this.blackboard.set("target", Level.find("player")); }
 *         Deactivated() { }
 *     }
 *
 * this.owner : l'acteur ; this.blackboard : sa memoire (get / set / has / clear) ;
 * this.params : les parametres du noeud dans l'editeur ; this.name : la classe.
 */

#include "Private/ScriptInternal.h"
#include "Private/ScriptSystem.h"
#include "Scripting.h"

#include "../core/Filesystem.h"
#include "../gameplay/Actor.h"
#include "../gameplay/BehaviorTree.h"
#include "../gameplay/Private/ECS.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lynx::script_detail
{
	namespace
	{
		using scripting::AINodeKind;

		constexpr const char* kLogPrefix = "[script] ";
		constexpr int kKindCount = 3;
		const char* const kBaseNames[kKindCount] = { "BTTask", "BTDecorator", "BTService" };

		JSClassID g_blackboard_class = 0;

		struct AIClass
		{
			AINodeKind kind = AINodeKind::Task;
			JSValue ctor = JS_UNDEFINED;
			std::string file;
		};

		struct AINode
		{
			JSValue obj = JS_UNDEFINED;
			std::string class_name;
		};

		/** Proprietes posees par le constructeur de base (super()). */
		struct Pending
		{
			bool active = false;
			Actor* owner = nullptr;
			std::string class_name;
			const std::vector<std::pair<std::string, std::string>>* params = nullptr;
		};

		struct State
		{
			JSValue base_ctor[kKindCount] = { JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };
			JSValue base_proto[kKindCount] = { JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };
			JSValue blackboard_proto = JS_UNDEFINED;

			std::unordered_map<std::string, AIClass> classes;
			std::unordered_map<uint32_t, AINode> nodes;
			uint32_t next_handle = 1;
			Pending pending;
		};

		State* s = nullptr;

		State& S()
		{
			if (!s)
				s = new State();
			return *s;
		}

		void* Encode(uint32_t entity) { return reinterpret_cast<void*>(static_cast<uintptr_t>(entity) + 1); }

		bool Decode(void* p, uint32_t& entity)
		{
			const uintptr_t v = reinterpret_cast<uintptr_t>(p);
			if (v == 0)
				return false;
			entity = static_cast<uint32_t>(v - 1);
			return true;
		}

		void DefFunc(JSContext* ctx, JSValueConst obj, const char* name, JSCFunction* fn, int length)
		{
			JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, length));
		}

		/** "3" -> 3, "true" -> true, "a" -> "a" (parametres de l'editeur). */
		JSValue ParamToJS(JSContext* ctx, const std::string& text)
		{
			if (text == "true")
				return JS_TRUE;
			if (text == "false")
				return JS_FALSE;
			if (!text.empty())
			{
				char* end = nullptr;
				const double v = std::strtod(text.c_str(), &end);
				if (end && *end == '\0' && !std::isspace(static_cast<unsigned char>(text[0])))
					return JS_NewFloat64(ctx, v);
			}
			return JS_NewString(ctx, text.c_str());
		}


		// =====================================================================
		// Blackboard (objet JS : entite de l'acteur ; la memoire est celle de
		// son composant AI)
		// =====================================================================

		Blackboard* BlackboardOf(JSValueConst this_val, Actor** actor_out = nullptr)
		{
			uint32_t entity = 0;
			if (!Decode(JS_GetOpaque(this_val, g_blackboard_class), entity))
				return nullptr;
			Actor* actor = ecs::GetActor(entity);
			if (actor_out)
				*actor_out = actor;
			if (!actor)
				return nullptr;
			auto* ai = actor->GetComponent<BehaviorTreeComponent>();
			return ai ? &ai->GetBlackboard() : nullptr;
		}

		JSValue ThrowNoBlackboard(JSContext* ctx)
		{
			return JS_ThrowReferenceError(ctx, "no blackboard : the actor is destroyed or has no AI component");
		}

		JSValue ValueToJS(JSContext* ctx, const BlackboardValue& v)
		{
			using T = BlackboardValue::Type;
			switch (v.type)
			{
			case T::Bool: return JS_NewBool(ctx, v.b);
			case T::Int: return JS_NewInt32(ctx, v.i);
			case T::Float: return JS_NewFloat64(ctx, v.f);
			case T::String: return JS_NewString(ctx, v.s.c_str());
			case T::Vector:
			{
				const float f[3] = { v.v.x, v.v.y, v.v.z };
				return NewPlainVec(ctx, f, 3);
			}
			case T::Actor:
			{
				Actor* a = ecs::GetActor(v.entity);
				return a ? ActorObject(ctx, a) : JS_NULL;
			}
			default: return JS_UNDEFINED;
			}
		}

		JSValue BBGet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			Blackboard* bb = BlackboardOf(this_val);
			if (!bb)
				return ThrowNoBlackboard(ctx);
			const std::string key = argc > 0 ? ToStdString(ctx, argv[0]) : std::string();
			if (!bb->IsSet(key))
				return argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;
			return ValueToJS(ctx, *bb->Get(key));
		}

		JSValue BBSet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			Blackboard* bb = BlackboardOf(this_val);
			if (!bb)
				return ThrowNoBlackboard(ctx);
			if (argc < 1)
				return JS_ThrowTypeError(ctx, "blackboard.set(key, value)");

			const std::string key = ToStdString(ctx, argv[0]);
			JSValueConst v = argc > 1 ? argv[1] : JS_UNDEFINED;
			const std::string declared = bb->GetDeclaredType(key);

			if (JS_IsUndefined(v) || JS_IsNull(v))
				bb->Clear(key);
			else if (JS_IsBool(v))
				bb->SetBool(key, JS_ToBool(ctx, v) > 0);
			else if (JS_IsNumber(v))
			{
				double d = 0.0;
				JS_ToFloat64(ctx, &d, v);
				if (declared == "int")
					bb->SetInt(key, static_cast<int>(d));
				else if (declared == "bool")
					bb->SetBool(key, d != 0.0);
				else
					bb->SetFloat(key, static_cast<float>(d));
			}
			else if (JS_IsString(v))
			{
				const std::string text = ToStdString(ctx, v);
				if (declared.empty() || declared == "string")
					bb->SetString(key, text);
				else
					bb->SetFromText(key, text);
			}
			else if (Actor* a = ActorFromJS(v))
				bb->SetActor(key, a);
			else if (JS_IsObject(v))
			{
				float f[3] = { 0.f, 0.f, 0.f };
				if (!ReadFloats(ctx, v, f, 3) && !ReadFloats(ctx, v, f, 2))
					return JS_ThrowTypeError(ctx, "blackboard.set : unsupported value for '%s'", key.c_str());
				bb->SetVector(key, vec3(f[0], f[1], f[2]));
			}
			else
				return JS_ThrowTypeError(ctx, "blackboard.set : unsupported value for '%s'", key.c_str());

			return JS_UNDEFINED;
		}

		JSValue BBHas(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			Blackboard* bb = BlackboardOf(this_val);
			if (!bb)
				return JS_FALSE;
			return JS_NewBool(ctx, argc > 0 && bb->IsSet(ToStdString(ctx, argv[0])));
		}

		JSValue BBClear(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			Blackboard* bb = BlackboardOf(this_val);
			if (!bb)
				return ThrowNoBlackboard(ctx);
			if (argc > 0)
				bb->Clear(ToStdString(ctx, argv[0]));
			else
				bb->ClearAll();
			return JS_UNDEFINED;
		}

		JSValue BBKeys(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			JSValue array = JS_NewArray(ctx);
			if (Blackboard* bb = BlackboardOf(this_val))
			{
				uint32_t i = 0;
				for (const std::string& k : bb->GetKeys())
					JS_SetPropertyUint32(ctx, array, i++, JS_NewString(ctx, k.c_str()));
			}
			return array;
		}

		JSValue BBValid(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int)
		{
			return JS_NewBool(ctx, BlackboardOf(this_val) != nullptr);
		}

		JSValue BBOwner(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int)
		{
			Actor* actor = nullptr;
			BlackboardOf(this_val, &actor);
			return actor ? ActorObject(ctx, actor) : JS_NULL;
		}

		JSValue BBToString(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			Blackboard* bb = BlackboardOf(this_val);
			if (!bb)
				return JS_NewString(ctx, "[Blackboard (invalid)]");
			std::string out = "[Blackboard";
			for (const std::string& k : bb->GetKeys())
				out += " " + k + "=" + (bb->IsSet(k) ? bb->Get(k)->ToString() : std::string("<unset>"));
			out += "]";
			return JS_NewString(ctx, out.c_str());
		}


		// =====================================================================
		// BTTask / BTDecorator / BTService
		// =====================================================================

		/** super() des classes JS : objet du prototype de la classe + owner, params... */
		JSValue BaseConstructor(JSContext* ctx, JSValueConst new_target, int, JSValueConst*, int magic)
		{
			State& st = S();
			JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
			if (JS_IsException(proto))
				return proto;
			if (!JS_IsObject(proto))
			{
				JS_FreeValue(ctx, proto);
				proto = JS_DupValue(ctx, st.base_proto[magic]);
			}
			JSValue obj = JS_NewObjectProto(ctx, proto);
			JS_FreeValue(ctx, proto);
			if (JS_IsException(obj))
				return obj;

			// Hors d'un arbre (new Attack() dans un script) : pas d'acteur.
			const Pending& p = st.pending;
			JS_SetPropertyStr(ctx, obj, "owner", p.active && p.owner ? ActorObject(ctx, p.owner) : JS_NULL);
			JS_SetPropertyStr(ctx, obj, "blackboard",
				p.active && p.owner ? BlackboardObject(ctx, p.owner) : JS_NULL);
			JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, p.active ? p.class_name.c_str() : ""));

			JSValue params = JS_NewObject(ctx);
			if (p.active && p.params)
				for (const auto& [name, value] : *p.params)
					if (name != "class" && name != "interval")
						JS_SetPropertyStr(ctx, params, name.c_str(), ParamToJS(ctx, value));
			JS_SetPropertyStr(ctx, obj, "params", params);
			return obj;
		}

		int KindOfConstructor(JSContext* ctx, JSValueConst value)
		{
			if (!s || !JS_IsFunction(ctx, value))
				return -1;

			JSValue p = JS_GetPropertyStr(ctx, value, "prototype");
			int found = -1;
			for (int guard = 0; guard < 64 && JS_IsObject(p) && found < 0; ++guard)
			{
				JSValue next = JS_GetPrototype(ctx, p);
				if (JS_IsObject(next))
					for (int k = 0; k < kKindCount; ++k)
						if (JS_VALUE_GET_PTR(next) == JS_VALUE_GET_PTR(s->base_proto[k]))
							found = k;
				JS_FreeValue(ctx, p);
				p = next;
			}
			JS_FreeValue(ctx, p);
			return found;
		}

		/** Valeur rendue par un noeud -> 0 succes, 1 echec, 2 en cours. */
		int ResultOf(JSContext* ctx, JSValueConst r)
		{
			if (JS_IsUndefined(r) || JS_IsNull(r))
				return 0;
			if (JS_IsBool(r))
				return JS_ToBool(ctx, r) > 0 ? 0 : 1;
			if (JS_IsString(r))
			{
				std::string t = ToStdString(ctx, r);
				std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (t == "running")
					return 2;
				if (t == "failure" || t == "failed" || t == "fail")
					return 1;
				return 0;
			}
			if (JS_IsNumber(r))
			{
				int v = 0;
				JS_ToInt32(ctx, &v, r);
				return v == 1 || v == 2 ? v : 0;
			}
			return JS_ToBool(ctx, r) > 0 ? 0 : 1;
		}

		/** Appel suivi : exception -> echec (affichee). */
		int CallMethod(JSContext* ctx, JSValueConst obj, const char* method, float dt, bool* found, const std::string& where)
		{
			JSValue fn = JS_GetPropertyStr(ctx, obj, method);
			if (!JS_IsFunction(ctx, fn))
			{
				JS_FreeValue(ctx, fn);
				if (found)
					*found = false;
				return 0;
			}
			if (found)
				*found = true;

			JSValue self = JS_DupValue(ctx, obj);    // l'objet peut etre detruit pendant l'appel
			JSValue arg = JS_NewFloat64(ctx, dt);
			EnterCall();
			JSValue r = JS_Call(ctx, fn, self, 1, &arg);
			LeaveCall();

			int result = 1;
			if (JS_IsException(r))
				LogException(ctx, where + "." + method);
			else
				result = ResultOf(ctx, r);

			JS_FreeValue(ctx, r);
			JS_FreeValue(ctx, self);
			JS_FreeValue(ctx, fn);
			RunPendingJobs();
			return result;
		}
	}


	// =========================================================================
	// Interface interne (ScriptSystem.cpp, ComponentBindings.cpp)
	// =========================================================================

	JSValue BlackboardObject(JSContext* ctx, Actor* actor)
	{
		if (!ctx || !actor || !s)
			return JS_NULL;
		JSValue obj = JS_NewObjectProtoClass(ctx, s->blackboard_proto, g_blackboard_class);
		if (!JS_IsException(obj))
			JS_SetOpaque(obj, Encode(actor->GetEntity()));
		return obj;
	}

	void RegisterAIGlobals(JSContext* ctx)
	{
		JSRuntime* rt = JS_GetRuntime(ctx);
		State& st = S();

		g_blackboard_class = 0;
		JS_NewClassID(rt, &g_blackboard_class);
		JSClassDef bb_def{};
		bb_def.class_name = "Blackboard";
		JS_NewClass(rt, g_blackboard_class, &bb_def);

		JSValue global = JS_GetGlobalObject(ctx);

		// ---- Blackboard --------------------------------------------------------
		st.blackboard_proto = JS_NewObject(ctx);
		DefFunc(ctx, st.blackboard_proto, "get", BBGet, 2);
		DefFunc(ctx, st.blackboard_proto, "set", BBSet, 2);
		DefFunc(ctx, st.blackboard_proto, "has", BBHas, 1);
		DefFunc(ctx, st.blackboard_proto, "isSet", BBHas, 1);
		DefFunc(ctx, st.blackboard_proto, "clear", BBClear, 1);
		DefFunc(ctx, st.blackboard_proto, "keys", BBKeys, 0);
		DefFunc(ctx, st.blackboard_proto, "toString", BBToString, 0);
		DefGetSet(ctx, st.blackboard_proto, "valid", BBValid, nullptr, 0);
		DefGetSet(ctx, st.blackboard_proto, "owner", BBOwner, nullptr, 0);
		JS_SetClassProto(ctx, g_blackboard_class, JS_DupValue(ctx, st.blackboard_proto));

		// ---- BTTask / BTDecorator / BTService -------------------------------
		for (int k = 0; k < kKindCount; ++k)
		{
			st.base_proto[k] = JS_NewObject(ctx);
			st.base_ctor[k] = JS_NewCFunctionMagic(ctx, BaseConstructor, kBaseNames[k], 0, JS_CFUNC_constructor_magic, k);
			JS_SetConstructor(ctx, st.base_ctor[k], st.base_proto[k]);
			JS_SetPropertyStr(ctx, global, kBaseNames[k], JS_DupValue(ctx, st.base_ctor[k]));
		}

		// ---- BT.Success / Failure / Running ----------------------------------
		JSValue bt = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, bt, "Success", JS_NewString(ctx, "success"));
		JS_SetPropertyStr(ctx, bt, "Failure", JS_NewString(ctx, "failure"));
		JS_SetPropertyStr(ctx, bt, "Running", JS_NewString(ctx, "running"));
		JS_SetPropertyStr(ctx, global, "BT", bt);

		JS_FreeValue(ctx, global);
	}

	void ShutdownAIBindings(JSContext* ctx)
	{
		if (!s)
			return;

		for (auto& [handle, node] : s->nodes)
			JS_FreeValue(ctx, node.obj);
		for (auto& [name, cls] : s->classes)
			JS_FreeValue(ctx, cls.ctor);
		for (int k = 0; k < kKindCount; ++k)
		{
			JS_FreeValue(ctx, s->base_ctor[k]);
			JS_FreeValue(ctx, s->base_proto[k]);
		}
		JS_FreeValue(ctx, s->blackboard_proto);

		delete s;
		s = nullptr;
	}

	void ForgetAIClasses(JSContext* ctx)
	{
		if (!s)
			return;

		JSValue global = JS_GetGlobalObject(ctx);
		for (const auto& [name, cls] : s->classes)
		{
			JSAtom atom = JS_NewAtom(ctx, name.c_str());
			JS_DeleteProperty(ctx, global, atom, 0);
			JS_FreeAtom(ctx, atom);
		}
		JS_FreeValue(ctx, global);
	}

	bool IsAIClassConstructor(JSContext* ctx, JSValueConst value)
	{
		return KindOfConstructor(ctx, value) >= 0;
	}

	void RegisterAIClass(JSContext* ctx, const std::string& name, JSValueConst ctor, const std::string& file)
	{
		const int kind = KindOfConstructor(ctx, ctor);
		if (kind < 0)
			return;

		State& st = S();
		auto& cls = st.classes[name];
		JS_FreeValue(ctx, cls.ctor);
		cls.ctor = JS_DupValue(ctx, ctor);
		cls.kind = static_cast<AINodeKind>(kind);
		cls.file = file;

		JSValue global = JS_GetGlobalObject(ctx);
		JS_SetPropertyStr(ctx, global, name.c_str(), JS_DupValue(ctx, ctor));
		JS_FreeValue(ctx, global);

		// Rechargement : les noeuds vivants de cette classe prennent les nouvelles methodes.
		JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");
		for (auto& [handle, node] : st.nodes)
			if (node.class_name == name)
				JS_SetPrototype(ctx, node.obj, proto);
		JS_FreeValue(ctx, proto);

		std::cout << kLogPrefix << kBaseNames[kind] << " class " << name << " (" << file << ")" << std::endl;
	}
}


// ============================================================================
// Interface moteur (Private/ScriptSystem.h)
// ============================================================================

namespace lynx::scripting
{
	using namespace script_detail;

	bool IsAIClass(const std::string& name, AINodeKind kind)
	{
		if (!s)
			return false;
		auto it = s->classes.find(name);
		return it != s->classes.end() && it->second.kind == kind;
	}

	uint32_t CreateAINode(AINodeKind kind, const std::string& class_name, Actor* owner,
	                      const std::vector<std::pair<std::string, std::string>>& params)
	{
		JSContext* ctx = Context();
		if (!ctx || !s || class_name.empty())
		{
			if (!class_name.empty())
				std::cout << kLogPrefix << "behavior tree : no script runtime for class " << class_name << std::endl;
			return 0;
		}

		auto it = s->classes.find(class_name);
		if (it == s->classes.end() || it->second.kind != kind)
		{
			std::cout << kLogPrefix << "behavior tree : \"" << class_name << "\" is not a JavaScript class extends "
			          << kBaseNames[static_cast<int>(kind)] << std::endl;
			return 0;
		}

		Pending saved = s->pending;
		s->pending = Pending{ true, owner, class_name, &params };

		JSValue ctor = JS_DupValue(ctx, it->second.ctor);
		EnterCall();
		JSValue obj = JS_CallConstructor(ctx, ctor, 0, nullptr);
		LeaveCall();
		JS_FreeValue(ctx, ctor);

		if (s)
			s->pending = saved;

		if (JS_IsException(obj))
		{
			LogException(ctx, class_name + ".constructor");
			return 0;
		}
		if (!s)
		{
			JS_FreeValue(ctx, obj);
			return 0;
		}

		const uint32_t handle = s->next_handle++;
		s->nodes[handle] = AINode{ obj, class_name };
		RunPendingJobs();
		return handle;
	}

	void DestroyAINode(uint32_t handle)
	{
		JSContext* ctx = Context();
		if (!s || !handle)
			return;
		auto it = s->nodes.find(handle);
		if (it == s->nodes.end())
			return;
		JSValue obj = it->second.obj;
		s->nodes.erase(it);
		if (ctx)
			JS_FreeValue(ctx, obj);
	}

	int CallAINode(uint32_t handle, const char* method, float dt, bool* found)
	{
		if (found)
			*found = false;

		JSContext* ctx = Context();
		if (!ctx || !s || !method)
			return 1;
		auto it = s->nodes.find(handle);
		if (it == s->nodes.end())
			return 1;

		// Copie : l'appel peut detruire le noeud (Stop depuis le JS...).
		const std::string where = it->second.class_name;
		JSValue obj = JS_DupValue(ctx, it->second.obj);
		const int r = CallMethod(ctx, obj, method, dt, found, where);
		JS_FreeValue(ctx, obj);
		return r;
	}

	int CallActorTask(Actor* self, const char* function, float dt, bool* found)
	{
		if (found)
			*found = false;

		JSContext* ctx = Context();
		if (!ctx || !self || !function || !*function)
			return 1;

		bool f = false;
		JSValue arg = JS_NewFloat64(ctx, dt);
		JSValue r = CallActorFunction(ctx, self, function, 1, &arg, &f);
		const int result = JS_IsException(r) ? 1 : ResultOf(ctx, r);
		JS_FreeValue(ctx, r);
		RunPendingJobs();

		if (found)
			*found = f;
		return f ? result : 1;
	}
}


// ============================================================================
// API publique (Scripting.h)
// ============================================================================

namespace lynx
{
	namespace
	{
		bool IsIdentChar(char c)
		{
			return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
		}

		/** Paires (classe, base) des "class X extends Y" d'un fichier. */
		void ScanExtends(const std::string& code, std::vector<std::pair<std::string, std::string>>& out)
		{
			size_t pos = 0;
			while ((pos = code.find("class", pos)) != std::string::npos)
			{
				const bool start_ok = pos == 0 || !IsIdentChar(code[pos - 1]);
				size_t i = pos + 5;
				pos = i;
				if (!start_ok || i >= code.size() || !std::isspace(static_cast<unsigned char>(code[i])))
					continue;

				auto skip_ws = [&]() { while (i < code.size() && std::isspace(static_cast<unsigned char>(code[i]))) ++i; };
				auto word = [&]() { const size_t b = i; while (i < code.size() && IsIdentChar(code[i])) ++i; return code.substr(b, i - b); };

				skip_ws();
				const std::string name = word();
				skip_ws();
				if (name.empty() || code.compare(i, 7, "extends") != 0)
					continue;
				i += 7;
				skip_ws();
				const std::string base = word();
				if (!base.empty())
					out.emplace_back(name, base);
			}
		}
	}

	std::vector<std::string> GetScriptClassesOf(const char* base)
	{
		std::set<std::string> result;
		if (!base || !*base)
			return {};

		// Classes chargees (le jeu tourne ou a tourne).
		if (script_detail::s)
		{
			for (int k = 0; k < script_detail::kKindCount; ++k)
				if (std::string(script_detail::kBaseNames[k]) == base)
					for (const auto& [name, cls] : script_detail::s->classes)
						if (static_cast<int>(cls.kind) == k)
							result.insert(name);
		}

		// Fichiers : aussi les classes pas encore chargees, et l'heritage indirect
		// (class Boss extends Enemy, class Enemy extends BTTask).
		std::vector<std::pair<std::string, std::string>> pairs;
		for (const std::string& path : fs::ListFiles("", true))
		{
			if (path.size() <= 3 || path.compare(path.size() - 3, 3, ".js") != 0)
				continue;
			const auto data = fs::ReadBinary(path);
			ScanExtends(std::string(reinterpret_cast<const char*>(data.data()), data.size()), pairs);
		}

		std::set<std::string> bases{ base };
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (const auto& [name, parent] : pairs)
				if (bases.count(parent) && !bases.count(name))
				{
					bases.insert(name);
					result.insert(name);
					changed = true;
				}
		}

		return { result.begin(), result.end() };
	}
}
