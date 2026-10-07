#include "CameraComponent.h"

#include "Actor.h"
#include "CameraShake.h"
#include "PlayerController.h"
#include "../core/Engine.h"

#include <hrl/hrl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

		// Les joueurs qui regardent `camera` reprennent leur camera par defaut.
		void ReleaseViews(uint32_t camera)
		{
			Engine* engine = Engine::Get();
			if (!engine || camera == kInvalid)
				return;

			for (PlayerController* player : engine->GetPlayers())
			{
				if (player && player->GetViewCamera() == camera)
					player->SetViewCamera(kInvalid);
			}
		}
	}

	CameraComponent::CameraComponent() = default;

	CameraComponent::~CameraComponent()
	{
		if (camera_ == kInvalid)
			return;

		// La camera est reutilisee par un autre composant : aucun joueur ne
		// doit continuer a la regarder.
		ReleaseViews(camera_);

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
			const uint32_t scene = Engine::GetScene();

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

		// Plus de retard a rattraper : la camera repart de la position actuelle.
		history_.clear();
		history_time_ = 0.f;

		ApplyTransform(current_.x, current_.y, current_.z, 0.f);
	}

	void CameraComponent::Activate()
	{
		ActivateFor(nullptr);
	}

	void CameraComponent::ActivateFor(PlayerController* player)
	{
		if (!player)
		{
			Actor* owner = GetOwner();
			player = owner ? owner->GetController() : nullptr;
		}

		// Acteur possede par personne : joueur par defaut (comportement
		// d'avant les PlayerController).
		if (!player && Engine::Get())
			player = Engine::Get()->GetDefaultPlayer();

		if (!player || !EnsureCamera())
			return;

		player->SetViewCamera(camera_);
	}

	bool CameraComponent::IsActive() const
	{
		if (camera_ == kInvalid || !Engine::Get())
			return false;

		for (const PlayerController* player : Engine::Get()->GetPlayers())
		{
			if (player && player->GetViewCamera() == camera_)
				return true;
		}
		return false;
	}

	void CameraComponent::BeginPlay()
	{
		SnapToTarget();

		Actor* owner = GetOwner();

		// The actor is possessed : this camera is the view of its player.
		if (PlayerController* player = owner ? owner->GetController() : nullptr)
		{
			ActivateFor(player);
			return;
		}

		// Not possessed : the default player only, and only when it does not
		// already look through the camera of the actor it possesses.
		if (!auto_activate || !Engine::Get())
			return;

		PlayerController* player = Engine::Get()->GetDefaultPlayer();
		if (!player)
			return;
		if (Actor* pawn = player->GetPossessedActor())
			if (pawn->GetComponent<CameraComponent>())
				return;

		ActivateFor(player);
	}

	void CameraComponent::EndPlay()
	{
		// La vue est rendue a l'hote (editeur / runtime) : il change lui-meme
		// la camera du joueur par defaut. Les joueurs qui la regardent encore
		// reprennent leur camera par defaut.
		ReleaseViews(camera_);
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

	void CameraComponent::LateUpdate(float dt)
	{
		// Le suivi se fait apres tout le gameplay (comme un LateUpdate Unity) :
		// dans Tick, selon l'ordre des composants, la camera pouvait suivre la
		// position de la frame precedente -> une frame de retard, saccades.
		if (!HasBegunPlay() || !tick_enabled)
			return;

		Actor* owner = GetOwner();

		if (!owner || !EnsureCamera())
			return;

		const vec3 target = DelayedTarget(owner->transform.location + offset, dt);

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

	vec3 CameraComponent::DelayedTarget(const vec3& target, float dt)
	{
		if (follow_delay <= 0.f)
		{
			history_.clear();
			history_time_ = 0.f;
			return target;
		}

		history_time_ += std::max(dt, 0.f);
		history_.push_back({ history_time_, target });

		const float wanted = history_time_ - follow_delay;

		// Pas encore assez d'historique : la plus ancienne position connue.
		if (history_.front().time >= wanted)
			return history_.front().target;

		// Garde un echantillon avant `wanted` (pour interpoler), oublie le reste.
		size_t first = 0;
		while (first + 1 < history_.size() && history_[first + 1].time <= wanted)
			++first;
		if (first > 0)
			history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(first));

		if (history_.size() < 2)
			return history_.front().target;

		const TargetSample& a = history_[0];
		const TargetSample& b = history_[1];
		const float span = b.time - a.time;
		const float t = span > 1e-6f ? std::clamp((wanted - a.time) / span, 0.f, 1.f) : 1.f;
		return a.target + (b.target - a.target) * t;
	}
}
