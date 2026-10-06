#pragma once

/**
 * Humanoid : un personnage qui marche, saute et tombe (comme le Pawn du jeu,
 * ou le Character d'Unreal), construit sur les composants du moteur :
 *   - ColliderComponent : sa boite (bloquante, movable). Les deplacements
 *     passent par MoveAndCollide (voxels + autres colliders bloquants,
 *     OnHit), les sondes de sol / plafond / marches par IsBlockedAt ;
 *   - EditorIconComponent : clic dans le viewport de l'editeur.
 *
 * Il ne lit AUCUN input : un PlayerController (ProcessInput), une IA ou un
 * script appelle ses fonctions de mouvement :
 *
 *     class Hero : public lynx::Humanoid
 *     {
 *         lynx::InputAxis1D move_{"MoveRight"};
 *         lynx::InputAction jump_{"Jump"};
 *
 *         void ProcessInput() override
 *         {
 *             Move(move_.GetValue());
 *             if (jump_.IsPressed()) Jump();
 *             if (jump_.IsReleased()) StopJumping();
 *         }
 *     };
 *
 *     // JS : class Hero extends Humanoid { ProcessInput() { this.Move(1); } }
 *
 * Mouvement :
 *   Move(direction)    -1..1, garde jusqu'au prochain appel (0 = s'arreter) ;
 *                      acceleration / deceleration au sol, controle en l'air
 *   Jump()             saute (max_jump_count sauts avant de retoucher le sol) ;
 *                      tant que le saut est tenu (StopJumping pas appele,
 *                      jump_hold_time), la gravite est reduite : saut variable
 *   Launch(x, y)       impulsion (knockback, trampoline...)
 *   gravite, vitesse de chute maximale, marches (max_step_height) a la
 *   montee comme a la descente.
 *
 * Evenements (C++ virtuels, et memes noms en JS) : OnLanded(), OnJumped(n)
 * (n = numero du saut, 1 = depuis le sol ; JS : OnJumped()), OnHit (voir Actor).
 *
 * Unites : monde (comme transform.location), vitesses par seconde.
 */

#include "Actor.h"

namespace lynx
{
	class ColliderComponent;

	class LYNX_API Humanoid : public Actor
	{
	public:
		Humanoid();

		// ====================================================================
		// Reglages (proprietes : Details, .level, JS this.move_speed...)
		// ====================================================================

		/** Taille de la boite de collision (monde, avant la scale de l'acteur). */
		vec2 collider_size{1.f, 2.f};

		/** Vitesse de marche maximale. */
		float move_speed = 10.f;

		/** Au sol : vers move_speed / vers 0 / en changeant de sens. */
		float ground_acceleration = 170.f;
		float ground_deceleration = 200.f;
		float direction_change_acceleration = 220.f;

		/** En l'air (multipliees par air_control : 0 = aucun controle, 1 = total). */
		float air_acceleration = 135.f;
		float air_deceleration = 120.f;
		float air_control = 1.f;

		float jump_speed = 18.f;
		float gravity = 55.f;
		/** Vitesse de chute maximale (0 = aucune limite). */
		float max_fall_speed = 60.f;

		/** Duree pendant laquelle tenir le saut reduit la gravite (saut variable). */
		float jump_hold_time = 0.32f;
		float jump_hold_gravity_scale = 0.25f;

		/** Sauts possibles avant de retoucher le sol (2 = double saut). */
		int max_jump_count = 1;

		/** Hauteur de marche franchie sans sauter (monde ; 0 = aucune). */
		float max_step_height = 0.4f;

		/** Retourne l'acteur (signe de transform.scale.x) dans le sens de Move. */
		bool face_movement_direction = true;

		/** false : plus de mouvement ni de gravite (cinematique, mort...). */
		bool movement_enabled = true;

		/** Filtres du collider (voir ColliderComponent::layer / mask). */
		int collision_layer = 1;
		int collision_mask = -1;

		/** Dessine la boite en jeu (elle l'est toujours dans l'editeur). */
		bool show_collider = false;

		// ====================================================================
		// Mouvement (aucune gestion d'input ici)
		// ====================================================================

		/** -1..1 : direction de marche, gardee jusqu'au prochain appel. */
		void Move(float direction);

		/** Saute si un saut est disponible. @return true si le saut a eu lieu. */
		bool Jump();

		/** Le saut n'est plus tenu : la gravite normale reprend. */
		void StopJumping();

		/** Impulsion ; override : remplace la composante au lieu de l'ajouter. */
		void Launch(float x, float y, bool override_x = false, bool override_y = false);

		/** Vitesse et direction de marche a 0. */
		void StopMovement();

		void SetVelocity(float x, float y) { velocity_ = { x, y }; }
		vec2 GetVelocity() const { return velocity_; }

		/** Direction donnee a Move (-1..1). */
		float GetMoveInput() const { return move_input_; }

		bool IsGrounded() const { return grounded_; }
		bool IsFalling() const { return !grounded_ && velocity_.y < 0.f; }
		bool IsFacingRight() const { return facing_right_; }
		int GetJumpCount() const { return jump_count_; }

		/** Un sol bloquant a moins de `distance` (monde) sous la boite. */
		bool IsGroundWithinDistance(float distance) const;

		ColliderComponent* GetCollider() const { return collider_; }

		// ====================================================================

		void Init() override;
		void Update(double dt) override;
		void Tick(double dt) override;
		void StartGame() override;
		void EndGame() override;

	protected:
		/** Retouche le sol apres avoir ete en l'air. */
		virtual void OnLanded() {}
		/** Un saut vient d'avoir lieu (`jump_index` : 1 = depuis le sol, 2 = double saut...). */
		virtual void OnJumped(int jump_index) {}

		/** Acceleration + gravite (vitesse), puis IntegrateMovement. */
		virtual void UpdateMovement(float dt);
		/** Applique la vitesse : collisions, marches, sol. */
		void IntegrateMovement(float dt);

		/** Copie les reglages dans le collider. */
		void SyncCollider();

	private:
		/** Monte une marche de `dx` (au sol, bloque sur X). */
		bool TryStepUp(float dx);
		/** Au sol avant le mouvement : redescend une marche au lieu de decoller. */
		bool SnapToFloor();
		/** Distance de contact (la boite "touche" a moins de ca). */
		float ContactDistance() const;

		void FireLanded();

		ColliderComponent* collider_ = nullptr;

		vec2 velocity_{0.f, 0.f};
		float move_input_ = 0.f;

		bool grounded_ = false;
		bool jump_held_ = false;
		float jump_hold_timer_ = 0.f;
		int jump_count_ = 0;
		bool facing_right_ = true;

		bool playing_ = false;
	};
}
