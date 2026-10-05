#pragma once

/**
 * Camera qui suit l'acteur.
 *
 *     auto& cam = player->AddComponent<lynx::CameraComponent>();
 *     cam.offset = {0.f, 2.f, 80.f};
 *     cam.follow_speed = 3.f;          // 0 = colle a l'acteur
 *
 * Au lancement du jeu (BeginPlay), une camera avec auto_activate devient la
 * camera de la vue. Activate() la choisit a tout moment ; la derniere activee
 * gagne. A l'arret du jeu, l'hote (editeur / runtime) reprend sa camera.
 *
 * Si le jeu deplace deja sa camera avec LynxGame_UpdateGameplayCamera,
 * utiliser l'un ou l'autre (sinon la camera shake avance deux fois par frame).
 */

#include <cstdint>

#include "Component.h"

namespace lynx
{
	class LYNX_API CameraComponent : public Component
	{
	public:
		CameraComponent();
		~CameraComponent() override;

		/** Position de la camera par rapport a l'acteur. */
		vec3 offset{0.f, 0.f, 80.f};

		/** Rotation (pitch, yaw, roll en degres), comme HRL_SetCameraRotation. */
		vec3 rotation{0.f, -90.f, 0.f};

		float fov = 20.f;
		float near_plane = 0.1f;
		float far_plane = 10000.f;   // voxels

		/** Lissage du suivi (par seconde). 0 = suit l'acteur exactement. */
		float follow_speed = 0.f;

		/** Applique lynx::GetCameraShake() (ajoute a la position / au roll). */
		bool use_camera_shake = true;

		/** Devient la camera de la vue au lancement du jeu. */
		bool auto_activate = true;

		/** Cette camera devient celle de la vue. */
		void Activate();
		bool IsActive() const;

		/** Place la camera sur sa cible, sans lissage. */
		void SnapToTarget();

		/** Id HRL de la camera. */
		uint32_t GetCameraId() const { return camera_; }

	protected:
		void OnAttach() override;
		void BeginPlay() override;
		void Update(float dt) override;
		void Tick(float dt) override;
		void EndPlay() override;

	private:
		bool EnsureCamera();
		void ApplySettings();
		void ApplyTransform(float x, float y, float z, float roll);

		uint32_t camera_ = 0xFFFFFFFFu;
		vec3 current_{0.f};
		bool has_current_ = false;
	};
}
