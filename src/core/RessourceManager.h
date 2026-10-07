#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Common.h"

namespace lynx
{
	// Returns the (cached) HRL texture for an asset path, loading it on first use.
	// Returns HRL_INVALID_ID if the file could not be read.
	LYNX_API uint32_t RessourceTex(const char* path);
	LYNX_API uint32_t RessourceFont(const char* path);

	/**
	 * Texture formats added by plugins (ex : ".lsprite" of the PixelSprite
	 * plugin). The decoder turns the bytes of the file into bytes HRL can
	 * read (PNG, TGA...), in memory : every texture of the engine (sprites,
	 * animations, widgets, voxel types) then accepts that file.
	 * `nearest` : the texture is drawn without smoothing (pixel art).
	 */
	using TextureDecoder = bool (*)(const std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& image);
	LYNX_API void RegisterTextureDecoder(const char* extension, TextureDecoder decoder, bool nearest = true);
	LYNX_API void UnregisterTextureDecoder(const char* extension);

	/** Bytes of an asset ready for HRL_CreateTexture (decoded by a plugin if needed). Empty : failed. */
	LYNX_API std::vector<std::uint8_t> ReadTextureFile(const char* path);

	/** The file changed : the cached texture is reloaded in place (same id, every user sees it). */
	LYNX_API bool ReloadRessourceTexture(const char* path);

	/** +1 each time ReloadRessourceTexture reloads `path` (0 : never) : users can refresh what they derived from it. */
	LYNX_API uint32_t GetTextureRevision(const char* path);
}
