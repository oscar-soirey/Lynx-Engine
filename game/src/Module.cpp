#include "GameClasses.h"
#include "Player.h"
#include "Ennemies/a.h"

#ifdef _WIN32
#define GAME_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define GAME_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

GAME_MODULE_EXPORT void Game_SetCollisionDebugEnabled(bool enabled)
{
    Pawn::SetCollisionDebugEnabled(enabled);
}


// ============================================================
// Module
// ============================================================

LYNX_LINK_MODULE(
		LYNX_MODULE_REGISTER(Sprite);
		LYNX_MODULE_REGISTER(Pawn);
		LYNX_MODULE_REGISTER(Player);
		LYNX_MODULE_REGISTER(A);
)