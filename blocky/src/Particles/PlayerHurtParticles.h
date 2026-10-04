#pragma once

#include <Lynx.h>
#include "hrl/hrl.h"

class PlayerHurtParticles
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

        HRL_SetVFXSystemDuration(system_, 0.0f);
        HRL_SetVFXSystemLooping(system_, HRL_FALSE);
        HRL_SetVFXSystemAutoUpdate(system_, HRL_TRUE);
        HRL_SetVFXSystemEnabled(system_, HRL_TRUE);

        CreateEmitter();

        HRL_PlayVFXSystem(system_);

        initialized_ = true;
    }


    void Play(
        float world_x,
        float world_y,
        float world_z)
    {
        if (!initialized_)
            return;

        if (emitter_ == HRL_INVALID_ID)
            return;

        HRL_SetVFXEmitterPosition(
            emitter_,
            world_x,
            world_y,
            world_z
        );

        // ----------------------------------------------------
        // Direction aléatoire dans toutes les directions
        // ----------------------------------------------------

        const float min_speed = -8.0f;
        const float max_speed = 8.0f;

        HRL_SetVFXEmitterInitialVelocity(
            emitter_,

            // Min
            min_speed,
            min_speed,
            min_speed,

            // Max
            max_speed,
            max_speed,
            max_speed
        );

        // Un seul appel génère tout le sang.
        HRL_SetVFXEmitterBurst(
            emitter_,
            40
        );
    }


private:

    void CreateEmitter()
    {
        emitter_ = HRL_CreateVFXEmitter(system_);

        if (emitter_ == HRL_INVALID_ID)
            return;


        // ----------------------------------------------------
        // Basic state
        // ----------------------------------------------------

        HRL_SetVFXEmitterEnabled(
            emitter_,
            HRL_TRUE
        );


        // ----------------------------------------------------
        // Spawn
        // ----------------------------------------------------

        // Aucun spawn automatique.
        // Tout est généré par Burst().
        HRL_SetVFXEmitterSpawnRate(
            emitter_,
            0.0f
        );

        HRL_SetVFXEmitterMaxParticles(
            emitter_,
            512
        );

        HRL_SetVFXEmitterLifetime(
            emitter_,
            0.45f,
            0.85f
        );


        // ----------------------------------------------------
        // Spawn shape
        // ----------------------------------------------------

        HRL_SetVFXEmitterSpawnShape(
            emitter_,
            HRL_VFX_SHAPE_BOX
        );

        HRL_SetVFXEmitterShapeSize(
            emitter_,
            0.18f,
            0.18f,
            0.18f
        );


        // ----------------------------------------------------
        // Gravity
        // ----------------------------------------------------

        HRL_SetVFXGravity(
            emitter_,
            0.0f,
            -5.0f,
            0.0f
        );


        // ----------------------------------------------------
        // Rotation
        // ----------------------------------------------------

        HRL_SetVFXEmitterInitialRotation(
            emitter_,

            0.0f,
            0.0f,
            0.0f,

            0.0f,
            0.0f,
            360.0f
        );

        HRL_SetVFXEmitterAngularVelocity(
            emitter_,

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
            emitter_,
            HRL_VFX_RENDER_BILLBOARD
        );

        HRL_SetVFXEmitterBlendMode(
            emitter_,
            HRL_VFX_BLEND_ALPHA
        );

        HRL_SetVFXEmitterParticleSize(
            emitter_,
            0.18f,
            0.18f
        );

        HRL_SetVFXEmitterSimulationSpace(
            emitter_,
            HRL_VFX_SIMULATION_WORLD
        );


        // ----------------------------------------------------
        // Texture
        // ----------------------------------------------------

        HRL_id texture =
            lynx::RessourceTex("particle-texture.png");

        HRL_SetVFXEmitterTexture(
            emitter_,
            texture
        );


        // ----------------------------------------------------
        // Blood color
        // ----------------------------------------------------

        HRL_id color_curve =
            HRL_CreateVFXColorCurve(emitter_);

        if (color_curve == HRL_INVALID_ID)
            return;

        HRL_AddVFXColorKey(
            color_curve,
            0.0f,
            1.f,
            .95f,
            .95f,
            1.0f
        );

        HRL_AddVFXColorKey(
            color_curve,
            1.0f,
            1.f,
            1.f,
            1.f,
            0.5
        );

        HRL_SetVFXEmitterColorCurve(
            emitter_,
            color_curve
        );
    }


private:

    HRL_id system_ = HRL_INVALID_ID;
    HRL_id emitter_ = HRL_INVALID_ID;

    bool initialized_ = false;
};