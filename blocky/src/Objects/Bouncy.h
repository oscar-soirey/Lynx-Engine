#pragma once

#include "../GameClasses.h"

// ============================================================
// BouncyBall : un sprite avec gravite + collisions, qui rebondit.
//
// A chaque rebond, la vitesse est multipliee par bounce_keep_ (0.7 = il garde
// 70% de sa vitesse) et la composante qui rentre dans l'obstacle est inversee.
// Sous rest_speed_ (vitesse d'impact, unites monde / s), il ne rebondit plus :
// il se pose, puis ground_friction_ le ralentit.
//
// Cree par code :
//   auto* ball = level->SpawnActorFromClass<BouncyBall>({ location, {}, {2.f, 2.f, 1.f} });
//   ball->SetVelocity({ 20.f, 15.f });
// ou place dans l'editeur (classe "BouncyBall", texture_path pour le visuel).
// ============================================================
class Bouncy : public StaticSprite {
public:
	Bouncy();

	void Init() override;
	void Tick(double dt) override;

	void SetVelocity(lynx::vec2 vel) { velocity_ = vel; }
	lynx::vec2 GetVelocity() const { return velocity_; }

	// Part de vitesse conservee a chaque rebond
	float bounce_keep_ = 0.9f;

	// Gravite (unites monde / s^2). Meme valeur que les Pawns par defaut.
	float gravity_ = 55.f;

	// Cote de la boite de collision, en unites de jeu (voir VoxelUnits.h).
	// A accorder avec la taille du sprite.
	float collider_size_ = 4.f;

	// Vitesse d'impact (monde / s) sous laquelle il ne rebondit plus.
	float rest_speed_ = 3.f;

	// Deceleration horizontale (monde / s^2) quand il est pose au sol.
	float ground_friction_ = 12.f;

	// Avec quoi il collisionne. Terrain seulement par defaut ; ajouter
	// collision::kPawnMask pour qu'il rebondisse aussi sur les Pawns.
	uint32_t collision_mask_ = collision::kTerrainMask;

protected:
	// Appele a chaque vrai rebond (pas quand il se pose). Utile pour un son,
	// des particules, etc. impact_speed = vitesse de l'impact avant amortissement.
	virtual void OnBounce(float impact_speed, bool horizontal) {}

private:
	// Gere l'impact sur un axe. Retourne true s'il y a eu un vrai rebond.
	bool ResolveImpact(bool horizontal);

	lynx::vec2 velocity_{};
};
