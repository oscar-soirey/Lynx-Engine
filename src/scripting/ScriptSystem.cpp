#include "Private/ScriptSystem.h"
#include "Private/ScriptInternal.h"
#include "Scripting.h"

#include "../core/Engine.h"
#include "../core/Filesystem.h"
#include "../core/Level.h"
#include "../core/data/Typename.h"
#include "../gameplay/Actor.h"
#include "../gameplay/Components.h"
#include "../gameplay/Input.h"
#include "../gameplay/Private/ECS.h"

#include <quickjs-ng/quickjs.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

/*
 * Systeme de script JavaScript (QuickJS-ng).
 *
 * Fonctionnement :
 *  - Chaque fichier .js est compile UNE fois, enveloppe dans une fonction :
 *        (function(parent){ <contenu du fichier> ; return lookup; })
 *    On obtient une "fabrique" mise en cache par chemin.
 *  - Attacher le script a un acteur = appeler la fabrique avec l'objet JS de
 *    l'acteur. Le code de haut niveau du fichier s'execute donc une fois par
 *    acteur, et ses variables (let/var/function) sont propres a cet acteur.
 *  - `lookup(name)` retrouve une fonction du script par son nom (BeginPlay,
 *    Update, ou n'importe quelle fonction appelee depuis le C++ / un autre script).
 *
 *  - Chaque acteur a UN objet JS unique (parent === Level.find("id") est vrai,
 *    et on peut y ranger des donnees partagees entre ses scripts : parent.hp = 3).
 *    L'objet ne contient que l'id d'entite : si l'acteur est detruit, l'acces
 *    leve une erreur au lieu de lire de la memoire liberee.
 */

namespace lynx
{
	// Donne au systeme de script l'acces a l'Impl d'un ScriptComponent.
	struct ScriptAccess
	{
		static ScriptComponent::Impl* Get(ScriptComponent& c) { return c.impl_; }
	};

	namespace
	{
		constexpr const char* kLogPrefix = "[script] ";

		struct ScriptInstance
		{
			std::string path;

			// function(name) -> fonction du script ou undefined
			JSValue lookup = JS_UNDEFINED;
			// cache des fonctions deja cherchees (undefined si absente)
			std::unordered_map<std::string, JSValue> functions;

			bool loaded = false; // false : fichier absent ou erreur JS
			bool begun = false;  // BeginPlay deja appele
			bool dead = false;   // retire, libere des que possible
		};

		enum class VecField : uint8_t
		{
			Location,
			Rotation,
			Scale,
			VelocityLinear,
			VelocityAngular
		};

		// Opaque des objets JS "Vec3Ref" : reference vers un vec3 d'un acteur.
		struct Vec3Ref
		{
			uint32_t entity;
			VecField field;
		};

		struct State
		{
			JSRuntime* rt = nullptr;
			JSContext* ctx = nullptr;

			// chemin -> fabrique compilee
			std::unordered_map<std::string, JSValue> factories;
			// entite -> objet JS unique de l'acteur
			std::unordered_map<uint32_t, JSValue> actor_objects;

			std::unordered_map<std::string, std::unique_ptr<InputAction>> actions;
			std::unordered_map<std::string, std::unique_ptr<InputAxis1D>> axes;

			// > 0 pendant un appel JS : on differe alors les liberations
			int call_depth = 0;

			// ---- Classes (class Player extends Actor) ----
			JSValue actor_proto = JS_UNDEFINED;   // Actor.prototype (= proto de la classe JS Actor)
			JSValue actor_ctor = JS_UNDEFINED;    // constructeur global Actor

			// Constructeurs JS des classes C++ (Actor, Pawn...) : index = magic
			std::vector<std::string> native_names;
			std::unordered_map<std::string, JSValue> native_ctors;

			// Classes de script (.js de assets/, tout dossier) : nom -> constructeur
			struct ScriptClass
			{
				JSValue ctor = JS_UNDEFINED;
				std::string file;
			};
			std::unordered_map<std::string, ScriptClass> classes;
			bool classes_loaded = false;

			// Construction demandee par la factory (niveau, Level.spawn, editeur)
			bool factory_pending = false;
			JSValue factory_target = JS_UNDEFINED;   // emprunte (classes[...])
			Actor* factory_result = nullptr;

			// Fonctions JS utilitaires (compilees a l'init)
			JSValue fn_is_actor_class = JS_UNDEFINED;
			JSValue fn_static_properties = JS_UNDEFINED;
		};

		State* g = nullptr;

		JSClassID g_actor_class = 0;
		JSClassID g_transform_class = 0;
		JSClassID g_vec3_class = 0;

		const char* const kWrapperBegin = "(function(parent){";
		// Le "\n" protege d'un commentaire // sur la derniere ligne du fichier.
		// eval direct : voit les fonctions/variables declarees dans le script.
		const char* const kWrapperEnd =
			"\n;return function(__lynx_name__) {"
			" var __lynx_f__;"
			" try { __lynx_f__ = eval(__lynx_name__); } catch (__lynx_e__) { return undefined; }"
			" return typeof __lynx_f__ === 'function' ? __lynx_f__ : undefined;"
			" };\n})";

		void EnsureInit();


		// ====================================================================
		// Utilitaires
		// ====================================================================

		std::string ToStdString(JSContext* ctx, JSValueConst v)
		{
			const char* s = JS_ToCString(ctx, v);
			if (!s)
			{
				// ToCString peut lever (ex: Symbol) : on avale l'exception
				JS_FreeValue(ctx, JS_GetException(ctx));
				return {};
			}
			std::string out(s);
			JS_FreeCString(ctx, s);
			return out;
		}

		void LogException(JSContext* ctx, const std::string& where)
		{
			JSValue ex = JS_GetException(ctx);
			std::cout << kLogPrefix << where << ": " << ToStdString(ctx, ex) << std::endl;

			if (JS_IsObject(ex))
			{
				JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
				if (!JS_IsUndefined(stack) && !JS_IsException(stack))
					std::cout << ToStdString(ctx, stack) << std::endl;
				JS_FreeValue(ctx, stack);
			}
			JS_FreeValue(ctx, ex);
		}

		void RunPendingJobs()
		{
			// Promesses / async : execute les jobs en attente
			JSContext* job_ctx = nullptr;
			int guard = 0;
			while (guard++ < 10000)
			{
				const int r = JS_ExecutePendingJob(g->rt, &job_ctx);
				if (r == 0)
					break;
				if (r < 0 && job_ctx)
					LogException(job_ctx, "promise");
			}
		}

		bool IsIdentifier(const char* name)
		{
			if (!name || !*name)
				return false;
			for (const char* c = name; *c; ++c)
			{
				const bool alpha = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_' || *c == '$';
				const bool digit = *c >= '0' && *c <= '9';
				if (!alpha && !(digit && c != name))
					return false;
			}
			return true;
		}

		void* EncodeEntity(uint32_t e) { return reinterpret_cast<void*>(static_cast<uintptr_t>(e) + 1); }

		uint32_t DecodeEntity(void* p)
		{
			if (!p)
				return ecs::kNullEntity;
			return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - 1);
		}

		float& VecComp(vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

		/** Lit {x,y,z,(w)} ou [x,y,z,(w)] dans out. */
		bool ReadFloats(JSContext* ctx, JSValueConst v, float* out, int n)
		{
			if (!JS_IsObject(v))
				return false;

			static const char* names[] = {"x", "y", "z", "w"};
			for (int i = 0; i < n; ++i)
			{
				JSValue c = JS_GetPropertyStr(ctx, v, names[i]);
				if (JS_IsUndefined(c))
					c = JS_GetPropertyUint32(ctx, v, static_cast<uint32_t>(i));

				double d = 0.0;
				const bool ok = !JS_IsUndefined(c) && !JS_IsException(c) && JS_ToFloat64(ctx, &d, c) == 0;
				JS_FreeValue(ctx, c);
				if (!ok)
					return false;
				out[i] = static_cast<float>(d);
			}
			return true;
		}

		bool ReadVec3(JSContext* ctx, JSValueConst v, vec3& out)
		{
			float f[3];
			if (!ReadFloats(ctx, v, f, 3))
				return false;
			out = vec3(f[0], f[1], f[2]);
			return true;
		}

		JSValue NewPlainVec(JSContext* ctx, const float* f, int n)
		{
			static const char* names[] = {"x", "y", "z", "w"};
			JSValue obj = JS_NewObject(ctx);
			for (int i = 0; i < n; ++i)
				JS_SetPropertyStr(ctx, obj, names[i], JS_NewFloat64(ctx, f[i]));
			return obj;
		}

		JSValue NewPlainVec3(JSContext* ctx, const vec3& v)
		{
			const float f[3] = {v.x, v.y, v.z};
			return NewPlainVec(ctx, f, 3);
		}

		JSValue NewPlainTransform(JSContext* ctx, const transform& t)
		{
			JSValue obj = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, obj, "location", NewPlainVec3(ctx, t.location));
			JS_SetPropertyStr(ctx, obj, "rotation", NewPlainVec3(ctx, t.rotation));
			JS_SetPropertyStr(ctx, obj, "scale", NewPlainVec3(ctx, t.scale));
			return obj;
		}

		/** Lit un champ optionnel vec3 d'un objet (ex: obj.rotation). */
		bool ReadVec3Field(JSContext* ctx, JSValueConst obj, const char* name, vec3& out)
		{
			JSValue f = JS_GetPropertyStr(ctx, obj, name);
			const bool ok = !JS_IsUndefined(f) && !JS_IsException(f) && ReadVec3(ctx, f, out);
			JS_FreeValue(ctx, f);
			return ok;
		}

		/**
		 * Applique a `t` un objet du type {position|location, rotation, scale}
		 * (champs optionnels). Si `allow_plain_vec`, un simple {x,y,z} est la position.
		 */
		bool ApplyTransformObject(JSContext* ctx, JSValueConst v, transform& t, bool allow_plain_vec)
		{
			if (!JS_IsObject(v))
				return false;

			if (allow_plain_vec && ReadVec3(ctx, v, t.location))
				return true;

			bool any = false;
			vec3 tmp;
			if (ReadVec3Field(ctx, v, "position", tmp) || ReadVec3Field(ctx, v, "location", tmp))
				{ t.location = tmp; any = true; }
			if (ReadVec3Field(ctx, v, "rotation", tmp))
				{ t.rotation = tmp; any = true; }
			if (ReadVec3Field(ctx, v, "scale", tmp))
				{ t.scale = tmp; any = true; }
			return any;
		}


		// ====================================================================
		// Proprietes reflechies (Object::properties_) <-> JS
		// ====================================================================

		JSValue PropertyToJS(JSContext* ctx, const PropVariantTypePtr& prop)
		{
			return std::visit([ctx](auto* ptr) -> JSValue
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return JS_UNDEFINED;

				if constexpr (std::is_same_v<T, int>)
					return JS_NewInt32(ctx, *ptr);
				else if constexpr (std::is_same_v<T, float>)
					return JS_NewFloat64(ctx, *ptr);
				else if constexpr (std::is_same_v<T, bool>)
					return JS_NewBool(ctx, *ptr);
				else if constexpr (std::is_same_v<T, std::string>)
					return JS_NewStringLen(ctx, ptr->data(), ptr->size());
				else if constexpr (std::is_same_v<T, vec2>)
				{
					const float f[2] = {ptr->x, ptr->y};
					return NewPlainVec(ctx, f, 2);
				}
				else if constexpr (std::is_same_v<T, vec3>)
					return NewPlainVec3(ctx, *ptr);
				else if constexpr (std::is_same_v<T, vec4>)
				{
					const float f[4] = {ptr->x, ptr->y, ptr->z, ptr->w};
					return NewPlainVec(ctx, f, 4);
				}
				else if constexpr (std::is_same_v<T, transform>)
					return NewPlainTransform(ctx, *ptr);
				else
					return JS_UNDEFINED;
			}, prop);
		}

		bool JSToProperty(JSContext* ctx, JSValueConst v, const PropVariantTypePtr& prop)
		{
			return std::visit([ctx, v](auto* ptr) -> bool
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return false;

				if constexpr (std::is_same_v<T, int>)
				{
					int32_t i = 0;
					if (JS_ToInt32(ctx, &i, v)) return false;
					*ptr = i;
					return true;
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					double d = 0;
					if (JS_ToFloat64(ctx, &d, v)) return false;
					*ptr = static_cast<float>(d);
					return true;
				}
				else if constexpr (std::is_same_v<T, bool>)
				{
					const int b = JS_ToBool(ctx, v);
					if (b < 0) return false;
					*ptr = b != 0;
					return true;
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					*ptr = ToStdString(ctx, v);
					return true;
				}
				else if constexpr (std::is_same_v<T, vec2>)
				{
					float f[2];
					if (!ReadFloats(ctx, v, f, 2)) return false;
					*ptr = vec2(f[0], f[1]);
					return true;
				}
				else if constexpr (std::is_same_v<T, vec3>)
				{
					return ReadVec3(ctx, v, *ptr);
				}
				else if constexpr (std::is_same_v<T, vec4>)
				{
					float f[4];
					if (!ReadFloats(ctx, v, f, 4)) return false;
					*ptr = vec4(f[0], f[1], f[2], f[3]);
					return true;
				}
				else if constexpr (std::is_same_v<T, transform>)
				{
					return ApplyTransformObject(ctx, v, *ptr, false);
				}
				else
					return false;
			}, prop);
		}


		// ====================================================================
		// Objets JS : Actor / Transform / Vec3Ref
		// ====================================================================

		void DefineReflectedMembers(JSContext* ctx, JSValueConst obj, Actor* a);

		JSValue GetActorObject(JSContext* ctx, uint32_t entity)
		{
			if (entity == ecs::kNullEntity || !ecs::GetActor(entity))
				return JS_NULL;

			auto it = g->actor_objects.find(entity);
			if (it != g->actor_objects.end())
				return JS_DupValue(ctx, it->second);

			JSValue obj = JS_NewObjectClass(ctx, g_actor_class);
			if (JS_IsException(obj))
				return obj;
			JS_SetOpaque(obj, EncodeEntity(entity));
			g->actor_objects.emplace(entity, JS_DupValue(ctx, obj));

			// Proprietes HPROPERTY et fonctions HFUNCTION du C++ : obj.nom
			DefineReflectedMembers(ctx, obj, ecs::GetActor(entity));
			return obj;
		}

		JSValue ActorToJS(JSContext* ctx, Actor* actor)
		{
			return actor ? GetActorObject(ctx, actor->GetEntity()) : JS_NULL;
		}

		uint32_t EntityOf(JSValueConst obj, JSClassID cls)
		{
			return DecodeEntity(JS_GetOpaque(obj, cls));
		}

		Actor* JSToActor(JSValueConst obj)
		{
			return ecs::GetActor(EntityOf(obj, g_actor_class));
		}

		JSValue ThrowDestroyed(JSContext* ctx)
		{
			return JS_ThrowReferenceError(ctx, "this Actor has been destroyed");
		}

		#define LYNX_THIS_ACTOR(var) \
			Actor* var = JSToActor(this_val); \
			if (!var) return ThrowDestroyed(ctx)

		JSValue NewVec3Ref(JSContext* ctx, uint32_t entity, VecField field)
		{
			JSValue obj = JS_NewObjectClass(ctx, g_vec3_class);
			if (JS_IsException(obj))
				return obj;
			JS_SetOpaque(obj, new Vec3Ref{entity, field});
			return obj;
		}

		JSValue NewTransformRef(JSContext* ctx, uint32_t entity)
		{
			JSValue obj = JS_NewObjectClass(ctx, g_transform_class);
			if (JS_IsException(obj))
				return obj;
			JS_SetOpaque(obj, EncodeEntity(entity));
			return obj;
		}

		/**
		 * Retrouve le vec3 cible d'un Vec3Ref. Pour la velocite, si `create`
		 * est faux et que l'acteur n'a pas de VelocityComponent, retourne nullptr.
		 */
		vec3* ResolveVec3(Actor* a, VecField field, bool create)
		{
			switch (field)
			{
				case VecField::Location: return &a->transform.location;
				case VecField::Rotation: return &a->transform.rotation;
				case VecField::Scale:    return &a->transform.scale;
				case VecField::VelocityLinear:
				case VecField::VelocityAngular:
				{
					auto* vel = create ? &a->AddComponent<VelocityComponent>() : a->GetComponent<VelocityComponent>();
					if (!vel)
						return nullptr;
					return field == VecField::VelocityLinear ? &vel->linear : &vel->angular;
				}
			}
			return nullptr;
		}

		// ---- Vec3Ref -------------------------------------------------------

		void Vec3RefFinalizer(JSRuntime*, JSValueConst val)
		{
			delete static_cast<Vec3Ref*>(JS_GetOpaque(val, g_vec3_class));
		}

		Vec3Ref* ThisVec(JSValueConst this_val)
		{
			return static_cast<Vec3Ref*>(JS_GetOpaque(this_val, g_vec3_class));
		}

		JSValue Vec3Get(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			Vec3Ref* ref = ThisVec(this_val);
			Actor* a = ref ? ecs::GetActor(ref->entity) : nullptr;
			if (!a)
				return ThrowDestroyed(ctx);
			vec3* v = ResolveVec3(a, ref->field, false);
			return JS_NewFloat64(ctx, v ? VecComp(*v, magic) : 0.0);
		}

		JSValue Vec3Set(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			Vec3Ref* ref = ThisVec(this_val);
			Actor* a = ref ? ecs::GetActor(ref->entity) : nullptr;
			if (!a)
				return ThrowDestroyed(ctx);
			double d = 0;
			if (JS_ToFloat64(ctx, &d, argv[0]))
				return JS_EXCEPTION;
			VecComp(*ResolveVec3(a, ref->field, true), magic) = static_cast<float>(d);
			return JS_UNDEFINED;
		}

		// v.set(x, y, z) ou v.set({x, y, z})
		JSValue Vec3SetAll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			Vec3Ref* ref = ThisVec(this_val);
			Actor* a = ref ? ecs::GetActor(ref->entity) : nullptr;
			if (!a)
				return ThrowDestroyed(ctx);

			vec3 value;
			if (argc >= 3)
			{
				double d[3];
				for (int i = 0; i < 3; ++i)
					if (JS_ToFloat64(ctx, &d[i], argv[i])) return JS_EXCEPTION;
				value = vec3(static_cast<float>(d[0]), static_cast<float>(d[1]), static_cast<float>(d[2]));
			}
			else if (argc < 1 || !ReadVec3(ctx, argv[0], value))
				return JS_ThrowTypeError(ctx, "set() expects (x, y, z) or {x, y, z}");

			*ResolveVec3(a, ref->field, true) = value;
			return JS_DupValue(ctx, this_val);
		}

		// copie "valeur" (objet JS simple, detache de l'acteur)
		JSValue Vec3Clone(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			Vec3Ref* ref = ThisVec(this_val);
			Actor* a = ref ? ecs::GetActor(ref->entity) : nullptr;
			if (!a)
				return ThrowDestroyed(ctx);
			vec3* v = ResolveVec3(a, ref->field, false);
			return NewPlainVec3(ctx, v ? *v : vec3(0.f));
		}

		JSValue Vec3ToString(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			Vec3Ref* ref = ThisVec(this_val);
			Actor* a = ref ? ecs::GetActor(ref->entity) : nullptr;
			if (!a)
				return JS_NewString(ctx, "vec3(<destroyed>)");
			vec3* v = ResolveVec3(a, ref->field, false);
			const vec3 val = v ? *v : vec3(0.f);
			const std::string s = "vec3(" + std::to_string(val.x) + ", " + std::to_string(val.y) + ", " + std::to_string(val.z) + ")";
			return JS_NewString(ctx, s.c_str());
		}

		// ---- Transform -----------------------------------------------------

		constexpr int kTrLocation = 0, kTrRotation = 1, kTrScale = 2;

		VecField TransformField(int magic)
		{
			return magic == kTrRotation ? VecField::Rotation : (magic == kTrScale ? VecField::Scale : VecField::Location);
		}

		JSValue TransformGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			const uint32_t e = EntityOf(this_val, g_transform_class);
			if (!ecs::GetActor(e))
				return ThrowDestroyed(ctx);
			return NewVec3Ref(ctx, e, TransformField(magic));
		}

		JSValue TransformSet(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			Actor* a = ecs::GetActor(EntityOf(this_val, g_transform_class));
			if (!a)
				return ThrowDestroyed(ctx);
			vec3 v;
			if (!ReadVec3(ctx, argv[0], v))
				return JS_ThrowTypeError(ctx, "expected {x, y, z}");
			*ResolveVec3(a, TransformField(magic), true) = v;
			return JS_UNDEFINED;
		}

		JSValue TransformClone(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			Actor* a = ecs::GetActor(EntityOf(this_val, g_transform_class));
			if (!a)
				return ThrowDestroyed(ctx);
			return NewPlainTransform(ctx, a->transform);
		}

		// ---- Actor ---------------------------------------------------------

		enum ActorProp
		{
			kId,
			kClassName,
			kValid,
			kEntity,
			kTransform,
			kPosition,
			kVelocity,
			kAngularVelocity,
			kDamping,
			kLifetime,
			kTags,
			kScripts
		};

		JSValue ActorGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			Actor* a = JSToActor(this_val);
			if (magic == kValid)
				return JS_NewBool(ctx, a != nullptr);
			if (!a)
				return ThrowDestroyed(ctx);

			switch (magic)
			{
				case kId:        return JS_NewString(ctx, a->object_id_.c_str());
				case kClassName: return JS_NewString(ctx, a->GetTypeName().c_str());
				case kEntity:    return JS_NewUint32(ctx, a->GetEntity());
				case kTransform: return NewTransformRef(ctx, a->GetEntity());
				case kPosition:  return NewVec3Ref(ctx, a->GetEntity(), VecField::Location);
				case kVelocity:  return NewVec3Ref(ctx, a->GetEntity(), VecField::VelocityLinear);
				case kAngularVelocity: return NewVec3Ref(ctx, a->GetEntity(), VecField::VelocityAngular);
				case kDamping:
				{
					auto* vel = a->GetComponent<VelocityComponent>();
					return JS_NewFloat64(ctx, vel ? vel->damping : 0.0);
				}
				case kLifetime:
				{
					auto* life = a->GetComponent<LifetimeComponent>();
					return life ? JS_NewFloat64(ctx, life->remaining) : JS_UNDEFINED;
				}
				case kTags:
				{
					JSValue arr = JS_NewArray(ctx);
					if (auto* tags = a->GetComponent<TagsComponent>())
					{
						uint32_t i = 0;
						for (const auto& t : tags->tags)
							JS_SetPropertyUint32(ctx, arr, i++, JS_NewString(ctx, t.c_str()));
					}
					return arr;
				}
				case kScripts:
				{
					JSValue arr = JS_NewArray(ctx);
					if (auto* sc = a->GetComponent<ScriptComponent>())
					{
						uint32_t i = 0;
						for (const auto& p : sc->GetScripts())
							JS_SetPropertyUint32(ctx, arr, i++, JS_NewString(ctx, p.c_str()));
					}
					return arr;
				}
				default:
					return JS_UNDEFINED;
			}
		}

		JSValue ActorSet(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			LYNX_THIS_ACTOR(a);
			JSValueConst v = argv[0];

			switch (magic)
			{
				case kId:
					a->object_id_ = ToStdString(ctx, v);
					return JS_UNDEFINED;

				case kTransform:
					if (!ApplyTransformObject(ctx, v, a->transform, false))
						return JS_ThrowTypeError(ctx, "expected {position, rotation, scale}");
					return JS_UNDEFINED;

				case kPosition:
				case kVelocity:
				case kAngularVelocity:
				{
					vec3 value;
					if (!ReadVec3(ctx, v, value))
						return JS_ThrowTypeError(ctx, "expected {x, y, z}");
					const VecField f = magic == kPosition ? VecField::Location
						: (magic == kVelocity ? VecField::VelocityLinear : VecField::VelocityAngular);
					*ResolveVec3(a, f, true) = value;
					return JS_UNDEFINED;
				}

				case kDamping:
				{
					double d = 0;
					if (JS_ToFloat64(ctx, &d, v)) return JS_EXCEPTION;
					a->AddComponent<VelocityComponent>().damping = static_cast<float>(d);
					return JS_UNDEFINED;
				}

				case kLifetime:
				{
					// null/undefined : retire le composant
					if (JS_IsNull(v) || JS_IsUndefined(v))
					{
						a->RemoveComponent<LifetimeComponent>();
						return JS_UNDEFINED;
					}
					double d = 0;
					if (JS_ToFloat64(ctx, &d, v)) return JS_EXCEPTION;
					a->AddComponent<LifetimeComponent>().remaining = static_cast<float>(d);
					return JS_UNDEFINED;
				}

				default:
					return JS_ThrowTypeError(ctx, "read-only property");
			}
		}

		// Recupere une propriete reflechie (HPROPERTY) par son nom
		const property* FindProperty(Actor* a, const std::string& name)
		{
			const auto& props = a->GetProperties();
			auto it = props.find(name);
			return it == props.end() ? nullptr : &it->second;
		}

		JSValue ActorGetProperty(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv)
		{
			LYNX_THIS_ACTOR(a);
			const property* p = FindProperty(a, ToStdString(ctx, argv[0]));
			return p ? PropertyToJS(ctx, p->property_member) : JS_UNDEFINED;
		}

		JSValue ActorSetProperty(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv)
		{
			LYNX_THIS_ACTOR(a);
			const std::string name = ToStdString(ctx, argv[0]);
			const property* p = FindProperty(a, name);
			if (!p)
				return JS_ThrowReferenceError(ctx, "property '%s' does not exist", name.c_str());
			if (!JSToProperty(ctx, argv[1], p->property_member))
				return JS_ThrowTypeError(ctx, "wrong value type for property '%s'", name.c_str());
			return JS_UNDEFINED;
		}

		JSValue ActorHasProperty(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv)
		{
			LYNX_THIS_ACTOR(a);
			return JS_NewBool(ctx, FindProperty(a, ToStdString(ctx, argv[0])) != nullptr);
		}

		JSValue ActorDestroy(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			Actor* a = JSToActor(this_val);
			if (!a)
				return JS_UNDEFINED; // deja detruit : rien a faire
			if (Level* level = Engine::Get() ? Engine::Get()->GetCurrentLevel() : nullptr)
				level->DestroyActor(a);
			return JS_UNDEFINED;
		}

		enum TagOp { kAddTag, kRemoveTag, kHasTag };

		JSValue ActorTagOp(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			LYNX_THIS_ACTOR(a);
			const std::string tag = ToStdString(ctx, argv[0]);
			switch (magic)
			{
				case kAddTag:
					a->AddComponent<TagsComponent>().Add(tag);
					return JS_UNDEFINED;
				case kRemoveTag:
					if (auto* t = a->GetComponent<TagsComponent>()) t->Remove(tag);
					return JS_UNDEFINED;
				default:
				{
					auto* t = a->GetComponent<TagsComponent>();
					return JS_NewBool(ctx, t && t->Has(tag));
				}
			}
		}

		enum ScriptOp { kAddScript, kRemoveScript, kHasScript };

		JSValue ActorScriptOp(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			LYNX_THIS_ACTOR(a);
			const std::string path = ToStdString(ctx, argv[0]);
			switch (magic)
			{
				case kAddScript:
					return JS_NewBool(ctx, a->AddScript(path.c_str()));
				case kRemoveScript:
					a->RemoveScript(path.c_str());
					return JS_UNDEFINED;
				default:
				{
					auto* sc = a->GetComponent<ScriptComponent>();
					return JS_NewBool(ctx, sc && sc->HasScript(path));
				}
			}
		}

		JSValue CallActorFunction(JSContext* ctx, Actor* a, const char* name, int argc, JSValueConst* argv, bool* found);

		// other.call("TakeDamage", 10) : appelle la fonction dans les scripts de l'acteur
		JSValue ActorCall(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_ACTOR(a);
			if (argc < 1)
				return JS_ThrowTypeError(ctx, "call(name, ...args)");
			const std::string name = ToStdString(ctx, argv[0]);
			bool found = false;
			return CallActorFunction(ctx, a, name.c_str(), argc - 1, argv + 1, &found);
		}

		JSValue ActorToString(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			Actor* a = JSToActor(this_val);
			if (!a)
				return JS_NewString(ctx, "Actor(<destroyed>)");
			const std::string s = "Actor(" + a->GetTypeName() + ", \"" + a->object_id_ + "\")";
			return JS_NewString(ctx, s.c_str());
		}


		// ====================================================================
		// Globaux : print, console, vec3, Level, Input, Engine
		// ====================================================================

		std::string JoinArgs(JSContext* ctx, int argc, JSValueConst* argv)
		{
			std::string out;
			for (int i = 0; i < argc; ++i)
			{
				if (i) out += ' ';
				out += ToStdString(ctx, argv[i]);
			}
			return out;
		}

		// magic : 0 log, 1 warn, 2 error
		JSValue JsPrint(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic)
		{
			static const char* prefixes[] = {"", "[warning] ", "[error] "};
			std::cout << kLogPrefix << prefixes[magic] << JoinArgs(ctx, argc, argv) << std::endl;
			return JS_UNDEFINED;
		}

		JSValue JsVec3(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			double d[3] = {0, 0, 0};
			for (int i = 0; i < 3 && i < argc; ++i)
				if (JS_ToFloat64(ctx, &d[i], argv[i])) return JS_EXCEPTION;
			if (argc == 1)
				d[1] = d[2] = d[0];
			return NewPlainVec3(ctx, vec3(static_cast<float>(d[0]), static_cast<float>(d[1]), static_cast<float>(d[2])));
		}

		Level* CurrentLevel()
		{
			return Engine::Get() ? Engine::Get()->GetCurrentLevel() : nullptr;
		}

		// Level.spawn("ClassName", {position, rotation, scale} | {x,y,z})
		JSValue LevelSpawn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			Level* level = CurrentLevel();
			if (!level)
				return JS_ThrowInternalError(ctx, "no level loaded");

			const std::string cls = ToStdString(ctx, argv[0]);
			Actor* a = level->SpawnActor(cls.c_str());
			if (!a)
				return JS_NULL;

			if (argc > 1)
				ApplyTransformObject(ctx, argv[1], a->transform, true);
			return ActorToJS(ctx, a);
		}

		JSValue LevelFind(JSContext* ctx, JSValueConst, int, JSValueConst* argv)
		{
			Level* level = CurrentLevel();
			if (!level)
				return JS_NULL;
			const std::string id = ToStdString(ctx, argv[0]);
			return ActorToJS(ctx, level->GetActorFromID(id.c_str()));
		}

		JSValue LevelFindWithTag(JSContext* ctx, JSValueConst, int, JSValueConst* argv)
		{
			const std::string tag = ToStdString(ctx, argv[0]);
			std::vector<Actor*> actors;
			ecs::CollectActorsWith(detail::ComponentTypeId<TagsComponent>(), actors);

			JSValue arr = JS_NewArray(ctx);
			uint32_t i = 0;
			for (Actor* a : actors)
			{
				auto* t = a->GetComponent<TagsComponent>();
				if (t && t->Has(tag))
					JS_SetPropertyUint32(ctx, arr, i++, ActorToJS(ctx, a));
			}
			return arr;
		}

		JSValue LevelAll(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			JSValue arr = JS_NewArray(ctx);
			if (Level* level = CurrentLevel())
			{
				uint32_t i = 0;
				for (Actor* a : level->GetActors())
					JS_SetPropertyUint32(ctx, arr, i++, ActorToJS(ctx, a));
			}
			return arr;
		}

		JSValue LevelCount(JSContext* ctx, JSValueConst, int, JSValueConst* argv)
		{
			Level* level = CurrentLevel();
			if (!level)
				return JS_NewInt32(ctx, 0);
			const std::string cls = ToStdString(ctx, argv[0]);
			return JS_NewInt32(ctx, level->CountActorsOfClass(cls.c_str()));
		}

		JSValue LevelDestroy(JSContext* ctx, JSValueConst, int, JSValueConst* argv)
		{
			Actor* a = JSToActor(argv[0]);
			Level* level = CurrentLevel();
			if (a && level)
				level->DestroyActor(a);
			return JS_UNDEFINED;
		}

		InputAction& GetAction(const std::string& name)
		{
			auto& slot = g->actions[name];
			if (!slot)
				slot = std::make_unique<InputAction>(name.c_str());
			return *slot;
		}

		enum InputOp { kPressed, kHeld, kReleased };

		JSValue InputActionOp(JSContext* ctx, JSValueConst, int, JSValueConst* argv, int magic)
		{
			InputAction& action = GetAction(ToStdString(ctx, argv[0]));
			bool r = false;
			switch (magic)
			{
				case kPressed:  r = action.IsPressed(); break;
				case kHeld:     r = action.IsHeld(); break;
				default:        r = action.IsReleased(); break;
			}
			return JS_NewBool(ctx, r);
		}

		JSValue InputAxis(JSContext* ctx, JSValueConst, int, JSValueConst* argv)
		{
			const std::string name = ToStdString(ctx, argv[0]);
			auto& slot = g->axes[name];
			if (!slot)
				slot = std::make_unique<InputAxis1D>(name.c_str());
			return JS_NewFloat64(ctx, slot->GetValue());
		}

		JSValue EngineGetTimeDilation(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			return JS_NewFloat64(ctx, Engine::Get() ? Engine::Get()->GetGlobalTimeDilatation() : 1.0);
		}

		// Engine.setTimeDilation(value) ou Engine.setTimeDilation(value, duree)
		JSValue EngineSetTimeDilation(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			double v = 1, dur = 0;
			if (JS_ToFloat64(ctx, &v, argv[0])) return JS_EXCEPTION;
			if (argc > 1 && JS_ToFloat64(ctx, &dur, argv[1])) return JS_EXCEPTION;
			if (Engine* e = Engine::Get())
			{
				if (argc > 1)
					e->SetGlobalTimeDilatation(static_cast<float>(v), static_cast<float>(dur));
				else
					e->SetGlobalTimeDilatation(static_cast<float>(v));
			}
			return JS_UNDEFINED;
		}

		JSValue EngineIsPlaying(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			return JS_NewBool(ctx, ecs::IsPlaying());
		}


		// ====================================================================
		// Enregistrement
		// ====================================================================

		void DefGetSet(JSContext* ctx, JSValueConst obj, const char* name, JSCFunctionMagic* getter, JSCFunctionMagic* setter, int magic)
		{
			JSAtom atom = JS_NewAtom(ctx, name);
			JSValue get = getter ? JS_NewCFunctionMagic(ctx, getter, name, 0, JS_CFUNC_generic_magic, magic) : JS_UNDEFINED;
			JSValue set = setter ? JS_NewCFunctionMagic(ctx, setter, name, 1, JS_CFUNC_generic_magic, magic) : JS_UNDEFINED;
			JS_DefinePropertyGetSet(ctx, obj, atom, get, set, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
			JS_FreeAtom(ctx, atom);
		}

		void DefFunc(JSContext* ctx, JSValueConst obj, const char* name, JSCFunction* fn, int length)
		{
			JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, length));
		}

		void DefFuncMagic(JSContext* ctx, JSValueConst obj, const char* name, JSCFunctionMagic* fn, int length, int magic)
		{
			JS_SetPropertyStr(ctx, obj, name, JS_NewCFunctionMagic(ctx, fn, name, length, JS_CFUNC_generic_magic, magic));
		}

		void RegisterClasses(JSContext* ctx)
		{
			JSRuntime* rt = g->rt;

			g_actor_class = 0;
			g_transform_class = 0;
			g_vec3_class = 0;
			JS_NewClassID(rt, &g_actor_class);
			JS_NewClassID(rt, &g_transform_class);
			JS_NewClassID(rt, &g_vec3_class);

			JSClassDef actor_def{};
			actor_def.class_name = "Actor";
			JS_NewClass(rt, g_actor_class, &actor_def);

			JSClassDef transform_def{};
			transform_def.class_name = "Transform";
			JS_NewClass(rt, g_transform_class, &transform_def);

			JSClassDef vec_def{};
			vec_def.class_name = "Vec3Ref";
			vec_def.finalizer = Vec3RefFinalizer;
			JS_NewClass(rt, g_vec3_class, &vec_def);

			// --- Vec3Ref ---
			JSValue vec_proto = JS_NewObject(ctx);
			DefGetSet(ctx, vec_proto, "x", Vec3Get, Vec3Set, 0);
			DefGetSet(ctx, vec_proto, "y", Vec3Get, Vec3Set, 1);
			DefGetSet(ctx, vec_proto, "z", Vec3Get, Vec3Set, 2);
			DefFunc(ctx, vec_proto, "set", Vec3SetAll, 3);
			DefFunc(ctx, vec_proto, "clone", Vec3Clone, 0);
			DefFunc(ctx, vec_proto, "toJSON", Vec3Clone, 0);
			DefFunc(ctx, vec_proto, "toString", Vec3ToString, 0);
			JS_SetClassProto(ctx, g_vec3_class, vec_proto);

			// --- Transform ---
			JSValue tr_proto = JS_NewObject(ctx);
			DefGetSet(ctx, tr_proto, "position", TransformGet, TransformSet, kTrLocation);
			DefGetSet(ctx, tr_proto, "location", TransformGet, TransformSet, kTrLocation);
			DefGetSet(ctx, tr_proto, "rotation", TransformGet, TransformSet, kTrRotation);
			DefGetSet(ctx, tr_proto, "scale", TransformGet, TransformSet, kTrScale);
			DefFunc(ctx, tr_proto, "clone", TransformClone, 0);
			DefFunc(ctx, tr_proto, "toJSON", TransformClone, 0);
			JS_SetClassProto(ctx, g_transform_class, tr_proto);

			// --- Actor ---
			JSValue actor_proto = JS_NewObject(ctx);
			DefGetSet(ctx, actor_proto, "id", ActorGet, ActorSet, kId);
			DefGetSet(ctx, actor_proto, "className", ActorGet, nullptr, kClassName);
			DefGetSet(ctx, actor_proto, "valid", ActorGet, nullptr, kValid);
			DefGetSet(ctx, actor_proto, "entity", ActorGet, nullptr, kEntity);
			DefGetSet(ctx, actor_proto, "transform", ActorGet, ActorSet, kTransform);
			DefGetSet(ctx, actor_proto, "position", ActorGet, ActorSet, kPosition);
			DefGetSet(ctx, actor_proto, "velocity", ActorGet, ActorSet, kVelocity);
			DefGetSet(ctx, actor_proto, "angularVelocity", ActorGet, ActorSet, kAngularVelocity);
			DefGetSet(ctx, actor_proto, "damping", ActorGet, ActorSet, kDamping);
			DefGetSet(ctx, actor_proto, "lifetime", ActorGet, ActorSet, kLifetime);
			DefGetSet(ctx, actor_proto, "tags", ActorGet, nullptr, kTags);
			DefGetSet(ctx, actor_proto, "scripts", ActorGet, nullptr, kScripts);
			DefFunc(ctx, actor_proto, "getProperty", ActorGetProperty, 1);
			DefFunc(ctx, actor_proto, "setProperty", ActorSetProperty, 2);
			DefFunc(ctx, actor_proto, "hasProperty", ActorHasProperty, 1);
			DefFunc(ctx, actor_proto, "destroy", ActorDestroy, 0);
			DefFuncMagic(ctx, actor_proto, "addTag", ActorTagOp, 1, kAddTag);
			DefFuncMagic(ctx, actor_proto, "removeTag", ActorTagOp, 1, kRemoveTag);
			DefFuncMagic(ctx, actor_proto, "hasTag", ActorTagOp, 1, kHasTag);
			DefFuncMagic(ctx, actor_proto, "addScript", ActorScriptOp, 1, kAddScript);
			DefFuncMagic(ctx, actor_proto, "removeScript", ActorScriptOp, 1, kRemoveScript);
			DefFuncMagic(ctx, actor_proto, "hasScript", ActorScriptOp, 1, kHasScript);
			DefFunc(ctx, actor_proto, "call", ActorCall, 1);
			DefFunc(ctx, actor_proto, "toString", ActorToString, 0);

			// addComponent / getComponent... et les classes des composants
			script_detail::RegisterComponentBindings(ctx, actor_proto);

			JS_SetClassProto(ctx, g_actor_class, actor_proto);
		}

		void RegisterGlobals(JSContext* ctx)
		{
			JSValue global = JS_GetGlobalObject(ctx);

			DefFuncMagic(ctx, global, "print", JsPrint, 1, 0);

			JSValue console = JS_NewObject(ctx);
			DefFuncMagic(ctx, console, "log", JsPrint, 1, 0);
			DefFuncMagic(ctx, console, "info", JsPrint, 1, 0);
			DefFuncMagic(ctx, console, "warn", JsPrint, 1, 1);
			DefFuncMagic(ctx, console, "error", JsPrint, 1, 2);
			JS_SetPropertyStr(ctx, global, "console", console);

			DefFunc(ctx, global, "vec3", JsVec3, 3);

			JSValue level = JS_NewObject(ctx);
			DefFunc(ctx, level, "spawn", LevelSpawn, 2);
			DefFunc(ctx, level, "find", LevelFind, 1);
			DefFunc(ctx, level, "findWithTag", LevelFindWithTag, 1);
			DefFunc(ctx, level, "all", LevelAll, 0);
			DefFunc(ctx, level, "count", LevelCount, 1);
			DefFunc(ctx, level, "destroy", LevelDestroy, 1);
			JS_SetPropertyStr(ctx, global, "Level", level);

			JSValue input = JS_NewObject(ctx);
			DefFuncMagic(ctx, input, "pressed", InputActionOp, 1, kPressed);
			DefFuncMagic(ctx, input, "held", InputActionOp, 1, kHeld);
			DefFuncMagic(ctx, input, "released", InputActionOp, 1, kReleased);
			DefFunc(ctx, input, "axis", InputAxis, 1);
			JS_SetPropertyStr(ctx, global, "Input", input);

			JSValue engine = JS_NewObject(ctx);
			DefFunc(ctx, engine, "getTimeDilation", EngineGetTimeDilation, 0);
			DefFunc(ctx, engine, "setTimeDilation", EngineSetTimeDilation, 2);
			DefFunc(ctx, engine, "isPlaying", EngineIsPlaying, 0);
			JS_SetPropertyStr(ctx, global, "Engine", engine);

			JS_FreeValue(ctx, global);
		}


		// ====================================================================
		// Classes : class Player extends Actor { BeginPlay() { } }
		//
		//  - Actor, et chaque classe C++ de la factory (Pawn...), est un
		//    constructeur JS. new Player() cree l'acteur C++ de la classe C++
		//    la plus proche, avec Player.prototype comme prototype.
		//  - Les .js de assets/ (tous les dossiers) qui declarent une classe sont charges avant chaque niveau ;
		//    leurs classes rejoignent la factory (niveaux, Level.spawn, editeur).
		//  - BeginPlay / Update(dt) / EndPlay de la classe sont appeles par un
		//    composant interne (ScriptClassComponent).
		// ====================================================================

		bool SameObject(JSValueConst a, JSValueConst b)
		{
			return JS_IsObject(a) && JS_IsObject(b) && JS_VALUE_GET_PTR(a) == JS_VALUE_GET_PTR(b);
		}

		/** Appelle obj.name(...) sur l'objet de l'acteur. false si pas de methode. */
		bool CallClassMethod(Actor* a, const char* name, int argc, JSValueConst* argv, JSValue* result = nullptr)
		{
			if (result)
				*result = JS_UNDEFINED;

			if (!g || !a)
				return false;

			auto it = g->actor_objects.find(a->GetEntity());
			if (it == g->actor_objects.end())
				return false;

			JSContext* ctx = g->ctx;
			JSValue obj = JS_DupValue(ctx, it->second);
			JSValue fn = JS_GetPropertyStr(ctx, obj, name);

			if (!JS_IsFunction(ctx, fn))
			{
				JS_FreeValue(ctx, fn);
				JS_FreeValue(ctx, obj);
				return false;
			}

			const std::string where = a->GetTypeName() + "." + name;

			++g->call_depth;
			JSValue r = JS_Call(ctx, fn, obj, argc, argv);
			--g->call_depth;

			JS_FreeValue(ctx, fn);
			JS_FreeValue(ctx, obj);

			if (JS_IsException(r))
			{
				LogException(ctx, where);
				r = JS_UNDEFINED;
			}

			if (result)
				*result = r;
			else
				JS_FreeValue(ctx, r);

			return true;
		}

		/** Appelle BeginPlay / Update / EndPlay de la classe JS de l'acteur. */
		struct ScriptClassComponent : Component
		{
		protected:
			void BeginPlay() override
			{
				CallClassMethod(GetOwner(), "BeginPlay", 0, nullptr);
				RunPendingJobs();
			}

			void Tick(float dt) override
			{
				if (!g)
					return;

				JSValue arg = JS_NewFloat64(g->ctx, dt);
				CallClassMethod(GetOwner(), "Update", 1, &arg);
				RunPendingJobs();
			}

			void EndPlay() override
			{
				CallClassMethod(GetOwner(), "EndPlay", 0, nullptr);
				RunPendingJobs();
			}
		};

		// ---- Valeurs JS <-> PropVariantType ---------------------------------

		/** type : "", "int", "float", "bool", "string", "vec2", "vec3", "vec4". */
		std::optional<PropVariantType> JSToVariant(JSContext* ctx, JSValueConst v, const std::string& type)
		{
			if (type == "int")
			{
				int32_t i = 0;
				if (JS_ToInt32(ctx, &i, v)) { JS_FreeValue(ctx, JS_GetException(ctx)); return std::nullopt; }
				return PropVariantType(static_cast<int>(i));
			}

			if (type == "float" || (type.empty() && JS_IsNumber(v)))
			{
				double d = 0;
				if (JS_ToFloat64(ctx, &d, v)) { JS_FreeValue(ctx, JS_GetException(ctx)); return std::nullopt; }
				return PropVariantType(static_cast<float>(d));
			}

			if (type == "bool" || (type.empty() && JS_IsBool(v)))
				return PropVariantType(JS_ToBool(ctx, v) > 0);

			if (type == "string" || (type.empty() && JS_IsString(v)))
				return PropVariantType(ToStdString(ctx, v));

			if (!JS_IsObject(v))
				return std::nullopt;

			auto has = [&](const char* key)
			{
				JSValue f = JS_GetPropertyStr(ctx, v, key);
				const bool ok = !JS_IsUndefined(f);
				JS_FreeValue(ctx, f);
				return ok;
			};

			float f[4] = {};

			if (type == "vec4" || (type.empty() && has("w")))
			{
				if (ReadFloats(ctx, v, f, 4)) return PropVariantType(vec4(f[0], f[1], f[2], f[3]));
				return std::nullopt;
			}

			if (type == "vec3" || (type.empty() && has("z")))
			{
				if (ReadFloats(ctx, v, f, 3)) return PropVariantType(vec3(f[0], f[1], f[2]));
				return std::nullopt;
			}

			if (type == "vec2" || (type.empty() && has("y")))
			{
				if (ReadFloats(ctx, v, f, 2)) return PropVariantType(vec2(f[0], f[1]));
				return std::nullopt;
			}

			if (type.empty() && (has("location") || has("position") || has("rotation") || has("scale")))
			{
				transform t;
				if (ApplyTransformObject(ctx, v, t, false)) return PropVariantType(t);
			}

			return std::nullopt;
		}

		JSValue VariantToJS(JSContext* ctx, PropVariantType& value)
		{
			return std::visit([ctx](auto& x) { return PropertyToJS(ctx, PropVariantTypePtr(&x)); }, value);
		}

		// ---- Membres reflechis : obj.collider_width_, obj.Jump() ------------

		JSValue ReflectedGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int, JSValue* data)
		{
			LYNX_THIS_ACTOR(a);
			const property* p = FindProperty(a, ToStdString(ctx, data[0]));
			return p ? PropertyToJS(ctx, p->property_member) : JS_UNDEFINED;
		}

		JSValue ReflectedSet(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int, JSValue* data)
		{
			LYNX_THIS_ACTOR(a);
			const std::string name = ToStdString(ctx, data[0]);
			const property* p = FindProperty(a, name);

			if (p && !JSToProperty(ctx, argv[0], p->property_member))
				return JS_ThrowTypeError(ctx, "wrong value type for property '%s'", name.c_str());

			return JS_UNDEFINED;
		}

		JSValue ReflectedCall(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int, JSValue* data)
		{
			LYNX_THIS_ACTOR(a);
			const std::string name = ToStdString(ctx, data[0]);

			std::vector<PropVariantType> args;
			args.reserve(static_cast<size_t>(argc));

			for (int i = 0; i < argc; ++i)
			{
				auto v = JSToVariant(ctx, argv[i], "");

				if (!v)
					return JS_ThrowTypeError(ctx, "%s : unsupported argument %d", name.c_str(), i + 1);

				args.push_back(std::move(*v));
			}

			std::optional<PropVariantType> result;

			if (!a->CallFunctionByName(name, args, &result))
				return JS_ThrowReferenceError(ctx, "no C++ function '%s'", name.c_str());

			return result ? VariantToJS(ctx, *result) : JS_UNDEFINED;
		}

		void DefineReflectedMembers(JSContext* ctx, JSValueConst obj, Actor* a)
		{
			if (!a)
				return;

			auto define = [&](const std::string& name, bool is_function, int arity)
			{
				JSAtom atom = JS_NewAtom(ctx, name.c_str());

				// Deja present (API Actor, methode de la classe JS...) : la
				// version JS gagne, le C++ reste accessible par getProperty /
				// callNative.
				if (JS_HasProperty(ctx, obj, atom) > 0)
				{
					JS_FreeAtom(ctx, atom);
					return;
				}

				JSValue data = JS_NewString(ctx, name.c_str());

				if (is_function)
				{
					JSValue fn = JS_NewCFunctionData(ctx, ReflectedCall, arity, 0, 1, &data);
					JS_DefinePropertyValue(ctx, obj, atom, fn, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
				}
				else
				{
					JSValue get = JS_NewCFunctionData(ctx, ReflectedGet, 0, 0, 1, &data);
					JSValue set = JS_NewCFunctionData(ctx, ReflectedSet, 1, 0, 1, &data);
					JS_DefinePropertyGetSet(ctx, obj, atom, get, set, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
				}

				JS_FreeValue(ctx, data);
				JS_FreeAtom(ctx, atom);
			};

			for (const auto& [name, prop] : a->GetProperties())
				define(name, false, 0);

			for (const auto& [name, fn] : a->GetFunctions())
				define(name, true, fn.arity);
		}

		// actor.callNative("Jump", ...) : fonction HFUNCTION meme si masquee par le JS
		JSValue ActorCallNative(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			if (argc < 1)
				return JS_ThrowTypeError(ctx, "callNative(name, ...args)");

			JSValue data = JS_DupValue(ctx, argv[0]);
			JSValue r = ReflectedCall(ctx, this_val, argc - 1, argv + 1, 0, &data);
			JS_FreeValue(ctx, data);
			return r;
		}

		// ---- static properties = { hp: 100, speed: {value: 3, type: "int"} } ----

		void ApplyStaticProperties(JSContext* ctx, JSValueConst new_target, Actor* a)
		{
			// [parent.properties, ..., new_target.properties] (helper JS)
			JSValueConst arg = new_target;
			JSValue lists = JS_Call(ctx, g->fn_static_properties, JS_UNDEFINED, 1, &arg);

			if (JS_IsException(lists))
			{
				LogException(ctx, "static properties");
				return;
			}

			JSValue length_v = JS_GetPropertyStr(ctx, lists, "length");
			int32_t count = 0;
			JS_ToInt32(ctx, &count, length_v);
			JS_FreeValue(ctx, length_v);

			for (int32_t i = 0; i < count; ++i)
			{
				// entree : [nom, valeur]
				JSValue entry = JS_GetPropertyUint32(ctx, lists, static_cast<uint32_t>(i));
				JSValue name_v = JS_GetPropertyUint32(ctx, entry, 0);
				JSValue value_v = JS_GetPropertyUint32(ctx, entry, 1);

				const std::string name = ToStdString(ctx, name_v);

				// {value, type} ou valeur directe
				std::string type;
				JSValue value = JS_DupValue(ctx, value_v);

				if (JS_IsObject(value_v))
				{
					JSValue inner = JS_GetPropertyStr(ctx, value_v, "value");

					if (!JS_IsUndefined(inner))
					{
						JSValue type_v = JS_GetPropertyStr(ctx, value_v, "type");
						if (JS_IsString(type_v))
							type = ToStdString(ctx, type_v);
						JS_FreeValue(ctx, type_v);

						JS_FreeValue(ctx, value);
						value = inner;
					}
					else
					{
						JS_FreeValue(ctx, inner);
					}
				}

				if (auto variant = JSToVariant(ctx, value, type))
				{
					if (const property* existing = FindProperty(a, name))
					{
						// redefini par une classe fille : nouvelle valeur par defaut
						JSToProperty(ctx, value, existing->property_member);
					}
					else
					{
						a->AddDynamicProperty(name, *variant);
					}
				}
				else
				{
					std::cout << kLogPrefix << a->GetTypeName() << " : unsupported value for static property '"
					          << name << "'" << std::endl;
				}

				JS_FreeValue(ctx, value);
				JS_FreeValue(ctx, value_v);
				JS_FreeValue(ctx, name_v);
				JS_FreeValue(ctx, entry);
			}

			JS_FreeValue(ctx, lists);
		}

		// ---- Constructeurs ---------------------------------------------------

		Actor* CreateNativeActor(const std::string& native)
		{
			if (native == "Actor")
				return new Actor();

			Engine* engine = Engine::Get();
			if (!engine)
				return nullptr;

			auto ctor = engine->GetFactory().GetObjectConstr(native.c_str());
			if (!ctor.has_value() || !ctor.value())
				return nullptr;

			Object* obj = ctor.value()();
			Actor* actor = dynamic_cast<Actor*>(obj);

			if (!actor)
				delete obj;

			return actor;
		}

		std::string ScriptClassNameOf(JSContext* ctx, JSValueConst ctor)
		{
			for (const auto& [name, cls] : g->classes)
			{
				if (SameObject(cls.ctor, ctor))
					return name;
			}

			JSValue n = JS_GetPropertyStr(ctx, ctor, "name");
			std::string name = ToStdString(ctx, n);
			JS_FreeValue(ctx, n);
			return name;
		}

		// new Actor() / new Pawn() / super() d'une classe JS. magic = classe C++.
		JSValue ActorConstruct(JSContext* ctx, JSValueConst new_target, int, JSValueConst*, int magic)
		{
			if (!JS_IsObject(new_target))
				return JS_ThrowTypeError(ctx, "an actor class must be called with 'new'");

			const std::string native = g->native_names[magic];

			// Construction demandee par la factory pour cette classe ?
			const bool from_factory = g->factory_pending && SameObject(new_target, g->factory_target);
			if (from_factory)
				g->factory_pending = false;

			Actor* a = CreateNativeActor(native);
			if (!a)
				return JS_ThrowInternalError(ctx, "could not create the C++ actor '%s'", native.c_str());

			JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
			JSValue obj = JS_NewObjectProtoClass(ctx, proto, g_actor_class);
			JS_FreeValue(ctx, proto);

			if (JS_IsException(obj))
				return obj;

			const uint32_t entity = a->GetEntity();
			JS_SetOpaque(obj, EncodeEntity(entity));

			if (auto it = g->actor_objects.find(entity); it != g->actor_objects.end())
			{
				JS_FreeValue(ctx, it->second);
				g->actor_objects.erase(it);
			}

			g->actor_objects.emplace(entity, JS_DupValue(ctx, obj));

			// Classe JS (pas new Pawn() directement)
			auto native_it = g->native_ctors.find(native);
			const bool scripted = native_it == g->native_ctors.end() || !SameObject(new_target, native_it->second);

			if (scripted)
			{
				a->script_class_ = ScriptClassNameOf(ctx, new_target);
				ApplyStaticProperties(ctx, new_target, a);
				a->AddComponent<ScriptClassComponent>();
			}

			DefineReflectedMembers(ctx, obj, a);

			if (from_factory)
			{
				// la factory (niveau, Level.spawn, editeur) l'ajoute elle-meme
				g->factory_result = a;
			}
			else if (Level* level = CurrentLevel())
			{
				level->AddSpawnedActor(a);
			}
			else
			{
				std::cout << kLogPrefix << "new " << a->GetTypeName() << "() without level : actor not added" << std::endl;
			}

			return obj;
		}

		/** Factory : cree un acteur de la classe JS `name` (niveau, Level.spawn, editeur). */
		Object* CreateScriptActor(const std::string& name)
		{
			EnsureInit();

			if (!g->classes_loaded)
				scripting::PrepareClasses();

			auto it = g->classes.find(name);
			if (it == g->classes.end())
			{
				std::cout << kLogPrefix << "script class not found: " << name << std::endl;
				return nullptr;
			}

			JSContext* ctx = g->ctx;
			JSValue ctor = JS_DupValue(ctx, it->second.ctor);

			g->factory_pending = true;
			g->factory_target = ctor;
			g->factory_result = nullptr;

			++g->call_depth;
			JSValue obj = JS_CallConstructor(ctx, ctor, 0, nullptr);
			--g->call_depth;

			g->factory_pending = false;
			g->factory_target = JS_UNDEFINED;

			Actor* a = g->factory_result;
			g->factory_result = nullptr;

			if (JS_IsException(obj))
				LogException(ctx, name + " constructor");

			JS_FreeValue(ctx, obj);
			JS_FreeValue(ctx, ctor);
			RunPendingJobs();
			return a;
		}

		/** Constructeur JS d'une classe C++ (magic = index dans native_names). */
		JSValue NewNativeConstructor(JSContext* ctx, const std::string& name, JSValueConst proto)
		{
			const int magic = static_cast<int>(g->native_names.size());
			g->native_names.push_back(name);

			JSValue ctor = JS_NewCFunctionMagic(ctx, ActorConstruct, name.c_str(), 0,
			                                    JS_CFUNC_constructor_or_func_magic, magic);
			JS_SetConstructor(ctx, ctor, proto);

			g->native_ctors[name] = JS_DupValue(ctx, ctor);
			return ctor;
		}

		void RegisterActorConstructor(JSContext* ctx)
		{
			g->actor_proto = JS_GetClassProto(ctx, g_actor_class);

			JSValue ctor = NewNativeConstructor(ctx, "Actor", g->actor_proto);
			g->actor_ctor = JS_DupValue(ctx, ctor);

			JSValue global = JS_GetGlobalObject(ctx);
			JS_SetPropertyStr(ctx, global, "Actor", ctor);
			JS_FreeValue(ctx, global);

			DefFunc(ctx, g->actor_proto, "callNative", ActorCallNative, 1);

			// Utilitaires ecrits en JS
			const char* is_actor_class =
				"(function (c) { return typeof c === 'function' && c.prototype instanceof Actor; })";

			// static properties de toute la chaine : [[nom, valeur], ...], parents d'abord
			const char* static_properties =
				"(function (c) {"
				"  const chain = [];"
				"  for (let p = c; p && p !== Actor && p !== Function.prototype; p = Object.getPrototypeOf(p))"
				"    if (Object.prototype.hasOwnProperty.call(p, 'properties') && p.properties)"
				"      chain.unshift(p.properties);"
				"  const out = [];"
				"  for (const props of chain) for (const k of Object.keys(props)) out.push([k, props[k]]);"
				"  return out;"
				"})";

			g->fn_is_actor_class = JS_Eval(ctx, is_actor_class, std::strlen(is_actor_class), "<lynx>", JS_EVAL_TYPE_GLOBAL);
			g->fn_static_properties = JS_Eval(ctx, static_properties, std::strlen(static_properties), "<lynx>", JS_EVAL_TYPE_GLOBAL);
		}

		/** Constructeurs JS des classes C++ de la factory (Pawn, Mushroom...). */
		void EnsureNativeClassGlobals()
		{
			Engine* engine = Engine::Get();
			if (!engine || !engine->GetFactory().GetInternalFactory())
				return;

			JSContext* ctx = g->ctx;
			JSValue global = JS_GetGlobalObject(ctx);

			for (const auto& [name, constructor] : *engine->GetFactory().GetInternalFactory())
			{
				if (g->classes.count(name) || g->native_ctors.count(name) || !IsIdentifier(name.c_str()))
					continue;

				JSValue proto = JS_NewObjectProto(ctx, g->actor_proto);
				JSValue ctor = NewNativeConstructor(ctx, name, proto);
				JS_FreeValue(ctx, proto);

				// static : Pawn.__proto__ = Actor
				JS_SetPrototype(ctx, ctor, g->actor_ctor);

				JSAtom atom = JS_NewAtom(ctx, name.c_str());

				if (JS_HasProperty(ctx, global, atom) > 0)
				{
					std::cout << kLogPrefix << "C++ class " << name
					          << " : a global with this name already exists, not exposed" << std::endl;
					JS_FreeValue(ctx, ctor);
				}
				else
				{
					JS_SetProperty(ctx, global, atom, ctor);
				}

				JS_FreeAtom(ctx, atom);
			}

			JS_FreeValue(ctx, global);
		}

		bool IsActorClass(JSContext* ctx, JSValueConst value)
		{
			JSValue r = JS_Call(ctx, g->fn_is_actor_class, JS_UNDEFINED, 1, &value);
			const bool ok = JS_ToBool(ctx, r) > 0;
			JS_FreeValue(ctx, r);
			return ok;
		}

		void RegisterScriptClass(JSContext* ctx, const std::string& name, JSValueConst ctor, const std::string& file)
		{
			Engine* engine = Engine::Get();

			// Une classe C++ porte deja ce nom : la factory ne peut en avoir qu'une.
			const bool cpp_exists =
				g->native_ctors.count(name) ||
				(engine && engine->GetFactory().GetObjectConstr(name.c_str()).has_value() && !g->classes.count(name));

			if (cpp_exists)
			{
				std::cout << kLogPrefix << file << " : a C++ class is already called " << name
				          << ", rename the JavaScript class" << std::endl;
				return;
			}

			const bool first_time = !g->classes.count(name);
			auto& cls = g->classes[name];

			JS_FreeValue(ctx, cls.ctor);
			cls.ctor = JS_DupValue(ctx, ctor);
			cls.file = file;

			// Global : les autres fichiers peuvent en heriter.
			JSValue global = JS_GetGlobalObject(ctx);
			JS_SetPropertyStr(ctx, global, name.c_str(), JS_DupValue(ctx, ctor));
			JS_FreeValue(ctx, global);

			if (first_time && engine)
			{
				engine->GetFactory().RegisterObject(name.c_str(), [name]() -> Object*
				{
					return CreateScriptActor(name);
				});
			}

			// Rechargement : les acteurs existants prennent les nouvelles methodes.
			JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");

			for (auto& [entity, obj] : g->actor_objects)
			{
				Actor* a = ecs::GetActor(entity);

				if (a && a->script_class_ == name)
					JS_SetPrototype(ctx, obj, proto);
			}

			JS_FreeValue(ctx, proto);
			std::cout << kLogPrefix << "class " << name << " (" << file << ")" << std::endl;
		}

		/** Noms declares par "class Nom extends ..." dans le fichier. */
		std::vector<std::string> DeclaredClasses(const std::string& code)
		{
			std::vector<std::string> names;
			size_t pos = 0;

			while ((pos = code.find("class", pos)) != std::string::npos)
			{
				const bool start_ok = pos == 0 || !(std::isalnum(static_cast<unsigned char>(code[pos - 1])) || code[pos - 1] == '_' || code[pos - 1] == '$');
				pos += 5;

				if (!start_ok || pos >= code.size() || !std::isspace(static_cast<unsigned char>(code[pos])))
					continue;

				size_t i = pos;
				while (i < code.size() && std::isspace(static_cast<unsigned char>(code[i]))) ++i;

				const size_t name_start = i;
				while (i < code.size() && (std::isalnum(static_cast<unsigned char>(code[i])) || code[i] == '_' || code[i] == '$')) ++i;

				if (i == name_start)
					continue;

				std::string name = code.substr(name_start, i - name_start);

				while (i < code.size() && std::isspace(static_cast<unsigned char>(code[i]))) ++i;

				if (code.compare(i, 7, "extends") == 0 && IsIdentifier(name.c_str()))
					names.push_back(std::move(name));
			}

			return names;
		}

		void LoadClassFiles()
		{
			JSContext* ctx = g->ctx;

			// Rechargement : les anciennes classes ne sont plus visibles, sinon
			// "class Boss extends Enemy" (Boss.js lu avant Enemy.js) heriterait
			// de l'ancienne version d'Enemy.
			{
				JSValue global = JS_GetGlobalObject(ctx);

				for (const auto& [name, cls] : g->classes)
				{
					JSAtom atom = JS_NewAtom(ctx, name.c_str());
					JS_DeleteProperty(ctx, global, atom, 0);
					JS_FreeAtom(ctx, atom);
				}

				JS_FreeValue(ctx, global);
			}

			struct Pending
			{
				std::string path;
				std::string code;
				std::vector<std::string> names;
				std::string error;
			};

			std::vector<Pending> pending;

			// Every .js of assets/ (any folder, not only classes/). Only the
			// files that declare "class X extends ..." are evaluated here : the
			// attached scripts (BeginPlay / Update + parent) are left alone.
			for (const std::string& path : fs::ListFiles("", true))
			{
				if (path.size() <= 3 || path.compare(path.size() - 3, 3, ".js") != 0)
					continue;

				const auto data = fs::ReadBinary(path);
				size_t skip = 0;

				if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
					skip = 3;

				Pending file;
				file.path = path;
				file.code.assign(reinterpret_cast<const char*>(data.data()) + skip, data.size() - skip);
				file.names = DeclaredClasses(file.code);

				if (!file.names.empty())
					pending.push_back(std::move(file));
			}

			// Plusieurs passes : un fichier qui herite d'une classe d'un autre
			// fichier pas encore charge (ReferenceError) est reessaye.
			bool progress = true;

			while (!pending.empty() && progress)
			{
				progress = false;
				std::vector<Pending> retry;

				for (Pending& file : pending)
				{
					const std::string& code = file.code;
					const std::vector<std::string>& names = file.names;

					std::string wrapped = "(function () {\n" + code + "\n;return {";

					for (const std::string& n : names)
						wrapped += n + ": (typeof " + n + " !== 'undefined' ? " + n + " : undefined),";

					wrapped += "};\n})()";

					JSValue result = JS_Eval(ctx, wrapped.c_str(), wrapped.size(), file.path.c_str(), JS_EVAL_TYPE_GLOBAL);

					if (JS_IsException(result))
					{
						JSValue ex = JS_GetException(ctx);
						JSValue ex_name = JS_GetPropertyStr(ctx, ex, "name");
						const bool missing = ToStdString(ctx, ex_name) == "ReferenceError";
						file.error = ToStdString(ctx, ex);
						JS_FreeValue(ctx, ex_name);

						if (missing)
						{
							retry.push_back(file);
							JS_FreeValue(ctx, ex);
							continue;
						}

						std::cout << kLogPrefix << file.path << ": " << file.error << std::endl;
						JS_FreeValue(ctx, ex);
						progress = true;
						continue;
					}

					progress = true;

					for (const std::string& n : names)
					{
						JSValue cls = JS_GetPropertyStr(ctx, result, n.c_str());

						if (IsActorClass(ctx, cls))
							RegisterScriptClass(ctx, n, cls, file.path);

						JS_FreeValue(ctx, cls);
					}

					JS_FreeValue(ctx, result);
				}

				pending = std::move(retry);
			}

			for (const Pending& file : pending)
				std::cout << kLogPrefix << file.path << ": " << file.error << std::endl;

			RunPendingJobs();
		}

		void FreeClassValues()
		{
			JSContext* ctx = g->ctx;

			for (auto& [name, cls] : g->classes)
				JS_FreeValue(ctx, cls.ctor);

			for (auto& [name, ctor] : g->native_ctors)
				JS_FreeValue(ctx, ctor);

			g->classes.clear();
			g->native_ctors.clear();
			g->native_names.clear();

			JS_FreeValue(ctx, g->actor_ctor);
			JS_FreeValue(ctx, g->actor_proto);
			JS_FreeValue(ctx, g->fn_is_actor_class);
			JS_FreeValue(ctx, g->fn_static_properties);

			g->actor_ctor = g->actor_proto = JS_UNDEFINED;
			g->fn_is_actor_class = g->fn_static_properties = JS_UNDEFINED;
		}

		void EnsureInit()
		{
			if (g)
				return;

			g = new State();
			g->rt = JS_NewRuntime();
			g->ctx = JS_NewContext(g->rt);

			RegisterClasses(g->ctx);
			RegisterGlobals(g->ctx);
			RegisterActorConstructor(g->ctx);
		}


		// ====================================================================
		// Compilation / instances
		// ====================================================================

		/** Retourne la fabrique du script (reference empruntee), ou false. */
		bool GetFactory(const std::string& path, JSValue& out)
		{
			auto it = g->factories.find(path);
			if (it != g->factories.end())
			{
				out = it->second;
				return true;
			}

			if (!fs::Exists(path))
			{
				std::cout << kLogPrefix << "script not found: " << path << std::endl;
				return false;
			}

			const auto data = fs::ReadBinary(path);
			size_t skip = 0;
			if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
				skip = 3; // BOM UTF-8

			std::string code = kWrapperBegin;
			code.append(reinterpret_cast<const char*>(data.data()) + skip, data.size() - skip);
			code += kWrapperEnd;

			JSValue fn = JS_Eval(g->ctx, code.c_str(), code.size(), path.c_str(), JS_EVAL_TYPE_GLOBAL);
			if (JS_IsException(fn))
			{
				LogException(g->ctx, path);
				return false;
			}
			if (!JS_IsFunction(g->ctx, fn))
			{
				JS_FreeValue(g->ctx, fn);
				return false;
			}

			g->factories.emplace(path, fn);
			out = fn;
			return true;
		}

		void FreeInstanceValues(ScriptInstance& inst)
		{
			if (g)
			{
				for (auto& [name, fn] : inst.functions)
					JS_FreeValue(g->ctx, fn);
				JS_FreeValue(g->ctx, inst.lookup);
			}
			inst.functions.clear();
			inst.lookup = JS_UNDEFINED;
			inst.loaded = false;
		}

		bool Instantiate(ScriptInstance& inst, uint32_t entity)
		{
			EnsureInit();
			FreeInstanceValues(inst);
			inst.begun = false;

			JSValue factory;
			if (!GetFactory(inst.path, factory))
				return false;

			JSValue fn = JS_DupValue(g->ctx, factory);
			JSValue parent = GetActorObject(g->ctx, entity);

			++g->call_depth;
			JSValue lookup = JS_Call(g->ctx, fn, JS_UNDEFINED, 1, &parent);
			--g->call_depth;

			JS_FreeValue(g->ctx, parent);
			JS_FreeValue(g->ctx, fn);

			if (JS_IsException(lookup))
			{
				LogException(g->ctx, inst.path);
				return false;
			}

			inst.lookup = lookup;
			inst.loaded = true;
			return true;
		}

		/** Fonction du script par nom (reference empruntee, peut etre undefined). */
		JSValue FindFunction(ScriptInstance& inst, const char* name)
		{
			if (!inst.loaded)
				return JS_UNDEFINED;

			auto it = inst.functions.find(name);
			if (it != inst.functions.end())
				return it->second;

			JSValue result = JS_UNDEFINED;
			if (IsIdentifier(name))
			{
				JSValue arg = JS_NewString(g->ctx, name);
				result = JS_Call(g->ctx, inst.lookup, JS_UNDEFINED, 1, &arg);
				JS_FreeValue(g->ctx, arg);
				if (JS_IsException(result))
				{
					JS_FreeValue(g->ctx, JS_GetException(g->ctx));
					result = JS_UNDEFINED;
				}
			}
			inst.functions.emplace(name, result);
			return result;
		}

		/**
		 * Appelle `name` dans une instance (this = parent).
		 * @param result si non null, recoit la valeur de retour (a liberer).
		 * @return true si la fonction existait.
		 */
		bool CallInstance(ScriptInstance& inst, uint32_t entity, const char* name,
		                  int argc, JSValueConst* argv, JSValue* result = nullptr)
		{
			if (result)
				*result = JS_UNDEFINED;
			if (inst.dead || !inst.loaded)
				return false;

			JSValue found = FindFunction(inst, name);
			if (!JS_IsFunction(g->ctx, found))
				return false;

			// dup : l'instance peut etre retiree pendant l'appel
			JSValue fn = JS_DupValue(g->ctx, found);
			JSValue self = GetActorObject(g->ctx, entity);
			const std::string path = inst.path;

			++g->call_depth;
			JSValue r = JS_Call(g->ctx, fn, self, argc, argv);
			--g->call_depth;

			JS_FreeValue(g->ctx, self);
			JS_FreeValue(g->ctx, fn);

			if (JS_IsException(r))
			{
				LogException(g->ctx, path + " (" + name + ")");
				r = JS_UNDEFINED;
			}

			if (result)
				*result = r;
			else
				JS_FreeValue(g->ctx, r);
			return true;
		}

		using Impl = ScriptComponent::Impl;
	}


	// ========================================================================
	// ScriptComponent
	// ========================================================================

	struct ScriptComponent::Impl
	{
		uint32_t entity = ecs::kNullEntity;
		// unique_ptr : adresses stables meme si le vector grandit pendant un appel
		std::vector<std::unique_ptr<ScriptInstance>> instances;

		ScriptInstance* Find(const std::string& path)
		{
			for (auto& i : instances)
				if (!i->dead && i->path == path)
					return i.get();
			return nullptr;
		}

		// libere les instances retirees (seulement hors appel JS)
		void Compact()
		{
			if (g && g->call_depth > 0)
				return;
			std::erase_if(instances, [](const std::unique_ptr<ScriptInstance>& i)
			{
				if (!i->dead)
					return false;
				FreeInstanceValues(*i);
				return true;
			});
		}

		// recopie la liste dans la propriete Actor::scripts (sauvegarde niveau)
		void SyncOwnerProperty()
		{
			Actor* owner = ecs::GetActor(entity);
			if (!owner)
				return;

			std::string joined;
			for (auto& i : instances)
			{
				if (i->dead)
					continue;
				if (!joined.empty())
					joined += ';';
				joined += i->path;
			}
			owner->scripts = joined;
		}

		void BeginInstance(ScriptInstance& inst)
		{
			if (inst.dead || !inst.loaded || inst.begun || !g)
				return;
			inst.begun = true;
			CallInstance(inst, entity, "BeginPlay", 0, nullptr);
		}

		void EndPlayInstance(ScriptInstance& inst)
		{
			if (!inst.begun)
				return;
			inst.begun = false;
			if (g)
				CallInstance(inst, entity, "EndPlay", 0, nullptr);
		}

		// Applique fn a chaque instance. Par index : la liste peut grandir
		// pendant un appel (addScript), les nouvelles sont ignorees ce tour-ci.
		template<typename Fn>
		void ForEachInstance(Fn&& fn)
		{
			const size_t count = instances.size();
			for (size_t i = 0; i < count && i < instances.size(); ++i)
				fn(*instances[i]);
		}
	};

	namespace
	{
		Impl* ImplOf(Actor* a)
		{
			auto* sc = a ? a->GetComponent<ScriptComponent>() : nullptr;
			return sc ? ScriptAccess::Get(*sc) : nullptr;
		}

		std::vector<ScriptComponent*> AllScriptComponents()
		{
			std::vector<ScriptComponent*> out;
			ecs::CollectComponents(out);
			return out;
		}

		JSValue CallActorFunction(JSContext* ctx, Actor* a, const char* name, int argc, JSValueConst* argv, bool* found)
		{
			*found = false;
			JSValue last = JS_UNDEFINED;

			// Methode de la classe JS de l'acteur
			if (a && !a->script_class_.empty())
			{
				JSValue r;

				if (CallClassMethod(a, name, argc, argv, &r))
				{
					*found = true;
					last = r;
				}
			}

			// Fonctions des scripts attaches
			Impl* impl = ImplOf(a);
			if (!impl)
				return last;

			impl->ForEachInstance([&](ScriptInstance& inst)
			{
				JSValue r;
				if (CallInstance(inst, impl->entity, name, argc, argv, &r))
				{
					*found = true;
					JS_FreeValue(ctx, last);
					last = r;
				}
			});
			return last;
		}
	}

	ScriptComponent::ScriptComponent()
		: impl_(new Impl())
	{
	}

	ScriptComponent::~ScriptComponent()
	{
		for (auto& i : impl_->instances)
			FreeInstanceValues(*i);
		delete impl_;
	}

	void ScriptComponent::OnAttach()
	{
		Actor* owner = GetOwner();
		impl_->entity = owner ? owner->GetEntity() : ecs::kNullEntity;

		// scripts ajoutes avant l'attache : on les instancie maintenant
		impl_->ForEachInstance([&](ScriptInstance& inst)
		{
			if (!inst.dead && !inst.loaded)
				Instantiate(inst, impl_->entity);
		});
		impl_->SyncOwnerProperty();
	}

	void ScriptComponent::BeginPlay()
	{
		impl_->ForEachInstance([&](ScriptInstance& inst) { impl_->BeginInstance(inst); });
		impl_->Compact();
		if (g)
			RunPendingJobs();
	}

	void ScriptComponent::Tick(float dt)
	{
		if (!g)
			return;

		JSValue arg = JS_NewFloat64(g->ctx, dt);
		impl_->ForEachInstance([&](ScriptInstance& inst)
		{
			// script ajoute pendant le jeu (addScript, reload...)
			impl_->BeginInstance(inst);
			if (!inst.dead && inst.loaded)
				CallInstance(inst, impl_->entity, "Update", 1, &arg);
		});
		impl_->Compact();
		RunPendingJobs();
	}

	void ScriptComponent::EndPlay()
	{
		impl_->ForEachInstance([&](ScriptInstance& inst) { impl_->EndPlayInstance(inst); });
		impl_->Compact();
		if (g)
			RunPendingJobs();
	}

	bool ScriptComponent::AddScript(const std::string& path)
	{
		if (path.empty())
			return false;
		if (ScriptInstance* existing = impl_->Find(path))
			return existing->loaded;

		auto inst = std::make_unique<ScriptInstance>();
		inst->path = path;
		ScriptInstance* raw = inst.get();
		impl_->instances.push_back(std::move(inst));
		impl_->SyncOwnerProperty();

		// Pas encore attache a un acteur : instancie dans OnAttach.
		if (impl_->entity == ecs::kNullEntity)
		{
			EnsureInit();
			JSValue unused;
			return GetFactory(path, unused);
		}

		// Meme en cas d'erreur, le script reste dans la liste : il sera
		// sauvegarde avec le niveau et reessaye par Reload().
		// Si le composant a deja commence, BeginPlay sera appele au prochain Tick.
		return Instantiate(*raw, impl_->entity);
	}

	void ScriptComponent::RemoveScript(const std::string& path)
	{
		ScriptInstance* inst = impl_->Find(path);
		if (!inst)
			return;

		impl_->EndPlayInstance(*inst);
		inst->dead = true;
		impl_->Compact();
		impl_->SyncOwnerProperty();
	}

	bool ScriptComponent::HasScript(const std::string& path) const
	{
		return impl_->Find(path) != nullptr;
	}

	std::vector<std::string> ScriptComponent::GetScripts() const
	{
		std::vector<std::string> out;
		for (auto& i : impl_->instances)
			if (!i->dead)
				out.push_back(i->path);
		return out;
	}

	bool ScriptComponent::CallFunction(const char* name, const std::vector<double>& args)
	{
		if (!g || !name)
			return false;

		std::vector<JSValue> js_args;
		js_args.reserve(args.size());
		for (double d : args)
			js_args.push_back(JS_NewFloat64(g->ctx, d)); // nombres : rien a liberer

		bool found = false;
		impl_->ForEachInstance([&](ScriptInstance& inst)
		{
			found |= CallInstance(inst, impl_->entity, name,
			                      static_cast<int>(js_args.size()), js_args.data());
		});

		impl_->Compact();
		RunPendingJobs();
		return found;
	}

	void ScriptComponent::Reload()
	{
		EnsureInit();

		for (auto& i : impl_->instances)
		{
			auto it = g->factories.find(i->path);
			if (it != g->factories.end())
			{
				JS_FreeValue(g->ctx, it->second);
				g->factories.erase(it);
			}
		}

		impl_->ForEachInstance([&](ScriptInstance& inst)
		{
			if (inst.dead)
				return;
			impl_->EndPlayInstance(inst);
			Instantiate(inst, impl_->entity);
			// BeginPlay au prochain Tick si le composant a deja commence
		});
	}


	// ========================================================================
	// Interface interne (Engine / Actor)
	// ========================================================================

	namespace scripting
	{
		void Init()
		{
			EnsureInit();
		}

		void Shutdown()
		{
			if (!g)
				return;

			// Les composants peuvent survivre (acteurs hors niveau) : on libere
			// leurs valeurs JS maintenant, ils ne garderont que les chemins.
			for (ScriptComponent* sc : AllScriptComponents())
			{
				for (auto& i : ScriptAccess::Get(*sc)->instances)
				{
					FreeInstanceValues(*i);
					i->begun = false;
				}
			}

			// Fonctions JS gardees par les composants (evenements d'animation...).
			script_detail::ShutdownComponentBindings(g->ctx);

			// Classes (constructeurs, prototypes)
			FreeClassValues();
			g->classes_loaded = false;

			for (auto& [e, obj] : g->actor_objects)
				JS_FreeValue(g->ctx, obj);
			g->actor_objects.clear();

			for (auto& [path, fn] : g->factories)
				JS_FreeValue(g->ctx, fn);
			g->factories.clear();

			g->actions.clear();
			g->axes.clear();

			JS_RunGC(g->rt);
			JS_FreeContext(g->ctx);
			JS_FreeRuntime(g->rt);

			delete g;
			g = nullptr;
		}

		void PrepareClasses()
		{
			EnsureInit();

			// Classes C++ d'abord : "extends Pawn" doit les trouver.
			EnsureNativeClassGlobals();

			if (!g->classes_loaded)
			{
				g->classes_loaded = true;
				LoadClassFiles();
			}
		}

		bool CallActorEvent(Actor* self, const char* function, Actor* other)
		{
			if (!g || !self || !function)
				return false;

			JSValue arg = ActorToJS(g->ctx, other);
			bool found = false;
			JSValue r = CallActorFunction(g->ctx, self, function, 1, &arg, &found);
			JS_FreeValue(g->ctx, r);
			JS_FreeValue(g->ctx, arg);
			RunPendingJobs();
			return found;
		}

		void OnEntityDestroyed(uint32_t entity)
		{
			if (!g)
				return;
			auto it = g->actor_objects.find(entity);
			if (it == g->actor_objects.end())
				return;
			JSValue obj = it->second;
			g->actor_objects.erase(it);
			JS_FreeValue(g->ctx, obj);
		}
	}


	// ========================================================================
	// API publique
	// ========================================================================

	void ReloadScripts()
	{
		EnsureInit();

		for (auto& [path, fn] : g->factories)
			JS_FreeValue(g->ctx, fn);
		g->factories.clear();
		g->actions.clear();
		g->axes.clear();

		for (ScriptComponent* sc : AllScriptComponents())
			sc->Reload();

		// Classes : fichiers relus, les acteurs existants prennent les
		// nouvelles methodes.
		g->classes_loaded = false;
		scripting::PrepareClasses();
	}

	bool CallScriptFunction(Actor* actor, const char* name, const std::vector<double>& args)
	{
		if (!g || !actor || !name)
			return false;

		std::vector<JSValue> js_args;
		js_args.reserve(args.size());

		for (double d : args)
			js_args.push_back(JS_NewFloat64(g->ctx, d));

		bool found = false;
		JSValue r = CallActorFunction(g->ctx, actor, name, static_cast<int>(js_args.size()), js_args.data(), &found);
		JS_FreeValue(g->ctx, r);
		RunPendingJobs();
		return found;
	}

	bool ExecuteScript(const char* code, const char* name)
	{
		if (!code)
			return false;
		EnsureInit();

		JSValue r = JS_Eval(g->ctx, code, std::strlen(code), name ? name : "<console>", JS_EVAL_TYPE_GLOBAL);
		const bool ok = !JS_IsException(r);
		if (!ok)
			LogException(g->ctx, name ? name : "<console>");
		JS_FreeValue(g->ctx, r);
		RunPendingJobs();
		return ok;
	}

	bool EvaluateScript(const char* code, std::string& result_json, std::string& error, const char* name)
	{
		result_json = "null";
		error.clear();

		if (!code)
		{
			error = "no code";
			return false;
		}

		EnsureInit();

		JSContext* ctx = g->ctx;
		JSValue r = JS_Eval(ctx, code, std::strlen(code), name ? name : "<eval>", JS_EVAL_TYPE_GLOBAL);

		if (JS_IsException(r))
		{
			JSValue ex = JS_GetException(ctx);
			error = ToStdString(ctx, ex);

			if (JS_IsObject(ex))
			{
				JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");

				if (!JS_IsUndefined(stack) && !JS_IsException(stack))
				{
					const std::string text = ToStdString(ctx, stack);

					if (!text.empty())
						error += "\n" + text;
				}

				JS_FreeValue(ctx, stack);
			}

			JS_FreeValue(ctx, ex);
			RunPendingJobs();
			return false;
		}

		if (!JS_IsUndefined(r))
		{
			// Not serializable as JSON (cycle, function...) : its text, quoted.
			const auto as_text = [&]()
			{
				const std::string text = ToStdString(ctx, r);
				std::string quoted = "\"";

				for (char c : text)
				{
					if (c == '"' || c == '\\')
						quoted += '\\';

					if (c == '\n')
						quoted += "\\n";
					else if (static_cast<unsigned char>(c) >= 0x20)
						quoted += c;
				}

				return quoted + "\"";
			};

			JSValue json = JS_JSONStringify(ctx, r, JS_UNDEFINED, JS_UNDEFINED);

			if (JS_IsException(json))
			{
				JSValue ex = JS_GetException(ctx);
				JS_FreeValue(ctx, ex);
				result_json = as_text();
			}
			else
			{
				result_json = JS_IsUndefined(json) ? as_text() : ToStdString(ctx, json);
				JS_FreeValue(ctx, json);
			}
		}

		JS_FreeValue(ctx, r);
		RunPendingJobs();
		return true;
	}
}


// ============================================================================
// Utilitaires partages avec ComponentBindings.cpp (Private/ScriptInternal.h)
// ============================================================================

namespace lynx::script_detail
{
	JSContext* Context() { return g ? g->ctx : nullptr; }

	JSValue ActorObject(JSContext* ctx, Actor* actor) { return ActorToJS(ctx, actor); }

	Actor* ActorFromJS(JSValueConst value) { return JSToActor(value); }

	std::string ToStdString(JSContext* ctx, JSValueConst value) { return lynx::ToStdString(ctx, value); }

	bool ReadFloats(JSContext* ctx, JSValueConst value, float* out, int n)
	{
		return lynx::ReadFloats(ctx, value, out, n);
	}

	JSValue NewPlainVec(JSContext* ctx, const float* values, int n)
	{
		return lynx::NewPlainVec(ctx, values, n);
	}

	void LogException(JSContext* ctx, const std::string& where) { lynx::LogException(ctx, where); }

	void RunPendingJobs()
	{
		if (g)
			lynx::RunPendingJobs();
	}

	void EnterCall()
	{
		if (g)
			++g->call_depth;
	}

	void LeaveCall()
	{
		if (g)
			--g->call_depth;
	}

	void DefGetSet(JSContext* ctx, JSValueConst obj, const char* name,
	               JSCFunctionMagic* getter, JSCFunctionMagic* setter, int magic)
	{
		lynx::DefGetSet(ctx, obj, name, getter, setter, magic);
	}

	void DefFuncMagic(JSContext* ctx, JSValueConst obj, const char* name,
	                  JSCFunctionMagic* fn, int length, int magic)
	{
		lynx::DefFuncMagic(ctx, obj, name, fn, length, magic);
	}
}

