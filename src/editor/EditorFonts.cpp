#include "EditorFonts.h"

#include "../host/GameProject.h"

#include <imgui/imgui_internal.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace lynx::editor::fonts
{
	namespace
	{
		ImFont* g_editor = nullptr;
		ImFont* g_lynxie = nullptr;
		ImFont* g_lynxie_bold = nullptr;
		ImFont* g_lynxie_italic = nullptr;
		ImFont* g_lynxie_bold_italic = nullptr;
		int g_lynxie_pushes = 0;   // PushLynxie calls that pushed a font
		ImFont* g_code = nullptr;
		ImFont* g_code_bold = nullptr;
		ImFont* g_code_italic = nullptr;
		ImFont* g_code_bold_italic = nullptr;

		// JetBrains Mono at the size of the editor font looks too big next to
		// the pixel font : a bit smaller.
		constexpr float kCodeScale = 0.78f;

		// Lynxie uses the editor font at its normal size (OpenDyslexic dropped).
		constexpr float kLynxieScale = 1.f;

		// fonts/<name> next to the editor, then in the current folder.
		fs::path Find(const char* name)
		{
			const fs::path candidates[] = {
				host::GetEditorDirectory() / "fonts" / name,
				fs::path("fonts") / name,
				host::GetEditorDirectory() / name,
				fs::path(name),
			};
			for (const fs::path& path : candidates)
			{
				std::error_code ec;
				if (fs::is_regular_file(path, ec))
					return path;
			}
			return fs::path();
		}

		// Characters missing from a font (accents, arrows...) : taken from
		// this one (merged into it).
		fs::path FallbackFont(bool monospace)
		{
			fs::path path = Find("normal-font.ttf");
			if (!path.empty())
				return path;
#ifdef _WIN32
			const char* windir = std::getenv("WINDIR");
			const fs::path fonts_dir = fs::path(windir ? windir : "C:\\Windows") / "Fonts";
			const char* names[] = { monospace ? "consola.ttf" : "segoeui.ttf", "arial.ttf" };
			for (const char* name : names)
			{
				std::error_code ec;
				if (fs::is_regular_file(fonts_dir / name, ec))
					return fonts_dir / name;
			}
#else
			(void)monospace;
#endif
			return fs::path();
		}

		ImFont* AddFont(const fs::path& path, float size, bool monospace)
		{
			if (path.empty())
				return nullptr;

			ImGuiIO& io = ImGui::GetIO();
			ImFont* font = io.Fonts->AddFontFromFileTTF(path.string().c_str(), size);
			if (!font)
				return nullptr;

			const fs::path fallback = FallbackFont(monospace);
			std::error_code ec;
			if (!fallback.empty() && !fs::equivalent(fallback, path, ec))
			{
				ImFontConfig merge;
				merge.MergeMode = true;
				merge.DstFont = font;
				io.Fonts->AddFontFromFileTTF(fallback.string().c_str(), size, &merge);
			}
			return font;
		}
	}

	void Load(float size)
	{
		ImGuiIO& io = ImGui::GetIO();

		// The editor font is the first one added : ImGui's default font.
		g_editor = AddFont(Find("VCR-OSD-MONO.ttf"), size, true);
		if (!g_editor)
			g_editor = AddFont(Find("normal-font.ttf"), size, false);
		if (!g_editor)
			g_editor = io.Fonts->AddFontDefault();

		// Lynxie : no font of its own any more (OpenDyslexic dropped). Her
		// messages use the editor font at the normal size ; PushLynxie does
		// nothing and Markdown draws bold / italic itself.
		g_lynxie = g_lynxie_bold = g_lynxie_italic = g_lynxie_bold_italic = nullptr;

		// Code editors : JetBrains Mono (the 4 styles have the same width).
		const float code_size = std::floor(size * kCodeScale);
		g_code = AddFont(Find("JetBrainsMono-Regular.ttf"), code_size, true);
		g_code_bold = AddFont(Find("JetBrainsMono-Bold.ttf"), code_size, true);
		g_code_italic = AddFont(Find("JetBrainsMono-Italic.ttf"), code_size, true);
		g_code_bold_italic = AddFont(Find("JetBrainsMono-BoldItalic.ttf"), code_size, true);
		if (!g_code)
			g_code_bold = g_code_italic = g_code_bold_italic = nullptr;

		io.FontDefault = g_editor;
	}

	ImFont* Editor() { return g_editor ? g_editor : ImGui::GetFont(); }
	ImFont* Lynxie() { return g_lynxie; }
	ImFont* LynxieBold() { return g_lynxie_bold; }
	ImFont* LynxieItalic() { return g_lynxie_italic; }
	ImFont* LynxieBoldItalic() { return g_lynxie_bold_italic; }

	float LynxieScale() { return kLynxieScale; }

	ImFont* Code() { return g_code ? g_code : Editor(); }
	ImFont* CodeBold() { return g_code_bold; }
	ImFont* CodeItalic() { return g_code_italic; }
	ImFont* CodeBoldItalic() { return g_code_bold_italic; }
	float CodeScale() { return g_code ? kCodeScale : 1.f; }

	void PushLynxie()
	{
		if (!g_lynxie)
			return;
		ImGui::PushFont(g_lynxie, ImGui::GetCurrentContext()->FontSizeBase * kLynxieScale);
		++g_lynxie_pushes;
	}

	void PopLynxie()
	{
		if (g_lynxie_pushes <= 0)
			return;
		ImGui::PopFont();
		--g_lynxie_pushes;
	}
}
