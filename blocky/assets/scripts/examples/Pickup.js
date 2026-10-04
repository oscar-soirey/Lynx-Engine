// Exemple : objet a ramasser, entierement construit avec des composants.
// A attacher a un acteur (propriete "scripts" : scripts/examples/Pickup.js).

let sprite, box, sound;
let time = 0;

function BeginPlay() {
    sprite = parent.addComponent("AnimationSprite", { size: {x: 2, y: 2} });
    sprite.setAnimation("Mushroom.png", 1, 0.1, true);

    box = parent.addComponent("BoxCollider", { size: {x: 2, y: 2}, trigger: true, debugDraw: true });

    sound = parent.addComponent("SoundSource", { sound: "hit.wav", spatial: true, pitchVariation: 0.1 });

    parent.addComponent("Light", { color: vec3(1, 0.9, 0.4), intensity: 2, offset: vec3(0, 0, 2) });
}

let collected = false;

function Update(dt) {
    // petit flottement
    time += dt;
    sprite.offset = vec3(0, Math.sin(time * 3) * 0.3, 0);

    // Le Pawn de Blocky n'a pas de BoxCollider (il a son propre module de
    // collision) : on le detecte a la distance.
    const pawn = Level.find("Pawn");

    if (pawn) {
        const dx = pawn.position.x - parent.position.x;
        const dy = pawn.position.y - parent.position.y;

        if (dx * dx + dy * dy < 4)
            collect();
    }
}

// Appele quand une autre boite (acteur avec un BoxCollider) entre dans la sienne.
function OnBeginOverlap(other) {
    collect();
}

function collect() {
    if (collected)
        return;

    collected = true;
    sound.play();
    sprite.visible = false;
    parent.removeComponent("BoxCollider");
    parent.lifetime = 0.5;     // detruit dans 0.5 s (le son a le temps de jouer)
}
