#include "Mushroom.h"

#include "BehaviorTree.h"

Mushroom::Mushroom()
{
	relative_sprite_transform_.scale.x = 7.f;
	relative_sprite_transform_.scale.y = 7.f;
	// Offset du mesh NON miroite (= ennemi tourne vers la gauche, art natif). Miroir automatique.
	relative_sprite_transform_.location.x = -1.55f;
	relative_sprite_transform_.location.y = 1.9f;
}

void Mushroom::Init()
{
	life=25;
	invert_right_left_=true;

	move_speed_ = 5.f;
	see_radius_ = 13.f;

	attack_range_ = 5.f;
	attack_damage_ = 10.f;
	attack_windup_ = 0.4f;
	attack_duration_ = 1.f;
	attack_cooldown_ = 1.0f;


	//autio sources
	hurt_src_.AttachToActor(this);
	hurt_src_.max_distance = 15.f;
	hurt_src_.pitch_min = 1.2f;
	hurt_src_.pitch_max = 1.5f;
	hurt_source_reference_ = &hurt_src_;


	Ennemy::Init();

	bs_default.add(
				0.f,
				idle_
		);

	bs_default.add(
			1.f,
			walk_
	);

	bs_default.add(
			-1.f,
			walk_
	);

	//Regles de transition
	anim_manager_.add_state("loco", bs_default, { .variable = "speed" });

	anim_manager_.add_state("air_idle", air_idle_, { .restart_on_enter = true });

	anim_manager_.add_state("attack", attack_, { .restart_on_enter = true });

	anim_manager_.add_state("hurt", hurt_);
	anim_manager_.add_any_transition("hurt", anim_manager_.triggered("hurt"), 0);

	anim_manager_.add_state("death", death_, {
		.interruptible = false,
		.restart_on_enter = true,
		.on_exit = [&]
		{
			//particules de mort (orange)
			death_particles_.Play(transform.location.x, transform.location.y, 0.f);

			hurt_src_.Play();

			lynx::GetEngine()->GetCurrentLevel()->DestroyActor(this);
		}});
}

void Mushroom::StartGame()
{
	auto* player = lynx::GetEngine()->GetCurrentLevel()->GetActorFromID("Pawn");
	SetTarget(player);
}

void Mushroom::Hurt(Actor *instigator, float amount)
{
	Ennemy::Hurt(instigator, amount);
}

void Mushroom::BuildBehaviorTree()
{
	using namespace bt;

	// Branche -1 (prioritaire) : obstacle devant moi -> je saute AVANT de tenter quoi que ce soit d'autre
	auto unstick = std::make_unique<Sequence>();
	unstick->Add(Make<Condition>([this] { return ShouldJumpOverObstacle(); }));
	unstick->Add(Make<Action>([this](float) {
		Jump("obstacle");
		return Status::Success;
	}));

	// Branche 0 : si la cible est a portee, j'attaque (et je finis l'attaque une fois commencee)
	auto attack = std::make_unique<Sequence>();
	attack->Add(Make<Condition>([this] { return IsAttacking() || CanAttackTarget(); }));
	attack->Add(Make<Action>([this](float dt) {
		return Attack(dt);
	}));

	// Branche 1 : si je vois la cible, je vais vers elle
	auto chase = std::make_unique<Sequence>();
	chase->Add(Make<Condition>([this] { return CanSeeTarget(); }));
	chase->Add(Make<Action>([this](float dt) {
		return MoveToTarget(dt, chase_acceptance_radius_);
	}));

	// Branche 2 (repli) : je ne bouge pas
	auto idle = Make<Action>([this](float) {
		Move(0.f);
		return Status::Success;
	});

	// Selector : essaie "unstick", puis "attack", puis "chase", sinon "idle"
	auto root = std::make_unique<Selector>();
	root->Add(std::move(unstick));
	root->Add(std::move(attack));
	root->Add(std::move(chase));
	root->Add(std::move(idle));

	behavior_root_ = std::move(root);
}

void Mushroom::OnAttackStart()
{
	anim_manager_.force_state("attack");
}

void Mushroom::OnAttackEnd()
{
	anim_manager_.force_state("loco");
}

void Mushroom::OnJump()
{
	// Idle pendant tout le saut / la chute
	anim_manager_.force_state("air_idle");
}

void Mushroom::OnLand()
{
	anim_manager_.force_state("loco");
}

void Mushroom::Death()
{
	Ennemy::Death();
	anim_manager_.force_state("death");
	process_behavior_tick_ = false;
	movement_enabled_ = false;
}
