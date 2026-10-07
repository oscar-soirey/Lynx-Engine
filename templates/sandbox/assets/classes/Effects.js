// Effects spawned by the gameplay.

class Explosion extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 14, y: 14 } });
        this.anim.setAnimation("sprites/explosion.png", 6, 0.06, false);
        this.light = this.addComponent("Light", { type: "point", color: vec3(1, 0.7, 0.3), intensity: 6, offset: vec3(0, 0, 6) });
    }

    BeginPlay() {
        this.time = 0;
        this.lifetime = 0.45;
    }

    Update(dt) {
        this.time += dt;
        this.light.intensity = Math.max(0, 6 * (1 - this.time / 0.4));
    }
}

class Spark extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 1.6, y: 1.6 } });
    }

    BeginPlay() {
        this.lifetime = 0.08;
    }
}

class Debris extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 1, y: 1 } });
    }

    BeginPlay() {
        this.vx = (Math.random() - 0.5) * 30;
        this.vy = 8 + Math.random() * 18;
        this.lifetime = 0.6 + Math.random() * 0.4;
    }

    Update(dt) {
        this.vy -= 60 * dt;
        this.position.x += this.vx * dt;
        this.position.y += this.vy * dt;
    }
}

class AimCursor extends Actor {
    constructor() {
        super();
        this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 1.4, y: 1.4 } });
    }
}

// A crate near the start : open it (walk into it) for bombs and materials.
class SupplyCrate extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/chest_closed.png", size: { x: 3, y: 3 } });
        this.addComponent("Collider", { size: { x: 2.6, y: 2.4 }, trigger: true, movable: false });
    }

    BeginPlay() {
        this.opened = false;
    }

    OnBeginOverlap(other) {
        if (this.opened || !other.inventory)
            return;
        this.opened = true;
        this.sprite.texture = "sprites/chest_open.png";
        Sfx.play("chest");
        other.inventory.Stone = (other.inventory.Stone || 0) + 30;
        other.inventory.Sand = (other.inventory.Sand || 0) + 30;
        other.inventory.Water = (other.inventory.Water || 0) + 30;
        other.RefreshHud();
        other.ShowMessage("Coffre : pierre, sable et eau !", 2);
    }
}
