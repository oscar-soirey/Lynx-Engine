#include "CameraState.h"

#include <hrl/hrl.h>

#include <unordered_map>

namespace lynx::camera_state
{
	namespace
	{
		std::unordered_map<uint32_t, State>& Cameras()
		{
			static std::unordered_map<uint32_t, State> cameras;
			return cameras;
		}

		std::unordered_map<uint32_t, uint32_t>& Viewports()
		{
			static std::unordered_map<uint32_t, uint32_t> viewports;
			return viewports;
		}
	}

	namespace
	{
		vec3 g_last_location{0.f};
		bool g_has_last = false;
	}

	bool GetLastLocation(vec3& out)
	{
		if (!g_has_last)
			return false;
		out = g_last_location;
		return true;
	}

	void SetLocation(uint32_t camera, float x, float y, float z)
	{
		HRL_SetCameraLocation(camera, x, y, z);
		g_last_location = vec3(x, y, z);
		g_has_last = true;
		State& s = Cameras()[camera];
		s.location = vec3(x, y, z);
		s.has_location = true;
	}

	void SetRotation(uint32_t camera, float pitch, float yaw, float roll)
	{
		HRL_SetCameraRotation(camera, pitch, yaw, roll);
		Cameras()[camera].rotation = vec3(pitch, yaw, roll);
	}

	void SetFov(uint32_t camera, float fov)
	{
		HRL_SetCameraPerspectiveFov(camera, fov);
		Cameras()[camera].fov = fov;
	}

	void SetViewportCamera(uint32_t viewport, uint32_t camera)
	{
		HRL_SetViewportCamera(viewport, camera);
		Viewports()[viewport] = camera;
	}

	bool Get(uint32_t camera, State& out)
	{
		const auto it = Cameras().find(camera);
		if (it == Cameras().end())
			return false;
		out = it->second;
		return true;
	}

	uint32_t GetViewportCamera(uint32_t viewport)
	{
		const auto it = Viewports().find(viewport);
		return it != Viewports().end() ? it->second : 0xFFFFFFFFu;
	}

	void Clear()
	{
		Cameras().clear();
		Viewports().clear();
	}
}
