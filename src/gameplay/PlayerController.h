#ifndef HGE_PLAYER_CONTROLLER_H
#define HGE_PLAYER_CONTROLLER_H

#include "../core/Common.h"
#include <cstdint>

namespace lynx
{
	class Actor;

	/**
	 * Create player controller
	 * @return ID of the new PlayerController
	 */
	int LYNX_API CreatePlayer();
	void LYNX_API DeletePlayer(int pc);
	void LYNX_API PossessActor(int pc, Actor* act);
	void LYNX_API UnpossessActor(int pc);
	LYNX_API Actor* GetPossessedActor(int pc);

	//ajouter une gestion auto
	void LYNX_API SetPlayerViewportSize(int pc,
		float x, float y,
		float _width, float _height
	);
	uint32_t GetPlayerViewportBackend(int pc);

	int LYNX_API GetPlayerCount();
}

#endif