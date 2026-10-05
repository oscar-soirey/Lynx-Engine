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

	// -------------------------------------------------------------------------
	// Engine (singleton)
	// -------------------------------------------------------------------------
	//   lynx::Engine::SetReleaseMode(true);   // optional, BEFORE Create()
	//   lynx::Engine::Create();               // fs, audio, scripting, factory
	//   ... HRL_Init + HRL_InitContext ...
	//   lynx::Engine::Get()->CreateScene();   // the HRL scene, owned by the engine
	//   lynx::Engine::GetScene();             // anywhere after that
	//   ...
	//   lynx::Engine::Destroy();
	//
	// The engine exists before the HRL context (the game DLL fills its factory
	// first) : that is why the scene has its own call.
	// -------------------------------------------------------------------------

	class LYNX_API Engine {
	private:
		Engine();
		~Engine();

	public:
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		// Asset source of the next Create() : false (default) = the assets/
		// folder, true = the packed archive (shipped game).
		static void SetReleaseMode(bool release);
		static bool IsReleaseMode();

		// Creates the engine (once ; a second call returns the same one).
		static Engine* Create();

		// nullptr before Create() / after Destroy().
		static Engine* Get();

		// Deletes the engine (level, scripts, audio, HRL). Get() is nullptr after.
		static void Destroy();

		// Creates the HRL scene. Needs HRL_Init + HRL_InitContext first ; once.
		// Returns the scene id (same as GetScene()).
		uint32_t CreateScene();

		// The HRL scene (HRL_INVALID_ID = 0xFFFFFFFF before CreateScene()).
		static uint32_t GetScene();


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

		//HRL scene (CreateScene)
		uint32_t scene_ = 0xFFFFFFFFu;   // HRL_INVALID_ID
		bool scene_created_ = false;

		static Engine* instance_;
		static bool release_mode_;


		//async functions registered
		//std::unordered_map<float, std::function<void>()> async_registered_;
	};


	// Old singleton functions : kept only so that game projects still compile
	// (with a warning at each use). Replace them, then delete these lines.
	[[deprecated("use lynx::Engine::Get()")]]
	inline Engine* GetEngine() { return Engine::Get(); }

	[[deprecated("use lynx::Engine::GetScene()")]]
	inline uint32_t GetScene() { return Engine::GetScene(); }

	LYNX_API void SetViewportID(uint32_t id);
	LYNX_API uint32_t GetViewport();

	//Utility functions
	LYNX_API std::string GetEngineVersion();

	//apelle un callback apres un certain temps donné
	LYNX_API void AsyncFunc(float time, std::function<void()> callback);
}