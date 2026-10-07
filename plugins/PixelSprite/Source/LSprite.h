#pragma once

// =============================================================================
// .lsprite : a pixel art sprite made in the editor (PixelSprite plugin).
// -----------------------------------------------------------------------------
// JSON (readable, versionable) :
// {
//   "format": "lsprite", "version": 1,
//   "width": 16, "height": 16,
//   "fps": 8, "loop": true,
//   "palette": [ "00000000", "1d2b53ff", ... ],   // RRGGBBAA, index 0 = transparent
//   "frames":  [ "0001010203...", ... ]           // width*height indices, 2 hex digits each
// }
// It is never saved as an image : the game turns it into a texture in memory
// (frames side by side : a horizontal strip, like AnimationSprite expects).
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace lsprite
{
	constexpr int kMaxSize = 512;
	constexpr int kMaxFrames = 256;
	constexpr int kMaxColors = 256;
	/** Width of the texture (every frame side by side) : GPUs and TGA limits. */
	constexpr int kMaxStripWidth = 8192;

	struct Sprite
	{
		int width = 16;
		int height = 16;
		float fps = 8.f;
		bool loop = true;
		/** RGBA (0xRRGGBBAA). Index 0 : transparent. */
		std::vector<uint32_t> palette;
		/** One index per pixel (row by row, top first). */
		std::vector<std::vector<uint8_t>> frames;

		int FrameCount() const { return static_cast<int>(frames.size()); }
		uint8_t Get(int frame, int x, int y) const;
		void Set(int frame, int x, int y, uint8_t index);
		bool Inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

		/** Crops / extends every frame (top-left anchored). */
		void Resize(int new_width, int new_height);
		/** Removes a palette color : its pixels become transparent, the indices above move down. */
		void RemoveColor(int index);
	};

	/** A new sprite : `width` x `height`, one empty frame, the default palette. */
	Sprite MakeDefault(int width = 16, int height = 16);
	std::vector<uint32_t> DefaultPalette();

	bool Parse(const std::string& text, Sprite& out, std::string& error);
	std::string Serialize(const Sprite& sprite);

	/** RGBA pixels of every frame side by side (width * frames x height), top row first. */
	std::vector<uint8_t> BuildStrip(const Sprite& sprite);
	/** The strip as an uncompressed 32 bit TGA, in memory (what HRL_CreateTexture reads). Empty : too wide. */
	std::vector<uint8_t> BuildTga(const Sprite& sprite);

	inline uint8_t R(uint32_t c) { return static_cast<uint8_t>(c >> 24); }
	inline uint8_t G(uint32_t c) { return static_cast<uint8_t>(c >> 16); }
	inline uint8_t B(uint32_t c) { return static_cast<uint8_t>(c >> 8); }
	inline uint8_t A(uint32_t c) { return static_cast<uint8_t>(c); }
	inline uint32_t Rgba(int r, int g, int b, int a)
	{
		return (static_cast<uint32_t>(r & 255) << 24) | (static_cast<uint32_t>(g & 255) << 16) |
		       (static_cast<uint32_t>(b & 255) << 8) | static_cast<uint32_t>(a & 255);
	}
}
