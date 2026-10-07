// JS : Voxels (destruction / construction / reading) and Physics (traces,
// overlaps). See src/core/VoxelEdit.h, src/gameplay/PhysicsQueries.h and
// src/scripting/README.md.

#include "Private/ScriptInternal.h"

#include "../core/VoxelEdit.h"
#include "../core/Voxels.h"
#include "../gameplay/Actor.h"
#include "../gameplay/PhysicsQueries.h"

#include <cmath>
#include <string>

namespace lynx::script_detail
{
	namespace
	{
		// ---- Reading the arguments -------------------------------------------

		// {x, y} / [x, y] / an Actor (its location).
		bool ReadPoint(JSContext* ctx, JSValueConst v, vec2& out)
		{
			if (Actor* a = ActorFromJS(v))
			{
				out = vec2(a->transform.location.x, a->transform.location.y);
				return true;
			}
			float f[2];
			if (!ReadFloats(ctx, v, f, 2))
				return false;
			out = vec2(f[0], f[1]);
			return true;
		}

		// A number : radius / thickness, or {x, y} : size.
		bool ReadSize(JSContext* ctx, JSValueConst v, vec2& out)
		{
			if (JS_IsNumber(v))
			{
				double d = 0.0;
				JS_ToFloat64(ctx, &d, v);
				out = vec2(static_cast<float>(d), static_cast<float>(d));
				return true;
			}
			return ReadPoint(ctx, v, out);
		}

		float ReadNumber(JSContext* ctx, JSValueConst v, float fallback)
		{
			double d = 0.0;
			if (JS_IsUndefined(v) || JS_IsNull(v) || JS_ToFloat64(ctx, &d, v) != 0)
				return fallback;
			return static_cast<float>(d);
		}

		// Type : id (number) or name ("Stone"). 0 if unknown.
		uint8_t ReadType(JSContext* ctx, JSValueConst v)
		{
			if (JS_IsNumber(v))
			{
				int32_t i = 0;
				JS_ToInt32(ctx, &i, v);
				return (i > 0 && i < 256) ? static_cast<uint8_t>(i) : 0;
			}
			if (JS_IsString(v))
				return voxels::FindType(ToStdString(ctx, v).c_str());
			return 0;
		}

		// Flags : number, "NAME" or ["A", "B"].
		uint32_t ReadFlags(JSContext* ctx, JSValueConst v)
		{
			if (JS_IsNumber(v))
			{
				uint32_t u = 0;
				JS_ToUint32(ctx, &u, v);
				return u;
			}
			if (JS_IsString(v))
				return voxels::GetFlagMask(ToStdString(ctx, v).c_str());
			uint32_t mask = 0u;
			if (JS_IsArray(v))
			{
				JSValue len_v = JS_GetPropertyStr(ctx, v, "length");
				uint32_t len = 0;
				JS_ToUint32(ctx, &len, len_v);
				JS_FreeValue(ctx, len_v);
				for (uint32_t i = 0; i < len; ++i)
				{
					JSValue item = JS_GetPropertyUint32(ctx, v, i);
					mask |= ReadFlags(ctx, item);
					JS_FreeValue(ctx, item);
				}
			}
			return mask;
		}

		// A type or a list of types.
		void ReadTypes(JSContext* ctx, JSValueConst v, std::vector<uint8_t>& out)
		{
			if (JS_IsArray(v))
			{
				JSValue len_v = JS_GetPropertyStr(ctx, v, "length");
				uint32_t len = 0;
				JS_ToUint32(ctx, &len, len_v);
				JS_FreeValue(ctx, len_v);
				for (uint32_t i = 0; i < len; ++i)
				{
					JSValue item = JS_GetPropertyUint32(ctx, v, i);
					if (const uint8_t t = ReadType(ctx, item))
						out.push_back(t);
					JS_FreeValue(ctx, item);
				}
			}
			else if (!JS_IsUndefined(v) && !JS_IsNull(v))
			{
				if (const uint8_t t = ReadType(ctx, v))
					out.push_back(t);
			}
		}

		// opts.<name> (undefined if no opts / no field ; to free).
		JSValue Opt(JSContext* ctx, JSValueConst opts, const char* name)
		{
			if (!JS_IsObject(opts))
				return JS_UNDEFINED;
			return JS_GetPropertyStr(ctx, opts, name);
		}

		bool OptBool(JSContext* ctx, JSValueConst opts, const char* name, bool fallback)
		{
			JSValue v = Opt(ctx, opts, name);
			const bool r = JS_IsUndefined(v) ? fallback : JS_ToBool(ctx, v) > 0;
			JS_FreeValue(ctx, v);
			return r;
		}

		// { types, ignoreTypes, flags, includeIndestructible, events, cells }
		voxels::VoxelFilter ReadFilter(JSContext* ctx, JSValueConst opts)
		{
			voxels::VoxelFilter f;
			if (!JS_IsObject(opts))
				return f;

			JSValue v = Opt(ctx, opts, "types");
			ReadTypes(ctx, v, f.types);
			JS_FreeValue(ctx, v);

			v = Opt(ctx, opts, "ignoreTypes");
			ReadTypes(ctx, v, f.ignore_types);
			JS_FreeValue(ctx, v);

			v = Opt(ctx, opts, "flags");
			if (!JS_IsUndefined(v))
				f.flags = ReadFlags(ctx, v);
			JS_FreeValue(ctx, v);

			f.include_indestructible = OptBool(ctx, opts, "includeIndestructible", false);
			f.fire_events = OptBool(ctx, opts, "events", true);
			f.keep_cells = OptBool(ctx, opts, "cells", true);
			return f;
		}

		voxels::VoxelFillOptions ReadFill(JSContext* ctx, JSValueConst opts)
		{
			voxels::VoxelFillOptions o;
			o.filter = ReadFilter(ctx, opts);
			o.replace = OptBool(ctx, opts, "replace", false);
			return o;
		}

		// ---- Results -----------------------------------------------------------

		JSValue Vec(JSContext* ctx, const vec2& v)
		{
			const float f[2] = { v.x, v.y };
			return NewPlainVec(ctx, f, 2);
		}

		const char* TypeName(uint8_t type)
		{
			if (type == 0)
				return "Empty";
			const voxels::VoxelType* t = voxels::GetType(type);
			return t ? t->name.c_str() : "";
		}

		/**
		 * { count, any, cells: [{x, y, type, typeName, world}], types: {Name: n},
		 *   perType: [{type, typeName, count}], min, max, center }
		 */
		JSValue EditResult(JSContext* ctx, const voxels::VoxelEditResult& r)
		{
			JSValue o = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, o, "count", JS_NewInt32(ctx, r.count));
			JS_SetPropertyStr(ctx, o, "any", JS_NewBool(ctx, r.count > 0));

			JSValue cells = JS_NewArray(ctx);
			uint32_t i = 0;
			for (const voxels::VoxelCell& c : r.cells)
			{
				JSValue cell = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, cell, "x", JS_NewInt32(ctx, c.x));
				JS_SetPropertyStr(ctx, cell, "y", JS_NewInt32(ctx, c.y));
				JS_SetPropertyStr(ctx, cell, "type", JS_NewInt32(ctx, c.type));
				JS_SetPropertyStr(ctx, cell, "typeName", JS_NewString(ctx, TypeName(c.type)));
				JS_SetPropertyStr(ctx, cell, "world", Vec(ctx, c.world));
				JS_SetPropertyUint32(ctx, cells, i++, cell);
			}
			JS_SetPropertyStr(ctx, o, "cells", cells);

			JSValue types = JS_NewObject(ctx);
			JSValue per_type = JS_NewArray(ctx);
			i = 0;
			for (const auto& [type, n] : r.per_type)
			{
				JS_SetPropertyStr(ctx, types, TypeName(type), JS_NewInt32(ctx, n));
				JSValue e = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, e, "type", JS_NewInt32(ctx, type));
				JS_SetPropertyStr(ctx, e, "typeName", JS_NewString(ctx, TypeName(type)));
				JS_SetPropertyStr(ctx, e, "count", JS_NewInt32(ctx, n));
				JS_SetPropertyUint32(ctx, per_type, i++, e);
			}
			JS_SetPropertyStr(ctx, o, "types", types);
			JS_SetPropertyStr(ctx, o, "perType", per_type);

			if (r.count > 0)
			{
				JS_SetPropertyStr(ctx, o, "min", Vec(ctx, r.bounds_min));
				JS_SetPropertyStr(ctx, o, "max", Vec(ctx, r.bounds_max));
				JS_SetPropertyStr(ctx, o, "center", Vec(ctx, r.Center()));
			}
			return o;
		}

		// ---- Voxels.* --------------------------------------------------------------

		enum VoxelOp
		{
			kDestroyCircle, kDestroyRect, kDestroyLine, kDestroyCell, kDestroyAt,
			kFillCircle, kFillRect, kFillLine, kFillCell, kFillAt,
			kCountCircle, kCountRect,
			kTypeAt, kTypeAtCell, kTypeId, kTypeName, kWorldToCell, kCellToWorld,
		};

		JSValue Arg(int argc, JSValueConst* argv, int i)
		{
			return i < argc ? argv[i] : JS_UNDEFINED;
		}

		JSValue VoxelsOp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic)
		{
			vec2 a, b;
			auto point = [&](int i, vec2& out, const char* usage) -> bool
			{
				if (ReadPoint(ctx, Arg(argc, argv, i), out))
					return true;
				JS_ThrowTypeError(ctx, "%s", usage);
				return false;
			};
			auto cell = [&](int i, int& x, int& y) -> bool
			{
				int32_t xi = 0, yi = 0;
				if (argc <= i + 1 || JS_ToInt32(ctx, &xi, argv[i]) != 0 || JS_ToInt32(ctx, &yi, argv[i + 1]) != 0)
				{
					JS_ThrowTypeError(ctx, "voxel coordinates (x, y) expected");
					return false;
				}
				x = xi;
				y = yi;
				return true;
			};
			auto type_arg = [&](int i) -> int
			{
				const uint8_t t = ReadType(ctx, Arg(argc, argv, i));
				if (t == 0)
					JS_ThrowTypeError(ctx, "unknown voxel type (name of voxels.json or id > 0)");
				return t;
			};

			switch (magic)
			{
			// ---- Destruction
			case kDestroyCircle:
				if (!point(0, a, "Voxels.destroyCircle(center, radius, opts)")) return JS_EXCEPTION;
				return EditResult(ctx, voxels::DestroyCircle(a, ReadNumber(ctx, Arg(argc, argv, 1), 1.f), ReadFilter(ctx, Arg(argc, argv, 2))));
			case kDestroyRect:
				if (!point(0, a, "Voxels.destroyRect(center, size, opts)") || !ReadSize(ctx, Arg(argc, argv, 1), b))
					return JS_ThrowTypeError(ctx, "Voxels.destroyRect(center, size, opts)");
				return EditResult(ctx, voxels::DestroyRect(a, b, ReadFilter(ctx, Arg(argc, argv, 2))));
			case kDestroyLine:
				if (!point(0, a, "Voxels.destroyLine(from, to, thickness, opts)") ||
				    !point(1, b, "Voxels.destroyLine(from, to, thickness, opts)"))
					return JS_EXCEPTION;
				return EditResult(ctx, voxels::DestroyLine(a, b, ReadNumber(ctx, Arg(argc, argv, 2), 1.f), ReadFilter(ctx, Arg(argc, argv, 3))));
			case kDestroyCell:
			{
				int x, y;
				if (!cell(0, x, y)) return JS_EXCEPTION;
				return EditResult(ctx, voxels::DestroyCell(x, y, ReadFilter(ctx, Arg(argc, argv, 2))));
			}
			case kDestroyAt:
				if (!point(0, a, "Voxels.destroyAt(point, opts)")) return JS_EXCEPTION;
				return EditResult(ctx, voxels::DestroyAt(a, ReadFilter(ctx, Arg(argc, argv, 1))));

			// ---- Construction
			case kFillCircle:
			{
				if (!point(0, a, "Voxels.fillCircle(center, radius, type, opts)")) return JS_EXCEPTION;
				const int t = type_arg(2);
				if (!t) return JS_EXCEPTION;
				return EditResult(ctx, voxels::FillCircle(a, ReadNumber(ctx, Arg(argc, argv, 1), 1.f), static_cast<uint8_t>(t), ReadFill(ctx, Arg(argc, argv, 3))));
			}
			case kFillRect:
			{
				if (!point(0, a, "Voxels.fillRect(center, size, type, opts)") || !ReadSize(ctx, Arg(argc, argv, 1), b))
					return JS_ThrowTypeError(ctx, "Voxels.fillRect(center, size, type, opts)");
				const int t = type_arg(2);
				if (!t) return JS_EXCEPTION;
				return EditResult(ctx, voxels::FillRect(a, b, static_cast<uint8_t>(t), ReadFill(ctx, Arg(argc, argv, 3))));
			}
			case kFillLine:
			{
				if (!point(0, a, "Voxels.fillLine(from, to, thickness, type, opts)") ||
				    !point(1, b, "Voxels.fillLine(from, to, thickness, type, opts)"))
					return JS_EXCEPTION;
				const int t = type_arg(3);
				if (!t) return JS_EXCEPTION;
				return EditResult(ctx, voxels::FillLine(a, b, ReadNumber(ctx, Arg(argc, argv, 2), 1.f), static_cast<uint8_t>(t), ReadFill(ctx, Arg(argc, argv, 4))));
			}
			case kFillCell:
			{
				int x, y;
				if (!cell(0, x, y)) return JS_EXCEPTION;
				const int t = type_arg(2);
				if (!t) return JS_EXCEPTION;
				return EditResult(ctx, voxels::FillCell(x, y, static_cast<uint8_t>(t), ReadFill(ctx, Arg(argc, argv, 3))));
			}
			case kFillAt:
			{
				if (!point(0, a, "Voxels.fillAt(point, type, opts)")) return JS_EXCEPTION;
				const int t = type_arg(1);
				if (!t) return JS_EXCEPTION;
				return EditResult(ctx, voxels::FillAt(a, static_cast<uint8_t>(t), ReadFill(ctx, Arg(argc, argv, 2))));
			}

			// ---- Reading
			case kCountCircle:
				if (!point(0, a, "Voxels.countCircle(center, radius, opts)")) return JS_EXCEPTION;
				return EditResult(ctx, voxels::CountCircle(a, ReadNumber(ctx, Arg(argc, argv, 1), 1.f), ReadFilter(ctx, Arg(argc, argv, 2))));
			case kCountRect:
				if (!point(0, a, "Voxels.countRect(center, size, opts)") || !ReadSize(ctx, Arg(argc, argv, 1), b))
					return JS_ThrowTypeError(ctx, "Voxels.countRect(center, size, opts)");
				return EditResult(ctx, voxels::CountRect(a, b, ReadFilter(ctx, Arg(argc, argv, 2))));
			case kTypeAt:
				if (!point(0, a, "Voxels.typeAt(point)")) return JS_EXCEPTION;
				return JS_NewInt32(ctx, voxels::GetTypeAtWorld(a));
			case kTypeAtCell:
			{
				int x, y;
				if (!cell(0, x, y)) return JS_EXCEPTION;
				return JS_NewInt32(ctx, voxels::GetTypeAt(x, y));
			}
			case kTypeId:
				return JS_NewInt32(ctx, ReadType(ctx, Arg(argc, argv, 0)));
			case kTypeName:
			{
				const uint8_t t = ReadType(ctx, Arg(argc, argv, 0));
				return JS_NewString(ctx, TypeName(t));
			}
			case kWorldToCell:
			{
				if (!point(0, a, "Voxels.worldToCell(point)")) return JS_EXCEPTION;
				int x = 0, y = 0;
				if (!voxels::WorldToCell(a, x, y))
					return JS_NULL;
				JSValue o = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, o, "x", JS_NewInt32(ctx, x));
				JS_SetPropertyStr(ctx, o, "y", JS_NewInt32(ctx, y));
				return o;
			}
			case kCellToWorld:
			{
				int x, y;
				if (!cell(0, x, y)) return JS_EXCEPTION;
				return Vec(ctx, voxels::CellToWorld(x, y));
			}
			}
			return JS_UNDEFINED;
		}

		// ---- Physics.* -------------------------------------------------------------

		// { actors, voxels, triggers, layers, voxelFlags, types, ignoreTypes,
		//   ignore, debug, duration, cells }
		physics::QueryParams ReadParams(JSContext* ctx, JSValueConst opts)
		{
			physics::QueryParams p;
			if (!JS_IsObject(opts))
				return p;

			p.actors = OptBool(ctx, opts, "actors", true);
			p.voxels = OptBool(ctx, opts, "voxels", true);
			p.triggers = OptBool(ctx, opts, "triggers", false);
			p.debug_draw = OptBool(ctx, opts, "debug", false);
			p.voxel_filter = ReadFilter(ctx, opts);
			// The filter's "flags" are the voxel flags of the query.
			p.voxel_flags = p.voxel_filter.flags;
			p.voxel_filter.flags = 0u;

			JSValue v = Opt(ctx, opts, "voxelFlags");
			if (!JS_IsUndefined(v))
				p.voxel_flags = ReadFlags(ctx, v);
			JS_FreeValue(ctx, v);

			v = Opt(ctx, opts, "layers");
			if (!JS_IsUndefined(v))
				JS_ToUint32(ctx, &p.layer_mask, v);
			JS_FreeValue(ctx, v);

			v = Opt(ctx, opts, "duration");
			p.debug_duration = ReadNumber(ctx, v, 0.f);
			JS_FreeValue(ctx, v);
			if (p.debug_duration > 0.f)
				p.debug_draw = true;

			v = Opt(ctx, opts, "ignore");
			if (JS_IsArray(v))
			{
				JSValue len_v = JS_GetPropertyStr(ctx, v, "length");
				uint32_t len = 0;
				JS_ToUint32(ctx, &len, len_v);
				JS_FreeValue(ctx, len_v);
				for (uint32_t i = 0; i < len; ++i)
				{
					JSValue item = JS_GetPropertyUint32(ctx, v, i);
					if (Actor* a = ActorFromJS(item))
						p.ignore.push_back(a);
					JS_FreeValue(ctx, item);
				}
			}
			else if (Actor* a = ActorFromJS(v))
			{
				p.ignore.push_back(a);
			}
			JS_FreeValue(ctx, v);
			return p;
		}

		/** { point, normal, distance, fraction, initialOverlap, actor, voxel: {x, y, type, typeName} | null } */
		JSValue HitObject(JSContext* ctx, const physics::HitResult& h)
		{
			JSValue o = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, o, "point", Vec(ctx, h.point));
			JS_SetPropertyStr(ctx, o, "normal", Vec(ctx, h.normal));
			JS_SetPropertyStr(ctx, o, "distance", JS_NewFloat64(ctx, h.distance));
			JS_SetPropertyStr(ctx, o, "fraction", JS_NewFloat64(ctx, h.fraction));
			JS_SetPropertyStr(ctx, o, "initialOverlap", JS_NewBool(ctx, h.initial_overlap));
			JS_SetPropertyStr(ctx, o, "actor", ActorObject(ctx, h.actor));
			if (h.voxel)
			{
				JSValue vx = JS_NewObject(ctx);
				JS_SetPropertyStr(ctx, vx, "x", JS_NewInt32(ctx, h.voxel_x));
				JS_SetPropertyStr(ctx, vx, "y", JS_NewInt32(ctx, h.voxel_y));
				JS_SetPropertyStr(ctx, vx, "type", JS_NewInt32(ctx, h.voxel_type));
				JS_SetPropertyStr(ctx, vx, "typeName", JS_NewString(ctx, TypeName(h.voxel_type)));
				JS_SetPropertyStr(ctx, o, "voxel", vx);
			}
			else
			{
				JS_SetPropertyStr(ctx, o, "voxel", JS_NULL);
			}
			return o;
		}

		JSValue OverlapObject(JSContext* ctx, const physics::OverlapResult& r)
		{
			JSValue o = JS_NewObject(ctx);
			JSValue actors = JS_NewArray(ctx);
			uint32_t i = 0;
			for (Actor* a : r.actors)
				JS_SetPropertyUint32(ctx, actors, i++, ActorObject(ctx, a));
			JS_SetPropertyStr(ctx, o, "actors", actors);
			JS_SetPropertyStr(ctx, o, "voxels", EditResult(ctx, r.voxels));
			JS_SetPropertyStr(ctx, o, "any", JS_NewBool(ctx, r.Any()));
			return o;
		}

		enum PhysicsOp { kRaycast, kRaycastAll, kLineOfSight, kOverlapBox, kOverlapCircle, kSetDebug, kIsDebug };

		JSValue PhysicsOpFn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic)
		{
			vec2 a, b;
			switch (magic)
			{
			case kRaycast:
			case kRaycastAll:
			case kLineOfSight:
			{
				if (!ReadPoint(ctx, Arg(argc, argv, 0), a) || !ReadPoint(ctx, Arg(argc, argv, 1), b))
					return JS_ThrowTypeError(ctx, "Physics.raycast(from, to, opts)");
				const physics::QueryParams p = ReadParams(ctx, Arg(argc, argv, 2));
				if (magic == kLineOfSight)
					return JS_NewBool(ctx, physics::LineOfSight(a, b, p));
				if (magic == kRaycast)
				{
					const physics::HitResult h = physics::Raycast(a, b, p);
					return h.hit ? HitObject(ctx, h) : JS_NULL;
				}
				JSValue arr = JS_NewArray(ctx);
				uint32_t i = 0;
				for (const physics::HitResult& h : physics::RaycastAll(a, b, p))
					JS_SetPropertyUint32(ctx, arr, i++, HitObject(ctx, h));
				return arr;
			}
			case kOverlapBox:
				if (!ReadPoint(ctx, Arg(argc, argv, 0), a) || !ReadSize(ctx, Arg(argc, argv, 1), b))
					return JS_ThrowTypeError(ctx, "Physics.overlapBox(center, size, opts)");
				return OverlapObject(ctx, physics::OverlapBox(a, b, ReadParams(ctx, Arg(argc, argv, 2))));
			case kOverlapCircle:
				if (!ReadPoint(ctx, Arg(argc, argv, 0), a))
					return JS_ThrowTypeError(ctx, "Physics.overlapCircle(center, radius, opts)");
				return OverlapObject(ctx, physics::OverlapCircle(a, ReadNumber(ctx, Arg(argc, argv, 1), 1.f), ReadParams(ctx, Arg(argc, argv, 2))));
			case kSetDebug:
				physics::SetDebugDrawAll(argc > 0 ? JS_ToBool(ctx, argv[0]) > 0 : true);
				return JS_UNDEFINED;
			case kIsDebug:
				return JS_NewBool(ctx, physics::IsDebugDrawAll());
			}
			return JS_UNDEFINED;
		}
	}


	void RegisterQueryGlobals(JSContext* ctx)
	{
		JSValue global = JS_GetGlobalObject(ctx);

		JSValue vox = JS_NewObject(ctx);
		DefFuncMagic(ctx, vox, "destroyCircle", VoxelsOp, 3, kDestroyCircle);
		DefFuncMagic(ctx, vox, "destroyRect", VoxelsOp, 3, kDestroyRect);
		DefFuncMagic(ctx, vox, "destroyLine", VoxelsOp, 4, kDestroyLine);
		DefFuncMagic(ctx, vox, "destroyCell", VoxelsOp, 3, kDestroyCell);
		DefFuncMagic(ctx, vox, "destroyAt", VoxelsOp, 2, kDestroyAt);
		DefFuncMagic(ctx, vox, "fillCircle", VoxelsOp, 4, kFillCircle);
		DefFuncMagic(ctx, vox, "fillRect", VoxelsOp, 4, kFillRect);
		DefFuncMagic(ctx, vox, "fillLine", VoxelsOp, 5, kFillLine);
		DefFuncMagic(ctx, vox, "fillCell", VoxelsOp, 4, kFillCell);
		DefFuncMagic(ctx, vox, "fillAt", VoxelsOp, 3, kFillAt);
		DefFuncMagic(ctx, vox, "countCircle", VoxelsOp, 3, kCountCircle);
		DefFuncMagic(ctx, vox, "countRect", VoxelsOp, 3, kCountRect);
		DefFuncMagic(ctx, vox, "typeAt", VoxelsOp, 1, kTypeAt);
		DefFuncMagic(ctx, vox, "typeAtCell", VoxelsOp, 2, kTypeAtCell);
		DefFuncMagic(ctx, vox, "typeId", VoxelsOp, 1, kTypeId);
		DefFuncMagic(ctx, vox, "typeName", VoxelsOp, 1, kTypeName);
		DefFuncMagic(ctx, vox, "worldToCell", VoxelsOp, 1, kWorldToCell);
		DefFuncMagic(ctx, vox, "cellToWorld", VoxelsOp, 2, kCellToWorld);
		JS_SetPropertyStr(ctx, global, "Voxels", vox);

		JSValue phys = JS_NewObject(ctx);
		DefFuncMagic(ctx, phys, "raycast", PhysicsOpFn, 3, kRaycast);
		DefFuncMagic(ctx, phys, "raycastAll", PhysicsOpFn, 3, kRaycastAll);
		DefFuncMagic(ctx, phys, "lineOfSight", PhysicsOpFn, 3, kLineOfSight);
		DefFuncMagic(ctx, phys, "overlapBox", PhysicsOpFn, 3, kOverlapBox);
		DefFuncMagic(ctx, phys, "overlapCircle", PhysicsOpFn, 3, kOverlapCircle);
		DefFuncMagic(ctx, phys, "setDebugDraw", PhysicsOpFn, 1, kSetDebug);
		DefFuncMagic(ctx, phys, "isDebugDraw", PhysicsOpFn, 0, kIsDebug);
		JS_SetPropertyStr(ctx, global, "Physics", phys);

		JS_FreeValue(ctx, global);
	}
}
