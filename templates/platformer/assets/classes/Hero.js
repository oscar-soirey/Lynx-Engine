// The hero : a Humanoid (engine actor : walk, jump, gravity, collisions, voxel
// surfaces such as ice / mud / rubber, swimming) driven by the input.
//
//   A / D, arrows   move            Space / W     jump (twice : double jump)
//   Shift / K       dash            S / Down      ground pound (in the air) :
//   R               back to the     breaks the "BREAKABLE" voxels (bricks) and
//                   checkpoint      squashes the enemies around the landing.

class Hero extends Humanoid {
    static interfaces = ["Damageable", "Collector", "Respawnable", "Finisher"];

    static properties = {
        max_hp: { value: 5, type: "int" },
        dash_speed: 36.0,
        dash_time: 0.16,
        pound_speed: 70.0,
        kill_height: 40.0,          // fell that far below the start : lose a heart
    };

    constructor() {
        super();
        this.autoPossessPlayer = 0;  // player 0 controls the hero at Play
        this.addTag("player");

        // Humanoid settings (units : voxels, seconds). Editable in Details too.
        this.collider_size = { x: 1.8, y: 2.9 };
        this.move_speed = 16;
        this.jump_speed = 30;
        this.gravity = 75;
        this.max_fall_speed = 60;
        this.max_jump_count = 2;

        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.4, y: 3.4 }, offset: vec3(0, 0.15, 0) });
        this.anim.setAnimation("sprites/hero_idle.png", 2, 0.45, true);
    }

    BeginPlay() {
        this.hp = this.max_hp;
        this.coins = 0;
        this.start = this.position.clone();
        Sfx.listener = this;
        Sfx.music("music_chip", 0.35);
        this.checkpoint = this.position.clone();
        this.invulnerable = 0;
        this.dashLeft = 0;
        this.dashCooldown = 0;
        this.pounding = false;
        this.dead = false;
        this.won = false;
        this.state = "";

        this.addComponent("Camera", { offset: vec3(0, 3, 60), fov: 32, followSpeed: 6, followDelay: 0.04 });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.85, 0.6), intensity: 0.5, offset: vec3(0, 1, 6) });

        this.totalCoins = Level.count("Coin");
        this.hud = UI.create("ui/Hud.widget");
        this.hud.addToViewport(10);
        this.RefreshHud();
        this.ShowMessage("Rejoins le drapeau !", 2.5);
    }

    // ---- Input (only while possessed) ---------------------------------------

    ProcessInput(player) {
        if (this.dead || this.won) {
            this.Move(0);
            return;
        }
        if (this.dashLeft <= 0 && !this.pounding)
            this.Move(Input.axis("move_x"));
        if (Input.pressed("jump")) {
            Sfx.play(this.IsGrounded() ? "jump" : "double_jump");
            this.Jump();
        }
        if (Input.released("jump"))
            this.StopJumping();
        if (Input.pressed("dash") && this.dashCooldown <= 0)
            this.StartDash();
        if (Input.pressed("pound") && !this.IsGrounded() && !this.pounding)
            this.StartPound();
        if (Input.pressed("restart"))
            this.Respawn();
    }

    StartDash() {
        this.dashDir = this.IsFacingRight() ? 1 : -1;
        this.dashLeft = this.dash_time;
        this.dashCooldown = 0.6;
        Sfx.play("dash");
        Level.spawn("Sparkle", this.position);
    }

    StartPound() {
        this.pounding = true;
        this.dashLeft = 0;
        this.SetVelocity(0, -this.pound_speed);
        Sfx.play("dash", null, { volume: 0.5 });
    }

    // ---- Every frame --------------------------------------------------------

    Update(dt) {
        if (this.dead) {
            this.respawnTimer -= dt;
            if (this.respawnTimer <= 0)
                this.Respawn(true);
        }
        if (this.invulnerable > 0) {
            this.invulnerable -= dt;
            this.anim.visible = this.invulnerable <= 0 || Math.floor(this.invulnerable * 14) % 2 === 0;
        }
        this.dashCooldown -= dt;
        if (this.dashLeft > 0) {
            this.dashLeft -= dt;
            this.SetVelocity(this.dashDir * this.dash_speed, 0);   // straight line, no gravity
        }
        if (this.pounding)
            this.SetVelocity(0, -this.pound_speed);

        if (!this.dead && this.position.y < this.start.y - this.kill_height) {
            this.TakeDamage(1, null);
            if (!this.dead)
                this.Respawn(false);
        }

        if (this.messageTime > 0) {
            this.messageTime -= dt;
            if (this.messageTime <= 0)
                this.hud.find("Message").visibility = "Hidden";
        }

        this.UpdateAnimation();
    }

    UpdateAnimation() {
        let state;
        if (this.dead)
            state = "hurt";
        else if (!this.IsGrounded())
            state = this.GetVelocity().y > 0 ? "jump" : "fall";
        else
            state = Math.abs(this.GetVelocity().x) > 1 ? "run" : "idle";

        if (state === this.state)
            return;
        this.state = state;

        const anims = {
            idle: ["sprites/hero_idle.png", 2, 0.45],
            run:  ["sprites/hero_run.png", 4, 0.08],
            jump: ["sprites/hero_jump.png", 1, 1.0],
            fall: ["sprites/hero_fall.png", 1, 1.0],
            hurt: ["sprites/hero_hurt.png", 2, 0.08],
        };
        const a = anims[state];
        this.anim.setAnimation(a[0], a[1], a[2], true);
    }

    // Humanoid event : touched the ground.
    OnLanded() {
        if (!this.pounding)
        Sfx.play(this.pounding ? "stomp" : "land", null, { volume: this.pounding ? 0.9 : 0.4 });
            return;
        this.pounding = false;
        const feet = vec3(this.position.x, this.position.y - this.collider_size.y * 0.5, 0);

        // Voxels : only the BREAKABLE ones (bricks), the result says what broke.
        const broken = Voxels.destroyRect(vec3(feet.x, feet.y - 1.5, 0), { x: 5, y: 3 }, { flags: "BREAKABLE" });
        for (let i = 0; i < broken.cells.length; i += 3)
            Level.spawn("Debris", vec3(broken.cells[i].world.x, broken.cells[i].world.y, 0));
            Sfx.play("break_rock");
        if (broken.count > 0)
            this.SetVelocity(0, -this.pound_speed * 0.5);   // keeps falling through

        // Actors around the impact.
        const hit = Physics.overlapCircle(feet, 3.5, { ignore: this, voxels: false });
        for (const actor of hit.actors)
            if (actor.implements("Damageable"))
                actor.send("Damageable", "TakeDamage", 5, this);
        Level.spawn("Sparkle", feet);
    }

    // ---- Interfaces ---------------------------------------------------------

    // Damageable : enemies, spikes and lava (voxel physics : instigator null).
    TakeDamage(amount, instigator) {
        if (this.dead || this.won || this.invulnerable > 0)
            return;
        this.hp -= Math.max(1, Math.round(amount));
        this.invulnerable = 1.2;
        this.pounding = false;
        Sfx.play("hurt");
        this.dashLeft = 0;
        if (instigator) {
            const dir = this.position.x >= instigator.position.x ? 1 : -1;
            this.Launch(dir * 20, 18, true, true);
        } else {
            this.Launch(0, 24, false, true);
        }
        this.RefreshHud();
        if (this.hp <= 0)
            this.Die();
    }

    OnCollected(kind, amount) {
        Sfx.play(kind === "coin" ? "coin" : "heal", null, { volume: 0.6 });
        if (kind === "coin")
            this.coins += amount;
        else if (kind === "heart")
            this.hp = Math.min(this.max_hp, this.hp + amount);
        this.RefreshHud();
    }

    SetCheckpoint(position) {
        this.checkpoint = vec3(position.x, position.y + 1, 0);
        this.ShowMessage("Checkpoint !", 1.5);
        Sfx.play("checkpoint");
    }

    OnFinish(goal) {
        if (this.won)
            return;
        this.won = true;
        this.StopMovement();
        this.ShowMessage("Bravo ! " + this.coins + " / " + this.totalCoins + " pièces", 1000);
        Engine.setTimeDilation(0.3, 1.5);   // slow motion for a moment
        Sfx.stopMusic();
        Sfx.play("win");
    }

    // ---- Life ---------------------------------------------------------------

    Die() {
        this.dead = true;
        this.movement_enabled = false;
        this.ShowMessage("Aïe !", 1.2);
        Sfx.play("death");
        this.respawnTimer = 1.2;
    }

    Respawn(full = true) {
        this.position.set(this.checkpoint.x, this.checkpoint.y, 0);
        this.StopMovement();
        this.dead = false;
        this.pounding = false;
        this.movement_enabled = true;
        this.invulnerable = 1.5;
        if (full)
            this.hp = this.max_hp;
        this.RefreshHud();
    }

    // ---- HUD (ui/Hud.widget) ------------------------------------------------

    RefreshHud() {
        if (!this.hud || !this.hud.valid)
            return;
        this.hud.find("Coins").text = this.coins + " / " + this.totalCoins;
        for (let i = 1; i <= 5; ++i)
            this.hud.find("Heart" + i).visibility = i <= this.hp ? "Visible" : "Hidden";
    }

    ShowMessage(text, seconds) {
        if (!this.hud || !this.hud.valid)
            return;
        const message = this.hud.find("Message");
        message.text = text;
        message.visibility = "Visible";
        this.messageTime = seconds;
    }
}
