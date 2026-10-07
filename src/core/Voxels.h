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
//               "texture": "textures/stone.png", // optional : texture (multiplied by the color)
//               "texture_tile": 8,          // optional : voxels covered by one copy (default 1)
//               "indestructible": true,     // optional : attacks cannot destroy it
//               "on_destroyed": "example",  // optional : event, see RegisterDestroyedEvent()
//               "physics": {                // optional : see VoxelPhysicsProps (VoxelPhysics.h)
//                   "preset": "Sand", "behavior": "powder", "density": 1.6,
//                   "dispersion": 1, "friction": 0.8, "bounciness": 0,
//                   "drag": 0, "buoyancy": 0, "damage": 0, "contact_events": false
//               }
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
	/**
	 * How a voxel type moves while the game runs (VoxelPhysics.h) :
	 *   Static  never moves (ground, walls) ;
	 *   Powder  falls, slides down in piles (sand, gravel, snow) ;
	 *   Liquid  falls and spreads sideways (water, oil, lava) ;
	 *   Gas     rises and spreads (smoke, steam).
	 */
	enum class VoxelBehavior : uint8_t { Static = 0, Powder, Liquid, Gas };

	/** Physical properties of a voxel type (editor : Voxel types > Physics). */
	struct VoxelPhysicsProps
	{
		/** Name of the preset it comes from ("" : custom). */
		std::string preset;
		VoxelBehavior behavior = VoxelBehavior::Static;
		/** Heavier sinks in lighter (sand in water, water under oil). */
		float density = 1.f;
		/** Liquid / gas : cells it can move sideways per step (speed of spreading). */
		int dispersion = 4;
		/** Surface : 1 normal, 0.1 ice (slides), 2 mud (sticks). Multiplies the ground grip of the characters. */
		float friction = 1.f;
		/** Surface : 0 none, 1 a full bounce when landing on it. */
		float bounciness = 0.f;
		/** Inside (liquids, gases) : 0 nothing, 1 very thick (slows the actors down). */
		float drag = 0.f;
		/** Inside : 0 no effect, 1 cancels gravity, > 1 pushes up. */
		float buoyancy = 0.f;
		/** Damage per second to the actors touching it (Damageable.TakeDamage). */
		float damage = 0.f;
		/** VoxelEvents.OnVoxelContact / OnVoxelContactEnd to the actors that touch it. */
		bool contact_events = false;

		/** Inline : usable from the editor / game DLLs (the struct is not exported). */
		bool IsDefault() const
		{
			const VoxelPhysicsProps d;
			return behavior == d.behavior && density == d.density && dispersion == d.dispersion &&
			       friction == d.friction && bounciness == d.bounciness && drag == d.drag &&
			       buoyancy == d.buoyancy && damage == d.damage && contact_events == d.contact_events;
		}
	};

	/** Presets of the editor : name -> properties (Sand, Water, Ice...). */
	LYNX_API const std::vector<std::pair<std::string, VoxelPhysicsProps>>& GetPhysicsPresets();

	/** "static", "powder", "liquid", "gas". */
	LYNX_API const char* BehaviorName(VoxelBehavior behavior);

	struct VoxelType
	{
		std::string name;
		float color[4] = { 1.f, 1.f, 1.f, 1.f };

		bool emissive = false;
		float emissive_color[3] = { 1.f, 1.f, 1.f };

		// Collision flags + game flags (one word, as stored in the scene).
		uint32_t flags = 0;

		bool indestructible = false;

		// Texture (asset path, "" = color only), repeated every texture_tile
		// voxels in world space and multiplied by the color (white = raw texture).
		std::string texture;
		float texture_tile = 1.f;

		// Name of the event called when the voxel is destroyed ("" = none).
		std::string on_destroyed;

		// Physics (falling sand, flowing water, slippery ice...).
		VoxelPhysicsProps physics;
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
