#pragma once

#include <cstdint>
#include <vector>

#include "hrl/hrl.h"

class Pawn;

// ============================================================
// Collision : tout ce qui touche aux collisions du jeu
//
//  - tests contre le monde voxel (boite, cellule, ligne de vue)
//  - tests Pawn contre Pawn (grille spatiale)
//  - deplacement d'une boite (CanMoveX / CanMoveY)
//  - debug draw des colliders
//
// Sauf mention contraire, toutes les coordonnees et tailles sont en
// VOXELS (comme HRL_VoxelCheckCollision). Les fonctions de debug draw
// prennent des positions MONDE et des tailles en voxels.
// ============================================================
namespace collision
{
    // Masques de collision : un bit par categorie. Un Pawn collisionne avec
    // ce qui est dans son masque (Pawn::collision_mask_) :
    //   kTerrainMask : les voxels du terrain
    //   kPawnMask    : les autres Pawns (blocage, attaques, projectiles)
    // Pour un Pawn sans kPawnMask, les autres Pawns ne le bloquent pas, il ne
    // les bloque pas, et il ne peut pas etre touche (attaque / projectile).
    constexpr uint32_t kTerrainMask = 1u;
    constexpr uint32_t kPawnMask    = 1u << 31;

    // Masque par defaut d'un Pawn : tout.
    constexpr uint32_t kDefaultMask = kTerrainMask | kPawnMask;

    // Boite alignee sur les axes : centre + demi-tailles (voxels).
    struct Box
    {
        float x = 0.f;
        float y = 0.f;
        float half_w = 0.f;
        float half_h = 0.f;
    };

    // Un Pawn touche par une requete, avec sa boite au moment du test.
    struct PawnHit
    {
        Pawn* pawn = nullptr;
        Box box;
    };

    // --------------------------------------------------------
    // Monde voxel
    // --------------------------------------------------------

    // Flags du voxel de la cellule (cell_x, cell_y), 0 si vide.
    uint32_t GetVoxelFlags(int cell_x, int cell_y);

    // true si une boite (centre x,y ; taille width x height) touche un
    // voxel du masque. Remplit out_collision si non nul.
    bool VoxelBoxHit(
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        HRL_VoxelCollision* out_collision = nullptr
    );

    // Comme VoxelBoxHit, mais ne compte que si la collision porte au moins
    // un des flags de blocking_flags (ex. HRL_VOXEL_COLLISION_RIGHT | INSIDE).
    bool VoxelBoxBlocked(
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        uint32_t blocking_flags,
        HRL_VoxelCollision* out_collision = nullptr
    );

    // true si la cellule (cell_x, cell_y) est pleine (test au centre de la cellule).
    bool IsCellSolid(int cell_x, int cell_y, uint32_t mask);

    // true si aucun voxel plein entre (start) et (end), cellules de depart
    // et d'arrivee ignorees (parcours de grille DDA).
    bool HasLineOfSight(
        float start_x,
        float start_y,
        float end_x,
        float end_y,
        uint32_t mask
    );

    // --------------------------------------------------------
    // Pawns (grille spatiale)
    //
    // Chaque Pawn publie sa boite via SetPawnBody (a chaque changement de
    // position / taille) et la retire avec RemovePawnBody a sa destruction.
    // --------------------------------------------------------

    void SetPawnBody(Pawn* pawn, const Box& box);
    void RemovePawnBody(Pawn* pawn);

    // Ajoute a `out` les Pawns dont la boite chevauche `box` (AABB strict).
    void QueryPawnsInBox(
        const Box& box,
        std::vector<Pawn*>& out,
        const Pawn* ignore = nullptr
    );

    // Ajoute a `out` les Pawns dont la boite touche le disque donne.
    void QueryPawnsInDisc(
        float center_x,
        float center_y,
        float radius,
        std::vector<PawnHit>& out,
        const Pawn* ignore = nullptr
    );

    // true si au moins un Pawn (hors `ignore`) chevauche `box`.
    bool AnyPawnOverlaps(const Box& box, const Pawn* ignore = nullptr);

    // --------------------------------------------------------
    // Deplacement d'une boite (voxels + Pawns)
    // --------------------------------------------------------

    // true si la boite (centre x,y ; taille width x height) peut aller en
    // (x, y) en se deplacant de dx / dy. `self` est ignore pour les Pawns.
    bool CanMoveX(
        const Pawn* self,
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        float dx
    );

    bool CanMoveY(
        const Pawn* self,
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        float dy
    );

    // --------------------------------------------------------
    // Debug draw
    // --------------------------------------------------------

    void SetDebugEnabled(bool enabled);
    bool IsDebugEnabled();

    // Contour d'un rectangle centre en (world_x, world_y, world_z), dimensions
    // en unites MONDE, couleur rgb. Sert a visualiser les bounds du mesh d'un sprite.
    void DrawDebugRectWorld(
        float world_x,
        float world_y,
        float world_z,
        float width_world,
        float height_world,
        float r,
        float g,
        float b
    );

    // Dessine le contour d'un collider centre en (world_x, world_y, world_z)
    // + un point au centre. Dessine toujours (pas de test de IsDebugEnabled).
    void DrawDebugCollider(
        float world_x,
        float world_y,
        float world_z,
        float width_voxels,
        float height_voxels
    );
}
