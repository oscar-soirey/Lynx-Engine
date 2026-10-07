// Checkpoint and goal : zones (ColliderActor with trigger = true) placed in the
// level. They only talk to the interfaces of the actor that enters them.

class Checkpoint extends ColliderActor {
    constructor() {
        super();
        this.trigger = true;
        this.movable = false;
        this.size = { x: 3, y: 5 };
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.4, y: 3.4 }, offset: vec3(0, -0.6, 0) });
        this.anim.setAnimation("sprites/checkpoint_off.png", 1, 1, false);
    }

    BeginPlay() {
        this.active = false;
    }

    OnBeginOverlap(other) {
        if (this.active || !other.implements("Respawnable"))
            return;
        this.active = true;
        other.send("Respawnable", "SetCheckpoint", this.position);
        this.anim.setAnimation("sprites/checkpoint_on.png", 2, 0.2, true);
        Level.spawn("Sparkle", this.position);
    }
}

class Goal extends ColliderActor {
    constructor() {
        super();
        this.trigger = true;
        this.movable = false;
        this.size = { x: 3, y: 6 };
        this.anim = this.addComponent("AnimationSprite", { size: { x: 4.5, y: 4.5 }, offset: vec3(0, -0.6, 0) });
        this.anim.setAnimation("sprites/flag.png", 2, 0.25, true);
        this.addComponent("Light", { type: "point", color: vec3(1, 0.4, 0.4), intensity: 1.2, offset: vec3(0, 1, 4) });
    }

    OnBeginOverlap(other) {
        if (other.implements("Finisher"))
            other.send("Finisher", "OnFinish", this);
    }
}
