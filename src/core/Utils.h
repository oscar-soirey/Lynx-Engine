#pragma once

// Small helpers available to the engine and to the games (Lynx.h).

#include <cstdint>
#include <random>
#include <string>

namespace lynx
{
	// Random float in [min, max].
	inline float FRandomInRange(float min, float max)
	{
		static std::random_device rd;
		static std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dist(min, max);
		return dist(gen);
	}

	// "75d420" or "#75d420" -> rgba in [0, 1] (alpha = 1).
	// Returns false (and leaves `out` untouched) when `hex` is not a color.
	inline bool HexToColor(const std::string& hex, float out[4])
	{
		std::string digits = hex;

		if (!digits.empty() && digits[0] == '#')
			digits.erase(0, 1);

		if (digits.size() != 6)
			return false;

		uint32_t value = 0;

		for (char c : digits)
		{
			value <<= 4;

			if (c >= '0' && c <= '9')
				value |= static_cast<uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f')
				value |= static_cast<uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				value |= static_cast<uint32_t>(c - 'A' + 10);
			else
				return false;
		}

		out[0] = ((value >> 16) & 0xFF) / 255.f;
		out[1] = ((value >> 8) & 0xFF) / 255.f;
		out[2] = (value & 0xFF) / 255.f;
		out[3] = 1.f;

		return true;
	}
}
