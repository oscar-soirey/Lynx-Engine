#include "EditorOptions.h"
#include "EditorIcons.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <string>
#include <system_error>

namespace lynx::editor::options
{
	namespace
	{
		Options g_options;
		bool g_dirty = false;       // style to rebuild before the next frame
		float g_scale_edit = 1.f;   // slider value (applied when released)
		int g_page = 0;             // 0 Interface, 1 Project, 2 Plugins
		std::function<void()> g_project_page;

		constexpr float kMinScale = 0.5f;
		constexpr float kMaxScale = 2.f;

		std::filesystem::path OptionsFile()
		{
			std::filesystem::path dir;
#ifdef _WIN32
			if (const char* appdata = std::getenv("APPDATA"))
				dir = std::filesystem::path(appdata) / "Lynx";
#else
			if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
				dir = std::filesystem::path(xdg) / "lynx";
			else if (const char* home = std::getenv("HOME"))
				dir = std::filesystem::path(home) / ".config" / "lynx";
#endif
			if (dir.empty())
				dir = std::filesystem::current_path();
			return dir / "editor_options.cfg";
		}

		void Save()
		{
			const std::filesystem::path path = OptionsFile();
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::trunc);
			if (!file)
			{
				std::cerr << "[OPTIONS] Could not write " << path.string() << "\n";
				return;
			}
			file << "version 1\n";
			file << "ui_scale " << g_options.ui_scale << "\n";
			file << "theme " << static_cast<int>(g_options.theme) << "\n";
			file << "rounded " << (g_options.rounded ? 1 : 0) << "\n";
		}

		void Load()
		{
			std::ifstream file(OptionsFile());
			if (!file)
				return;
			std::string key;
			while (file >> key)
			{
				if (key == "ui_scale")
				{
					float v = 1.f;
					if (file >> v && std::isfinite(v))
						g_options.ui_scale = std::clamp(v, kMinScale, kMaxScale);
				}
				else if (key == "theme")
				{
					int v = 0;
					if (file >> v && v >= 0 && v < static_cast<int>(Theme::Count))
						g_options.theme = static_cast<Theme>(v);
				}
				else if (key == "rounded")
				{
					int v = 0;
					if (file >> v)
						g_options.rounded = v != 0;
				}
				else
				{
					std::string ignored;   // newer keys : skipped
					file >> ignored;
				}
			}
		}

		// Warm dark theme : brown greys, orange accents (the Paint tiles).
		void StyleColorsLynx(ImGuiStyle& s)
		{
			ImGui::StyleColorsDark(&s);
			ImVec4* c = s.Colors;
			const ImVec4 accent(0.94f, 0.63f, 0.16f, 1.f);
			c[ImGuiCol_Text]                 = ImVec4(0.93f, 0.90f, 0.85f, 1.f);
			c[ImGuiCol_TextDisabled]         = ImVec4(0.58f, 0.54f, 0.49f, 1.f);
			c[ImGuiCol_WindowBg]             = ImVec4(0.13f, 0.12f, 0.11f, 1.f);
			c[ImGuiCol_ChildBg]              = ImVec4(0.11f, 0.10f, 0.09f, 1.f);
			c[ImGuiCol_PopupBg]              = ImVec4(0.15f, 0.14f, 0.12f, 0.98f);
			c[ImGuiCol_Border]               = ImVec4(0.30f, 0.27f, 0.23f, 1.f);
			c[ImGuiCol_FrameBg]              = ImVec4(0.21f, 0.19f, 0.17f, 1.f);
			c[ImGuiCol_FrameBgHovered]       = ImVec4(0.28f, 0.25f, 0.21f, 1.f);
			c[ImGuiCol_FrameBgActive]        = ImVec4(0.34f, 0.29f, 0.22f, 1.f);
			c[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.09f, 0.08f, 1.f);
			c[ImGuiCol_TitleBgActive]        = ImVec4(0.18f, 0.15f, 0.12f, 1.f);
			c[ImGuiCol_MenuBarBg]            = ImVec4(0.15f, 0.14f, 0.12f, 1.f);
			c[ImGuiCol_CheckMark]            = accent;
			c[ImGuiCol_SliderGrab]           = ImVec4(0.80f, 0.53f, 0.14f, 1.f);
			c[ImGuiCol_SliderGrabActive]     = accent;
			c[ImGuiCol_Button]               = ImVec4(0.26f, 0.23f, 0.19f, 1.f);
			c[ImGuiCol_ButtonHovered]        = ImVec4(0.38f, 0.31f, 0.22f, 1.f);
			c[ImGuiCol_ButtonActive]         = ImVec4(0.55f, 0.38f, 0.15f, 1.f);
			c[ImGuiCol_Header]               = ImVec4(0.32f, 0.26f, 0.18f, 1.f);
			c[ImGuiCol_HeaderHovered]        = ImVec4(0.42f, 0.32f, 0.20f, 1.f);
			c[ImGuiCol_HeaderActive]         = ImVec4(0.55f, 0.38f, 0.15f, 1.f);
			c[ImGuiCol_Separator]            = c[ImGuiCol_Border];
			c[ImGuiCol_SeparatorHovered]     = ImVec4(0.70f, 0.47f, 0.15f, 1.f);
			c[ImGuiCol_SeparatorActive]      = accent;
			c[ImGuiCol_ResizeGrip]           = ImVec4(0.55f, 0.38f, 0.15f, 0.25f);
			c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.70f, 0.47f, 0.15f, 0.65f);
			c[ImGuiCol_ResizeGripActive]     = accent;
			c[ImGuiCol_Tab]                  = ImVec4(0.19f, 0.17f, 0.14f, 1.f);
			c[ImGuiCol_TabHovered]           = ImVec4(0.42f, 0.32f, 0.20f, 1.f);
			c[ImGuiCol_TabSelected]          = ImVec4(0.33f, 0.26f, 0.17f, 1.f);
			c[ImGuiCol_TabSelectedOverline]  = accent;
			c[ImGuiCol_TabDimmed]            = ImVec4(0.14f, 0.13f, 0.11f, 1.f);
			c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.24f, 0.20f, 0.15f, 1.f);
			c[ImGuiCol_DockingPreview]       = ImVec4(0.94f, 0.63f, 0.16f, 0.5f);
			c[ImGuiCol_TextSelectedBg]       = ImVec4(0.94f, 0.63f, 0.16f, 0.35f);
			c[ImGuiCol_NavCursor]            = accent;
			c[ImGuiCol_PlotHistogram]        = accent;
		}

		void ApplyStyle()
		{
			ImGuiStyle s;   // default sizes
			switch (g_options.theme)
			{
			case Theme::Light:   ImGui::StyleColorsLight(&s); break;
			case Theme::Classic: ImGui::StyleColorsClassic(&s); break;
			case Theme::Lynx:    StyleColorsLynx(s); break;
			default:             ImGui::StyleColorsDark(&s); break;
			}
			if (g_options.rounded)
			{
				s.WindowRounding = 6.f;
				s.ChildRounding = 4.f;
				s.FrameRounding = 4.f;
				s.PopupRounding = 4.f;
				s.GrabRounding = 3.f;
				s.TabRounding = 4.f;
				s.ScrollbarRounding = 6.f;
			}
			s.ScaleAllSizes(g_options.ui_scale);
			s.FontScaleMain = g_options.ui_scale;
			ImGui::GetStyle() = s;
		}

		void Changed()
		{
			g_dirty = true;
			Save();
		}

		// A theme card : a small preview of its colors, the name under it.
		bool ThemeCard(Theme theme, bool selected)
		{
			ImGuiStyle s;
			switch (theme)
			{
			case Theme::Light:   ImGui::StyleColorsLight(&s); break;
			case Theme::Classic: ImGui::StyleColorsClassic(&s); break;
			case Theme::Lynx:    StyleColorsLynx(s); break;
			default:             ImGui::StyleColorsDark(&s); break;
			}
			const float scale = ImGui::GetFontSize() / 24.f;
			const ImVec2 size(150.f * scale, 96.f * scale);
			ImGui::PushID(static_cast<int>(theme));
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::InvisibleButton("##theme", size);
			const bool hovered = ImGui::IsItemHovered();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 p1(p0.x + size.x, p0.y + size.y);
			const float r = 4.f * scale;
			const float pad = 6.f * scale;
			auto col = [&](ImGuiCol c) { return ImGui::ColorConvertFloat4ToU32(s.Colors[c]); };

			// Window : title bar, a frame, a button, a checkbox mark.
			const float preview_h = size.y - ImGui::GetTextLineHeight() - pad;
			const ImVec2 w0(p0.x + pad, p0.y + pad);
			const ImVec2 w1(p1.x - pad, p0.y + preview_h);
			dl->AddRectFilled(w0, w1, col(ImGuiCol_WindowBg), r);
			dl->AddRectFilled(w0, ImVec2(w1.x, w0.y + 10.f * scale), col(ImGuiCol_TitleBgActive), r, ImDrawFlags_RoundCornersTop);
			const float u = (w1.x - w0.x);
			dl->AddRectFilled(ImVec2(w0.x + 6 * scale, w0.y + 16 * scale), ImVec2(w0.x + u * 0.7f, w0.y + 26 * scale), col(ImGuiCol_FrameBg), 2.f * scale);
			dl->AddRectFilled(ImVec2(w0.x + u * 0.7f - u * 0.5f + 6 * scale, w0.y + 18 * scale),
			                  ImVec2(w0.x + u * 0.42f, w0.y + 24 * scale), col(ImGuiCol_SliderGrab), 1.f * scale);
			dl->AddRectFilled(ImVec2(w0.x + 6 * scale, w0.y + 32 * scale), ImVec2(w0.x + u * 0.45f, w0.y + 44 * scale), col(ImGuiCol_Button), 2.f * scale);
			dl->AddRectFilled(ImVec2(w0.x + u * 0.52f, w0.y + 32 * scale), ImVec2(w0.x + u * 0.52f + 12 * scale, w0.y + 44 * scale), col(ImGuiCol_FrameBg), 2.f * scale);
			dl->AddRectFilled(ImVec2(w0.x + u * 0.52f + 3 * scale, w0.y + 35 * scale), ImVec2(w0.x + u * 0.52f + 9 * scale, w0.y + 41 * scale), col(ImGuiCol_CheckMark));
			dl->AddRectFilled(ImVec2(w0.x + u * 0.52f + 18 * scale, w0.y + 36 * scale), ImVec2(w1.x - 6 * scale, w0.y + 40 * scale), col(ImGuiCol_Text), 1.f);
			dl->AddRect(w0, w1, col(ImGuiCol_Border), r);

			// Name.
			const char* name = ThemeName(theme);
			const ImVec2 ts = ImGui::CalcTextSize(name);
			dl->AddText(ImVec2(p0.x + (size.x - ts.x) * 0.5f, p1.y - ImGui::GetTextLineHeight() - 2.f * scale),
			            ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled), name);

			// Frame of the card.
			const ImU32 frame = selected ? IM_COL32(240, 160, 40, 255)
			                    : ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border);
			dl->AddRect(p0, p1, frame, r, 0, selected ? 3.f * scale : 1.f * scale);
			ImGui::PopID();
			return clicked;
		}

		void PageInterface()
		{
			ImGui::SeparatorText("UI scale");
			ImGui::TextDisabled("Size of the text and of every widget of the editor.");

			ImGui::SetNextItemWidth(260.f * ImGui::GetFontSize() / 24.f);
			int percent = static_cast<int>(std::lround(g_scale_edit * 100.f));
			if (ImGui::SliderInt("##UiScale", &percent, static_cast<int>(kMinScale * 100), static_cast<int>(kMaxScale * 100), "%d %%"))
				g_scale_edit = std::clamp(percent / 100.f, kMinScale, kMaxScale);
			// Applied when released : the slider would run away under the mouse.
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				g_options.ui_scale = g_scale_edit;
				Changed();
			}

			const int presets[] = { 75, 100, 125, 150, 200 };
			for (int p : presets)
			{
				ImGui::SameLine();
				const bool current = std::lround(g_options.ui_scale * 100.f) == p;
				if (current)
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				const std::string label = std::to_string(p) + "%";
				if (ImGui::Button(label.c_str()))
				{
					g_options.ui_scale = g_scale_edit = p / 100.f;
					Changed();
				}
				if (current)
					ImGui::PopStyleColor();
			}

			ImGui::SeparatorText("Theme");
			for (int i = 0; i < static_cast<int>(Theme::Count); ++i)
			{
				if (i > 0)
				{
					const float next = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + 150.f * ImGui::GetFontSize() / 24.f;
					if (next < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
						ImGui::SameLine();
				}
				const Theme t = static_cast<Theme>(i);
				if (ThemeCard(t, g_options.theme == t) && g_options.theme != t)
				{
					g_options.theme = t;
					Changed();
				}
			}

			if (ImGui::Checkbox("Rounded corners", &g_options.rounded))
				Changed();

			ImGui::Spacing();
			if (ImGui::Button("Reset interface"))
			{
				g_options = Options{};
				g_scale_edit = g_options.ui_scale;
				Changed();
			}
		}

		void PagePlugins()
		{
			ImGui::SeparatorText("Plugins");
			ImGui::TextWrapped("Editor plugins will be listed here : enable / disable them, see their version and their author.");
			ImGui::Spacing();
			ImGui::BeginDisabled();
			ImGui::Button("Open the plugins folder");
			ImGui::SameLine();
			ImGui::Button("Reload plugins");
			ImGui::EndDisabled();
			ImGui::Spacing();
			ImGui::TextDisabled("Coming later.");
		}
	}

	const char* ThemeName(Theme theme)
	{
		switch (theme)
		{
		case Theme::Light:   return "Light";
		case Theme::Classic: return "Classic";
		case Theme::Lynx:    return "Lynx";
		default:             return "Pixel";
		}
	}

	void Init()
	{
		Load();
		g_scale_edit = g_options.ui_scale;
		ApplyStyle();
		g_dirty = false;
	}

	void ApplyPending()
	{
		if (!g_dirty)
			return;
		g_dirty = false;
		ApplyStyle();
	}

	void SetProjectPage(const std::function<void()>& draw)
	{
		g_project_page = draw;
	}

	const Options& Get()
	{
		return g_options;
	}

	void Draw(bool* open)
	{
		if (open && !*open)
			return;

		const float k = ImGui::GetFontSize() / 24.f;
		ImGui::SetNextWindowSize(ImVec2(820.f * k, 520.f * k), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Options", open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		// Categories on the left.
		ImGui::BeginChild("##OptionsPages", ImVec2(190.f * k, 0.f), ImGuiChildFlags_Borders);
		struct Page { const char* name; icons::Icon icon; };
		const Page pages[] = {
			{ "Interface", icons::Icon::Settings },
			{ "Project",   icons::Icon::Project },
			{ "Plugins",   icons::Icon::Commands },
		};
		for (int i = 0; i < static_cast<int>(std::size(pages)); ++i)
		{
			ImGui::PushID(i);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			if (ImGui::Selectable("##page", g_page == i, 0, ImVec2(0.f, ImGui::GetFrameHeight())))
				g_page = i;
			const float y = pos.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
			icons::DrawAt(ImGui::GetWindowDrawList(), pages[i].icon, ImVec2(pos.x + 4.f * k, y), ImGui::GetTextLineHeight(), ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + ImGui::GetTextLineHeight() + 12.f * k, y), ImGui::GetColorU32(ImGuiCol_Text), pages[i].name);
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##OptionsPage", ImVec2(0.f, 0.f));
		if (g_page == 0)
			PageInterface();
		else if (g_page == 1)
		{
			ImGui::TextDisabled("Settings of this project (settings.cfg).");
			if (g_project_page)
				g_project_page();
			else
				ImGui::TextDisabled("No project open.");
		}
		else
			PagePlugins();
		ImGui::EndChild();

		ImGui::End();
	}
}
