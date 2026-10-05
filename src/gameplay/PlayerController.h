#pragma once

// =============================================================================
// PlayerController (Unreal-like)
// -----------------------------------------------------------------------------
// A local player. Each one owns :
//   - its HRL viewport (split-screen : the engine lays the viewports out
//     automatically, see Engine::UpdatePlayerViewports) ;
//   - a default camera (the view when nothing else is chosen ; it is the
//     "gameplay camera" given to the game hooks for player 0) ;
//   - the actor it possesses (at most one ; an actor is possessed by at most
//     one player).
//
//   lynx::PlayerController* p2 = lynx::Engine::Get()->CreatePlayer();
//   p2->Possess(level->SpawnActor("Player"));
//
// Player 0 (the default player) is created with the engine and can not be
// destroyed : the editor / runtime render through its viewport.
//
// Auto possess : Actor::auto_possess_player = index of the player (-1 =
// disabled). At game start (and for actors spawned while playing), the actor
// is possessed by that player, like AutoPossessPlayer in Unreal.
//
// View : when the possessed actor has a CameraComponent (auto_activate), it
// becomes the view of this player. SetViewTarget / SetViewCamera choose
// another one.
// =============================================================================

#include "../core/Common.h"
#include <cstdint>

namespace lynx
{
	class Actor;
	class Engine;

	class LYNX_API PlayerController {
		friend class Engine;
		friend class Actor;
		explicit PlayerController(int id);
	public:
		~PlayerController();

		PlayerController(const PlayerController&) = delete;
		PlayerController& operator=(const PlayerController&) = delete;


		// ---------------------------------------------------------------------
		// Possession
		// ---------------------------------------------------------------------

		/**
		 * Possesses `a` (nullptr = Unpossess). The current actor is released
		 * first ; if `a` belongs to another player, that player releases it.
		 * Calls Actor::OnUnpossessed / Actor::OnPossessed.
		 */
		void Possess(Actor* a);
		void Unpossess();
		Actor* GetPossessedActor() const;

		template<typename T>
		T* GetPossessed() const { return dynamic_cast<T*>(GetPossessedActor()); }


		// ---------------------------------------------------------------------
		// Identity
		// ---------------------------------------------------------------------

		/** Position in Engine::GetPlayers() (0 = default player). -1 if removed. */
		int GetPlayerIndex() const;

		/** Unique id, never reused (unlike the index). */
		int GetId() const { return id_; }

		bool IsDefaultPlayer() const;


		// ---------------------------------------------------------------------
		// View
		// ---------------------------------------------------------------------

		/** The CameraComponent of `actor` becomes the view of this player. */
		void SetViewTarget(Actor* actor);

		/** HRL camera seen by this player. HRL_INVALID_ID = default camera. */
		void SetViewCamera(uint32_t camera);
		uint32_t GetViewCamera() const { return view_camera_; }

		/** Camera owned by the player (fov 20, looking down). */
		uint32_t GetDefaultCamera() const { return default_camera_; }


		// ---------------------------------------------------------------------
		// Viewport
		// ---------------------------------------------------------------------

		/**
		 * Fixed rectangle (normalized [0..1], (0,0) = top-left). The player
		 * leaves the automatic split-screen layout.
		 */
		void SetViewportSize(
			float x, float y,
			float _width, float _height
		);

		/** Back to the automatic split-screen layout. */
		void ResetViewportSize();
		bool HasCustomViewportSize() const { return custom_rect_; }

		/** Rectangle of the viewport (normalized : x, y, width, height). */
		vec4 GetViewportRect() const { return vec4(rect_[0], rect_[1], rect_[2], rect_[3]); }

		/** HRL viewport (HRL_INVALID_ID until the engine scene exists). */
		uint32_t GetViewportBackend() const { return viewport_; }

	private:
		// Engine side
		void CreateRenderObjects();     // once the scene exists
		void DestroyRenderObjects();
		void ApplyRect(float x, float y, float w, float h);

		// The possessed actor is being destroyed (no callback : it is dying).
		void ForgetActor();

		// Each game frame, before the actors tick (Engine) : calls
		// Actor::ProcessInput of the possessed actor (if its input is enabled).
		void ProcessInput();

		int id_ = 0;
		Actor* possessed_actor_ = nullptr;

		uint32_t viewport_ = 0xFFFFFFFFu;        // HRL_INVALID_ID
		uint32_t default_camera_ = 0xFFFFFFFFu;
		uint32_t view_camera_ = 0xFFFFFFFFu;

		bool custom_rect_ = false;
		float rect_[4] = { 0.f, 0.f, 1.f, 1.f };

		// Created while the game runs : removed by Engine::EndGame (like the
		// local players created during a Play In Editor session).
		bool created_during_play_ = false;
	};

	/** Number of local players (Engine::GetPlayerCount). */
	LYNX_API int GetPlayerCount();
}
