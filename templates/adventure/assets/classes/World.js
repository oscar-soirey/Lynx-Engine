// Characters and objects of the world.

// Villagers : DialogueNPC (plugin Dialogue) already implements "Interactable"
// (Interact starts its dialogue). The JS classes only add a sprite and a body.
class Elder extends DialogueNPC {
    constructor() {
        super();
        this.dialogue = "dialogues/elder.dialogue";
        this.speaker_name = "Ancien";
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.2, y: 3.2 }, offset: vec3(0, 0.8, 0) });
        this.anim.setAnimation("sprites/npc_elder.png", 2, 0.6, true);
        this.addComponent("Collider", { size: { x: 1.8, y: 1.6 }, movable: false });
    }
}

class Smith extends DialogueNPC {
    constructor() {
        super();
        this.dialogue = "dialogues/smith.dialogue";
        this.speaker_name = "Forgeronne";
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.2, y: 3.2 }, offset: vec3(0, 0.8, 0) });
        this.anim.setAnimation("sprites/npc_smith.png", 2, 0.5, true);
        this.addComponent("Collider", { size: { x: 1.8, y: 1.6 }, movable: false });
    }
}

// The chest of the forest : gives the key (a Story variable + a quest objective).
class Chest extends Actor {
    static interfaces = ["Interactable"];

    constructor() {
        super();
        this.verb = "ouvrir";
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/chest_closed.png", size: { x: 3, y: 3 } });
        this.addComponent("Collider", { size: { x: 2.6, y: 2 }, movable: false });
    }

    BeginPlay() {
        this.opened = false;
    }

    CanInteract(instigator) {
        return !this.opened;
    }

    Interact(instigator) {
        this.opened = true;
        this.sprite.texture = "sprites/chest_open.png";
        Sfx.play("chest");
        Sfx.play("pickup", null, { volume: 0.6 });
        Story.set("has_key", true);
        if (Quest.state("lost_key") === "active")
            Quest.objective("lost_key", "find_key");
        instigator.send("Notifiable", "Notify", "Tu trouves une vieille clé rouillée !");
        Level.spawn("KeyFlash", vec3(this.position.x, this.position.y + 2, 1));
    }
}

class KeyFlash extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/key.png", size: { x: 2.5, y: 2.5 } });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.85, 0.4), intensity: 2, offset: vec3(0, 0, 3) });
    }

    BeginPlay() {
        this.lifetime = 1.5;
    }

    Update(dt) {
        this.position.y += dt * 2;
    }
}

class Signpost extends Actor {
    static interfaces = ["Interactable"];
    static properties = { text: "..." };

    constructor() {
        super();
        this.verb = "lire";
        this.addComponent("StaticSprite", { texture: "sprites/sign.png", size: { x: 3, y: 3 }, offset: vec3(0, 0.8, 0) });
        this.addComponent("Collider", { size: { x: 1.4, y: 1 }, movable: false });
    }

    Interact(instigator) {
        instigator.send("Notifiable", "Notify", this.text);
    }
}

// The gate of the ruins : made of "Gate" voxels. When the elder's dialogue
// sends the event "OpenGate", the camera cuts to it (CineCamera plugin) and it
// crumbles row by row (Voxels.destroyRect), then the camera comes back.
class Gate extends Actor {
    static interfaces = ["DialogueEvents"];
    static properties = { width: 10.0, height: 3.0 };

    OnDialogueEvent(event, dialogue) {
        if (event !== "OpenGate" || this.opening)
            return;
        this.opening = true;
        this.time = 0;
        this.row = 0;
        Cine.cutTo("cam_ruins", 1.2);
        Sfx.play("door");
    }

    Update(dt) {
        if (!this.opening)
            return;
        this.time += dt;
        // One column of voxels every 0.15 s, after the camera blend.
        const columns = Math.ceil(this.width);
        while (this.time > 1.3 + this.row * 0.15 && this.row < columns) {
            const x = this.position.x - this.width / 2 + this.row + 0.5;
            const r = Voxels.destroyRect(vec3(x, this.position.y, 0), { x: 1, y: this.height }, { types: ["Gate"] });
            for (const cell of r.cells)
                Level.spawn("Dust", vec3(cell.world.x, cell.world.y, 1));
            Sfx.play("rock_fall", null, { volume: 0.5 });
            this.row++;
        }
        if (this.time > 1.3 + columns * 0.15 + 1.2) {
            this.opening = false;
            Cine.release();
        }
    }
}

class Dust extends Actor {
    constructor() {
        super();
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/explosion.png", size: { x: 2, y: 2 },
                                                          region: { x: 0.68, y: 0, z: 0.83, w: 1 } });
    }

    BeginPlay() {
        this.vx = (Math.random() - 0.5) * 6;
        this.vy = (Math.random() - 0.5) * 6;
        this.lifetime = 0.8;
    }

    Update(dt) {
        this.position.x += this.vx * dt;
        this.position.y += this.vy * dt;
    }
}

// The relic at the end : completes the last quest, lights up the ruins.
class RuinsRelic extends Actor {
    static interfaces = ["Interactable"];

    constructor() {
        super();
        this.verb = "toucher";
        this.sprite = this.addComponent("StaticSprite", { texture: "sprites/gem.png", size: { x: 2.6, y: 2.6 } });
        this.light = this.addComponent("Light", { type: "point", color: vec3(0.4, 0.8, 1), intensity: 1.5, offset: vec3(0, 0, 4) });
        this.addComponent("Collider", { size: { x: 2, y: 2 }, movable: false });
    }

    BeginPlay() {
        this.time = 0;
        this.done = false;
    }

    CanInteract(instigator) {
        return !this.done && Quest.state("relic") === "active";
    }

    Interact(instigator) {
        this.done = true;
        Quest.objective("relic", "touch_relic");
        Quest.complete("relic");
        Sfx.play("powerup");
        // Crystals grow in the four corners of the ruins (they light the room).
        for (const [dx, dy] of [[-12, -9], [12, -9], [-12, 9], [12, 9]])
            Voxels.fillCircle(vec3(this.position.x + dx, this.position.y + dy, 0), 1.8, "Crystal", { replace: true, types: ["Tiles"] });
        instigator.send("Notifiable", "Notify", "La relique s'illumine... Fin de l'aventure !");
    }

    Update(dt) {
        this.time += dt;
        this.light.intensity = (this.done ? 4 : 1.5) + Math.sin(this.time * 3) * 0.5;
    }
}

// Night ambience : a small light that wanders around its start position.
class Firefly extends Actor {
    constructor() {
        super();
        this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 0.8, y: 0.8 } });
        this.addComponent("Light", { type: "point", color: vec3(0.7, 1, 0.4), intensity: 1.2, offset: vec3(0, 0, 3) });
    }

    BeginPlay() {
        this.home = this.position.clone();
        this.time = Math.random() * 10;
    }

    Update(dt) {
        this.time += dt;
        this.position.x = this.home.x + Math.sin(this.time * 0.7) * 6 + Math.sin(this.time * 2.3) * 1.5;
        this.position.y = this.home.y + Math.cos(this.time * 0.5) * 5;
    }
}
