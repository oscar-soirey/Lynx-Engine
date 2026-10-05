// A coin : floats up and down, collected when the player touches it.
// JavaScript class (see scripting/README.md in the engine) : it shows up in
// "Place Actors" like a C++ class ; "value" is editable in Details.

class Coin extends Actor {
    static properties = {
        value: { value: 1, type: "int" },
    };

    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", {
            texture: "sprites/coin.png",
            size: { x: 2.7, y: 2.7 },   // voxels
        });
    }

    BeginPlay() {
        this.baseY = this.position.y;
        this.time = Math.random() * 6.0;
    }

    Update(dt) {
        this.time += dt;
        this.position.y = this.baseY + Math.sin(this.time * 3.0) * 0.4;

        const player = Level.find("player");
        if (!player)
            return;

        const dx = player.position.x - this.position.x;
        const dy = player.position.y - this.position.y;

        if (dx * dx + dy * dy < 2.5 * 2.5) {
            globalThis.coins = (globalThis.coins || 0) + this.value;
            print("Coins : " + globalThis.coins + " / " + (globalThis.coins + Level.count("Coin") - 1));
            this.destroy();
        }
    }

    EndPlay() {
        globalThis.coins = 0;
    }
}
