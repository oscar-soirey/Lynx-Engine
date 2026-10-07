#pragma once

/**
 * Camera of an actor : the view of the player who possesses the actor.
 *
 *     auto& cam = player->AddComponent<lynx::CameraComponent>();
 *     cam.offset = {0.f, 2.f, 80.f};
 *     cam.follow_speed = 3.f;          // 0 = stuck to the actor
 *
 * The camera is handled by the engine, not by the game's main : when a
 * PlayerController possesses the actor (Possess, auto_possess_player), the
 * camera of the actor is shown in the viewport of that player (split-screen :
 * each player sees through the camera of its own actor). Unpossess : the
 * player goes back to its default camera.
 *
 * An actor possessed by nobody : with auto_activate, its camera becomes the
 * view of the default player, unless that player already sees through the
 * camera of the actor it possesses. Activate() / ActivateFor() choose a camera
 * at any time (the last one wins). When the game stops, the host (editor /
 * runtime) takes its camera back.
 */

#include <cstdint>
#include <vector>

#include "Component.h"

namespace lynx
{
	class PlayerController;

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

		/**
		 * Retard du suivi (secondes de jeu) : la camera vise la position
		 * qu'avait l'acteur il y a `follow_delay` secondes (puis follow_speed
		 * lisse ce mouvement). 0 = aucun retard.
		 */
		float follow_delay = 0.f;

		/** Applique lynx::GetCameraShake() (ajoute a la position / au roll). */
		bool use_camera_shake = true;

		/** Actor possessed by nobody : becomes the view of the default player at game start. */
		bool auto_activate = true;

		/**
		 * Cette camera devient la vue du joueur qui possede l'acteur (joueur
		 * par defaut si personne ne le possede).
		 */
		void Activate();

		/** Cette camera devient la vue de `player` (nullptr = Activate()). */
		void ActivateFor(PlayerController* player);

		/** Vue d'au moins un joueur. */
		bool IsActive() const;

		/** Place la camera sur sa cible, sans lissage. */
		void SnapToTarget();

		/** Id HRL de la camera. */
		uint32_t GetCameraId() const { return camera_; }

		/**
		 * Position actuelle de la camera (suivi, retard et lissage compris,
		 * sans le camera shake). Sert aux blends entre cameras (plugins).
		 */
		vec3 GetCurrentLocation() const { return current_; }

	protected:
		void OnAttach() override;
		void BeginPlay() override;
		void Update(float dt) override;
		void LateUpdate(float dt) override;
		void EndPlay() override;

	private:
		bool EnsureCamera();
		void ApplySettings();
		void ApplyTransform(float x, float y, float z, float roll);

		/** Position de l'acteur (+ offset) il y a `follow_delay` secondes. */
		vec3 DelayedTarget(const vec3& target, float dt);

		uint32_t camera_ = 0xFFFFFFFFu;
		vec3 current_{0.f};
		bool has_current_ = false;

		// follow_delay : positions passees (temps de jeu, cible), les plus
		// anciennes d'abord.
		struct TargetSample { float time; vec3 target; };
		std::vector<TargetSample> history_;
		float history_time_ = 0.f;
	};
}
