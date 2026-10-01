#pragma once

#include <cstdint>
#include <string>
#include <initializer_list>

// Max. 32 flags
#define GAME_VOXEL_ROCK (1u << 5)
#define GAME_VOXEL_SAND (1u << 6)

inline void HexToColor(const std::string& hex, float out[4])
{
	unsigned int value = std::stoul(hex, nullptr, 16);

	out[0] = ((value >> 16) & 0xFF) / 255.f;
	out[1] = ((value >> 8)  & 0xFF) / 255.f;
	out[2] = ((value      ) & 0xFF) / 255.f;
	out[3] = 1.f;
}

struct VoxelData
{
	uint32_t flags;
	float color[4];
	bool is_emissive;
	float emissive_color[4];

	VoxelData(uint32_t flags, const char* hex, bool emissive = false, const char* emissive_hex = "ffffff")
			: flags(flags), is_emissive(emissive)
	{
		HexToColor(hex, color);
		if (emissive)
		{
			HexToColor(emissive_hex, emissive_color);
		}
	}
};

inline const VoxelData voxelData[] = {
	{
		//Viorose b21ab4 clair
		HRL_VOXEL_COLLISION_LEFT |
		HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP |
		HRL_VOXEL_COLLISION_BOTTOM |
		GAME_VOXEL_ROCK,
		"b21ab4"
	},

//Viorose 851588 sombre
	{
		HRL_VOXEL_COLLISION_LEFT |
		HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP |
		HRL_VOXEL_COLLISION_BOTTOM |
		GAME_VOXEL_SAND,
		"851588"
	},

//Viorose violet sombre 7625a4
	{
		HRL_VOXEL_COLLISION_LEFT |
		HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP |
		HRL_VOXEL_COLLISION_BOTTOM |
		GAME_VOXEL_ROCK,
		"7625a4"
	},

	//Viorose blanc emissive b21ab4
	{
		HRL_VOXEL_COLLISION_LEFT |
		HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP |
		HRL_VOXEL_COLLISION_BOTTOM |
		GAME_VOXEL_ROCK,
		"d21ab4",
		true
	},

//Mossrite 51fa34
	{
		HRL_VOXEL_COLLISION_LEFT |
		HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP |
		HRL_VOXEL_COLLISION_BOTTOM |
		GAME_VOXEL_ROCK,
		"51fa34"
	}
};