#pragma once

/**
 * 2D lighting : a post process pass that lights the scene seen from the front
 * with the Light2DComponent / Light2DActor of the level, and the shadows cast
 * by the voxels (solid voxels block the light, liquids and gases a little).
 *
 *   Ambient light (darkness of the level)  +  each 2D light :
 *     radius / falloff, color, intensity, cone (flashlight), flicker,
 *     hard or soft shadows (source_radius), optional pixel-art look
 *     (light computed per voxel, banding).
 *
 * The lighting is computed inside the post process shader (PostProcess.h), on
 * the scene color, before bloom and tone mapping.
 *
 * Settings of the project (assets/lighting2d.json, loaded by the editor AND the
 * shipped game), like the post process (PostProcess.h) :
 *
 *   lynx::lighting2d::SetFloat("enabled", 1.f);
 *   lynx::lighting2d::SetColor("ambientColor", 0.2f, 0.25f, 0.4f);
 *   lynx::lighting2d::Save();          // editor : assets/lighting2d.json
 *
 * JS : Lighting2D.set("ambientIntensity", 0.1), Lighting2D.get("enabled"),
 *      Lighting2D.reset(), Lighting2D.params().
 * Editor : window "2D Lighting" (Windows menu).
 *
 * How it works : every frame, for each player viewport, the camera of the
 * viewport (camera_state, CameraState.h) gives the world position of each
 * pixel on the voxel plane (Z = 0) ; the voxels around the view are copied in
 * a small occlusion texture (one texel per voxel) where the shader casts a ray
 * from the pixel to each light. At most kMaxLights lights per viewport (the
 * closest / brightest ones).
 */

#include <cstdint>
#include <string>
#include <vector>

#include "Common.h"
#include "PostProcess.h"   // ParamInfo / ParamType (same model of settings)

namespace lynx::lighting2d
{
	using ParamInfo = postprocess::ParamInfo;
	using ParamType = postprocess::ParamType;

	constexpr int kMaxLights = 16;

	/** Every setting, in display order. */
	LYNX_API const std::vector<ParamInfo>& GetParams();
	LYNX_API const ParamInfo* FindParam(const std::string& name);

	/**
	 * Creates the shader and puts the pass on the viewport of every player (and
	 * of the players created later), loads assets/lighting2d.json. After
	 * postprocess::Install. Once.
	 */
	LYNX_API void Install();

	/** Every frame, after the gameplay (Engine::ProgressOneFrame calls it). */
	LYNX_API void Update(float dt);

	LYNX_API bool IsEnabled();

	LYNX_API bool GetValue(const std::string& name, float out[3]);
	LYNX_API float GetFloat(const std::string& name);
	LYNX_API bool SetValue(const std::string& name, const float* values, int count);
	LYNX_API bool SetFloat(const std::string& name, float value);
	LYNX_API bool SetColor(const std::string& name, float r, float g, float b);
	LYNX_API void Reset(const std::string& name = "");
	LYNX_API bool IsModified(const std::string& name);

	/** Reads a file (relative to assets/, lynx::fs). Missing : default values. */
	LYNX_API bool Load(const std::string& path = "lighting2d.json");
	/** Editor : writes assets/<path> on the disk. */
	LYNX_API bool Save(const std::string& path = "lighting2d.json");
	LYNX_API bool IsDirty();

	/** Lights drawn last frame (all viewports), for the editor window. */
	LYNX_API int GetDrawnLightCount();

	/**
	 * GLSL inserted in the post process shader (PostProcess.cpp) : the uniforms and
	 * `vec3 ApplyLighting2D(vec3 scene)`, called on the scene color before the
	 * exposure / bloom / tone mapping. One pass : nothing can draw over it.
	 */
	LYNX_API std::string ShaderCode();

	/** "" or why the pass could not start (shader error...). */
	LYNX_API const std::string& GetError();

	/** HRL_Shutdown : forgets the HRL objects (Install again after). */
	LYNX_API void Shutdown();
}
