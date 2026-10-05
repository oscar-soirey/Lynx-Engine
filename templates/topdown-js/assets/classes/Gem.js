// A gem : spins a little, picked up when the hero walks on it.
// When the last one is picked up, the level is done.

class Gem extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", {
            texture: "sprites/gem.png",
            size: { x: 2.7, y: 2.7 },   // voxels
        });
    }

    BeginPlay() {
        this.time = Math.random() * 6.0;
    }

    Update(dt) {
        this.time += dt;
        this.sprite.size = { x: 2.7 * Math.abs(Math.cos(this.time * 2.0)) + 0.3, y: 2.7 };

        const hero = Level.find("hero");
        if (!hero)
            return;

        const dx = hero.position.x - this.position.x;
        const dy = hero.position.y - this.position.y;

        if (dx * dx + dy * dy < 2.3 * 2.3) {
            const left = Level.count("Gem") - 1;
            print(left > 0 ? "Gem ! " + left + " left" : "All the gems : well done !");
            this.destroy();
        }
    }
}
