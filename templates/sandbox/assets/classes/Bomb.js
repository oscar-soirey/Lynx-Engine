// A bomb : simple ballistic physics written in JS (gravity, bounces found with
// Physics.raycast), then an explosion that digs the world, hurts and pushes
// the actors around, and shows an effect with a flash of light.

class Bomb extends Actor {
    static properties = {
        fuse: 2.0,
        radius: 7.0,
        damage: 45.0,
    };

    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 2, y: 2 } });
        this.anim.setAnimation("sprites/bomb.png", 2, 0.12, true);
        // Here and not in BeginPlay : Throw() is called right after Level.spawn.
        this.vx = 0;
        this.vy = 0;
        this.time = 0;
        this.owner = null;
    }

    Throw(vx, vy, owner) {
        this.vx = vx;
        this.vy = vy;
        this.owner = owner;
    }

    Update(dt) {
        this.time += dt;
        if (this.time >= this.fuse) {
            this.Explode();
            return;
        }
        this.anim.frameTime = this.time > this.fuse - 0.6 ? 0.04 : 0.12;

        this.vy -= 60 * dt;
        const from = vec3(this.position.x, this.position.y, 0);
        const to = vec3(from.x + this.vx * dt, from.y + this.vy * dt, 0);
        const hit = Physics.raycast(from, to, { actors: false });
        if (hit && !hit.initialOverlap) {
            // Bounce : reflect the velocity on the surface, lose energy.
            const n = hit.normal;
            const dot = this.vx * n.x + this.vy * n.y;
            this.vx = (this.vx - 2 * dot * n.x) * 0.45;
            this.vy = (this.vy - 2 * dot * n.y) * 0.45;
            this.position.set(hit.point.x + n.x * 0.3, hit.point.y + n.y * 0.3, 0);
        } else {
            this.position.set(to.x, to.y, 0);
        }
    }

    Explode() {
        const center = vec3(this.position.x, this.position.y, 0);

        Sfx.play("explosion", center, { volume: 1.0 });
        // The world : everything destructible in the radius.
        const r = Voxels.destroyCircle(center, this.radius);
        for (let i = 0; i < r.cells.length; i += 25)
            Level.spawn("Debris", vec3(r.cells[i].world.x, r.cells[i].world.y, 0));

        // A puff of smoke where it was.
        Voxels.fillCircle(center, 2.5, "Smoke");

        // The actors : damage decreasing with the distance, and a push.
        const around = Physics.overlapCircle(center, this.radius + 2, { voxels: false });
        for (const actor of around.actors) {
            const dx = actor.position.x - center.x, dy = actor.position.y - center.y;
            const d = Math.max(0.5, Math.hypot(dx, dy));
            const k = 1 - Math.min(1, d / (this.radius + 2));
            if (actor.implements("Damageable"))
                actor.send("Damageable", "TakeDamage", this.damage * k, this.owner);
            if (actor.Launch)
                actor.Launch(dx / d * 40 * k, dy / d * 40 * k + 10, false, false);
        }

        Level.spawn("Explosion", center);
        this.destroy();
    }
}
