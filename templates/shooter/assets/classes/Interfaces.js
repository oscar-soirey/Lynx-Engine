// Interfaces of the game. Engine interface used too : "Damageable"
// (TakeDamage(amount, instigator)).

// Events of the arena, received by the WaveDirector (and anyone else).
class ArenaEvents extends Interface {
    OnEnemyKilled(enemy, points) {}
    OnPlayerDied(player) {}
}

// Picks up health packs.
class Healable extends Interface {
    Heal(amount) {}
}
