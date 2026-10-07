// Interfaces of the game (scripting/README.md, "Interfaces") : the sender does
// not know the class of the receiver. Engine interfaces used too :
// "Damageable" (TakeDamage(amount, instigator)).

// Picks up things : coins, hearts...
class Collector extends Interface {
    OnCollected(kind, amount) {}
}

// Can be sent back to a checkpoint.
class Respawnable extends Interface {
    SetCheckpoint(position) {}
}

// Reached the end of the level.
class Finisher extends Interface {
    OnFinish(goal) {}
}
