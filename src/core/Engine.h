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

		static Engine& Get()
		{
			static Engine engine_instance("", false); //Supprimer les parametres du constructeur
			return engine_instance;
		}


		void ProgressOneFrame(float dt);

		void StartGame();
		void EndGame();

		Level* CreateLevel(const char* file_name);
		void DeleteCurrentLevel();
		Level* GetCurrentLevel() const;

		static Level* OpenLevel(const char* file_name)
		{
			return Engine::Get().CreateLevel(file_name);
		}
		static Level* CurrentLevel()
		{
			return Engine::Get().GetCurrentLevel();
		}

		void SetGlobalTimeDilatation(float td);
		void SetGlobalTimeDilatation(float dilation, float duration);
		float GetGlobalTimeDilatation() const;

		//static void SetGlobalTimeDilatation(float td);

		FactoryObject& GetFactory();


	private:
		//gameplay
		bool game_tick_enabled_=false;
		// Dilatation "de fond" (ex : menu de selection). Ne change que par
		// SetGlobalTimeDilatation(float), jamais par un hit-stop.
		float base_time_dilatation_ = 1.f;

		// Dilatation temporaire (ex : hit-stop), active tant que le timer > 0.
		// Elle ne sauvegarde plus l'ancienne valeur : la dilatation effective
		// est recalculee a chaque frame, donc rien ne peut rester bloque.
		float temp_time_dilatation_ = 1.f;

		// Temps restant du hit-stop, en temps réel
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
	[[deprecated]] LYNX_API Engine* CreateEngine(const char* config_file, bool release);
	[[deprecated]] LYNX_API Engine* GetEngine();

	[[deprecated]] LYNX_API void SetSceneID(uint32_t id);
	[[deprecated]] LYNX_API uint32_t GetScene();

	[[deprecated]] LYNX_API void SetViewportID(uint32_t id);
	[[deprecated]] LYNX_API uint32_t GetViewport();

	//Utility functions
	LYNX_API std::string GetEngineVersion();
}