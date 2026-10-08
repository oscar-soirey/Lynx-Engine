#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include "Filesystem.h"
#include "Profiler.h"
#include "Level.h"
#include "Plugins.h"
#include "CameraState.h"
#include "Lighting2D.h"
#include "Particles.h"
#include "VoxelPhysics.h"
#include "../gameplay/PhysicsQueries.h"
#include "../gameplay/Actor.h"
#include "../gameplay/EngineActors.h"
#include "../gameplay/PlayerController.h"
#include "../widgets/UserWidget.h"
#include "../widgets/Private/WidgetSystem.h"
#include "../gameplay/Private/InputManager.h"
#include "audio/AudioCommon.h"
#include "audio/AudioListener.h"
#include "Private/SystemModule.h"
#include "../gameplay/Private/ECS.h"
#include "../scripting/Private/ScriptSystem.h"


namespace lynx
{
	Engine* Engine::instance_ = nullptr;
	bool Engine::release_mode_ = false;

	Engine::Engine()
	{
		fs::AssetSource asset_src = fs::AssetSource::Directory;
		if (release_mode_) asset_src = fs::AssetSource::Archive;
		fs::Init(asset_src);
		InitializeAudio();
		scripting::Init();

		// Actor, lights, SpriteActor, SoundActor : always available.
		RegisterEngineActors(factory_);

		// Default player (player 0) : its viewport is created with the scene.
		players_.push_back(new PlayerController(next_player_id_++));
	}

	Engine::~Engine()
	{
		// OnUnpossessed while the actors are still alive.
		UnpossessAll();

		// Widgets (NativeDestruct may still use the level), before the renderer.
		WidgetSystem::Shutdown();

		delete current_level_;
		current_level_ = nullptr;
		// apres le niveau : les acteurs detruits liberent leurs scripts
		scripting::Shutdown();
		ShutdownAudio();

		// Viewports / cameras of the players : before HRL_Shutdown.
		for (PlayerController* player : players_)
			delete player;
		players_.clear();

		// frees every HRL object, the scene too
		lighting2d::Shutdown();
		particles::Shutdown();
		camera_state::Clear();
		HRL_Shutdown();
		scene_ = HRL_INVALID_ID;
		scene_created_ = false;
	}

	void Engine::RequestRenderRefresh(int frames)
	{
		render_refresh_frames_ = std::max(render_refresh_frames_, frames);
	}

	void Engine::ProgressOneFrame(float dt)
	{
		LYNX_PROFILE_SCOPE("Engine::ProgressOneFrame");

		// A mesh deletion makes HRL rebuild its draw lists (see
		// RequestRenderRefresh) : a temporary sprite, created and deleted.
		if (render_refresh_frames_ > 0 && scene_created_ && HRL_IsValidScene(scene_))
		{
			LYNX_PROFILE_SCOPE("Render refresh (mesh deleted)");
			--render_refresh_frames_;
			const HRL_id temp = HRL_CreateMeshSprite(scene_);
			if (temp != HRL_INVALID_ID)
				HRL_DeleteMesh(temp);
		}

		// ========================================================
		// Time dilation timer
		// ========================================================

		if (time_dilatation_timer_ > 0.0f)
		{
			time_dilatation_timer_ -= dt;

			if (time_dilatation_timer_ <= 0.0f)
				time_dilatation_timer_ = 0.0f;
		}


		// ========================================================
		// Dilated delta time
		// ========================================================

		const float game_dt = dt * GetGlobalTimeDilatation();

		// Plugins : every frame (game time ; also in the editor, not playing).
		{
			LYNX_PROFILE_SCOPE("Plugins");
			plugins::Tick(game_dt, game_tick_enabled_);
		}


		// ========================================================
		// Physics & gameplay
		// ========================================================

		{
			LYNX_PROFILE_SCOPE("Actors Update");
			for (const auto& a : current_level_->GetActors())
			{
				a->Update(game_dt);
			}
		}

		if (current_level_)
		{
			LYNX_PROFILE_SCOPE("Components Update");
			// Les spawns faits par les composants sont differes jusqu'a
			// Level::Update (voir Level::IterationScope).
			Level::IterationScope scope(*current_level_);
			ecs::Update(game_dt);
		}


		if (game_tick_enabled_)
		{
			// Actors spawned (or changed) since the last frame.
			{
				LYNX_PROFILE_SCOPE("Auto possess");
				ProcessAutoPossess();
			}

			// Input first (like Unreal's PlayerTick) : each player gives the
			// input to the actor it possesses, then the actors tick.
			{
				LYNX_PROFILE_SCOPE("Players Input");
				// Simulation : nobody is possessed, nobody gets input.
				if (!simulating_)
				{
					const std::vector<PlayerController*> players = players_;
					for (PlayerController* player : players)
						player->ProcessInput();
				}
			}

			{
				LYNX_PROFILE_SCOPE("Actors Tick");
				for (const auto& a : current_level_->GetActors())
				{
					a->Tick(game_dt);
				}
			}

			if (current_level_)
			{
				LYNX_PROFILE_SCOPE("Components Tick");
				// Les spawns faits par les scripts / systemes sont differes
				// jusqu'a Level::Update (voir Level::IterationScope).
				Level::IterationScope scope(*current_level_);

				// Composants : BeginPlay des nouveaux, puis Tick (scripts JS,
				// Velocity, Lifetime, composants du jeu...)
				ecs::Tick(game_dt);
			}

			// Voxel physics (sand, water, gas... around the cameras).
			voxel_physics::Tick(game_dt);

			{
				LYNX_PROFILE_SCOPE("Input");
				InputTick();
			}
		}

		if (current_level_)
		{
			// Apres TOUT le gameplay : les sprites, lumieres et la camera
			// recopient les positions finales de la frame vers HRL. (Avant,
			// la synchro se faisait dans Update, avant le mouvement : le rendu
			// avait une frame de retard sur la camera -> saccades.)
			LYNX_PROFILE_SCOPE("Components LateUpdate");
			Level::IterationScope scope(*current_level_);
			ecs::LateUpdate(game_dt);
		}

		if (current_level_)
		{
			LYNX_PROFILE_SCOPE("Level::Update");
			current_level_->Update();
		}

		{
			// 2D lighting : after the cameras and lights got their final position.
			LYNX_PROFILE_SCOPE("Lighting2D");
			lighting2d::Update(dt);
		}

		// One-shot particle effects (Particles.spawn) : the finished ones go.
		{
			LYNX_PROFILE_SCOPE("Particles");
			particles::Tick(game_dt);
		}

		LYNX_PROFILE_PLOT("Actors", static_cast<int64_t>(current_level_ ? current_level_->GetActors().size() : 0));

		// Le listener suit l'acteur attache (AttachAudioListener) : sans cet appel,
		// il reste a (0,0,0) et il n'y a aucune attenuation.
		{
			LYNX_PROFILE_SCOPE("Audio listener");
			UpdateAudioListener();
		}

		// Debug drawings of the physics queries that last (debug_duration).
		{
			LYNX_PROFILE_SCOPE("Physics debug draw");
			physics::TickDebug(dt);
		}

		{
			// Widgets : clicks of the last frame, changes, layout. Real time
			// (a paused game still has a working menu).
			LYNX_PROFILE_SCOPE("Widgets");
			WidgetSystem::Tick(dt);
		}

		{
			LYNX_PROFILE_SCOPE("Render (HRL)");
			HRL_BeginFrame();
			HRL_EndFrame();
		}
	}

	void Engine::SetGlobalTimeDilatation(float dilation)
	{
		// Valeur de fond uniquement : un hit-stop en cours n'est pas annule
		// (la dilatation effective prend le minimum des deux).
		base_time_dilatation_ = dilation;
	}

	void Engine::SetGlobalTimeDilatation(
			float dilation,
			float duration)
	{
		if (duration <= 0.0f)
			return;

		// Hit-stop deja en cours : on garde le plus fort et la duree la plus
		// longue. Avant, on memorisait la valeur courante (deja dilatee) comme
		// "valeur a restaurer", qui restait donc bloquee a 0.1 pour toujours.
		if (time_dilatation_timer_ > 0.0f)
		{
			temp_time_dilatation_ = std::min(temp_time_dilatation_, dilation);
			time_dilatation_timer_ = std::max(time_dilatation_timer_, duration);
		}
		else
		{
			temp_time_dilatation_ = dilation;
			time_dilatation_timer_ = duration;
		}
	}

	float Engine::GetGlobalTimeDilatation() const
	{
		if (time_dilatation_timer_ > 0.0f)
			return std::min(base_time_dilatation_, temp_time_dilatation_);

		return base_time_dilatation_;
	}

	void Engine::StartGame()
	{
		game_tick_enabled_ = true;

		// Before StartGame / BeginPlay (like Unreal) : OnPossessed comes first,
		// and a CameraComponent knows its player in its BeginPlay.
		ProcessAutoPossess();

		for (auto& a: current_level_->GetActors())
		{
			a->StartGame();
		}

		// apres StartGame : les composants voient les acteurs deja initialises
		ecs::BeginPlay();

		voxel_physics::Reset();

		// Plugins (LynxGame_OnGameStart of their runtime modules).
		plugins::OnGameStart();
	}

	void Engine::EndGame()
	{
		// Plugins first, while the game is still entire.
		if (game_tick_enabled_)
			plugins::OnGameEnd();

		game_tick_enabled_ = false;

		// The players release their actors while the game is still entire.
		UnpossessAll();

		// The widgets of the game go with it (NativeDestruct while the actors live).
		ui::DestroyAllWidgets();

		// One-shot particle effects of the game (Particles.spawn).
		particles::ClearSpawned();

		ecs::EndPlay();

		if (current_level_)
		{
			for (auto& a: current_level_->GetActors())
			{
				a->EndGame();

				// Possessed again at the next StartGame.
				a->auto_possess_done_ = false;
			}
		}

		// The players created during the game go away with it (like the local
		// players of a Play In Editor session). The default player stays.
		const std::vector<PlayerController*> players = players_;
		for (PlayerController* player : players)
		{
			if (player->created_during_play_)
				DestroyPlayer(player);
		}
	}


	// ------------------------------------------------------------------
	// Local players
	// ------------------------------------------------------------------

	PlayerController* Engine::CreatePlayer()
	{
		auto* player = new PlayerController(next_player_id_++);
		player->created_during_play_ = game_tick_enabled_;

		players_.push_back(player);

		if (scene_created_)
			player->CreateRenderObjects();

		UpdatePlayerViewports();

		ED_player_created.Call(player);

		return player;
	}

	bool Engine::DestroyPlayer(PlayerController* player)
	{
		auto it = std::find(players_.begin(), players_.end(), player);

		if (it == players_.end())
			return false;

		if (it == players_.begin())
		{
			std::cerr << "[PLAYER] The default player (player 0) can not be destroyed\n";
			return false;
		}

		player->Unpossess();

		// Its widgets leave the screen before its viewport goes away.
		WidgetSystem::OnPlayerDestroyed(player);
		scripting::OnPlayerDestroyed(player);

		// The index of the player is still valid during Unpossess.
		it = std::find(players_.begin(), players_.end(), player);
		if (it != players_.end())
			players_.erase(it);

		delete player;

		UpdatePlayerViewports();
		return true;
	}

	PlayerController* Engine::GetPlayer(int index) const
	{
		if (index < 0 || index >= static_cast<int>(players_.size()))
			return nullptr;
		return players_[static_cast<size_t>(index)];
	}

	PlayerController* Engine::GetDefaultPlayer() const
	{
		return GetPlayer(0);
	}

	int Engine::GetPlayerCount() const
	{
		return static_cast<int>(players_.size());
	}

	const std::vector<PlayerController*>& Engine::GetPlayers() const
	{
		return players_;
	}

	PlayerController* Engine::GetPlayerOf(const Actor* actor) const
	{
		return actor ? actor->GetController() : nullptr;
	}

	void Engine::UpdatePlayerViewports()
	{
		std::vector<PlayerController*> automatic;
		for (PlayerController* player : players_)
		{
			if (!player->custom_rect_)
				automatic.push_back(player);
		}

		const int count = static_cast<int>(automatic.size());
		if (count == 0)
			return;

		auto set = [&](int i, float x, float y, float w, float h)
		{
			automatic[static_cast<size_t>(i)]->ApplyRect(x, y, w, h);
		};

		switch (count)
		{
		case 1:
			set(0, 0.f, 0.f, 1.f, 1.f);
			break;

		case 2:     // top / bottom
			set(0, 0.f, 0.f, 1.f, 0.5f);
			set(1, 0.f, 0.5f, 1.f, 0.5f);
			break;

		case 3:     // top, then two halves at the bottom
			set(0, 0.f, 0.f, 1.f, 0.5f);
			set(1, 0.f, 0.5f, 0.5f, 0.5f);
			set(2, 0.5f, 0.5f, 0.5f, 0.5f);
			break;

		default:    // quarters (4), grid beyond
		{
			const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
			const int rows = (count + columns - 1) / columns;
			const float w = 1.f / static_cast<float>(columns);
			const float h = 1.f / static_cast<float>(rows);

			for (int i = 0; i < count; ++i)
				set(i, static_cast<float>(i % columns) * w, static_cast<float>(i / columns) * h, w, h);
			break;
		}
		}
	}

	void Engine::SetSimulating(bool simulating, uint32_t camera)
	{
		simulating_ = simulating;
		simulation_camera_ = simulating ? camera : 0xFFFFFFFFu;

		if (simulating)
			UnpossessAll();

		// The default player shows the right camera at once : the forced one,
		// or again the camera it was asked for.
		if (PlayerController* player = GetDefaultPlayer())
			player->SetViewCamera(player->GetViewCamera());
	}

	void Engine::ProcessAutoPossess()
	{
		// Simulation : auto_possess_player is ignored (and stays pending for
		// the next real Play : auto_possess_done_ is not set).
		if (!current_level_ || simulating_)
			return;

		// OnPossessed may spawn actors : they wait for Level::Update.
		Level::IterationScope scope(*current_level_);

		for (Actor* actor : current_level_->GetActors())
		{
			if (!actor || actor->auto_possess_done_ || actor->auto_possess_player < 0)
				continue;

			actor->auto_possess_done_ = true;

			PlayerController* player = GetPlayer(actor->auto_possess_player);

			if (!player)
			{
				std::cerr << "[PLAYER] " << actor->GetTypeName() << " \"" << actor->object_id_
				          << "\" : auto possess player " << actor->auto_possess_player
				          << " does not exist (" << players_.size() << " player(s), "
				          << "see Engine::CreatePlayer)\n";
				continue;
			}

			player->Possess(actor);
		}
	}

	void Engine::UnpossessAll()
	{
		const std::vector<PlayerController*> players = players_;
		for (PlayerController* player : players)
			player->Unpossess();
	}

	void Engine::SetWindowHandle(LynxWindow *win)
	{
		win_ = win;
	}

	void Engine::SetRenderSize(int width, int height)
	{
		if (width <= 0 || height <= 0)
			return;

		HRL_WindowResizeCallback(width, height);
		WidgetSystem::SetRenderSize(width, height);
	}

	void Engine::GetRenderSize(int& width, int& height) const
	{
		WidgetSystem::GetRenderSize(width, height);
	}


	Level* Engine::CreateLevel(const char *file_name)
	{
		// Classes JavaScript (tous les .js de assets/ qui declarent une classe) : dans la factory avant le chargement.
		scripting::PrepareClasses();

		current_level_ = new Level();
		current_level_->LoadFromFile(file_name, this);

		// The sprites of the loaded actors must be drawn at once.
		RequestRenderRefresh();

		plugins::OnLevelLoaded();
		return current_level_;
	}

	void Engine::DeleteCurrentLevel()
	{
		// Le listener pointe sur un acteur du niveau : evite un pointeur pendant.
		UnattachAudioListener();
		// Les joueurs relachent leurs acteurs avant leur destruction.
		UnpossessAll();
		// Les widgets vivent avec le niveau (et peuvent venir de la DLL du jeu).
		ui::DestroyAllWidgets();
		delete current_level_;
		current_level_ = nullptr;
	}

	Level *Engine::GetCurrentLevel() const
	{
		return current_level_;
	}


	FactoryObject &Engine::GetFactory()
	{
		return factory_;
	}



	// ------------------------------------------------------------------
	// Singleton
	// ------------------------------------------------------------------

	void Engine::SetReleaseMode(bool release)
	{
		release_mode_ = release;
	}

	bool Engine::IsReleaseMode()
	{
		return release_mode_;
	}

	Engine* Engine::Create()
	{
		if (!instance_)
			instance_ = new Engine();
		return instance_;
	}

	Engine* Engine::Get()
	{
		return instance_;
	}

	void Engine::Destroy()
	{
		// instance_ stays valid during the destructor : the level / scripts
		// destroyed there may still call Engine::Get().
		delete instance_;
		instance_ = nullptr;
	}

	uint32_t Engine::CreateScene()
	{
		if (!scene_created_)
		{
			scene_ = HRL_CreateScene(false);

			// One unit in the whole engine : 1 world unit = 1 voxel (positions,
			// sizes, speeds, cameras, lights... are all in voxels).
			if (scene_ != HRL_INVALID_ID)
				HRL_SetVoxelPhysicalSize(scene_, 1.f);
			scene_created_ = true;

			// Viewport + default camera of every player (the default one at least).
			for (PlayerController* player : players_)
				player->CreateRenderObjects();

			UpdatePlayerViewports();
		}
		return scene_;
	}

	uint32_t Engine::GetScene()
	{
		return instance_ ? instance_->scene_ : HRL_INVALID_ID;
	}

	void SetViewportID(uint32_t)
	{
		// The viewports belong to the PlayerControllers.
	}

	uint32_t GetViewport()
	{
		const Engine* engine = Engine::Get();
		const PlayerController* player = engine ? engine->GetDefaultPlayer() : nullptr;
		return player ? player->GetViewportBackend() : HRL_INVALID_ID;
	}


	// LYNX_VERSION_* : CMakeLists.txt (target Lynx). "2026.1.0"
#ifndef LYNX_VERSION_YEAR
#define LYNX_VERSION_YEAR 0
#endif
#ifndef LYNX_VERSION_MAJOR
#define LYNX_VERSION_MAJOR 0
#endif
#ifndef LYNX_VERSION_MINOR
#define LYNX_VERSION_MINOR 0
#endif
	std::string GetEngineVersion()
	{
		return std::to_string(LYNX_VERSION_YEAR) + "." + std::to_string(LYNX_VERSION_MAJOR) + "." +
		       std::to_string(LYNX_VERSION_MINOR);
	}

	void AsyncFunc(float time, const std::function<void()>& callback)
	{
		//engine_->async_registered_.emplace(time, callback);
	}
}
