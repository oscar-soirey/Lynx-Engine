#pragma once

// =============================================================================
// Voxel physics (Noita-like materials)
// -----------------------------------------------------------------------------
// While the game runs (Play, Simulate, shipped game), the voxels whose type
// has a behavior (voxels.json "physics", editor : Voxel types > Physics) move
// on the grid :
//   powder  falls, slides down in piles          (sand, gravel, snow)
//   liquid  falls, spreads sideways               (water, oil, lava, acid)
//   gas     rises, spreads                        (smoke, steam)
// A heavier material sinks in a lighter liquid / gas (density).
//
// Only the area around the cameras of the players (and the editor camera in
// Simulate) is simulated : radius SetRadius() voxels. The rest stays frozen
// until a camera gets close.
//
// Actors :
//   - the blocking colliders (ColliderComponent, not trigger) block the
//     moving voxels : sand piles up on them, water flows around them ;
//   - the solid voxels block the actors as before (collision of the type) ;
//   - inside a liquid / gas : drag and buoyancy (Humanoid, VelocityComponent) ;
//   - on a surface : friction (ice, mud) and bounciness (Humanoid) ;
//   - damage per second -> Damageable.TakeDamage(amount, null) ;
//   - contact_events -> VoxelEvents.OnVoxelContact(type_name, type) /
//     OnVoxelContactEnd(type_name, type) to the actors that implement it.
//
// The editor puts the voxel world back after Play / Simulate.
// JS : VoxelPhysics.setEnabled(b), setRadius(n), setRate(hz), addFocus(x, y).
// =============================================================================

#include <cstdint>

#include "Common.h"

namespace lynx
{
	class Actor;
}

namespace lynx::voxel_physics
{
	LYNX_API void SetEnabled(bool enabled);
	LYNX_API bool IsEnabled();

	/** Half size (voxels) of the simulated square around each camera. Default 96. */
	LYNX_API void SetRadius(int voxels);
	LYNX_API int GetRadius();

	/** Simulation steps per second. Default 30. */
	LYNX_API void SetRate(float steps_per_second);

	/**
	 * One more place to simulate around (world units), for the next step only
	 * (call it every frame). The editor gives its camera while simulating.
	 */
	LYNX_API void AddFocus(float world_x, float world_y);

	/** What surrounds an actor (its collider box, or its position). */
	struct Medium
	{
		/** 0..1 : part of the box inside a liquid / gas. */
		float fluid_fraction = 0.f;
		float drag = 0.f;
		float buoyancy = 0.f;
		/** Voxel type right below the box (0 : none) and its surface. */
		uint8_t ground_type = 0;
		float ground_friction = 1.f;
		float ground_bounciness = 0.f;
	};

	LYNX_API Medium GetMedium(const Actor* actor);

	// ---- Engine ---------------------------------------------------------------

	/** Game start / end : contacts forgotten, voxel cache emptied. */
	LYNX_API void Reset();

	/**
	 * The simulation keeps a copy of the voxels it reads (tiles of 32 x 32,
	 * re-read from HRL at least once per second). Whoever changes voxels
	 * while the game runs says so : the touched tiles are read again.
	 * (VoxelEdit does it for the game ; the editor brush in Simulate too.)
	 */
	LYNX_API void InvalidateVoxels(int x0, int y0, int x1, int y1);
	LYNX_API void InvalidateAllVoxels();

	/**
	 * Type of a voxel from that copy, when its tile is cached and fresh (the
	 * area simulated around the cameras) : voxels::GetTypeAt uses it while
	 * the game runs (an HRL call per voxel is ~100 ns). false : ask HRL.
	 */
	LYNX_API bool CachedType(int voxel_x, int voxel_y, uint8_t& type);

	/** Game end : the copy is dropped (the editor edits the world again). */
	LYNX_API void OnGameEnd();

	/** Every frame while the game runs (Engine::ProgressOneFrame). */
	LYNX_API void Tick(float dt);
}
