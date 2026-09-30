#pragma once

#include <hrl/hrl.h>
#include <Lynx.h>
#include "Common.h"

inline uint32_t GetFlagsBelow(float x, float y)
{
	float voxel_x;
	float voxel_y;

	if (HRL_WorldToVoxelCoordinates(
			lynx::GetScene(),
			x,
			y,
			&voxel_x,
			&voxel_y) != HRL_TRUE)
	{
		return 0;
	}

	const int x_coord = static_cast<int>(std::floor(voxel_x));
	const int y_coord = static_cast<int>(std::floor(voxel_y)) - 1;

	const uint8_t type = HRL_GetVoxelType(
			lynx::GetScene(),
			x_coord,
			y_coord
	);

	if (type == 0)
		return 0;

	return voxelData[type - 1].flags;
}