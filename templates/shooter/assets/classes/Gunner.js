// The player : a plain Actor with a blocking Collider and a velocity.
// Twin-stick : WASD moves, the arrows (right stick) aim and the aim direction
// is kept ; Space / J / click fires, Shift dashes (invulnerable during the dash).

class Gunner extends Actor {
    static interfaces = ["Damageable", "Healable"];

    static properties = {
        max_hp: 100.0,
        speed: 15.0,
        fire_interval: 0.11,
        bullet_speed: 70.0,
        bullet_damage: 12.0,
        spread: 3.0,            // degrees
        dash_speed: 55.0,
    };

    constructor() {
        super();
        this.autoPossessPlayer = 0;
        this.addTag("player");
        this.anim = this.addComponent("AnimationSprite", { size: { x: 3.2, y: 3.2 }, offset: vec3(0, 0.6, 0) });
        this.anim.setAnimation("sprites/hero_top.png", 4, 0.1, true);
        this.addComponent("Collider", { size: { x: 1.6, y: 1.6 } });
    }

    BeginPlay() {
        this.hp = this.max_hp;
        this.aim = { x: 0, y: 1 };
        this.cooldown = 0;
        this.dashLeft = 0;
        this.dashCooldown = 0;
        this.dead = false;
        this.start = this.position.clone();
        this.addComponent("Camera", { offset: vec3(0, -6, 75), rotation: vec3(0, -85, 0), fov: 40, followSpeed: 6 });
        this.addComponent("Light", { type: "point", color: vec3(1, 0.9, 0.7), intensity: 1.2, offset: vec3(0, 0, 8) });
        this.cursor = Level.spawn("AimCursor", this.position);

        this.hud = UI.create("ui/Hud.widget");
        this.hud.addToViewport(10);
        this.RefreshHud();
    }

    ProcessInput(player) {
        if (this.dead) {
            this.velocity = vec3(0, 0, 0);
            if (Input.pressed("restart"))
                this.Restart();
            return;
        }

        let mx = Input.axis("move_x"), my = Input.axis("move_y");
        const ml = Math.hypot(mx, my);
        if (ml > 1) { mx /= ml; my /= ml; }

        const ax = Input.axis("aim_x"), ay = Input.axis("aim_y");
        const al = Math.hypot(ax, ay);
        if (al > 0.3)
            this.aim = { x: ax / al, y: ay / al };
        else if (ml > 0.3 && !Input.held("fire"))
            this.aim = { x: mx / ml, y: my / ml };

        if (Input.pressed("dash") && this.dashCooldown <= 0 && ml > 0.1) {
            this.dashDir = { x: mx / ml, y: my / ml };
            this.dashLeft = 0.15;
            this.dashCooldown = 0.8;
        }

        if (this.dashLeft > 0)
            this.velocity = vec3(this.dashDir.x * this.dash_speed, this.dashDir.y * this.dash_speed, 0);
        else
            this.velocity = vec3(mx * this.speed, my * this.speed, 0);

        if (ml > 0.1) this.anim.play(); else this.anim.pause();
        if (Math.abs(this.aim.x) > 0.1) this.anim.flipX = this.aim.x < 0;

        if (Input.held("fire") && this.cooldown <= 0)
            this.Fire();
    }

    Fire() {
        this.cooldown = this.fire_interval;
        const a = Math.atan2(this.aim.y, this.aim.x) + (Math.random() - 0.5) * this.spread * Math.PI / 180;
        const dir = { x: Math.cos(a), y: Math.sin(a) };
        const muzzle = vec3(this.position.x + dir.x * 2, this.position.y + 0.6 + dir.y * 2, 1);
        const bullet = Level.spawn("Bullet", muzzle);
        bullet.Fire(dir, this.bullet_speed, this.bullet_damage, this);
    }

    Update(dt) {
        this.cooldown -= dt;
        this.dashLeft -= dt;
        this.dashCooldown -= dt;
        if (this.cursor && this.cursor.valid)
            this.cursor.position.set(this.position.x + this.aim.x * 6, this.position.y + 0.6 + this.aim.y * 6, 1);
        if (this.flash > 0) {
            this.flash -= dt;
            this.anim.visible = this.flash <= 0 || Math.floor(this.flash * 20) % 2 === 0;
        }
    }

    TakeDamage(amount, instigator) {
        if (this.dead || this.dashLeft > 0)
            return;                                     // dodged
        this.hp -= amount;
        this.flash = 0.25;
        if (this.hp <= 0) {
            this.hp = 0;
            this.dead = true;
            this.anim.visible = false;
            Level.spawn("Explosion", this.position);
            for (const d of Level.findImplementing("ArenaEvents"))
                d.send("ArenaEvents", "OnPlayerDied", this);
        }
        this.RefreshHud();
    }

    Heal(amount) {
        this.hp = Math.min(this.max_hp, this.hp + amount);
        this.RefreshHud();
    }

    Restart() {
        this.dead = false;
        this.hp = this.max_hp;
        this.anim.visible = true;
        this.position.set(this.start.x, this.start.y, 0);
        for (const d of Level.findImplementing("ArenaEvents"))
            if (d.Restart) d.Restart();
        this.RefreshHud();
    }

    RefreshHud() {
        if (!this.hud || !this.hud.valid)
            return;
        this.hud.find("Health").percent = this.hp / this.max_hp;
        this.hud.find("HealthText").text = Math.ceil(this.hp) + " PV";
    }
}

class AimCursor extends Actor {
    constructor() {
        super();
        this.addComponent("StaticSprite", { texture: "sprites/spark.png", size: { x: 1.6, y: 1.6 } });
    }
}
