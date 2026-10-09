#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <string>

#include "Common.h"
#include "Factory.h"
#include "data/EventDispatcher.h"

namespace lynx
{
	class Level;
	class Actor;
	class PlayerController;

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
	//   lynx::Engine::Get()->GetDefaultPlayer()->GetViewportBackend();
	//                                         // the viewport to render (player 0)
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

		/**
		 * Rebuilds the draw lists of the renderer for the next `frames`
		 * frames (a temporary sprite mesh is created then deleted : HRL
		 * refreshes its lists on a mesh deletion). Called after a level load :
		 * sprites created during the load could stay invisible until the
		 * next deletion of a mesh.
		 */
		void RequestRenderRefresh(int frames = 10);
		void DeleteCurrentLevel();
		Level* GetCurrentLevel() const;

		void SetGlobalTimeDilatation(float td);
		void SetGlobalTimeDilatation(float dilation, float duration);
		float GetGlobalTimeDilatation() const;


		void SetWindowHandle(LynxWindow* win);

		/** true between StartGame() and EndGame(). */
		bool IsGameRunning() const { return game_tick_enabled_; }

		/**
		 * Simulation (editor "Simulate", like Unreal) : the game runs but
		 * nobody is possessed (auto_possess_player is ignored, no player
		 * input) and the default player keeps looking through `camera` (the
		 * host's camera), whatever the CameraComponents ask. Call it before
		 * StartGame() ; SetSimulating(false) after EndGame().
		 */
		void SetSimulating(bool simulating, uint32_t camera = 0xFFFFFFFFu);
		bool IsSimulating() const { return simulating_; }
		/** Camera forced on the default player while simulating (0xFFFFFFFF : none). */
		uint32_t GetSimulationCamera() const { return simulation_camera_; }

		/**
		 * Size of the render target in pixels (window, or the image of the
		 * editor viewport). Hosts call it on every resize : it resizes the
		 * renderer and the widgets (DPI scale) follow.
		 */
		void SetRenderSize(int width, int height);
		void GetRenderSize(int& width, int& height) const;


		// ---------------------------------------------------------------------
		// Local players (see gameplay/PlayerController.h)
		// ---------------------------------------------------------------------
		// Player 0 (default player) exists from Create() and can not be
		// destroyed. Each player owns its HRL viewport (created with the scene)
		// and the viewports share the screen automatically (split-screen).

		/** New local player (its viewport is created now if the scene exists). */
		PlayerController* CreatePlayer();

		/**
		 * Unpossesses and deletes `player` (`player` is dangling afterwards).
		 * false : not a player of this engine, or the default player.
		 */
		bool DestroyPlayer(PlayerController* player);

		/** nullptr if `index` is out of range. */
		PlayerController* GetPlayer(int index) const;
		PlayerController* GetDefaultPlayer() const;
		int GetPlayerCount() const;
		const std::vector<PlayerController*>& GetPlayers() const;

		/** Player that possesses `actor` (nullptr = none). */
		PlayerController* GetPlayerOf(const Actor* actor) const;

		/**
		 * Lays out the viewports of the players without a custom size :
		 * 1 = full screen, 2 = top / bottom, 3 = top + two halves at the
		 * bottom, 4 = quarters, more = grid. Called automatically.
		 */
		void UpdatePlayerViewports();

		/** Called with each new player (ex : the host adds its post process). */
		HEventDispatcher<PlayerController*> ED_player_created;


		FactoryObject& GetFactory();


	private:
		//gameplay
		bool game_tick_enabled_=false;

		// RequestRenderRefresh
		int render_refresh_frames_ = 0;

		// SetSimulating
		bool simulating_ = false;
		uint32_t simulation_camera_ = 0xFFFFFFFFu;
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

		//local players (owned ; [0] = default player)
		std::vector<PlayerController*> players_;
		int next_player_id_ = 0;

		// Actor::auto_possess_player of the actors not handled yet.
		void ProcessAutoPossess();
		// Every player releases its actor (before the actors are destroyed).
		void UnpossessAll();


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

	// Each PlayerController owns its viewport now : the host does not create
	// one anymore. Kept for old code, does nothing.
	[[deprecated("the viewports belong to the PlayerControllers (Engine::GetDefaultPlayer())")]]
	LYNX_API void SetViewportID(uint32_t id);

	// Viewport of the default player (player 0).
	LYNX_API uint32_t GetViewport();

	//Utility functions
	// "2026.1.0" : LYNX_VERSION_YEAR.LYNX_VERSION_MAJOR.LYNX_VERSION_MINOR (CMakeLists.txt).
	LYNX_API std::string GetEngineVersion();

	// ---- Mouse cursor / quit -------------------------------------------------
	// The window belongs to the host (runtime, editor) : the engine keeps the
	// request, the host applies it every frame. Both are reset by EndGame().

	/** Shows / hides the mouse cursor over the game window. JS : Engine.setMouseCursorVisible(b). */
	LYNX_API void SetMouseCursorVisible(bool visible);
	LYNX_API bool IsMouseCursorVisible();

	/**
	 * Quits the game : the shipped game closes its window, the editor stops
	 * Play (end of the current frame). JS : Engine.quit().
	 */
	LYNX_API void QuitGame();
	LYNX_API bool IsQuitRequested();
	LYNX_API void ClearQuitRequest();

	//apelle un callback apres un certain temps donné
	LYNX_API void AsyncFunc(float time, std::function<void()> callback);
}