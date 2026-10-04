// Exemple de classe JavaScript : un Pawn (classe C++ du jeu) pilote en JS.
//
// Les fichiers de assets/classes/ sont charges automatiquement : la classe
// apparait dans "Place Actors" de l'editeur, peut etre sauvegardee dans un
// niveau (<Wanderer .../>) et creee par Level.spawn("Wanderer") ou new Wanderer().

class Wanderer extends Pawn {
    // Proprietes editables (panneau Details) et sauvegardees avec le niveau.
    static properties = {
        walkTime: 2.0,
        jumpChance: 0.3,
    };

    BeginPlay() {
        this.direction = 1;
        this.timer = 0;

        // Membre protected du Pawn C++ (HPROPERTY dans Pawn::Pawn)
        this.move_speed_ = 6;

        this.sprite = this.addComponent("AnimationSprite", { size: { x: 6, y: 6 } });
        this.sprite.setAnimation("Mushroom.png", 1, 0.1);
    }

    Update(dt) {
        this.timer += dt;

        if (this.timer > this.walkTime) {
            this.timer = 0;
            this.direction = -this.direction;

            // Fonctions protected du Pawn C++ (HFUNCTION dans Pawn::Pawn)
            if (Math.random() < this.jumpChance)
                this.Jump();
        }

        this.Move(this.direction);
    }
}
