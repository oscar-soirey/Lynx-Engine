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

	invert_right_left_=true;

	move_speed_ = 5.f;
	see_radius_ = 13.f;


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

	// Branche 1 : si je vois la cible, je vais vers elle
	auto chase = std::make_unique<Sequence>();
	chase->Add(Make<Condition>([this] { return CanSeeTarget(); }));
	chase->Add(Make<Action>([this](float dt) {
		return MoveToTarget(dt, 1.f);
	}));

	// Branche 2 (repli) : je ne bouge pas
	auto idle = Make<Action>([this](float) {
		Move(0.f);
		return Status::Success;
	});

	// Selector : essaie "chase", sinon "idle"
	auto root = std::make_unique<Selector>();
	root->Add(std::move(chase));
	root->Add(std::move(idle));

	behavior_root_ = std::move(root);
}

void Mushroom::Death()
{
	Ennemy::Death();
	anim_manager_.force_state("death");
	process_behavior_tick_ = false;
	movement_enabled_ = false;
}
