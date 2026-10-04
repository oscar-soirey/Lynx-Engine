#include <hrl/hrl.h>
#include <core/GameModuleAPI.h>

#include <cmath>
#include <memory>

#include "GameClasses.h"
#include "Collision.h"
#include "Player.h"
#include "Ennemies/Mushroom.h"
#include "Objects/Bouncy.h"


// ============================================================
// Host functions (see core/GameModuleAPI.h)
// ------------------------------------------------------------
// Called by the Lynx editor and the Lynx runtime.
// The voxel types are in assets/voxels.json (lynx::voxels).
// ============================================================

// ------------------------------------------------------------
// Voxel events : "on_destroyed" in assets/voxels.json
// ------------------------------------------------------------

static void OnExampleVoxelDestroyed(int voxel_x, int voxel_y)
{
	(void)voxel_x;
	(void)voxel_y;

	if (lynx::FRandomInRange(0.0f, 1.f) > 0.95f)
	{
		//auto* act = lynx::GetEngine()->GetCurrentLevel()->SpawnActor("Bouncy");
		//act->transform = lynx::transform{{(float)voxel_x, (float)voxel_y, 0.f}, {}, {1.f, 1.f, 1.f}};
	}
}

LYNX_GAME_EXPORT void LynxGame_SetupScene(uint32_t scene)
{
	(void)scene;

	lynx::voxels::RegisterDestroyedEvent("example", &OnExampleVoxelDestroyed);
}


// ------------------------------------------------------------
// Game start / end : background music
// ------------------------------------------------------------

static std::unique_ptr<lynx::Audio2D> ambient_music;

LYNX_GAME_EXPORT void LynxGame_OnGameStart()
{
	ambient_music = std::make_unique<lynx::Audio2D>("cave_ambience.mp3");
	ambient_music->looping = true;
	ambient_music->Play();
}

LYNX_GAME_EXPORT void LynxGame_OnGameEnd()
{
	ambient_music.reset();
}


// Collision debug overlay (editor F4).
LYNX_GAME_EXPORT void LynxGame_SetCollisionDebugEnabled(int enabled)
{
	collision::SetDebugEnabled(enabled != 0);
}


// ------------------------------------------------------------
// Gameplay camera : follows the "Pawn" actor, with camera shake
// ------------------------------------------------------------

namespace
{
	constexpr const char* kCameraTargetId = "Pawn";
	constexpr float kCameraHeight = 80.f;
	constexpr float kCameraFollowSpeed = 3.f;
	constexpr float kCameraOffsetY = 2.f;

	float gameplayCamX = 0.f;
	float gameplayCamY = 0.f;

	// Looked up every frame : the editor can delete / respawn the actor at
	// any time, a cached pointer could dangle.
	lynx::Actor* FindCameraTarget()
	{
		lynx::Engine* engine = lynx::GetEngine();

		if (!engine || !engine->GetCurrentLevel())
			return nullptr;

		return engine->GetCurrentLevel()->GetActorFromID(kCameraTargetId);
	}
}

// New level : the camera jumps on the player (no smoothing).
LYNX_GAME_EXPORT void LynxGame_OnLevelLoaded(uint32_t gameplay_camera)
{
	if (lynx::Actor* target = FindCameraTarget())
	{
		gameplayCamX = target->transform.location.x;
		gameplayCamY = target->transform.location.y;
	}

	HRL_SetCameraLocation(
			gameplay_camera,
			gameplayCamX,
			gameplayCamY,
			kCameraHeight
	);
}

LYNX_GAME_EXPORT void LynxGame_UpdateGameplayCamera(uint32_t gameplay_camera, float dt)
{
	if (lynx::Actor* target = FindCameraTarget())
	{
		const float follow = 1.f - std::exp(-kCameraFollowSpeed * dt);

		gameplayCamX += (target->transform.location.x - gameplayCamX) * follow;
		gameplayCamY += (target->transform.location.y - gameplayCamY) * follow;
	}

	float shakenX = gameplayCamX;
	float shakenY = gameplayCamY + kCameraOffsetY;
	float shakenRotationZ = 0.f;

	lynx::GetCameraShake().Update(
			dt,
			shakenX,
			shakenY,
			shakenRotationZ
	);

	HRL_SetCameraLocation(
			gameplay_camera,
			shakenX,
			shakenY,
			kCameraHeight
	);

	HRL_SetCameraRotation(
			gameplay_camera,
			0.f,
			-90.f,
			shakenRotationZ
	);
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
		LYNX_MODULE_REGISTER(Mushroom);
		LYNX_MODULE_REGISTER(Bouncy);
)
