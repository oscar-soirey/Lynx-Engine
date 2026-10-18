// =============================================================================
// Voxel Assembly : the runtime (see VoxelAssembly.h)
// -----------------------------------------------------------------------------
//   1. Program   : a .vasm read into trees of blocks (Node)
//   2. Value     : what a block gives (number, condition, text, actor)
//   3. Runner    : the scripts of an actor (threads, frames, waits)
//   4. Component : VasmComponent, one per actor of a Voxel Assembly class
//   5. Classes   : the factory, the events of the engine
// No JavaScript : the engine reads and runs the blocks itself.
// =============================================================================

#include "VoxelAssembly.h"
#include "VasmKinds.h"

#include "../core/Engine.h"
#include "../core/Factory.h"
#include "../core/Filesystem.h"
#include "../core/Level.h"
#include "../gameplay/Actor.h"
#include "../gameplay/BoxColliderComponent.h"
#include "../gameplay/CameraShake.h"
#include "../gameplay/Components.h"
#include "../gameplay/Humanoid.h"
#include "../gameplay/Input.h"
#include "../gameplay/SoundSourceComponent.h"
#include "../gameplay/SpriteComponents.h"
#include "../gameplay/Private/ECS.h"
#include "../widgets/UserWidget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace lynx::vasm
{
	namespace
	{
		constexpr const char* kLog = "[VASM] ";

		// =====================================================================
		// 1. Program
		// =====================================================================

		// Kinds made for a file (not in kKinds).
		enum : int
		{
			kOpUnknown = -1,
			kOpVar = 10000,     // var.<id>
			kOpCall,            // call.<fid>
			kOpCallValue,       // callv.<fid>
			kOpArg,             // arg.<fid>.<n>
		};

		struct Value
		{
			enum class T : unsigned char { None, Number, Bool, Text, Actor };
			T t = T::None;
			double n = 0.0;
			bool b = false;
			std::string s;
			uint32_t actor = ecs::kNullEntity;

			static Value Num(double v) { Value x; x.t = T::Number; x.n = v; return x; }
			static Value Bool(bool v) { Value x; x.t = T::Bool; x.b = v; return x; }
			static Value Text(std::string v) { Value x; x.t = T::Text; x.s = std::move(v); return x; }
			static Value Of(Actor* a)
			{
				Value x;
				if (a)
				{
					x.t = T::Actor;
					x.actor = a->GetEntity();
				}
				return x;
			}
		};

		struct Node
		{
			int op = kOpUnknown;
			std::string kind;
			std::vector<std::string> values;
			std::vector<Value> typed;                              // the typed values (number or text)
			std::vector<std::unique_ptr<Node>> inputs;             // one per slot, null when typed
			std::vector<std::vector<std::unique_ptr<Node>>> bodies;
			int ref = -1;   // variable / function / input index
			int arg = -1;   // var.set, var.change, ctl.for : an input of the function with that name
		};
		using Chain = std::vector<std::unique_ptr<Node>>;

		struct VarDef
		{
			std::string id, type = "number", name, value = "0";
			bool global = false;
		};

		struct Function
		{
			std::string id, name;
			std::vector<std::string> inputs;
			Chain body;
		};

		struct Script
		{
			std::unique_ptr<Node> hat;
			Chain body;
		};

		struct Program
		{
			std::string file, class_name, parent = "Actor";
			std::vector<VarDef> vars;
			std::vector<Function> functions;
			std::vector<Script> scripts;
			bool uses_touch = false;
		};

		// ---- reading the scripts of Raven : (script x y (block "kind" ("v") (in 0 ...) (body 0 ...)) ...)

		struct Reader
		{
			const char* c;
			bool ok = true;

			void Skip() { while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') ++c; }
			bool Expect(char ch)
			{
				Skip();
				if (*c != ch) { ok = false; return false; }
				++c;
				return true;
			}
			bool Word(const char* w)
			{
				Skip();
				const size_t n = std::strlen(w);
				if (std::strncmp(c, w, n) != 0)
					return false;
				c += n;
				return true;
			}
			std::string Str()
			{
				Skip();
				std::string out;
				if (*c != '"') { ok = false; return out; }
				++c;
				while (*c && *c != '"')
				{
					if (*c == '\\' && c[1])
					{
						++c;
						if (*c == 'n') { out += '\n'; ++c; continue; }
					}
					out += *c++;
				}
				if (*c != '"') { ok = false; return out; }
				++c;
				return out;
			}
			double Number()
			{
				Skip();
				char* end = nullptr;
				const double v = std::strtod(c, &end);
				if (end == c) { ok = false; return 0.0; }
				c = end;
				return v;
			}
		};

		std::unique_ptr<Node> ReadBlock(Reader& r)
		{
			if (!r.Expect('(') || !r.Word("block")) { r.ok = false; return nullptr; }
			auto node = std::make_unique<Node>();
			node->kind = r.Str();
			if (!r.ok || !r.Expect('('))
				return nullptr;
			for (;;)
			{
				r.Skip();
				if (*r.c == ')') { ++r.c; break; }
				node->values.push_back(r.Str());
				if (!r.ok)
					return nullptr;
			}
			node->inputs.resize(node->values.size());
			for (;;)
			{
				r.Skip();
				if (*r.c == ')') { ++r.c; return node; }
				if (!r.Expect('('))
					return nullptr;
				if (r.Word("in"))
				{
					const int slot = static_cast<int>(r.Number());
					auto child = r.ok ? ReadBlock(r) : nullptr;
					if (!child || !r.Expect(')'))
						return nullptr;
					if (slot >= 0 && slot < 64)
					{
						if (static_cast<int>(node->inputs.size()) <= slot)
							node->inputs.resize(slot + 1);
						node->inputs[slot] = std::move(child);
					}
				}
				else if (r.Word("body"))
				{
					const int mouth = static_cast<int>(r.Number());
					if (!r.ok || mouth < 0 || mouth > 16)
						return nullptr;
					if (static_cast<int>(node->bodies.size()) <= mouth)
						node->bodies.resize(mouth + 1);
					for (;;)
					{
						r.Skip();
						if (*r.c == ')') { ++r.c; break; }
						auto child = ReadBlock(r);
						if (!child)
							return nullptr;
						node->bodies[mouth].push_back(std::move(child));
					}
				}
				else
				{
					r.ok = false;
					return nullptr;
				}
			}
		}

		// Each script : its blocks from the top.
		bool ReadScripts(const std::string& text, std::vector<Chain>& scripts)
		{
			Reader r{ text.c_str() };
			for (;;)
			{
				r.Skip();
				if (!*r.c)
					return true;
				if (!r.Expect('(') || !r.Word("script"))
					return false;
				r.Number();
				r.Number();
				if (!r.ok)
					return false;
				Chain chain;
				for (;;)
				{
					r.Skip();
					if (*r.c == ')') { ++r.c; break; }
					auto block = ReadBlock(r);
					if (!block)
						return false;
					chain.push_back(std::move(block));
				}
				scripts.push_back(std::move(chain));
			}
		}

		bool ParseNumber(const std::string& text, double& out)
		{
			size_t a = text.find_first_not_of(" \t");
			if (a == std::string::npos)
				return false;
			const char* begin = text.c_str() + a;
			char* end = nullptr;
			out = std::strtod(begin, &end);
			if (end == begin)
				return false;
			while (*end == ' ' || *end == '\t')
				++end;
			return *end == '\0' && std::isfinite(out);
		}

		int FindVar(const Program& p, const std::string& name)
		{
			for (size_t i = 0; i < p.vars.size(); ++i)
				if (p.vars[i].name == name)
					return static_cast<int>(i);
			return -1;
		}

		int FindVarById(const Program& p, const std::string& id)
		{
			for (size_t i = 0; i < p.vars.size(); ++i)
				if (p.vars[i].id == id)
					return static_cast<int>(i);
			return -1;
		}

		int FindFunction(const Program& p, const std::string& id)
		{
			for (size_t i = 0; i < p.functions.size(); ++i)
				if (p.functions[i].id == id)
					return static_cast<int>(i);
			return -1;
		}

		int FindFunctionByName(const Program& p, const std::string& name)
		{
			for (size_t i = 0; i < p.functions.size(); ++i)
				if (p.functions[i].name == name || p.functions[i].id == name)
					return static_cast<int>(i);
			return -1;
		}

		// Kinds, variables, functions and inputs, once (not at each run).
		void Resolve(Program& p, Node& n, const Function* fn)
		{
			n.op = FindKind(n.kind);
			if (n.op < 0)
			{
				if (n.kind.rfind("var.", 0) == 0)
				{
					n.op = kOpVar;
					n.ref = FindVarById(p, n.kind.substr(4));
				}
				else if (n.kind.rfind("callv.", 0) == 0)
				{
					n.op = kOpCallValue;
					n.ref = FindFunction(p, n.kind.substr(6));
				}
				else if (n.kind.rfind("call.", 0) == 0)
				{
					n.op = kOpCall;
					n.ref = FindFunction(p, n.kind.substr(5));
				}
				else if (n.kind.rfind("arg.", 0) == 0)
				{
					n.op = kOpArg;
					const size_t dot = n.kind.rfind('.');
					const std::string fid = n.kind.substr(4, dot > 4 ? dot - 4 : 0);
					n.ref = fn && fn->id == fid ? std::atoi(n.kind.c_str() + dot + 1) : -1;
				}
			}

			if (n.op == KindIndex("var.set") || n.op == KindIndex("var.change") || n.op == KindIndex("ctl.for"))
			{
				const std::string name = n.values.empty() ? std::string() : n.values[0];
				n.ref = FindVar(p, name);
				if (fn)
					for (size_t i = 0; i < fn->inputs.size(); ++i)
						if (fn->inputs[i] == name)
							n.arg = static_cast<int>(i);
			}
			if (n.op == KindIndex("se.touching") || n.op == KindIndex("ev.overlap") || n.op == KindIndex("ev.overlap_end") ||
			    n.op == KindIndex("ev.hit"))
				p.uses_touch = true;

			n.typed.clear();
			for (const std::string& v : n.values)
			{
				double d = 0.0;
				n.typed.push_back(ParseNumber(v, d) ? Value::Num(d) : Value::Text(v));
			}
			if (n.inputs.size() < n.values.size())
				n.inputs.resize(n.values.size());
			if (n.bodies.size() < 2)
				n.bodies.resize(2);

			for (auto& in : n.inputs)
				if (in)
					Resolve(p, *in, fn);
			for (auto& body : n.bodies)
				for (auto& b : body)
					Resolve(p, *b, fn);
		}

		std::string Trim(const std::string& s)
		{
			const size_t a = s.find_first_not_of(" \t\r\n");
			if (a == std::string::npos)
				return {};
			const size_t b = s.find_last_not_of(" \t\r\n");
			return s.substr(a, b - a + 1);
		}

		bool ParseProgram(const std::string& text, const std::string& file, Program& p, std::string& error)
		{
			p = Program();
			p.file = file;
			std::istringstream in(text);
			std::string line;
			if (!std::getline(in, line) || line.rfind("lynx-vasm", 0) != 0)
			{
				error = file + " : not a Voxel Assembly file";
				return false;
			}

			struct Graph { bool function; std::string id; std::string text; };
			std::vector<Graph> graphs;

			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				std::istringstream words(line);
				std::string key;
				words >> key;
				std::string rest;
				std::getline(words, rest);
				rest = Trim(rest);

				if (key == "class")
					p.class_name = rest;
				else if (key == "extends")
					p.parent = rest.empty() ? std::string("Actor") : rest;
				else if (key == "var")
				{
					std::istringstream w(rest);
					VarDef v;
					std::string scope;
					w >> v.id >> v.type >> scope >> v.name;
					std::getline(w, v.value);
					if (!v.value.empty() && v.value[0] == ' ')
						v.value.erase(0, 1);
					v.global = scope == "global";
					if (!v.name.empty())
						p.vars.push_back(v);
				}
				else if (key == "function")
				{
					std::istringstream w(rest);
					Function f;
					w >> f.id;
					std::getline(w, f.name);
					f.name = Trim(f.name);
					if (!f.id.empty())
						p.functions.push_back(std::move(f));
				}
				else if (key == "input")
				{
					std::istringstream w(rest);
					std::string fid, name;
					w >> fid;
					std::getline(w, name);
					const int f = FindFunction(p, fid);
					if (f >= 0)
						p.functions[f].inputs.push_back(Trim(name));
				}
				else if (key == "graph")
				{
					std::istringstream w(rest);
					std::string type, id;
					w >> type;
					std::getline(w, id);
					Graph g{ type == "function", Trim(id), {} };
					while (std::getline(in, line))
					{
						if (!line.empty() && line.back() == '\r')
							line.pop_back();
						if (line == "end")
							break;
						g.text += line + "\n";
					}
					graphs.push_back(std::move(g));
				}
			}

			if (p.class_name.empty())
			{
				error = file + " : no class name";
				return false;
			}

			for (Graph& g : graphs)
			{
				std::vector<Chain> scripts;
				if (!ReadScripts(g.text, scripts))
				{
					error = file + " : a graph could not be read (" + (g.function ? "function " : "") + g.id + ")";
					return false;
				}
				if (g.function)
				{
					const int f = FindFunction(p, g.id);
					if (f < 0)
						continue;
					for (Chain& chain : scripts)
						if (!chain.empty() && chain[0]->kind == "fn.entry")
						{
							p.functions[f].body.clear();
							for (size_t i = 1; i < chain.size(); ++i)
								p.functions[f].body.push_back(std::move(chain[i]));
						}
				}
				else
				{
					for (Chain& chain : scripts)
					{
						if (chain.empty())
							continue;
						const int k = FindKind(chain[0]->kind);
						if (k < 0 || kKinds[k].shape != Shape::Hat || chain[0]->kind == "fn.entry")
							continue;   // a loose stack : not run
						Script s;
						s.hat = std::move(chain[0]);
						for (size_t i = 1; i < chain.size(); ++i)
							s.body.push_back(std::move(chain[i]));
						p.scripts.push_back(std::move(s));
					}
				}
			}

			for (Function& f : p.functions)
				for (auto& n : f.body)
					Resolve(p, *n, &f);
			for (Script& s : p.scripts)
			{
				Resolve(p, *s.hat, nullptr);
				for (auto& n : s.body)
					Resolve(p, *n, nullptr);
			}
			return true;
		}

		// =====================================================================
		// 2. Value
		// =====================================================================

		Level* CurrentLevel()
		{
			Engine* e = Engine::Get();
			return e ? e->GetCurrentLevel() : nullptr;
		}

		Actor* ActorOf(const Value& v)
		{
			if (v.t == Value::T::Actor)
				return ecs::GetActor(v.actor);
			if (v.t == Value::T::Text && !v.s.empty())
				if (Level* level = CurrentLevel())
					return level->GetActorFromID(v.s.c_str());
			return nullptr;
		}

		double ToNum(const Value& v)
		{
			switch (v.t)
			{
			case Value::T::Number: return v.n;
			case Value::T::Bool: return v.b ? 1.0 : 0.0;
			case Value::T::Text: { double d = 0.0; return ParseNumber(v.s, d) ? d : 0.0; }
			default: return 0.0;
			}
		}

		bool ToBool(const Value& v)
		{
			switch (v.t)
			{
			case Value::T::Bool: return v.b;
			case Value::T::Number: return v.n != 0.0;
			case Value::T::Text: return !v.s.empty() && v.s != "false" && v.s != "0";
			case Value::T::Actor: return ActorOf(v) != nullptr;
			default: return false;
			}
		}

		std::string NumberText(double d)
		{
			if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15)
			{
				char buffer[32];
				std::snprintf(buffer, sizeof(buffer), "%.0f", d);
				return buffer;
			}
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.6g", d);
			return buffer;
		}

		std::string ToText(const Value& v)
		{
			switch (v.t)
			{
			case Value::T::Number: return NumberText(v.n);
			case Value::T::Bool: return v.b ? "true" : "false";
			case Value::T::Text: return v.s;
			case Value::T::Actor:
			{
				Actor* a = ActorOf(v);
				if (!a)
					return "(destroyed)";
				return a->object_id_.empty() ? a->GetTypeName() : a->object_id_;
			}
			default: return {};
			}
		}

		std::string Lower(std::string s)
		{
			for (char& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		bool IsNumeric(const Value& v, double& out)
		{
			if (v.t == Value::T::Number) { out = v.n; return true; }
			if (v.t == Value::T::Bool) return false;
			if (v.t == Value::T::Text) return ParseNumber(v.s, out);
			return false;
		}

		// Scratch : numbers as numbers, texts without the case.
		bool Equal(const Value& a, const Value& b)
		{
			if (a.t == Value::T::Actor || b.t == Value::T::Actor)
				return ActorOf(a) == ActorOf(b);
			double x = 0, y = 0;
			if (IsNumeric(a, x) && IsNumeric(b, y))
				return x == y;
			return Lower(ToText(a)) == Lower(ToText(b));
		}

		bool Less(const Value& a, const Value& b)
		{
			double x = 0, y = 0;
			if (IsNumeric(a, x) && IsNumeric(b, y))
				return x < y;
			return Lower(ToText(a)) < Lower(ToText(b));
		}

		Value FromProperty(const property& prop)
		{
			return std::visit([](auto* ptr) -> Value
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return Value();
				if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
					return Value::Num(static_cast<double>(*ptr));
				else if constexpr (std::is_same_v<T, bool>)
					return Value::Bool(*ptr);
				else if constexpr (std::is_same_v<T, std::string>)
					return Value::Text(*ptr);
				else
					return Value::Text(GetStringByValue(PropVariantType(*ptr)));
			}, prop.property_member);
		}

		bool ToProperty(const property& prop, const Value& v)
		{
			return std::visit([&](auto* ptr) -> bool
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return false;
				if constexpr (std::is_same_v<T, int>)
					*ptr = static_cast<int>(std::lround(ToNum(v)));
				else if constexpr (std::is_same_v<T, float>)
					*ptr = static_cast<float>(ToNum(v));
				else if constexpr (std::is_same_v<T, bool>)
					*ptr = ToBool(v);
				else if constexpr (std::is_same_v<T, std::string>)
					*ptr = ToText(v);
				else
					return false;
				return true;
			}, prop.property_member);
		}

		std::unordered_map<std::string, Value>& Globals()
		{
			static std::unordered_map<std::string, Value> globals;
			return globals;
		}

		Value DefaultOf(const VarDef& v)
		{
			if (v.type == "number")
			{
				double d = 0.0;
				return Value::Num(ParseNumber(v.value, d) ? d : 0.0);
			}
			if (v.type == "bool")
				return Value::Bool(Trim(v.value) == "true");
			if (v.type == "actor")
				return Value();
			return Value::Text(v.value);
		}

		PropVariantType PropertyDefault(const VarDef& v)
		{
			if (v.type == "number")
			{
				double d = 0.0;
				return static_cast<float>(ParseNumber(v.value, d) ? d : 0.0);
			}
			if (v.type == "bool")
				return Trim(v.value) == "true";
			return v.value;
		}

		// =====================================================================
		// 3. Runner
		// =====================================================================

		struct Frame
		{
			const Chain* chain = nullptr;
			size_t pc = 0;
			const Node* loop = nullptr;   // the C block that loops over this chain
			double count = 0.0, end = 0.0;
			bool function = false;
			std::vector<Value> args;
		};

		enum class Wait { None, Time, Until, Glide, Threads };

		struct Thread
		{
			int id = 0;
			int script = -1;              // index in Program::scripts (-1 : a call)
			std::vector<Frame> frames;
			Wait wait = Wait::None;
			double wake = 0.0;
			const Node* until = nullptr;
			float gx0 = 0, gy0 = 0, gx1 = 0, gy1 = 0;
			double gt0 = 0, gd = 0;
			std::vector<std::pair<uint32_t, int>> waiting;   // broadcast and wait : (entity, thread)
			Value other, spawned, result;
			bool done = false;
			bool sync = false;            // runs to its end at once (value of a function, game end)
		};

		enum class Step { Next, Yield, Stay, Pushed, Popped, End };
	}

	// =========================================================================
	// 4. Component
	// =========================================================================

	class VasmComponent : public Component
	{
	public:
		explicit VasmComponent(std::string class_name) : class_name_(std::move(class_name)) {}

		const std::string& ClassName() const { return class_name_; }

		/** Starts the scripts of a hat (OnBeginOverlap...). */
		void Fire(int hat, Actor* other);
		/** "when I receive" ; the threads started. */
		std::vector<int> Receive(const std::string& message);
		bool Running(int thread) const;
		/** A function of this actor by its name (another actor calls it). */
		bool Call(const std::string& name, const std::vector<Value>& args, Value* result, const Value& other);

		bool GetVariable(const std::string& name, Value& out) const;
		bool SetVariable(const std::string& name, const Value& v);

		// Used by the runner.
		void Run(Thread& t);
		Value Eval(const Node& n, Thread& t);
		Step Exec(const Node& n, Thread& t, size_t frame);

		std::shared_ptr<const Program> program_;
		double time_ = 0.0, timer0_ = 0.0, dt_ = 0.0;

	protected:
		void BeginPlay() override;
		void Tick(float dt) override;
		void EndPlay() override;

	private:
		Thread& Start(int script, Actor* other, bool sync = false);
		bool ScriptRunning(int script) const;
		bool ContinueLoop(Thread& t, Frame& f);
		Value Slot(const Node& n, size_t i, Thread& t);
		double Num(const Node& n, size_t i, Thread& t) { return ToNum(Slot(n, i, t)); }
		std::string Text(const Node& n, size_t i, Thread& t) { return ToText(Slot(n, i, t)); }
		bool Cond(const Node& n, size_t i, Thread& t) { return i < n.inputs.size() && n.inputs[i] && ToBool(Eval(*n.inputs[i], t)); }
		Value CallFunction(int f, const std::vector<Value>& args, const Value& other);
		Value* ArgsOf(Thread& t, int index);
		Value ReadVar(const Node& n, Thread& t);
		void WriteVar(const Node& n, Thread& t, const Value& v);
		void Broadcast(const std::string& message, Thread* waiter);
		void StopAll();

		std::string class_name_;
		std::vector<std::unique_ptr<Thread>> threads_;
		std::unordered_map<std::string, Value> actor_vars_;   // variables of type actor (not properties)
		int next_thread_ = 1;
		int running_ = 0;
		bool alive_ = false;
		bool warned_ = false;
	};

	namespace
	{
		std::map<std::string, std::shared_ptr<const Program>>& Classes()
		{
			static std::map<std::string, std::shared_ptr<const Program>> classes;
			return classes;
		}

		std::mt19937& Random()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}

		InputAction& ActionNamed(const std::string& name)
		{
			static std::unordered_map<std::string, std::unique_ptr<InputAction>> actions;
			auto& slot = actions[name];
			if (!slot)
				slot = std::make_unique<InputAction>(name.c_str());
			return *slot;
		}

		float AxisNamed(const std::string& name)
		{
			static std::unordered_map<std::string, std::unique_ptr<InputAxis1D>> axes;
			auto& slot = axes[name];
			if (!slot)
				slot = std::make_unique<InputAxis1D>(name.c_str());
			return slot->GetValue();
		}

		SpriteComponent* SpriteOf(Actor* a)
		{
			if (auto* anim = a->GetComponent<AnimationSpriteComponent>())
				return anim;
			return a->GetComponent<StaticSpriteComponent>();
		}

		SoundSourceComponent& SoundOf(Actor* a)
		{
			if (auto* s = a->GetComponent<SoundSourceComponent>())
				return *s;
			auto& s = a->AddComponent<SoundSourceComponent>();
			s.spatial = false;
			return s;
		}

		bool HasTag(Actor* a, const std::string& tag)
		{
			auto* tags = a ? a->GetComponent<TagsComponent>() : nullptr;
			return tags && tags->Has(tag);
		}

		Actor* FirstWithTag(const std::string& tag)
		{
			std::vector<Actor*> actors;
			ecs::CollectActorsWith(detail::ComponentTypeId<TagsComponent>(), actors);
			for (Actor* a : actors)
				if (HasTag(a, tag))
					return a;
			return nullptr;
		}

		double MathOf(const std::string& op, double v)
		{
			constexpr double r = 3.14159265358979323846 / 180.0;
			const std::string o = Lower(Trim(op));
			if (o == "abs") return std::fabs(v);
			if (o == "floor") return std::floor(v);
			if (o == "ceiling" || o == "ceil") return std::ceil(v);
			if (o == "sqrt") return std::sqrt(v);
			if (o == "sin") return std::sin(v * r);
			if (o == "cos") return std::cos(v * r);
			if (o == "tan") return std::tan(v * r);
			if (o == "asin") return std::asin(v) / r;
			if (o == "acos") return std::acos(v) / r;
			if (o == "atan") return std::atan(v) / r;
			if (o == "ln") return std::log(v);
			if (o == "log") return std::log10(v);
			if (o == "e ^" || o == "e^" || o == "exp") return std::exp(v);
			if (o == "10 ^" || o == "10^") return std::pow(10.0, v);
			return v;
		}

		Value PropertyOf(Actor* a, const std::string& raw)
		{
			if (!a)
				return Value();
			const std::string name = Trim(raw);
			if (name == "x") return Value::Num(a->transform.location.x);
			if (name == "y") return Value::Num(a->transform.location.y);
			if (name == "z") return Value::Num(a->transform.location.z);
			if (name == "id") return Value::Text(a->object_id_);
			if (name == "class") return Value::Text(a->GetTypeName());
			if (auto* c = a->GetComponent<VasmComponent>())
			{
				Value v;
				if (c->GetVariable(name, v))
					return v;
			}
			const auto& props = a->GetProperties();
			auto it = props.find(name);
			if (it != props.end())
				return FromProperty(it->second);
			return Value();
		}

		void SetPropertyOf(Actor* a, const std::string& raw, const Value& v)
		{
			if (!a)
				return;
			const std::string name = Trim(raw);
			if (name == "x") { a->transform.location.x = static_cast<float>(ToNum(v)); return; }
			if (name == "y") { a->transform.location.y = static_cast<float>(ToNum(v)); return; }
			if (name == "z") { a->transform.location.z = static_cast<float>(ToNum(v)); return; }
			if (auto* c = a->GetComponent<VasmComponent>())
				if (c->SetVariable(name, v))
					return;
			const auto& props = a->GetProperties();
			auto it = props.find(name);
			if (it != props.end())
				ToProperty(it->second, v);
		}

		PropVariantType ToVariant(const Value& v)
		{
			switch (v.t)
			{
			case Value::T::Number: return static_cast<float>(v.n);
			case Value::T::Bool: return v.b;
			default: return ToText(v);
			}
		}
	}

	// ---- variables -----------------------------------------------------------------------

	bool VasmComponent::GetVariable(const std::string& name, Value& out) const
	{
		if (!program_)
			return false;
		const int i = FindVar(*program_, name);
		if (i < 0)
			return false;
		const VarDef& v = program_->vars[i];
		if (v.global)
		{
			auto it = Globals().find(v.name);
			out = it != Globals().end() ? it->second : DefaultOf(v);
			return true;
		}
		if (v.type == "actor")
		{
			auto it = actor_vars_.find(v.name);
			out = it != actor_vars_.end() ? it->second : Value();
			return true;
		}
		const auto& props = GetOwner()->GetProperties();
		auto it = props.find(v.name);
		out = it != props.end() ? FromProperty(it->second) : DefaultOf(v);
		return true;
	}

	bool VasmComponent::SetVariable(const std::string& name, const Value& value)
	{
		if (!program_)
			return false;
		const int i = FindVar(*program_, name);
		if (i < 0)
			return false;
		const VarDef& v = program_->vars[i];
		if (v.global)
			Globals()[v.name] = value;
		else if (v.type == "actor")
			actor_vars_[v.name] = value;
		else
		{
			const auto& props = GetOwner()->GetProperties();
			auto it = props.find(v.name);
			if (it != props.end())
				ToProperty(it->second, value);
		}
		return true;
	}

	Value* VasmComponent::ArgsOf(Thread& t, int index)
	{
		for (size_t f = t.frames.size(); f > 0; --f)
		{
			Frame& frame = t.frames[f - 1];
			if (frame.function)
				return index >= 0 && index < static_cast<int>(frame.args.size()) ? &frame.args[index] : nullptr;
		}
		return nullptr;
	}

	Value VasmComponent::ReadVar(const Node& n, Thread& t)
	{
		if (n.arg >= 0)
			if (Value* a = ArgsOf(t, n.arg))
				return *a;
		Value v;
		if (n.ref >= 0 && program_ && GetVariable(program_->vars[n.ref].name, v))
			return v;
		return Value();
	}

	void VasmComponent::WriteVar(const Node& n, Thread& t, const Value& v)
	{
		if (n.arg >= 0)
			if (Value* a = ArgsOf(t, n.arg))
			{
				*a = v;
				return;
			}
		if (n.ref >= 0 && program_)
			SetVariable(program_->vars[n.ref].name, v);
	}

	// ---- evaluation --------------------------------------------------------------------------

	Value VasmComponent::Slot(const Node& n, size_t i, Thread& t)
	{
		if (i < n.inputs.size() && n.inputs[i])
			return Eval(*n.inputs[i], t);
		if (i < n.typed.size())
			return n.typed[i];
		return Value();
	}

	Value VasmComponent::CallFunction(int f, const std::vector<Value>& args, const Value& other)
	{
		if (!program_ || f < 0 || f >= static_cast<int>(program_->functions.size()))
			return Value();
		Thread t;
		t.sync = true;
		t.other = other;
		Frame frame;
		frame.chain = &program_->functions[f].body;
		frame.function = true;
		frame.args = args;
		frame.args.resize(program_->functions[f].inputs.size());
		t.frames.push_back(std::move(frame));
		Run(t);
		return t.result;
	}

	bool VasmComponent::Call(const std::string& name, const std::vector<Value>& args, Value* result, const Value& other)
	{
		if (!program_)
			return false;
		const int f = FindFunctionByName(*program_, name);
		if (f < 0)
			return false;
		const Value r = CallFunction(f, args, other);
		if (result)
			*result = r;
		return true;
	}

	Value VasmComponent::Eval(const Node& n, Thread& t)
	{
		Actor* self = GetOwner();
		switch (n.op)
		{
		case kOpVar:
			if (n.ref >= 0 && program_)
			{
				Value v;
				GetVariable(program_->vars[n.ref].name, v);
				return v;
			}
			return Value();
		case kOpArg:
		{
			Value* a = ArgsOf(t, n.ref);
			return a ? *a : Value();
		}
		case kOpCallValue:
		{
			std::vector<Value> args;
			for (size_t i = 0; i < n.values.size(); ++i)
				args.push_back(Slot(n, i, t));
			return CallFunction(n.ref, args, t.other);
		}

		// ---- Control
		case KindIndex("mo.x"): return Value::Num(self->transform.location.x);
		case KindIndex("mo.y"): return Value::Num(self->transform.location.y);
		case KindIndex("mo.vx"):
		{
			auto* v = self->GetComponent<VelocityComponent>();
			if (auto* h = dynamic_cast<Humanoid*>(self))
				return Value::Num(h->GetVelocity().x);
			return Value::Num(v ? v->linear.x : 0.0);
		}
		case KindIndex("mo.vy"):
		{
			auto* v = self->GetComponent<VelocityComponent>();
			if (auto* h = dynamic_cast<Humanoid*>(self))
				return Value::Num(h->GetVelocity().y);
			return Value::Num(v ? v->linear.y : 0.0);
		}

		// ---- Character
		case KindIndex("ch.grounded"): { auto* h = dynamic_cast<Humanoid*>(self); return Value::Bool(h && h->IsGrounded()); }
		case KindIndex("ch.falling"): { auto* h = dynamic_cast<Humanoid*>(self); return Value::Bool(h && h->IsFalling()); }
		case KindIndex("ch.facing_right"): { auto* h = dynamic_cast<Humanoid*>(self); return Value::Bool(h && h->IsFacingRight()); }

		// ---- Looks
		case KindIndex("lk.state"):
		{
			auto* anim = self->GetComponent<AnimationSpriteComponent>();
			return Value::Text(anim ? anim->GetCurrentState() : std::string());
		}

		// ---- Sensing
		case KindIndex("se.pressed"): return Value::Bool(ActionNamed(Text(n, 0, t)).IsPressed());
		case KindIndex("se.held"): return Value::Bool(ActionNamed(Text(n, 0, t)).IsHeld());
		case KindIndex("se.released"): return Value::Bool(ActionNamed(Text(n, 0, t)).IsReleased());
		case KindIndex("se.axis"): return Value::Num(AxisNamed(Text(n, 0, t)));
		case KindIndex("se.touching"):
		{
			const std::string tag = Text(n, 0, t);
			auto* c = self->GetComponent<ColliderComponent>();
			if (!c)
				return Value::Bool(false);
			for (ColliderComponent* o : c->GetOverlapping())
			{
				Actor* a = o ? o->GetOwner() : nullptr;
				if (a && (tag.empty() || HasTag(a, tag)))
					return Value::Bool(true);
			}
			return Value::Bool(false);
		}
		case KindIndex("se.has_tag"): return Value::Bool(HasTag(ActorOf(Slot(n, 0, t)), Text(n, 1, t)));
		case KindIndex("se.distance"):
		{
			Actor* a = ActorOf(Slot(n, 0, t));
			if (!a)
				return Value::Num(0.0);
			const double dx = a->transform.location.x - self->transform.location.x;
			const double dy = a->transform.location.y - self->transform.location.y;
			return Value::Num(std::sqrt(dx * dx + dy * dy));
		}
		case KindIndex("se.of"): return PropertyOf(ActorOf(Slot(n, 1, t)), Text(n, 0, t));
		case KindIndex("se.prop"): return PropertyOf(ActorOf(Slot(n, 1, t)), Text(n, 0, t));
		case KindIndex("se.valid"): return Value::Bool(ActorOf(Slot(n, 0, t)) != nullptr);
		case KindIndex("se.timer"): return Value::Num(time_ - timer0_);

		// ---- Operators
		case KindIndex("op.add"): return Value::Num(Num(n, 0, t) + Num(n, 1, t));
		case KindIndex("op.sub"): return Value::Num(Num(n, 0, t) - Num(n, 1, t));
		case KindIndex("op.mul"): return Value::Num(Num(n, 0, t) * Num(n, 1, t));
		case KindIndex("op.div"):
		{
			const double b = Num(n, 1, t);
			return Value::Num(b != 0.0 ? Num(n, 0, t) / b : 0.0);
		}
		case KindIndex("op.rand"):
		{
			double a = Num(n, 0, t), b = Num(n, 1, t);
			if (a > b)
				std::swap(a, b);
			if (std::floor(a) == a && std::floor(b) == b)
				return Value::Num(static_cast<double>(std::uniform_int_distribution<long long>(
					static_cast<long long>(a), static_cast<long long>(b))(Random())));
			return Value::Num(std::uniform_real_distribution<double>(a, b)(Random()));
		}
		case KindIndex("op.gt"): return Value::Bool(Less(Slot(n, 1, t), Slot(n, 0, t)));
		case KindIndex("op.lt"): return Value::Bool(Less(Slot(n, 0, t), Slot(n, 1, t)));
		case KindIndex("op.eq"): return Value::Bool(Equal(Slot(n, 0, t), Slot(n, 1, t)));
		case KindIndex("op.and"): return Value::Bool(Cond(n, 0, t) && Cond(n, 1, t));
		case KindIndex("op.or"): return Value::Bool(Cond(n, 0, t) || Cond(n, 1, t));
		case KindIndex("op.not"): return Value::Bool(!Cond(n, 0, t));
		case KindIndex("op.join"): return Value::Text(Text(n, 0, t) + Text(n, 1, t));
		case KindIndex("op.letter"):
		{
			const std::string s = Text(n, 1, t);
			const long i = std::lround(Num(n, 0, t)) - 1;
			return Value::Text(i >= 0 && i < static_cast<long>(s.size()) ? s.substr(static_cast<size_t>(i), 1) : std::string());
		}
		case KindIndex("op.length"): return Value::Num(static_cast<double>(Text(n, 0, t).size()));
		case KindIndex("op.contains"): return Value::Bool(Lower(Text(n, 0, t)).find(Lower(Text(n, 1, t))) != std::string::npos);
		case KindIndex("op.mod"):
		{
			const double a = Num(n, 0, t), b = Num(n, 1, t);
			if (b == 0.0)
				return Value::Num(0.0);
			double r = std::fmod(a, b);
			if (r != 0.0 && (r < 0.0) != (b < 0.0))
				r += b;
			return Value::Num(r);
		}
		case KindIndex("op.round"): return Value::Num(std::round(Num(n, 0, t)));
		case KindIndex("op.math"): return Value::Num(MathOf(Text(n, 0, t), Num(n, 1, t)));

		// ---- Actors
		case KindIndex("ac.me"): return Value::Of(self);
		case KindIndex("ac.other"): return t.other;
		case KindIndex("ac.spawned"): return t.spawned;
		case KindIndex("ac.find"):
		{
			Level* level = CurrentLevel();
			const std::string id = Text(n, 0, t);
			return Value::Of(level ? level->GetActorFromID(id.c_str()) : nullptr);
		}
		case KindIndex("ac.with_tag"): return Value::Of(FirstWithTag(Text(n, 0, t)));
		case KindIndex("ac.count"):
		{
			Level* level = CurrentLevel();
			const std::string cls = Text(n, 0, t);
			return Value::Num(level ? level->CountActorsOfClass(cls.c_str()) : 0);
		}

		default:
			return Value();
		}
	}

	// ---- statements ----------------------------------------------------------------------------

	void VasmComponent::Broadcast(const std::string& message, Thread* waiter)
	{
		std::vector<VasmComponent*> all;
		ecs::CollectComponents<VasmComponent>(all);
		for (VasmComponent* c : all)
		{
			if (!c || !c->alive_)
				continue;
			Actor* owner = c->GetOwner();
			const std::vector<int> started = c->Receive(message);
			if (waiter && owner)
				for (int id : started)
					waiter->waiting.emplace_back(owner->GetEntity(), id);
		}
	}

	void VasmComponent::StopAll()
	{
		for (auto& t : threads_)
			t->done = true;
	}

	Step VasmComponent::Exec(const Node& n, Thread& t, size_t fi)
	{
		Actor* self = GetOwner();
		auto push = [&](const Chain& chain, const Node* loop, double count = 0.0, double end = 0.0)
		{
			Frame f;
			f.chain = &chain;
			f.loop = loop;
			f.count = count;
			f.end = end;
			t.frames[fi].pc++;
			t.frames.push_back(std::move(f));
			return Step::Pushed;
		};
		auto body = [&](size_t mouth) -> const Chain&
		{
			static const Chain empty;
			return mouth < n.bodies.size() ? n.bodies[mouth] : empty;
		};

		switch (n.op)
		{
		case kOpCall:
		{
			if (!program_ || n.ref < 0 || n.ref >= static_cast<int>(program_->functions.size()))
				return Step::Next;
			const Function& fn = program_->functions[n.ref];
			Frame f;
			f.chain = &fn.body;
			f.function = true;
			for (size_t i = 0; i < n.values.size(); ++i)
				f.args.push_back(Slot(n, i, t));
			f.args.resize(fn.inputs.size());
			t.frames[fi].pc++;
			t.frames.push_back(std::move(f));
			return Step::Pushed;
		}

		// ---- Events
		case KindIndex("ev.broadcast"):
			Broadcast(Text(n, 0, t), nullptr);
			return Step::Next;
		case KindIndex("ev.broadcast_wait"):
			t.waiting.clear();
			Broadcast(Text(n, 0, t), t.sync ? nullptr : &t);
			if (t.sync || t.waiting.empty())
				return Step::Next;
			t.wait = Wait::Threads;
			return Step::Yield;

		// ---- Control
		case KindIndex("ctl.wait"):
			if (t.sync)
				return Step::Next;
			t.wait = Wait::Time;
			t.wake = time_ + std::max(0.0, Num(n, 0, t));
			return Step::Yield;
		case KindIndex("ctl.repeat"):
		{
			const double count = std::round(Num(n, 0, t));
			if (count < 1.0)
				return Step::Next;
			return push(body(0), &n, count);
		}
		case KindIndex("ctl.forever"): return push(body(0), &n);
		case KindIndex("ctl.if"):
			if (Cond(n, 0, t))
				return push(body(0), nullptr);
			return Step::Next;
		case KindIndex("ctl.ifelse"): return push(body(Cond(n, 0, t) ? 0 : 1), nullptr);
		case KindIndex("ctl.wait_until"):
			if (Cond(n, 0, t) || t.sync)
				return Step::Next;
			t.wait = Wait::Until;
			t.until = &n;
			return Step::Stay;
		case KindIndex("ctl.repeat_until"):
			if (Cond(n, 0, t))
				return Step::Next;
			return push(body(0), &n);
		case KindIndex("ctl.while"):
			if (!Cond(n, 0, t))
				return Step::Next;
			return push(body(0), &n);
		case KindIndex("ctl.for"):
		{
			const double from = Num(n, 1, t), to = Num(n, 2, t);
			WriteVar(n, t, Value::Num(from));
			if (from > to)
				return Step::Next;
			return push(body(0), &n, from, to);
		}
		case KindIndex("ctl.stop"): return Step::End;
		case KindIndex("ctl.stop_all"):
			StopAll();
			return Step::End;

		// ---- Motion
		case KindIndex("mo.change_x"): self->transform.location.x += static_cast<float>(Num(n, 0, t)); return Step::Next;
		case KindIndex("mo.set_x"): self->transform.location.x = static_cast<float>(Num(n, 0, t)); return Step::Next;
		case KindIndex("mo.change_y"): self->transform.location.y += static_cast<float>(Num(n, 0, t)); return Step::Next;
		case KindIndex("mo.set_y"): self->transform.location.y = static_cast<float>(Num(n, 0, t)); return Step::Next;
		case KindIndex("mo.goto"):
			self->transform.location.x = static_cast<float>(Num(n, 0, t));
			self->transform.location.y = static_cast<float>(Num(n, 1, t));
			return Step::Next;
		case KindIndex("mo.goto_actor"):
			if (Actor* a = ActorOf(Slot(n, 0, t)))
			{
				self->transform.location.x = a->transform.location.x;
				self->transform.location.y = a->transform.location.y;
			}
			return Step::Next;
		case KindIndex("mo.glide"):
		{
			const double seconds = std::max(0.0, Num(n, 0, t));
			const float x = static_cast<float>(Num(n, 1, t)), y = static_cast<float>(Num(n, 2, t));
			if (t.sync || seconds <= 0.0)
			{
				self->transform.location.x = x;
				self->transform.location.y = y;
				return Step::Next;
			}
			t.wait = Wait::Glide;
			t.gx0 = self->transform.location.x;
			t.gy0 = self->transform.location.y;
			t.gx1 = x;
			t.gy1 = y;
			t.gt0 = time_;
			t.gd = seconds;
			return Step::Yield;
		}
		case KindIndex("mo.toward"):
		{
			Actor* a = ActorOf(Slot(n, 0, t));
			if (!a)
				return Step::Next;
			const double dx = a->transform.location.x - self->transform.location.x;
			const double dy = a->transform.location.y - self->transform.location.y;
			const double d = std::sqrt(dx * dx + dy * dy), step = Num(n, 1, t) * dt_;
			if (d <= step || d == 0.0)
			{
				self->transform.location.x = a->transform.location.x;
				self->transform.location.y = a->transform.location.y;
			}
			else
			{
				self->transform.location.x += static_cast<float>(dx / d * step);
				self->transform.location.y += static_cast<float>(dy / d * step);
			}
			return Step::Next;
		}
		case KindIndex("mo.set_vel"):
		case KindIndex("mo.set_vel_x"):
		case KindIndex("mo.set_vel_y"):
		case KindIndex("mo.change_vel"):
		{
			const bool change = n.op == KindIndex("mo.change_vel");
			const bool set_x = n.op != KindIndex("mo.set_vel_y"), set_y = n.op != KindIndex("mo.set_vel_x");
			const double a = Num(n, 0, t), b = n.values.size() > 1 ? Num(n, 1, t) : 0.0;
			const float vx = static_cast<float>(n.op == KindIndex("mo.set_vel_y") ? 0.0 : a);
			const float vy = static_cast<float>(n.op == KindIndex("mo.set_vel_y") ? a : b);
			if (auto* h = dynamic_cast<Humanoid*>(self))
			{
				vec2 v = h->GetVelocity();
				if (set_x) v.x = change ? v.x + vx : vx;
				if (set_y) v.y = change ? v.y + vy : vy;
				h->SetVelocity(v.x, v.y);
			}
			else
			{
				auto& vel = self->AddComponent<VelocityComponent>();
				if (set_x) vel.linear.x = change ? vel.linear.x + vx : vx;
				if (set_y) vel.linear.y = change ? vel.linear.y + vy : vy;
			}
			return Step::Next;
		}

		// ---- Character
		case KindIndex("ch.walk"): if (auto* h = dynamic_cast<Humanoid*>(self)) h->Move(static_cast<float>(Num(n, 0, t))); return Step::Next;
		case KindIndex("ch.jump"): if (auto* h = dynamic_cast<Humanoid*>(self)) h->Jump(); return Step::Next;
		case KindIndex("ch.stop_jump"): if (auto* h = dynamic_cast<Humanoid*>(self)) h->StopJumping(); return Step::Next;
		case KindIndex("ch.launch"):
			if (auto* h = dynamic_cast<Humanoid*>(self))
				h->Launch(static_cast<float>(Num(n, 0, t)), static_cast<float>(Num(n, 1, t)));
			return Step::Next;
		case KindIndex("ch.stop"): if (auto* h = dynamic_cast<Humanoid*>(self)) h->StopMovement(); return Step::Next;

		// ---- Looks
		case KindIndex("lk.show"):
		case KindIndex("lk.hide"):
			if (SpriteComponent* s = SpriteOf(self))
				s->visible = n.op == KindIndex("lk.show");
			return Step::Next;
		case KindIndex("lk.image"):
		{
			auto& sprite = self->AddComponent<StaticSpriteComponent>();
			sprite.texture = Text(n, 0, t);
			return Step::Next;
		}
		case KindIndex("lk.anim"):
		{
			auto& anim = self->AddComponent<AnimationSpriteComponent>();
			const std::string texture = Text(n, 0, t);
			const int frames = std::max(1, static_cast<int>(std::lround(Num(n, 1, t))));
			const float frame_time = static_cast<float>(std::max(0.001, Num(n, 2, t)));
			if (anim.texture != texture || anim.frame_count != frames)
				anim.SetAnimation(texture, frames, frame_time, true);
			else
				anim.frame_time = frame_time;
			return Step::Next;
		}
		case KindIndex("lk.graph"):
		{
			auto& anim = self->AddComponent<AnimationSpriteComponent>();
			const std::string graph = Text(n, 0, t);
			if (anim.graph != graph)
			{
				anim.graph = graph;
				anim.LoadGraph(graph);
			}
			return Step::Next;
		}
		case KindIndex("lk.param"):
			if (auto* anim = self->GetComponent<AnimationSpriteComponent>())
			{
				const Value v = Slot(n, 1, t);
				if (v.t == Value::T::Bool)
					anim->SetBool(Text(n, 0, t), v.b);
				else
					anim->SetFloat(Text(n, 0, t), static_cast<float>(ToNum(v)));
			}
			return Step::Next;
		case KindIndex("lk.trigger"):
			if (auto* anim = self->GetComponent<AnimationSpriteComponent>())
				anim->SetTrigger(Text(n, 0, t));
			return Step::Next;
		case KindIndex("lk.face"):
			if (SpriteComponent* s = SpriteOf(self))
				s->flip_x = Lower(Trim(Text(n, 0, t))).rfind('l', 0) == 0;
			return Step::Next;
		case KindIndex("lk.size"):
		{
			const float k = static_cast<float>(Num(n, 0, t) / 100.0);
			self->transform.scale.x = (self->transform.scale.x < 0.f ? -1.f : 1.f) * k;
			self->transform.scale.y = k;
			return Step::Next;
		}

		// ---- Sound
		case KindIndex("snd.play"):
		{
			auto& s = SoundOf(self);
			const std::string sound = Text(n, 0, t);
			if (!sound.empty())
				s.sound = sound;
			s.Play();
			return Step::Next;
		}
		case KindIndex("snd.stop"):
			if (auto* s = self->GetComponent<SoundSourceComponent>())
				s->Stop();
			return Step::Next;
		case KindIndex("snd.volume"): SoundOf(self).volume = static_cast<float>(Num(n, 0, t) / 100.0); return Step::Next;

		// ---- Sensing
		case KindIndex("se.reset_timer"): timer0_ = time_; return Step::Next;

		// ---- Variables
		case KindIndex("var.set"): WriteVar(n, t, Slot(n, 1, t)); return Step::Next;
		case KindIndex("var.change"): WriteVar(n, t, Value::Num(ToNum(ReadVar(n, t)) + Num(n, 1, t))); return Step::Next;

		// ---- Actors
		case KindIndex("ac.spawn"):
		case KindIndex("ac.spawn_here"):
		{
			Level* level = CurrentLevel();
			const std::string cls = Text(n, 0, t);
			Actor* a = level ? level->SpawnActor(cls.c_str()) : nullptr;
			if (a)
			{
				if (n.op == KindIndex("ac.spawn"))
					a->transform.location = vec3(static_cast<float>(Num(n, 1, t)), static_cast<float>(Num(n, 2, t)), 0.f);
				else
					a->transform.location = self->transform.location;
			}
			t.spawned = Value::Of(a);
			return Step::Next;
		}
		case KindIndex("ac.destroy"):
			if (Level* level = CurrentLevel())
				level->DestroyActor(self);
			StopAll();
			return Step::End;
		case KindIndex("ac.destroy_other"):
			if (Actor* a = ActorOf(Slot(n, 0, t)))
				if (Level* level = CurrentLevel())
					level->DestroyActor(a);
			return Step::Next;
		case KindIndex("ac.lifetime"):
			self->AddComponent<LifetimeComponent>().remaining = static_cast<float>(Num(n, 0, t));
			return Step::Next;
		case KindIndex("ac.tag"): self->AddComponent<TagsComponent>().Add(Text(n, 0, t)); return Step::Next;
		case KindIndex("ac.untag"):
			if (auto* tags = self->GetComponent<TagsComponent>())
				tags->Remove(Text(n, 0, t));
			return Step::Next;
		case KindIndex("ac.set_of"): SetPropertyOf(ActorOf(Slot(n, 1, t)), Text(n, 0, t), Slot(n, 2, t)); return Step::Next;
		case KindIndex("ac.call"):
		{
			Actor* a = ActorOf(Slot(n, 1, t));
			const std::string name = Trim(Text(n, 0, t));
			const Value arg = Slot(n, 2, t);
			if (!a)
				return Step::Next;
			if (auto* c = a->GetComponent<VasmComponent>())
				if (c->Call(name, { arg }, nullptr, Value::Of(self)))
					return Step::Next;
			a->CallFunctionByName(name, { ToVariant(arg) });
			return Step::Next;
		}
		case KindIndex("ac.collider"):
		case KindIndex("ac.trigger"):
		{
			auto& c = self->AddComponent<ColliderComponent>();
			c.size = vec2(static_cast<float>(Num(n, 0, t)), static_cast<float>(Num(n, 1, t)));
			c.trigger = n.op == KindIndex("ac.trigger");
			return Step::Next;
		}

		// ---- World
		case KindIndex("wd.print"):
			std::cout << "[" << class_name_ << "] " << Text(n, 0, t) << std::endl;
			return Step::Next;
		case KindIndex("wd.shake"): GetCameraShake().Trigger(static_cast<float>(Num(n, 0, t)), 1.f); return Step::Next;
		case KindIndex("wd.slowmo"):
			if (Engine* e = Engine::Get())
				e->SetGlobalTimeDilatation(static_cast<float>(Num(n, 0, t)));
			return Step::Next;
		case KindIndex("wd.slowmo_for"):
			if (Engine* e = Engine::Get())
				e->SetGlobalTimeDilatation(static_cast<float>(Num(n, 0, t)), static_cast<float>(Num(n, 1, t)));
			return Step::Next;
		case KindIndex("wd.widget"):
			if (Engine* e = Engine::Get())
				if (UserWidget* w = CreateWidget(e->GetDefaultPlayer(), Text(n, 0, t)))
					w->AddToViewport();
			return Step::Next;

		// ---- Functions
		case KindIndex("fn.return"):
		{
			t.result = Slot(n, 0, t);
			while (!t.frames.empty())
			{
				const bool function = t.frames.back().function;
				t.frames.pop_back();
				if (function)
					break;
			}
			return Step::Popped;
		}

		default:
			return Step::Next;   // a hat, a value alone, a kind of a newer engine
		}
	}

	bool VasmComponent::ContinueLoop(Thread& t, Frame& f)
	{
		const Node& n = *f.loop;
		switch (n.op)
		{
		case KindIndex("ctl.repeat"): f.count -= 1.0; return f.count >= 1.0;
		case KindIndex("ctl.forever"): return true;
		case KindIndex("ctl.repeat_until"): return !Cond(n, 0, t);
		case KindIndex("ctl.while"): return Cond(n, 0, t);
		case KindIndex("ctl.for"):
			f.count += 1.0;
			if (f.count > f.end)
				return false;
			WriteVar(n, t, Value::Num(f.count));
			return true;
		default: return false;
		}
	}

	void VasmComponent::Run(Thread& t)
	{
		++running_;
		int budget = t.sync ? 1000000 : 200000;
		while (!t.done)
		{
			if (--budget < 0)
			{
				if (!warned_)
					std::cout << kLog << class_name_ << " : a script runs too long without waiting (a loop in a function, or a "
					          << "wait until in a value ?) : stopped" << std::endl;
				warned_ = true;
				t.done = true;
				break;
			}
			if (t.frames.empty())
			{
				t.done = true;
				break;
			}
			const size_t fi = t.frames.size() - 1;
			if (t.frames[fi].pc >= t.frames[fi].chain->size())
			{
				if (t.frames[fi].loop && ContinueLoop(t, t.frames[fi]))
				{
					t.frames[fi].pc = 0;
					if (!t.sync)
						break;   // one turn per frame
					continue;
				}
				t.frames.pop_back();
				continue;
			}
			const Node& n = *(*t.frames[fi].chain)[t.frames[fi].pc];
			const Step step = Exec(n, t, fi);
			if (t.done)
				break;
			switch (step)
			{
			case Step::Next: t.frames[fi].pc++; break;
			case Step::Yield: t.frames[fi].pc++; goto yield;
			case Step::Stay: goto yield;
			case Step::End: t.done = true; break;
			default: break;   // Pushed, Popped
			}
		}
	yield:
		--running_;
	}

	// ---- threads -----------------------------------------------------------------------------------

	Thread& VasmComponent::Start(int script, Actor* other, bool sync)
	{
		auto t = std::make_unique<Thread>();
		t->id = next_thread_++;
		t->script = script;
		t->sync = sync;
		t->other = Value::Of(other);
		Frame f;
		f.chain = &program_->scripts[script].body;
		t->frames.push_back(std::move(f));
		threads_.push_back(std::move(t));
		Thread& ref = *threads_.back();
		Run(ref);
		return ref;
	}

	bool VasmComponent::ScriptRunning(int script) const
	{
		for (const auto& t : threads_)
			if (t->script == script && !t->done)
				return true;
		return false;
	}

	bool VasmComponent::Running(int thread) const
	{
		for (const auto& t : threads_)
			if (t->id == thread)
				return !t->done;
		return false;
	}

	void VasmComponent::Fire(int hat, Actor* other)
	{
		if (!alive_ || !program_)
			return;
		for (size_t i = 0; i < program_->scripts.size(); ++i)
			if (program_->scripts[i].hat->op == hat)
				Start(static_cast<int>(i), other);
	}

	std::vector<int> VasmComponent::Receive(const std::string& message)
	{
		std::vector<int> started;
		if (!alive_ || !program_)
			return started;
		for (size_t i = 0; i < program_->scripts.size(); ++i)
		{
			const Script& s = program_->scripts[i];
			if (s.hat->op != KindIndex("ev.receive"))
				continue;
			const Value wanted = s.hat->typed.empty() ? Value() : s.hat->typed[0];
			if (!Equal(wanted, Value::Text(message)))
				continue;
			Thread& t = Start(static_cast<int>(i), nullptr);
			if (!t.done)
				started.push_back(t.id);
		}
		return started;
	}

	void VasmComponent::BeginPlay()
	{
		auto it = Classes().find(class_name_);
		program_ = it != Classes().end() ? it->second : nullptr;
		threads_.clear();
		actor_vars_.clear();
		time_ = timer0_ = dt_ = 0.0;
		warned_ = false;
		alive_ = program_ != nullptr;
		if (!program_)
			return;

		// "when something touches me", "touching tag" : the actor needs a collider.
		Actor* self = GetOwner();
		if (program_->uses_touch && !self->GetComponent<ColliderComponent>() && !dynamic_cast<Humanoid*>(self))
			self->AddComponent<ColliderComponent>().trigger = true;

		Fire(KindIndex("ev.start"), nullptr);
	}

	void VasmComponent::Tick(float dt)
	{
		if (!alive_ || !program_)
			return;
		dt_ = dt;
		time_ += dt;

		// The scripts that wait.
		const size_t count = threads_.size();
		for (size_t i = 0; i < count && i < threads_.size(); ++i)
		{
			Thread& t = *threads_[i];
			if (t.done)
				continue;
			switch (t.wait)
			{
			case Wait::Time:
				if (time_ < t.wake)
					continue;
				break;
			case Wait::Until:
				if (!t.until || !Cond(*t.until, 0, t))
					continue;
				t.frames.back().pc++;
				break;
			case Wait::Glide:
			{
				const double k = std::min(1.0, (time_ - t.gt0) / std::max(t.gd, 1e-6));
				GetOwner()->transform.location.x = t.gx0 + static_cast<float>((t.gx1 - t.gx0) * k);
				GetOwner()->transform.location.y = t.gy0 + static_cast<float>((t.gy1 - t.gy0) * k);
				if (k < 1.0)
					continue;
				break;
			}
			case Wait::Threads:
			{
				bool busy = false;
				for (const auto& [entity, id] : t.waiting)
				{
					Actor* a = ecs::GetActor(entity);
					auto* c = a ? a->GetComponent<VasmComponent>() : nullptr;
					busy = busy || (c && c->Running(id));
				}
				if (busy)
					continue;
				t.waiting.clear();
				break;
			}
			default:
				break;
			}
			t.wait = Wait::None;
			Run(t);
		}

		// New runs : every frame, actions.
		for (size_t i = 0; i < program_->scripts.size(); ++i)
		{
			const Node& hat = *program_->scripts[i].hat;
			bool go = false;
			if (hat.op == KindIndex("ev.tick"))
				go = true;
			else if (hat.op == KindIndex("ev.key"))
				go = ActionNamed(hat.values.empty() ? std::string() : hat.values[0]).IsPressed();
			else if (hat.op == KindIndex("ev.key_up"))
				go = ActionNamed(hat.values.empty() ? std::string() : hat.values[0]).IsReleased();
			if (go && !ScriptRunning(static_cast<int>(i)))
				Start(static_cast<int>(i), nullptr);
		}

		if (running_ == 0)
			threads_.erase(std::remove_if(threads_.begin(), threads_.end(), [](const auto& t) { return t->done; }), threads_.end());
	}

	void VasmComponent::EndPlay()
	{
		if (alive_ && program_)
		{
			// "when the game ends" : at once, no waits.
			for (size_t i = 0; i < program_->scripts.size(); ++i)
				if (program_->scripts[i].hat->op == KindIndex("ev.end"))
					Start(static_cast<int>(i), nullptr, true);
		}
		alive_ = false;
		StopAll();
		if (running_ == 0)
			threads_.clear();
	}

	// =========================================================================
	// 5. Classes and events
	// =========================================================================

	const char* GetExtension() { return ".vasm"; }

	namespace
	{
		bool IsBuiltinParent(const std::string& parent)
		{
			Engine* e = Engine::Get();
			return e && e->GetFactory().GetObjectConstr(parent.c_str()).has_value() && !Classes().count(parent);
		}

		Object* CreateActor(const std::string& name)
		{
			auto it = Classes().find(name);
			if (it == Classes().end())
			{
				std::cout << kLog << "class not found : " << name << std::endl;
				return nullptr;
			}
			const std::shared_ptr<const Program> program = it->second;
			Engine* engine = Engine::Get();
			auto ctor = engine ? engine->GetFactory().GetObjectConstr(program->parent.c_str()) : std::nullopt;
			if (!ctor || !*ctor || Classes().count(program->parent))
			{
				std::cout << kLog << program->file << " : the parent class " << program->parent << " does not exist" << std::endl;
				return nullptr;
			}
			Object* object = (*ctor)();
			Actor* actor = dynamic_cast<Actor*>(object);
			if (!actor)
			{
				delete object;
				std::cout << kLog << program->file << " : " << program->parent << " is not an actor" << std::endl;
				return nullptr;
			}
			if (!actor->script_class_.empty())
			{
				std::cout << kLog << program->file << " : " << program->parent
				          << " is a JavaScript class, a Voxel Assembly class extends a C++ class" << std::endl;
				delete object;
				return nullptr;
			}
			actor->script_class_ = name;
			for (const VarDef& v : program->vars)
				if (!v.global && v.type != "actor")
					actor->AddDynamicProperty(v.name, PropertyDefault(v));
			actor->AddComponent<VasmComponent>(name);
			return actor;
		}

		std::string ReadAsset(const std::string& path)
		{
			const std::vector<std::uint8_t> bytes = fs::ReadBinary(path);
			return std::string(bytes.begin(), bytes.end());
		}

		bool EndsWith(const std::string& s, const char* suffix)
		{
			const size_t n = std::strlen(suffix);
			if (s.size() < n)
				return false;
			for (size_t i = 0; i < n; ++i)
				if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i])
					return false;
			return true;
		}
	}

	void PrepareClasses()
	{
		Engine* engine = Engine::Get();
		std::map<std::string, std::shared_ptr<const Program>> loaded;

		for (const std::string& path : fs::ListFiles("", true))
		{
			if (!EndsWith(path, ".vasm"))
				continue;
			auto program = std::make_shared<Program>();
			std::string error;
			if (!ParseProgram(ReadAsset(path), path, *program, error))
			{
				std::cout << kLog << error << std::endl;
				continue;
			}
			if (loaded.count(program->class_name))
			{
				std::cout << kLog << path << " : the class " << program->class_name << " is already in "
				          << loaded[program->class_name]->file << std::endl;
				continue;
			}
			loaded[program->class_name] = program;
		}

		Classes() = loaded;
		Globals().clear();
		for (const auto& [name, program] : Classes())
			for (const VarDef& v : program->vars)
				if (v.global && !Globals().count(v.name))
					Globals()[v.name] = DefaultOf(v);

		if (!engine)
			return;
		static std::set<std::string> registered;
		for (const auto& [name, program] : Classes())
		{
			if (registered.count(name))
				continue;
			if (!IsBuiltinParent(program->parent))
			{
				std::cout << kLog << program->file << " : the parent class " << program->parent
				          << " is not a C++ class of the engine or of the game" << std::endl;
				continue;
			}
			if (engine->GetFactory().GetObjectConstr(name.c_str()).has_value())
			{
				std::cout << kLog << program->file << " : a class is already called " << name << ", rename it" << std::endl;
				continue;
			}
			engine->GetFactory().RegisterObject(name.c_str(), [name]() -> Object* { return CreateActor(name); });
			registered.insert(name);
			std::cout << kLog << "class " << name << " (" << program->file << ")" << std::endl;
		}
	}

	std::vector<std::string> GetClassNames()
	{
		std::vector<std::string> names;
		for (const auto& [name, program] : Classes())
			names.push_back(name);
		return names;
	}

	bool IsClass(const std::string& name)
	{
		return Classes().count(name) != 0;
	}

	bool Check(const std::string& text, std::string& error)
	{
		Program p;
		if (!ParseProgram(text, "this file", p, error))
			return false;
		if (Engine::Get() && !IsBuiltinParent(p.parent))
		{
			error = "the parent class " + p.parent + " is not a C++ class of the engine or of the game";
			return false;
		}
		return true;
	}

	void OnActorEvent(Actor* self, const char* event, Actor* other)
	{
		auto* c = self && event ? self->GetComponent<VasmComponent>() : nullptr;
		if (!c)
			return;
		if (std::strcmp(event, "OnBeginOverlap") == 0)
			c->Fire(KindIndex("ev.overlap"), other);
		else if (std::strcmp(event, "OnEndOverlap") == 0)
			c->Fire(KindIndex("ev.overlap_end"), other);
		else if (std::strcmp(event, "OnLanded") == 0)
			c->Fire(KindIndex("ev.landed"), other);
		else if (std::strcmp(event, "OnJumped") == 0)
			c->Fire(KindIndex("ev.jumped"), other);
	}

	void OnActorHit(Actor* self, Actor* other, const vec3&)
	{
		if (auto* c = self ? self->GetComponent<VasmComponent>() : nullptr)
			c->Fire(KindIndex("ev.hit"), other);
	}
}
