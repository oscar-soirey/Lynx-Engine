#include "EditorOptions.h"

#include "raven/RavenUi.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace lynx::editor::options
{
	namespace
	{
		Options g_options;
		Rid g_panel = R_INVALID_ID;
		Rid g_scale = R_INVALID_ID;
		Rid g_scale_value = R_INVALID_ID;
		Rid g_theme = R_INVALID_ID;
		Rid g_rounded = R_INVALID_ID;

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
			file << "version 2\n";
			file << "ui_scale " << g_options.ui_scale << "\n";
			file << "theme_name " << g_options.theme << "\n";
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
					// Version 1 (ImGui editor) : Pixel, Light, Classic, Lynx.
					int v = 0;
					if (file >> v)
					{
						const char* names[] = { "Lynx Pixel", "Light", "Lynx Pixel Dark", "Lynx Warm" };
						if (v >= 0 && v < 4)
							g_options.theme = names[v];
					}
				}
				else if (key == "theme_name")
				{
					std::string rest;
					std::getline(file, rest);
					const size_t first = rest.find_first_not_of(" \t");
					if (first != std::string::npos)
					{
						rest = rest.substr(first);
						while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' '))
							rest.pop_back();
						g_options.theme = rest;
					}
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

		bool ThemeExists(const std::string& name)
		{
			for (const ui::ThemeInfo& theme : ui::Themes())
				if (theme.name == name)
					return true;
			return false;
		}

		void Apply()
		{
			if (!ThemeExists(g_options.theme))
				g_options.theme = ui::Themes().empty() ? std::string("Dark") : ui::Themes().front().name;
			ui::UseTheme(g_options.theme);
			if (g_options.rounded)
			{
				Rtheme_set_metric(R_METRIC_CORNER_RADIUS, 5.f);
				Rtheme_set_metric(R_METRIC_POPUP_RADIUS, 6.f);
			}
			Rset_ui_scale(g_options.ui_scale);
		}

		void Changed()
		{
			Save();
			Apply();
			if (g_panel != R_INVALID_ID)
			{
				ui::SetValue(g_scale, g_options.ui_scale * 100.f);
				ui::SetText(g_scale_value, std::to_string(static_cast<int>(std::lround(g_options.ui_scale * 100.f))) + " %");
				ui::SetChecked(g_rounded, g_options.rounded);
				for (int i = 0; i < static_cast<int>(ui::Themes().size()); ++i)
					if (ui::Themes()[i].name == g_options.theme && Rcombo_box_get_selected(g_theme) != i)
						Rcombo_box_set_selected(g_theme, i);
			}
		}
	}

	void Init()
	{
		Load();
		Apply();
	}

	const Options& Get()
	{
		return g_options;
	}

	void BindPanel(Rid panel)
	{
		g_panel = panel;
		g_scale = ui::Find(panel, "options.scale");
		g_scale_value = ui::Find(panel, "options.scale.value");
		g_theme = ui::Find(panel, "options.theme");
		g_rounded = ui::Find(panel, "options.rounded");

		Rcombo_box_clear_items(g_theme);
		for (const ui::ThemeInfo& theme : ui::Themes())
			Rcombo_box_add_item(g_theme, theme.label.c_str());

		// The scale is applied when the slider is released (it would run away under the mouse).
		ui::OnValue(g_scale, [](double value)
		{
			ui::SetText(g_scale_value, std::to_string(static_cast<int>(std::lround(value))) + " %");
		});
		ui::OnEvent(g_scale, [](const REvent& event)
		{
			if (event.type == R_EVENT_MOUSE_UP)
			{
				ui::Later([]()
				{
					g_options.ui_scale = std::clamp(static_cast<float>(Rslider_get_value(g_scale)) / 100.f, kMinScale, kMaxScale);
					Changed();
				});
			}
			return false;
		});
		for (int preset : { 75, 100, 125, 150, 200 })
		{
			const std::string name = "options.scale." + std::to_string(preset);
			ui::OnClick(ui::Find(panel, name.c_str()), [preset]()
			{
				g_options.ui_scale = preset / 100.f;
				Changed();
			});
		}
		ui::OnSelect(g_theme, [](int index)
		{
			if (index >= 0 && index < static_cast<int>(ui::Themes().size()) && ui::Themes()[index].name != g_options.theme)
			{
				g_options.theme = ui::Themes()[index].name;
				Changed();
			}
		});
		ui::OnToggle(g_rounded, [](bool on)
		{
			g_options.rounded = on;
			Changed();
		});
		ui::OnClick(ui::Find(panel, "options.reset"), []()
		{
			g_options = Options{};
			Changed();
		});
		Changed();
	}

	void OpenPage(Page page)
	{
		if (g_panel != R_INVALID_ID)
			Rtab_view_set_active(g_panel, static_cast<int>(page));
	}
}
