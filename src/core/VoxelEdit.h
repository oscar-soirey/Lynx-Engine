#pragma once

// =============================================================================
// Voxel destruction / construction (game code, C++ and JS)
// -----------------------------------------------------------------------------
//   using namespace lynx;
//
//   // Explosion : every destructible voxel in a radius of 6 around the bomb.
//   voxels::VoxelEditResult r = voxels::DestroyCircle(bomb->transform.location, 6.f);
//   if (r.CountOf(voxels::FindType("Gold")) > 0) ...
//   for (const voxels::VoxelCell& c : r.cells) SpawnDebris(c.world, c.type);
//
//   // Only some types / flags :
//   voxels::VoxelFilter only_dirt;
//   only_dirt.types = { voxels::FindType("Dirt"), voxels::FindType("Grass") };
//   voxels::DestroyLine(from, to, 2.f, only_dirt);
//
//   // Construction : a wall of stone (only in the empty cells by default).
//   voxels::FillRect({10.f, 5.f}, {2.f, 8.f}, voxels::FindType("Stone"));
//
// JS : Voxels.destroyCircle(center, radius, opts) / destroyRect / destroyLine /
//      destroyCell, Voxels.fillCircle(center, radius, "Stone", opts) / fillRect /
//      fillLine / fillCell... (see src/scripting/README.md).
//
// Units : world (1 = 1 voxel by default). Shapes :
//   Circle : center + radius ;  Rect : center + full size (like the colliders) ;
//   Line   : from, to + thickness ;  Cell : voxel coordinates (integers).
// The cell that contains a point is always part of a circle / line around it.
//
// Destroy : never touches empty cells ; skips the indestructible types unless
// filter.include_indestructible ; calls the "on_destroyed" events of voxels.json
// (filter.fire_events). Fill : empty cells only, unless options.replace (then
// the cells that pass options.filter are replaced too).
// =============================================================================

#include <cstdint>
#include <utility>
#include <vector>

#include "Common.h"

namespace lynx::voxels
{
	/** One voxel touched by an edit. */
	struct VoxelCell
	{
		int x = 0;              // voxel coordinates
		int y = 0;
		uint8_t type = 0;       // type BEFORE the edit (0 : was empty)
		vec2 world;             // center of the cell, world units
	};

	/** Which voxels an edit (or a query) may touch. Default : every type. */
	struct VoxelFilter
	{
		/** Only these types (empty : every type). */
		std::vector<uint8_t> types;
		/** Never these types. */
		std::vector<uint8_t> ignore_types;
		/** Only the types with ANY of these flags (voxels::GetFlagMask ; 0 : any). */
		uint32_t flags = 0u;
		/** Destroy / replace the "indestructible" types too. */
		bool include_indestructible = false;
		/** Destroy : call the on_destroyed events of voxels.json. */
		bool fire_events = true;
		/** Fill the result's `cells` (false : only the counts, faster for huge edits). */
		bool keep_cells = true;

		bool Accepts(uint8_t type, uint32_t type_flags, bool indestructible) const
		{
			if (indestructible && !include_indestructible)
				return false;
			if (!types.empty())
			{
				bool found = false;
				for (uint8_t t : types)
					found = found || t == type;
				if (!found)
					return false;
			}
			for (uint8_t t : ignore_types)
				if (t == type)
					return false;
			return flags == 0u || (type_flags & flags) != 0u;
		}
	};

	/** Fill : which cells may receive the new type. */
	struct VoxelFillOptions
	{
		/** Also replace the cells that already have a voxel (that pass `filter`). */
		bool replace = false;
		/** With replace : which existing voxels may be replaced. */
		VoxelFilter filter;
	};

	/** What an edit did. Everything inline (usable from every DLL). */
	struct VoxelEditResult
	{
		/** Cells changed. */
		int count = 0;
		/** The changed cells (unless filter.keep_cells is false). */
		std::vector<VoxelCell> cells;
		/** Type before the edit -> number of cells (sorted by type ; 0 = were empty). */
		std::vector<std::pair<uint8_t, int>> per_type;
		/** Bounds of the changed cells, world units (valid if count > 0). */
		vec2 bounds_min;
		vec2 bounds_max;

		bool Any() const { return count > 0; }

		int CountOf(uint8_t type) const
		{
			for (const auto& [t, n] : per_type)
				if (t == type)
					return n;
			return 0;
		}

		vec2 Center() const { return vec2((bounds_min.x + bounds_max.x) * 0.5f, (bounds_min.y + bounds_max.y) * 0.5f); }
	};

	// ---- Types -----------------------------------------------------------------

	/** Type id from its name in voxels.json (case-insensitive), 0 if unknown. */
	LYNX_API uint8_t FindType(const char* name);

	// ---- Coordinates -------------------------------------------------------------

	/** Voxel cell containing a world position. false : no voxel world. */
	LYNX_API bool WorldToCell(const vec2& world, int& voxel_x, int& voxel_y);

	/** Center of a voxel cell, world units. */
	LYNX_API vec2 CellToWorld(int voxel_x, int voxel_y);

	/** Type at a world position (0 = empty). */
	LYNX_API uint8_t GetTypeAtWorld(const vec2& world);

	// ---- Destruction -------------------------------------------------------------

	LYNX_API VoxelEditResult DestroyCell(int voxel_x, int voxel_y, const VoxelFilter& filter = {});
	LYNX_API VoxelEditResult DestroyAt(const vec2& world, const VoxelFilter& filter = {});
	LYNX_API VoxelEditResult DestroyCircle(const vec2& center, float radius, const VoxelFilter& filter = {});
	LYNX_API VoxelEditResult DestroyRect(const vec2& center, const vec2& size, const VoxelFilter& filter = {});
	LYNX_API VoxelEditResult DestroyLine(const vec2& from, const vec2& to, float thickness = 1.f, const VoxelFilter& filter = {});

	// ---- Construction ------------------------------------------------------------
	// `type` : a type id (FindType) ; 0 is refused (use Destroy*).

	LYNX_API VoxelEditResult FillCell(int voxel_x, int voxel_y, uint8_t type, const VoxelFillOptions& options = {});
	LYNX_API VoxelEditResult FillAt(const vec2& world, uint8_t type, const VoxelFillOptions& options = {});
	LYNX_API VoxelEditResult FillCircle(const vec2& center, float radius, uint8_t type, const VoxelFillOptions& options = {});
	LYNX_API VoxelEditResult FillRect(const vec2& center, const vec2& size, uint8_t type, const VoxelFillOptions& options = {});
	LYNX_API VoxelEditResult FillLine(const vec2& from, const vec2& to, float thickness, uint8_t type, const VoxelFillOptions& options = {});

	// ---- Reading (no change) -------------------------------------------------------

	/** The voxels of a circle / rect that pass the filter (count, cells, per type). */
	LYNX_API VoxelEditResult CountCircle(const vec2& center, float radius, const VoxelFilter& filter = {});
	LYNX_API VoxelEditResult CountRect(const vec2& center, const vec2& size, const VoxelFilter& filter = {});
}
