// Slime : a Humanoid (gravity, collisions) moved by its own small AI.
// Turns around at walls and at the edge of the platforms (two raycasts),
// hops from time to time. Jump on it (or ground pound) to squash it.

class Slime extends Humanoid {
    static interfaces = ["Damageable"];

    static properties = {
        damage: { value: 1, type: "int" },
        hop_interval: 2.5,
    };

    constructor() {
        super();
        this.addTag("enemy");
        this.collider_size = { x: 2.2, y: 1.4 };
        this.move_speed = 5;
        this.jump_speed = 16;
        this.gravity = 70;
        // Layer 2, only meets layer 2 : the hero walks through, the contact is
        // tested below (squash from above, damage otherwise).
        this.collision_layer = 2;
        this.collision_mask = 2;

        this.anim = this.addComponent("AnimationSprite", { size: { x: 2.8, y: 2.8 }, offset: vec3(0, 0.6, 0) });
        this.anim.setAnimation("sprites/slime.png", 2, 0.25, true);
    }

    BeginPlay() {
        this.dir = -1;
        this.hop = this.hop_interval * (0.5 + Math.random());
        this.dead = false;
    }

    Update(dt) {
        if (this.dead)
            return;

        // Wall in front, or no ground in front : turn around.
        const p = this.position;
        const ahead = vec3(p.x + this.dir * 1.7, p.y, 0);
        const wall = Physics.raycast(p, ahead, { actors: false });
        const ground = Physics.raycast(ahead, vec3(ahead.x, ahead.y - 2.5, 0), { actors: false });
        if (this.IsGrounded() && (wall || !ground))
            this.dir = -this.dir;
        this.Move(this.dir);

        this.hop -= dt;
        if (this.hop <= 0 && this.IsGrounded()) {
            this.Jump();
            this.hop = this.hop_interval * (0.5 + Math.random());
        }

        // Contact with the hero.
        for (const hero of Level.findWithTag("player")) {
            const dx = hero.position.x - p.x;
            const dy = hero.position.y - p.y;
            if (Math.abs(dx) > 2.0 || Math.abs(dy) > 2.4)
                continue;
            if (dy > 1.2 && hero.GetVelocity().y < 0) {
                hero.Launch(0, 26, false, true);      // bounce on it
                this.Squash();
            } else {
                hero.send("Damageable", "TakeDamage", this.damage, this);
            }
        }
    }

    TakeDamage(amount, instigator) {
        this.Squash();
    }

    Squash() {
        if (this.dead)
            return;
        this.dead = true;
        this.movement_enabled = false;
        this.anim.size = { x: 3.2, y: 1.2 };
        this.anim.offset = vec3(0, -0.4, 0);
        Level.spawn("Sparkle", this.position);
        this.lifetime = 0.5;                          // destroyed in 0.5 s
        Sfx.play("enemy_die", this.position);
    }
}
