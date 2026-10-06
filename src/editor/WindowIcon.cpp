#include "WindowIcon.h"

#include "../host/GameProject.h"

#include <glfw/glfw3.h>

// Implementation compiled in EditorMain.cpp (STB_IMAGE_IMPLEMENTATION).
#include "../../third-party/stb/stb_image.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

namespace lynx::editor::window_icon
{
	namespace
	{
		int g_width = 0;
		int g_height = 0;
		std::vector<unsigned char> g_pixels;   // RGBA
		bool g_tried_file = false;
	}


	bool SetImage(const std::vector<std::uint8_t>& bytes)
	{
		if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
			return false;
		int w = 0, h = 0;
		stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, nullptr, 4);
		if (!pixels)
			return false;
		g_width = w;
		g_height = h;
		g_pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);
		return true;
	}


	void Apply(GLFWwindow* window)
	{
		if (!window)
			return;
		// First window : icon.png next to the editor executable, if any.
		if (g_pixels.empty() && !g_tried_file)
		{
			g_tried_file = true;
			std::ifstream file(host::GetEditorDirectory() / "icon.png", std::ios::binary);
			if (file)
				SetImage(std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
		}
		if (g_pixels.empty())
			return;   // Windows : the GLFW_ICON resource of the executable
		GLFWimage image{ g_width, g_height, g_pixels.data() };
		glfwSetWindowIcon(window, 1, &image);
	}
}
