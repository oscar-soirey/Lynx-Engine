#include "Engine.h"
#include "Engine.h"

#include <glfw/glfw3.h>
#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include "Filesystem.h"
#include "Level.h"
#include "../gameplay/Actor.h"
#include "../gameplay/Private/InputManager.h"
#include "audio/AudioCommon.h"
#include "Private/SystemModule.h"


namespace lynx
{
	LynxWindow::LynxWindow(const char* title)
	{
		glfwInit();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		win_ = glfwCreateWindow(1280,720, title, nullptr, nullptr);

		glfwMakeContextCurrent((GLFWwindow*)win_);

		// désactiver la v-sync
		glfwSwapInterval(0);

		HRL_InitContext(1280, 720, (void*)glfwGetProcAddress);
	}

	LynxWindow::~LynxWindow()
	{
		glfwDestroyWindow((GLFWwindow*)win_);
		glfwTerminate();
	}

	void LynxWindow::InitHRL()
	{
	}

	bool LynxWindow::ShouldClose() const
	{
		return glfwWindowShouldClose((GLFWwindow*)win_);
	}

	void LynxWindow::PollEvents()
	{
		glfwPollEvents();
	}

	void LynxWindow::SwapBuffers()
	{
		glfwSwapBuffers((GLFWwindow*)win_);
	}

	void *LynxWindow::GetWindowHandle() const
	{
		return win_;
	}






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
			PollInputDevices();

			for (const auto& a : current_level_->GetActors())
			{
				a->ProcessInput();
				a->Tick(game_dt);
			}

			InputTick();
		}

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
	}

	void Engine::EndGame()
	{
		game_tick_enabled_ = false;
	}

	void Engine::SetWindowHandle(LynxWindow *win)
	{
		win_ = win;
	}


	void Engine::CreateLevel(const char *file_name)
	{
		current_level_ = new Level();
		current_level_->LoadFromFile(file_name, this);
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
}
