// Actor classes of the Lynxie evaluation (written to assets/_lynxie_eval/ by
// run_eval.py, removed at the end). Plain actors with a few properties : the
// tests only look at positions, properties, counts and voxels.

class EvalHero extends Actor {
    static properties = {
        hp: { value: 100, type: "int" },
        speed: 2.0,
        label: "hero",
    };
}

class EvalEnemy extends Actor {
    static properties = {
        hp: { value: 30, type: "int" },
        damage: { value: 5, type: "int" },
        speed: 1.0,
    };
}

class EvalCoin extends Actor {
    static properties = {
        value: { value: 1, type: "int" },
    };
}

class EvalTorch extends Actor {
    static properties = {
        intensity: 1.0,
        radius: 4.0,
    };
}

class EvalDoor extends Actor {
    static properties = {
        open: false,
        locked: true,
    };
}

class EvalCrate extends Actor {
    static properties = {
        weight: 10.0,
    };
}

class EvalPlatform extends Actor {
    static properties = {
        width: { value: 3, type: "int" },
    };
}
