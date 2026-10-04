#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Common.h"

namespace lynx::fs
{
	enum class AssetSource
	{
		Directory,
		Archive
	};

	// Initializes the filesystem and selects the asset source.
	// Auto uses ./assets/ when available, otherwise assets.pak.
	LYNX_API bool Init(AssetSource source);

	// Checks whether an asset exists.
	LYNX_API bool Exists(const std::string& path);

	// Reads an asset into a byte vector.
	LYNX_API std::vector<std::uint8_t> ReadBinary(const std::string& path);

	// Reads an asset into a caller-provided buffer.
	LYNX_API bool ReadBinary(
		const std::string& path,
		void* buffer,
		std::size_t size
	);

	// Files of an asset folder ("classes" -> "classes/Player.js"...), sorted.
	// recursive : sub folders too. Empty if the folder does not exist.
	LYNX_API std::vector<std::string> ListFiles(const std::string& folder, bool recursive);

	// Returns the currently selected asset source.
	LYNX_API AssetSource GetSource();

	// Returns whether the filesystem was initialized successfully.
	LYNX_API bool IsInitialized();
}
