// The creatures of the Hollow.
//
//   Crawler   walks, turns at walls and ledges (plain JS)
//   Hopper    leaps at the player                (Behavior Tree ai/Hopper.bt)
//   Spitter   flies and spits, parry the spit    (ai/Spitter.bt)
//   Knight    shield up, telegraphed thrust      (ai/Knight.bt) : hit it from
//             behind, or parry the thrust (stunned : double damage)
//   Burrower  swims through the dirt, erupts under the player (ai/Burrower.bt)
//
// The functions used by the trees (CallFunction nodes) are methods of the
// classes : they return true (success), false (failure) or BT.Running.

// ---- Shared code (hit flash, knockback, contact damage, death) -------------------
class EnemyKit {
    static setup(e, hp, anims, size, offset) {
        e.addTag("enemy");
        e.hp = hp;
        e.maxHp = hp;
        e.anims = anims;
        e.flash = 0;
        e.current = "";
        e.anim = e.addComponent("AnimationSprite", { size: size, offset: offset || vec3(0, 0, 0) });
        EnemyKit.play(e, Object.keys(anims)[0]);
    }

    static play(e, name) {
        if (e.current === name || !e.anims[name])
            return;
        e.current = name;
        if (e.flash <= 0) {
            const a = e.anims[name];
            e.anim.setAnimation(a[0], a[1], a[2], a.length > 3 ? a[3] : true);
        }
    }

    // White flash : the "hurt" sprite for a moment.
    static update(e, dt) {
        if (e.flash > 0) {
            e.flash -= dt;
            if (e.flash <= 0) {
                const name = e.current;
                e.current = "";
                EnemyKit.play(e, name);
            }
        }
    }

    static hit(e, amount, instigator, knock) {
        if (e.hp <= 0)
            return false;
        e.hp -= amount;
        if (e.anims.hurt) {
            const h = e.anims.hurt;
            e.anim.setAnimation(h[0], h[1], h[2], true);
            e.flash = 0.09;
        }
        if (knock && e.Launch && instigator && instigator.valid) {
            const dir = e.position.x >= instigator.position.x ? 1 : -1;
            e.Launch(dir * knock, knock * 0.4, true, false);
        }
        if (e.hp <= 0) {
            EnemyKit.die(e);
            return true;
        }
        return false;
    }

    static die(e) {
        const p = e.position.clone();
        for (let i = 0; i < (e.geo || 3); ++i)
        Sfx.play("enemy_die", p);
            Level.spawn("Geo", vec3(p.x, p.y + 0.5, 1)).Pop();
        Game.burst("sprites/dust.png", 4, p, 8, 10, { size: 1.6, life: 0.5 });
        Game.spark(p);
        e.destroy();
    }

    // Damages the player when it touches the box.
    static contact(e, halfW, halfH) {
        const p = Game.player();
        if (!p || e.hp <= 0)
            return;
        if (Math.abs(p.position.x - e.position.x) < halfW + 0.7 && Math.abs(p.position.y - e.position.y) < halfH + 1.2)
            p.send("Damageable", "TakeDamage", 1, e);
    }

    static solidAt(x, y, w, h) {
        return Physics.overlapBox(vec3(x, y, 0), { x: w, y: h }, { actors: false, ignoreTypes: ["Acid"] }).any;
    }
}
globalThis.EnemyKit = EnemyKit;


// =============================================================================
// Crawler
// =============================================================================
class Crawler extends Humanoid {
    static interfaces = ["Damageable"];
    static properties = { speed: 4.5 };

    constructor() {
        super();
        this.collider_size = { x: 2.2, y: 1.3 };
        this.move_speed = 4.5;
        this.collision_layer = 2;
        this.collision_mask = 0;
        this.face_movement_direction = true;
        EnemyKit.setup(this, 3, {
            walk: ["sprites/crawler_walk.png", 4, 0.12],
            hurt: ["sprites/crawler_hurt.png", 1, 1],
        }, { x: 3, y: 2 }, vec3(0, 0.25, 0));
        this.dir = Math.random() < 0.5 ? 1 : -1;
        this.geo = 2;
        this.turnCooldown = 0;
    }

    Update(dt) {
        EnemyKit.update(this, dt);
        this.turnCooldown -= dt;
        if (this.IsGrounded() && this.turnCooldown <= 0) {
            const ahead = this.position.x + this.dir * 1.6;
            const wall = EnemyKit.solidAt(ahead, this.position.y + 0.2, 0.4, 0.8);
            const ground = EnemyKit.solidAt(ahead, this.position.y - 1.1, 0.6, 0.6);
            if (wall || !ground) {
                this.dir = -this.dir;
                this.turnCooldown = 0.3;
            }
        }
        this.Move(this.dir);
        this.move_speed = this.speed;
        EnemyKit.contact(this, 1.1, 0.65);
    }

    TakeDamage(amount, instigator) {
        EnemyKit.hit(this, amount, instigator, 14);
    }
}


// =============================================================================
// Hopper
// =============================================================================
class Hopper extends Humanoid {
    static interfaces = ["Damageable"];

    constructor() {
        super();
        this.collider_size = { x: 2, y: 1.8 };
        this.move_speed = 0;
        this.collision_layer = 2;
        this.collision_mask = 0;
        this.air_control = 0;
        this.gravity = 70;
        EnemyKit.setup(this, 4, {
            idle: ["sprites/hopper_idle.png", 2, 0.4],
            crouch: ["sprites/hopper_crouch.png", 1, 1],
            jump: ["sprites/hopper_jump.png", 1, 1],
            hurt: ["sprites/hopper_hurt.png", 1, 1],
        }, { x: 3, y: 3 }, vec3(0, 0.55, 0));
        this.geo = 3;
        this.t = 0;
    }

    BeginPlay() {
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Hopper.bt" });
    }

    Target() {
        const t = this.ai.blackboard.get("target");
        return t && t.valid ? t : null;
    }

    // ---- Behavior tree ------------------------------------------------------------
    Crouch(dt) {
        if (!this.IsGrounded())
            return BT.Running;
        if (this.current !== "crouch") {
            EnemyKit.play(this, "crouch");
            this.t = 0;
        }
        this.t += dt;
        return this.t > 0.35 ? true : BT.Running;
    }

    Leap(dt) {
        const target = this.Target();
        if (this.current !== "jump") {
            if (!target)
                return false;
            const dx = target.position.x - this.position.x;
            this.Launch(Math.max(-18, Math.min(18, dx * 1.4)), 26, true, true);
            this.transform.scale.x = dx < 0 ? -1 : 1;
            EnemyKit.play(this, "jump");
            this.t = 0;
            return BT.Running;
        }
        this.t += dt;
        if (this.t > 0.2 && this.IsGrounded()) {
            this.SetVelocity(0, 0);
            EnemyKit.play(this, "idle");
            Game.dust(vec3(this.position.x, this.position.y - 0.8, 1), 2);
            return true;
        }
        return BT.Running;
    }

    Idle(dt) {
        EnemyKit.play(this, "idle");
        if (this.IsGrounded())
            this.SetVelocity(0, this.GetVelocity().y);
        return true;
    }

    Update(dt) {
        EnemyKit.update(this, dt);
        EnemyKit.contact(this, 1, 0.9);
    }

    TakeDamage(amount, instigator) {
        EnemyKit.hit(this, amount, instigator, 12);
    }
}


// =============================================================================
// Spitter (flying)
// =============================================================================
class Spitter extends Actor {
    static interfaces = ["Damageable"];

    constructor() {
        super();
        EnemyKit.setup(this, 3, {
            fly: ["sprites/spitter_fly.png", 4, 0.08],
            shoot: ["sprites/spitter_shoot.png", 2, 0.12],
            hurt: ["sprites/spitter_hurt.png", 1, 1],
        }, { x: 3, y: 3 });
        this.box = this.addComponent("Collider", { size: { x: 1.8, y: 1.8 }, trigger: false, layer: 2, mask: 0 });
        this.vx = 0; this.vy = 0;
        this.geo = 3;
        this.t = 0;
        this.knock = { x: 0, y: 0 };
    }

    BeginPlay() {
        this.home = this.position.clone();
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Spitter.bt" });
    }

    Target() {
        const t = this.ai.blackboard.get("target");
        return t && t.valid ? t : null;
    }

    FlyTo(x, y, speed, dt) {
        const dx = x - this.position.x, dy = y - this.position.y;
        const d = Math.hypot(dx, dy);
        const wantX = d > 0.3 ? dx / d * speed : 0, wantY = d > 0.3 ? dy / d * speed : 0;
        this.vx += (wantX - this.vx) * Math.min(1, dt * 3);
        this.vy += (wantY - this.vy) * Math.min(1, dt * 3);
        return d;
    }

    // ---- Behavior tree ------------------------------------------------------------
    CanSpit() {
        const t = this.Target();
        return !!t && this.ai.blackboard.get("distance", 999) < 26 &&
               Physics.lineOfSight(this.position, t.position, { ignore: [this, t], actors: false });
    }

    Spit(dt) {
        const t = this.Target();
        if (!t)
            return false;
        EnemyKit.play(this, "shoot");
        Sfx.play("spit", this.position, { volume: 0.6 });
        const dx = t.position.x - this.position.x, dy = t.position.y - this.position.y;
        const d = Math.max(0.1, Math.hypot(dx, dy));
        const s = Level.spawn("Spit", vec3(this.position.x + dx / d * 1.5, this.position.y + dy / d * 1.5, 1));
        s.Fire({ x: dx / d, y: dy / d }, 17, this);
        this.transform.scale.x = dx < 0 ? -1 : 1;
        return true;
    }

    Hover(dt) {
        const t = this.Target();
        if (!t)
            return false;
        EnemyKit.play(this, "fly");
        const side = this.position.x < t.position.x ? -1 : 1;
        this.FlyTo(t.position.x + side * 9, t.position.y + 7, 9, dt);
        this.transform.scale.x = t.position.x < this.position.x ? -1 : 1;
        this.t += dt;
        if (this.t > 0.6) { this.t = 0; return true; }
        return BT.Running;
    }

    Drift(dt) {
        EnemyKit.play(this, "fly");
        this.wander = (this.wander || 0) + dt;
        this.FlyTo(this.home.x + Math.sin(this.wander * 0.7) * 6, this.home.y + Math.sin(this.wander * 1.3) * 2, 4, dt);
        return true;
    }

    Update(dt) {
        EnemyKit.update(this, dt);
        this.knock.x *= Math.max(0, 1 - dt * 6);
        this.knock.y *= Math.max(0, 1 - dt * 6);
        const r = this.box.moveAndCollide({ x: (this.vx + this.knock.x) * dt, y: (this.vy + this.knock.y) * dt, z: 0 });
        if (r.blockedX) this.vx = 0;
        if (r.blockedY) this.vy = 0;
        EnemyKit.contact(this, 0.9, 0.9);
    }

    TakeDamage(amount, instigator) {
        if (instigator && instigator.valid) {
            const dx = this.position.x - instigator.position.x, dy = this.position.y - instigator.position.y;
            const d = Math.max(0.1, Math.hypot(dx, dy));
            this.knock = { x: dx / d * 22, y: dy / d * 22 };
        }
        EnemyKit.hit(this, amount, instigator, 0);
    }
}


// =============================================================================
// Shield Knight
// =============================================================================
class Knight extends Humanoid {
    static interfaces = ["Damageable", "NailTarget", "Parryable"];

    constructor() {
        super();
        this.collider_size = { x: 2.2, y: 3.6 };
        this.move_speed = 5;
        this.collision_layer = 2;
        this.collision_mask = 0;
        this.face_movement_direction = true;
        EnemyKit.setup(this, 10, {
            walk: ["sprites/knight_walk.png", 4, 0.15],
            guard: ["sprites/knight_guard.png", 1, 1],
            windup: ["sprites/knight_windup.png", 2, 0.15],
            attack: ["sprites/knight_attack.png", 3, 0.1, false],
            hurt: ["sprites/knight_hurt.png", 1, 1],
        }, { x: 5, y: 5 }, vec3(0, 0.65, 0));
        this.geo = 8;
        this.t = 0;
        this.stun = 0;
        this.thrusting = false;
    }

    BeginPlay() {
        this.home = this.position.clone();
        this.patrolDir = 1;
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Knight.bt" });
    }

    Target() {
        const t = this.ai.blackboard.get("target");
        return t && t.valid ? t : null;
    }

    Facing() { return this.transform.scale.x < 0 ? -1 : 1; }

    FaceTarget(t) {
        const d = t.position.x - this.position.x;
        if (Math.abs(d) > 0.5)
            this.transform.scale.x = d < 0 ? -Math.abs(this.transform.scale.x) : Math.abs(this.transform.scale.x);
    }

    // ---- Behavior tree ------------------------------------------------------------
    CanThrust() {
        const t = this.Target();
        return !!t && this.stun <= 0 && Math.abs(t.position.x - this.position.x) < 7 && Math.abs(t.position.y - this.position.y) < 4;
    }

    Patrol(dt) {
        if (this.stun > 0) { this.Move(0); return BT.Running; }
        EnemyKit.play(this, "walk");
        if (EnemyKit.solidAt(this.position.x + this.patrolDir * 1.8, this.position.y, 0.4, 1.5) ||
            !EnemyKit.solidAt(this.position.x + this.patrolDir * 1.8, this.position.y - 2.2, 0.6, 0.6) ||
            Math.abs(this.position.x + this.patrolDir - this.home.x) > 12)
            this.patrolDir = -this.patrolDir;
        this.move_speed = 3;
        this.Move(this.patrolDir);
        return BT.Running;
    }

    Approach(dt) {
        const t = this.Target();
        if (!t) return false;
        if (this.stun > 0) { this.Move(0); return BT.Running; }
        EnemyKit.play(this, "guard");
        this.move_speed = 4;
        const dx = t.position.x - this.position.x;
        if (Math.abs(dx) > 3)
            this.Move(Math.sign(dx));
        else
            this.Move(0);
        this.FaceTarget(t);
        return BT.Running;
    }

    Windup(dt) {
        const t = this.Target();
        if (this.current !== "windup") {
            if (!t) return false;
            this.Move(0);
            this.FaceTarget(t);
            EnemyKit.play(this, "windup");
            Sfx.play("clink", this.position, { volume: 0.4 });
            this.t = 0;
        }
        if (this.stun > 0) return false;
        this.t += dt;
        return this.t > 0.55 ? true : BT.Running;
    }

    Thrust(dt) {
        if (this.current !== "attack") {
            EnemyKit.play(this, "attack");
            this.Launch(this.Facing() * 24, 3, true, true);
            this.t = 0;
            this.thrusting = true;
        }
        if (this.stun > 0) { this.thrusting = false; return false; }
        this.t += dt;
        // The spear : a box in front during the lunge.
        if (this.thrusting && this.t < 0.3) {
            const p = Game.player();
            const cx = this.position.x + this.Facing() * 3.5;
            if (p && Math.abs(p.position.x - cx) < 2.6 && Math.abs(p.position.y - this.position.y) < 2.2)
                p.send("Damageable", "TakeDamage", 1, this);
        }
        if (this.t > 0.4) {
            this.thrusting = false;
            EnemyKit.play(this, "walk");
            return true;
        }
        return BT.Running;
    }

    // Parried during the thrust : stunned (open to attacks).
    OnParried(by) {
        this.thrusting = false;
        this.stun = 1.4;
        Sfx.play("clink", this.position);
        this.Launch(-this.Facing() * 14, 6, true, true);
        EnemyKit.play(this, "walk");
    }

    // The shield blocks the front hits (when not stunned, not attacking).
    OnNailHit(amount, attacker, dirX, dirY) {
        const fromFront = attacker && (attacker.position.x - this.position.x) * this.Facing() > 0;
        const guarding = this.stun <= 0 && (this.current === "guard" || this.current === "walk") && dirY === 0;
        if (fromFront && guarding) {
            this.Launch(-this.Facing() * 4, 0, false, false);
            return "blocked";
        }
        this.TakeDamage(this.stun > 0 ? amount * 2 : amount, attacker);
        return "hit";
    }

    Update(dt) {
        EnemyKit.update(this, dt);
        if (this.stun > 0) {
            this.stun -= dt;
            this.anim.visible = Math.floor(this.stun * 10) % 2 === 0;
            if (this.stun <= 0) this.anim.visible = true;
        }
        this.move_speed = Math.max(this.move_speed, 0);
        EnemyKit.contact(this, 1.1, 1.8);
    }

    TakeDamage(amount, instigator) {
        EnemyKit.hit(this, amount, instigator, 8);
    }
}


// =============================================================================
// Burrower : moves through the ground (digs it)
// =============================================================================
class Burrower extends Actor {
    static interfaces = ["Damageable"];

    constructor() {
        super();
        EnemyKit.setup(this, 5, {
            idle: ["sprites/burrower_idle.png", 2, 0.2],
            emerge: ["sprites/burrower_emerge.png", 4, 0.07, false],
            hurt: ["sprites/burrower_hurt.png", 1, 1],
        }, { x: 4, y: 4 }, vec3(0, 0.6, 0));
        this.box = this.addComponent("Collider", { size: { x: 2, y: 3 }, trigger: true, movable: false, collideWithVoxels: false });
        this.geo = 5;
        this.underground = true;
        this.t = 0;
    }

    BeginPlay() {
        this.home = this.position.clone();
        this.Hide(true);
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Burrower.bt" });
    }

    Hide(hidden) {
        this.underground = hidden;
        this.anim.visible = !hidden;
    }

    Target() {
        const t = this.ai.blackboard.get("target");
        return t && t.valid ? t : null;
    }

    // Floor height under x (first solid voxel going down from y).
    FloorAt(x, y) {
        const hit = Physics.raycast(vec3(x, y, 0), vec3(x, y - 30, 0), { actors: false, ignoreTypes: ["Acid"] });
        return hit ? hit.point.y : y - 3;
    }

    // ---- Behavior tree ------------------------------------------------------------
    Lurk(dt) {
        this.Hide(true);
        return true;
    }

    Burrow(dt) {
        const t = this.Target();
        if (!t)
            return false;
        if (!this.underground) {
            // Dive : a hole in the ground.
            this.Hide(true);
            Voxels.destroyCircle(vec3(this.position.x, this.position.y - 1, 0), 1.8, { flags: "SOFT", cells: false });
            Game.dust(this.position, 6);
            this.t = 0;
        }
        this.t += dt;
        // Swims under the floor toward the player, digging a tunnel.
        const floor = this.FloorAt(t.position.x, t.position.y + 1);
        const tx = t.position.x, ty = floor - 3;
        const dx = tx - this.position.x, dy = ty - this.position.y;
        const d = Math.hypot(dx, dy);
        const step = Math.min(d, 13 * dt);
        if (d > 0.01) {
            this.position.x += dx / d * step;
            this.position.y += dy / d * step;
        }
        if (Math.random() < 0.3)
            Voxels.destroyCircle(this.position, 1.3, { flags: "SOFT", cells: false });
        // Dust at the surface : the player can see it coming.
        if (Math.random() < 0.25)
            Game.dust(vec3(this.position.x, this.FloorAt(this.position.x, this.position.y + 6) + 0.3, 1), 1);
        return d < 1 || this.t > 3 ? true : BT.Running;
    }

    Erupt(dt) {
        if (this.underground) {
            this.t = 0;
            this.underground = false;
            this.eruptFrom = this.position.clone();
            Sfx.play("break_rock", this.position, { volume: 0.7 });
            this.surface = this.FloorAt(this.position.x, this.position.y + 8);
        }
        this.t += dt;
        if (this.t < 0.45) {
            // Warning : the ground trembles.
            if (Math.random() < 0.5)
                Game.dust(vec3(this.position.x + (Math.random() - 0.5) * 3, this.surface + 0.3, 1), 1);
            return BT.Running;
        }
        if (!this.anim.visible) {
            this.anim.visible = true;
            EnemyKit.play(this, "emerge");
            const r = Voxels.destroyCircle(vec3(this.position.x, this.surface, 0), 2.4, { flags: "SOFT", cells: true });
            Game.debris(r.cells, 6);
            Game.shake();
        }
        // Rises above the surface.
        const goal = this.surface + 1.8;
        this.position.y += (goal - this.position.y) * Math.min(1, dt * 12);
        const p = Game.player();
        if (p && Math.abs(p.position.x - this.position.x) < 2 && Math.abs(p.position.y - this.position.y) < 3)
            p.send("Damageable", "TakeDamage", 1, this);
        return this.t > 0.8 ? true : BT.Running;
    }

    Exposed(dt) {
        EnemyKit.play(this, "idle");
        this.t += dt;
        EnemyKit.contact(this, 1, 1.5);
        return this.t > 2.2 ? true : BT.Running;
    }

    Update(dt) {
        EnemyKit.update(this, dt);
    }

    TakeDamage(amount, instigator) {
        if (this.underground)
            return;
        EnemyKit.hit(this, amount, instigator, 0);
    }
}
