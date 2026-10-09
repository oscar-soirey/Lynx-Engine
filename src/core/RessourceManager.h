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

	/**
	 * Several textures at once (level start, voxel types...) : the files are
	 * read and decoded (plugins) on the worker threads, then decoded by HRL
	 * in the background (HRL_CreateTextureAsync) ; returns when every texture
	 * is uploaded. ids[i] : HRL_INVALID_ID when paths[i] failed. Not cached
	 * (see PreloadTextures). Main thread.
	 */
	LYNX_API std::vector<std::uint32_t> CreateTextures(const std::vector<std::string>& paths);

	/** CreateTextures for the paths RessourceTex does not know yet : RessourceTex(path) is then instant. */
	LYNX_API void PreloadTextures(const std::vector<std::string>& paths);

	/**
	 * true (default) : CreateTextures lets HRL decode in the background
	 * (HRL_CreateTextureAsync). false : HRL_CreateTexture, one after the other
	 * (the files are still read in parallel).
	 */
	LYNX_API void SetAsyncTextureDecoding(bool enabled);

	/** The file changed : the cached texture is reloaded in place (same id, every user sees it). */
	LYNX_API bool ReloadRessourceTexture(const char* path);

	/** +1 each time ReloadRessourceTexture reloads `path` (0 : never) : users can refresh what they derived from it. */
	LYNX_API uint32_t GetTextureRevision(const char* path);
}
