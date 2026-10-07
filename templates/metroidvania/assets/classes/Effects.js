// Visual effects and small physical objects : slashes, sparks, dust, debris,
// geo, soul, bombs, projectiles, falling rocks, shockwaves, the hook.
//
// Every one of them is a small actor : Level.spawn("Fx", position) then
// Setup(...). They destroy themselves.

// ---- Helpers shared by the game classes --------------------------------------
class Game {
    // World origin of the map (see world.level) and the areas, in voxels.
    static X0 = 900;
    static Y0 = 1400;

    static player() {
        const p = Level.find("player");
        return p && p.valid ? p : null;
    }

    static fx(texture, frames, frameTime, position, size, opts) {
        const fx = Level.spawn("Fx", vec3(position.x, position.y, (opts && opts.z) || 1));
        fx.Setup(texture, frames, frameTime, size, opts || {});
        return fx;
    }

    static burst(texture, frames, position, count, speed, opts) {
        for (let i = 0; i < count; ++i) {
            const a = Math.random() * Math.PI * 2;
            const s = speed * (0.4 + Math.random() * 0.6);
            Game.fx(texture, frames, 0.08 + Math.random() * 0.05, position, (opts && opts.size) || 0.8,
                    { vx: Math.cos(a) * s, vy: Math.sin(a) * s + ((opts && opts.up) || 0), gravity: (opts && opts.gravity) || 0,
                      life: (opts && opts.life) || 0.5 });
        }
    }

    static dust(position, count = 4) {
        Game.burst("sprites/dust.png", 4, position, count, 4, { up: 2, size: 1.2, life: 0.4 });
    }

    static spark(position) {
        Game.fx("sprites/hit_spark.png", 4, 0.04, position, 2.4, { z: 2 });
    }

    // Debris of broken voxels (a few, not one per voxel).
    static debris(cells, max = 8) {
        const step = Math.max(1, Math.floor(cells.length / max));
        for (let i = 0; i < cells.length; i += step) {
            const c = cells[i];
            const f = Level.spawn("Debris", vec3(c.world.x, c.world.y, 1));
            f.Throw((Math.random() - 0.5) * 16, 6 + Math.random() * 10, c.typeName);
        }
    }

    // Geo from the crystals of a voxel edit result, and the dirt for the pouch.
    static loot(result, at) {
        let crystals = 0;
        if (result && result.types)
            crystals = result.types.Crystal || 0;
        for (let i = 0; i < Math.ceil(crystals / 2); ++i)
            Level.spawn("Geo", vec3(at.x + (Math.random() - 0.5) * 2, at.y + 0.5, 1)).Pop();
        return crystals;
    }

    static dirtIn(result) {
        if (!result || !result.types)
            return 0;
        const t = result.types;
        return (t.Dirt || 0) + (t.Moss || 0) + (t.Packed || 0) + (t.Sand || 0) + (t.Wood || 0);
    }

    // Damage everything (but `ignore`) that can take it in a circle.
    static hurtCircle(center, radius, amount, instigator, ignore) {
        const hits = Physics.overlapCircle(center, radius, { voxels: false, triggers: true, ignore: ignore || [] });
        for (const a of hits.actors)
            if (a.valid && a.implements("Damageable"))
                a.send("Damageable", "TakeDamage", amount, instigator);
        return hits.actors;
    }

    static shake(strength = 1) {
        Engine.cameraShake();
    }

    static hitStop(seconds) {
        Engine.setTimeDilation(0.05, seconds);
    }
}

// The class files are evaluated in their own scope : the helpers are made
// global (the actor classes are registered globally by the engine).
globalThis.Game = Game;

// A one-shot animated sprite, optionally moving (velocity, gravity).
class Fx extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 1, y: 1 } });
        this.vx = 0; this.vy = 0; this.gravity = 0; this.life = 0; this.loopAnim = false;
    }

    Setup(texture, frames, frameTime, size, opts) {
        this.anim.size = { x: size, y: size };
        this.anim.setAnimation(texture, frames, frameTime, !!opts.loop);
        this.vx = opts.vx || 0;
        this.vy = opts.vy || 0;
        this.gravity = opts.gravity || 0;
        this.life = opts.life || frames * frameTime;
        if (opts.flip)
            this.anim.flipX = true;
    }

    Update(dt) {
        this.vy -= this.gravity * dt;
        this.position.x += this.vx * dt;
        this.position.y += this.vy * dt;
        this.life -= dt;
        if (this.life <= 0)
            this.destroy();
    }
}

// The slash of the nail : follows its owner for a moment.
class SlashFx extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 6, y: 4 } });
        this.time = 0.18;
    }

    Attach(owner, dir, big, facing) {
        this.owner = owner;
        this.dir = dir;
        const tex = dir === "up" ? "sprites/slash_up.png" : dir === "down" ? "sprites/slash_down.png"
                  : big ? "sprites/slash_big.png" : "sprites/slash.png";
        if (dir === "up" || dir === "down")
            this.anim.size = { x: 4, y: 6 };
        else
            this.anim.size = big ? { x: 7.5, y: 5 } : { x: 6, y: 4 };
        this.anim.setAnimation(tex, 3, 0.05, false);
        this.facing = facing;
        this.anim.flipX = facing < 0 && dir === "side";
        this.Follow();
    }

    Follow() {
        if (!this.owner || !this.owner.valid)
            return;
        const p = this.owner.position;
        if (this.dir === "up")
            this.position.set(p.x + this.facing * 0.4, p.y + 3.2, 2);
        else if (this.dir === "down")
            this.position.set(p.x, p.y - 3.0, 2);
        else
            this.position.set(p.x + this.facing * 3.0, p.y + 0.3, 2);
    }

    Update(dt) {
        this.Follow();
        this.time -= dt;
        if (this.time <= 0)
            this.destroy();
    }
}

// A piece of a broken voxel : bounces once, fades.
class Debris extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/debris.png", size: { x: 0.6, y: 0.6 },
                                                          region: { x: 0, y: 0, z: 0.5, w: 1 } });
        this.vx = 0; this.vy = 0; this.life = 0.8 + Math.random() * 0.5;
    }

    Throw(vx, vy, typeName) {
        this.vx = vx; this.vy = vy;
        if (typeName === "Rock" || typeName === "Brick" || typeName === "Bedrock")
            this.sprite.region = { x: 0.5, y: 0, z: 1, w: 1 };
    }

    Update(dt) {
        this.vy -= 50 * dt;
        this.position.x += this.vx * dt;
        this.position.y += this.vy * dt;
        this.transform.rotation.z += this.vx * 30 * dt;
        this.life -= dt;
        if (this.life <= 0)
            this.destroy();
    }
}

// Geo : the currency (from enemies and crystals). Pops out, falls, is pulled
// toward the player when close.
class Geo extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 1.1, y: 1.1 } });
        this.anim.setAnimation("sprites/geo.png", 4, 0.1, true);
        this.box = this.addComponent("Collider", { size: { x: 0.8, y: 0.8 }, trigger: true, movable: false, collideWithVoxels: true });
        this.vx = 0; this.vy = 0; this.age = 0;
    }

    Pop() {
        this.vx = (Math.random() - 0.5) * 14;
        this.vy = 10 + Math.random() * 10;
    }

    Update(dt) {
        this.age += dt;
        const p = Game.player();
        if (p && this.age > 0.4) {
            const dx = p.position.x - this.position.x, dy = p.position.y - this.position.y;
            const d = Math.hypot(dx, dy);
            if (d < 1.6) {
                p.send("Collector", "OnCollected", "geo", 1);
                this.destroy();
                return;
            }
            if (d < 5) {
                this.vx = dx / d * 24;
                this.vy = dy / d * 24;
                this.position.x += this.vx * dt;
                this.position.y += this.vy * dt;
                return;
            }
        }
        this.vy -= 45 * dt;
        const r = this.box.moveAndCollide({ x: this.vx * dt, y: this.vy * dt, z: 0 });
        if (r.blockedY) { this.vy = Math.abs(this.vy) > 6 ? -this.vy * 0.35 : 0; this.vx *= 0.6; }
        if (r.blockedX) this.vx = -this.vx * 0.5;
        if (this.age > 30)
            this.destroy();
    }
}

// Soul : flies to the player when an enemy is hit (just a visual).
class SoulWisp extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 0.9, y: 0.9 } });
        this.anim.setAnimation("sprites/soul.png", 2, 0.1, true);
        this.t = 0;
        this.vx = (Math.random() - 0.5) * 12;
        this.vy = 6 + Math.random() * 8;
    }

    Update(dt) {
        this.t += dt;
        const p = Game.player();
        if (!p || this.t > 1.5) { this.destroy(); return; }
        const dx = p.position.x - this.position.x, dy = p.position.y - this.position.y;
        const d = Math.hypot(dx, dy);
        if (d < 1) { this.destroy(); return; }
        const k = Math.min(1, this.t * 1.5);
        this.vx += (dx / d * 30 - this.vx) * k * dt * 6;
        this.vy += (dy / d * 30 - this.vy) * k * dt * 6;
        this.position.x += this.vx * dt;
        this.position.y += this.vy * dt;
    }
}

// Bomb : thrown in an arc, explodes after its fuse. Breaks rock and dirt.
class Bomb extends Actor {
    static properties = { radius: 5.0, damage: 5 };

    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 1.4, y: 1.4 } });
        this.anim.setAnimation("sprites/bomb.png", 2, 0.12, true);
        this.box = this.addComponent("Collider", { size: { x: 1, y: 1 }, trigger: true, movable: false });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.6, 0.2), intensity: 0.5, offset: vec3(0, 0, 3) });
        this.vx = 0; this.vy = 0; this.fuse = 1.3;
    }

    Throw(vx, vy, owner) {
        this.vx = vx; this.vy = vy; this.owner = owner;
    }

    Update(dt) {
        this.fuse -= dt;
        this.anim.frameTime = this.fuse < 0.5 ? 0.04 : 0.12;
        this.vy -= 50 * dt;
        const r = this.box.moveAndCollide({ x: this.vx * dt, y: this.vy * dt, z: 0 });
        if (r.blockedY) { this.vy = -this.vy * 0.3; this.vx *= 0.6; }
        if (r.blockedX) this.vx = -this.vx * 0.4;
        if (this.fuse <= 0)
            this.Explode();
    }

    Explode() {
        const p = this.position.clone();
        const result = Voxels.destroyCircle(p, this.radius, { cells: true });
        Sfx.play("explosion", p, { volume: 1.0 });
        Game.debris(result.cells, 12);
        Game.loot(result, p);
        Game.hurtCircle(p, this.radius + 1, this.damage, this.owner, this.owner ? [this, this.owner] : [this]);
        // The owner is pushed, not hurt.
        if (this.owner && this.owner.valid) {
            const dx = this.owner.position.x - p.x, dy = this.owner.position.y - p.y;
            const d = Math.hypot(dx, dy);
            if (d < this.radius + 2 && this.owner.Launch)
                this.owner.Launch(dx / Math.max(d, 0.5) * 18, 16, true, true);
        }
        Game.fx("sprites/explosion.png", 6, 0.06, p, this.radius * 2.6, { z: 2 });
        Game.shake();
        this.destroy();
    }
}

// Projectile of the Spitter. Parry it : it goes back to its shooter.
class Spit extends Actor {
    static interfaces = ["Parryable"];

    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 1.2, y: 1.2 } });
        this.anim.setAnimation("sprites/spit.png", 2, 0.08, true);
        this.box = this.addComponent("Collider", { size: { x: 0.8, y: 0.8 }, trigger: true, movable: false });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.3, 0.7), intensity: 0.35, offset: vec3(0, 0, 2) });
        this.vx = 0; this.vy = 0; this.life = 4; this.reflected = false;
    }

    Fire(dir, speed, shooter) {
        this.vx = dir.x * speed; this.vy = dir.y * speed; this.shooter = shooter;
    }

    OnParried(by) {
        this.reflected = true;
        this.vx = -this.vx * 1.6; this.vy = -this.vy * 1.6;
        if (this.shooter && this.shooter.valid) {
            const dx = this.shooter.position.x - this.position.x, dy = this.shooter.position.y - this.position.y;
            const d = Math.max(0.1, Math.hypot(dx, dy));
            this.vx = dx / d * 34; this.vy = dy / d * 34;
        }
    }

    Update(dt) {
        this.life -= dt;
        const r = this.box.moveAndCollide({ x: this.vx * dt, y: this.vy * dt, z: 0 });
        if (r.blockedX || r.blockedY || this.life <= 0) {
            Game.spark(this.position);
            this.destroy();
            return;
        }
        const hits = Physics.overlapCircle(this.position, 0.7, { voxels: false, ignore: [this, this.shooter] });
        for (const a of hits.actors) {
            if (!a.valid || !a.implements("Damageable"))
                continue;
            const isPlayer = a.hasTag("player");
            if (isPlayer === this.reflected)
                continue;   // a reflected spit hurts the enemies only
            a.send("Damageable", "TakeDamage", this.reflected ? 3 : 1, this);
            // Parried by the player : TakeDamage turned it around, it flies on.
            if (this.valid && isPlayer && this.reflected)
                continue;
            Game.spark(this.position);
            this.destroy();
            return;
        }
    }
}

// A rock that falls from the ceiling (the Warden). Breaks on the ground.
class FallingRock extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/rock.png", size: { x: 2.2, y: 2.2 } });
        this.box = this.addComponent("Collider", { size: { x: 1.6, y: 1.6 }, trigger: true, movable: false });
        this.vy = 0; this.delay = 0.6;
    }

    Update(dt) {
        if (this.delay > 0) {
            // Warning : shakes in place before falling.
            this.delay -= dt;
            this.position.x += (Math.random() - 0.5) * 0.2;
            return;
        }
        this.vy -= 60 * dt;
        this.transform.rotation.z += 200 * dt;
        const r = this.box.moveAndCollide({ x: 0, y: this.vy * dt, z: 0 });
        const p = Game.player();
        if (p && Math.abs(p.position.x - this.position.x) < 1.6 && Math.abs(p.position.y - this.position.y) < 2.2)
            p.send("Damageable", "TakeDamage", 1, this);
        if (r.blockedY) {
            Game.dust(this.position, 5);
            Game.burst("sprites/debris.png", 2, this.position, 4, 10, { gravity: 40, up: 8, size: 0.6 });
            Sfx.play("rock_fall", this.position, { volume: 0.6 });
            this.destroy();
        }
    }
}

// The shockwave of the Warden's slam : runs along the floor.
class Shockwave extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3, y: 2.4 } });
        this.anim.setAnimation("sprites/dust.png", 4, 0.06, true);
        this.addComponent("Light", { type: "point", color: vec3(1, 0.8, 0.5), intensity: 0.6, offset: vec3(0, 0, 3) });
        this.dir = 1; this.life = 1.2;
    }

    Launch(dir) { this.dir = dir; }

    Update(dt) {
        this.position.x += this.dir * 24 * dt;
        this.life -= dt;
        // Stops at walls.
        const wall = Physics.overlapBox(vec3(this.position.x + this.dir * 1.2, this.position.y + 0.6, 0), { x: 0.5, y: 0.8 },
                                        { actors: false, ignoreTypes: ["Acid"] });
        if (wall.any || this.life <= 0) {
            this.destroy();
            return;
        }
        const p = Game.player();
        if (p && Math.abs(p.position.x - this.position.x) < 1.6 && p.position.y - this.position.y < 2.2 &&
            p.position.y > this.position.y - 0.5)
            p.send("Damageable", "TakeDamage", 1, this);
    }
}

// Hook tip and chain (drawn by the Wanderer while it pulls).
class HookTip extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/hook.png", size: { x: 1, y: 1 } });
    }
}

class ChainLink extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/chain.png", size: { x: 0.5, y: 0.5 } });
    }
}
