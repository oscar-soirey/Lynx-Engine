#include "EditorIcons.h"

#include <imgui/imgui_internal.h>   // FindRenderedTextEnd, GetItemFlags

#include "../host/GameProject.h"

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace lynx::editor::icons
{
	namespace
	{
#include "resources/editor_icons_png.inl"

		constexpr int kColumns = 8;
		constexpr int kRows = (static_cast<int>(Icon::Count) + kColumns - 1) / kColumns;

		bool g_tried = false;
		HRL_id g_texture = HRL_INVALID_ID;

		void Create()
		{
			g_tried = true;

			std::vector<char> data;
			const std::filesystem::path override_file = host::GetEditorDirectory() / "editor_icons.png";
			std::error_code ec;
			if (std::filesystem::is_regular_file(override_file, ec))
			{
				std::ifstream file(override_file, std::ios::binary);
				data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			}
			if (data.empty())
				data.assign(reinterpret_cast<const char*>(kEditorIconsPng),
				            reinterpret_cast<const char*>(kEditorIconsPng) + sizeof(kEditorIconsPng));

			g_texture = HRL_CreateTexture(data.data(), data.size());
			if (g_texture == HRL_INVALID_ID)
				std::cerr << "[Icons] Could not create the icon texture\n";
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

		// UVs of an icon. HRL textures are stored bottom-up (v flipped), like
		// the actor previews of the editor. A half-texel inset avoids bleeding
		// from the neighbour with linear filtering.
		void IconUv(Icon icon, ImVec2& uv0, ImVec2& uv1)
		{
			const int index = std::clamp(static_cast<int>(icon), 0, static_cast<int>(Icon::Count) - 1);
			const float cw = 1.f / kColumns;
			const float ch = 1.f / kRows;
			const float inset_u = 0.5f / (kColumns * 64.f);
			const float inset_v = 0.5f / (kRows * 64.f);
			const float u0 = (index % kColumns) * cw + inset_u;
			const float u1 = (index % kColumns + 1) * cw - inset_u;
			const float top = (index / kColumns) * ch + inset_v;
			const float bottom = (index / kColumns + 1) * ch - inset_v;
			uv0 = ImVec2(u0, 1.f - top);
			uv1 = ImVec2(u1, 1.f - bottom);
		}

		ImVec4 Resolve(ImVec4 tint)
		{
			return tint.w <= 0.f ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : tint;
		}

		// Pixel-perfect sizes look best : 16, 32... (the glyphs are 16 px).
		float DefaultSize()
		{
			return ImGui::GetTextLineHeight();
		}
	}


	void Draw(Icon icon, float size, ImVec4 tint)
	{
		if (size <= 0.f)
			size = DefaultSize();
		const ImTextureID texture = TextureId();
		if (texture == ImTextureID_Invalid)
		{
			ImGui::Dummy(ImVec2(size, size));
			return;
		}
		ImVec2 uv0, uv1;
		IconUv(icon, uv0, uv1);
		ImGui::ImageWithBg(ImTextureRef(texture), ImVec2(size, size), uv0, uv1, ImVec4(0.f, 0.f, 0.f, 0.f), Resolve(tint));
	}


	void DrawAt(ImDrawList* draw_list, Icon icon, const ImVec2& min, float size, ImU32 tint)
	{
		const ImTextureID texture = TextureId();
		if (texture == ImTextureID_Invalid || !draw_list)
			return;
		ImVec2 uv0, uv1;
		IconUv(icon, uv0, uv1);
		draw_list->AddImage(ImTextureRef(texture), min, ImVec2(min.x + size, min.y + size), uv0, uv1, tint);
	}


	bool Button(const char* id, Icon icon, ImVec4 tint, bool active)
	{
		if (active)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

		bool pressed = false;
		const ImTextureID texture = TextureId();
		if (texture == ImTextureID_Invalid)
		{
			pressed = ImGui::Button(id);   // the label, while the texture loads
		}
		else
		{
			ImVec2 uv0, uv1;
			IconUv(icon, uv0, uv1);
			const float size = DefaultSize();
			pressed = ImGui::ImageButton(id, ImTextureRef(texture), ImVec2(size, size), uv0, uv1,
			                             ImVec4(0.f, 0.f, 0.f, 0.f), Resolve(tint));
		}

		if (active)
			ImGui::PopStyleColor();
		return pressed;
	}


	bool ButtonWithLabel(const char* label, Icon icon, ImVec4 tint, bool active)
	{
		// A normal button with room on the left, then the icon drawn over it.
		const ImGuiStyle& style = ImGui::GetStyle();
		const float size = DefaultSize();
		const char* label_end = ImGui::FindRenderedTextEnd(label);
		const ImVec2 text_size = ImGui::CalcTextSize(label, label_end);
		const ImVec2 button_size(style.FramePadding.x * 2.f + size + style.ItemInnerSpacing.x + text_size.x,
		                         ImGui::GetFrameHeight());

		if (active)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

		ImGui::PushID(label);
		const bool pressed = ImGui::Button("##icon_button", button_size);
		ImGui::PopID();

		if (active)
			ImGui::PopStyleColor();

		const ImVec2 min = ImGui::GetItemRectMin();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const float y = min.y + (button_size.y - size) * 0.5f;
		const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
		ImVec4 color = Resolve(tint);
		if (disabled)
			color.w *= ImGui::GetStyle().DisabledAlpha;
		DrawAt(draw, icon, ImVec2(min.x + style.FramePadding.x, y), size, ImGui::ColorConvertFloat4ToU32(color));
		draw->AddText(ImVec2(min.x + style.FramePadding.x + size + style.ItemInnerSpacing.x,
		                     min.y + (button_size.y - text_size.y) * 0.5f),
		              ImGui::GetColorU32(ImGuiCol_Text), label, label_end);
		return pressed;
	}


	void Label(Icon icon, const char* text, ImVec4 tint)
	{
		Draw(icon, 0.f, tint);
		ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::TextUnformatted(text);
	}


	bool Checkbox(const char* label, Icon icon, bool* value)
	{
		const bool changed = ImGui::Checkbox(("##" + std::string(label)).c_str(), value);
		ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
		// Same baseline as the text of the line (the checkbox is a frame).
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
		Draw(icon);
		ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::TextUnformatted(label);
		// Clicking the text toggles too, like a normal checkbox.
		if (ImGui::IsItemClicked())
		{
			*value = !*value;
			return true;
		}
		return changed;
	}


	Icon ForFile(const char* path, bool is_directory)
	{
		if (is_directory)
			return Icon::Folder;
		std::string extension = std::filesystem::path(path ? path : "").extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		for (const char* e : { ".js", ".py", ".cpp", ".h", ".hpp", ".json", ".xml", ".glsl", ".txt", ".md", ".ini", ".cfg" })
			if (extension == e)
				return (extension == ".txt" || extension == ".md") ? Icon::File : Icon::FileCode;
		for (const char* e : { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd" })
			if (extension == e)
				return Icon::FileImage;
		for (const char* e : { ".wav", ".mp3", ".flac", ".ogg" })
			if (extension == e)
				return Icon::FileSound;
		return Icon::File;
	}
}
