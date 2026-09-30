#pragma once

#include <Lynx.h>
#include "hrl/hrl.h"

#include "../Common.h"


class BlockParticles
{
public:

    void Initialize()
    {
        if (initialized_)
            return;

        const HRL_id scene = lynx::GetScene();

        system_ = HRL_CreateVFXSystem(scene);

        if (system_ == HRL_INVALID_ID)
            return;

        HRL_SetVFXSystemDuration(
            system_,
            0.0f
        );

        HRL_SetVFXSystemLooping(
            system_,
            HRL_FALSE
        );

        HRL_SetVFXSystemAutoUpdate(
            system_,
            HRL_TRUE
        );

        HRL_SetVFXSystemEnabled(
            system_,
            HRL_TRUE
        );


        // ----------------------------------------------------
        // One emitter for each voxel type.
        //
        // The color comes directly from voxelColors[]
        // defined in Common.h.
        // ----------------------------------------------------

        for (int i = 0; i < 4; ++i)
        {
            const float* color = voxelColors[i];

            CreateEmitter(
                i,
                color[0],
                color[1],
                color[2],
                color[3]
            );
        }


        // Start the system once.
        //
        // We don't reset/replay it for every block.
        HRL_PlayVFXSystem(system_);

        initialized_ = true;
    }


    // ========================================================
    // Play particles at voxel coordinates.
    //
    // voxel_type:
    //     1 -> voxelColors[0]
    //     2 -> voxelColors[1]
    //     3 -> voxelColors[2]
    //     4 -> voxelColors[3]
    // ========================================================

    void PlayVoxel(
    float voxel_x,
    float voxel_y,
    float world_z,
    int voxel_type,
    float dir_x,
    float dir_y)
    {
        if (!initialized_)
            return;

        if (voxel_type < 1 || voxel_type > 4)
            return;

        float world_x;
        float world_y;

        if (HRL_VoxelToWorldCoordinates(
            lynx::GetScene(),
            voxel_x,
            voxel_y,
            &world_x,
            &world_y
        ) != HRL_TRUE)
        {
            return;
        }

        const int emitter_index = voxel_type - 1;

        HRL_id emitter = emitters_[emitter_index];

        if (emitter == HRL_INVALID_ID)
            return;

        HRL_SetVFXEmitterPosition(
            emitter,
            world_x,
            world_y,
            world_z
        );

        // ----------------------------------------------------
        // Initial velocity
        // ----------------------------------------------------

        const float speed = 5.0f;
        const float speed_variation = 5.5f;
        const float spreadX = 3.2f;
        const float spreadY = 1.2f;

        const float min_speed = speed - speed_variation;
        const float max_speed = speed + speed_variation;

        HRL_SetVFXEmitterInitialVelocity(
            emitter,

            // X
            dir_x * min_speed - spreadX,
            // Y
            dir_y * min_speed - spreadY,
            // Z
            0.0f,

            // X
            dir_x * max_speed + spreadX,
            // Y
            dir_y * max_speed + spreadY,
            // Z
            0.0f
        );

        HRL_SetVFXEmitterBurst(
            emitter,
            burst_count_
        );
    }


    // ========================================================
    // Configuration
    // ========================================================

    void SetBurstCount(HRL_uint count)
    {
        burst_count_ = count;
    }


private:

    // ========================================================
    // Create an emitter for one voxel type
    // ========================================================

    void CreateEmitter(
        int index,
        float r,
        float g,
        float b,
        float a)
    {
        const HRL_id emitter =
            HRL_CreateVFXEmitter(system_);

        if (emitter == HRL_INVALID_ID)
            return;

        emitters_[index] = emitter;


        // ----------------------------------------------------
        // Basic state
        // ----------------------------------------------------

        HRL_SetVFXEmitterEnabled(
            emitter,
            HRL_TRUE
        );


        // ----------------------------------------------------
        // Spawn
        // ----------------------------------------------------

        // No automatic spawning.
        // Particles are generated exclusively by Burst().
        HRL_SetVFXEmitterSpawnRate(
            emitter,
            0.0f
        );

        HRL_SetVFXEmitterMaxParticles(
            emitter,
            512
        );

        HRL_SetVFXEmitterLifetime(
            emitter,
            0.35f,
            0.75f
        );


        // ----------------------------------------------------
        // Spawn shape
        // ----------------------------------------------------

        HRL_SetVFXEmitterSpawnShape(
            emitter,
            HRL_VFX_SHAPE_BOX
        );

        HRL_SetVFXEmitterShapeSize(
            emitter,
            0.8f,
            0.8f,
            0.8f
        );


        // ----------------------------------------------------
        // Initial velocity
        // ----------------------------------------------------

        HRL_SetVFXEmitterInitialVelocity(
            emitter,

            -3.0f,
             3.0f,
            -1.0f,

             3.0f,
             7.0f,
             1.0f
        );


        // ----------------------------------------------------
        // Gravity
        // ----------------------------------------------------

        HRL_SetVFXGravity(
            emitter,
            0.0f,
            -30.0f,
            0.0f
        );


        // ----------------------------------------------------
        // Rotation
        // ----------------------------------------------------

        HRL_SetVFXEmitterInitialRotation(
            emitter,

            0.0f,
            0.0f,
            0.0f,

            0.0f,
            0.0f,
            360.0f
        );

        HRL_SetVFXEmitterAngularVelocity(
            emitter,

            0.0f,
            0.0f,
            0.0f,

            0.0f,
            0.0f,
             360.0f
        );


        // ----------------------------------------------------
        // Rendering
        // ----------------------------------------------------

        HRL_SetVFXEmitterRenderMode(
            emitter,
            HRL_VFX_RENDER_BILLBOARD
        );

        HRL_SetVFXEmitterBlendMode(
            emitter,
            HRL_VFX_BLEND_ALPHA
        );

        HRL_SetVFXEmitterParticleSize(
            emitter,
            0.25f,
            0.25f
        );

        HRL_SetVFXEmitterSimulationSpace(
            emitter,
            HRL_VFX_SIMULATION_WORLD
        );

        auto texture_data = lynx::fs::ReadBinary("block_particle_texture.png");
        HRL_id emitter_texture = HRL_CreateTexture(reinterpret_cast<const char*>(texture_data.data()), texture_data.size());
        HRL_SetVFXEmitterTexture(emitter, emitter_texture);


        // ----------------------------------------------------
        // Color
        // ----------------------------------------------------

        HRL_id color_curve =
            HRL_CreateVFXColorCurve(emitter);

        if (color_curve == HRL_INVALID_ID)
            return;

        HRL_AddVFXColorKey(
            color_curve,
            0.0f,
            r,
            g,
            b,
            a
        );

        HRL_AddVFXColorKey(
            color_curve,
            1.0f,
            0,
            0,
            0,
            1
        );

        HRL_SetVFXEmitterColorCurve(
            emitter,
            color_curve
        );
    }


private:

    HRL_id system_ =
        HRL_INVALID_ID;

    HRL_id emitters_[4] =
    {
        HRL_INVALID_ID,
        HRL_INVALID_ID,
        HRL_INVALID_ID,
        HRL_INVALID_ID
    };

    HRL_uint burst_count_ = 8;

    bool initialized_ = false;
};