#pragma once

#include <memory>

#include "../GameClasses.h"
#include "../Particles/BloodParticles.h"
#include "../Particles/DeathParticles.h"
#include "BehaviorTree.h"
#include "../Particles/PlayerHurtParticles.h"

class Ennemy : public Pawn {
public:
	int life=100;

	Ennemy();
	void Init() override;

	void Tick(double _dt) override;

	void Hurt(Actor *instigator, float amount) override;

	// Behavior tree
	virtual void ProcessBehavior(float delta_time);

	void SetTarget(Actor *new_target) { target_ = new_target; }
	Actor *GetTarget() const { return target_; }

protected:

	virtual void Death() {}

	// A surcharger dans les classes filles pour construire l'arbre
	virtual void BuildBehaviorTree() {}

	// Conditions / actions utilisables dans les noeuds
	bool HasTarget() const;
	float DistanceToTarget() const;
	bool CanSeeTarget() const;
	bt::Status MoveToTarget(float delta_time, float acceptance_radius = 10.f);

	// Saut anti-blocage
	void Jump(const char *reason = "");                                          // saut de l'ennemi
	void UpdateStuckDetection(float delta_time, float direction);
	virtual void OnJump() {}                              // hook : decollage (animation, son...)
	virtual void OnLand() {}                              // hook : atterrissage
	bool IsJumping() const { return is_jumping_; }        // vrai du decollage jusqu'a l'atterrissage
	void UpdateJumpState(float delta_time);

	// Obstacle devant l'ennemi dans la direction donnee (+1 / -1)
	bool HasObstacleAhead(float direction) const;
	// Vrai si l'ennemi doit sauter maintenant pour franchir un obstacle
	bool ShouldJumpOverObstacle() const;

	// Attaque
	bool CanAttackTarget() const;                 // cible visible, a portee, cooldown fini
	bool IsAttacking() const { return is_attacking_; }
	bt::Status Attack(float delta_time);          // Running pendant l'attaque, Success a la fin
	void CancelAttack();                          // interrompt l'attaque en cours (ex: ennemi blesse)

	// Hooks pour les classes filles (animation, son...)
	virtual void OnAttackStart() {}
	virtual void OnAttackEnd() {}

	// Vision
	bool HasLineOfSight() const;

	float knockback_intensity_ = 20.0f;

	// IA
	bool process_behavior_tick_=true;
	Actor *target_ = nullptr;
	float see_radius_ = 300.f;

	// Parametres du saut anti-blocage
	float jump_velocity_ = 30.f;          // vitesse verticale du saut
	float jump_cooldown_ = 0.8f;          // delai minimal entre deux sauts (s)
	float stuck_check_interval_ = 0.3f;   // on mesure le deplacement sur cette duree (s)
	float stuck_min_distance_ = 0.3f;     // en dessous, l'ennemi est considere bloque (unites monde)

	float chase_acceptance_radius_ = 1.f;   // distance en x a laquelle la poursuite s'arrete
	float obstacle_probe_distance_ = 2.5f;  // longueur du rayon qui cherche un obstacle devant
	float obstacle_probe_height_ = 0.5f;    // hauteur du rayon par rapport a transform.location.y
	float air_speed_threshold_ = 0.5f;      // vitesse verticale (unites/s) au-dessus de laquelle il est en l'air
	float landed_confirm_time_ = 0.15f;     // duree d'immobilite verticale pour confirmer l'atterrissage (s)
	float takeoff_timeout_ = 0.3f;          // si il n'a pas decolle apres ce delai, le saut est annule (s)
	float max_jump_time_ = 2.f;             // securite : on force l'atterrissage au-dela (s)

	// Etat du saut
	bool is_jumping_ = false;
	bool jump_left_ground_ = false;
	float jump_still_timer_ = 0.f;
	float debug_timer_ = 0.f;
	float jump_air_time_ = 0.f;
	float prev_y_ = 0.f;

	// Etat du saut anti-blocage
	float jump_cooldown_timer_ = 0.f;
	float stuck_timer_ = 0.f;
	float stuck_dir_ = 0.f;
	float stuck_ref_x_ = 0.f;
	float stuck_ref_y_ = 0.f;
	bool stuck_tracked_this_tick_ = false;

	// Parametres d'attaque
	float attack_range_ = 3.f;      // distance (unites monde) a partir de laquelle l'ennemi attaque
	float attack_damage_ = 10.f;
	float attack_windup_ = 0.4f;    // delai entre le debut de l'attaque et l'instant du coup (s)
	float attack_duration_ = 0.8f;  // duree totale de l'attaque (s), doit etre >= windup
	float attack_cooldown_ = 1.0f;  // delai avant de pouvoir re-attaquer (s)

	// Etat d'attaque
	bool is_attacking_ = false;
	bool attack_hit_done_ = false;
	float attack_timer_ = 0.f;
	float attack_cooldown_timer_ = 0.f;
	float eye_offset_y_ = 0.f;   // decalage vertical (en unites monde) du regard par rapport a transform.location
	std::unique_ptr<bt::Node> behavior_root_;
	bool is_dead_=false;

	BloodParticles blood_particles_;
	DeathParticles death_particles_;
	PlayerHurtParticles player_hurt_particles_;

	lynx::CameraShake hurt_target_cs_;
};
