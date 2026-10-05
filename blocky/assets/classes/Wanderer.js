// Exemple de classe JavaScript : un Pawn (classe C++ du jeu) pilote en JS.
//
// Les fichiers de assets/classes/ sont charges automatiquement : la classe
// apparait dans "Place Actors" de l'editeur, peut etre sauvegardee dans un
// niveau (<Wanderer .../>) et creee par Level.spawn("Wanderer") ou new Wanderer().

class Wanderer extends Pawn {
    // Proprietes editables (panneau Details) et sauvegardees avec le niveau.
    static properties = {
        walkTime: 2.0,
        jumpChance: 0.3
		};

    BeginPlay() {
        console.log("start game");
    }

    EndPlay() {
        print("hello");
    }
}

