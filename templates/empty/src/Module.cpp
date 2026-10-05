#include <Lynx.h>
#include <hrl/hrl.h>
#include <core/GameModuleAPI.h>

#include "ExampleActor.h"

// =============================================================================
// Game module : the classes of the game, and the hooks called by the editor and
// the runtime (every LynxGame_* function is optional, see core/GameModuleAPI.h).
// =============================================================================

LYNX_LINK_MODULE(
	LYNX_MODULE_REGISTER(ExampleActor);
)

// Gameplay camera : looks at the start of the world. Make it follow an actor
// with LynxGame_UpdateGameplayCamera (see the Platformer template).
LYNX_GAME_EXPORT void LynxGame_OnLevelLoaded(uint32_t camera)
{
	HRL_SetCameraLocation(camera, 1000.f, 1670.f, 100.f);   // voxels
	HRL_SetCameraRotation(camera, 0.f, -90.f, 0.f);
}
