// Robot : its decisions come from the Behavior Tree ai/Robot.bt (AI component)
// which calls two methods of this class : CanShoot() (decorator) and Shoot()
// (task). It moves with the MoveTo task, which uses its Collider.

class Robot extends Actor {
    static interfaces = ["Damageable"];

    static properties = {
        max_hp: 40.0,
        shoot_range: 26.0,
        points: { value: 100, type: "int" },
    };

    constructor() {
        super();
        this.addTag("enemy");
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.4, y: 3.4 }, offset: vec3(0, 0.7, 0) });
        this.anim.setAnimation("sprites/robot.png", 2, 0.2, true);
        this.addComponent("Collider", { size: { x: 2, y: 1.8 } });
    }

    BeginPlay() {
        this.hp = this.max_hp;
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Robot.bt" });
    }

    // ---- Called by the Behavior Tree ----------------------------------------

    CanShoot() {
        const target = this.ai.blackboard.get("target");
        if (!target || !target.valid || this.ai.blackboard.get("distance", 999) > this.shoot_range)
            return false;
        // Line of sight : walls and cover block it (the target itself does not count).
        return Physics.lineOfSight(this.position, target.position, { ignore: [this, target] });
    }

    Shoot() {
        const target = this.ai.blackboard.get("target");
        if (!target || !target.valid)
            return false;
        const dx = target.position.x - this.position.x, dy = target.position.y - this.position.y;
        const d = Math.max(0.1, Math.hypot(dx, dy));
        const dir = { x: dx / d, y: dy / d };
        const bullet = Level.spawn("EnemyBullet", vec3(this.position.x + dir.x * 2, this.position.y + dir.y * 2, 1));
        bullet.Fire(dir, 35, 8, this);
        Sfx.play("laser", this.position, { volume: 0.4, gap: 60 });
        this.anim.flipX = dx < 0;
        return true;
    }

    // ---- Damageable ---------------------------------------------------------

    TakeDamage(amount, instigator) {
        if (this.hp <= 0)
            return;
        this.hp -= amount;
        Sfx.play("hit", this.position, { volume: 0.5, gap: 50 });
        if (this.hp > 0)
            return;
        Level.spawn("Explosion", this.position);
        Voxels.destroyCircle(this.position, 3, { flags: "COVER" });
        Sfx.play("explosion", this.position);
        if (Math.random() < 0.25)
            Level.spawn("HealthPack", vec3(this.position.x, this.position.y, 0));
        for (const d of Level.findImplementing("ArenaEvents"))
            d.send("ArenaEvents", "OnEnemyKilled", this, this.points);
        this.destroy();
    }
}

class HealthPack extends Actor {
    constructor() {
        super();
        this.addComponent("StaticSprite", { texture: "sprites/heart.png", size: { x: 2.2, y: 2.2 } });
        this.addComponent("Collider", { size: { x: 2, y: 2 }, trigger: true, movable: false });
    }

    BeginPlay() {
        this.lifetime = 12;
    }

    OnBeginOverlap(other) {
        if (this.taken || !other.implements("Healable"))
            return;
        this.taken = true;
        other.send("Healable", "Heal", 30);
        this.destroy();
    }
}
