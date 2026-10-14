#pragma once

// =============================================================================
// Editor options (Options panel of the toolbar)
// -----------------------------------------------------------------------------
// Preferences of the user, the same for every project : saved in
//   Windows : %APPDATA%/Lynx/editor_options.cfg
//   others  : ~/.config/lynx/editor_options.cfg
//
//   Interface : global UI scale, theme (the themes of src/editor/interface/themes
//               and the ones of Raven), rounded corners ;
//   Project   : the settings of the open project (vsync, volume, camera
//               speed, voxels... : settings.cfg of the project), bound by
//               EditorMain to the widgets of panels/options.xml ;
//   Plugins   : the plugins (enable / disable), filled by EditorMain.
// =============================================================================

#include <Raven.h>

#include <string>

namespace lynx::editor::options
{
	struct Options
	{
		float ui_scale = 1.f;              // 0.5 .. 2
		std::string theme = "Lynx Glass";  // a theme loaded by ui::Init (raven/RavenUi.h)
		bool rounded = false;              // rounded corners on top of the theme
	};

	/** After ui::Init (the themes are loaded) : reads the file, applies theme and scale. */
	void Init();

	const Options& Get();

	/** The Interface page of the Options panel (panels/options.xml). */
	void BindPanel(Rid panel);

	enum class Page { Interface = 0, Project, Plugins };

	/** Shows this page of the panel (the editor shows the panel itself). */
	void OpenPage(Page page);
}
