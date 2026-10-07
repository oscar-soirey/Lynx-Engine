#include <Lynx.h>
#include <core/GameModuleAPI.h>

#include "Game.h"

// =============================================================================
// Game module : the classes of the game and the hooks called by the editor
// and the runtime (every LynxGame_* function is optional, see
// core/GameModuleAPI.h). The camera is the CameraComponent of the hero.
// =============================================================================

LYNX_LINK_MODULE(
	LYNX_MODULE_REGISTER(Hero);
	LYNX_MODULE_REGISTER(Coin);
	LYNX_MODULE_REGISTER(Slime);
)

// Before the level : interfaces of the game (also usable from JavaScript).
LYNX_GAME_EXPORT void LynxGame_SetupScene(uint32_t scene)
{
	(void)scene;
	lynx::DefineInterface("Collector", { "OnCollected" });
}
