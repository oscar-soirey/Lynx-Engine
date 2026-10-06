#pragma once

// =============================================================================
// Voxel types
// -----------------------------------------------------------------------------
// The voxel types of a project are described in assets/voxels.json :
//
//   {
//       "voxels": [
//           {
//               "name": "Grass",            // shown in the editor
//               "color": "75d420",          // rgb hex
//               "collision": "SOLID",       // "SOLID" (4 sides), "NONE", or a list :
//                                           // ["LEFT", "RIGHT", "TOP", "BOTTOM", "INSIDE"]
//               "flags": ["ROCK"],          // game flags (any name), see GetFlagMask()
//               "emissive": "ffffff",       // optional : emissive color
//               "indestructible": true,     // optional : attacks cannot destroy it
//               "on_destroyed": "example"   // optional : event, see RegisterDestroyedEvent()
//           }
//       ]
//   }
//
// Type id = position in the list + 1 (0 = empty voxel). // comments are allowed.
//
// The host (editor, runtime) loads the file and applies it to the scene before
// the level is created ; games only read the result.
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "Common.h"

namespace lynx::voxels
{
	struct VoxelType
	{
		std::string name;
		float color[4] = { 1.f, 1.f, 1.f, 1.f };

		bool emissive = false;
		float emissive_color[3] = { 1.f, 1.f, 1.f };

		// Collision flags + game flags (one word, as stored in the scene).
		uint32_t flags = 0;

		bool indestructible = false;

		// Name of the event called when the voxel is destroyed ("" = none).
		std::string on_destroyed;
	};

	// Event called when a voxel is destroyed (voxel cell coordinates).
	// Called after the voxel edit is finished : it may change the world.
	using DestroyedEventFn = void (*)(int voxel_x, int voxel_y);


	// -------------------------------------------------------------------------
	// Loading (host side)
	// -------------------------------------------------------------------------

	// Reads a voxel file from the assets (default "voxels.json").
	// On error the previous types are kept and GetLoadError() explains why.
	LYNX_API bool LoadFile(const char* asset_path = "voxels.json");

	// Last LoadFile() error ("" = none).
	LYNX_API const std::string& GetLoadError();

	// Sends every type (color, collision, emissive) to the scene.
	LYNX_API void ApplyToScene(uint32_t scene);


	// -------------------------------------------------------------------------
	// Types
	// -------------------------------------------------------------------------

	LYNX_API int GetTypeCount();

	// nullptr for 0 (empty) or an unknown type.
	LYNX_API const VoxelType* GetType(uint8_t type);

	// 0 for an empty / unknown type.
	LYNX_API uint32_t GetFlags(uint8_t type);

	LYNX_API bool IsIndestructible(uint8_t type);

	// -------------------------------------------------------------------------
	// Editing (editor : the "Color Picking" window)
	// -------------------------------------------------------------------------

	// Collision bits of VoxelType::flags (low bits) and the "SOLID" set.
	LYNX_API uint32_t GetCollisionMask();
	LYNX_API uint32_t GetSolidMask();

	// Game flags known so far (declared in voxels.json, or by DeclareFlag).
	LYNX_API std::vector<std::string> GetFlagNames();

	// Mask of a game flag, created if needed (0 : more than 27 flags).
	LYNX_API uint32_t DeclareFlag(const char* name);

	// Replaces a type (1..GetTypeCount()). Call ApplyToScene() to see it.
	LYNX_API bool SetType(uint8_t type, const VoxelType& value);

	// Adds a type at the end : its id, 0 when there are already 255.
	LYNX_API uint8_t AddType(const VoxelType& value);

	// Removes the last type (the others keep their id : the levels store ids).
	LYNX_API bool RemoveLastType();

	// Content of voxels.json for the current types (comments are not kept).
	LYNX_API std::string SaveToString();


	// Bit mask of a flag name : "ROCK" (game flag declared in voxels.json),
	// or a collision flag "LEFT", "RIGHT", "TOP", "BOTTOM", "INSIDE".
	// 0 when the name is unknown.
	LYNX_API uint32_t GetFlagMask(const char* name);


	// -------------------------------------------------------------------------
	// World queries (current scene)
	// -------------------------------------------------------------------------

	// Type of the voxel at a voxel cell (0 = empty).
	LYNX_API uint8_t GetTypeAt(int voxel_x, int voxel_y);

	// Flags of the voxel at a voxel cell (0 = empty).
	LYNX_API uint32_t GetFlagsAt(int voxel_x, int voxel_y);

	// Flags of the voxel right below a world position (0 = empty / outside).
	LYNX_API uint32_t GetFlagsBelow(float world_x, float world_y);


	// -------------------------------------------------------------------------
	// Destruction events
	// -------------------------------------------------------------------------

	// The game binds a name used in voxels.json ("on_destroyed") to a function.
	// Call it from LynxGame_SetupScene (it runs again after a DLL reload :
	// every event is cleared when the game DLL is unloaded).
	LYNX_API void RegisterDestroyedEvent(const char* name, DestroyedEventFn fn);
	LYNX_API void ClearDestroyedEvents();

	struct VoxelEvent
	{
		DestroyedEventFn fn;
		int x;
		int y;
	};

	// For each destroyed voxel : remembers its event (if it has one).
	LYNX_API void QueueDestroyedEvent(std::vector<VoxelEvent>& queue, uint8_t type, int voxel_x, int voxel_y);

	// Once the voxel edit is finished (after HRL_EndVoxelEdit) : calls them.
	LYNX_API void FireDestroyedEvents(const std::vector<VoxelEvent>& queue);
}
