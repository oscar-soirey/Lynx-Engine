// A bullet : moves in straight line, a Physics.raycast between its previous and
// next position finds what it hits (no collider needed, never goes through a
// thin wall). Cover voxels (flag "COVER") are destroyed around the impact.

class Bullet extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/bullet.png", size: { x: 2, y: 2 } });
        // Defaults here, not in BeginPlay : Fire() is called right after
        // Level.spawn, before the first BeginPlay of the new actor.
        this.dir = { x: 1, y: 0 };
        this.speed = 60;
        this.damage = 10;
        this.owner = null;
        this.blast = 1.3;               // radius destroyed in the cover
    }

    BeginPlay() {
        this.lifetime = 2;
    }

    Fire(dir, speed, damage, owner) {
        this.dir = dir;
        this.speed = speed;
        this.damage = damage;
        this.owner = owner;
        // Rotation of the sprite along the direction (degrees, around Z).
        this.transform.rotation.z = Math.atan2(dir.y, dir.x) * 180 / Math.PI;
    }

    Update(dt) {
        const from = vec3(this.position.x, this.position.y, 0);
        const to = vec3(from.x + this.dir.x * this.speed * dt, from.y + this.dir.y * this.speed * dt, 0);
        const ignore = this.owner && this.owner.valid ? [this, this.owner] : [this];
        const hit = Physics.raycast(from, to, { ignore: ignore });
        if (!hit) {
            this.position.set(to.x, to.y, 1);
            return;
        }
        if (hit.actor) {
            if (hit.actor.implements("Damageable") && !this.SameTeam(hit.actor))
                hit.actor.send("Damageable", "TakeDamage", this.damage, this.owner);
        } else if (hit.voxel) {
            Voxels.destroyCircle(hit.point, this.blast, { flags: "COVER" });
            Sfx.play("clink", hit.point, { volume: 0.3, gap: 60 });
        }
        Level.spawn("Spark", vec3(hit.point.x, hit.point.y, 1));
        this.destroy();
    }

    SameTeam(actor) {
        return this.owner && this.owner.valid && actor.hasTag("enemy") === this.owner.hasTag("enemy");
    }
}

class EnemyBullet extends Bullet {
    constructor() {
        super();
        this.sprite.texture = "sprites/enemy_bullet.png";
        this.blast = 0.8;
    }
}

class Spark extends Actor {
    constructor() {
        super();
        this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 2, y: 2 } });
    }

    BeginPlay() {
        this.lifetime = 0.08;
    }
}

class Explosion extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 9, y: 9 } });
        this.anim.setAnimation("sprites/explosion.png", 6, 0.06, false);
        this.light = this.addComponent("Light", { type: "point", color: vec3(1, 0.6, 0.3), intensity: 5, offset: vec3(0, 0, 6) });
    }

    BeginPlay() {
        this.time = 0;
        this.lifetime = 0.4;
    }

    Update(dt) {
        this.time += dt;
        this.light.intensity = Math.max(0, 5 * (1 - this.time / 0.4));
    }
}
