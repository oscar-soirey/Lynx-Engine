// Level actors : benches, ability shrines, hint stones, mask shards,
// lanterns, the boss arena, the relic (the end) ; and the pause menu.

// Bench : W to rest (heal, respawn point).
class Bench extends Actor {
    static properties = { title: "Bench" };

    constructor() {
        super();
        this.addComponent("StaticSprite", { texture: "sprites/bench.png", size: { x: 5, y: 2.5 }, offset: vec3(0, -0.2, 0) });
        this.addComponent("Collider", { size: { x: 6, y: 4 }, trigger: true, movable: false });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.8, 0.5), intensity: 0.9, offset: vec3(0, 3, 6) });
    }

    OnBeginOverlap(other) {
        if (other.hasTag("player"))
            other.send("Collector", "NearBench", this, true);
    }

    OnEndOverlap(other) {
        if (other.hasTag("player"))
            other.send("Collector", "NearBench", this, false);
    }
}

// Ability shrine : touch it to unlock its ability (dash, claw, wings, hook, bombs).
class AbilityShrine extends Actor {
    static properties = { ability: "dash" };

    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 4, y: 6 } });
        this.anim.setAnimation("sprites/shrine_on.png", 2, 0.3, true);
        this.addComponent("Collider", { size: { x: 4, y: 6 }, trigger: true, movable: false });
        this.light = this.addComponent("Light", { type: "point", color: vec3(0.4, 0.9, 1), intensity: 1.4, offset: vec3(0, 1, 5) });
        this.used = false;
    }

    OnBeginOverlap(other) {
        if (this.used || !other.hasTag("player"))
            return;
        Sfx.play("checkpoint");
        this.used = true;
        other.send("Collector", "UnlockAbility", this.ability);
        this.anim.setAnimation("sprites/shrine_off.png", 1, 1, true);
        this.light.intensity = 0.25;
        Game.burst("sprites/soul.png", 2, this.position, 16, 14, { size: 0.9, life: 0.7 });
    }
}

// A stone that shows a text when the player is near.
class HintStone extends Actor {
    static properties = { text: "Hello", radius: 6.0 };

    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/shrine_off.png", size: { x: 1.6, y: 2.4 } });
        this.shown = false;
    }

    Update(dt) {
        const p = Game.player();
        if (!p)
            return;
        const near = Math.abs(p.position.x - this.position.x) < this.radius && Math.abs(p.position.y - this.position.y) < 4;
        if (near !== this.shown) {
            this.shown = near;
            p.send("Collector", "ShowHint", this.text, near);
        }
    }
}

// Mask shard : one more mask. Hidden in pockets of the dirt.
class MaskShard extends Actor {
    constructor() {
        super();
        this.addComponent("StaticSprite", { texture: "sprites/mask_shard.png", size: { x: 1.8, y: 1.8 } });
        this.addComponent("Collider", { size: { x: 1.8, y: 1.8 }, trigger: true, movable: false });
        this.addComponent("Light", { type: "point", color: vec3(0.6, 0.9, 1), intensity: 0.6, offset: vec3(0, 0, 3) });
        this.t = Math.random() * 6;
    }

    BeginPlay() {
        this.base = this.position.y;
    }

    Update(dt) {
        this.t += dt;
        this.position.y = this.base + Math.sin(this.t * 2.5) * 0.2;
    }

    OnBeginOverlap(other) {
        if (!other.hasTag("player") || this.taken)
            return;
        this.taken = true;
        other.send("Collector", "OnCollected", "shard", 1);
        Sfx.play("chest");
        Game.burst("sprites/soul.png", 2, this.position, 12, 10, { size: 0.8 });
        this.destroy();
    }
}

class Lantern extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 1.4, y: 2.4 } });
        this.anim.setAnimation("sprites/lantern.png", 2, 0.4 + Math.random() * 0.3, true);
        this.addComponent("Light", { type: "point", color: vec3(1, 0.75, 0.45), intensity: 1.1, offset: vec3(0, -0.5, 4) });
    }
}

// The arena of the Warden : the player enters -> the way in is sealed
// (voxels), the boss wakes up. The player dies -> everything is reset.
class BossArena extends ColliderActor {
    static interfaces = ["GameEvents"];
    static properties = { boss: "warden" };

    constructor() {
        super();
        this.trigger = true;
        this.movable = false;
        this.show_in_game = false;
        this.state = "idle";   // idle, fight, done
    }

    // The seal over the shaft (local map coordinates, see world.py).
    SealCenter() { return vec3(Game.X0 + 233, Game.Y0 + 47, 0); }

    OnBeginOverlap(other) {
        if (this.state !== "idle" || !other.hasTag("player"))
            return;
        // Only once the player is down in the arena.
        this.state = "arming";
        this.player = other;
    }

    Update(dt) {
        if (this.state === "arming" && this.player && this.player.valid) {
            if (this.player.IsGrounded() && this.player.position.y < Game.Y0 + 30)
                this.StartFight();
        }
    }

    StartFight() {
        this.state = "fight";
        Voxels.fillRect(this.SealCenter(), { x: 16, y: 6 }, "Seal", { replace: true });
        Sfx.play("door");
        Game.shake();
        const boss = Level.find(this.boss);
        if (boss)
            boss.Wake(this.player);
    }

    OnBossDefeated(boss) {
        this.state = "done";
        // The seal opens, and the way to the heart of the hollow.
        Sfx.music("ambience_cave", 0.5);
        Sfx.play("door");
        Voxels.destroyRect(this.SealCenter(), { x: 16, y: 6 }, { types: ["Seal"], includeIndestructible: true });
        Voxels.destroyRect(vec3(Game.X0 + 325, Game.Y0 + 20, 0), { x: 10, y: 20 }, { types: ["Seal"], includeIndestructible: true });
        PostProcess.reset();
    }

    OnPlayerDied(player) {
        if (this.state !== "fight" && this.state !== "arming")
            return;
        this.state = "idle";
        Voxels.destroyRect(this.SealCenter(), { x: 16, y: 6 }, { types: ["Seal"], includeIndestructible: true });
        const boss = Level.find(this.boss);
        if (boss)
            boss.ResetFight();
        PostProcess.reset();
    }

    OnPlayerRespawned(player) {}
}

// The relic at the end : the game is won.
class Relic extends Actor {
    constructor() {
        super();
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3, y: 3 } });
        this.anim.setAnimation("sprites/relic.png", 4, 0.12, true);
        this.addComponent("Collider", { size: { x: 3, y: 3 }, trigger: true, movable: false });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.9, 0.6), intensity: 2.0, offset: vec3(0, 0, 5) });
    }

    OnBeginOverlap(other) {
        if (other.hasTag("player") && other.Win)
            other.Win();
    }
}

// Pause menu (ui/PauseMenu.widget, Class = PauseMenu in the Widget Editor).
class PauseMenu extends UserWidget {
    Construct() {
        this.find("Controls").text =
            "A / D move   SPACE jump   J attack (W / S : up / down)   K dash   L parry   F focus (heal)\n" +
            "B bomb   E dirt block (S + E : step up)   R hook   W rest on a bench   ESC resume";
        this.find("Resume").onClicked(() => {
            const p = Game.player();
            if (p) p.TogglePause();
        });
        this.find("Bench").onClicked(() => {
            const p = Game.player();
            if (p) p.BackToBench();
        });
        this.find("TitleScreen").onClicked(() => {
            const p = Game.player();
            if (p) {
                p.TogglePause();
                p.ShowTitle();
            }
        });
    }
}
