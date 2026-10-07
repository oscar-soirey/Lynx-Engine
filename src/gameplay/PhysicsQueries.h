#pragma once

// =============================================================================
// Physics queries : traces and overlaps (colliders + voxels), C++ and JS
// -----------------------------------------------------------------------------
//   using namespace lynx;
//
//   physics::QueryParams p;
//   p.ignore = { this };                   // do not hit myself
//   p.debug_draw = true;                   // draw the trace (1 frame)
//   physics::HitResult hit = physics::Raycast(eye, eye + dir * 30.f, p);
//   if (hit.hit && hit.actor) interfaces::Call(hit.actor, "Damageable", "TakeDamage", ...);
//   if (hit.hit && hit.voxel) voxels::DestroyCircle(hit.point, 2.f);
//
//   // Everything in a radius (actors + voxels) :
//   physics::OverlapResult o = physics::OverlapCircle(center, 5.f);
//   for (Actor* a : o.actors) ...
//
// What is tested (QueryParams) :
//   actors   : the ColliderComponent of the actors (triggers : params.triggers),
//              whose layer & params.layer_mask != 0 ;
//   voxels   : traces : the voxels whose flags & params.voxel_flags != 0 (0 :
//              the blocking voxels, like the colliders ; water is crossed) ;
//              overlaps : every non-empty voxel (0) or those flags ;
//              params.voxel_filter (types, ignore_types...) for both.
//
// Debug draw : params.debug_draw (and debug_duration, seconds), or every query
// with SetDebugDrawAll(true) (editor : F4 with the colliders).
// Green : nothing hit / the trace before the hit ; red : after the hit / overlap.
//
// JS : Physics.raycast(from, to, opts) / raycastAll / lineOfSight /
//      overlapBox(center, size, opts) / overlapCircle(center, radius, opts) /
//      setDebugDraw(b) (see src/scripting/README.md).
// =============================================================================

#include <cstdint>
#include <vector>

#include "../core/Common.h"
#include "../core/VoxelEdit.h"

namespace lynx
{
	class Actor;
	class ColliderComponent;
}

namespace lynx::physics
{
	struct QueryParams
	{
		/** Test the colliders of the actors. */
		bool actors = true;
		/** Test the voxels. */
		bool voxels = true;
		/** Also the trigger colliders (zones). */
		bool triggers = false;
		/** Only the colliders whose layer has one of these bits. */
		uint32_t layer_mask = 0xFFFFFFFFu;
		/** Voxels whose flags have one of these bits (0 : the blocking voxels). */
		uint32_t voxel_flags = 0u;
		/** Only some voxel types... (overlaps, keep_cells : the list of cells). */
		voxels::VoxelFilter voxel_filter;
		/** These actors are never hit (ex : the one that shoots). */
		std::vector<const Actor*> ignore;

		/** Draw the query. */
		bool debug_draw = false;
		/** Seconds the drawing stays (0 : one frame). */
		float debug_duration = 0.f;
	};

	struct HitResult
	{
		bool hit = false;
		/** The trace started inside what it hit (point = start, normal = -direction). */
		bool initial_overlap = false;
		vec2 point;
		/** Surface normal (unit vector toward the start of the trace). */
		vec2 normal;
		float distance = 0.f;
		/** 0..1 along the trace. */
		float fraction = 1.f;

		/** An actor was hit (its collider). */
		Actor* actor = nullptr;
		ColliderComponent* collider = nullptr;

		/** A voxel was hit. */
		bool voxel = false;
		int voxel_x = 0;
		int voxel_y = 0;
		uint8_t voxel_type = 0;
	};

	struct OverlapResult
	{
		std::vector<Actor*> actors;
		std::vector<ColliderComponent*> colliders;
		/** Voxels in the shape that pass the filter (cells, counts per type). */
		voxels::VoxelEditResult voxels;

		bool Any() const { return !actors.empty() || voxels.count > 0; }
	};

	/** First hit between `from` and `to`. */
	LYNX_API HitResult Raycast(const vec2& from, const vec2& to, const QueryParams& params = {});

	/**
	 * Every actor crossed until the first blocking hit (a voxel or a non-trigger
	 * collider ; included), sorted by distance.
	 */
	LYNX_API std::vector<HitResult> RaycastAll(const vec2& from, const vec2& to, const QueryParams& params = {});

	/** Nothing (that the params test) between the two points. */
	LYNX_API bool LineOfSight(const vec2& from, const vec2& to, const QueryParams& params = {});

	/** Actors and voxels in an axis-aligned box (center + full size). */
	LYNX_API OverlapResult OverlapBox(const vec2& center, const vec2& size, const QueryParams& params = {});

	/** Actors and voxels in a circle. */
	LYNX_API OverlapResult OverlapCircle(const vec2& center, float radius, const QueryParams& params = {});

	/** Draw every query (debug_draw on all of them). */
	LYNX_API void SetDebugDrawAll(bool enabled);
	LYNX_API bool IsDebugDrawAll();

	/** Engine, every frame : the debug drawings that last (debug_duration). */
	LYNX_API void TickDebug(float dt);
}
