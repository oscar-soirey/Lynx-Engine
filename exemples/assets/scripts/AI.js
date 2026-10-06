// Exemple d'IA (ai/Guard.bt) et d'Anim Graph (anim/Hero.animgraph).
// Mettre un acteur "Guard" dans le niveau, et un acteur d'id "player".

// Tache : attaque (declenche l'animation, dure params.duration secondes).
class Attack extends BTTask {
    Execute(dt) {
        this.owner.getComponent("AnimationSprite")?.setTrigger("hurt");   // une animation d'attaque ici
        this.t = 0;
        return BT.Running;
    }
    Tick(dt) {
        this.t += dt;
        return this.t >= (this.params.duration ?? 0.4) ? BT.Success : BT.Running;
    }
    Abort() { }
}

// Tache : va-et-vient autour du point de depart (ne finit jamais : interrompue par un abort).
class Patrol extends BTTask {
    Execute(dt) {
        if (this.origin === undefined)
            this.origin = this.owner.position.x;
        this.time = 0;
        return BT.Running;
    }
    Tick(dt) {
        this.time += dt;
        const amplitude = this.params.amplitude ?? 3;
        this.owner.position.x = this.origin + Math.sin(this.time) * amplitude;
        this.owner.getComponent("AnimationSprite")?.setFloat("speed", Math.abs(Math.cos(this.time)) * amplitude);
        return BT.Running;
    }
    Abort() {
        this.owner.getComponent("AnimationSprite")?.setFloat("speed", 0);
    }
}

// Service : voit le joueur a moins de params.range unites -> Blackboard "target".
class Perception extends BTService {
    Tick(dt) {
        const player = Level.find("player");
        const range = this.params.range ?? 6;
        if (player && Math.abs(player.position.x - this.owner.position.x) < range)
            this.blackboard.set("target", player);
        else
            this.blackboard.clear("target");
    }
}

class Guard extends Actor {
    BeginPlay() {
        this.ai = this.addComponent("AI", { behaviorTree: "ai/Guard.bt" });
        this.sprite = this.addComponent("AnimationSprite", { graph: "anim/Hero.animgraph" });
    }

    // Evenements de l'Anim Graph (notify, on enter / on exit)
    OnHurtEnter() { }
    OnHurtExit() { }
    OnHitFrame() { print("hit frame"); }
}
