#include "Engine.h"

#include <algorithm>

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include "Filesystem.h"
#include "Level.h"
#include "../gameplay/Actor.h"
#include "../gameplay/Private/InputManager.h"
#include "audio/AudioCommon.h"
#include "audio/AudioListener.h"
#include "Private/SystemModule.h"
#include "../gameplay/Private/ECS.h"
#include "../scripting/Private/ScriptSystem.h"


namespace lynx
{
	Engine::Engine(const char *config_file, bool release)
	{
		fs::AssetSource asset_src = fs::AssetSource::Directory;
		if (release) asset_src = fs::AssetSource::Archive;
		fs::Init(asset_src);
		InitializeAudio();
		scripting::Init();
	}

	Engine::~Engine()
	{
		delete current_level_;
		current_level_ = nullptr;
		// apres le niveau : les acteurs detruits liberent leurs scripts
		scripting::Shutdown();
		ShutdownAudio();
		HRL_Shutdown();
	}

	void Engine::ProgressOneFrame(float dt)
	{
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


		// ========================================================
		// Physics & gameplay
		// ========================================================

		for (const auto& a : current_level_->GetActors())
		{
			a->Update(game_dt);
		}

		if (current_level_)
		{
			// Les spawns faits par les composants sont differes jusqu'a
			// Level::Update (voir Level::IterationScope).
			Level::IterationScope scope(*current_level_);
			ecs::Update(game_dt);
		}


		if (game_tick_enabled_)
		{
			for (const auto& a : current_level_->GetActors())
			{
				if (a->input_enabled_)
					a->ProcessInput();
				a->Tick(game_dt);
			}

			if (current_level_)
			{
				// Les spawns faits par les scripts / systemes sont differes
				// jusqu'a Level::Update (voir Level::IterationScope).
				Level::IterationScope scope(*current_level_);

				// Composants : BeginPlay des nouveaux, puis Tick (scripts JS,
				// Velocity, Lifetime, composants du jeu...)
				ecs::Tick(game_dt);
			}

			InputTick();
		}

		if (current_level_)
		{
			current_level_->Update();
		}

		// Le listener suit l'acteur attache (AttachAudioListener) : sans cet appel,
		// il reste a (0,0,0) et il n'y a aucune attenuation.
		UpdateAudioListener();

		HRL_BeginFrame();
		HRL_EndFrame();
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
		for (auto& a: current_level_->GetActors())
		{
			a->StartGame();
		}

		// apres StartGame : les composants voient les acteurs deja initialises
		ecs::BeginPlay();
	}

	void Engine::EndGame()
	{
		game_tick_enabled_ = false;

		ecs::EndPlay();

		for (auto& a: current_level_->GetActors())
		{
			a->EndGame();
		}
	}

	void Engine::SetWindowHandle(LynxWindow *win)
	{
		win_ = win;
	}


	Level* Engine::CreateLevel(const char *file_name)
	{
		// Classes JavaScript (assets/classes/) : dans la factory avant le chargement.
		scripting::PrepareClasses();

		current_level_ = new Level();
		current_level_->LoadFromFile(file_name, this);
		return current_level_;
	}

	void Engine::DeleteCurrentLevel()
	{
		// Le listener pointe sur un acteur du niveau : evite un pointeur pendant.
		UnattachAudioListener();
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



	static Engine* engine_;
	Engine *CreateEngine(const char* config_file, bool release)
	{
		engine_ = new Engine(config_file, release);
		return engine_;
	}
	Engine* GetEngine()
	{
		return engine_;
	}


	static uint32_t scene_id_;
	void SetSceneID(uint32_t id)
	{
		scene_id_ = id;
	}
	uint32_t GetScene()
	{
		return scene_id_;
	}

	static uint32_t viewport_id_;
	void SetViewportID(uint32_t id)
	{
		viewport_id_ = id;
	}
	uint32_t GetViewport()
	{
		return viewport_id_;
	}


	void AsyncFunc(float time, const std::function<void()>& callback)
	{
		//engine_->async_registered_.emplace(time, callback);
	}
}
