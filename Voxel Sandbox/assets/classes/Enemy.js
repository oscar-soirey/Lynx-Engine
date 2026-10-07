class Enemy extends Miner {
    static properties = {
        speed: 8.0,
        attack_range: 3.0,
        attack_damage: 10,
        follow_distance: 10.0
    }

    BeginPlay() {
        this.addComponent("Sprite", {
            texture: "textures/enemy.png",
            size: {x: 1.5, y: 2.0}
        });
        
        this.attack_cooldown = 0;
    }

    Update(dt) {
        // Get player position
        let player = Level.find("hero");
        if (!player) return;
        
        // Calculate distance to player
        let dx = player.position.x - this.position.x;
        let dy = player.position.y - this.position.y;
        let distance = Math.sqrt(dx * dx + dy * dy);
        
        // Move towards player if not too close
        if (distance > this.attack_range) {
            if (distance > this.follow_distance) {
                // Move toward player
                let moveX = dx / distance;
                let moveY = dy / distance;
                
                this.call("SetVelocity", moveX * this.speed, moveY * this.speed);
            } else {
                // Stop moving when close to follow distance
                this.call("SetVelocity", 0, 0);
            }
        } else {
            // Attack player if in range
            this.Attack(player);
        }
        
        // Update attack cooldown
        if (this.attack_cooldown > 0) {
            this.attack_cooldown -= dt;
        }
    }

    Attack(target) {
        if (this.attack_cooldown <= 0) {
            // Deal damage to player
            target.call("TakeDamage", this.attack_damage);
            this.attack_cooldown = 1.0; // 1 second cooldown
        }
    }
}