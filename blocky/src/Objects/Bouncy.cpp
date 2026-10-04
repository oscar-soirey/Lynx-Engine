#include "Bouncy.h"

#include <algorithm>
#include <cmath>

#include <hrl/hrl.h>

#include "../Collision.h"
#include "../VoxelUnits.h"

namespace
{
	// Plus grand deplacement (en voxels) entre deux tests de collision :
	// evite de traverser un voxel quand la balle va vite.
	// Precision de grille : reste en voxels, ne depend pas du gameplay.
	constexpr float kMaxStep = 0.25f;

	// Evite un gros saut (lag, pause editeur) qui ferait traverser le decor.
	constexpr float kMaxDt = 0.05f;

	float MoveTowardsZero(float value, float max_delta)
	{
		if (value > 0.f)
			return std::max(value - max_delta, 0.f);

		if (value < 0.f)
			return std::min(value + max_delta, 0.f);

		return 0.f;
	}
}

Bouncy::Bouncy()
{
	HPROPERTY(bounce_keep_, lynx::Exposed);
	HPROPERTY(gravity_, lynx::Exposed);
	HPROPERTY(collider_size_, lynx::Exposed);
	HPROPERTY(rest_speed_, lynx::Exposed);
	HPROPERTY(ground_friction_, lynx::Exposed);
}

void Bouncy::Init()
{
	StaticSprite::Init();
}

bool Bouncy::ResolveImpact(bool horizontal)
{
	float& normal = horizontal ? velocity_.x : velocity_.y;

	const float impact_speed = std::abs(normal);

	// Trop lent pour rebondir : simple contact. On annule juste la composante
	// qui rentre dans l'obstacle, sans amortir le reste (sinon une balle posee
	// perdrait 30% de sa vitesse horizontale a chaque frame).
	if (impact_speed < rest_speed_)
	{
		normal = 0.f;
		return false;
	}

	// Vrai rebond : on garde bounce_keep_ de la vitesse totale, et on inverse
	// la composante qui rentrait dans l'obstacle.
	velocity_.x = lynx::FRandomInRange(-7.f, 7.f);
	velocity_.y *= bounce_keep_;
	normal = -normal;

	OnBounce(impact_speed, horizontal);

	return true;
}

void Bouncy::Tick(double _dt)
{
	StaticSprite::Tick(_dt);

	const float dt = std::min(static_cast<float>(_dt), kMaxDt);

	if (dt <= 0.f)
		return;

	velocity_.y -= gravity_ * dt;

	// Position et deplacement de la frame, en coordonnees voxel.
	float voxel_x;
	float voxel_y;
	float move_x;
	float move_y;

	if (HRL_WorldToVoxelCoordinates(
			lynx::GetScene(),
			transform.location.x,
			transform.location.y,
			&voxel_x,
			&voxel_y) != HRL_TRUE ||
		HRL_WorldToVoxelCoordinates(
			lynx::GetScene(),
			velocity_.x * dt,
			velocity_.y * dt,
			&move_x,
			&move_y) != HRL_TRUE)
	{
		return;
	}

	const float distance = std::max(std::abs(move_x), std::abs(move_y));
	const int steps = std::max(1, static_cast<int>(std::ceil(distance / kMaxStep)));

	float step_x = move_x / static_cast<float>(steps);
	float step_y = move_y / static_cast<float>(steps);

	// Boite de collision : unites de jeu -> voxels.
	const float collider_voxels = units::GameToVoxels(collider_size_);

	bool touched_ground = false;

	for (int i = 0; i < steps; ++i)
	{
		// ----- X -----
		if (step_x != 0.f)
		{
			if (collision::CanMoveX(
					nullptr, voxel_x + step_x, voxel_y,
					collider_voxels, collider_voxels, collision_mask_, step_x))
			{
				voxel_x += step_x;
			}
			else
			{
				ResolveImpact(true);

				// Une seule reaction par axe et par frame : le reste du
				// deplacement a ete calcule avec l'ancienne vitesse.
				step_x = 0.f;
			}
		}

		// ----- Y -----
		if (step_y != 0.f)
		{
			if (collision::CanMoveY(
					nullptr, voxel_x, voxel_y + step_y,
					collider_voxels, collider_voxels, collision_mask_, step_y))
			{
				voxel_y += step_y;
			}
			else
			{
				if (step_y < 0.f)
					touched_ground = true;

				ResolveImpact(false);
				step_y = 0.f;
			}
		}
	}

	// Pose au sol : friction horizontale.
	if (touched_ground && velocity_.y == 0.f)
		velocity_.x = MoveTowardsZero(velocity_.x, ground_friction_ * dt);

	// Reporte la position voxel dans le monde.
	float world_x;
	float world_y;

	if (HRL_VoxelToWorldCoordinates(
			lynx::GetScene(),
			voxel_x,
			voxel_y,
			&world_x,
			&world_y) == HRL_TRUE)
	{
		transform.location.x = world_x;
		transform.location.y = world_y;

		OnTransformChanged();
	}

	if (collision::IsDebugEnabled())
	{
		collision::DrawDebugCollider(
			transform.location.x,
			transform.location.y,
			transform.location.z,
			units::GameToWorld(collider_size_),
			units::GameToWorld(collider_size_)
		);
	}
}
