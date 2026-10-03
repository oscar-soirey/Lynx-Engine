#include "Projectile.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <hrl/hrl.h>

#include "Collision.h"
#include "Player.h"

namespace
{
	// Plus grand deplacement (en voxels) entre deux tests de collision :
	// evite de traverser un voxel ou un Pawn quand le projectile va vite.
	constexpr float kMaxStep = 0.5f;
}

void Projectile::Init()
{
	Sprite::Init();

	projectile_particles_.Initialize();

	// Le materiau (proj.png) est gere par anim_ : on ne le recree plus ici,
	// sinon on ecraserait la spritesheet par la texture entiere.
	anim_.restart();
	anim_.play();
}

void Projectile::Tick(double _dt)
{
	Sprite::Tick(_dt);

	// Avant le test consumed_ : l'anim continue jusqu'a la destruction du projectile.
	anim_.update(_dt);

	if (consumed_)
		return;

	const float dt = static_cast<float>(_dt);

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

	const float step_x = move_x / static_cast<float>(steps);
	const float step_y = move_y / static_cast<float>(steps);

	for (int i = 0; i < steps; ++i)
	{
		voxel_x += step_x;
		voxel_y += step_y;

		// Pawn touche : degats, pas de cratere.
		if (HurtPawnsAt(voxel_x, voxel_y))
		{
			Consume();
			break;
		}

		// Voxel touche : on creuse un cratere centre sur le point d'impact.
		if (IsInsideVoxel(voxel_x, voxel_y))
		{
			Pawn::DestroyVoxelsInRadius(
				voxel_x,
				voxel_y,
				explosion_radius_,
				transform.location.z
			);

			Consume();
			break;
		}
	}

	// Reporte la position voxel (celle de l'impact si on s'est arrete) dans le monde.
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
				kHitSize,
				kHitSize
		);
	}
}

void Projectile::SetVelocity(lynx::vec2 vel)
{
	velocity_ = vel;
}

bool Projectile::IsInsideVoxel(float voxel_x, float voxel_y) const
{
	// Meme test que les Pawns (collision::CanMoveX/Y), pour que le projectile
	// s'arrete sur exactement ce qui bloque aussi les Pawns.
	return collision::VoxelBoxHit(
		voxel_x,
		voxel_y,
		kHitSize,
		kHitSize,
		collision::kTerrainMask
	);
}

bool Projectile::HurtPawnsAt(float voxel_x, float voxel_y)
{
	std::vector<Pawn*> overlapping;

	collision::QueryPawnsInBox(
		collision::Box{voxel_x, voxel_y, kHitSize * 0.5f, kHitSize * 0.5f},
		overlapping
	);

	// On filtre d'abord, on blesse ensuite : Hurt() peut modifier l'etat des Pawns.
	overlapping.erase(
		std::remove_if(
			overlapping.begin(),
			overlapping.end(),
			[](Pawn* pawn) { return dynamic_cast<Player*>(pawn) != nullptr; }
		),
		overlapping.end()
	);

	for (Pawn* pawn : overlapping)
		pawn->Hurt(this, damage_);

	return !overlapping.empty();
}

void Projectile::Consume()
{
	//particules d'impact
	projectile_particles_.Play(transform.location.x, transform.location.y, 0.f);


	consumed_ = true;

	// Le moteur itere sur les acteurs pendant Tick : la destruction est differee
	// a la fin de la frame.
	lynx::GetEngine()->GetCurrentLevel()->DestroyActor(this);
}
