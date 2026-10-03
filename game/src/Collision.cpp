#include "Collision.h"

#include <Lynx.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "hrl/hrl.h"
#include "../Common.h"
#include "GameClasses.h"

// ============================================================
// Interne : grille spatiale des Pawns
//
// Les Pawns sont indexes par la cellule contenant leur centre, pour que les
// requetes (attaque, projectile, deplacement) n'inspectent que les cellules
// voisines au lieu de tous les Pawns du jeu.
// ============================================================
namespace
{
    constexpr int kPawnCellSize = 16;

    // Taille (en voxels) de la boite de test d'une cellule : petite, au centre.
    constexpr float kCellProbeSize = 0.1f;

    struct Cell
    {
        int x;
        int y;

        bool operator==(const Cell& other) const
        {
            return x == other.x && y == other.y;
        }
    };

    struct CellHash
    {
        std::size_t operator()(const Cell& cell) const
        {
            const std::uint64_t x = static_cast<std::uint32_t>(cell.x);
            const std::uint64_t y = static_cast<std::uint32_t>(cell.y);
            return static_cast<std::size_t>((x << 32) ^ y);
        }
    };

    struct BodyEntry
    {
        collision::Box box;
        Cell cell;
    };

    std::unordered_map<const Pawn*, BodyEntry> g_bodies;
    std::unordered_map<Cell, std::unordered_set<Pawn*>, CellHash> g_cells;

    bool g_debug_enabled = false;

    Cell CellOf(float voxel_x, float voxel_y)
    {
        return {
            static_cast<int>(std::floor(voxel_x / static_cast<float>(kPawnCellSize))),
            static_cast<int>(std::floor(voxel_y / static_cast<float>(kPawnCellSize)))
        };
    }

    void EraseFromCell(const Cell& cell, Pawn* pawn)
    {
        auto it = g_cells.find(cell);
        if (it == g_cells.end())
            return;

        it->second.erase(pawn);
        if (it->second.empty())
            g_cells.erase(it);
    }

    bool BoxesOverlap(const collision::Box& a, const collision::Box& b)
    {
        return std::abs(a.x - b.x) < a.half_w + b.half_w &&
               std::abs(a.y - b.y) < a.half_h + b.half_h;
    }

    // Appelle fn(Pawn*, const Box&) pour chaque Pawn dont la cellule est a
    // portee de `area` (candidats : le test precis est fait par l'appelant).
    // fn retourne true pour arreter la recherche. Retourne true si arretee.
    template <typename Fn>
    bool ForEachPawnNear(const collision::Box& area, const Pawn* ignore, Fn&& fn)
    {
        // On elargit la zone d'une cellule pour couvrir les colliders qui
        // depassent de leur cellule.
        const float cell_size = static_cast<float>(kPawnCellSize);

        const int min_cell_x = static_cast<int>(
            std::floor((area.x - area.half_w - cell_size) / cell_size));
        const int max_cell_x = static_cast<int>(
            std::floor((area.x + area.half_w + cell_size) / cell_size));
        const int min_cell_y = static_cast<int>(
            std::floor((area.y - area.half_h - cell_size) / cell_size));
        const int max_cell_y = static_cast<int>(
            std::floor((area.y + area.half_h + cell_size) / cell_size));

        for (int cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y)
        {
            for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x)
            {
                const auto cell_it = g_cells.find({cell_x, cell_y});
                if (cell_it == g_cells.end())
                    continue;

                for (Pawn* pawn : cell_it->second)
                {
                    if (pawn == ignore)
                        continue;

                    // Pawn sans kPawnMask : invisible pour les autres Pawns.
                    if ((pawn->GetCollisionMask() & collision::kPawnMask) == 0u)
                        continue;

                    const auto body_it = g_bodies.find(pawn);
                    if (body_it == g_bodies.end())
                        continue;

                    if (fn(pawn, body_it->second.box))
                        return true;
                }
            }
        }

        return false;
    }
}

namespace collision
{
    // ========================================================
    // Monde voxel
    // ========================================================

    uint32_t GetVoxelFlags(int cell_x, int cell_y)
    {
        const uint8_t type = HRL_GetVoxelType(lynx::GetScene(), cell_x, cell_y);

        if (type == 0)
            return 0;

        return voxelData[type - 1].flags;
    }

    bool VoxelBoxHit(
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        HRL_VoxelCollision* out_collision)
    {
        HRL_VoxelCollision collision{};

        const bool collided = HRL_VoxelCheckCollision(
            lynx::GetScene(),
            x,
            y,
            width,
            height,
            mask & ~kPawnMask,   // le bit Pawn n'existe pas cote HRL
            &collision
        ) == HRL_TRUE;

        if (out_collision)
            *out_collision = collision;

        return collided;
    }

    bool VoxelBoxBlocked(
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        uint32_t blocking_flags,
        HRL_VoxelCollision* out_collision)
    {
        HRL_VoxelCollision collision{};

        if (!VoxelBoxHit(x, y, width, height, mask, &collision))
        {
            if (out_collision)
                *out_collision = collision;

            return false;
        }

        if (out_collision)
            *out_collision = collision;

        return (collision.flags & blocking_flags) != 0u;
    }

    bool IsCellSolid(int cell_x, int cell_y, uint32_t mask)
    {
        HRL_VoxelCollision collision{};

        // Test au centre de la cellule [cell, cell + 1)
        const bool collided = VoxelBoxHit(
            static_cast<float>(cell_x) + 0.5f,
            static_cast<float>(cell_y) + 0.5f,
            kCellProbeSize,
            kCellProbeSize,
            mask,
            &collision
        );

        return collided && (collision.flags & HRL_VOXEL_COLLISION_INSIDE) != 0u;
    }

    bool HasLineOfSight(
        float start_x,
        float start_y,
        float end_x,
        float end_y,
        uint32_t mask)
    {
        int cx = static_cast<int>(std::floor(start_x));
        int cy = static_cast<int>(std::floor(start_y));
        const int end_cx = static_cast<int>(std::floor(end_x));
        const int end_cy = static_cast<int>(std::floor(end_y));

        const float dx = end_x - start_x;
        const float dy = end_y - start_y;

        const int step_x = dx > 0.f ? 1 : -1;
        const int step_y = dy > 0.f ? 1 : -1;

        // t va de 0 (depart) a 1 (arrivee)
        const float t_delta_x = dx != 0.f ? std::fabs(1.f / dx) : INFINITY;
        const float t_delta_y = dy != 0.f ? std::fabs(1.f / dy) : INFINITY;

        float t_max_x = dx > 0.f ? (cx + 1 - start_x) / dx
                      : dx < 0.f ? (start_x - cx) / -dx
                      : INFINITY;
        float t_max_y = dy > 0.f ? (cy + 1 - start_y) / dy
                      : dy < 0.f ? (start_y - cy) / -dy
                      : INFINITY;

        // Garde-fou contre les erreurs d'arrondi flottant
        int max_steps = std::abs(end_cx - cx) + std::abs(end_cy - cy) + 2;

        // On ignore la cellule de depart et celle d'arrivee
        while ((cx != end_cx || cy != end_cy) && max_steps-- > 0)
        {
            if (t_max_x < t_max_y)
            {
                cx += step_x;
                t_max_x += t_delta_x;
            }
            else
            {
                cy += step_y;
                t_max_y += t_delta_y;
            }

            if (cx == end_cx && cy == end_cy)
                break;

            if (IsCellSolid(cx, cy, mask))
                return false;
        }

        return true;
    }

    // ========================================================
    // Pawns
    // ========================================================

    void SetPawnBody(Pawn* pawn, const Box& box)
    {
        const Cell new_cell = CellOf(box.x, box.y);

        auto it = g_bodies.find(pawn);

        if (it == g_bodies.end())
        {
            g_bodies[pawn] = BodyEntry{box, new_cell};
            g_cells[new_cell].insert(pawn);
            return;
        }

        BodyEntry& entry = it->second;
        entry.box = box;

        if (!(entry.cell == new_cell))
        {
            EraseFromCell(entry.cell, pawn);
            g_cells[new_cell].insert(pawn);
            entry.cell = new_cell;
        }
    }

    void RemovePawnBody(Pawn* pawn)
    {
        auto it = g_bodies.find(pawn);
        if (it == g_bodies.end())
            return;

        EraseFromCell(it->second.cell, pawn);
        g_bodies.erase(it);
    }

    void QueryPawnsInBox(const Box& box, std::vector<Pawn*>& out, const Pawn* ignore)
    {
        ForEachPawnNear(box, ignore, [&](Pawn* pawn, const Box& other)
        {
            if (BoxesOverlap(box, other))
                out.push_back(pawn);

            return false;
        });
    }

    void QueryPawnsInDisc(
        float center_x,
        float center_y,
        float radius,
        std::vector<PawnHit>& out,
        const Pawn* ignore)
    {
        const Box area{center_x, center_y, radius, radius};

        ForEachPawnNear(area, ignore, [&](Pawn* pawn, const Box& other)
        {
            // Point de la boite du Pawn le plus proche du centre du disque.
            const float closest_x = std::clamp(
                center_x, other.x - other.half_w, other.x + other.half_w);
            const float closest_y = std::clamp(
                center_y, other.y - other.half_h, other.y + other.half_h);

            const float dx = closest_x - center_x;
            const float dy = closest_y - center_y;

            if ((dx * dx + dy * dy) <= radius * radius)
                out.push_back(PawnHit{pawn, other});

            return false;
        });
    }

    bool AnyPawnOverlaps(const Box& box, const Pawn* ignore)
    {
        return ForEachPawnNear(box, ignore, [&](Pawn*, const Box& other)
        {
            return BoxesOverlap(box, other);
        });
    }

    // ========================================================
    // Deplacement
    // ========================================================

    bool CanMoveX(
        const Pawn* self,
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        float dx)
    {
        if (dx == 0.f)
            return true;

        if ((mask & kPawnMask) != 0u &&
            AnyPawnOverlaps(Box{x, y, width * 0.5f, height * 0.5f}, self))
            return false;

        const uint32_t side = dx > 0.f
            ? HRL_VOXEL_COLLISION_RIGHT
            : HRL_VOXEL_COLLISION_LEFT;

        return !VoxelBoxBlocked(
            x, y, width, height, mask,
            side | HRL_VOXEL_COLLISION_INSIDE);
    }

    bool CanMoveY(
        const Pawn* self,
        float x,
        float y,
        float width,
        float height,
        uint32_t mask,
        float dy)
    {
        if (dy == 0.f)
            return true;

        if ((mask & kPawnMask) != 0u &&
            AnyPawnOverlaps(Box{x, y, width * 0.5f, height * 0.5f}, self))
            return false;

        const uint32_t side = dy > 0.f
            ? HRL_VOXEL_COLLISION_TOP
            : HRL_VOXEL_COLLISION_BOTTOM;

        return !VoxelBoxBlocked(
            x, y, width, height, mask,
            side | HRL_VOXEL_COLLISION_INSIDE);
    }

    // ========================================================
    // Debug draw
    // ========================================================

    void SetDebugEnabled(bool enabled)
    {
        g_debug_enabled = enabled;
    }

    bool IsDebugEnabled()
    {
        return g_debug_enabled;
    }

    void DrawDebugRectWorld(
        float world_x,
        float world_y,
        float world_z,
        float width_world,
        float height_world,
        float r,
        float g,
        float b)
    {
        const float left   = world_x - width_world * 0.5f;
        const float right  = world_x + width_world * 0.5f;
        const float bottom = world_y - height_world * 0.5f;
        const float top    = world_y + height_world * 0.5f;

        // The voxel world front face is at Z = 0 and the gameplay camera
        // looks toward negative Z from positive Z. Keep the debug outline just
        // in front of that face so it is not depth-occluded by the voxel world.
        const float xs[] = {left, right, right, left};
        const float ys[] = {bottom, bottom, top, top};
        const float zs[] = {world_z, world_z, world_z, world_z};

        HRL_DrawDebugPolygon(
            lynx::GetScene(),
            HRL_DEBUG_HOLLOW,
            xs,
            ys,
            zs,
            4,
            r,
            g,
            b
        );
    }

    void DrawDebugCollider(
        float world_x,
        float world_y,
        float world_z,
        float width_voxels,
        float height_voxels)
    {
        float half_width_world;
        float half_height_world;

        if (HRL_VoxelToWorldCoordinates(
                lynx::GetScene(),
                width_voxels * 0.5f,
                height_voxels * 0.5f,
                &half_width_world,
                &half_height_world) != HRL_TRUE)
        {
            return;
        }

        DrawDebugRectWorld(
            world_x,
            world_y,
            world_z,
            half_width_world * 2.f,
            half_height_world * 2.f,
            1.f,
            0.1f,
            0.1f
        );

        HRL_DrawDebugPoint(
            lynx::GetScene(),
            world_x,
            world_y,
            world_z,
            6.f,
            1.f,
            1.f,
            0.f
        );
    }
}
