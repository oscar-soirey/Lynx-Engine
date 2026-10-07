#pragma once

// =============================================================================
// Camera state
// -----------------------------------------------------------------------------
// HRL can set a camera (location, rotation, fov) but not read it back. These
// functions do the HRL call AND remember the values, so the engine can know
// what each viewport shows (2D lighting : Lighting2D.h).
//
//   lynx::camera_state::SetLocation(cam, x, y, z);   // instead of HRL_SetCameraLocation
//   lynx::camera_state::SetRotation(cam, p, y, r);   // instead of HRL_SetCameraRotation
//   lynx::camera_state::SetFov(cam, 20.f);           // instead of HRL_SetCameraPerspectiveFov
//   lynx::camera_state::SetViewportCamera(vp, cam);  // instead of HRL_SetViewportCamera
//
// Use them everywhere a camera is moved (engine, editor, plugins) : a camera
// moved with HRL directly is not seen by the 2D lighting.
// =============================================================================

#include <cstdint>

#include "Common.h"

namespace lynx::camera_state
{
	struct State
	{
		vec3 location{0.f, 0.f, 0.f};
		vec3 rotation{0.f, -90.f, 0.f};   // pitch, yaw, roll (degrees)
		float fov = 45.f;                 // vertical, degrees
		bool has_location = false;        // SetLocation was called at least once
	};

	LYNX_API void SetLocation(uint32_t camera, float x, float y, float z);
	LYNX_API void SetRotation(uint32_t camera, float pitch, float yaw, float roll);
	LYNX_API void SetFov(uint32_t camera, float fov);
	LYNX_API void SetViewportCamera(uint32_t viewport, uint32_t camera);

	/** false : camera never set through these functions. */
	LYNX_API bool Get(uint32_t camera, State& out);

	/** Camera shown by a viewport (HRL_INVALID_ID / 0xFFFFFFFF : unknown). */
	LYNX_API uint32_t GetViewportCamera(uint32_t viewport);

	/** Forgets everything (HRL_Shutdown : ids are reused). */
	LYNX_API void Clear();
}
