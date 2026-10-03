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

	// The window (and GLFW) are owned by the host application (Main.cpp), never
	// by lynx.dll. Kept as an opaque forward declaration for SetWindowHandle().
	class LynxWindow;

	class LYNX_API Engine {
		friend Engine* CreateEngine(const char*, bool);
		//friend void AsyncFunc(float time, std::function<void()> callback);
	private:
		Engine(const char* config_file, bool release);

	public:
		~Engine();


		void ProgressOneFrame(float dt);

		void StartGame();
		void EndGame();

		Level* CreateLevel(const char* file_name);
		void DeleteCurrentLevel();
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


		//async functions registered
		//std::unordered_map<float, std::function<void>()> async_registered_;
	};


	//singleton functions
	LYNX_API Engine* CreateEngine(const char* config_file, bool release);
	LYNX_API Engine* GetEngine();

	LYNX_API void SetSceneID(uint32_t id);
	LYNX_API uint32_t GetScene();

	LYNX_API void SetViewportID(uint32_t id);
	LYNX_API uint32_t GetViewport();

	//Utility functions
	LYNX_API std::string GetEngineVersion();

	//apelle un callback apres un certain temps donné
	LYNX_API void AsyncFunc(float time, std::function<void()> callback);
}