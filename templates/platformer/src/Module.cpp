#include <Lynx.h>
#include <hrl/hrl.h>
#include <core/GameModuleAPI.h>

#include <cmath>

#include "Player.h"

// =============================================================================
// Game module : the classes of the game, and the hooks called by the editor and
// the runtime (see core/GameModuleAPI.h in the engine).
// JavaScript classes (assets/classes/*.js) are registered by the engine itself.
// =============================================================================

LYNX_LINK_MODULE(
	LYNX_MODULE_REGISTER(Player);
)


// -----------------------------------------------------------------------------
// Gameplay camera : follows the actor "player"
// -----------------------------------------------------------------------------

namespace
{
	constexpr const char* kCameraTarget = "player";
	constexpr float kCameraHeight = 100.f;     // distance to the voxel plane (voxels)
	constexpr float kCameraOffsetY = 7.f;
	constexpr float kCameraFollowSpeed = 4.f;

	float camera_x = 0.f;
	float camera_y = 0.f;

	// Looked up every frame : the editor can delete / respawn the actor.
	lynx::Actor* FindTarget()
	{
		lynx::Engine* engine = lynx::Engine::Get();
		if (!engine || !engine->GetCurrentLevel())
			return nullptr;
		return engine->GetCurrentLevel()->GetActorFromID(kCameraTarget);
	}
}

LYNX_GAME_EXPORT void LynxGame_OnLevelLoaded(uint32_t camera)
{
	if (lynx::Actor* target = FindTarget())
	{
		camera_x = target->transform.location.x;
		camera_y = target->transform.location.y;
	}

	HRL_SetCameraLocation(camera, camera_x, camera_y + kCameraOffsetY, kCameraHeight);
	HRL_SetCameraRotation(camera, 0.f, -90.f, 0.f);
}

LYNX_GAME_EXPORT void LynxGame_UpdateGameplayCamera(uint32_t camera, float dt)
{
	if (lynx::Actor* target = FindTarget())
	{
		const float follow = 1.f - std::exp(-kCameraFollowSpeed * dt);
		camera_x += (target->transform.location.x - camera_x) * follow;
		camera_y += (target->transform.location.y - camera_y) * follow;
	}

	float x = camera_x;
	float y = camera_y + kCameraOffsetY;
	float roll = 0.f;
	lynx::GetCameraShake().Update(dt, x, y, roll);

	HRL_SetCameraLocation(camera, x, y, kCameraHeight);
	HRL_SetCameraRotation(camera, 0.f, -90.f, roll);
}
