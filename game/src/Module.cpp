#include "GameClasses.h"
#include "Collision.h"
#include "Player.h"
#include "Ennemies/a.h"

#ifdef _WIN32
#define GAME_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define GAME_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

GAME_MODULE_EXPORT void Game_SetCollisionDebugEnabled(bool enabled)
{
  collision::SetDebugEnabled(enabled);
}


// ============================================================
// Module
// ============================================================

LYNX_LINK_MODULE(
		LYNX_MODULE_REGISTER(DebugCollisionShape);
		LYNX_MODULE_REGISTER(LightActor);
		LYNX_MODULE_REGISTER(AudioSource2D);
		LYNX_MODULE_REGISTER(Sprite);
		LYNX_MODULE_REGISTER(StaticSprite);
		LYNX_MODULE_REGISTER(Pawn);
		LYNX_MODULE_REGISTER(Player);
		LYNX_MODULE_REGISTER(A);
)