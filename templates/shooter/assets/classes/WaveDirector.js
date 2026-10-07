// The waves : spawns robots on the SpawnPoint actors, a bigger wave each time,
// counts the score. Receives the ArenaEvents of the robots and of the player.

class SpawnPoint extends Actor {
    constructor() {
        super();
        this.addTag("spawn");
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 2, y: 2 } });
    }

    BeginPlay() {
        this.sprite.visible = false;        // only visible in the editor
    }
}

class WaveDirector extends Actor {
    static interfaces = ["ArenaEvents"];

    static properties = {
        first_wave: { value: 3, type: "int" },
        per_wave: { value: 2, type: "int" },
        pause: 3.0,                 // seconds between two waves
        spawn_interval: 0.6,
    };

    BeginPlay() {
        this.hud = UI.create("ui/Waves.widget");
        this.hud.addToViewport(11);
        this.Restart();
    }

    Restart() {
        for (const robot of Level.findWithTag("enemy"))
            robot.destroy();
        this.wave = 0;
        this.score = 0;
        this.alive = 0;
        this.toSpawn = 0;
        this.timer = 1.5;
        this.over = false;
        this.Refresh("Prépare-toi !");
    }

    Update(dt) {
        if (this.over)
            return;
        this.timer -= dt;
        if (this.toSpawn > 0 && this.timer <= 0) {
            const spawns = Level.findWithTag("spawn");
            if (spawns.length > 0) {
                const at = spawns[Math.floor(Math.random() * spawns.length)].position;
                Level.spawn("Robot", vec3(at.x, at.y, 0));
                this.alive++;
            }
            this.toSpawn--;
            this.timer = this.spawn_interval;
        } else if (this.toSpawn === 0 && this.alive <= 0 && this.timer <= 0) {
            this.wave++;
            this.toSpawn = this.first_wave + (this.wave - 1) * this.per_wave;
            this.timer = this.pause;
            this.Refresh("Vague " + this.wave + " !");
            Sfx.play("wave_start");
        }
        if (this.bannerTime > 0 && (this.bannerTime -= dt) <= 0)
            this.hud.find("Banner").visibility = "Hidden";
    }

    OnEnemyKilled(enemy, points) {
        this.alive--;
        this.score += points;
        if (this.alive <= 0 && this.toSpawn === 0)
            this.timer = this.pause;
        this.Refresh();
    }

    OnPlayerDied(player) {
        this.over = true;
        this.Refresh("Game over ! Score " + this.score + "  (R pour rejouer)", 1000);
        Sfx.stopMusic();
    }

    Refresh(banner, seconds = 2) {
        if (!this.hud || !this.hud.valid)
            return;
        this.hud.find("Wave").text = "Vague " + Math.max(1, this.wave);
        this.hud.find("Score").text = "Score " + this.score;
        if (banner) {
            const b = this.hud.find("Banner");
            b.text = banner;
            b.visibility = "Visible";
            this.bannerTime = seconds;
        }
    }
}
