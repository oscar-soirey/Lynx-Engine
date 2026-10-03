#pragma once

#include <memory>

#include "../GameClasses.h"
#include "../Particles/BloodParticles.h"
#include "../Particles/DeathParticles.h"
#include "BehaviorTree.h"

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

	// Vision
	bool HasLineOfSight() const;

	float knockback_intensity_ = 20.0f;

	// IA
	bool process_behavior_tick_=true;
	Actor *target_ = nullptr;
	float see_radius_ = 300.f;
	float eye_offset_y_ = 0.f;   // decalage vertical (en unites monde) du regard par rapport a transform.location
	std::unique_ptr<bt::Node> behavior_root_;
	bool is_dead_=false;

	BloodParticles blood_particles_;
	DeathParticles death_particles_;
};