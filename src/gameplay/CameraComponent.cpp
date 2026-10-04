#include "CameraComponent.h"

#include "Actor.h"
#include "CameraShake.h"
#include "../core/Engine.h"

#include <hrl/hrl.h>

#include <cmath>
#include <vector>

namespace lynx
{
	namespace
	{
		constexpr uint32_t kInvalid = 0xFFFFFFFFu;

		// HRL n'a pas (dans l'API utilisee ici) de suppression de camera : les
		// cameras des composants detruits sont reutilisees.
		std::vector<uint32_t>& FreeCameras()
		{
			static std::vector<uint32_t> cameras;
			return cameras;
		}

		uint32_t g_active_camera = kInvalid;
	}

	CameraComponent::CameraComponent() = default;

	CameraComponent::~CameraComponent()
	{
		if (camera_ == kInvalid)
			return;

		if (g_active_camera == camera_)
			g_active_camera = kInvalid;

		FreeCameras().push_back(camera_);
	}

	bool CameraComponent::EnsureCamera()
	{
		if (camera_ != kInvalid)
			return true;

		auto& free_cameras = FreeCameras();

		if (!free_cameras.empty())
		{
			camera_ = free_cameras.back();
			free_cameras.pop_back();
		}
		else
		{
			const uint32_t scene = GetScene();

			if (!HRL_IsValidScene(scene))
				return false;

			camera_ = HRL_CreateCamera(scene, HRL_PERSPECTIVE);
		}

		if (camera_ == kInvalid)
			return false;

		ApplySettings();
		return true;
	}

	void CameraComponent::ApplySettings()
	{
		HRL_SetCameraPerspectiveFov(camera_, fov);
		HRL_SetCameraNearPlane(camera_, near_plane);
		HRL_SetCameraFarPlane(camera_, far_plane);
	}

	void CameraComponent::ApplyTransform(float x, float y, float z, float roll)
	{
		HRL_SetCameraLocation(camera_, x, y, z);
		HRL_SetCameraRotation(camera_, rotation.x, rotation.y, rotation.z + roll);
	}

	void CameraComponent::OnAttach()
	{
		if (EnsureCamera())
			SnapToTarget();
	}

	void CameraComponent::SnapToTarget()
	{
		Actor* owner = GetOwner();

		if (!owner || !EnsureCamera())
			return;

		current_ = owner->transform.location + offset;
		has_current_ = true;

		ApplyTransform(current_.x, current_.y, current_.z, 0.f);
	}

	void CameraComponent::Activate()
	{
		if (!EnsureCamera())
			return;

		HRL_SetViewportCamera(GetViewport(), camera_);
		g_active_camera = camera_;
	}

	bool CameraComponent::IsActive() const
	{
		return camera_ != kInvalid && g_active_camera == camera_;
	}

	void CameraComponent::BeginPlay()
	{
		SnapToTarget();

		if (auto_activate)
			Activate();
	}

	void CameraComponent::EndPlay()
	{
		// La vue est rendue a l'hote (editeur / runtime) : il change lui-meme
		// la camera de la vue, on oublie seulement l'etat actif.
		if (IsActive())
			g_active_camera = kInvalid;
	}

	void CameraComponent::Update(float)
	{
		if (!EnsureCamera())
			return;

		// Champs modifiables a tout moment.
		ApplySettings();

		// Hors jeu (editeur) : la camera reste sur l'acteur.
		if (!HasBegunPlay())
			SnapToTarget();
	}

	void CameraComponent::Tick(float dt)
	{
		Actor* owner = GetOwner();

		if (!owner || !EnsureCamera())
			return;

		const vec3 target = owner->transform.location + offset;

		if (!has_current_ || follow_speed <= 0.f)
		{
			current_ = target;
			has_current_ = true;
		}
		else
		{
			const float k = 1.f - std::exp(-follow_speed * dt);
			current_ += (target - current_) * k;
		}

		float x = current_.x;
		float y = current_.y;
		float roll = 0.f;

		if (use_camera_shake)
			GetCameraShake().Update(dt, x, y, roll);

		ApplyTransform(x, y, current_.z, roll);
	}
}
