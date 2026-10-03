#include "Engine.h"

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include "Filesystem.h"
#include "Level.h"
#include "../gameplay/Actor.h"
#include "../gameplay/Private/InputManager.h"
#include "audio/AudioCommon.h"
#include "audio/AudioListener.h"
#include "Private/SystemModule.h"


namespace lynx
{
	Engine::Engine(const char *config_file, bool release)
	{
		fs::AssetSource asset_src = fs::AssetSource::Directory;
		if (release) asset_src = fs::AssetSource::Archive;
		fs::Init(asset_src);
		InitializeAudio();
	}

	Engine::~Engine()
	{
		delete current_level_;
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
			{
				time_dilatation_timer_ = 0.0f;

				global_time_dilatation_ =
						previous_time_dilatation_;
			}
		}


		// ========================================================
		// Dilated delta time
		// ========================================================

		const float game_dt = dt * global_time_dilatation_;


		// ========================================================
		// Physics & gameplay
		// ========================================================

		for (const auto& a : current_level_->GetActors())
		{
			a->Update(game_dt);
		}


		if (game_tick_enabled_)
		{
			for (const auto& a : current_level_->GetActors())
			{
				a->ProcessInput();
				a->Tick(game_dt);
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
		global_time_dilatation_ = dilation;
		time_dilatation_timer_ = 0.0f;
	}

	void Engine::SetGlobalTimeDilatation(
			float dilation,
			float duration)
	{
		previous_time_dilatation_ = global_time_dilatation_;

		global_time_dilatation_ = dilation;

		time_dilatation_timer_ = duration;
	}

	float Engine::GetGlobalTimeDilatation() const
	{
		return global_time_dilatation_;
	}

	void Engine::StartGame()
	{
		game_tick_enabled_ = true;
		for (auto& a: current_level_->GetActors())
		{
			a->StartGame();
		}
	}

	void Engine::EndGame()
	{
		game_tick_enabled_ = false;
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
