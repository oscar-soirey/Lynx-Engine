// The adventurer, seen from above. A plain Actor : a blocking Collider and a
// velocity (VelocityComponent) : it slides along the walls by itself.
//
//   WASD / arrows  move     Shift  run     E  talk / open / read
//   Space          next line of a dialogue (default dialogue box of the plugin)

class Adventurer extends Actor {
    static interfaces = ["Notifiable", "DialogueEvents", "StoryEvents", "SequenceEvents"];

    static properties = {
        speed: 14.0,
        run_speed: 22.0,
        interact_radius: 4.0,
    };

    constructor() {
        super();
        this.autoPossessPlayer = 0;
        this.addTag("player");
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.2, y: 3.2 }, offset: vec3(0, 0.8, 0) });
        this.anim.setAnimation("sprites/hero_top.png", 4, 0.12, true);
        this.addComponent("Collider", { size: { x: 1.6, y: 1.4 } });
    }

    BeginPlay() {
        this.busy = true;              // until the intro sequence ends
        Sfx.listener = this;
        Sfx.music("ambience_forest", 0.5);
        this.stepTime = 0;
        this.target = null;
        this.addComponent("Camera", { offset: vec3(0, 0, 70), fov: 32, followSpeed: 5 });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.8, 0.55), intensity: 0.9, offset: vec3(0, 0, 6) });

        this.hud = UI.create("ui/Hud.widget");
        this.hud.addToViewport(10);
        this.RefreshQuest();
    }

    ProcessInput(player) {
        const talking = Dialogue.isActive();
        let x = Input.axis("move_x"), y = Input.axis("move_y");
        const length = Math.hypot(x, y);
        if (length > 1) { x /= length; y /= length; }
        if (this.busy || talking) { x = 0; y = 0; }

        const speed = Input.held("sprint") ? this.run_speed : this.speed;
        this.velocity = vec3(x * speed, y * speed, 0);    // stopped by the walls (Collider)

        this.anim.frameTime = speed > this.speed ? 0.08 : 0.12;
        if (length > 0.1 && !this.busy && !talking) {
            this.anim.play();
            if (Math.abs(x) > 0.1)
                this.anim.flipX = x < 0;
        } else {
            this.anim.pause();
        }

        if (!this.busy && !talking && Input.pressed("interact") && this.target)
            { Sfx.play("blip", null, { volume: 0.4 }); this.target.send("Interactable", "Interact", this); }
    }

    Update(dt) {
        // Footsteps.
        const moving = Math.hypot(this.velocity.x, this.velocity.y) > 1;
        if (moving && (this.stepTime -= dt) <= 0) {
            this.stepTime = Math.hypot(this.velocity.x, this.velocity.y) > this.speed + 1 ? 0.24 : 0.34;
            Sfx.play("step", null, { volume: 0.25, pitchVariation: 0.15 });
        }

        // Closest thing to interact with (NPCs, chest, signs...).
        this.target = null;
        if (!this.busy && !Dialogue.isActive()) {
            const around = Physics.overlapCircle(this.position, this.interact_radius,
                                                 { ignore: this, triggers: true, voxels: false });
            let best = 1e9;
            for (const actor of around.actors) {
                if (!actor.implements("Interactable") || actor.send("Interactable", "CanInteract", this) === false)
                    continue;
                const d = Math.hypot(actor.position.x - this.position.x, actor.position.y - this.position.y);
                if (d < best) { best = d; this.target = actor; }
            }
        }
        const prompt = this.hud.find("Prompt");
        prompt.visibility = this.target ? "Visible" : "Hidden";
        if (this.target)
            prompt.text = "E : " + (this.target.verb || "parler");

        if (this.messageTime > 0 && (this.messageTime -= dt) <= 0)
            this.hud.find("Message").visibility = "Hidden";
    }

    // ---- Interfaces ---------------------------------------------------------

    Notify(text) {
        const message = this.hud.find("Message");
        message.text = text;
        message.visibility = "Visible";
        this.messageTime = 3;
    }

    OnSequenceFinished(sequence) {
        this.busy = false;
        Dialogue.start("dialogues/welcome.dialogue", null, this);
    }

    OnDialogueEvent(event, dialogue) {}
    OnDialogueStarted(dialogue) {}
    OnDialogueEnded(dialogue) {}

    OnQuestChanged(quest, state) {
        this.RefreshQuest();
        if (state === "active")
            { Sfx.play("powerup", null, { volume: 0.6 }); this.Notify("Nouvelle quête : " + (Quest.get(quest) || {}).title); }
        else if (state === "completed")
            { Sfx.play("win"); this.Notify("Quête terminée !"); }
    }

    // ---- HUD : the active quest and its next objective ----------------------

    RefreshQuest() {
        if (!this.hud || !this.hud.valid)
            return;
        const active = Quest.list().filter(q => q.state === "active");
        const quest = active[active.length - 1];
        this.hud.find("QuestTitle").text = quest ? quest.title : "";
        const next = quest ? quest.objectives.find(o => !o.done) : null;
        this.hud.find("QuestObjective").text = next ? "- " + next.text : "";
    }
}
