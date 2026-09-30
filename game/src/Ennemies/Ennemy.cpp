#include "Ennemy.h"

Ennemy::Ennemy()
{
	HPROPERTY(life, lynx::Exposed);
}

void Ennemy::Init()
{
	Pawn::Init();
}

void Ennemy::Hurt(Actor *instigator, float amount)
{
	life -= amount;
	printf("hurt\n");
}
