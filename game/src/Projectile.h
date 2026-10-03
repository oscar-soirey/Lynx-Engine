#pragma once

#include "GameClasses.h"
#include "Particles/ProjectileParticles.h"

class Projectile : public Sprite {
public:
	void Init() override;
	void Tick(double _dt) override;
	void SetVelocity(lynx::vec2 vel);

	// Rayon (en voxels) du cratere creuse quand le projectile touche un voxel.
	float explosion_radius_ = 7.f;

	float kHitSize = 4.f;

	// Degats infliges a chaque Pawn touche (le Player est ignore).
	float damage_ = 25.f;

private:
	// true si le projectile touche un voxel a cette position (coordonnees voxel).
	bool IsInsideVoxel(float voxel_x, float voxel_y) const;

	// Blesse les Pawns (hors Player) qui chevauchent cette position.
	// Retourne true si au moins un Pawn a ete touche.
	bool HurtPawnsAt(float voxel_x, float voxel_y);

	// Le projectile est consomme : il ne bouge plus et sera detruit en fin de frame.
	void Consume();

	lynx::vec2 velocity_;
	bool consumed_ = false;

	ProjectileParticles projectile_particles_;
};