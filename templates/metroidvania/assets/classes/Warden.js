// The Hollow Warden : the boss. A Humanoid driven by a Behavior Tree
// (ai/Warden.bt), animated by an Anim Graph (anim/Warden.animgraph).
//
//   0 Slam      winds up, leaps on the player, shockwaves on the floor
//   1 Charge    runs across the arena ; hitting a wall shakes rocks loose
//   2 RockFall  roars : rocks fall from the ceiling (and break it)
//   3 Burrow    phase 2 : dives into the dirt floor, erupts under the player
//   4 Stalk     walks toward the player
// At half health it enrages (phase 2) : faster, shorter rests, burrows.

class Warden extends Humanoid {
    static interfaces = ["Damageable", "NailTarget"];
    static properties = { max_hp: { value: 70, type: "int" } };

    constructor() {
        super();
        this.collider_size = { x: 6.5, y: 5 };
        this.move_speed = 9;
        this.gravity = 90;
        this.max_fall_speed = 60;
        this.collision_layer = 2;
        this.collision_mask = 0;
        this.face_movement_direction = false;
        this.anim = this.addComponent("AnimationSprite", { size: { x: 10, y: 8 }, offset: vec3(0, 1.4, 0) });
        this.anim.loadGraph("anim/Warden.animgraph");
        this.addComponent("Light", { type: "point", color: vec3(1, 0.8, 0.4), intensity: 1.3, offset: vec3(1.5, 4, 6) });
        this.addTag("enemy");
    }

    BeginPlay() {
        this.home = this.position.clone();
        this.hp = this.max_hp;
        this.phase = 1;
        this.awake = false;
        this.dead = false;
        this.t = 0;
        this.step = "";
        this.flash = 0;
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Warden.bt" });
    }

    // ---- Arena -------------------------------------------------------------------
    Wake(player) {
        this.awake = true;
        this.anim.setTrigger("roar");
        Game.shake();
        Sfx.play("boss_roar");
        Sfx.music("music_boss", 0.45);
        const p = Game.player();
        if (p) {
            this.SetHud(true);
            if (p.hud && p.hud.valid)
                p.hud.find("BossBar").percent = 1;
            p.ShowMessage("THE HOLLOW WARDEN", 2.5);
        }
    }

    ResetFight() {
        this.awake = false;
        this.hp = this.max_hp;
        this.phase = 1;
        this.step = "";
        this.anim.visible = true;
        this.collider_size = { x: 6.5, y: 5 };
        this.position.set(this.home.x, this.home.y, 0);
        this.StopMovement();
        this.ai.restart();
        this.SetHud(false);
    }

    SetHud(visible) {
        const p = Game.player();
        if (!p || !p.hud || !p.hud.valid)
            return;
        p.hud.find("BossName").visibility = visible ? "Visible" : "Hidden";
        p.hud.find("BossBar").visibility = visible ? "Visible" : "Hidden";
    }

    Target() {
        const t = this.ai.blackboard.get("target");
        return t && t.valid ? t : null;
    }

    Facing() { return this.transform.scale.x < 0 ? -1 : 1; }

    Face(x) {
        if (Math.abs(x - this.position.x) > 1)
            this.transform.scale.x = x < this.position.x ? -1 : 1;
    }

    Begin(step) {
        if (this.step !== step) {
            this.step = step;
            this.t = 0;
            return true;
        }
        return false;
    }

    Speed() { return this.phase === 2 ? 1.35 : 1; }

    // ---- Behavior tree : conditions ------------------------------------------------
    IsAsleep() { return !this.awake || this.dead; }

    ShouldEnrage() { return this.phase === 1 && this.hp <= this.max_hp * 0.5 && !this.dead; }

    Enrage(dt) {
        if (this.Begin("enrage")) {
            this.Move(0);
            this.anim.setTrigger("roar");
            Game.shake();
            PostProcess.set("tintColor", [1, 0.82, 0.8]);
            const p = Game.player();
            if (p) p.ShowMessage("The Warden is enraged !", 2);
        }
        this.t += dt;
        if (Math.random() < 0.3)
            Level.spawn("FallingRock", vec3(this.home.x + (Math.random() - 0.5) * 70, Game.Y0 + 39, 1));
        if (this.t > 1.4) {
            this.phase = 2;
            this.step = "";
            return true;
        }
        return BT.Running;
    }

    ChooseAttack() {
        const t = this.Target();
        if (!t) return false;
        const d = Math.abs(t.position.x - this.position.x);
        const options = [];
        if (d < 16) options.push(0, 0);
        if (d > 10) options.push(1, 1);
        options.push(2, 4);
        if (this.phase === 2) options.push(3, 3);
        let pick = options[Math.floor(Math.random() * options.length)];
        if (pick === this.lastAttack && Math.random() < 0.6)
            pick = options[Math.floor(Math.random() * options.length)];
        this.lastAttack = pick;
        this.ai.blackboard.set("attack", pick);
        this.step = "";
        return true;
    }

    // ---- Attacks --------------------------------------------------------------------
    Slam(dt) {
        const t = this.Target();
        if (this.Begin("slam_windup")) {
            this.Move(0);
            if (t) this.Face(t.position.x);
            this.anim.setBool("winding", true);
        }
        this.t += dt;
        if (this.step === "slam_windup" && this.t > 0.7 / this.Speed()) {
            this.anim.setBool("winding", false);
            this.step = "slam_air";
            this.t = 0;
            const dx = t ? t.position.x - this.position.x : 0;
            this.Launch(Math.max(-30, Math.min(30, dx * 1.3)), 34, true, true);
            return BT.Running;
        }
        if (this.step === "slam_air" && this.t > 0.15 && this.IsGrounded()) {
            this.step = "slam_land";
            this.t = 0;
            this.SetVelocity(0, 0);
            this.anim.setTrigger("slam");
            this.Impact();
        }
        if (this.step === "slam_land" && this.t > 0.6 / this.Speed())
            return true;
        return BT.Running;
    }

    // Landing (also the notify OnSlamImpact of the animation : only once).
    Impact() {
        const feet = vec3(this.position.x, this.position.y - 2.4, 0);
        Sfx.play("boss_slam", this.position);
        for (const dir of [-1, 1]) {
            const w = Level.spawn("Shockwave", vec3(feet.x + dir * 3.5, feet.y + 0.6, 1));
            w.Launch(dir);
        }
        const r = Voxels.destroyCircle(feet, 3.5, { flags: "SOFT", cells: true });
        Game.debris(r.cells, 6);
        Game.hurtCircle(vec3(this.position.x, this.position.y, 0), 4.5, 1, this, [this]);
        Game.shake();
    }

    OnSlamImpact() {}

    Charge(dt) {
        const t = this.Target();
        if (this.Begin("charge_roar")) {
            this.Move(0);
            if (t) this.Face(t.position.x);
            this.anim.setTrigger("roar");
            Sfx.play("boss_roar", this.position, { volume: 0.7 });
        }
        this.t += dt;
        if (this.step === "charge_roar") {
            if (this.t > 0.6 / this.Speed()) {
                this.step = "charge_run";
                this.t = 0;
                this.chargeDir = this.Facing();
                this.anim.setBool("charging", true);
            }
            return BT.Running;
        }
        if (this.step === "charge_run") {
            this.move_speed = 28 * this.Speed();
            this.Move(this.chargeDir);
            this.Face(this.position.x + this.chargeDir * 5);
            const wall = EnemyKit.solidAt(this.position.x + this.chargeDir * 4, this.position.y, 1, 3);
            if (wall || this.t > 2.2) {
                this.anim.setBool("charging", false);
                this.Move(0);
                this.move_speed = 9;
                this.Launch(-this.chargeDir * 10, 10, true, true);
                if (wall) {
                    Game.shake();
                    for (let i = 0; i < 4 + this.phase * 2; ++i)
                        this.DropRock(this.position.x + (Math.random() - 0.5) * 40, 0.4 + Math.random() * 0.6);
                }
                this.step = "charge_stun";
                this.t = 0;
            }
            return BT.Running;
        }
        return this.t > 0.8 / this.Speed() ? true : BT.Running;
    }

    RockFall(dt) {
        if (this.Begin("rockfall")) {
            this.Move(0);
            this.anim.setTrigger("roar");
            Game.shake();
            this.rocks = 0;
        }
        this.t += dt;
        const t = this.Target();
        const total = this.phase === 2 ? 10 : 6;
        if (this.rocks < total && this.t > 0.3 + this.rocks * 0.18) {
            const near = t ? t.position.x : this.position.x;
            const x = this.rocks % 2 === 0 ? near + (Math.random() - 0.5) * 6 : this.home.x + (Math.random() - 0.5) * 80;
            this.DropRock(x, 0.6);
            this.rocks++;
        }
        return this.t > 0.6 + total * 0.18 ? true : BT.Running;
    }

    // A rock from the ceiling : the ceiling voxels there are broken too.
    DropRock(x, delay) {
        const ceiling = Game.Y0 + 41;
        const xmin = Game.X0 + 152, xmax = Game.X0 + 318;
        x = Math.max(xmin, Math.min(xmax, x));
        const r = Voxels.destroyCircle(vec3(x, ceiling + 1, 0), 1.5, { cells: false });
        const rock = Level.spawn("FallingRock", vec3(x, ceiling - 1.2, 1));
        rock.delay = delay;
    }

    Burrow(dt) {
        const t = this.Target();
        if (this.Begin("burrow_dive")) {
            this.Move(0);
            this.anim.setTrigger("roar");
        }
        this.t += dt;
        if (this.step === "burrow_dive" && this.t > 0.5) {
            // Into the dirt floor : invisible, follows the player under the ground.
            const r = Voxels.destroyCircle(vec3(this.position.x, this.position.y - 3, 0), 3, { flags: "SOFT", cells: true });
            Game.debris(r.cells, 8);
            this.anim.visible = false;
            this.movement_enabled = false;
            this.step = "burrow_under";
            this.t = 0;
            return BT.Running;
        }
        if (this.step === "burrow_under") {
            if (t) {
                const dx = t.position.x - this.position.x;
                this.position.x += Math.sign(dx) * Math.min(Math.abs(dx), 18 * dt);
            }
            if (Math.random() < 0.4)
                Game.dust(vec3(this.position.x + (Math.random() - 0.5) * 4, Game.Y0 + 14.4, 1), 1);
            if (this.t > 1.6) {
                this.step = "burrow_erupt";
                this.t = 0;
                this.anim.visible = true;
                this.movement_enabled = true;
                this.Launch(0, 30, true, true);
                const r = Voxels.destroyCircle(vec3(this.position.x, Game.Y0 + 13, 0), 3, { flags: "SOFT", cells: true });
                Game.debris(r.cells, 8);
                Game.hurtCircle(vec3(this.position.x, Game.Y0 + 16, 0), 4.5, 1, this, [this]);
                Game.shake();
                this.anim.setTrigger("slam");
            }
            return BT.Running;
        }
        // Fills the holes back a little (the floor stays playable).
        if (this.step === "burrow_erupt" && this.t > 0.2 && this.IsGrounded()) {
            Voxels.fillRect(vec3(this.position.x, Game.Y0 + 12, 0), { x: 10, y: 4 }, "Dirt");
            return true;
        }
        return this.t > 2 ? true : BT.Running;
    }

    Stalk(dt) {
        const t = this.Target();
        if (!t) return false;
        this.Begin("stalk");
        this.t += dt;
        this.move_speed = 9 * this.Speed();
        const dx = t.position.x - this.position.x;
        this.Move(Math.abs(dx) > 4 ? Math.sign(dx) : 0);
        this.Face(t.position.x);
        if (this.t > 1.2) {
            this.Move(0);
            return true;
        }
        return BT.Running;
    }

    Recover(dt) {
        if (this.Begin("recover")) {
            this.Move(0);
            this.anim.setBool("charging", false);
            this.anim.setBool("winding", false);
        }
        this.t += dt;
        return this.t > (this.phase === 2 ? 0.35 : 0.8) ? true : BT.Running;
    }

    OnStomp() {
        if (Math.random() < 0.6)
        Sfx.play("stomp", this.position, { volume: 0.45, gap: 150 });
            Game.dust(vec3(this.position.x + (Math.random() - 0.5) * 4, this.position.y - 2.4, 1), 1);
    }

    // ---- Every frame ---------------------------------------------------------------
    Update(dt) {
        if (this.flash > 0) {
            this.flash -= dt;
            this.anim.visible = this.step === "burrow_under" ? false : Math.floor(this.flash * 30) % 2 === 0;
            if (this.flash <= 0 && this.step !== "burrow_under") this.anim.visible = true;
        }
        this.anim.setFloat("speed", Math.abs(this.GetVelocity().x));
        // Contact damage (not while under the ground).
        if (this.awake && !this.dead && this.step !== "burrow_under")
            EnemyKit.contact(this, 3, 2.4);
    }

    // ---- Damage ------------------------------------------------------------------------
    OnNailHit(amount, attacker, dirX, dirY) {
        if (!this.awake || this.dead || this.step === "burrow_under")
            return "none";
        this.TakeDamage(amount, attacker);
        return "hit";
    }

    TakeDamage(amount, instigator) {
        if (!this.awake || this.dead || this.step === "burrow_under")
            return;
        this.hp -= amount;
        this.flash = 0.12;
        const p = Game.player();
        Sfx.play("hit", this.position, { volume: 0.6 });
        if (p && p.hud && p.hud.valid)
            p.hud.find("BossBar").percent = Math.max(0, this.hp / this.max_hp);
        if (this.hp <= 0)
            this.Die();
    }

    Die() {
        this.dead = true;
        this.ai.stop();
        this.Move(0);
        Sfx.stopMusic();
        Sfx.play("explosion");
        this.anim.setTrigger("die");
        Engine.setTimeDilation(0.2, 2);
        Game.shake();
        for (let i = 0; i < 30; ++i)
            Level.spawn("Geo", vec3(this.position.x, this.position.y + 2, 1)).Pop();
        Game.burst("sprites/soul.png", 2, this.position, 30, 18, { size: 1.2, life: 1.2 });
        this.SetHud(false);
        const p = Game.player();
        if (p) p.ShowMessage("The Warden falls.  The way to the heart is open.", 4);
        for (const a of Level.findImplementing("GameEvents"))
            a.send("GameEvents", "OnBossDefeated", this);
        this.lifetime = 3;
    }
}
