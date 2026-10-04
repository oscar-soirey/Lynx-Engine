#pragma once

// Small helpers for the editor commands : base64, PNG encoding (screenshots).

#include <cstdint>
#include <string>
#include <vector>

namespace lynx::editor::commands
{
	std::string Base64Encode(const uint8_t* data, size_t size);
	std::string Base64Encode(const std::string& data);

	// false on invalid characters. Whitespace is ignored.
	bool Base64Decode(const std::string& text, std::string& out);

	// PNG file (zlib compressed) of `width` x `height` pixels, `channels` = 3
	// (RGB) or 4 (RGBA), rows top to bottom.
	std::string EncodePng(const uint8_t* pixels, int width, int height, int channels);

	// Box downscale so that the biggest side is <= max_size (no change if
	// already smaller). Rows top to bottom.
	void Downscale(std::vector<uint8_t>& pixels, int& width, int& height, int channels, int max_size);
}
