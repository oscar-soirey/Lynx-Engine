// Interfaces of the game : messages between classes that do not know each
// other (see scripting/README.md, "Interfaces"). Engine ones : Damageable
// (TakeDamage(amount, instigator)), Interactable, VoxelEvents.

// The player : pickups, shrines, benches talk to it through this.
class Collector extends Interface {
    OnCollected(kind, amount) {}
    UnlockAbility(name) {}
    NearBench(bench, near) {}
    ShowHint(text, show) {}
}

// An attack that can be parried (the attacker gets stunned / the projectile
// is sent back). `by` : who parried.
class Parryable extends Interface {
    OnParried(by) {}
}

// Hit by the nail : direction of the hit (for the knockback), and whether
// the attacker should bounce (pogo) or recoil (shield).
class NailTarget extends Interface {
    // Returns "hit", "blocked" (shield : the attacker recoils) or "none".
    OnNailHit(amount, attacker, dirX, dirY) { return "none"; }
}

// Game wide events (the boss arena, the end).
class GameEvents extends Interface {
    OnBossDefeated(boss) {}
    OnPlayerDied(player) {}
    OnPlayerRespawned(player) {}
}
