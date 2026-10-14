#include "StartupBuild.h"

#include "GameBuild.h"

#include "raven/RavenUi.h"

#include <Raven.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

// =============================================================================
// Build before opening, in Raven (interface/windows/startup_build.xml). Its
// own loop of Rwait_frame until the build succeeds or the user chooses.
// =============================================================================

namespace lynx::editor
{
	namespace
	{
		enum class State
		{
			Building,
			Failed,
		};

		bool Contains(const std::string& line, const char* word)
		{
			std::string lower(line);
			std::transform(lower.begin(), lower.end(), lower.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return lower.find(word) != std::string::npos;
		}

		struct Window
		{
			Rid window = R_INVALID_ID;
			Rid lines = R_INVALID_ID;
			Rid status = R_INVALID_ID;
			uint64_t version = ~0ull;
			size_t shown = 0;
			bool close_requested = false;
		};

		// New lines of the build output (errors in red, warnings in yellow).
		void UpdateLines(Window& w)
		{
			uint64_t version = 0;
			std::vector<std::string> all = game_build::OutputAll(&version);
			if (version == w.version)
				return;

			if (version != w.version && all.size() < w.shown)
			{
				Rtree_view_clear_items(w.lines);   // a new build : the output starts again
				w.shown = 0;
			}
			w.version = version;

			Rid last = R_INVALID_ID;
			for (size_t i = w.shown; i < all.size(); ++i)
			{
				last = Rtree_view_add_item(w.lines, R_INVALID_ID, all[i].c_str());
				if (Contains(all[i], "error"))
					Rtree_item_set_text_color(last, 0.95f, 0.45f, 0.45f, 1.f);
				else if (Contains(all[i], "warning"))
					Rtree_item_set_text_color(last, 0.95f, 0.80f, 0.35f, 1.f);
			}
			w.shown = all.size();
			if (last != R_INVALID_ID)
				Rtree_view_scroll_to_item(w.lines, last);
		}

		int OnCloseRequest(Rid, void* user)
		{
			static_cast<Window*>(user)->close_requested = true;
			return R_FALSE;
		}
	}


	StartupBuildResult RunStartupBuild(
		const host::GameProject& project,
		const std::string& reason,
		bool allow_open_anyway)
	{
		std::cout << "[BUILD] " << reason << "\n"
		          << "[BUILD] Compiling " << project.name << " before opening it\n";

		Window w;
		w.window = ui::LoadWindow("windows/startup_build.xml");
		if (w.window == R_INVALID_ID)
		{
			std::cerr << "[BUILD] Could not create the build window\n";
			return StartupBuildResult::Quit;
		}
		Rwindow_set_title(w.window, ("Lynx Engine - Compiling " + project.name).c_str());
		Rwindow_set_close_callback(w.window, OnCloseRequest, &w);

		w.lines = ui::Find(w.window, "sb.lines");
		w.status = ui::Find(w.window, "sb.status");
		ui::SetText(ui::Find(w.window, "sb.project"), "Project : " + project.name);
		ui::SetText(ui::Find(w.window, "sb.path"), project.root.string());
		ui::SetText(ui::Find(w.window, "sb.reason"), reason);

		Rid retry = ui::Find(w.window, "sb.retry");
		Rid anyway = ui::Find(w.window, "sb.anyway");
		Rid other = ui::Find(w.window, "sb.other");
		Rid quit = ui::Find(w.window, "sb.quit");
		Rid cancel = ui::Find(w.window, "sb.cancel");

		State state = State::Building;
		StartupBuildResult result = StartupBuildResult::Quit;
		bool done = false;

		auto show_state = [&]()
		{
			const bool failed = state == State::Failed;
			ui::SetVisible(cancel, !failed);
			ui::SetVisible(retry, failed);
			ui::SetVisible(anyway, failed && allow_open_anyway);
			ui::SetVisible(other, failed);
			ui::SetVisible(quit, failed);
			ui::SetClass(w.status, "error", failed);
			if (failed)
				ui::SetText(w.status, allow_open_anyway
					? "The build failed. Fix the errors and retry, or open the project with the previous build of the game."
					: "The build failed. Fix the errors and retry : the current game DLL cannot be loaded by this engine "
					  "(it would crash the editor).");
		};

		ui::OnClick(cancel, []() { game_build::Cancel(); });   // ends as a failed build
		ui::OnClick(retry, [&]()
		{
			if (game_build::Start())
			{
				state = State::Building;
				show_state();
			}
		});
		ui::OnClick(anyway, [&]() { result = StartupBuildResult::OpenAnyway; done = true; });
		ui::OnClick(other, [&]() { result = StartupBuildResult::OtherProject; done = true; });
		ui::OnClick(quit, [&]() { result = StartupBuildResult::Quit; done = true; });

		if (!game_build::Start())
			state = State::Failed;
		show_state();

		Rwindow_show(w.window);

		while (!done && Rwait_frame(0.05))
		{
			// Window closed : stop the build and quit.
			if (w.close_requested)
			{
				game_build::Shutdown();
				result = StartupBuildResult::Quit;
				break;
			}

			UpdateLines(w);

			bool success = false;
			if (state == State::Building)
			{
				const std::string status = game_build::ToolbarStatus();
				ui::SetText(w.status, status.empty() ? "Building..." : status);

				if (game_build::PollFinished(success))
				{
					UpdateLines(w);
					if (success)
					{
						// The editor's "Build" panel does not need to pop up.
						game_build::WindowOpen() = false;
						result = StartupBuildResult::Built;
						done = true;
					}
					else
					{
						state = State::Failed;
						show_state();
					}
				}
			}
		}

		Rwindow_set_close_callback(w.window, nullptr, nullptr);
		Rwindow_destroy(w.window);
		return result;
	}
}
