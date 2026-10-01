#pragma once

#include <cstdint>
#include "Common.h"

namespace lynx
{
	// Returns the (cached) HRL texture for an asset path, loading it on first use.
	// Returns HRL_INVALID_ID if the file could not be read.
	LYNX_API uint32_t RessourceTex(const char* path);
}