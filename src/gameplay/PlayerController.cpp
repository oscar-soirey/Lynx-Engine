#include "PlayerController.h"

#include "Actor.h"
#include "CameraComponent.h"
#include "../core/Engine.h"
#include "../scripting/Private/ScriptSystem.h"

#include <hrl/hrl.h>

#include <iostream>

namespace lynx
{
	PlayerController::PlayerController(int id)
		: id_(id)
	{
	}

	PlayerController::~PlayerController()
	{
		// Released without callback : the engine unpossesses properly before
		// (EndGame, DestroyPlayer, level deletion).
		if (possessed_actor_)
			possessed_actor_->controller_ = nullptr;
		possessed_actor_ = nullptr;

		DestroyRenderObjects();
	}


	// -------------------------------------------------------------------------
	// Possession
	// -------------------------------------------------------------------------

	void PlayerController::Possess(Actor* a)
	{
		if (!a)
		{
			Unpossess();
			return;
		}

		if (a == possessed_actor_)
			return;

		// An actor has one controller : the other player lets it go first.
		if (a->controller_ && a->controller_ != this)
			a->controller_->Unpossess();

		Unpossess();

		possessed_actor_ = a;
		a->controller_ = this;
		a->OnPossessed(this);
		if (possessed_actor_ == a)
			scripting::CallPlayerEvent(a, "OnPossessed", this);

		// The camera of the actor becomes the view. Before BeginPlay the
		// component does it itself (CameraComponent::BeginPlay).
		if (possessed_actor_ == a)
		{
			if (auto* camera = a->GetComponent<CameraComponent>();
				camera && camera->auto_activate && camera->HasBegunPlay())
			{
				camera->ActivateFor(this);
			}
		}
	}

	void PlayerController::Unpossess()
	{
		if (!possessed_actor_)
			return;

		Actor* a = possessed_actor_;
		possessed_actor_ = nullptr;
		a->controller_ = nullptr;

		// The view stays where it is (like Unreal) until another one is chosen.
		a->OnUnpossessed(this);
		scripting::CallPlayerEvent(a, "OnUnpossessed", this);
	}

	Actor* PlayerController::GetPossessedActor() const
	{
		return possessed_actor_;
	}

	void PlayerController::ForgetActor()
	{
		possessed_actor_ = nullptr;
	}

	void PlayerController::ProcessInput()
	{
		// Only the possessed actor receives the input (like a Pawn) : C++,
		// then JavaScript (ProcessInput(player) of its class and scripts).
		if (possessed_actor_ && possessed_actor_->input_enabled_)
			possessed_actor_->ProcessInput();
		if (possessed_actor_ && possessed_actor_->input_enabled_)
			scripting::CallPlayerEvent(possessed_actor_, "ProcessInput", this);
	}


	// -------------------------------------------------------------------------
	// Identity
	// -------------------------------------------------------------------------

	int PlayerController::GetPlayerIndex() const
	{
		const Engine* engine = Engine::Get();
		if (!engine)
			return -1;

		const auto& players = engine->GetPlayers();
		for (size_t i = 0; i < players.size(); ++i)
		{
			if (players[i] == this)
				return static_cast<int>(i);
		}
		return -1;
	}

	bool PlayerController::IsDefaultPlayer() const
	{
		return GetPlayerIndex() == 0;
	}


	// -------------------------------------------------------------------------
	// View
	// -------------------------------------------------------------------------

	void PlayerController::SetViewTarget(Actor* actor)
	{
		if (!actor)
		{
			SetViewCamera(HRL_INVALID_ID);
			return;
		}

		if (auto* camera = actor->GetComponent<CameraComponent>())
		{
			camera->ActivateFor(this);
			return;
		}

		std::cout << "[PLAYER] SetViewTarget : " << actor->GetTypeName()
		          << " has no CameraComponent\n";
	}

	void PlayerController::SetViewCamera(uint32_t camera)
	{
		if (camera == HRL_INVALID_ID)
			camera = default_camera_;

		view_camera_ = camera;

		if (viewport_ != HRL_INVALID_ID && camera != HRL_INVALID_ID)
			HRL_SetViewportCamera(viewport_, camera);
	}


	// -------------------------------------------------------------------------
	// Viewport
	// -------------------------------------------------------------------------

	void PlayerController::SetViewportSize(
		float x, float y,
		float _width, float _height)
	{
		custom_rect_ = true;
		ApplyRect(x, y, _width, _height);

		// The other players share what is left.
		if (Engine* engine = Engine::Get())
			engine->UpdatePlayerViewports();
	}

	void PlayerController::ResetViewportSize()
	{
		if (!custom_rect_)
			return;

		custom_rect_ = false;

		if (Engine* engine = Engine::Get())
			engine->UpdatePlayerViewports();
	}

	void PlayerController::ApplyRect(float x, float y, float w, float h)
	{
		rect_[0] = x;
		rect_[1] = y;
		rect_[2] = w;
		rect_[3] = h;

		if (viewport_ != HRL_INVALID_ID)
			HRL_SetViewportRect(viewport_, x, y, w, h);
	}

	void PlayerController::CreateRenderObjects()
	{
		if (viewport_ != HRL_INVALID_ID)
			return;

		const uint32_t scene = Engine::GetScene();
		if (scene == HRL_INVALID_ID)
			return;   // done by Engine::CreateScene

		// Default camera : same as the old gameplay camera of the hosts.
		default_camera_ = HRL_CreateCamera(scene, HRL_PERSPECTIVE);
		if (default_camera_ != HRL_INVALID_ID)
		{
			HRL_SetCameraPerspectiveFov(default_camera_, 20.f);
			HRL_SetCameraRotation(default_camera_, 0.f, -90.f, 0.f);
		}

		if (view_camera_ == HRL_INVALID_ID)
			view_camera_ = default_camera_;

		viewport_ = HRL_CreateViewport(scene, view_camera_, rect_[0], rect_[1], rect_[2], rect_[3]);

		if (viewport_ == HRL_INVALID_ID)
			std::cerr << "[PLAYER] Could not create the viewport of player " << id_ << "\n";
	}

	void PlayerController::DestroyRenderObjects()
	{
		if (viewport_ != HRL_INVALID_ID && HRL_IsValidViewport(viewport_))
			HRL_DeleteViewport(viewport_);

		if (default_camera_ != HRL_INVALID_ID)
			HRL_DeleteCamera(default_camera_);

		viewport_ = HRL_INVALID_ID;
		default_camera_ = HRL_INVALID_ID;
		view_camera_ = HRL_INVALID_ID;
	}


	int GetPlayerCount()
	{
		const Engine* engine = Engine::Get();
		return engine ? engine->GetPlayerCount() : 0;
	}
}
