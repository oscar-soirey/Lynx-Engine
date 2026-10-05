/*
 * Composants du moteur cote JavaScript.
 *
 *     const sprite = parent.addComponent("StaticSprite", { texture: "hero.png", size: {x: 2, y: 3} });
 *     sprite.visible = false;
 *     parent.getComponent("SoundSource")?.play();
 *
 * Un objet JS de composant ne garde que (entite, type) : si le composant ou
 * l'acteur disparait, l'acces leve une ReferenceError au lieu de crasher.
 *
 * Chaque type est decrit par un Binder<T> : proprietes (champs du composant)
 * et methodes. Les vecteurs sont des objets simples {x, y, z} (copie) :
 * sprite.offset = vec3(0, 2, 0), pas sprite.offset.y = 2.
 */

#include "Private/ScriptInternal.h"

#include "../core/Engine.h"
#include "../core/Voxels.h"
#include "../gameplay/Actor.h"
#include "../gameplay/BoxColliderComponent.h"
#include "../gameplay/CameraComponent.h"
#include "../gameplay/Components.h"
#include "../gameplay/LightComponent.h"
#include "../gameplay/Private/ECS.h"
#include "../gameplay/SoundSourceComponent.h"
#include "../gameplay/SpriteComponents.h"

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lynx::script_detail
{
	namespace
	{
		// ====================================================================
		// Fonctions JS gardees par le C++ (evenements, conditions...)
		// ====================================================================

		struct JsFunctionRef
		{
			JSValue fn = JS_UNDEFINED;
			uint32_t entity = ecs::kNullEntity;  // this de l'appel
			bool alive = true;
		};

		std::unordered_set<JsFunctionRef*>& LiveRefs()
		{
			static std::unordered_set<JsFunctionRef*> refs;
			return refs;
		}

		using JsRef = std::shared_ptr<JsFunctionRef>;

		JsRef MakeRef(JSContext* ctx, JSValueConst fn, uint32_t entity)
		{
			auto* raw = new JsFunctionRef{ JS_DupValue(ctx, fn), entity, true };
			LiveRefs().insert(raw);

			return JsRef(raw, [](JsFunctionRef* r)
			{
				// Runtime deja ferme : la valeur a ete liberee par Shutdown.
				if (r->alive)
				{
					if (JSContext* c = Context())
						JS_FreeValue(c, r->fn);
				}

				LiveRefs().erase(r);
				delete r;
			});
		}

		/** Appelle la fonction (this = acteur). Retourne la valeur (a liberer). */
		JSValue CallRef(const JsRef& ref, int argc, JSValueConst* argv)
		{
			JSContext* ctx = Context();

			if (!ref || !ref->alive || !ctx)
				return JS_UNDEFINED;

			JSValue fn = JS_DupValue(ctx, ref->fn);
			JSValue self = ActorObject(ctx, ecs::GetActor(ref->entity));

			EnterCall();
			JSValue r = JS_Call(ctx, fn, self, argc, argv);
			LeaveCall();

			JS_FreeValue(ctx, self);
			JS_FreeValue(ctx, fn);

			if (JS_IsException(r))
			{
				LogException(ctx, "component callback");
				r = JS_UNDEFINED;
			}

			return r;
		}

		void CallRefVoid(const JsRef& ref, int argc = 0, JSValueConst* argv = nullptr)
		{
			JSValue r = CallRef(ref, argc, argv);

			if (JSContext* ctx = Context())
				JS_FreeValue(ctx, r);
		}

		std::function<void()> ToCallback(JSContext* ctx, JSValueConst fn, uint32_t entity)
		{
			if (!JS_IsFunction(ctx, fn))
				return {};

			JsRef ref = MakeRef(ctx, fn, entity);
			return [ref]() { CallRefVoid(ref); };
		}


		// ====================================================================
		// Conversions
		// ====================================================================

		JSValue ToJS(JSContext* ctx, float v) { return JS_NewFloat64(ctx, v); }
		JSValue ToJS(JSContext* ctx, int v) { return JS_NewInt32(ctx, v); }
		JSValue ToJS(JSContext* ctx, uint32_t v) { return JS_NewUint32(ctx, v); }
		JSValue ToJS(JSContext* ctx, bool v) { return JS_NewBool(ctx, v); }
		JSValue ToJS(JSContext* ctx, const std::string& v) { return JS_NewStringLen(ctx, v.data(), v.size()); }

		JSValue ToJS(JSContext* ctx, const vec2& v)
		{
			const float f[2] = { v.x, v.y };
			return NewPlainVec(ctx, f, 2);
		}

		JSValue ToJS(JSContext* ctx, const vec3& v)
		{
			const float f[3] = { v.x, v.y, v.z };
			return NewPlainVec(ctx, f, 3);
		}

		JSValue ToJS(JSContext* ctx, const vec4& v)
		{
			const float f[4] = { v.x, v.y, v.z, v.w };
			return NewPlainVec(ctx, f, 4);
		}

		bool FromJS(JSContext* ctx, JSValueConst v, float& out)
		{
			double d = 0.0;

			if (JS_ToFloat64(ctx, &d, v))
				return false;

			out = static_cast<float>(d);
			return true;
		}

		bool FromJS(JSContext* ctx, JSValueConst v, int& out)
		{
			int32_t i = 0;

			if (JS_ToInt32(ctx, &i, v))
				return false;

			out = i;
			return true;
		}

		bool FromJS(JSContext* ctx, JSValueConst v, uint32_t& out)
		{
			uint32_t u = 0;

			if (JS_ToUint32(ctx, &u, v))
				return false;

			out = u;
			return true;
		}

		bool FromJS(JSContext* ctx, JSValueConst v, bool& out)
		{
			const int b = JS_ToBool(ctx, v);

			if (b < 0)
				return false;

			out = b != 0;
			return true;
		}

		bool FromJS(JSContext* ctx, JSValueConst v, std::string& out)
		{
			out = ToStdString(ctx, v);
			return true;
		}

		bool FromJS(JSContext* ctx, JSValueConst v, vec2& out)
		{
			float f[2];

			if (!ReadFloats(ctx, v, f, 2))
				return false;

			out = vec2(f[0], f[1]);
			return true;
		}

		bool FromJS(JSContext* ctx, JSValueConst v, vec3& out)
		{
			float f[3];

			if (!ReadFloats(ctx, v, f, 3))
				return false;

			out = vec3(f[0], f[1], f[2]);
			return true;
		}

		bool FromJS(JSContext* ctx, JSValueConst v, vec4& out)
		{
			float f[4];

			if (!ReadFloats(ctx, v, f, 4))
				return false;

			out = vec4(f[0], f[1], f[2], f[3]);
			return true;
		}

		/** Argument optionnel : defaut si absent / undefined. */
		template <class V>
		V Arg(JSContext* ctx, int argc, JSValueConst* argv, int index, V fallback)
		{
			if (index >= argc || JS_IsUndefined(argv[index]))
				return fallback;

			V value = fallback;

			if (!FromJS(ctx, argv[index], value))
			{
				JS_FreeValue(ctx, JS_GetException(ctx));
				return fallback;
			}

			return value;
		}


		/** Array.isArray(v) (independant de la version de QuickJS-ng). */
		bool IsArray(JSContext* ctx, JSValueConst v)
		{
			if (!JS_IsObject(v))
				return false;

			JSValue global = JS_GetGlobalObject(ctx);
			JSValue array = JS_GetPropertyStr(ctx, global, "Array");
			JSValue is_array = JS_GetPropertyStr(ctx, array, "isArray");
			JSValue r = JS_Call(ctx, is_array, array, 1, &v);

			const bool result = JS_ToBool(ctx, r) > 0;

			JS_FreeValue(ctx, r);
			JS_FreeValue(ctx, is_array);
			JS_FreeValue(ctx, array);
			JS_FreeValue(ctx, global);
			return result;
		}

		int64_t LengthOf(JSContext* ctx, JSValueConst v)
		{
			JSValue length = JS_GetPropertyStr(ctx, v, "length");
			int64_t n = 0;

			if (JS_ToInt64(ctx, &n, length))
			{
				JS_FreeValue(ctx, JS_GetException(ctx));
				n = 0;
			}

			JS_FreeValue(ctx, length);
			return n;
		}


		// ====================================================================
		// Registre des types de composants
		// ====================================================================

		JSClassID g_component_class = 0;

		struct CompRef
		{
			uint32_t entity;
			uint32_t type_id;
		};

		using GetterFn = std::function<JSValue(JSContext*, Component&)>;
		using SetterFn = std::function<JSValue(JSContext*, Component&, JSValueConst)>;
		using MethodFn = std::function<JSValue(JSContext*, Component&, Actor&, int, JSValueConst*)>;

		struct TypeBinding
		{
			std::string name;
			uint32_t type_id = 0;
			std::function<Component*(Actor&)> add;
			std::function<void(Actor&)> remove;
			JSValue proto = JS_UNDEFINED;
		};

		struct PropDesc
		{
			GetterFn get;
			SetterFn set;
		};

		std::vector<TypeBinding>& Types()
		{
			static std::vector<TypeBinding> types;
			return types;
		}

		std::vector<PropDesc>& Props()
		{
			static std::vector<PropDesc> props;
			return props;
		}

		std::vector<MethodFn>& Methods()
		{
			static std::vector<MethodFn> methods;
			return methods;
		}

		const TypeBinding* FindType(const std::string& name)
		{
			for (const TypeBinding& t : Types())
			{
				if (t.name == name)
					return &t;
			}

			return nullptr;
		}

		const TypeBinding* FindType(uint32_t type_id)
		{
			for (const TypeBinding& t : Types())
			{
				if (t.type_id == type_id)
					return &t;
			}

			return nullptr;
		}

		void CompRefFinalizer(JSRuntime*, JSValueConst val)
		{
			delete static_cast<CompRef*>(JS_GetOpaque(val, g_component_class));
		}

		JSValue NewComponentObject(JSContext* ctx, Actor& actor, const TypeBinding& type)
		{
			JSValue obj = JS_NewObjectProtoClass(ctx, type.proto, g_component_class);

			if (JS_IsException(obj))
				return obj;

			JS_SetOpaque(obj, new CompRef{ actor.GetEntity(), type.type_id });
			return obj;
		}

		/** Composant d'un objet JS (nullptr + exception si disparu). */
		Component* Resolve(JSContext* ctx, JSValueConst this_val, Actor** actor_out = nullptr)
		{
			auto* ref = static_cast<CompRef*>(JS_GetOpaque(this_val, g_component_class));

			if (!ref)
			{
				JS_ThrowTypeError(ctx, "not a component");
				return nullptr;
			}

			Actor* actor = ecs::GetActor(ref->entity);
			Component* comp = actor ? actor->GetComponentInternal(ref->type_id) : nullptr;

			if (!comp)
			{
				JS_ThrowReferenceError(ctx, "this component has been removed (or its actor destroyed)");
				return nullptr;
			}

			if (actor_out)
				*actor_out = actor;

			return comp;
		}

		JSValue PropGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			Component* c = Resolve(ctx, this_val);

			if (!c)
				return JS_EXCEPTION;

			const PropDesc& p = Props()[magic];
			return p.get ? p.get(ctx, *c) : JS_UNDEFINED;
		}

		JSValue PropSet(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			Component* c = Resolve(ctx, this_val);

			if (!c)
				return JS_EXCEPTION;

			const PropDesc& p = Props()[magic];

			if (!p.set)
				return JS_ThrowTypeError(ctx, "read-only property");

			return p.set(ctx, *c, argv[0]);
		}

		JSValue MethodCall(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic)
		{
			Actor* actor = nullptr;
			Component* c = Resolve(ctx, this_val, &actor);

			if (!c)
				return JS_EXCEPTION;

			return Methods()[magic](ctx, *c, *actor, argc, argv);
		}


		// ====================================================================
		// Binder<T> : description d'un type de composant
		// ====================================================================

		template <class T>
		class Binder
		{
		public:
			Binder(JSContext* ctx, const char* name)
				: ctx_(ctx)
			{
				TypeBinding type;
				type.name = name;
				type.type_id = detail::ComponentTypeId<T>();
				type.add = [](Actor& a) -> Component* { return &a.AddComponent<T>(); };
				type.remove = [](Actor& a) { a.RemoveComponent<T>(); };
				type.proto = JS_NewObject(ctx);

				proto_ = type.proto;
				Types().push_back(std::move(type));

				// Commun a tous les composants
				prop("tickEnabled",
					[](JSContext* c, T& t) { return ToJS(c, t.tick_enabled); },
					[](JSContext* c, T& t, JSValueConst v) -> JSValue
					{
						bool b = true;
						if (!FromJS(c, v, b)) return JS_EXCEPTION;
						t.tick_enabled = b;
						return JS_UNDEFINED;
					});

				get("owner", [](JSContext* c, T& t) { return ActorObject(c, t.GetOwner()); });
				get("hasBegunPlay", [](JSContext* c, T& t) { return ToJS(c, t.HasBegunPlay()); });

				JS_SetPropertyStr(ctx, proto_, "type", JS_NewString(ctx, name));
			}

			/** Champ du composant (ou d'une classe de base) en lecture / ecriture. */
			template <class C, class V>
			Binder& field(const char* name, V C::*member)
			{
				static_assert(std::is_base_of_v<C, T>, "member of another class");

				return prop(name,
					[member](JSContext* c, T& t) { return ToJS(c, static_cast<C&>(t).*member); },
					[member, name](JSContext* c, T& t, JSValueConst v) -> JSValue
					{
						V value{};

						if (!FromJS(c, v, value))
							return JS_ThrowTypeError(c, "wrong value type for '%s'", name);

						static_cast<C&>(t).*member = value;
						return JS_UNDEFINED;
					});
			}

			Binder& prop(const char* name,
			             std::function<JSValue(JSContext*, T&)> getter,
			             std::function<JSValue(JSContext*, T&, JSValueConst)> setter)
			{
				PropDesc desc;

				if (getter)
					desc.get = [getter](JSContext* c, Component& comp) { return getter(c, static_cast<T&>(comp)); };

				if (setter)
					desc.set = [setter](JSContext* c, Component& comp, JSValueConst v) { return setter(c, static_cast<T&>(comp), v); };

				Props().push_back(std::move(desc));
				const int index = static_cast<int>(Props().size()) - 1;

				DefGetSet(ctx_, proto_, name, PropGet, setter ? PropSet : nullptr, index);
				return *this;
			}

			Binder& get(const char* name, std::function<JSValue(JSContext*, T&)> getter)
			{
				return prop(name, std::move(getter), nullptr);
			}

			Binder& method(const char* name, int length,
			               std::function<JSValue(JSContext*, T&, Actor&, int, JSValueConst*)> fn)
			{
				Methods().push_back([fn](JSContext* c, Component& comp, Actor& a, int argc, JSValueConst* argv)
				{
					return fn(c, static_cast<T&>(comp), a, argc, argv);
				});

				DefFuncMagic(ctx_, proto_, name, MethodCall, length, static_cast<int>(Methods().size()) - 1);
				return *this;
			}

			/** Methode sans argument ni valeur de retour. */
			Binder& action(const char* name, std::function<void(T&)> fn)
			{
				return method(name, 0, [fn](JSContext*, T& t, Actor&, int, JSValueConst*) -> JSValue
				{
					fn(t);
					return JS_UNDEFINED;
				});
			}

		private:
			JSContext* ctx_;
			JSValue proto_;
		};


		// ====================================================================
		// Champs communs des sprites
		// ====================================================================

		template <class T>
		void BindSpriteFields(Binder<T>& b)
		{
			b.field("size", &SpriteComponent::size)
			 .field("offset", &SpriteComponent::offset)
			 .field("visible", &SpriteComponent::visible)
			 .field("flipX", &SpriteComponent::flip_x)
			 .field("flipY", &SpriteComponent::flip_y)
			 .action("refresh", [](T& t) { t.Refresh(); });
		}


		// ====================================================================
		// Conditions de transition (AnimationSprite)
		// ====================================================================

		/**
		 * Condition JS : une fonction (this = acteur, argument = le composant)
		 * qui retourne un booleen, ou un objet :
		 *   { param: "speed", op: ">", value: 0.1 }     (> >= < <= == !=)
		 *   { isTrue: "airborne" } / { isFalse: "airborne" } / { trigger: "hurt" }
		 * null / undefined : toujours vrai.
		 */
		bool ToCondition(JSContext* ctx, JSValueConst v, Actor& actor, uint32_t type_id,
		                 anim_manager::condition& out)
		{
			if (JS_IsUndefined(v) || JS_IsNull(v))
			{
				out = nullptr;
				return true;
			}

			if (JS_IsFunction(ctx, v))
			{
				JsRef ref = MakeRef(ctx, v, actor.GetEntity());
				const uint32_t entity = actor.GetEntity();

				out = [ref, entity, type_id](const anim_manager&) -> bool
				{
					JSContext* c = Context();
					Actor* a = ecs::GetActor(entity);
					const TypeBinding* type = FindType(type_id);

					if (!c || !a || !type)
						return false;

					JSValue comp = NewComponentObject(c, *a, *type);
					JSValue r = CallRef(ref, 1, &comp);
					JS_FreeValue(c, comp);

					const int b = JS_ToBool(c, r);
					JS_FreeValue(c, r);
					return b > 0;
				};

				return true;
			}

			if (!JS_IsObject(v))
				return false;

			auto read_string = [&](const char* key, std::string& s) -> bool
			{
				JSValue f = JS_GetPropertyStr(ctx, v, key);
				const bool ok = JS_IsString(f);

				if (ok)
					s = ToStdString(ctx, f);

				JS_FreeValue(ctx, f);
				return ok;
			};

			std::string name;

			if (read_string("trigger", name))
			{
				out = [name](const anim_manager& m) { return m.has_trigger(name); };
				return true;
			}

			if (read_string("isTrue", name))
			{
				out = [name](const anim_manager& m) { return m.get_bool(name); };
				return true;
			}

			if (read_string("isFalse", name))
			{
				out = [name](const anim_manager& m) { return !m.get_bool(name); };
				return true;
			}

			if (read_string("param", name))
			{
				std::string op = ">";
				read_string("op", op);

				JSValue value_js = JS_GetPropertyStr(ctx, v, "value");
				float value = 0.f;
				FromJS(ctx, value_js, value);
				JS_FreeValue(ctx, value_js);

				if (op == ">")       out = [name, value](const anim_manager& m) { return m.get_float(name) > value; };
				else if (op == ">=") out = [name, value](const anim_manager& m) { return m.get_float(name) >= value; };
				else if (op == "<")  out = [name, value](const anim_manager& m) { return m.get_float(name) < value; };
				else if (op == "<=") out = [name, value](const anim_manager& m) { return m.get_float(name) <= value; };
				else if (op == "==") out = [name, value](const anim_manager& m) { return m.get_float(name) == value; };
				else if (op == "!=") out = [name, value](const anim_manager& m) { return m.get_float(name) != value; };
				else
					return false;

				return true;
			}

			return false;
		}

		JSValue ThrowConditionError(JSContext* ctx)
		{
			return JS_ThrowTypeError(ctx,
				"condition : function(sprite) or {param, op, value} / {isTrue} / {isFalse} / {trigger}");
		}

		std::string ModeName(AnimationSpriteComponent::Mode mode)
		{
			return mode == AnimationSpriteComponent::Mode::Single ? "single" : "stateMachine";
		}


		// ====================================================================
		// Enregistrement des types
		// ====================================================================

		void RegisterTypes(JSContext* ctx)
		{
			// ---------------- StaticSprite ----------------
			{
				Binder<StaticSpriteComponent> b(ctx, "StaticSprite");
				BindSpriteFields(b);
				b.field("texture", &StaticSpriteComponent::texture)
				 .field("region", &StaticSpriteComponent::region);
			}

			// ---------------- AnimationSprite ----------------
			{
				using AS = AnimationSpriteComponent;
				Binder<AS> b(ctx, "AnimationSprite");
				BindSpriteFields(b);

				b.prop("mode",
					[](JSContext* c, AS& t) { return ToJS(c, ModeName(t.mode)); },
					[](JSContext* c, AS& t, JSValueConst v) -> JSValue
					{
						const std::string m = ToStdString(c, v);

						if (m == "single")
							t.mode = AS::Mode::Single;
						else if (m == "stateMachine")
							t.mode = AS::Mode::StateMachine;
						else
							return JS_ThrowTypeError(c, "mode : \"single\" or \"stateMachine\"");

						return JS_UNDEFINED;
					});

				b.field("texture", &AS::texture)
				 .field("frameCount", &AS::frame_count)
				 .field("frameTime", &AS::frame_time)
				 .field("loop", &AS::loop)
				 .field("autoPlay", &AS::auto_play)
				 .get("playing", [](JSContext* c, AS& t) { return ToJS(c, t.IsPlaying()); })
				 .get("finished", [](JSContext* c, AS& t) { return ToJS(c, t.IsFinished()); })
				 .get("frame", [](JSContext* c, AS& t) { return ToJS(c, t.GetFrame()); })
				 .get("currentState", [](JSContext* c, AS& t) { return ToJS(c, t.GetCurrentState()); });

				// --- Single ---
				b.method("setAnimation", 4, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					t.SetAnimation(
						Arg<std::string>(c, argc, argv, 0, ""),
						Arg<int>(c, argc, argv, 1, 1),
						Arg<float>(c, argc, argv, 2, 0.1f),
						Arg<bool>(c, argc, argv, 3, true));
					return JS_UNDEFINED;
				})
				.action("play", [](AS& t) { t.Play(); })
				.action("pause", [](AS& t) { t.Pause(); })
				.action("stop", [](AS& t) { t.Stop(); })
				.action("restart", [](AS& t) { t.Restart(); })
				.action("clearFrameEvents", [](AS& t) { t.ClearFrameEvents(); })
				.method("onFrame", 2, [](JSContext* c, AS& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					if (argc < 2 || !JS_IsFunction(c, argv[1]))
						return JS_ThrowTypeError(c, "onFrame(frame, function)");

					t.AddFrameEvent(Arg<int>(c, argc, argv, 0, 1), ToCallback(c, argv[1], a.GetEntity()));
					return JS_UNDEFINED;
				})
				.method("onFinished", 1, [](JSContext* c, AS& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					t.OnFinished(argc > 0 ? ToCallback(c, argv[0], a.GetEntity()) : std::function<void()>());
					return JS_UNDEFINED;
				});

				// --- StateMachine ---
				b.method("addAnimation", 5, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					const std::string name = Arg<std::string>(c, argc, argv, 0, "");

					if (name.empty())
						return JS_ThrowTypeError(c, "addAnimation(name, texture, frameCount, loop, frameTime)");

					t.AddAnimation(
						name,
						Arg<std::string>(c, argc, argv, 1, ""),
						Arg<int>(c, argc, argv, 2, 1),
						Arg<bool>(c, argc, argv, 3, true),
						Arg<float>(c, argc, argv, 4, 0.1f));
					return JS_UNDEFINED;
				})
				.method("onAnimationFrame", 3, [](JSContext* c, AS& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					animation* anim = t.GetAnimation(Arg<std::string>(c, argc, argv, 0, ""));

					if (!anim || argc < 3 || !JS_IsFunction(c, argv[2]))
						return JS_ThrowTypeError(c, "onAnimationFrame(animation, frame, function)");

					anim->add_event(Arg<int>(c, argc, argv, 1, 1), ToCallback(c, argv[2], a.GetEntity()));
					return JS_UNDEFINED;
				})
				.method("onAnimationFinished", 2, [](JSContext* c, AS& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					animation* anim = t.GetAnimation(Arg<std::string>(c, argc, argv, 0, ""));

					if (!anim)
						return JS_ThrowTypeError(c, "onAnimationFinished(animation, function)");

					anim->on_finished(argc > 1 ? ToCallback(c, argv[1], a.GetEntity()) : std::function<void()>());
					return JS_UNDEFINED;
				})
				// addBlendSpace("move", [[0, "idle"], [5, "run"]]) ou [{position, animation}]
				.method("addBlendSpace", 2, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					const std::string name = Arg<std::string>(c, argc, argv, 0, "");

					if (name.empty())
						return JS_ThrowTypeError(c, "addBlendSpace(name, samples)");

					t.AddBlendSpace(name);

					if (argc < 2 || !IsArray(c, argv[1]))
						return JS_UNDEFINED;

					const int64_t length = LengthOf(c, argv[1]);

					for (int64_t i = 0; i < length; ++i)
					{
						JSValue sample = JS_GetPropertyInt64(c, argv[1], i);
						const bool pair = IsArray(c, sample);
						JSValue pos_js = pair ? JS_GetPropertyUint32(c, sample, 0) : JS_GetPropertyStr(c, sample, "position");
						JSValue anim_js = pair ? JS_GetPropertyUint32(c, sample, 1) : JS_GetPropertyStr(c, sample, "animation");

						float position = 0.f;
						FromJS(c, pos_js, position);
						const std::string anim = ToStdString(c, anim_js);

						JS_FreeValue(c, pos_js);
						JS_FreeValue(c, anim_js);
						JS_FreeValue(c, sample);

						if (!t.AddBlendSample(name, position, anim))
							return JS_ThrowTypeError(c, "addBlendSpace : no animation \"%s\"", anim.c_str());
					}

					return JS_UNDEFINED;
				})
				// addState(name, animationOrBlendSpace, {interruptible, restartOnEnter, next, variable, onEnter, onExit})
				.method("addState", 3, [](JSContext* c, AS& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					const std::string state = Arg<std::string>(c, argc, argv, 0, "");
					const std::string target = Arg<std::string>(c, argc, argv, 1, state);

					anim_state_options options;

					if (argc > 2 && JS_IsObject(argv[2]))
					{
						JSValueConst o = argv[2];
						auto read = [&](const char* key) { return JS_GetPropertyStr(c, o, key); };

						JSValue v = read("interruptible");
						if (!JS_IsUndefined(v)) FromJS(c, v, options.interruptible);
						JS_FreeValue(c, v);

						v = read("restartOnEnter");
						if (!JS_IsUndefined(v)) FromJS(c, v, options.restart_on_enter);
						JS_FreeValue(c, v);

						v = read("next");
						if (JS_IsString(v)) options.next_state = ToStdString(c, v);
						JS_FreeValue(c, v);

						v = read("variable");
						if (JS_IsString(v)) options.variable = ToStdString(c, v);
						JS_FreeValue(c, v);

						v = read("onEnter");
						options.on_enter = ToCallback(c, v, a.GetEntity());
						JS_FreeValue(c, v);

						v = read("onExit");
						options.on_exit = ToCallback(c, v, a.GetEntity());
						JS_FreeValue(c, v);
					}

					if (!t.AddState(state, target, std::move(options)))
						return JS_ThrowTypeError(c, "addState : no animation or blend space \"%s\"", target.c_str());

					return JS_UNDEFINED;
				})
				.method("setDefaultState", 1, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					t.SetDefaultState(Arg<std::string>(c, argc, argv, 0, ""));
					return JS_UNDEFINED;
				})
				// addTransition(from, to, condition, priority = 0, waitFinished = false)
				.method("addTransition", 5, [](JSContext* c, AS& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					anim_manager::condition cond;

					if (!ToCondition(c, argc > 2 ? argv[2] : JS_UNDEFINED, a,
					                 detail::ComponentTypeId<AS>(), cond))
					{
						return ThrowConditionError(c);
					}

					t.GetAnimManager().add_transition(
						Arg<std::string>(c, argc, argv, 0, ""),
						Arg<std::string>(c, argc, argv, 1, ""),
						std::move(cond),
						Arg<int>(c, argc, argv, 3, 0),
						Arg<bool>(c, argc, argv, 4, false));
					return JS_UNDEFINED;
				})
				// addAnyTransition(to, condition, priority = 0)
				.method("addAnyTransition", 3, [](JSContext* c, AS& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					anim_manager::condition cond;

					if (!ToCondition(c, argc > 1 ? argv[1] : JS_UNDEFINED, a,
					                 detail::ComponentTypeId<AS>(), cond))
					{
						return ThrowConditionError(c);
					}

					t.GetAnimManager().add_any_transition(
						Arg<std::string>(c, argc, argv, 0, ""),
						std::move(cond),
						Arg<int>(c, argc, argv, 2, 0));
					return JS_UNDEFINED;
				})
				.method("setFloat", 2, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					t.SetFloat(Arg<std::string>(c, argc, argv, 0, ""), Arg<float>(c, argc, argv, 1, 0.f));
					return JS_UNDEFINED;
				})
				.method("setBool", 2, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					t.SetBool(Arg<std::string>(c, argc, argv, 0, ""), Arg<bool>(c, argc, argv, 1, false));
					return JS_UNDEFINED;
				})
				.method("setInt", 2, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					t.SetInt(Arg<std::string>(c, argc, argv, 0, ""), Arg<int>(c, argc, argv, 1, 0));
					return JS_UNDEFINED;
				})
				.method("trigger", 1, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					t.SetTrigger(Arg<std::string>(c, argc, argv, 0, ""));
					return JS_UNDEFINED;
				})
				.method("getFloat", 1, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					return ToJS(c, t.GetFloat(Arg<std::string>(c, argc, argv, 0, "")));
				})
				.method("getBool", 1, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					return ToJS(c, t.GetBool(Arg<std::string>(c, argc, argv, 0, "")));
				})
				.method("forceState", 1, [](JSContext* c, AS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					t.ForceState(Arg<std::string>(c, argc, argv, 0, ""));
					return JS_UNDEFINED;
				})
				.action("stopAll", [](AS& t) { t.GetAnimManager().StopAllAnimations(); })
				.action("resumeAll", [](AS& t) { t.GetAnimManager().RestartAllAnimations(); });
			}

			// ---------------- Camera ----------------
			{
				Binder<CameraComponent> b(ctx, "Camera");
				b.field("offset", &CameraComponent::offset)
				 .field("rotation", &CameraComponent::rotation)
				 .field("fov", &CameraComponent::fov)
				 .field("near", &CameraComponent::near_plane)
				 .field("far", &CameraComponent::far_plane)
				 .field("followSpeed", &CameraComponent::follow_speed)
				 .field("useCameraShake", &CameraComponent::use_camera_shake)
				 .field("autoActivate", &CameraComponent::auto_activate)
				 .get("active", [](JSContext* c, CameraComponent& t) { return ToJS(c, t.IsActive()); })
				 .action("activate", [](CameraComponent& t) { t.Activate(); })
				 .method("activateFor", 1, [](JSContext*, CameraComponent& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				 {
					 // Vue de ce joueur (null : celui qui possede l'acteur).
					 t.ActivateFor(argc > 0 ? PlayerFromJS(argv[0]) : nullptr);
					 return JS_UNDEFINED;
				 })
				 .action("snap", [](CameraComponent& t) { t.SnapToTarget(); });
			}

			// ---------------- BoxCollider ----------------
			{
				using BC = BoxColliderComponent;
				Binder<BC> b(ctx, "BoxCollider");

				b.field("size", &BC::size)
				 .field("offset", &BC::offset)
				 .field("trigger", &BC::trigger)
				 .field("layer", &BC::layer)
				 .field("mask", &BC::mask)
				 .field("collideWithVoxels", &BC::collide_with_voxels)
				 .field("debugDraw", &BC::debug_draw);

				// voxelFlags = 0x20 ou ["ROCK", "SAND"] (noms de voxels.json)
				b.prop("voxelFlags",
					[](JSContext* c, BC& t) { return ToJS(c, t.voxel_flags); },
					[](JSContext* c, BC& t, JSValueConst v) -> JSValue
					{
						if (IsArray(c, v))
						{
							uint32_t flags = 0;
							const int64_t length = LengthOf(c, v);

							for (int64_t i = 0; i < length; ++i)
							{
								JSValue item = JS_GetPropertyInt64(c, v, i);
								flags |= voxels::GetFlagMask(ToStdString(c, item).c_str());
								JS_FreeValue(c, item);
							}

							t.voxel_flags = flags;
							return JS_UNDEFINED;
						}

						uint32_t flags = 0;
						if (!FromJS(c, v, flags))
							return JS_EXCEPTION;

						t.voxel_flags = flags;
						return JS_UNDEFINED;
					});

				b.method("moveAndCollide", 1, [](JSContext* c, BC& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					vec3 delta;

					if (argc < 1 || !FromJS(c, argv[0], delta))
						return JS_ThrowTypeError(c, "moveAndCollide({x, y, z})");

					const vec3 moved = t.MoveAndCollide(delta);

					JSValue r = ToJS(c, moved);
					JS_SetPropertyStr(c, r, "blockedX", JS_NewBool(c, t.WasBlockedX()));
					JS_SetPropertyStr(c, r, "blockedY", JS_NewBool(c, t.WasBlockedY()));
					return r;
				})
				.method("overlapping", 0, [](JSContext* c, BC& t, Actor&, int, JSValueConst*) -> JSValue
				{
					JSValue arr = JS_NewArray(c);
					uint32_t i = 0;

					for (BC* other : t.GetOverlapping())
						JS_SetPropertyUint32(c, arr, i++, ActorObject(c, other->GetOwner()));

					return arr;
				})
				// isOverlapping(otherActor)
				.method("isOverlapping", 1, [](JSContext* c, BC& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				{
					Actor* other = argc > 0 ? ActorFromJS(argv[0]) : nullptr;
					BC* box = other ? other->GetComponent<BC>() : nullptr;
					return ToJS(c, box != nullptr && t.IsOverlapping(*box));
				})
				.method("overlapsVoxels", 0, [](JSContext* c, BC& t, Actor&, int, JSValueConst*) -> JSValue
				{
					return ToJS(c, t.OverlapsVoxels());
				})
				.method("onBeginOverlap", 1, [](JSContext* c, BC& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					if (argc < 1 || !JS_IsFunction(c, argv[0]))
					{
						t.on_begin_overlap = nullptr;
						return JS_UNDEFINED;
					}

					JsRef ref = MakeRef(c, argv[0], a.GetEntity());

					t.on_begin_overlap = [ref](BC& other)
					{
						JSContext* ctx = Context();

						if (!ctx)
							return;

						JSValue arg = ActorObject(ctx, other.GetOwner());
						JSValue r = CallRef(ref, 1, &arg);
						JS_FreeValue(ctx, r);
						JS_FreeValue(ctx, arg);
					};

					return JS_UNDEFINED;
				})
				.method("onEndOverlap", 1, [](JSContext* c, BC& t, Actor& a, int argc, JSValueConst* argv) -> JSValue
				{
					if (argc < 1 || !JS_IsFunction(c, argv[0]))
					{
						t.on_end_overlap = nullptr;
						return JS_UNDEFINED;
					}

					JsRef ref = MakeRef(c, argv[0], a.GetEntity());

					t.on_end_overlap = [ref](BC& other)
					{
						JSContext* ctx = Context();

						if (!ctx)
							return;

						JSValue arg = ActorObject(ctx, other.GetOwner());
						JSValue r = CallRef(ref, 1, &arg);
						JS_FreeValue(ctx, r);
						JS_FreeValue(ctx, arg);
					};

					return JS_UNDEFINED;
				});
			}

			// ---------------- SoundSource ----------------
			{
				using SS = SoundSourceComponent;
				Binder<SS> b(ctx, "SoundSource");

				b.field("sound", &SS::sound)
				 .field("spatial", &SS::spatial)
				 .field("loop", &SS::loop)
				 .field("playOnBegin", &SS::play_on_begin)
				 .field("volume", &SS::volume)
				 .field("pitch", &SS::pitch)
				 .field("volumeVariation", &SS::volume_variation)
				 .field("pitchVariation", &SS::pitch_variation)
				 .field("referenceDistance", &SS::reference_distance)
				 .field("maxDistance", &SS::max_distance)
				 .field("rolloff", &SS::rolloff)
				 .get("playing", [](JSContext* c, SS& t) { return ToJS(c, t.IsPlaying()); })
				 .action("play", [](SS& t) { t.Play(); })
				 .action("stop", [](SS& t) { t.Stop(); })
				 .method("playAt", 1, [](JSContext* c, SS& t, Actor&, int argc, JSValueConst* argv) -> JSValue
				 {
					 vec3 location;

					 if (argc < 1 || !FromJS(c, argv[0], location))
						 return JS_ThrowTypeError(c, "playAt({x, y, z})");

					 t.PlayAtLocation(location);
					 return JS_UNDEFINED;
				 });
			}

			// ---------------- Light ----------------
			{
				Binder<LightComponent> b(ctx, "Light");

				b.prop("type",
					[](JSContext* c, LightComponent& t)
					{
						switch (t.type)
						{
							case LightComponent::Type::Sky:         return ToJS(c, std::string("sky"));
							case LightComponent::Type::Spot:        return ToJS(c, std::string("spot"));
							case LightComponent::Type::Directional: return ToJS(c, std::string("directional"));
							default:                                return ToJS(c, std::string("point"));
						}
					},
					[](JSContext* c, LightComponent& t, JSValueConst v) -> JSValue
					{
						const std::string type = ToStdString(c, v);

						if (type == "point")
							t.type = LightComponent::Type::Point;
						else if (type == "sky")
							t.type = LightComponent::Type::Sky;
						else if (type == "spot")
							t.type = LightComponent::Type::Spot;
						else if (type == "directional" || type == "sun")
							t.type = LightComponent::Type::Directional;
						else
							return JS_ThrowTypeError(c, "type : \"point\", \"spot\", \"directional\" or \"sky\"");

						return JS_UNDEFINED;
					});

				b.field("color", &LightComponent::color)
				 .field("intensity", &LightComponent::intensity)
				 .field("offset", &LightComponent::offset)
				 .field("enabled", &LightComponent::enabled)
				 .field("attenuation", &LightComponent::attenuation)
				 .field("rotation", &LightComponent::rotation)
				 .field("useActorRotation", &LightComponent::use_actor_rotation)
				 .field("innerAngle", &LightComponent::inner_angle)
				 .field("outerAngle", &LightComponent::outer_angle)
				 .field("castShadows", &LightComponent::cast_shadows)
				 .field("shadowStrength", &LightComponent::shadow_strength);
			}

			// ---------------- Velocity / Lifetime ----------------
			{
				Binder<VelocityComponent> b(ctx, "Velocity");
				b.field("linear", &VelocityComponent::linear)
				 .field("angular", &VelocityComponent::angular)
				 .field("damping", &VelocityComponent::damping);
			}

			{
				Binder<LifetimeComponent> b(ctx, "Lifetime");
				b.field("remaining", &LifetimeComponent::remaining);
			}
		}


		// ====================================================================
		// Actor.addComponent / getComponent / hasComponent / removeComponent
		// ====================================================================

		enum ActorComponentOp
		{
			kAdd,
			kGet,
			kHas,
			kRemove
		};

		/** Applique {cle: valeur} aux proprietes du composant (setters JS). */
		JSValue ApplyOptions(JSContext* ctx, JSValueConst comp, JSValueConst options, const std::string& type)
		{
			if (!JS_IsObject(options))
				return JS_UNDEFINED;

			JSPropertyEnum* props = nullptr;
			uint32_t count = 0;

			if (JS_GetOwnPropertyNames(ctx, &props, &count, options, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
				return JS_EXCEPTION;

			JSValue result = JS_UNDEFINED;

			for (uint32_t i = 0; i < count && !JS_IsException(result); ++i)
			{
				const JSAtom atom = props[i].atom;

				// Une faute de frappe ne cree pas une propriete JS silencieuse.
				if (JS_HasProperty(ctx, comp, atom) <= 0)
				{
					const char* name = JS_AtomToCString(ctx, atom);
					result = JS_ThrowTypeError(ctx, "%s has no property '%s'", type.c_str(), name ? name : "?");
					JS_FreeCString(ctx, name);
					break;
				}

				JSValue value = JS_GetProperty(ctx, options, atom);

				if (JS_SetProperty(ctx, comp, atom, value) < 0)  // prend value
					result = JS_EXCEPTION;
			}

			for (uint32_t i = 0; i < count; ++i)
				JS_FreeAtom(ctx, props[i].atom);

			js_free(ctx, props);
			return result;
		}

		JSValue ActorComponent(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic)
		{
			Actor* actor = ActorFromJS(this_val);

			if (!actor)
				return JS_ThrowReferenceError(ctx, "this Actor has been destroyed");

			const std::string name = argc > 0 ? ToStdString(ctx, argv[0]) : std::string();
			const TypeBinding* type = FindType(name);

			if (!type)
			{
				std::string known;

				for (const TypeBinding& t : Types())
					known += (known.empty() ? "" : ", ") + t.name;

				return JS_ThrowTypeError(ctx, "unknown component \"%s\" (known : %s)", name.c_str(), known.c_str());
			}

			switch (magic)
			{
				case kAdd:
				{
					type->add(*actor);

					JSValue comp = NewComponentObject(ctx, *actor, *type);

					if (argc > 1)
					{
						JSValue r = ApplyOptions(ctx, comp, argv[1], type->name);

						if (JS_IsException(r))
						{
							JS_FreeValue(ctx, comp);
							return r;
						}
					}

					return comp;
				}

				case kGet:
					if (!actor->GetComponentInternal(type->type_id))
						return JS_NULL;

					return NewComponentObject(ctx, *actor, *type);

				case kHas:
					return JS_NewBool(ctx, actor->GetComponentInternal(type->type_id) != nullptr);

				default:
					type->remove(*actor);
					return JS_UNDEFINED;
			}
		}

		// parent.components : noms des composants (connus du JS) de l'acteur
		JSValue ActorComponentList(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int)
		{
			Actor* actor = ActorFromJS(this_val);

			if (!actor)
				return JS_ThrowReferenceError(ctx, "this Actor has been destroyed");

			JSValue arr = JS_NewArray(ctx);
			uint32_t i = 0;

			for (const TypeBinding& t : Types())
			{
				if (actor->GetComponentInternal(t.type_id))
					JS_SetPropertyUint32(ctx, arr, i++, JS_NewString(ctx, t.name.c_str()));
			}

			return arr;
		}

		JSValue ComponentToString(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int)
		{
			auto* ref = static_cast<CompRef*>(JS_GetOpaque(this_val, g_component_class));
			const TypeBinding* type = ref ? FindType(ref->type_id) : nullptr;
			Actor* actor = ref ? ecs::GetActor(ref->entity) : nullptr;

			std::string s = type ? type->name : std::string("Component");
			s += actor ? "(\"" + actor->object_id_ + "\")" : "(<removed>)";
			return JS_NewString(ctx, s.c_str());
		}

		JSValue ComponentValid(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int)
		{
			auto* ref = static_cast<CompRef*>(JS_GetOpaque(this_val, g_component_class));
			Actor* actor = ref ? ecs::GetActor(ref->entity) : nullptr;
			return JS_NewBool(ctx, actor && actor->GetComponentInternal(ref->type_id));
		}
	}


	void RegisterComponentBindings(JSContext* ctx, JSValueConst actor_proto)
	{
		JSRuntime* rt = JS_GetRuntime(ctx);

		// Nouveau runtime (apres un Shutdown) : tout est recree.
		Types().clear();
		Props().clear();
		Methods().clear();

		g_component_class = 0;
		JS_NewClassID(rt, &g_component_class);

		JSClassDef def{};
		def.class_name = "Component";
		def.finalizer = CompRefFinalizer;
		JS_NewClass(rt, g_component_class, &def);

		RegisterTypes(ctx);

		// Membres communs, definis sur chaque prototype.
		for (TypeBinding& t : Types())
		{
			DefGetSet(ctx, t.proto, "valid", ComponentValid, nullptr, 0);
			DefFuncMagic(ctx, t.proto, "toString", ComponentToString, 0, 0);
		}

		DefFuncMagic(ctx, actor_proto, "addComponent", ActorComponent, 2, kAdd);
		DefFuncMagic(ctx, actor_proto, "getComponent", ActorComponent, 1, kGet);
		DefFuncMagic(ctx, actor_proto, "hasComponent", ActorComponent, 1, kHas);
		DefFuncMagic(ctx, actor_proto, "removeComponent", ActorComponent, 1, kRemove);
		DefGetSet(ctx, actor_proto, "components", ActorComponentList, nullptr, 0);
	}


	void ShutdownComponentBindings(JSContext* ctx)
	{
		// Les composants peuvent survivre au runtime : leurs fonctions JS sont
		// liberees maintenant, les callbacks deviennent des no-op.
		for (JsFunctionRef* r : LiveRefs())
		{
			if (r->alive)
			{
				JS_FreeValue(ctx, r->fn);
				r->alive = false;
			}
		}

		for (TypeBinding& t : Types())
		{
			JS_FreeValue(ctx, t.proto);
			t.proto = JS_UNDEFINED;
		}

		Types().clear();
		Props().clear();
		Methods().clear();
	}
}
