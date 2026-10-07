// Things to pick up : they talk to the "Collector" interface, they never touch
// the variables of the hero directly.

class Coin extends Actor {
    static properties = {
        value: { value: 1, type: "int" },
    };

    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 2.2, y: 2.2 } });
        this.anim.setAnimation("sprites/coin_spin.png", 4, 0.12, true);
        this.addComponent("Collider", { size: { x: 1.8, y: 1.8 }, trigger: true, movable: false });
    }

    BeginPlay() {
        this.baseY = this.position.y;
        this.time = Math.random() * 6;
    }

    Update(dt) {
        this.time += dt;
        this.position.y = this.baseY + Math.sin(this.time * 3) * 0.3;
    }

    OnBeginOverlap(other) {
        if (this.taken || !other.implements("Collector"))
            return;
        this.taken = true;
        other.send("Collector", "OnCollected", "coin", this.value);
        Level.spawn("Sparkle", this.position);
        this.destroy();
    }
}

class HeartPickup extends Coin {
    constructor() {
        super();
        this.anim.setAnimation("sprites/heart.png", 1, 1, false);
    }

    OnBeginOverlap(other) {
        if (this.taken || !other.implements("Collector"))
            return;
        this.taken = true;
        other.send("Collector", "OnCollected", "heart", 1);
        Level.spawn("Sparkle", this.position);
        this.destroy();
    }
}
