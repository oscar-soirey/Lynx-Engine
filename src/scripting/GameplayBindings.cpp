/*
 * Joueurs (PlayerController) et widgets (UI) cote JavaScript.
 *
 *     const p2 = Engine.createPlayer();
 *     p2.possess(Level.spawn("Hero"));
 *
 *     const hud = UI.create("ui/Hud.widget", p2);
 *     hud.find("Health").percent = 0.5;
 *     hud.find("Pause").onClicked(() => print("pause"));
 *     hud.addToViewport();
 *
 *     class MainMenu extends UserWidget {         // Widget Editor > Class = MainMenu
 *         Construct() { this.find("Play").onClicked(() => this.removeFromParent()); }
 *         Tick(dt) { }
 *         Destruct() { }
 *     }
 *
 * Un objet JS ne garde que l'id du joueur / du widget : s'il est detruit,
 * l'utiliser leve une ReferenceError (`valid` vaut false) au lieu de crasher.
 */

#include "Private/ScriptInternal.h"
#include "Private/ScriptSystem.h"

#include "../core/Engine.h"
#include "../gameplay/Actor.h"
#include "../gameplay/PlayerController.h"
#include "../widgets/Widget.h"
#include "../widgets/Panels.h"
#include "../widgets/CommonWidgets.h"
#include "../widgets/UserWidget.h"
#include "../widgets/Private/WidgetSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lynx::script_detail
{
	namespace
	{
		constexpr const char* kLogPrefix = "[script] ";

		JSClassID g_player_class = 0;
		JSClassID g_widget_class = 0;
		JSClassID g_slot_class = 0;

		struct WidgetClass
		{
			JSValue ctor = JS_UNDEFINED;
			std::string file;
		};

		struct State
		{
			JSValue player_proto = JS_UNDEFINED;
			JSValue widget_proto = JS_UNDEFINED;        // tous les widgets
			JSValue user_widget_proto = JS_UNDEFINED;   // UserWidget.prototype
			JSValue user_widget_ctor = JS_UNDEFINED;
			JSValue slot_proto = JS_UNDEFINED;

			std::unordered_map<int, JSValue> players;          // id du joueur -> objet
			std::unordered_map<uint64_t, JSValue> widgets;     // id du widget -> objet
			std::unordered_map<uint64_t, std::string> bound;   // id -> classe JS liee
			std::unordered_map<std::string, WidgetClass> classes;
		};

		State* s = nullptr;

		State& S()
		{
			if (!s)
				s = new State();
			return *s;
		}

		void* Encode(uint64_t id) { return reinterpret_cast<void*>(static_cast<uintptr_t>(id)); }
		uint64_t Decode(void* p) { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)); }

		JSValue ThrowDead(JSContext* ctx, const char* what)
		{
			return JS_ThrowReferenceError(ctx, "this %s has been destroyed", what);
		}

		void DefGet(JSContext* ctx, JSValueConst obj, const char* name, JSCFunctionMagic* getter, int magic)
		{
			DefGetSet(ctx, obj, name, getter, nullptr, magic);
		}

		void DefFunc(JSContext* ctx, JSValueConst obj, const char* name, JSCFunction* fn, int length)
		{
			JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, length));
		}

		// "font_size" -> "fontSize"
		std::string CamelCase(const std::string& name)
		{
			std::string out;
			bool upper = false;
			for (char c : name)
			{
				if (c == '_')
				{
					upper = !out.empty();
					continue;
				}
				out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
				upper = false;
			}
			return out;
		}

		// "Hit Test Invisible" / "hit_test_invisible" -> "hittestinvisible"
		std::string Simplify(const std::string& text)
		{
			std::string out;
			for (char c : text)
				if (std::isalnum(static_cast<unsigned char>(c)))
					out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return out;
		}

		bool IsColorProperty(const std::string& name)
		{
			return name.find("color") != std::string::npos || name == "tint";
		}


		// =====================================================================
		// Fonctions JS gardees par les widgets (onClicked...)
		// =====================================================================

		struct WidgetRef
		{
			JSValue fn = JS_UNDEFINED;
			uint64_t widget = 0;       // this de l'appel
			bool alive = true;
		};

		std::unordered_set<WidgetRef*>& LiveRefs()
		{
			static std::unordered_set<WidgetRef*> refs;
			return refs;
		}

		using Ref = std::shared_ptr<WidgetRef>;

		Ref MakeRef(JSContext* ctx, JSValueConst fn, uint64_t widget)
		{
			auto* raw = new WidgetRef{ JS_DupValue(ctx, fn), widget, true };
			LiveRefs().insert(raw);
			return Ref(raw, [](WidgetRef* r)
			{
				if (r->alive)
					if (JSContext* c = Context())
						JS_FreeValue(c, r->fn);
				LiveRefs().erase(r);
				delete r;
			});
		}

		JSValue WidgetToJS(JSContext* ctx, Widget* widget);

		void CallRef(const Ref& ref, int argc, JSValueConst* argv, const char* where)
		{
			JSContext* ctx = Context();
			if (!ref || !ref->alive || !ctx)
				return;

			JSValue fn = JS_DupValue(ctx, ref->fn);
			JSValue self = WidgetToJS(ctx, WidgetSystem::Find(ref->widget));

			EnterCall();
			JSValue r = JS_Call(ctx, fn, self, argc, argv);
			LeaveCall();

			if (JS_IsException(r))
				LogException(ctx, where);

			JS_FreeValue(ctx, r);
			JS_FreeValue(ctx, self);
			JS_FreeValue(ctx, fn);
			RunPendingJobs();
		}


		// =====================================================================
		// Joueurs
		// =====================================================================

		PlayerController* FindPlayer(int id)
		{
			Engine* engine = Engine::Get();
			if (!engine)
				return nullptr;
			for (PlayerController* p : engine->GetPlayers())
				if (p && p->GetId() == id)
					return p;
			return nullptr;
		}

		PlayerController* ThisPlayer(JSValueConst this_val)
		{
			void* opaque = JS_GetOpaque(this_val, g_player_class);
			return opaque ? FindPlayer(static_cast<int>(Decode(opaque)) - 1) : nullptr;
		}

		#define LYNX_THIS_PLAYER(var) \
			PlayerController* var = ThisPlayer(this_val); \
			if (!var) return ThrowDead(ctx, "PlayerController")

		// Argument "player" : un PlayerController, ou null / undefined.
		bool ReadOptionalPlayer(JSContext* ctx, JSValueConst value, PlayerController*& out)
		{
			out = nullptr;
			if (JS_IsUndefined(value) || JS_IsNull(value))
				return true;
			out = PlayerFromJS(value);
			if (!out)
			{
				JS_ThrowTypeError(ctx, "a PlayerController (or null) is expected");
				return false;
			}
			return true;
		}

		JSValue NewRect(JSContext* ctx, float x, float y, float w, float h)
		{
			JSValue obj = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, x));
			JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, y));
			JS_SetPropertyStr(ctx, obj, "width", JS_NewFloat64(ctx, w));
			JS_SetPropertyStr(ctx, obj, "height", JS_NewFloat64(ctx, h));
			return obj;
		}

		enum PlayerGetter { kPIndex, kPId, kPValid, kPDefault, kPPossessed, kPRect, kPCustom };

		JSValue PlayerGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			PlayerController* p = ThisPlayer(this_val);
			if (magic == kPValid)
				return JS_NewBool(ctx, p != nullptr);
			if (!p)
				return ThrowDead(ctx, "PlayerController");

			switch (magic)
			{
			case kPIndex: return JS_NewInt32(ctx, p->GetPlayerIndex());
			case kPId: return JS_NewInt32(ctx, p->GetId());
			case kPDefault: return JS_NewBool(ctx, p->IsDefaultPlayer());
			case kPPossessed: return ActorObject(ctx, p->GetPossessedActor());
			case kPRect:
			{
				const vec4 r = p->GetViewportRect();
				return NewRect(ctx, r.x, r.y, r.z, r.w);
			}
			case kPCustom: return JS_NewBool(ctx, p->HasCustomViewportSize());
			default: return JS_UNDEFINED;
			}
		}

		JSValue PlayerPossess(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_PLAYER(p);
			if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
			{
				p->Unpossess();
				return JS_UNDEFINED;
			}
			Actor* a = ActorFromJS(argv[0]);
			if (!a)
				return JS_ThrowTypeError(ctx, "possess(actor) : not a live Actor");
			p->Possess(a);
			return JS_UNDEFINED;
		}

		JSValue PlayerUnpossess(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			LYNX_THIS_PLAYER(p);
			p->Unpossess();
			return JS_UNDEFINED;
		}

		JSValue PlayerSetViewTarget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_PLAYER(p);
			Actor* a = argc > 0 ? ActorFromJS(argv[0]) : nullptr;
			p->SetViewTarget(a);
			return JS_UNDEFINED;
		}

		JSValue PlayerSetViewportSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_PLAYER(p);
			double v[4] = { 0, 0, 1, 1 };
			if (argc == 1 && JS_IsObject(argv[0]))
			{
				// {x, y, width, height}
				const char* keys[4] = { "x", "y", "width", "height" };
				for (int i = 0; i < 4; ++i)
				{
					JSValue f = JS_GetPropertyStr(ctx, argv[0], keys[i]);
					if (!JS_IsUndefined(f))
						JS_ToFloat64(ctx, &v[i], f);
					JS_FreeValue(ctx, f);
				}
			}
			else
			{
				if (argc < 4)
					return JS_ThrowTypeError(ctx, "setViewportSize(x, y, width, height) : normalized 0..1");
				for (int i = 0; i < 4; ++i)
					if (JS_ToFloat64(ctx, &v[i], argv[i]))
						return JS_EXCEPTION;
			}
			p->SetViewportSize(static_cast<float>(v[0]), static_cast<float>(v[1]),
			                   static_cast<float>(v[2]), static_cast<float>(v[3]));
			return JS_UNDEFINED;
		}

		JSValue PlayerResetViewportSize(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			LYNX_THIS_PLAYER(p);
			p->ResetViewportSize();
			return JS_UNDEFINED;
		}

		JSValue UICreate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv);

		// player.createWidget(path[, class]) = UI.create(path, player[, class])
		JSValue PlayerCreateWidget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_PLAYER(p);
			(void)p;
			JSValueConst args[3] = { argc > 0 ? argv[0] : JS_UNDEFINED, this_val, argc > 1 ? argv[1] : JS_UNDEFINED };
			return UICreate(ctx, JS_UNDEFINED, 3, args);
		}

		JSValue PlayerToString(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			PlayerController* p = ThisPlayer(this_val);
			if (!p)
				return JS_NewString(ctx, "PlayerController(<destroyed>)");
			const std::string text = "PlayerController(" + std::to_string(p->GetPlayerIndex()) + ")";
			return JS_NewString(ctx, text.c_str());
		}

		// ---- Engine.* -------------------------------------------------------

		JSValue EngineCreatePlayer(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			Engine* engine = Engine::Get();
			if (!engine)
				return JS_ThrowInternalError(ctx, "no engine");
			return PlayerObject(ctx, engine->CreatePlayer());
		}

		JSValue EngineDestroyPlayer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			Engine* engine = Engine::Get();
			PlayerController* p = argc > 0 ? PlayerFromJS(argv[0]) : nullptr;
			if (!engine || !p)
				return JS_NewBool(ctx, false);
			return JS_NewBool(ctx, engine->DestroyPlayer(p));
		}

		JSValue EngineGetPlayer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			Engine* engine = Engine::Get();
			int32_t index = 0;
			if (argc > 0 && JS_ToInt32(ctx, &index, argv[0]))
				return JS_EXCEPTION;
			return PlayerObject(ctx, engine ? engine->GetPlayer(index) : nullptr);
		}

		enum EngineGetter { kEPlayers, kEPlayerCount, kEDefaultPlayer, kERenderSize };

		JSValue EngineGet(JSContext* ctx, JSValueConst, int, JSValueConst*, int magic)
		{
			Engine* engine = Engine::Get();
			if (!engine)
				return JS_UNDEFINED;

			switch (magic)
			{
			case kEPlayers:
			{
				JSValue array = JS_NewArray(ctx);
				uint32_t i = 0;
				for (PlayerController* p : engine->GetPlayers())
					JS_SetPropertyUint32(ctx, array, i++, PlayerObject(ctx, p));
				return array;
			}
			case kEPlayerCount: return JS_NewInt32(ctx, engine->GetPlayerCount());
			case kEDefaultPlayer: return PlayerObject(ctx, engine->GetDefaultPlayer());
			case kERenderSize:
			{
				int w = 0, h = 0;
				engine->GetRenderSize(w, h);
				const float f[2] = { static_cast<float>(w), static_cast<float>(h) };
				return NewPlainVec(ctx, f, 2);
			}
			default: return JS_UNDEFINED;
			}
		}


		// =====================================================================
		// Actor : controller, isPossessed, autoPossessPlayer
		// =====================================================================

		enum ActorGetter { kAController, kAPossessed, kAAutoPossess };

		JSValue ActorGameplayGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			Actor* a = ActorFromJS(this_val);
			if (!a)
				return JS_ThrowReferenceError(ctx, "this Actor has been destroyed");
			switch (magic)
			{
			case kAController: return PlayerObject(ctx, a->GetController());
			case kAPossessed: return JS_NewBool(ctx, a->IsPossessed());
			case kAAutoPossess: return JS_NewInt32(ctx, a->auto_possess_player);
			default: return JS_UNDEFINED;
			}
		}

		JSValue ActorGameplaySet(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			Actor* a = ActorFromJS(this_val);
			if (!a)
				return JS_ThrowReferenceError(ctx, "this Actor has been destroyed");
			if (magic == kAAutoPossess)
			{
				int32_t v = -1;
				if (!JS_IsNull(argv[0]) && JS_ToInt32(ctx, &v, argv[0]))
					return JS_EXCEPTION;
				a->auto_possess_player = v;
			}
			return JS_UNDEFINED;
		}


		// =====================================================================
		// Widgets : valeurs des proprietes reflechies
		// =====================================================================

		Widget* WidgetFrom(JSValueConst value, JSClassID cls)
		{
			void* opaque = JS_GetOpaque(value, cls);
			return opaque ? WidgetSystem::Find(Decode(opaque)) : nullptr;
		}

		Widget* ThisWidget(JSValueConst this_val) { return WidgetFrom(this_val, g_widget_class); }

		#define LYNX_THIS_WIDGET(var) \
			Widget* var = ThisWidget(this_val); \
			if (!var) return ThrowDead(ctx, "Widget")

		bool ParseHexColor(const std::string& text, float out[4])
		{
			std::string hex = text.substr(text[0] == '#' ? 1 : 0);
			if (hex.size() != 6 && hex.size() != 8)
				return false;
			for (char c : hex)
				if (!std::isxdigit(static_cast<unsigned char>(c)))
					return false;
			for (int i = 0; i < 4; ++i)
				out[i] = i * 2 < static_cast<int>(hex.size())
					? static_cast<float>(std::strtol(hex.substr(i * 2, 2).c_str(), nullptr, 16)) / 255.f
					: 1.f;
			return true;
		}

		bool ReadKeys(JSContext* ctx, JSValueConst v, const char* const* keys, int n, float* out, float default_last)
		{
			bool any = false;
			for (int i = 0; i < n; ++i)
			{
				JSValue f = JS_GetPropertyStr(ctx, v, keys[i]);
				double d = 0.0;
				if (!JS_IsUndefined(f) && JS_ToFloat64(ctx, &d, f) == 0)
				{
					out[i] = static_cast<float>(d);
					any = true;
				}
				else if (i == n - 1)
				{
					out[i] = default_last;
				}
				else
				{
					JS_FreeValue(ctx, f);
					return false;
				}
				JS_FreeValue(ctx, f);
			}
			return any;
		}

		// vec4 : {r,g,b,a}, {left,top,right,bottom}, {x,y,z,w}, [..], "#rrggbb(aa)".
		bool ReadVec4Any(JSContext* ctx, JSValueConst v, float out[4])
		{
			if (JS_IsString(v))
				return ParseHexColor(ToStdString(ctx, v), out);
			if (!JS_IsObject(v))
				return false;

			static const char* const rgba[] = { "r", "g", "b", "a" };
			static const char* const ltrb[] = { "left", "top", "right", "bottom" };
			if (ReadKeys(ctx, v, rgba, 4, out, 1.f) || ReadKeys(ctx, v, ltrb, 4, out, 0.f))
				return true;
			if (ReadFloats(ctx, v, out, 4))
				return true;
			if (ReadFloats(ctx, v, out, 3))
			{
				out[3] = 1.f;
				return true;
			}
			return false;
		}

		JSValue PropertyValueToJS(JSContext* ctx, const std::string& name, const property& prop)
		{
			return std::visit([&](auto* ptr) -> JSValue
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return JS_UNDEFINED;

				if constexpr (std::is_same_v<T, int>)
				{
					const auto& names = ui::GetEnumNames(name);
					if (!names.empty() && *ptr >= 0 && *ptr < static_cast<int>(names.size()))
					{
						std::string label = names[static_cast<size_t>(*ptr)];
						label.erase(std::remove(label.begin(), label.end(), ' '), label.end());
						return JS_NewString(ctx, label.c_str());
					}
					return JS_NewInt32(ctx, *ptr);
				}
				else if constexpr (std::is_same_v<T, float>)
					return JS_NewFloat64(ctx, *ptr);
				else if constexpr (std::is_same_v<T, bool>)
					return JS_NewBool(ctx, *ptr);
				else if constexpr (std::is_same_v<T, std::string>)
					return JS_NewStringLen(ctx, ptr->data(), ptr->size());
				else if constexpr (std::is_same_v<T, vec2>)
				{
					const float f[2] = { ptr->x, ptr->y };
					return NewPlainVec(ctx, f, 2);
				}
				else if constexpr (std::is_same_v<T, vec3>)
				{
					const float f[3] = { ptr->x, ptr->y, ptr->z };
					return NewPlainVec(ctx, f, 3);
				}
				else if constexpr (std::is_same_v<T, vec4>)
				{
					if (IsColorProperty(name))
					{
						JSValue obj = JS_NewObject(ctx);
						JS_SetPropertyStr(ctx, obj, "r", JS_NewFloat64(ctx, ptr->x));
						JS_SetPropertyStr(ctx, obj, "g", JS_NewFloat64(ctx, ptr->y));
						JS_SetPropertyStr(ctx, obj, "b", JS_NewFloat64(ctx, ptr->z));
						JS_SetPropertyStr(ctx, obj, "a", JS_NewFloat64(ctx, ptr->w));
						return obj;
					}
					const float f[4] = { ptr->x, ptr->y, ptr->z, ptr->w };
					return NewPlainVec(ctx, f, 4);
				}
				else
					return JS_UNDEFINED;
			}, prop.property_member);
		}

		// false + exception JS si la valeur ne convient pas.
		bool JSToPropertyValue(JSContext* ctx, JSValueConst v, const std::string& name, const property& prop)
		{
			return std::visit([&](auto* ptr) -> bool
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return false;

				if constexpr (std::is_same_v<T, int>)
				{
					const auto& names = ui::GetEnumNames(name);
					if (JS_IsString(v) && !names.empty())
					{
						const std::string wanted = Simplify(ToStdString(ctx, v));
						for (size_t i = 0; i < names.size(); ++i)
						{
							if (Simplify(names[i]) == wanted)
							{
								*ptr = static_cast<int>(i);
								return true;
							}
						}
						std::string list;
						for (const auto& n : names)
							list += (list.empty() ? "" : ", ") + n;
						JS_ThrowTypeError(ctx, "%s : one of %s", CamelCase(name).c_str(), list.c_str());
						return false;
					}
					int32_t i = 0;
					if (JS_ToInt32(ctx, &i, v))
						return false;
					*ptr = i;
					return true;
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					double d = 0;
					if (JS_ToFloat64(ctx, &d, v))
						return false;
					*ptr = static_cast<float>(d);
					return true;
				}
				else if constexpr (std::is_same_v<T, bool>)
				{
					*ptr = JS_ToBool(ctx, v) > 0;
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
					if (!ReadFloats(ctx, v, f, 2))
					{
						JS_ThrowTypeError(ctx, "%s : {x, y} expected", CamelCase(name).c_str());
						return false;
					}
					*ptr = vec2(f[0], f[1]);
					return true;
				}
				else if constexpr (std::is_same_v<T, vec3>)
				{
					float f[3];
					if (!ReadFloats(ctx, v, f, 3))
					{
						JS_ThrowTypeError(ctx, "%s : {x, y, z} expected", CamelCase(name).c_str());
						return false;
					}
					*ptr = vec3(f[0], f[1], f[2]);
					return true;
				}
				else if constexpr (std::is_same_v<T, vec4>)
				{
					float f[4];
					if (!ReadVec4Any(ctx, v, f))
					{
						JS_ThrowTypeError(ctx, IsColorProperty(name)
							? "%s : {r, g, b, a}, [r, g, b, a] or \"#rrggbb\" expected"
							: "%s : {x, y, z, w} (or {left, top, right, bottom}) expected",
							CamelCase(name).c_str());
						return false;
					}
					ptr->x = f[0]; ptr->y = f[1]; ptr->z = f[2]; ptr->w = f[3];
					return true;
				}
				else
					return false;
			}, prop.property_member);
		}

		// Propriete reflechie par son nom JS (camelCase) ou C++ (snake_case).
		const property* FindByJSName(const Object& object, const std::string& js_name, std::string* cpp_name = nullptr)
		{
			for (const auto& [name, prop] : object.GetProperties())
			{
				if (name == "object_id_")
					continue;
				if (name == js_name || CamelCase(name) == js_name)
				{
					if (cpp_name)
						*cpp_name = name;
					return &prop;
				}
			}
			return nullptr;
		}

		// ---- obj.fontSize (getters / setters par objet) ---------------------

		// data[0] : nom C++, data[1] : 0 widget, 1 slot
		Object* ReflectedTarget(JSValueConst this_val, int kind)
		{
			if (kind == 0)
				return ThisWidget(this_val);
			Widget* w = WidgetFrom(this_val, g_slot_class);
			return w ? w->GetSlot() : nullptr;
		}

		JSValue ReflectedGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int, JSValue* data)
		{
			int32_t kind = 0;
			JS_ToInt32(ctx, &kind, data[1]);
			Object* object = ReflectedTarget(this_val, kind);
			if (!object)
				return ThrowDead(ctx, kind ? "slot" : "Widget");

			const std::string name = ToStdString(ctx, data[0]);
			auto it = object->GetProperties().find(name);
			return it == object->GetProperties().end() ? JS_UNDEFINED : PropertyValueToJS(ctx, name, it->second);
		}

		JSValue ReflectedSet(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int, JSValue* data)
		{
			int32_t kind = 0;
			JS_ToInt32(ctx, &kind, data[1]);
			Object* object = ReflectedTarget(this_val, kind);
			if (!object)
				return ThrowDead(ctx, kind ? "slot" : "Widget");

			const std::string name = ToStdString(ctx, data[0]);
			auto it = object->GetProperties().find(name);
			if (it == object->GetProperties().end())
				return JS_UNDEFINED;
			if (!JSToPropertyValue(ctx, argv[0], name, it->second))
				return JS_EXCEPTION;

			// A nested UserWidget shows its new asset.
			if (name == "asset")
				if (auto* user = dynamic_cast<UserWidget*>(object))
					user->LoadFromAsset(user->asset);
			return JS_UNDEFINED;
		}

		void DefineReflected(JSContext* ctx, JSValueConst obj, const Object& object, int kind)
		{
			for (const auto& [name, prop] : object.GetProperties())
			{
				if (name == "object_id_" || prop.GetType() == 7)
					continue;

				const std::string js_name = CamelCase(name);
				JSAtom atom = JS_NewAtom(ctx, js_name.c_str());

				// API (prototype, methode d'une classe JS) : elle gagne.
				if (JS_HasProperty(ctx, obj, atom) > 0)
				{
					JS_FreeAtom(ctx, atom);
					continue;
				}

				JSValue data[2] = { JS_NewString(ctx, name.c_str()), JS_NewInt32(ctx, kind) };
				JSValue get = JS_NewCFunctionData(ctx, ReflectedGet, 0, 0, 2, data);
				JSValue set = JS_NewCFunctionData(ctx, ReflectedSet, 1, 0, 2, data);
				JS_DefinePropertyGetSet(ctx, obj, atom, get, set, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
				JS_FreeValue(ctx, data[0]);
				JS_FreeValue(ctx, data[1]);
				JS_FreeAtom(ctx, atom);
			}
		}

		// obj.set({...}) / options de addChild : proprietes, `name`, `slot`.
		bool ApplyOptions(JSContext* ctx, Widget& widget, JSValueConst options)
		{
			if (JS_IsUndefined(options) || JS_IsNull(options))
				return true;
			if (!JS_IsObject(options))
			{
				JS_ThrowTypeError(ctx, "options : an object is expected");
				return false;
			}

			JSPropertyEnum* tab = nullptr;
			uint32_t len = 0;
			if (JS_GetOwnPropertyNames(ctx, &tab, &len, options, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
				return false;

			bool ok = true;
			for (uint32_t i = 0; i < len && ok; ++i)
			{
				const char* key_c = JS_AtomToCString(ctx, tab[i].atom);
				const std::string key = key_c ? key_c : "";
				JS_FreeCString(ctx, key_c);
				JSValue value = JS_GetProperty(ctx, options, tab[i].atom);

				if (key == "name")
				{
					widget.SetName(ToStdString(ctx, value));
				}
				else if (key == "slot")
				{
					PanelSlot* slot = widget.GetSlot();
					if (!slot)
					{
						JS_ThrowTypeError(ctx, "slot : this widget is not in a panel");
						ok = false;
					}
					else if (JS_IsObject(value))
					{
						JSPropertyEnum* stab = nullptr;
						uint32_t slen = 0;
						if (JS_GetOwnPropertyNames(ctx, &stab, &slen, value, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0)
						{
							for (uint32_t j = 0; j < slen && ok; ++j)
							{
								const char* sk = JS_AtomToCString(ctx, stab[j].atom);
								std::string cpp;
								const property* p = FindByJSName(*slot, sk ? sk : "", &cpp);
								JSValue sv = JS_GetProperty(ctx, value, stab[j].atom);
								if (!p)
								{
									JS_ThrowTypeError(ctx, "slot has no property '%s'", sk ? sk : "");
									ok = false;
								}
								else if (!JSToPropertyValue(ctx, sv, cpp, *p))
								{
									ok = false;
								}
								JS_FreeValue(ctx, sv);
								JS_FreeCString(ctx, sk);
							}
							for (uint32_t j = 0; j < slen; ++j)
								JS_FreeAtom(ctx, stab[j].atom);
							js_free(ctx, stab);
						}
						slot->Invalidate();
					}
				}
				else
				{
					std::string cpp;
					const property* p = FindByJSName(widget, key, &cpp);
					if (!p)
					{
						JS_ThrowTypeError(ctx, "%s has no property '%s'", widget.GetTypeName().c_str(), key.c_str());
						ok = false;
					}
					else if (!JSToPropertyValue(ctx, value, cpp, *p))
					{
						ok = false;
					}
					else if (cpp == "asset")
					{
						if (auto* user = dynamic_cast<UserWidget*>(&widget))
							user->LoadFromAsset(user->asset);
					}
				}
				JS_FreeValue(ctx, value);
			}

			for (uint32_t i = 0; i < len; ++i)
				JS_FreeAtom(ctx, tab[i].atom);
			js_free(ctx, tab);

			widget.Invalidate();
			return ok;
		}


		// =====================================================================
		// Objets JS des widgets
		// =====================================================================

		JSValue NewWidgetObject(JSContext* ctx, Widget& widget, JSValueConst proto)
		{
			JSValue obj = JS_NewObjectProtoClass(ctx, proto, g_widget_class);
			if (JS_IsException(obj))
				return obj;
			JS_SetOpaque(obj, Encode(widget.GetUniqueId()));
			DefineReflected(ctx, obj, widget, 0);
			S().widgets[widget.GetUniqueId()] = JS_DupValue(ctx, obj);
			return obj;
		}

		JSValue WidgetToJS(JSContext* ctx, Widget* widget)
		{
			if (!widget)
				return JS_NULL;

			auto it = S().widgets.find(widget->GetUniqueId());
			if (it != S().widgets.end())
				return JS_DupValue(ctx, it->second);

			const bool user = dynamic_cast<UserWidget*>(widget) != nullptr;
			return NewWidgetObject(ctx, *widget, user ? S().user_widget_proto : S().widget_proto);
		}

		// Lie l'objet du widget a la classe JS `name` (prototype).
		void BindClass(JSContext* ctx, UserWidget& widget, const std::string& name)
		{
			auto cls = S().classes.find(name);
			if (cls == S().classes.end())
				return;

			JSValue proto = JS_GetPropertyStr(ctx, cls->second.ctor, "prototype");
			auto it = S().widgets.find(widget.GetUniqueId());
			if (it != S().widgets.end())
			{
				JS_SetPrototype(ctx, it->second, proto);
			}
			else
			{
				JSValue obj = NewWidgetObject(ctx, widget, proto);
				JS_FreeValue(ctx, obj);
			}
			JS_FreeValue(ctx, proto);
			S().bound[widget.GetUniqueId()] = name;
		}

		// Appelle obj.name(...) si c'est une fonction (classe JS du widget).
		void CallWidgetMethod(JSContext* ctx, Widget& widget, const char* name, int argc, JSValueConst* argv)
		{
			auto it = S().widgets.find(widget.GetUniqueId());
			if (it == S().widgets.end())
				return;

			JSValue obj = JS_DupValue(ctx, it->second);
			JSValue fn = JS_GetPropertyStr(ctx, obj, name);
			if (JS_IsFunction(ctx, fn))
			{
				EnterCall();
				JSValue r = JS_Call(ctx, fn, obj, argc, argv);
				LeaveCall();
				if (JS_IsException(r))
				{
					auto cls = S().bound.find(widget.GetUniqueId());
					LogException(ctx, (cls != S().bound.end() ? cls->second : std::string("UserWidget")) + "." + name);
				}
				JS_FreeValue(ctx, r);
				RunPendingJobs();
			}
			JS_FreeValue(ctx, fn);
			JS_FreeValue(ctx, obj);
		}

		// Nouveau widget : "Button", ou un .widget (UserWidget de cet asset).
		std::unique_ptr<Widget> MakeWidget(JSContext* ctx, const std::string& what)
		{
			const bool asset = what.size() > 7 && what.compare(what.size() - 7, 7, ".widget") == 0;
			if (asset)
			{
				auto user = std::make_unique<UserWidget>();
				user->asset = what;
				if (!user->LoadFromAsset(what))
				{
					JS_ThrowReferenceError(ctx, "could not read assets/%s", what.c_str());
					return nullptr;
				}
				return user;
			}

			std::unique_ptr<Widget> widget = ui::NewWidget(what);
			if (!widget)
			{
				std::string list;
				for (const auto& c : ui::GetBuiltinWidgetClasses())
					list += (list.empty() ? "" : ", ") + c;
				JS_ThrowTypeError(ctx, "unknown widget class \"%s\" (%s, or a .widget asset)", what.c_str(), list.c_str());
				return nullptr;
			}
			return widget;
		}

		// Un widget ajoute a un arbre deja construit : sa classe JS (fichier) demarre.
		void ConstructAdded(Widget& widget)
		{
			std::vector<UserWidget*> users;
			widget.ForEachWidget([&](Widget& w)
			{
				if (auto* u = dynamic_cast<UserWidget*>(&w))
					users.push_back(u);
			});
			for (auto it = users.rbegin(); it != users.rend(); ++it)
				if (!(*it)->GetClassName().empty())
					scripting::OnUserWidgetEvent(**it, scripting::WidgetEvent::Construct, 0.f);
		}

		// ---- Getters / setters communs --------------------------------------

		enum WidgetProp
		{
			kWName, kWClassName, kWValid, kWParent, kWSlot, kWChildren, kWChildCount, kWUserWidget,
			kWVisible, kWHovered, kWGeometry, kWDesiredSize, kWInViewport, kWOwningPlayer, kWRoot,
			kWWidgetClass, kWAssetPath,
		};

		JSValue WidgetGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			Widget* w = ThisWidget(this_val);
			if (magic == kWValid)
				return JS_NewBool(ctx, w != nullptr);
			if (!w)
				return ThrowDead(ctx, "Widget");

			auto* user = dynamic_cast<UserWidget*>(w);
			auto* panel = dynamic_cast<PanelWidget*>(w);

			switch (magic)
			{
			case kWName: return JS_NewString(ctx, w->GetName().c_str());
			case kWClassName: return JS_NewString(ctx, w->GetTypeName().c_str());
			case kWParent:
			{
				if (PanelWidget* parent = w->GetParent())
					return WidgetToJS(ctx, parent);
				return WidgetToJS(ctx, w->GetUserWidget());
			}
			case kWSlot:
			{
				if (!w->GetSlot())
					return JS_NULL;
				JSValue obj = JS_NewObjectProtoClass(ctx, S().slot_proto, g_slot_class);
				JS_SetOpaque(obj, Encode(w->GetUniqueId()));
				DefineReflected(ctx, obj, *w->GetSlot(), 1);
				return obj;
			}
			case kWChildren:
			{
				JSValue array = JS_NewArray(ctx);
				if (panel)
					for (int i = 0; i < panel->GetChildCount(); ++i)
						JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), WidgetToJS(ctx, panel->GetChildAt(i)));
				else if (user && user->GetRoot())
					JS_SetPropertyUint32(ctx, array, 0, WidgetToJS(ctx, user->GetRoot()));
				return array;
			}
			case kWChildCount: return JS_NewInt32(ctx, panel ? panel->GetChildCount() : (user && user->GetRoot() ? 1 : 0));
			case kWUserWidget: return WidgetToJS(ctx, w->GetUserWidget());
			case kWVisible: return JS_NewBool(ctx, w->IsVisible());
			case kWHovered: return JS_NewBool(ctx, w->IsHovered());
			case kWGeometry:
			{
				const WidgetRect& g = w->GetGeometry();
				return NewRect(ctx, g.x, g.y, g.w, g.h);
			}
			case kWDesiredSize:
			{
				const vec2 d = w->GetDesiredSize();
				const float f[2] = { d.x, d.y };
				return NewPlainVec(ctx, f, 2);
			}
			case kWInViewport: return JS_NewBool(ctx, user && user->IsInViewport());
			case kWOwningPlayer: return user ? PlayerObject(ctx, user->GetOwningPlayer()) : JS_NULL;
			case kWRoot: return user ? WidgetToJS(ctx, user->GetRoot()) : JS_NULL;
			case kWWidgetClass: return user ? JS_NewString(ctx, user->GetClassName().c_str()) : JS_NULL;
			case kWAssetPath: return user ? JS_NewString(ctx, user->GetAssetPath().c_str()) : JS_NULL;
			default: return JS_UNDEFINED;
			}
		}

		JSValue WidgetSet(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv, int magic)
		{
			LYNX_THIS_WIDGET(w);
			switch (magic)
			{
			case kWName:
				w->SetName(ToStdString(ctx, argv[0]));
				break;
			case kWVisible:
				w->SetVisibility(JS_ToBool(ctx, argv[0]) > 0 ? EVisibility::Visible : EVisibility::Collapsed);
				break;
			case kWOwningPlayer:
			{
				auto* user = dynamic_cast<UserWidget*>(w);
				if (!user)
					return JS_ThrowTypeError(ctx, "owningPlayer : only a UserWidget has a player");
				PlayerController* p = nullptr;
				if (!ReadOptionalPlayer(ctx, argv[0], p))
					return JS_EXCEPTION;
				user->SetOwningPlayer(p);
				break;
			}
			default:
				break;
			}
			return JS_UNDEFINED;
		}

		// ---- Methodes ---------------------------------------------------------

		JSValue WidgetRemoveFromParent(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			LYNX_THIS_WIDGET(w);
			// UserWidget a l'ecran : retire (toujours utilisable). Sinon : detruit.
			w->RemoveFromParent();
			return JS_UNDEFINED;
		}

		JSValue WidgetDestroy(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			LYNX_THIS_WIDGET(w);
			if (auto* user = dynamic_cast<UserWidget*>(w); user && !user->GetParent())
			{
				const auto all = ui::GetAllWidgets();
				if (std::find(all.begin(), all.end(), user) != all.end())
				{
					DestroyWidget(user);
					return JS_UNDEFINED;
				}
			}
			if (w->GetParent())
				w->RemoveFromParent();
			return JS_UNDEFINED;
		}

		Widget* FindInTree(Widget& root, const std::string& name)
		{
			if (auto* user = dynamic_cast<UserWidget*>(&root))
				return user->GetWidgetFromName(name);

			Widget* found = nullptr;
			std::function<void(Widget&)> visit = [&](Widget& w)
			{
				if (found)
					return;
				if (&w != &root && w.GetName() == name)
				{
					found = &w;
					return;
				}
				if (auto* panel = dynamic_cast<PanelWidget*>(&w))
					for (int i = 0; i < panel->GetChildCount() && !found; ++i)
						visit(*panel->GetChildAt(i));
			};
			visit(root);
			return found;
		}

		JSValue WidgetFind(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_WIDGET(w);
			if (argc < 1)
				return JS_ThrowTypeError(ctx, "find(name)");
			return WidgetToJS(ctx, FindInTree(*w, ToStdString(ctx, argv[0])));
		}

		JSValue WidgetGetChildAt(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_WIDGET(w);
			int32_t index = 0;
			if (argc < 1 || JS_ToInt32(ctx, &index, argv[0]))
				return JS_ThrowTypeError(ctx, "getChildAt(index)");
			if (auto* panel = dynamic_cast<PanelWidget*>(w))
				return WidgetToJS(ctx, panel->GetChildAt(index));
			return JS_NULL;
		}

		// addChild(class | ".widget", options) / insertChildAt(index, class, options)
		JSValue WidgetAddChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic)
		{
			LYNX_THIS_WIDGET(w);
			auto* panel = dynamic_cast<PanelWidget*>(w);
			if (!panel)
				return JS_ThrowTypeError(ctx, "%s is not a panel : it can not have children", w->GetTypeName().c_str());

			const bool insert = magic == 1;
			int32_t index = panel->GetChildCount();
			if (insert && (argc < 1 || JS_ToInt32(ctx, &index, argv[0])))
				return JS_ThrowTypeError(ctx, "insertChildAt(index, class, options)");

			const int first = insert ? 1 : 0;
			if (argc <= first || !JS_IsString(argv[first]))
				return JS_ThrowTypeError(ctx, insert ? "insertChildAt(index, class, options)" : "addChild(class, options)");
			if (!panel->CanAddChild())
				return JS_ThrowRangeError(ctx, "%s can only have %d child", w->GetTypeName().c_str(), panel->GetMaxChildren());

			std::unique_ptr<Widget> child = MakeWidget(ctx, ToStdString(ctx, argv[first]));
			if (!child)
				return JS_EXCEPTION;

			Widget* raw = child.get();
			panel->InsertChildAt(index, std::move(child));

			// Point d'ancrage par defaut dans un canvas : sa taille voulue.
			if (auto* slot = raw->GetSlotAs<CanvasPanelSlot>())
			{
				const vec2 desired = raw->GetDesiredSize();
				if (desired.x > 0.f && desired.y > 0.f)
					slot->SetSize(desired);
			}

			if (argc > first + 1 && !ApplyOptions(ctx, *raw, argv[first + 1]))
			{
				raw->RemoveFromParent();
				return JS_EXCEPTION;
			}

			ConstructAdded(*raw);
			return WidgetToJS(ctx, raw);
		}

		JSValue WidgetRemoveChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_WIDGET(w);
			auto* panel = dynamic_cast<PanelWidget*>(w);
			Widget* child = argc > 0 ? ThisWidget(argv[0]) : nullptr;
			if (!panel || !child || child->GetParent() != panel)
				return JS_NewBool(ctx, false);
			panel->RemoveChild(child);   // detruit
			return JS_NewBool(ctx, true);
		}

		JSValue WidgetClearChildren(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			LYNX_THIS_WIDGET(w);
			if (auto* panel = dynamic_cast<PanelWidget*>(w))
				panel->ClearChildren();
			return JS_UNDEFINED;
		}

		JSValue WidgetSetOptions(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_WIDGET(w);
			if (argc < 1 || !ApplyOptions(ctx, *w, argv[0]))
				return argc < 1 ? JS_ThrowTypeError(ctx, "set({ property: value, ... })") : JS_EXCEPTION;
			return JS_DupValue(ctx, this_val);
		}

		JSValue WidgetInvalidate(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			LYNX_THIS_WIDGET(w);
			w->Invalidate();
			return JS_UNDEFINED;
		}

		JSValue WidgetAddToViewport(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_WIDGET(w);
			auto* user = dynamic_cast<UserWidget*>(w);
			if (!user)
				return JS_ThrowTypeError(ctx, "addToViewport : only a UserWidget (UI.create) goes on the screen");
			int32_t z = 0;
			if (argc > 0 && JS_ToInt32(ctx, &z, argv[0]))
				return JS_EXCEPTION;
			user->AddToViewport(z);
			return JS_UNDEFINED;
		}

		JSValue WidgetToString(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
		{
			Widget* w = ThisWidget(this_val);
			if (!w)
				return JS_NewString(ctx, "Widget(<destroyed>)");
			const std::string text = w->GetTypeName() + "(\"" + w->GetName() + "\")";
			return JS_NewString(ctx, text.c_str());
		}

		// ---- Evenements -------------------------------------------------------

		enum WidgetEventKind
		{
			kEvClicked, kEvPressed, kEvReleased, kEvHovered, kEvUnhovered, kEvValueChanged, kEvCheckChanged, kEvCount
		};

		const char* const kEventNames[kEvCount] = {
			"clicked", "pressed", "released", "hovered", "unhovered", "valueChanged", "checkStateChanged"
		};

		int EventIndex(const std::string& name)
		{
			const std::string wanted = Simplify(name);
			for (int i = 0; i < kEvCount; ++i)
				if (Simplify(kEventNames[i]) == wanted || Simplify(std::string("on") + kEventNames[i]) == wanted)
					return i;
			return -1;
		}

		HEventDispatcher<>* VoidEvent(Widget& w, int event)
		{
			auto* button = dynamic_cast<Button*>(&w);
			if (!button)
				return nullptr;
			switch (event)
			{
			case kEvClicked: return &button->OnClicked;
			case kEvPressed: return &button->OnPressed;
			case kEvReleased: return &button->OnReleased;
			case kEvHovered: return &button->OnHovered;
			case kEvUnhovered: return &button->OnUnhovered;
			default: return nullptr;
			}
		}

		// Abonne `fn` ; retourne l'id (pour off) ou leve une erreur.
		JSValue Subscribe(JSContext* ctx, Widget& w, int event, JSValueConst fn)
		{
			if (!JS_IsFunction(ctx, fn))
				return JS_ThrowTypeError(ctx, "a function is expected");

			const Ref ref = MakeRef(ctx, fn, w.GetUniqueId());
			const std::string where = w.GetTypeName() + " \"" + w.GetName() + "\" " + kEventNames[event];

			if (HEventDispatcher<>* d = VoidEvent(w, event))
				return JS_NewInt32(ctx, d->Subscribe([ref, where]() { CallRef(ref, 0, nullptr, where.c_str()); }));

			if (event == kEvValueChanged)
				if (auto* slider = dynamic_cast<Slider*>(&w))
					return JS_NewInt32(ctx, slider->OnValueChanged.Subscribe([ref, where](float v)
					{
						if (JSContext* c = Context())
						{
							JSValue arg = JS_NewFloat64(c, v);
							CallRef(ref, 1, &arg, where.c_str());
						}
					}));

			if (event == kEvCheckChanged)
				if (auto* check = dynamic_cast<CheckBox*>(&w))
					return JS_NewInt32(ctx, check->OnCheckStateChanged.Subscribe([ref, where](bool v)
					{
						if (JSContext* c = Context())
						{
							JSValue arg = JS_NewBool(c, v);
							CallRef(ref, 1, &arg, where.c_str());
						}
					}));

			return JS_ThrowTypeError(ctx, "%s has no \"%s\" event", w.GetTypeName().c_str(), kEventNames[event]);
		}

		// on("clicked", fn)
		JSValue WidgetOn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_WIDGET(w);
			if (argc < 2)
				return JS_ThrowTypeError(ctx, "on(event, function)");
			const int event = EventIndex(ToStdString(ctx, argv[0]));
			if (event < 0)
				return JS_ThrowTypeError(ctx, "unknown event (clicked, pressed, released, hovered, unhovered, valueChanged, checkStateChanged)");
			return Subscribe(ctx, *w, event, argv[1]);
		}

		// onClicked(fn)...
		JSValue WidgetOnMagic(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic)
		{
			LYNX_THIS_WIDGET(w);
			if (argc < 1)
				return JS_ThrowTypeError(ctx, "a function is expected");
			return Subscribe(ctx, *w, magic, argv[0]);
		}

		// off("clicked", id)
		JSValue WidgetOff(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
		{
			LYNX_THIS_WIDGET(w);
			int32_t id = 0;
			if (argc < 2 || JS_ToInt32(ctx, &id, argv[1]))
				return JS_ThrowTypeError(ctx, "off(event, id)");
			const int event = EventIndex(ToStdString(ctx, argv[0]));
			if (HEventDispatcher<>* d = VoidEvent(*w, event))
				d->Unsubscribe(id);
			else if (auto* slider = dynamic_cast<Slider*>(w); slider && event == kEvValueChanged)
				slider->OnValueChanged.Unsubscribe(id);
			else if (auto* check = dynamic_cast<CheckBox*>(w); check && event == kEvCheckChanged)
				check->OnCheckStateChanged.Unsubscribe(id);
			return JS_UNDEFINED;
		}


		// ---- Slot -------------------------------------------------------------

		Widget* ThisSlotWidget(JSValueConst this_val) { return WidgetFrom(this_val, g_slot_class); }

		JSValue SlotGet(JSContext* ctx, JSValueConst this_val, int, JSValueConst*, int magic)
		{
			Widget* w = ThisSlotWidget(this_val);
			if (magic == 2)
				return JS_NewBool(ctx, w && w->GetSlot());
			if (!w || !w->GetSlot())
				return ThrowDead(ctx, "slot");
			if (magic == 0)
				return WidgetToJS(ctx, w);
			// type : "CanvasPanelSlot"...
			return JS_NewString(ctx, w->GetSlot()->GetTypeName().c_str());
		}

		// magic : 0 setPosition, 1 setSize, 2 setAnchors, 3 setAlignment
		JSValue SlotCanvasOp(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic)
		{
			Widget* w = ThisSlotWidget(this_val);
			if (!w || !w->GetSlot())
				return ThrowDead(ctx, "slot");
			auto* slot = w->GetSlotAs<CanvasPanelSlot>();
			if (!slot)
				return JS_ThrowTypeError(ctx, "only a CanvasPanel slot has a position / size / anchors");

			const int needed = magic == 2 ? 4 : 2;
			float v[4] = {};
			if (argc == 1 && JS_IsObject(argv[0]))
			{
				if (magic == 2)
				{
					// {min: {x,y}, max: {x,y}}
					JSValue mn = JS_GetPropertyStr(ctx, argv[0], "min");
					JSValue mx = JS_GetPropertyStr(ctx, argv[0], "max");
					const bool ok = ReadFloats(ctx, mn, v, 2) && ReadFloats(ctx, mx, v + 2, 2);
					JS_FreeValue(ctx, mn);
					JS_FreeValue(ctx, mx);
					if (!ok)
						return JS_ThrowTypeError(ctx, "setAnchors(minX, minY, maxX, maxY) or ({min, max})");
				}
				else if (!ReadFloats(ctx, argv[0], v, 2))
				{
					return JS_ThrowTypeError(ctx, "{x, y} expected");
				}
			}
			else
			{
				if (argc < needed)
					return JS_ThrowTypeError(ctx, "%d numbers expected", needed);
				for (int i = 0; i < needed; ++i)
				{
					double d = 0;
					if (JS_ToFloat64(ctx, &d, argv[i]))
						return JS_EXCEPTION;
					v[i] = static_cast<float>(d);
				}
			}

			switch (magic)
			{
			case 0: slot->SetPosition(vec2(v[0], v[1])); break;
			case 1: slot->SetSize(vec2(v[0], v[1])); break;
			case 2: slot->SetAnchors(vec2(v[0], v[1]), vec2(v[2], v[3])); break;
			case 3: slot->alignment = vec2(v[0], v[1]); slot->Invalidate(); break;
			default: break;
			}
			return JS_DupValue(ctx, this_val);
		}


		// =====================================================================
		// UI.*
		// =====================================================================

		JSValue UICreate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			if (argc < 1 || !JS_IsString(argv[0]))
				return JS_ThrowTypeError(ctx, "UI.create(path [, player [, class]]) : path of a .widget in assets/");

			const std::string path = ToStdString(ctx, argv[0]);
			PlayerController* owner = nullptr;
			if (argc > 1 && !ReadOptionalPlayer(ctx, argv[1], owner))
				return JS_EXCEPTION;

			// Classe : nom ou constructeur (class MainMenu extends UserWidget).
			std::string cls;
			if (argc > 2 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]))
			{
				if (JS_IsFunction(ctx, argv[2]))
				{
					JSValue name = JS_GetPropertyStr(ctx, argv[2], "name");
					cls = ToStdString(ctx, name);
					JS_FreeValue(ctx, name);
				}
				else
				{
					cls = ToStdString(ctx, argv[2]);
				}
			}

			UserWidget* widget = nullptr;
			if (cls.empty())
			{
				widget = CreateWidget(owner, path);
			}
			else
			{
				std::unique_ptr<UserWidget> made;
				if (S().classes.count(cls))
				{
					made = std::make_unique<UserWidget>();
				}
				else
				{
					std::unique_ptr<Widget> w = ui::NewWidget(cls);
					if (auto* u = dynamic_cast<UserWidget*>(w.get()))
					{
						w.release();
						made.reset(u);
					}
				}
				if (!made)
					return JS_ThrowTypeError(ctx, "\"%s\" is not a UserWidget class (JavaScript class extends UserWidget, or C++)", cls.c_str());
				if (!made->LoadFromAsset(path))
					return JS_ThrowReferenceError(ctx, "could not read assets/%s", path.c_str());
				made->SetClassName(cls);
				widget = ui::AdoptWidget(std::move(made), owner);
			}

			if (!widget)
				return JS_ThrowReferenceError(ctx, "could not create the widget assets/%s", path.c_str());
			return WidgetToJS(ctx, widget);
		}

		JSValue UIDestroy(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			Widget* w = argc > 0 ? ThisWidget(argv[0]) : nullptr;
			if (auto* user = dynamic_cast<UserWidget*>(w))
				DestroyWidget(user);
			return JS_UNDEFINED;
		}

		JSValue UIAll(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			JSValue array = JS_NewArray(ctx);
			uint32_t i = 0;
			for (UserWidget* w : ui::GetAllWidgets())
				JS_SetPropertyUint32(ctx, array, i++, WidgetToJS(ctx, w));
			return array;
		}

		JSValue UIGetDPIReference(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			return JS_NewFloat64(ctx, ui::GetDPIReference());
		}

		JSValue UISetDPIReference(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
		{
			double v = 1080.0;
			if (argc < 1 || JS_ToFloat64(ctx, &v, argv[0]))
				return JS_ThrowTypeError(ctx, "setDPIReference(shortestSide)");
			ui::SetDPIReference(static_cast<float>(v));
			return JS_UNDEFINED;
		}

		JSValue UIClasses(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			JSValue array = JS_NewArray(ctx);
			uint32_t i = 0;
			for (const auto& c : ui::GetBuiltinWidgetClasses())
				JS_SetPropertyUint32(ctx, array, i++, JS_NewString(ctx, c.c_str()));
			return array;
		}

		// new UserWidget() : refuse (UI.create construit les widgets).
		JSValue UserWidgetConstructor(JSContext* ctx, JSValueConst, int, JSValueConst*)
		{
			return JS_ThrowTypeError(ctx, "a UserWidget is made by UI.create(\"ui/File.widget\", player, Class)");
		}
	}


	// =========================================================================
	// Interface interne (ScriptSystem.cpp, ComponentBindings.cpp)
	// =========================================================================

	JSValue PlayerObject(JSContext* ctx, PlayerController* player)
	{
		if (!player || !ctx)
			return JS_NULL;

		auto it = S().players.find(player->GetId());
		if (it != S().players.end())
			return JS_DupValue(ctx, it->second);

		JSValue obj = JS_NewObjectProtoClass(ctx, S().player_proto, g_player_class);
		if (JS_IsException(obj))
			return obj;
		JS_SetOpaque(obj, Encode(static_cast<uint64_t>(player->GetId()) + 1));
		S().players[player->GetId()] = JS_DupValue(ctx, obj);
		return obj;
	}

	PlayerController* PlayerFromJS(JSValueConst value)
	{
		return ThisPlayer(value);
	}

	void RegisterGameplayBindings(JSContext* ctx, JSValueConst actor_proto)
	{
		DefGetSet(ctx, actor_proto, "controller", ActorGameplayGet, nullptr, kAController);
		DefGetSet(ctx, actor_proto, "isPossessed", ActorGameplayGet, nullptr, kAPossessed);
		DefGetSet(ctx, actor_proto, "autoPossessPlayer", ActorGameplayGet, ActorGameplaySet, kAAutoPossess);
	}

	void RegisterGameplayGlobals(JSContext* ctx)
	{
		JSRuntime* rt = JS_GetRuntime(ctx);
		State& st = S();

		g_player_class = 0;
		g_widget_class = 0;
		g_slot_class = 0;
		JS_NewClassID(rt, &g_player_class);
		JS_NewClassID(rt, &g_widget_class);
		JS_NewClassID(rt, &g_slot_class);

		JSClassDef player_def{};
		player_def.class_name = "PlayerController";
		JS_NewClass(rt, g_player_class, &player_def);
		JSClassDef widget_def{};
		widget_def.class_name = "Widget";
		JS_NewClass(rt, g_widget_class, &widget_def);
		JSClassDef slot_def{};
		slot_def.class_name = "WidgetSlot";
		JS_NewClass(rt, g_slot_class, &slot_def);

		JSValue global = JS_GetGlobalObject(ctx);

		// ---- PlayerController ------------------------------------------------
		st.player_proto = JS_NewObject(ctx);
		DefGet(ctx, st.player_proto, "index", PlayerGet, kPIndex);
		DefGet(ctx, st.player_proto, "id", PlayerGet, kPId);
		DefGet(ctx, st.player_proto, "valid", PlayerGet, kPValid);
		DefGet(ctx, st.player_proto, "isDefault", PlayerGet, kPDefault);
		DefGet(ctx, st.player_proto, "possessed", PlayerGet, kPPossessed);
		DefGet(ctx, st.player_proto, "pawn", PlayerGet, kPPossessed);
		DefGet(ctx, st.player_proto, "viewportRect", PlayerGet, kPRect);
		DefGet(ctx, st.player_proto, "hasCustomViewportSize", PlayerGet, kPCustom);
		DefFunc(ctx, st.player_proto, "possess", PlayerPossess, 1);
		DefFunc(ctx, st.player_proto, "unpossess", PlayerUnpossess, 0);
		DefFunc(ctx, st.player_proto, "setViewTarget", PlayerSetViewTarget, 1);
		DefFunc(ctx, st.player_proto, "setViewportSize", PlayerSetViewportSize, 4);
		DefFunc(ctx, st.player_proto, "resetViewportSize", PlayerResetViewportSize, 0);
		DefFunc(ctx, st.player_proto, "createWidget", PlayerCreateWidget, 2);
		DefFunc(ctx, st.player_proto, "toString", PlayerToString, 0);
		JS_SetClassProto(ctx, g_player_class, JS_DupValue(ctx, st.player_proto));

		// ---- Engine.* (objet cree par ScriptSystem) ---------------------------
		JSValue engine = JS_GetPropertyStr(ctx, global, "Engine");
		if (JS_IsObject(engine))
		{
			DefFunc(ctx, engine, "createPlayer", EngineCreatePlayer, 0);
			DefFunc(ctx, engine, "destroyPlayer", EngineDestroyPlayer, 1);
			DefFunc(ctx, engine, "getPlayer", EngineGetPlayer, 1);
			DefGet(ctx, engine, "players", EngineGet, kEPlayers);
			DefGet(ctx, engine, "playerCount", EngineGet, kEPlayerCount);
			DefGet(ctx, engine, "defaultPlayer", EngineGet, kEDefaultPlayer);
			DefGet(ctx, engine, "renderSize", EngineGet, kERenderSize);
		}
		JS_FreeValue(ctx, engine);

		// ---- Widget (prototype commun) ------------------------------------------
		st.widget_proto = JS_NewObject(ctx);
		JSValue wp = st.widget_proto;
		DefGetSet(ctx, wp, "name", WidgetGet, WidgetSet, kWName);
		DefGet(ctx, wp, "className", WidgetGet, kWClassName);
		DefGet(ctx, wp, "valid", WidgetGet, kWValid);
		DefGet(ctx, wp, "parent", WidgetGet, kWParent);
		DefGet(ctx, wp, "slot", WidgetGet, kWSlot);
		DefGet(ctx, wp, "children", WidgetGet, kWChildren);
		DefGet(ctx, wp, "childCount", WidgetGet, kWChildCount);
		DefGet(ctx, wp, "userWidget", WidgetGet, kWUserWidget);
		DefGetSet(ctx, wp, "visible", WidgetGet, WidgetSet, kWVisible);
		DefGet(ctx, wp, "hovered", WidgetGet, kWHovered);
		DefGet(ctx, wp, "geometry", WidgetGet, kWGeometry);
		DefGet(ctx, wp, "desiredSize", WidgetGet, kWDesiredSize);
		DefGet(ctx, wp, "inViewport", WidgetGet, kWInViewport);
		DefGetSet(ctx, wp, "owningPlayer", WidgetGet, WidgetSet, kWOwningPlayer);
		DefGet(ctx, wp, "root", WidgetGet, kWRoot);
		DefGet(ctx, wp, "widgetClass", WidgetGet, kWWidgetClass);
		DefGet(ctx, wp, "assetPath", WidgetGet, kWAssetPath);

		DefFunc(ctx, wp, "removeFromParent", WidgetRemoveFromParent, 0);
		DefFunc(ctx, wp, "removeFromViewport", WidgetRemoveFromParent, 0);
		DefFunc(ctx, wp, "destroy", WidgetDestroy, 0);
		DefFunc(ctx, wp, "find", WidgetFind, 1);
		DefFunc(ctx, wp, "getChildAt", WidgetGetChildAt, 1);
		DefFuncMagic(ctx, wp, "addChild", WidgetAddChild, 2, 0);
		DefFuncMagic(ctx, wp, "insertChildAt", WidgetAddChild, 3, 1);
		DefFunc(ctx, wp, "removeChild", WidgetRemoveChild, 1);
		DefFunc(ctx, wp, "clearChildren", WidgetClearChildren, 0);
		DefFunc(ctx, wp, "set", WidgetSetOptions, 1);
		DefFunc(ctx, wp, "invalidate", WidgetInvalidate, 0);
		DefFunc(ctx, wp, "addToViewport", WidgetAddToViewport, 1);
		DefFunc(ctx, wp, "on", WidgetOn, 2);
		DefFunc(ctx, wp, "off", WidgetOff, 2);
		DefFuncMagic(ctx, wp, "onClicked", WidgetOnMagic, 1, kEvClicked);
		DefFuncMagic(ctx, wp, "onPressed", WidgetOnMagic, 1, kEvPressed);
		DefFuncMagic(ctx, wp, "onReleased", WidgetOnMagic, 1, kEvReleased);
		DefFuncMagic(ctx, wp, "onHovered", WidgetOnMagic, 1, kEvHovered);
		DefFuncMagic(ctx, wp, "onUnhovered", WidgetOnMagic, 1, kEvUnhovered);
		DefFuncMagic(ctx, wp, "onValueChanged", WidgetOnMagic, 1, kEvValueChanged);
		DefFuncMagic(ctx, wp, "onCheckStateChanged", WidgetOnMagic, 1, kEvCheckChanged);
		DefFunc(ctx, wp, "toString", WidgetToString, 0);
		JS_SetClassProto(ctx, g_widget_class, JS_DupValue(ctx, wp));

		// ---- UserWidget (base des classes JS) -------------------------------
		st.user_widget_proto = JS_NewObjectProto(ctx, wp);
		st.user_widget_ctor = JS_NewCFunction2(ctx, UserWidgetConstructor, "UserWidget", 0, JS_CFUNC_constructor, 0);
		JS_SetConstructor(ctx, st.user_widget_ctor, st.user_widget_proto);
		JS_SetPropertyStr(ctx, global, "UserWidget", JS_DupValue(ctx, st.user_widget_ctor));

		// ---- Slot -------------------------------------------------------------
		st.slot_proto = JS_NewObject(ctx);
		DefGet(ctx, st.slot_proto, "widget", SlotGet, 0);
		DefGet(ctx, st.slot_proto, "type", SlotGet, 1);
		DefGet(ctx, st.slot_proto, "valid", SlotGet, 2);
		DefFuncMagic(ctx, st.slot_proto, "setPosition", SlotCanvasOp, 2, 0);
		DefFuncMagic(ctx, st.slot_proto, "setSize", SlotCanvasOp, 2, 1);
		DefFuncMagic(ctx, st.slot_proto, "setAnchors", SlotCanvasOp, 4, 2);
		DefFuncMagic(ctx, st.slot_proto, "setAlignment", SlotCanvasOp, 2, 3);
		JS_SetClassProto(ctx, g_slot_class, JS_DupValue(ctx, st.slot_proto));

		// ---- UI ---------------------------------------------------------------
		JSValue ui_obj = JS_NewObject(ctx);
		DefFunc(ctx, ui_obj, "create", UICreate, 3);
		DefFunc(ctx, ui_obj, "destroy", UIDestroy, 1);
		DefFunc(ctx, ui_obj, "all", UIAll, 0);
		DefFunc(ctx, ui_obj, "classes", UIClasses, 0);
		DefFunc(ctx, ui_obj, "getDPIReference", UIGetDPIReference, 0);
		DefFunc(ctx, ui_obj, "setDPIReference", UISetDPIReference, 1);
		JS_SetPropertyStr(ctx, global, "UI", ui_obj);

		JS_FreeValue(ctx, global);
	}

	void ShutdownGameplayBindings(JSContext* ctx)
	{
		if (!s)
			return;

		for (WidgetRef* r : LiveRefs())
		{
			if (r->alive)
			{
				JS_FreeValue(ctx, r->fn);
				r->alive = false;
			}
		}

		for (auto& [id, obj] : s->players)
			JS_FreeValue(ctx, obj);
		for (auto& [id, obj] : s->widgets)
			JS_FreeValue(ctx, obj);
		for (auto& [name, cls] : s->classes)
			JS_FreeValue(ctx, cls.ctor);

		JS_FreeValue(ctx, s->player_proto);
		JS_FreeValue(ctx, s->widget_proto);
		JS_FreeValue(ctx, s->user_widget_proto);
		JS_FreeValue(ctx, s->user_widget_ctor);
		JS_FreeValue(ctx, s->slot_proto);

		delete s;
		s = nullptr;
	}

	void ForgetWidgetClasses(JSContext* ctx)
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
		// Les constructeurs restent jusqu'au nouvel enregistrement (widgets vivants).
	}

	bool IsWidgetClassConstructor(JSContext* ctx, JSValueConst value)
	{
		if (!s || !JS_IsFunction(ctx, value))
			return false;

		JSValue proto = JS_GetPropertyStr(ctx, value, "prototype");
		bool found = false;
		JSValue p = JS_DupValue(ctx, proto);
		for (int guard = 0; guard < 64 && JS_IsObject(p) && !found; ++guard)
		{
			JSValue next = JS_GetPrototype(ctx, p);
			found = JS_IsObject(next) && JS_VALUE_GET_PTR(next) == JS_VALUE_GET_PTR(s->user_widget_proto);
			JS_FreeValue(ctx, p);
			p = next;
		}
		JS_FreeValue(ctx, p);
		JS_FreeValue(ctx, proto);
		return found;
	}

	void RegisterWidgetClass(JSContext* ctx, const std::string& name, JSValueConst ctor, const std::string& file)
	{
		State& st = S();
		auto& cls = st.classes[name];
		JS_FreeValue(ctx, cls.ctor);
		cls.ctor = JS_DupValue(ctx, ctor);
		cls.file = file;

		JSValue global = JS_GetGlobalObject(ctx);
		JS_SetPropertyStr(ctx, global, name.c_str(), JS_DupValue(ctx, ctor));
		JS_FreeValue(ctx, global);

		// Rechargement : les widgets de cette classe prennent les nouvelles methodes.
		JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");
		for (const auto& [id, bound_name] : st.bound)
		{
			auto it = st.widgets.find(id);
			if (bound_name == name && it != st.widgets.end())
				JS_SetPrototype(ctx, it->second, proto);
		}
		JS_FreeValue(ctx, proto);

		std::cout << kLogPrefix << "widget class " << name << " (" << file << ")" << std::endl;
	}
}


// ============================================================================
// Interface moteur (Private/ScriptSystem.h)
// ============================================================================

namespace lynx::scripting
{
	using namespace script_detail;

	bool CallPlayerEvent(Actor* self, const char* function, PlayerController* player)
	{
		JSContext* ctx = Context();
		if (!ctx || !self || !function)
			return false;

		JSValue arg = PlayerObject(ctx, player);
		bool found = false;
		JSValue r = CallActorFunction(ctx, self, function, 1, &arg, &found);
		JS_FreeValue(ctx, r);
		JS_FreeValue(ctx, arg);
		RunPendingJobs();
		return found;
	}

	void OnPlayerDestroyed(PlayerController* player)
	{
		JSContext* ctx = Context();
		if (!ctx || !player || !s)
			return;
		auto it = s->players.find(player->GetId());
		if (it == s->players.end())
			return;
		JSValue obj = it->second;
		s->players.erase(it);
		JS_FreeValue(ctx, obj);
	}

	void OnWidgetDestroyed(uint64_t widget_id)
	{
		JSContext* ctx = Context();
		if (!ctx || !s)
			return;
		s->bound.erase(widget_id);
		auto it = s->widgets.find(widget_id);
		if (it == s->widgets.end())
			return;
		JSValue obj = it->second;
		s->widgets.erase(it);
		JS_FreeValue(ctx, obj);
	}

	bool IsWidgetClass(const std::string& name)
	{
		return s && s->classes.count(name) > 0;
	}

	void OnUserWidgetEvent(UserWidget& widget, WidgetEvent event, float dt)
	{
		JSContext* ctx = Context();
		if (!ctx || !s)
			return;

		if (event == WidgetEvent::Construct)
		{
			const std::string& cls = widget.GetClassName();
			auto bound = s->bound.find(widget.GetUniqueId());
			if (!cls.empty() && s->classes.count(cls) && (bound == s->bound.end() || bound->second != cls))
				BindClass(ctx, widget, cls);
			CallWidgetMethod(ctx, widget, "Construct", 0, nullptr);
		}
		else if (event == WidgetEvent::Tick)
		{
			// Pas d'objet JS : rien a appeler (le cas de la plupart des widgets).
			if (!s->widgets.count(widget.GetUniqueId()))
				return;
			JSValue arg = JS_NewFloat64(ctx, dt);
			CallWidgetMethod(ctx, widget, "Tick", 1, &arg);
		}
		else
		{
			CallWidgetMethod(ctx, widget, "Destruct", 0, nullptr);
		}
	}
}
