// The miner : a Humanoid (walk, jump, swim, slide on the voxel surfaces) with
// four tools. Everything it does to the world goes through the engine APIs :
//   Voxels.destroyCircle / fillCircle / destroyLine   (what was destroyed : r.types)
//   Physics.raycast / overlapCircle                    (what is in front / around)
//
//   A / D  move      Space / W  jump      arrows / right stick  aim
//   J, Ctrl, left click  use the tool     1-4, Q / E  change tool
//   F  material to build                  R  back to the start

class Miner extends Humanoid {
    // Static fields rather than file constants : the class files are evaluated
    // again before each level.
    static TOOLS = ["Pioche", "Construire", "Bombe", "Laser"];
    static BUILDABLE = ["Dirt", "Stone", "Sand", "Wood", "Brick", "Water", "Oil"];
    static NAMES = { Dirt: "Terre", Stone: "Pierre", Sand: "Sable", Gravel: "Gravier", Water: "Eau", Oil: "Huile",
                     Lava: "Lave", Acid: "Acide", Gold: "Or", Crystal: "Cristal", Wood: "Bois", Brick: "Brique" };

    static interfaces = ["Damageable"];

    static properties = {
        max_hp: 100.0,
        reach: 6.0,              // voxels, pickaxe and builder
        dig_radius: 1.8,
        gold_goal: { value: 60, type: "int" },
    };

    constructor() {
        super();
        this.autoPossessPlayer = 0;
        this.addTag("player");
        this.collider_size = { x: 1.8, y: 2.9 };
        this.move_speed = 13;
        this.jump_speed = 26;
        this.gravity = 70;
        this.max_jump_count = 1;
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.4, y: 3.4 }, offset: vec3(0, 0.15, 0) });
        this.anim.setAnimation("sprites/hero_idle.png", 2, 0.45, true);
    }

    BeginPlay() {
        this.hp = this.max_hp;
        this.start = this.position.clone();
        this.tool = 0;
        this.material = 0;
        this.inventory = { Dirt: 20, Wood: 20, Brick: 10 };
        this.cooldown = 0;
        this.aim = { x: 1, y: 0 };
        this.state = "";
        this.won = false;

        this.addComponent("Camera", { offset: vec3(0, 2, 60), fov: 38, followSpeed: 5, followDelay: 0.03 });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.8, 0.55), intensity: 1.1, offset: vec3(0, 1, 6) });

        // Aim marker : another actor (one component of each kind per actor).
        this.cursor = Level.spawn("AimCursor", this.position);

        // The voxels are only simulated around the cameras : a little more here.
        VoxelPhysics.setRadius(110);

        this.hud = UI.create("ui/Hud.widget");
        this.hud.addToViewport(10);
        this.RefreshHud();
    }

    // ---- Input --------------------------------------------------------------

    ProcessInput(player) {
        this.Move(Input.axis("move_x"));
        if (Input.pressed("jump")) this.Jump();
        if (Input.released("jump")) this.StopJumping();

        const ax = Input.axis("aim_x"), ay = Input.axis("aim_y");
        const len = Math.hypot(ax, ay);
        if (len > 0.25)
            this.aim = { x: ax / len, y: ay / len };
        else
            this.aim = { x: this.IsFacingRight() ? 1 : -1, y: this.aim.y * 0.9 };

        for (let i = 0; i < 4; ++i)
            if (Input.pressed("tool_" + (i + 1))) this.SetTool(i);
        if (Input.pressed("tool_next")) this.SetTool((this.tool + 1) % Miner.TOOLS.length);
        if (Input.pressed("tool_prev")) this.SetTool((this.tool + Miner.TOOLS.length - 1) % Miner.TOOLS.length);
        if (Input.pressed("material_next")) {
            this.material = (this.material + 1) % Miner.BUILDABLE.length;
            this.RefreshHud();
        }
        if (Input.pressed("restart")) this.Respawn();

        if (Input.held("use") && this.cooldown <= 0)
            this.UseTool();
    }

    SetTool(i) {
        this.tool = i;
        this.RefreshHud();
    }

    Eye() {
        return vec3(this.position.x, this.position.y + 0.6, 0);
    }

    // ---- Tools --------------------------------------------------------------

    UseTool() {
        const eye = this.Eye();
        const far = (d) => vec3(eye.x + this.aim.x * d, eye.y + this.aim.y * d, 0);

        switch (this.tool) {
        case 0: { // pickaxe : the first voxel in front, then a small circle there
            this.cooldown = 0.12;
            const hit = Physics.raycast(eye, far(this.reach), { ignore: this, actors: false, voxelFlags: 0xFFFFFFFF });
            if (!hit || !hit.voxel)
                return;
            const center = vec3(hit.point.x + this.aim.x * 0.8, hit.point.y + this.aim.y * 0.8, 0);
            const r = Voxels.destroyCircle(center, this.dig_radius, { ignoreTypes: ["Lava", "Acid"] });
            this.Collect(r);
            break;
        }
        case 1: { // builder : the selected material, if there is some left
            this.cooldown = 0.08;
            const name = Miner.BUILDABLE[this.material];
            if ((this.inventory[name] || 0) <= 0)
                return;
            const hit = Physics.raycast(eye, far(this.reach), { ignore: this, actors: false });
            const at = hit ? vec3(hit.point.x - this.aim.x * 0.7, hit.point.y - this.aim.y * 0.7, 0) : far(this.reach * 0.6);
            if (Math.hypot(at.x - this.position.x, at.y - this.position.y) < 2.2)
                return;                                   // not inside the miner
            const r = Voxels.fillCircle(at, 1.3, name);
            this.inventory[name] = Math.max(0, (this.inventory[name] || 0) - r.count);
            this.RefreshHud();
            break;
        }
        case 2: { // bomb : an actor with its own little physics
            this.cooldown = 0.6;
            const bomb = Level.spawn("Bomb", vec3(eye.x + this.aim.x * 1.5, eye.y + this.aim.y * 1.5, 0));
            const v = this.GetVelocity();
            bomb.Throw(this.aim.x * 26 + v.x * 0.5, this.aim.y * 26 + 8, this);
            break;
        }
        case 3: { // laser : a ray that cuts a thin line through everything but bedrock
            this.cooldown = 0.05;
            const hit = Physics.raycast(eye, far(40), { ignore: this, voxelFlags: 0xFFFFFFFF, debug: true });
            if (!hit)
                return;
            const r = Voxels.destroyLine(eye, hit.point, 1.4, { ignoreTypes: ["Water", "Oil"] });
            this.Collect(r, 0.5);
            if (hit.actor && hit.actor.implements("Damageable"))
                hit.actor.send("Damageable", "TakeDamage", 4, this);
            Level.spawn("Spark", vec3(hit.point.x, hit.point.y, 0));
            break;
        }
        }
    }

    // Adds what was destroyed to the inventory (r : result of Voxels.destroy*).
    Collect(r, ratio = 1) {
        if (!r || r.count === 0)
            return;
        for (const entry of r.perType) {
            const name = entry.typeName;
            if (name === "Bedrock" || name === "Smoke")
                continue;
            this.inventory[name] = (this.inventory[name] || 0) + Math.max(1, Math.round(entry.count * ratio));
        }
        // A few flying pieces.
        for (let i = 0; i < r.cells.length && i < 12; i += 4)
            Level.spawn("Debris", vec3(r.cells[i].world.x, r.cells[i].world.y, 0));
        this.RefreshHud();

        if (!this.won && (this.inventory.Gold || 0) >= this.gold_goal) {
            this.won = true;
            this.ShowMessage("Riche ! " + this.inventory.Gold + " pépites d'or", 4);
        }
    }

    // ---- Every frame --------------------------------------------------------

    Update(dt) {
        this.cooldown -= dt;
        if (this.cursor && this.cursor.valid) {
            const eye = this.Eye();
            const d = this.tool === 3 ? 8 : this.reach * 0.75;
            this.cursor.position.set(eye.x + this.aim.x * d, eye.y + this.aim.y * d, 1);
        }
        if (this.messageTime > 0 && (this.messageTime -= dt) <= 0)
            this.hud.find("Message").visibility = "Hidden";
        this.UpdateAnimation();
    }

    UpdateAnimation() {
        let state;
        if (!this.IsGrounded())
            state = this.GetVelocity().y > 0 ? "jump" : "fall";
        else
            state = Math.abs(this.GetVelocity().x) > 1 ? "run" : "idle";
        if (state === this.state)
            return;
        this.state = state;
        const a = {
            idle: ["sprites/hero_idle.png", 2, 0.45], run: ["sprites/hero_run.png", 4, 0.09],
            jump: ["sprites/hero_jump.png", 1, 1], fall: ["sprites/hero_fall.png", 1, 1],
        }[state];
        this.anim.setAnimation(a[0], a[1], a[2], true);
    }

    // Damageable : lava and acid (voxel physics, instigator null), bombs.
    TakeDamage(amount, instigator) {
        this.hp -= amount;
        if (this.hp <= 0) {
            this.ShowMessage("Perdu ! Retour au départ", 2.5);
            this.Respawn();
        }
        this.RefreshHud();
    }

    Respawn() {
        this.position.set(this.start.x, this.start.y, 0);
        this.StopMovement();
        this.hp = this.max_hp;
        this.RefreshHud();
    }

    // ---- HUD (ui/Hud.widget) ------------------------------------------------

    RefreshHud() {
        if (!this.hud || !this.hud.valid)
            return;
        const mat = Miner.BUILDABLE[this.material];
        this.hud.find("Tool").text = "[" + (this.tool + 1) + "] " + Miner.TOOLS[this.tool] +
            (this.tool === 1 ? " : " + Miner.NAMES[mat] + " (" + (this.inventory[mat] || 0) + ")" : "");
        const parts = [];
        for (const name of Object.keys(Miner.NAMES))
            if (this.inventory[name] > 0)
                parts.push(Miner.NAMES[name] + " " + this.inventory[name]);
        this.hud.find("Inventory").text = parts.join("   ");
        this.hud.find("Health").percent = Math.max(0, this.hp / this.max_hp);
    }

    ShowMessage(text, seconds) {
        if (!this.hud || !this.hud.valid)
            return;
        const message = this.hud.find("Message");
        message.text = text;
        message.visibility = "Visible";
        this.messageTime = seconds;
    }
}
