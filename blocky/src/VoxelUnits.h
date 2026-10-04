#pragma once

#include <hrl/hrl.h>
#include <Lynx.h>

// ============================================================
// Unites du gameplay
//
// Le gameplay (colliders, rayons d'attaque, hauteur de marche, hitbox des
// projectiles, vitesses...) ne doit PAS dependre du reglage
// "Physical voxel size". Regle :
//
//  - Positions (transform.location), vitesses et accelerations :
//    unites MONDE (inchange).
//  - Longueurs de gameplay (collider_width_, attack_radius, max_step_height_,
//    kHitSize, explosion_radius_, collider_size_ de Bouncy...) :
//    "unites de jeu". 1 unite de jeu = kGameUnit unites monde, quelle que soit
//    la taille des voxels.
//  - Monde voxel (HRL_*Voxel*, module Collision) : VOXELS. On convertit au
//    dernier moment avec GameToVoxels() / VoxelsPerGameUnit().
//
// Seul ce qui concerne la grille elle-meme reste en voxels (pas de sous-
// deplacement, sonde du sol, taille d'une cellule du spatial hash...).
// ============================================================
namespace units
{
    // Taille d'un voxel (unites monde) pour laquelle les valeurs de gameplay
    // ont ete reglees. Ce n'est PAS le reglage runtime : cette constante ne
    // bouge jamais. Si ton gameplay etait cale sur une autre taille de voxel,
    // modifie uniquement cette valeur.
    inline constexpr float kGameUnit = 0.3f;

    // Taille actuelle d'un voxel en unites monde (reglage "Physical voxel size").
    inline float VoxelSizeWorld()
    {
        float x0 = 0.f;
        float y0 = 0.f;
        float x1 = 0.f;
        float y1 = 0.f;

        if (HRL_VoxelToWorldCoordinates(lynx::GetScene(), 0.f, 0.f, &x0, &y0) == HRL_TRUE &&
            HRL_VoxelToWorldCoordinates(lynx::GetScene(), 1.f, 0.f, &x1, &y1) == HRL_TRUE &&
            x1 > x0)
        {
            return x1 - x0;
        }

        // Scene invalide : conversion neutre (1 unite de jeu = 1 voxel).
        return kGameUnit;
    }

    // Nombre de voxels dans une unite de jeu. A calculer une fois puis
    // reutiliser quand on convertit plusieurs valeurs d'affilee.
    inline float VoxelsPerGameUnit()
    {
        return kGameUnit / VoxelSizeWorld();
    }

    inline float GameToVoxels(float game_units)
    {
        return game_units * VoxelsPerGameUnit();
    }

    inline float GameToWorld(float game_units)
    {
        return game_units * kGameUnit;
    }
}
