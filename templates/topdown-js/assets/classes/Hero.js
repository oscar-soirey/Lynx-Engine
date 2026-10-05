// The hero : moves in 4 directions with the "move_x" / "move_y" axes (input.json).
// "speed" is editable in the Details window and saved with the level.

class Hero extends Actor {
    static properties = {
        speed: 17.0,   // voxels per second
    };

    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", {
            texture: "sprites/player.png",
            size: { x: 3.3, y: 3.3 },   // voxels
        });
        // A lantern that follows the hero ("point", "spot", "directional" or "sky").
        this.lantern = this.addComponent("Light", {
            type: "point",
            color: { x: 1, y: 0.85, z: 0.6 },
            intensity: 0.8,
            offset: { x: 0, y: 0, z: 5 },
        });
    }

    Update(dt) {
        let x = Input.axis("move_x");
        let y = Input.axis("move_y");

        // Same speed in diagonal.
        const length = Math.sqrt(x * x + y * y);
        if (length > 1) {
            x /= length;
            y /= length;
        }

        this.position.x += x * this.speed * dt;
        this.position.y += y * this.speed * dt;

        if (Math.abs(x) > 0.1)
            this.sprite.flipX = x < 0;
    }
}
