#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <string>

#include "Common.h"
#include "Factory.h"

namespace lynx
{
	class Level;

	class LYNX_API LynxWindow {
	public:
		LynxWindow(const char* title);
		~LynxWindow();

		void InitHRL();

		void PollEvents();
		void SwapBuffers();
		bool ShouldClose() const;

		void* GetWindowHandle() const;

		void SetIcon(const char* path){}
		void SetTitle(const char* path){}

	private:
		void* win_=nullptr;
	};

	class LYNX_API Engine {
		friend Engine* CreateEngine(const char*, bool);
	private:
		Engine(const char* config_file, bool release);

	public:
		~Engine();


		void ProgressOneFrame(float dt);

		void StartGame();
		void EndGame();

		Level* CreateLevel(const char* file_name);
		void DestroyCurrentLevel();
		Level* GetCurrentLevel() const;

		void SetGlobalTimeDilatation(float td);
		void SetGlobalTimeDilatation(float dilation, float duration);
		float GetGlobalTimeDilatation() const;


		void SetWindowHandle(LynxWindow* win);


		FactoryObject& GetFactory();


	private:
		//gameplay
		bool game_tick_enabled_=false;
		float global_time_dilatation_=1.f;

		float time_dilatation_end_time_ = 0.0f;
		float previous_time_dilatation_ = 1.0f;

		// Temps restant avant restauration, en temps réel
		float time_dilatation_timer_ = 0.0f;

		//game
		Level* current_level_=nullptr;

		//factory
		FactoryObject factory_{};


		//Can be nullptr if game is not rendered by LynxWindow
		LynxWindow* win_=nullptr;
	};


	//singleton
	LYNX_API Engine* CreateEngine(const char* config_file, bool release);
	LYNX_API Engine* GetEngine();

	LYNX_API void SetSceneID(uint32_t id);
	LYNX_API uint32_t GetScene();

	//Utility functions
	LYNX_API std::string GetEngineVersion();
}