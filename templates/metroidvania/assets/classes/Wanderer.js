// The Wanderer : the player. A Humanoid (engine : walk, gravity, collisions)
// whose jumps, dash, walls, attacks and abilities are written here.
//
//   A / D           move                  SPACE (hold)   jump, higher when held
//   J               attack (W / S : up / down ; down in the air : pogo)
//   K / SHIFT       dash                  L              parry (stuns, sends projectiles back)
//   F (hold)        focus : heal with SOUL (filled by hitting enemies)
//   B               bomb (breaks rock)    E              dirt block (dirt comes from digging)
//   R               grappling hook        W              rest on a bench
//   ESC             pause
//
// Feel : coyote time, jump buffer, variable jump height, faster fall,
// hit-stop and camera shake on hits, invulnerability frames.
// Animation : anim/Wanderer.animgraph (parameters set at the end of Update).

class Wanderer extends Humanoid {
    static interfaces = ["Damageable", "Collector", "VoxelEvents"];

    static properties = {
        max_hp: { value: 5, type: "int" },
        run_speed: 15.0,
        jump_velocity: 33.0,
        double_jump_velocity: 28.0,
        dash_speed: 44.0,
        dash_time: 0.17,
        wall_slide_speed: 7.0,
        // Abilities at the start (true : already unlocked, to test the level).
        has_dash: false,
        has_claw: false,
        has_wings: false,
        has_hook: false,
        has_bombs: false,
        skip_title: false,
    };

    // Named areas (local map coordinates, see world.level / Game.X0, Game.Y0).
    static AREAS = [
        ["Start Cave", 8, 150, 96, 186],
        ["Mossy Crossroads", 96, 140, 250, 197],
        ["Dash Ruins", 250, 136, 300, 172],
        ["Spike Gallery", 300, 128, 342, 156],
        ["Spire Summit", 280, 186, 406, 205],
        ["The Spire", 342, 60, 406, 192],
        ["Warden's Den", 150, 10, 320, 50],
        ["Heart of the Hollow", 330, 10, 362, 30],
        ["Lower Halls", 100, 50, 343, 118],
        ["Western Hollow", 12, 60, 100, 112],
    ];

    constructor() {
        super();
        this.autoPossessPlayer = 0;
        this.addTag("player");

        // Humanoid settings (voxels, seconds) : snappy, Hollow Knight like.
        this.collider_size = { x: 1.5, y: 2.6 };
        this.move_speed = 15;
        this.ground_acceleration = 260;
        this.ground_deceleration = 320;
        this.direction_change_acceleration = 420;
        this.air_acceleration = 190;
        this.air_deceleration = 120;
        this.air_control = 1;
        this.gravity = 85;
        this.max_fall_speed = 42;
        this.max_jump_count = 0;          // the jumps are done here (Launch)
        this.max_step_height = 0.6;
        this.collision_layer = 1;
        this.collision_mask = 1;          // triggers (layer 1) only : enemies (layer 2) go through

        this.inX = 0;
        this.inY = 0;
        this.anim = this.addComponent("AnimationSprite", { size: { x: 4.2, y: 4.2 }, offset: vec3(0, 0.25, 0) });
        this.anim.loadGraph("anim/Wanderer.animgraph");
    }

    BeginPlay() {
        this.hp = this.max_hp;
        this.maxHp = this.max_hp;
        this.soul = 0;
        Sfx.listener = this;
        Sfx.music("ambience_cave", 0.5);
        this.geo = 0;
        this.dirt = 0;
        this.shards = 0;
        this.deaths = 0;
        this.playTime = 0;
        this.abilities = { dash: this.has_dash, claw: this.has_claw, wings: this.has_wings, hook: this.has_hook, bombs: this.has_bombs };

        // State
        this.state = "normal";     // normal, dash, hurt, focus, dead, sit, hook, title, won
        this.stateTime = 0;
        this.invulnerable = 0;
        this.coyote = 0;
        this.jumpBuffer = 0;
        this.airJumps = 0;
        this.dashReady = true;
        this.dashCooldown = 0;
        this.dashDir = 1;
        this.attackCooldown = 0;
        this.comboStep = 0;
        this.comboTimer = 0;
        this.parryTime = 0;
        this.parryCooldown = 0;
        this.focusTime = 0;
        this.bombCooldown = 0;
        this.hookCooldown = 0;
        this.wallDir = 0;
        this.wallJumpLock = 0;
        this.lookDown = 0;
        this.camLook = 0;
        this.safeTimer = 0;
        this.lastSafe = this.position.clone();
        this.bench = { position: this.position.clone(), title: "" };
        this.nearBench = null;
        this.area = "";
        this.messageTime = 0;
        this.fade = 0;
        this.fadeTarget = 0;
        this.paused = false;
        this.pauseMenu = null;
        this.chain = [];

        this.cam = this.addComponent("Camera", { offset: vec3(0, 2.5, 62), fov: 34, followSpeed: 7, useCameraShake: true });
        this.light = this.addComponent("Light", { type: "point", color: vec3(0.75, 0.9, 1), intensity: 0.75, offset: vec3(0, 1, 7) });

        this.hud = UI.create("ui/Hud.widget");
        this.hud.addToViewport(10);
        this.hudCache = {};
        this.RefreshHud(true);

        if (this.skip_title) {
            this.SetState("normal");
            this.ShowArea();
        } else {
            this.ShowTitle();
        }
    }

    EndPlay() {
        this.ClearChain();
        if (this.title && this.title.valid) this.title.destroy();
        if (this.pauseMenu && this.pauseMenu.valid) this.pauseMenu.destroy();
        PostProcess.reset();
    }

    SetState(s) {
        if (this.state === "hook" && s !== "hook")
            this.ClearChain();
        this.state = s;
        this.stateTime = 0;
        this.movement_enabled = s !== "dead" && s !== "sit" && s !== "title" && s !== "won";
    }

    Facing() {
        return this.IsFacingRight() ? 1 : -1;
    }

    // =========================================================================
    // Input
    // =========================================================================

    ProcessInput(player) {
        const x = Input.axis("move_x");
        const y = Input.axis("aim_y");
        this.inX = Math.abs(x) > 0.25 ? Math.sign(x) : 0;
        this.inY = Math.abs(y) > 0.5 ? Math.sign(y) : 0;

        if (Input.pressed("pause") && this.state !== "title" && this.state !== "won")
            this.TogglePause();
        if (this.paused) {
            this.Move(0);
            return;
        }

        if (this.state === "title") {
            this.Move(0);
            if (Input.pressed("jump") || Input.pressed("attack"))
                this.CloseTitle();
            return;
        }
        if (this.state === "dead" || this.state === "won") {
            this.Move(0);
            return;
        }
        if (this.state === "sit") {
            this.Move(0);
            if (this.inX !== 0 || Input.pressed("jump") || this.inY < 0)
                this.StandUp();
            return;
        }

        if (Input.pressed("jump")) this.jumpBuffer = 0.13;
        if (Input.released("jump")) this.CutJump();

        // ---- Movement --------------------------------------------------------
        const locked = this.state === "dash" || this.state === "hurt" || this.state === "focus" || this.state === "hook";
        let move = locked ? 0 : this.inX;
        if (this.wallJumpLock > 0 && move === -this.wallJumpDir)
            move = 0;   // just jumped off a wall : no instant return
        if (!locked)
            this.Move(move);
        else if (this.state === "focus" || this.state === "hurt")
            this.Move(0);

        if (this.state === "normal") {
            this.TryJump();
            if (Input.pressed("dash")) this.TryDash();
            if (Input.pressed("attack")) this.Attack();
            if (Input.pressed("parry")) this.Parry();
            if (Input.held("focus")) this.TryFocus();
            if (Input.pressed("bomb")) this.ThrowBomb();
            if (Input.pressed("block")) this.PlaceBlock();
            if (Input.pressed("hook")) this.FireHook();
            if (Input.pressed("interact") && this.nearBench && this.IsGrounded()) this.SitDown();
        } else if (this.state === "focus") {
            if (!Input.held("focus"))
                this.SetState("normal");
        } else if (this.state === "hook") {
            if (Input.pressed("jump")) {   // cancel the pull with a jump
                this.SetState("normal");
                this.Launch(0, this.jump_velocity * 0.8, false, true);
            }
        }
    }

    // ---- Jumps -------------------------------------------------------------------

    TryJump() {
        if (this.jumpBuffer <= 0)
            return;
        if (this.IsGrounded() || this.coyote > 0) {
            this.DoJump(this.jump_velocity);
            this.coyote = 0;
        } else if (this.abilities.claw && this.wallDir !== 0) {
            // Wall jump : away from the wall, the direction is locked a moment.
            this.wallJumpDir = -this.wallDir;
            this.wallJumpLock = 0.16;
            Sfx.play("jump", null, { volume: 0.55 });
            this.Launch(this.wallJumpDir * 20, this.jump_velocity * 0.92, true, true);
            this.Move(this.wallJumpDir);
            this.jumpBuffer = 0;
            this.jumpHeld = true;
            this.dashReady = true;
            Game.dust(vec3(this.position.x + this.wallDir * 0.8, this.position.y, 1), 3);
        } else if (this.abilities.wings && this.airJumps < 1) {
            this.airJumps++;
            this.DoJump(this.double_jump_velocity);
            Game.burst("sprites/soul.png", 2, vec3(this.position.x, this.position.y - 1, 1), 6, 8, { up: -4, size: 0.7, life: 0.35 });
        }
    }

    DoJump(v) {
        this.Launch(0, v, false, true);
        Sfx.play(v === this.jump_velocity ? "jump" : "double_jump", null, { volume: 0.55 });
        this.jumpBuffer = 0;
        this.jumpHeld = true;
        Game.dust(vec3(this.position.x, this.position.y - 1.3, 1), 3);
    }

    // Released early : a short jump.
    CutJump() {
        const v = this.GetVelocity();
        if (this.jumpHeld && v.y > 0)
            this.SetVelocity(v.x, v.y * 0.45);
        this.jumpHeld = false;
    }

    // ---- Dash ----------------------------------------------------------------------

    TryDash() {
        if (!this.abilities.dash || !this.dashReady || this.dashCooldown > 0)
            return;
        this.dashDir = this.inX !== 0 ? this.inX : this.Facing();
        if (this.wallDir !== 0 && !this.IsGrounded())
            this.dashDir = -this.wallDir;   // dash off a wall
        this.Move(this.dashDir);
        this.SetState("dash");
        this.dashCooldown = 0.38;
        Sfx.play("dash", null, { volume: 0.6 });
        if (!this.IsGrounded())
            this.dashReady = false;
        Game.dust(vec3(this.position.x - this.dashDir, this.position.y - 0.8, 1), 4);
    }

    // ---- Attacks ---------------------------------------------------------------------

    Attack() {
        if (this.attackCooldown > 0)
            return;
        let dir = "side";
        if (this.inY > 0) dir = "up";
        else if (this.inY < 0 && !this.IsGrounded()) dir = "down";

        let damage = 1, size, center, big = false;
        const f = this.Facing();
        const p = this.position;
        if (dir === "up") {
            center = vec3(p.x, p.y + 3.0, 0); size = { x: 3.4, y: 3.8 };
            this.anim.setTrigger("attack_up");
        } else if (dir === "down") {
            center = vec3(p.x, p.y - 3.0, 0); size = { x: 3.4, y: 3.8 };
            this.anim.setTrigger("attack_down");
        } else {
            this.comboStep = this.comboTimer > 0 ? (this.comboStep % 3) + 1 : 1;
            big = this.comboStep === 3;
            damage = big ? 2 : 1;
            center = vec3(p.x + f * (big ? 3.3 : 2.7), p.y + 0.3, 0);
            size = big ? { x: 4.8, y: 3.2 } : { x: 3.8, y: 2.8 };
            this.anim.setTrigger("attack" + this.comboStep);
            this.comboTimer = 0.5;
            if (big && this.IsGrounded())
                this.Launch(f * 12, 0, false, false);   // a small lunge
        }
        this.attackCooldown = big ? 0.36 : 0.27;
        Sfx.play(big ? "slash_big" : "slash", null, { volume: 0.55 });

        const fx = Level.spawn("SlashFx", vec3(p.x, p.y, 2));
        fx.Attach(this, dir, big, f);

        // ---- Actors
        let pogo = false, recoil = false, hitSomething = false;
        const hits = Physics.overlapBox(center, size, { voxels: false, triggers: true, ignore: this });
        for (const a of hits.actors) {
            if (!a.valid)
                continue;
            let result = "none";
            if (a.implements("NailTarget"))
                result = a.send("NailTarget", "OnNailHit", damage, this, dir === "side" ? f : 0, dir === "up" ? 1 : dir === "down" ? -1 : 0);
            else if (a.implements("Damageable")) {
                a.send("Damageable", "TakeDamage", damage, this);
                result = "hit";
            }
            if (result === "hit") {
                hitSomething = true;
                this.GainSoul(11);
                for (let i = 0; i < 2; ++i)
                    Level.spawn("SoulWisp", vec3(a.position.x, a.position.y, 2));
                Game.spark(vec3((a.position.x + center.x) * 0.5, (a.position.y + center.y) * 0.5, 2));
                if (dir === "down") pogo = true;
                else if (dir === "side") recoil = true;
            } else if (result === "blocked") {
                hitSomething = true;
                recoil = true;
                Game.spark(vec3((a.position.x + center.x) * 0.5, (a.position.y + center.y) * 0.5, 2));
                if (dir === "down") pogo = true;
            }
        }

        // ---- Voxels : the nail digs SOFT ground ; spikes and rock make it bounce.
        const dig = Voxels.destroyRect(center, size, { flags: "SOFT", cells: true });
        if (dig.count > 0) {
            this.dirt = Math.min(99, this.dirt + Math.ceil(Game.dirtIn(dig) / 3));
            Sfx.play("dig", null, { volume: 0.5 });
            Game.loot(dig, center);
            Game.debris(dig.cells, 5);
            if (dir === "down") pogo = true;
        }
        const solid = Physics.overlapBox(center, { x: size.x * 0.7, y: size.y * 0.7 }, { actors: false, ignoreTypes: ["Acid"] });
        if (solid.voxels && solid.voxels.count > 0) {
            if (dir === "down") pogo = true;
            else if (dig.count === 0) {
                recoil = true;
                Game.spark(center);
            }
                Sfx.play("clink", null, { volume: 0.5 });
        }

        if (pogo) {
            this.Launch(0, this.jump_velocity * 0.85, false, true);
            this.airJumps = 0;
            this.dashReady = true;
            this.jumpHeld = false;
        } else if (recoil) {
            this.Launch(-f * (this.IsGrounded() ? 10 : 6), 0, false, false);
        }
        if (hitSomething) {
            Game.hitStop(0.05);
            Sfx.play("hit", null, { volume: 0.7 });
        }
        this.RefreshHud();
    }

    // ---- Parry ------------------------------------------------------------------------

    Parry() {
        if (this.parryCooldown > 0)
            return;
        this.parryTime = 0.24;
        this.parryCooldown = 0.6;
        Sfx.play("slash", null, { volume: 0.3 });
        this.anim.setTrigger("parry");
    }

    // ---- Focus (heal) -----------------------------------------------------------------

    TryFocus() {
        if (this.soul < 33 || this.hp >= this.maxHp || !this.IsGrounded())
            return;
        this.SetState("focus");
        this.focusTime = 0;
        Sfx.play("wall_slide", null, { volume: 0.3 });
    }

    GainSoul(n) {
        this.soul = Math.min(99, this.soul + n);
    }

    // ---- Bombs, blocks, hook ------------------------------------------------------

    ThrowBomb() {
        if (!this.abilities.bombs || this.bombCooldown > 0)
            return;
        this.bombCooldown = 0.9;
        Sfx.play("throw", null, { volume: 0.6 });
        const f = this.Facing();
        const b = Level.spawn("Bomb", vec3(this.position.x + f, this.position.y + 0.8, 1));
        const v = this.GetVelocity();
        if (this.inY < 0)
            b.Throw(v.x * 0.5, -4, this);              // drop it below
        else if (this.inY > 0)
            b.Throw(f * 4 + v.x * 0.5, 26, this);     // up
        else
            b.Throw(f * 16 + v.x * 0.5, 14, this);
    }

    // A 2 x 2 block of packed dirt : S + E on the ground builds under you (a
    // step up), in the air under your feet (a bridge), else in front of you.
    PlaceBlock() {
        if (this.dirt < 2) {
            this.ShowMessage("Not enough dirt : dig some with your nail", 1.5);
            return;
        }
        const f = this.Facing();
        const p = this.position;
        const feet = p.y - this.collider_size.y * 0.5;
        const grounded = this.IsGrounded();
        let center, lift = false;
        if (grounded && this.inY < 0) {
            center = vec3(p.x, feet + 1, 0);
            // Room above the head for the step up.
            if (Physics.overlapBox(vec3(p.x, p.y + 2.2, 0), { x: 1.4, y: 1.4 }, { actors: false }).any)
                return;
            lift = true;
        } else if (!grounded) {
            center = vec3(p.x, feet - 1, 0);
        } else {
            center = vec3(p.x + f * 2.2, feet + 1, 0);
            if (Physics.overlapBox(center, { x: 2, y: 2 }, { voxels: false, ignore: [] }).actors.length > 0)
                return;
        }
        if (lift)
            p.y += 2.1;   // the player stands on the new block
        const r = Voxels.fillRect(center, { x: 2, y: 2 }, "Packed");
        if (r.count > 0) {
            this.dirt -= Math.min(this.dirt, Math.max(1, Math.ceil(r.count / 2)));
            if (!grounded)
                this.SetVelocity(this.GetVelocity().x, Math.max(0, this.GetVelocity().y));
            Game.dust(center, 3);
        } else if (lift) {
            Sfx.play("place_block", null, { volume: 0.6 });
            p.y -= 2.1;
        }
        this.RefreshHud();
    }

    FireHook() {
        if (!this.abilities.hook || this.hookCooldown > 0)
            return;
        this.hookCooldown = 0.35;
        const f = this.Facing();
        let dir = { x: f * 0.7, y: 0.7 };
        if (this.inY > 0 && this.inX === 0) dir = { x: 0, y: 1 };
        else if (this.inY < 0) dir = { x: f, y: 0 };
        const from = vec3(this.position.x, this.position.y + 0.5, 0);
        const to = vec3(from.x + dir.x * 24, from.y + dir.y * 24, 0);
        const hit = Physics.raycast(from, to, { ignore: this, actors: false, ignoreTypes: ["Acid"] });
        if (!hit) {
            Game.fx("sprites/hook.png", 1, 0.15, to, 1, {});
            return;
        }
        this.hookPoint = vec3(hit.point.x - dir.x * 0.6, hit.point.y - dir.y * 0.6, 0);
        this.SetState("hook");
        Sfx.play("hook", null, { volume: 0.7 });
        this.hookTip = Level.spawn("HookTip", vec3(hit.point.x, hit.point.y, 1));
        this.hookTip.transform.rotation.z = Math.atan2(dir.y, dir.x) * 180 / Math.PI;
        for (let i = 0; i < 10; ++i)
            this.chain.push(Level.spawn("ChainLink", vec3(this.position.x, this.position.y, 1)));
        Game.spark(hit.point);
    }

    ClearChain() {
        for (const c of this.chain)
            if (c.valid) c.destroy();
        this.chain = [];
        if (this.hookTip && this.hookTip.valid)
            this.hookTip.destroy();
        this.hookTip = null;
    }

    UpdateHook(dt) {
        const dx = this.hookPoint.x - this.position.x, dy = this.hookPoint.y - this.position.y;
        const d = Math.hypot(dx, dy);
        if (d < 2.2 || this.stateTime > 0.7) {
            this.SetState("normal");
            this.Launch(Math.sign(dx) * 8, 22, true, true);   // a hop at the end
            this.airJumps = 0;
            this.dashReady = true;
            return;
        }
        this.SetVelocity(dx / d * 40, dy / d * 40);
        for (let i = 0; i < this.chain.length; ++i) {
            const t = (i + 1) / (this.chain.length + 1);
            if (this.chain[i].valid)
                this.chain[i].position.set(this.position.x + dx * t, this.position.y + 0.5 + (dy - 0.5) * t, 1);
        }
    }

    // ---- Benches -------------------------------------------------------------------

    NearBench(bench, near) {
        this.nearBench = near ? bench : (this.nearBench === bench ? null : this.nearBench);
        this.ShowHint(near ? "W : rest on the bench" : "", near);
    }

    SitDown() {
        const b = this.nearBench;
        this.SetState("sit");
        this.position.set(b.position.x, b.position.y + 0.4, 0);
        this.StopMovement();
        this.hp = this.maxHp;
        this.bench = { position: vec3(b.position.x, b.position.y + 1.6, 0), title: b.title };
        Sfx.play("bench");
        this.ShowMessage("Rested at " + (b.title || "the bench") + "  -  saved", 2.5);
        Level.findImplementing("GameEvents").forEach(a => a.send("GameEvents", "OnPlayerRespawned", this));
        this.RefreshHud();
    }

    StandUp() {
        this.SetState("normal");
    }

    // ---- Abilities / pickups (Collector) ----------------------------------------------

    UnlockAbility(name) {
        if (this.abilities[name])
            return;
        this.abilities[name] = true;
        const texts = {
            dash: "MOTHWING DASH  -  K / SHIFT : dash (once in the air)",
            claw: "MANTIS CLAW  -  jump against a wall to climb it",
            wings: "SHADE WINGS  -  jump again in the air",
            hook: "GRAPPLING HOOK  -  R : pull yourself up (W : straight up)",
            bombs: "BOMBS  -  B : throw a bomb (S : drop it). Breaks ROCK",
        };
        this.ShowMessage(texts[name] || name, 5);
        Engine.setTimeDilation(0.3, 0.8);
        Sfx.play("powerup");
        Game.shake();
        this.soul = 99;
        this.RefreshHud(true);
    }

    OnCollected(kind, amount) {
        if (kind === "geo") { this.geo += amount; Sfx.play("geo", null, { volume: 0.4 }); }
        else if (kind === "shard") {
            this.maxHp = Math.min(9, this.maxHp + 1);
            this.hp = this.maxHp;
            this.ShowMessage("A MASK SHARD  -  one more mask", 3);
            Sfx.play("pickup");
            Game.shake();
        }
        this.RefreshHud(kind === "shard");
    }

    ShowHint(text, show) {
        if (!this.hud || !this.hud.valid)
            return;
        this.SetText("Hint", show ? text : "");
        this.SetVis("Hint", show && text ? "Visible" : "Hidden");
        this.SetVis("HintBack", show && text ? "Visible" : "Hidden");
    }

    // =========================================================================
    // Damage
    // =========================================================================

    TakeDamage(amount, instigator) {
        if (this.state === "dead" || this.state === "won" || this.state === "title")
            return;
        if (this.invulnerable > 0)
            return;

        // Parry : an attack (or a projectile) during the parry window.
        if (this.parryTime > 0 && instigator && instigator.valid) {
            this.parryTime = 0;
            this.invulnerable = 0.35;
            this.GainSoul(33);
            if (instigator.implements("Parryable"))
                instigator.send("Parryable", "OnParried", this);
            Game.spark(vec3((this.position.x + instigator.position.x) * 0.5, (this.position.y + instigator.position.y) * 0.5, 2));
            Engine.setTimeDilation(0.15, 0.35);
            Sfx.play("parry");
            Game.shake();
            this.ShowMessage("PARRY !", 0.6);
            this.RefreshHud();
            return;
        }

        this.hp -= 1;
        if (instigator && instigator.className === "Warden")
            this.hp -= 1;   // the boss hits for two masks
        this.invulnerable = 1.3;
        this.SetState("hurt");
        Sfx.play("hit_heavy", null, { volume: 0.7 });
        Sfx.play("hurt", null, { volume: 0.7 });
        this.anim.setTrigger("hurt");
        const dir = instigator && instigator.valid ? (this.position.x >= instigator.position.x ? 1 : -1) : -this.Facing();
        this.Launch(dir * 16, 14, true, true);
        Game.hitStop(0.12);
        Game.shake();
        this.RefreshHud();
        if (this.hp <= 0)
            this.Die();
    }

    // Spikes and acid (voxel contact events) : damage, then back to safe ground.
    OnVoxelContact(name, type) {
        if (name !== "Spikes" && name !== "Acid")
            return;
        if (this.state === "dead" || this.state === "won")
            return;
        // A pogo on the spikes is allowed : the down attack bounces before the contact.
        if (this.invulnerable > 0 && this.hazardPending)
            return;
        this.invulnerable = 0;
        this.TakeDamage(1, null);
        if (this.state !== "dead") {
            this.hazardPending = true;
            this.hazardTimer = 0.45;
            this.fadeTarget = 1;
        }
    }

    Die() {
        this.SetState("dead");
        this.deaths++;
        this.anim.setTrigger("die");
        Sfx.play("death");
        this.StopMovement();
        Engine.setTimeDilation(0.3, 1.0);
        this.respawnTimer = 2.2;
        this.fadeTarget = 1;
        Level.findImplementing("GameEvents").forEach(a => a.send("GameEvents", "OnPlayerDied", this));
    }

    Respawn() {
        this.position.set(this.bench.position.x, this.bench.position.y, 0);
        this.StopMovement();
        this.hp = this.maxHp;
        this.soul = 0;
        this.invulnerable = 1.5;
        this.SetState("normal");
        this.anim.forceState("Idle");
        this.fadeTarget = 0;
        this.cam.snap();
        Level.findImplementing("GameEvents").forEach(a => a.send("GameEvents", "OnPlayerRespawned", this));
        this.RefreshHud();
    }

    // =========================================================================
    // Every frame
    // =========================================================================

    Update(dt) {
        if (this.paused)
            return;
        this.stateTime += dt;
        if (this.state !== "title" && this.state !== "won")
            this.playTime += dt;

        const grounded = this.IsGrounded();
        const v = this.GetVelocity();

        // Timers
        this.jumpBuffer -= dt;
        this.coyote = grounded ? 0.1 : this.coyote - dt;
        this.dashCooldown -= dt;
        this.attackCooldown -= dt;
        this.comboTimer -= dt;
        this.parryTime -= dt;
        this.parryCooldown -= dt;
        this.bombCooldown -= dt;
        this.hookCooldown -= dt;
        this.wallJumpLock -= dt;
        if (grounded) {
            this.airJumps = 0;
            this.dashReady = true;
        }

        // Invulnerability : blink
        if (this.invulnerable > 0) {
            this.invulnerable -= dt;
            this.anim.visible = this.state === "dead" || this.invulnerable <= 0 || Math.floor(this.invulnerable * 16) % 2 === 0;
        } else {
            this.anim.visible = true;
        }

        // Walls (claw) : touching a wall in the air, toward it.
        this.wallDir = 0;
        if (!grounded && this.state === "normal") {
            for (const side of [1, -1]) {
                const probe = Physics.overlapBox(vec3(this.position.x + side * 1.0, this.position.y + 0.2, 0), { x: 0.4, y: 1.6 },
                                                 { actors: false, ignoreTypes: ["Acid"] });
                if (probe.any) { this.wallDir = side; break; }
            }
        }
        const sliding = this.abilities.claw && this.wallDir !== 0 && this.inX === this.wallDir && v.y < 0 && this.state === "normal";
        if (sliding) {
            this.SetVelocity(v.x, Math.max(v.y, -this.wall_slide_speed));
            this.airJumps = 0;
            this.dashReady = true;
            if (Math.random() < 0.15)
                Game.dust(vec3(this.position.x + this.wallDir * 0.8, this.position.y + 1, 1), 1);
        }

        // Gravity : lighter going up while the jump is held, heavier falling.
        this.gravity = v.y > 0 && this.jumpHeld ? 78 : 105;

        switch (this.state) {
        case "dash":
            this.SetVelocity(this.dashDir * this.dash_speed, 0);
            if (this.stateTime >= this.dash_time) {
                this.SetVelocity(this.dashDir * this.move_speed, 0);
                this.SetState("normal");
            }
            break;
        case "hurt":
            if (this.stateTime > 0.22)
                this.SetState("normal");
            break;
        case "focus":
            this.focusTime += dt;
            if (Math.random() < 0.4)
                Level.spawn("SoulWisp", vec3(this.position.x + (Math.random() - 0.5) * 4, this.position.y + (Math.random() - 0.5) * 3, 2));
            if (this.focusTime >= 0.9) {
                this.focusTime = 0;
                this.soul -= 33;
                Sfx.play("heal");
                this.hp = Math.min(this.maxHp, this.hp + 1);
                Game.spark(this.position);
                this.RefreshHud();
                if (this.soul < 33 || this.hp >= this.maxHp)
                    this.SetState("normal");
            }
            break;
        case "hook":
            this.UpdateHook(dt);
            break;
        case "dead":
            this.respawnTimer -= dt;
            if (this.respawnTimer <= 0)
                this.Respawn();
            break;
        }

        // Hazard (spikes / acid) : fade out, back to the last safe ground.
        if (this.hazardPending) {
            this.hazardTimer -= dt;
            if (this.hazardTimer <= 0) {
                this.hazardPending = false;
                if (this.state !== "dead") {
                    this.position.set(this.lastSafe.x, this.lastSafe.y + 0.2, 0);
                    this.StopMovement();
                    this.SetState("normal");
                    this.cam.snap();
                }
                this.fadeTarget = 0;
            }
        }

        // Last safe ground : grounded, not on a hazard, not hurt.
        this.safeTimer -= dt;
        if (grounded && this.safeTimer <= 0 && this.state === "normal" && this.invulnerable <= 0) {
            this.safeTimer = 0.25;
            const below = Physics.overlapBox(vec3(this.position.x, this.position.y - 1.6, 0), { x: 2.4, y: 0.8 },
                                             { actors: false, types: ["Spikes", "Acid"] });
            if (!below.any)
                this.lastSafe = this.position.clone();
        }

        // Look down (hold S on the ground) / camera ahead of the movement.
        this.lookDown = grounded && this.inY < 0 && this.state === "normal" && Math.abs(v.x) < 1 ? this.lookDown + dt : 0;
        const lookY = this.lookDown > 0.5 ? -9 : (this.inY > 0 && grounded && Math.abs(v.x) < 1 ? 5 : 0);
        this.camLook += ((this.Facing() * 3.5) - this.camLook) * Math.min(1, dt * 2);
        this.camY = (this.camY || 2.5) + ((2.5 + lookY) - (this.camY || 2.5)) * Math.min(1, dt * 4);
        this.cam.offset = vec3(this.camLook, this.camY, 62);

        // Animation graph parameters
        this.anim.setFloat("speed", Math.abs(v.x));
        this.anim.setFloat("vy", v.y);
        this.anim.setBool("grounded", grounded);
        this.anim.setBool("wall", sliding);
        this.anim.setBool("dashing", this.state === "dash");
        this.anim.setBool("focus", this.state === "focus");
        this.anim.setBool("sit", this.state === "sit");
        this.anim.flipX = sliding;   // slides facing away from the wall

        this.UpdateArea();
        this.UpdateUi(dt);
    }

    OnLanded() {
        if (this.GetVelocity().y < -30 || this.state === "normal")
        if (this.GetVelocity().y < -20) Sfx.play("land", null, { volume: 0.45 });
            Game.dust(vec3(this.position.x, this.position.y - 1.3, 1), 3);
    }

    // Notify of the run animation (anim graph).
    OnFootstep() {
        if (Math.random() < 0.5)
        Sfx.play("step", null, { volume: 0.2, pitchVariation: 0.15, gap: 120 });
            Game.dust(vec3(this.position.x - this.Facing() * 0.6, this.position.y - 1.3, 1), 1);
    }

    // =========================================================================
    // UI : HUD, messages, title, pause
    // =========================================================================

    CloseTitle() {
        if (this.title && this.title.valid)
            this.title.destroy();
        this.title = null;
        this.SetState("normal");
        this.ShowArea();
        this.ShowHint("Explore the Hollow. ESC : pause and controls", true);
        this.hintTimer = 5;
    }

    ShowTitle() {
        if (!this.title || !this.title.valid) {
            this.title = UI.create("ui/Title.widget");
            this.title.addToViewport(50);
        }
        this.SetState("title");
    }

    TogglePause() {
        this.paused = !this.paused;
        if (this.paused) {
            Engine.setTimeDilation(0);
            this.pauseMenu = UI.create("ui/PauseMenu.widget");
            this.pauseMenu.addToViewport(40);
        } else {
            Engine.setTimeDilation(1);
            if (this.pauseMenu && this.pauseMenu.valid)
                this.pauseMenu.destroy();
            this.pauseMenu = null;
        }
    }

    BackToBench() {
        if (this.paused)
            this.TogglePause();
        if (this.state !== "dead")
            this.Respawn();
    }

    UpdateArea() {
        const lx = this.position.x - Game.X0, ly = this.position.y - Game.Y0;
        for (const [name, x0, y0, x1, y1] of Wanderer.AREAS) {
            if (lx >= x0 && lx <= x1 && ly >= y0 && ly <= y1) {
                if (name !== this.area) {
                    this.area = name;
                    if (this.state !== "title")
                        this.ShowArea();
                }
                return;
            }
        }
    }

    ShowArea() {
        if (!this.area)
            return;
        this.SetText("Area", this.area);
        this.SetVis("Area", "Visible");
        this.areaTime = 2.5;
    }

    ShowMessage(text, seconds) {
        this.SetText("Message", text);
        this.SetVis("Message", "Visible");
        this.messageTime = seconds;
    }

    // Widget fields are written only when they change (each change rebuilds the widget).
    SetText(name, text) {
        if (!this.hud || !this.hud.valid || this.hudCache[name + ".text"] === text)
            return;
        this.hudCache[name + ".text"] = text;
        this.hud.find(name).text = text;
    }

    SetVis(name, vis) {
        if (!this.hud || !this.hud.valid || this.hudCache[name + ".vis"] === vis)
            return;
        this.hudCache[name + ".vis"] = vis;
        this.hud.find(name).visibility = vis;
    }

    SetField(name, field, value) {
        const key = name + "." + field;
        if (!this.hud || !this.hud.valid || this.hudCache[key] === value)
            return;
        this.hudCache[key] = value;
        this.hud.find(name)[field] = value;
    }

    UpdateUi(dt) {
        if (this.areaTime > 0) {
            this.areaTime -= dt;
            if (this.areaTime <= 0) this.SetVis("Area", "Hidden");
        }
        if (this.messageTime > 0) {
            this.messageTime -= dt;
            if (this.messageTime <= 0) this.SetVis("Message", "Hidden");
        }
        if (this.hintTimer > 0) {
            this.hintTimer -= dt;
            if (this.hintTimer <= 0 && !this.nearBench) this.ShowHint("", false);
        }
        // Screen fade (death, hazards)
        this.fade += (this.fadeTarget - this.fade) * Math.min(1, dt * 6);
        const a = Math.round(this.fade * 20) / 20;
        this.SetField("Fade", "tint", "#000000" + Math.round(a * 255).toString(16).padStart(2, "0"));
        this.SetField("Soul", "percent", Math.round(this.soul) / 99);
        this.RefreshHud();
    }

    RefreshHud(force) {
        if (!this.hud || !this.hud.valid)
            return;
        if (force)
            this.hudCache = {};
        for (let i = 1; i <= 9; ++i) {
            this.SetVis("Mask" + i, i <= this.maxHp ? "Visible" : "Collapsed");
            this.SetField("Mask" + i, "texture", i <= this.hp ? "ui/img/mask_full.png" : "ui/img/mask_empty.png");
        }
        this.SetText("Geo", String(this.geo));
        this.SetText("Dirt", String(this.dirt));
        this.SetVis("Bombs", this.abilities.bombs ? "Visible" : "Collapsed");
        this.SetVis("BombsIcon", this.abilities.bombs ? "Visible" : "Collapsed");
        this.SetText("Bombs", "B");
        for (const a of ["dash", "claw", "wings", "hook", "bombs"])
            this.SetField("Ability_" + a, "tint", this.abilities[a] ? "#ffffffff" : "#ffffff26");
    }

    // ---- The end (Relic) ---------------------------------------------------------

    Win() {
        if (this.state === "won")
            return;
        this.SetState("won");
        this.StopMovement();
        const m = Math.floor(this.playTime / 60), s = Math.floor(this.playTime % 60);
        this.ShowMessage("You found the Heart of the Hollow.   Time " + m + ":" + String(s).padStart(2, "0") +
                         "   Geo " + this.geo + "   Deaths " + this.deaths + "   -   Thanks for playing !", 1e9);
        Engine.setTimeDilation(0.4, 3);
        Sfx.stopMusic();
        Sfx.play("win");
        Game.shake();
    }
}
