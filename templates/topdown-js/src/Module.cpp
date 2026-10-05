#include <Lynx.h>
#include <hrl/hrl.h>
#include <core/GameModuleAPI.h>

#include <cmath>

// =============================================================================
// Game module. The gameplay of this template is in JavaScript
// (assets/classes/*.js, registered by the engine) : the C++ side only moves the
// camera. Add C++ classes here when you need them :
//     LYNX_LINK_MODULE( LYNX_MODULE_REGISTER(MyActor); )
// =============================================================================

LYNX_LINK_MODULE()


namespace
{
	constexpr const char* kCameraTarget = "hero";
	constexpr float kCameraHeight = 100.f;   // voxels
	constexpr float kCameraFollowSpeed = 5.f;

	float camera_x = 0.f;
	float camera_y = 0.f;

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

	HRL_SetCameraLocation(camera, camera_x, camera_y, kCameraHeight);
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

	HRL_SetCameraLocation(camera, camera_x, camera_y, kCameraHeight);
	HRL_SetCameraRotation(camera, 0.f, -90.f, 0.f);
}
