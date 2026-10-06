#include "FileIcons.h"

#include "../host/GameProject.h"

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace lynx::editor::file_icons
{
	namespace
	{
#include "resources/file_icons_png.inl"

		constexpr int kColumns = 8;
		constexpr int kRows = (static_cast<int>(Kind::Count) + kColumns - 1) / kColumns;
		constexpr float kCellPixels = 128.f;   // 32 px x4 in the atlas

		bool g_tried = false;
		HRL_id g_texture = HRL_INVALID_ID;

		void Create()
		{
			g_tried = true;
			std::vector<char> data;
			const std::filesystem::path override_file = host::GetEditorDirectory() / "file_icons.png";
			std::error_code ec;
			if (std::filesystem::is_regular_file(override_file, ec))
			{
				std::ifstream file(override_file, std::ios::binary);
				data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			}
			if (data.empty())
				data.assign(reinterpret_cast<const char*>(kFileIconsPng),
				            reinterpret_cast<const char*>(kFileIconsPng) + sizeof(kFileIconsPng));
			g_texture = HRL_CreateTexture(data.data(), data.size());
			if (g_texture == HRL_INVALID_ID)
				std::cerr << "[Icons] Could not create the file icon texture\n";
		}

		ImTextureID TextureId()
		{
			if (!g_tried)
				Create();
			if (g_texture == HRL_INVALID_ID)
				return ImTextureID_Invalid;
			const unsigned int gl = HRL_GL_GetTextureGL_ID(g_texture);   // 0 while uploading
			return gl != 0 ? static_cast<ImTextureID>(static_cast<intptr_t>(gl)) : ImTextureID_Invalid;
		}

		// HRL textures are stored bottom-up (v flipped). Half-texel inset : no
		// bleeding from the neighbour with linear filtering.
		void Uv(Kind kind, ImVec2& uv0, ImVec2& uv1)
		{
			const int index = std::clamp(static_cast<int>(kind), 0, static_cast<int>(Kind::Count) - 1);
			const float cw = 1.f / kColumns;
			const float ch = 1.f / kRows;
			const float inset_u = 0.5f / (kColumns * kCellPixels);
			const float inset_v = 0.5f / (kRows * kCellPixels);
			const float top = (index / kColumns) * ch + inset_v;
			const float bottom = (index / kColumns + 1) * ch - inset_v;
			uv0 = ImVec2((index % kColumns) * cw + inset_u, 1.f - top);
			uv1 = ImVec2((index % kColumns + 1) * cw - inset_u, 1.f - bottom);
		}

		bool Is(const std::string& extension, std::initializer_list<const char*> list)
		{
			for (const char* e : list)
				if (extension == e)
					return true;
			return false;
		}
	}


	Kind ForFile(const std::filesystem::path& path, bool is_directory)
	{
		if (is_directory)
			return Kind::Folder;

		std::string name = path.filename().string();
		std::string extension = path.extension().string();
		auto lower = [](std::string& s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		};
		lower(name);
		lower(extension);

		if (name == "voxels.json")
			return Kind::Voxels;
		if (extension == ".js" || extension == ".mjs" || extension == ".ts")
			return Kind::Script;
		if (extension == ".py")
			return Kind::Python;
		if (Is(extension, { ".cpp", ".cc", ".c", ".h", ".hpp", ".inl" }))
			return Kind::Cpp;
		if (extension == ".json")
			return Kind::Data;
		if (extension == ".xml" || extension == ".hrlv")
			return Kind::Level;
		if (Is(extension, { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr" }))
			return Kind::Image;
		if (Is(extension, { ".wav", ".mp3", ".flac", ".ogg" }))
			return Kind::Sound;
		if (Is(extension, { ".ttf", ".otf", ".fnt" }))
			return Kind::Font;
		if (extension == ".widget")
			return Kind::Widget;
		if (extension == ".animgraph")
			return Kind::AnimGraph;
		if (extension == ".bt")
			return Kind::BehaviorTree;
		if (Is(extension, { ".ini", ".cfg", ".toml", ".yaml", ".yml" }))
			return Kind::Config;
		if (Is(extension, { ".glsl", ".hlsl", ".vert", ".frag", ".lua", ".cs", ".sh", ".bat" }))
			return Kind::Code;
		if (Is(extension, { ".html", ".htm", ".svg" }))
			return Kind::Markup;
		if (Is(extension, { ".txt", ".md", ".log" }))
			return Kind::Text;
		return Kind::File;
	}

	const char* Describe(Kind kind)
	{
		switch (kind)
		{
		case Kind::Folder: return "Folder";
		case Kind::Text: return "Text";
		case Kind::Script: return "JavaScript";
		case Kind::Python: return "Python";
		case Kind::Cpp: return "C / C++";
		case Kind::Data: return "JSON data";
		case Kind::Level: return "Level";
		case Kind::Image: return "Image";
		case Kind::Sound: return "Sound";
		case Kind::Font: return "Font";
		case Kind::Widget: return "Widget (UI)";
		case Kind::AnimGraph: return "Anim Graph";
		case Kind::BehaviorTree: return "Behavior Tree";
		case Kind::Voxels: return "Voxel types";
		case Kind::Config: return "Settings";
		case Kind::Code: return "Code";
		case Kind::Markup: return "Markup";
		default: return "File";
		}
	}

	void DrawAt(ImDrawList* draw_list, Kind kind, const ImVec2& min, float size, ImU32 tint)
	{
		const ImTextureID texture = TextureId();
		if (texture == ImTextureID_Invalid || !draw_list)
			return;
		ImVec2 uv0, uv1;
		Uv(kind, uv0, uv1);
		draw_list->AddImage(ImTextureRef(texture), min, ImVec2(min.x + size, min.y + size), uv0, uv1, tint);
	}

	void Draw(Kind kind, float size)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(size, size));
		DrawAt(ImGui::GetWindowDrawList(), kind, pos, size);
	}
}
