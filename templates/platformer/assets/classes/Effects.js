// Small effects, spawned by the gameplay : Level.spawn("Sparkle", vec3(x, y, 0)).

class Sparkle extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 1, y: 1 } });
    }

    BeginPlay() {
        this.time = 0;
        this.lifetime = 0.35;
    }

    Update(dt) {
        this.time += dt;
        const s = 1 + this.time * 14;
        this.sprite.size = { x: s, y: s };
    }
}

// A piece of a broken voxel : flies, falls, disappears.
class Debris extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 1.2, y: 1.2 } });
    }

    BeginPlay() {
        this.vx = (Math.random() - 0.5) * 24;
        this.vy = 12 + Math.random() * 14;
        this.lifetime = 0.7 + Math.random() * 0.4;
    }

    Update(dt) {
        this.vy -= 60 * dt;
        this.position.x += this.vx * dt;
        this.position.y += this.vy * dt;
    }
}
