#pragma once

// =============================================================================
// Editor options (Options window of the toolbar)
// -----------------------------------------------------------------------------
// Preferences of the user, the same for every project : saved in
//   Windows : %APPDATA%/Lynx/editor_options.cfg
//   others  : ~/.config/lynx/editor_options.cfg
//
//   Interface : global UI scale (fonts + sizes), theme (Pixel, Light, Classic,
//               Lynx), rounded corners ;
//   Plugins   : (later) the list of the editor plugins.
//
//   Project   : the settings of the open project (vsync, volume, camera
//               speed, voxels... : settings.cfg of the project), drawn by
//               EditorMain (SetProjectPage) ;
// =============================================================================

#include <functional>

namespace lynx::editor::options
{
	// Dark : the pixel theme of the editor (StyleColorsDark of the patched ImGui, Aseprite-like).
	enum class Theme { Dark = 0, Light, Classic, Lynx, Count };

	struct Options
	{
		float ui_scale = 1.f;          // 0.5 .. 2
		Theme theme = Theme::Dark;
		bool rounded = false;          // rounded windows / frames
	};

	/** After ImGui::CreateContext (InitImGui) : loads the file, applies the style. */
	void Init();

	/** Before ImGui::NewFrame : applies a change made in the window (style can't change mid frame). */
	void ApplyPending();

	const Options& Get();

	/** Content of the "Project" page (the editor's project settings). */
	void SetProjectPage(const std::function<void()>& draw);

	/** The Options window. */
	void Draw(bool* open);

	const char* ThemeName(Theme theme);
}
