#pragma once

#include <cstdint>
#include <string>
#include <initializer_list>
#include <iterator>
#include <vector>
#include <cstdio>
#include <random>


inline float FRandomInRange(float min, float max)
{
	static std::random_device rd;
	static std::mt19937 gen(rd());
	std::uniform_real_distribution<float> dist(min, max);
	return dist(gen);
}


#define GAME_DEBUG



// Max. 32 flags
#define GAME_VOXEL_ROCK (1u << 5)
#define GAME_VOXEL_SAND (1u << 6)

// Le voxel ne peut pas etre detruit (attaques, projectiles). L'editeur peut
// toujours le modifier.
#define GAME_VOXEL_INDESTRUCTIBLE (1u << 7)

// Evenement declenche quand un voxel est detruit. Recoit la position du voxel
// detruit (cellule voxel ; HRL_VoxelToWorldCoordinates pour le monde).
// Il est appele apres la fin de l'edition des voxels : il peut donc modifier
// le monde (spawn, autres voxels...).
using VoxelEventFn = void (*)(int voxel_x, int voxel_y);

//Voxels colors

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

	// Evenement appele quand le voxel est detruit (nullptr = aucun).
	VoxelEventFn on_destroyed;

	VoxelData(uint32_t flags, const char* hex, bool emissive = false, const char* emissive_hex = "ffffff",
			VoxelEventFn on_destroyed = nullptr)
			: flags(flags), is_emissive(emissive), on_destroyed(on_destroyed)
	{
		HexToColor(hex, color);
		if (emissive)
		{
			HexToColor(emissive_hex, emissive_color);
		}
	}
};

// ------------------------------------------------------------
// Evenements de voxels : ajouter une fonction ici (ou dans un .h inclus avant),
// puis la passer en 5e parametre du VoxelData voulu.
// ------------------------------------------------------------
inline void OnExampleVoxelDestroyed(int voxel_x, int voxel_y)
{
	if (FRandomInRange(0.0f, 1.f) > 0.95f)
	{
		//printf("spawned");
		//auto* act = lynx::GetEngine()->GetCurrentLevel()->SpawnActor("Bouncy");
		//if (!act)
		//	std::cout << "actor not found" << std::endl;
		//act->transform = lynx::transform{{(float)voxel_x, (float)voxel_y, 0.f}, {}, {1.f, 1.f, 1.f}};
	}
}

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
	},

//Voxel evenement (jaune) : appelle OnExampleVoxelDestroyed quand il est detruit
	{
		HRL_VOXEL_COLLISION_LEFT |
		HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP |
		HRL_VOXEL_COLLISION_BOTTOM |
		GAME_VOXEL_ROCK,
		"ffd700",
		true, "ffffff",
		&OnExampleVoxelDestroyed
	},

//Voxel indestructible (gris sombre)
	{
		HRL_VOXEL_COLLISION_LEFT |
		HRL_VOXEL_COLLISION_RIGHT |
		HRL_VOXEL_COLLISION_TOP |
		HRL_VOXEL_COLLISION_BOTTOM |
		GAME_VOXEL_ROCK |
		GAME_VOXEL_INDESTRUCTIBLE,
		"3a3a4a"
	}
};

inline constexpr size_t voxelDataCount = sizeof(voxelData) / sizeof(voxelData[0]);

// ------------------------------------------------------------
// Aides pour les boucles de destruction
// ------------------------------------------------------------

// type = valeur de HRL_GetVoxelType (0 = vide, sinon index + 1 dans voxelData).
inline bool IsVoxelIndestructible(uint8_t type)
{
	if (type == 0 || type > std::size(voxelData))
		return false;

	return (voxelData[type - 1].flags & GAME_VOXEL_INDESTRUCTIBLE) != 0u;
}

struct VoxelEvent
{
	VoxelEventFn fn;
	int x;
	int y;
};

// A appeler pour chaque voxel detruit : memorise son evenement (s'il en a un).
inline void QueueVoxelEvent(std::vector<VoxelEvent>& queue, uint8_t type, int x, int y)
{
	if (type == 0 || type > std::size(voxelData))
		return;

	const VoxelEventFn fn = voxelData[type - 1].on_destroyed;

	if (fn)
		queue.push_back({fn, x, y});
}

// A appeler une fois l'edition des voxels terminee (HRL_EndVoxelEdit).
inline void FireVoxelEvents(const std::vector<VoxelEvent>& queue)
{
	for (const VoxelEvent& e : queue)
		e.fn(e.x, e.y);
}