#pragma once

// =============================================================================
// Sprite -> voxels
// -----------------------------------------------------------------------------
// Right click on a sprite actor (viewport or Outliner) > "Convert to voxels..." :
// the image of the sprite is projected (nearest pixel, or the dominant color of
// each cell) onto the voxel grid, where the sprite is in the level.
//
// Colors : each one takes the closest voxel type of the palette (CIELAB
// distance) ; the colors that no type comes close to become new voxel types
// (grouped by median cut, at most "max new types"), saved in voxels.json.
// One Ctrl+Z undoes everything (voxels, new types, the sprite hidden / deleted).
// =============================================================================

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lynx
{
	class Actor;
}

namespace lynx::editor::sprite_voxels
{
	// -------------------------------------------------------------------------
	// Conversion (no editor, no renderer : testable alone)
	// -------------------------------------------------------------------------

	struct Image
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> rgba;   // row 0 = top of the image
	};

	/** Where the sprite is drawn. */
	struct Source
	{
		std::string name;            // for the new types ("Tree 1"...)
		std::string texture;
		float u0 = 0.f, v0 = 0.f;    // part of the image (v0 = top)
		float u1 = 1.f, v1 = 1.f;
		float center_x = 0.f;        // world (1 unit = 1 voxel)
		float center_y = 0.f;
		float width = 1.f;           // world size (> 0)
		float height = 1.f;
		bool flip_x = false;
		bool flip_y = false;
	};

	enum class SizeMode { MatchSprite, PixelPerVoxel, CustomWidth };
	enum class Sampling { Nearest, Dominant };
	enum class PaletteMode { ExistingAndNew, ExistingOnly, NewOnly };
	enum class After { Keep, Hide, Delete };

	struct Options
	{
		SizeMode size_mode = SizeMode::MatchSprite;
		int custom_width = 32;
		Sampling sampling = Sampling::Nearest;
		float alpha_threshold = 0.5f;    // pixel kept when alpha >= this
		PaletteMode palette = PaletteMode::ExistingAndNew;
		float tolerance = 12.f;          // CIELAB distance under which an existing type is used
		int max_new_types = 16;
		bool only_empty_cells = false;   // keep the voxels already there
		bool new_types_solid = true;     // collision of the new types
		After after = After::Hide;
	};

	/** A voxel type the colors can take. */
	struct PaletteType
	{
		int type = 0;                    // existing type id (0 : new type, created on Apply)
		std::array<float, 3> rgb{};      // 0..1
	};

	struct Result
	{
		int cols = 0;
		int rows = 0;
		int origin_x = 0;                // voxel cell of the bottom left corner
		int origin_y = 0;
		std::vector<int> cells;          // rows * cols, row 0 = top : -1 empty, else index in palette
		std::vector<PaletteType> palette;
		int voxel_count = 0;
		int existing_used = 0;
		int new_types = 0;
		float mean_error = 0.f;          // CIELAB distance image -> voxels (mean)
		std::string error;               // why nothing (empty image...)

		/** Voxel cell of a cell of the grid (row 0 = top). */
		int VoxelX(int col) const { return origin_x + col; }
		int VoxelY(int row) const { return origin_y + (rows - 1 - row); }
	};

	/** Grid size from the options (cols x rows). */
	void GridSize(const Image& image, const Source& source, const Options& options, int& cols, int& rows);

	Result Convert(const Image& image, const Source& source, const std::vector<PaletteType>& existing, const Options& options);

	/** CIELAB distance of two sRGB colors (0..1). */
	float ColorDistance(const std::array<float, 3>& a, const std::array<float, 3>& b);

	/** PNG / JPG... bytes -> Image. */
	bool DecodeImage(const std::vector<uint8_t>& bytes, Image& out, std::string& error);


	// -------------------------------------------------------------------------
	// Editor
	// -------------------------------------------------------------------------

	/** What the editor gives to the dialog. */
	struct Host
	{
		uint32_t scene = 0;
		std::function<void(std::function<void()>)> push_undo;
		std::function<void()> mark_dirty;
		/** Deletes the actor (selection cleared) and returns how to bring it back. */
		std::function<std::function<void()>(lynx::Actor*)> delete_actor;
		/** Actor by object id (undo). */
		std::function<lynx::Actor*(const std::string&)> find_actor;
		/** The actor is still in the level (actors without object id). */
		std::function<bool(lynx::Actor*)> is_alive;
		/** Writes assets/voxels.json. */
		std::function<void()> save_voxel_types;
	};

	/** The actor has a sprite that can be converted (why not : `reason`). */
	bool CanConvert(lynx::Actor* actor, std::string* reason = nullptr);

	/** Opens the dialog for this actor. */
	void Open(lynx::Actor* actor);

	/** Every frame (draws the dialog when open). */
	void Draw(const Host& host);

	/** Options of the dialog (kept from one conversion to the next). */
	Options& GetOptions();

	/** For the context menus : "Convert to voxels..." item. */
	void MenuItem(lynx::Actor* actor);
}
