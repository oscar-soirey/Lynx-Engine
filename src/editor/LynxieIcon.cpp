#include "LynxieIcon.h"

#include "../core/Filesystem.h"
#include "../host/GameProject.h"

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace lynx::editor::lynxie_icon
{
	namespace
	{
#include "resources/lynxie_png.inl"

		// Size of the embedded image (32 x 28 pixel art, x8).
		constexpr float kDefaultRatio = 32.f / 28.f;

		bool g_tried = false;
		HRL_id g_texture = HRL_INVALID_ID;
		float g_ratio = kDefaultRatio;

		// Reads the width / height of a PNG (IHDR chunk) : the ratio of a
		// replacement lynxie.png.
		float PngRatio(const std::vector<char>& data)
		{
			if (data.size() < 24)
				return kDefaultRatio;
			auto be32 = [&](size_t at)
			{
				return (static_cast<uint32_t>(static_cast<unsigned char>(data[at])) << 24) |
				       (static_cast<uint32_t>(static_cast<unsigned char>(data[at + 1])) << 16) |
				       (static_cast<uint32_t>(static_cast<unsigned char>(data[at + 2])) << 8) |
				       static_cast<uint32_t>(static_cast<unsigned char>(data[at + 3]));
			};
			const uint32_t width = be32(16);
			const uint32_t height = be32(20);
			return (width > 0 && height > 0) ? static_cast<float>(width) / static_cast<float>(height) : kDefaultRatio;
		}

		void Create()
		{
			g_tried = true;

			// A lynxie.png next to the editor replaces the embedded one.
			std::vector<char> data;
			const std::filesystem::path override_file = host::GetEditorDirectory() / "lynxie.png";
			std::error_code ec;
			if (std::filesystem::is_regular_file(override_file, ec))
			{
				std::ifstream file(override_file, std::ios::binary);
				data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			}

			if (data.empty())
				data.assign(reinterpret_cast<const char*>(kLynxiePng),
				            reinterpret_cast<const char*>(kLynxiePng) + sizeof(kLynxiePng));

			g_ratio = PngRatio(data);
			g_texture = HRL_CreateTexture(data.data(), data.size());

			if (g_texture == HRL_INVALID_ID)
				std::cerr << "[Lynxie] Could not create the icon texture\n";
		}

		ImTextureID TextureId()
		{
			if (!g_tried)
				Create();
			if (g_texture == HRL_INVALID_ID)
				return ImTextureID_Invalid;
			const unsigned int gl = HRL_GL_GetTextureGL_ID(g_texture);   // 0 while still uploading
			return gl != 0 ? static_cast<ImTextureID>(static_cast<intptr_t>(gl)) : ImTextureID_Invalid;
		}

		// HRL textures are stored bottom-up (same UVs as the actor previews).
		const ImVec2 kUv0(0.f, 1.f);
		const ImVec2 kUv1(1.f, 0.f);
	}


	bool Ready()
	{
		return TextureId() != ImTextureID_Invalid;
	}


	float WidthFor(float height)
	{
		return height * g_ratio;
	}


	void Draw(float height, ImVec4 tint)
	{
		const ImTextureID texture = TextureId();
		const ImVec2 size(WidthFor(height), height);

		if (texture == ImTextureID_Invalid)
		{
			ImGui::Dummy(size);   // same layout while the texture loads
			return;
		}

		if (tint.w <= 0.f)
			tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);

		ImGui::ImageWithBg(ImTextureRef(texture), size, kUv0, kUv1, ImVec4(0.f, 0.f, 0.f, 0.f), tint);
	}


	bool Button(const char* id, float height)
	{
		const ImTextureID texture = TextureId();
		if (texture == ImTextureID_Invalid)
			return ImGui::Button(id);

		return ImGui::ImageButton(id, ImTextureRef(texture), ImVec2(WidthFor(height), height), kUv0, kUv1,
		                          ImVec4(0.f, 0.f, 0.f, 0.f), ImGui::GetStyleColorVec4(ImGuiCol_Text));
	}


	void DrawAt(ImDrawList* draw_list, const ImVec2& min, float height, ImU32 tint)
	{
		const ImTextureID texture = TextureId();
		if (texture == ImTextureID_Invalid || !draw_list)
			return;
		draw_list->AddImage(ImTextureRef(texture), min, ImVec2(min.x + WidthFor(height), min.y + height), kUv0, kUv1, tint);
	}
}
