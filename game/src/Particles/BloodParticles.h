#pragma once

#include <Lynx.h>
#include "hrl/hrl.h"

class BloodParticles
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
        float world_z,
        float dir_x,
        float dir_y)
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

        // Direction générale du sang.
        //
        // Exemple :
        // dir_x = 1 -> sang vers la droite
        // dir_x = -1 -> sang vers la gauche
        //
        // On ajoute une dispersion directement dans le VFX.

        const float min_speed = 2.0f;
        const float max_speed = 8.0f;
        const float spread_x = 3.0f;
        const float spread_y = 5.0f;

        float min_x;
        float max_x;

        if (dir_x > 0.0f)
        {
            min_x = min_speed;
            max_x = max_speed;
        }
        else
        {
            min_x = -max_speed;
            max_x = -min_speed;
        }

        HRL_SetVFXEmitterInitialVelocity(
            emitter_,

            // Min
            min_x - spread_x,
            dir_y * min_speed,
            -0.5f,

            // Max
            max_x + spread_x,
            dir_y * max_speed + spread_y,
            0.5f
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
            0.25f,
            0.65f
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
            0.15f,
            0.15f,
            0.15f
        );


        // ----------------------------------------------------
        // Gravity
        // ----------------------------------------------------

        HRL_SetVFXGravity(
            emitter_,
            0.0f,
            -30.0f,
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
            0.12f,
            0.12f
        );

        HRL_SetVFXEmitterSimulationSpace(
            emitter_,
            HRL_VFX_SIMULATION_WORLD
        );


        // ----------------------------------------------------
        // Texture
        // ----------------------------------------------------

        HRL_id texture =
            lynx::RessourceTex("blood_particle_texture.png");

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
            0.98f,
            0.02f,
            0.02f,
            1.0f
        );

        HRL_AddVFXColorKey(
            color_curve,
            1.0f,
            0.98f,
            0.0f,
            0.0f,
            1.0f
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