#include "Ennemy.h"

Ennemy::Ennemy()
{
	HPROPERTY(life, lynx::Exposed);
}

void Ennemy::Init()
{
	Pawn::Init();
	blood_particles_.Initialize();
}

void Ennemy::Hurt(Actor *instigator, float amount)
{
	Pawn::Hurt(instigator, amount);
	life -= amount;

	//repousser l'ennemi
	if (instigator)
	{
		float direction =
				transform.location.x - instigator->transform.location.x;

		if (direction != 0.f)
		{
			direction = direction > 0.f ? 1.f : -1.f;
			LaunchPawn(direction * knockback_intensity_, 8.f, true, true);
		}

		//particules de sang
		printf("%f\n", direction);
		blood_particles_.Play(transform.location.x, transform.location.y, 0.f, direction, 1.f);
	}
}
