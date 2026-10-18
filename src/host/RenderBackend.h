#pragma once

// =============================================================================
// Render backend of a game (HRL) : Vulkan (default) or OpenGL 3.3.
// -----------------------------------------------------------------------------
// Chosen in the editor (Options > Project > Renderer), saved in the project's
// settings.cfg as "renderer vulkan" / "renderer opengl" (copied next to a
// shipped game by Ship Game). The command line wins over the file :
//
//   LynxRuntime.exe "C:/MyGame" --opengl
//   MyGame.exe --vulkan
//
// The runtime falls back to OpenGL when Vulkan is not available (no driver,
// no Vulkan loader). Header only : runtime and editor.
// =============================================================================

#include <cstring>
#include <string>

namespace lynx::host
{
	enum class RenderBackend
	{
		Vulkan = 0,
		OpenGL = 1,
	};

	constexpr RenderBackend kDefaultRenderBackend = RenderBackend::Vulkan;

	/** "vulkan" / "opengl" : the value written in settings.cfg. */
	inline const char* RenderBackendKey(RenderBackend backend)
	{
		return backend == RenderBackend::OpenGL ? "opengl" : "vulkan";
	}

	/** "Vulkan" / "OpenGL" : for the logs and the interface. */
	inline const char* RenderBackendLabel(RenderBackend backend)
	{
		return backend == RenderBackend::OpenGL ? "OpenGL" : "Vulkan";
	}

	/** settings.cfg value -> backend ; unknown values keep `fallback`. */
	inline RenderBackend ParseRenderBackend(const std::string& value, RenderBackend fallback = kDefaultRenderBackend)
	{
		if (value == "opengl" || value == "gl" || value == "OpenGL")
			return RenderBackend::OpenGL;
		if (value == "vulkan" || value == "vk" || value == "Vulkan")
			return RenderBackend::Vulkan;
		return fallback;
	}

	/** true when `arg` is --vulkan / --opengl (written into `backend`). */
	inline bool ParseRenderBackendArg(const char* arg, RenderBackend& backend)
	{
		if (!arg)
			return false;
		if (std::strcmp(arg, "--opengl") == 0 || std::strcmp(arg, "--gl") == 0)
		{
			backend = RenderBackend::OpenGL;
			return true;
		}
		if (std::strcmp(arg, "--vulkan") == 0 || std::strcmp(arg, "--vk") == 0)
		{
			backend = RenderBackend::Vulkan;
			return true;
		}
		return false;
	}
}
