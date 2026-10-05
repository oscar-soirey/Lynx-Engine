#include "ProfilerWindow.h"

#include "EditorIcons.h"
#include "../core/Profiler.h"
#include "../host/GameProject.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::profiler_window
{
	namespace
	{
		bool g_paused = false;
		bool g_sort_by_time = false;
		lynx::profiler::Snapshot g_snapshot;

		constexpr const char* kTracyReleases = "https://github.com/wolfpld/tracy/releases";

		// tracy-profiler.exe : next to the editor, in the tools folder of the
		// launcher (%LOCALAPPDATA%\Lynx\tools\tracy), or in the PATH.
		fs::path FindTracy()
		{
			std::error_code ec;
			std::vector<fs::path> candidates = {
				host::GetEditorDirectory() / "tracy-profiler.exe",
				host::GetEditorDirectory() / "tracy" / "tracy-profiler.exe",
			};
			if (const char* local = std::getenv("LOCALAPPDATA"))
				candidates.push_back(fs::path(local) / "Lynx" / "tools" / "tracy" / "tracy-profiler.exe");
			for (const fs::path& path : candidates)
				if (fs::is_regular_file(path, ec))
					return path;

			if (const char* path_env = std::getenv("PATH"))
			{
				std::string paths = path_env;
				size_t start = 0;
				while (start <= paths.size())
				{
					const size_t end = paths.find(';', start);
					const std::string dir = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
					if (!dir.empty())
					{
						for (const char* name : { "tracy-profiler.exe", "Tracy.exe", "tracy.exe" })
							if (fs::is_regular_file(fs::path(dir) / name, ec))
								return fs::path(dir) / name;
					}
					if (end == std::string::npos)
						break;
					start = end + 1;
				}
			}
			return {};
		}

		void Open(const fs::path& target)
		{
#ifdef _WIN32
			ShellExecuteW(nullptr, L"open", target.wstring().c_str(), nullptr,
			              target.parent_path().wstring().c_str(), SW_SHOWNORMAL);
#else
			(void)target;
#endif
		}

		void OpenUrl(const char* url)
		{
#ifdef _WIN32
			ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
			(void)url;
#endif
		}

		ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
		{
			return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
		}

		// Frame time graph with the 60 fps (16.7 ms) and 30 fps (33.3 ms) lines.
		void DrawFrameGraph(const std::vector<float>& frames, float height)
		{
			const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max(min.x + size.x, min.y + size.y);
			ImGui::Dummy(size);

			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
			draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border));
			if (frames.empty())
				return;

			float top = 33.3f * 1.25f;
			for (float f : frames)
				top = std::max(top, f * 1.1f);
			top = std::min(top, 250.f);

			auto y_of = [&](float ms) { return max.y - std::min(ms, top) / top * size.y; };

			const ImVec4 ok(0.25f, 0.62f, 0.28f, 1.f);
			const ImVec4 warn(0.90f, 0.62f, 0.15f, 1.f);
			const ImVec4 bad(0.85f, 0.22f, 0.20f, 1.f);

			const float bar = size.x / 300.f;
			const float start_x = max.x - bar * static_cast<float>(frames.size());
			for (size_t i = 0; i < frames.size(); ++i)
			{
				const float ms = frames[i];
				const ImVec4 color = ms <= 16.8f ? ok : ms <= 33.4f ? Mix(ok, warn, (ms - 16.8f) / 16.6f) : bad;
				const float x = start_x + bar * static_cast<float>(i);
				draw->AddRectFilled(ImVec2(x, y_of(ms)), ImVec2(x + std::max(1.f, bar - 0.5f), max.y),
				                    ImGui::ColorConvertFloat4ToU32(color));
			}

			for (const auto& [ms, label] : { std::pair<float, const char*>{ 16.7f, "60 fps" }, { 33.3f, "30 fps" } })
			{
				if (ms > top)
					continue;
				const float y = y_of(ms);
				draw->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), ImGui::GetColorU32(ImGuiCol_TextDisabled), 1.f);
				draw->AddText(ImVec2(min.x + 4.f, y - ImGui::GetTextLineHeight()), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
			}

			// Tooltip : the frame under the mouse.
			if (ImGui::IsItemHovered())
			{
				const float mx = ImGui::GetIO().MousePos.x;
				const int index = static_cast<int>((mx - start_x) / bar);
				if (index >= 0 && index < static_cast<int>(frames.size()))
					ImGui::SetTooltip("%.2f ms (%.0f fps)", frames[static_cast<size_t>(index)],
					                  frames[static_cast<size_t>(index)] > 0.f ? 1000.f / frames[static_cast<size_t>(index)] : 0.f);
			}
		}
	}


	void Draw(bool* open)
	{
		// The built-in profiler records only while the window is visible
		// (open, and its tab selected when it is docked behind another one).
		const bool open_now = !open || *open;
		if (!open_now)
		{
			lynx::profiler::SetEnabled(false);
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(620.f, 520.f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Profiler", open))
		{
			lynx::profiler::SetEnabled(false);
			ImGui::End();
			return;
		}
		lynx::profiler::SetEnabled(!g_paused);

		if (!g_paused)
			lynx::profiler::GetSnapshot(g_snapshot);

		// --- Tracy -----------------------------------------------------------
		{
			const bool available = lynx::profiler::TracyAvailable();
			const bool connected = lynx::profiler::TracyConnected();

			ImGui::AlignTextToFramePadding();
			icons::Draw(icons::Icon::Profiler, 0.f,
			            connected ? ImVec4(0.25f, 0.62f, 0.28f, 1.f) : icons::kThemeTint);
			ImGui::SameLine();
			if (!available)
				ImGui::TextDisabled("Tracy : not compiled in (CMake option LYNX_PROFILER_TRACY)");
			else if (connected)
				ImGui::TextColored(ImVec4(0.25f, 0.62f, 0.28f, 1.f), "Tracy : connected, recording");
			else
				ImGui::TextDisabled("Tracy : waiting for a connection (on demand, no cost until then)");

			if (available)
			{
				ImGui::SameLine();
				static fs::path tracy = FindTracy();
				if (!tracy.empty())
				{
					if (ImGui::Button("Open Tracy"))
						Open(tracy);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s\nThen \"Connect\" to 127.0.0.1 in Tracy.", tracy.string().c_str());
				}
				else
				{
					if (ImGui::Button("Get Tracy..."))
						OpenUrl(kTracyReleases);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("tracy-profiler.exe was not found (next to the editor, in the launcher's tools,\n"
						                  "or in the PATH). Download the Windows release (Tracy 0.14.x : same version\n"
						                  "as the client compiled in the engine).");
					ImGui::SameLine();
					if (ImGui::SmallButton("Search again"))
						tracy = FindTracy();
				}
			}
		}

		ImGui::Separator();

		// --- Frame -----------------------------------------------------------
		const double average = g_snapshot.average_frame_ms;
		ImGui::Text("%.1f fps", average > 0.0 ? 1000.0 / average : 0.0);
		ImGui::SameLine();
		ImGui::TextDisabled("frame %.2f ms (average)   worst %.2f ms", average, g_snapshot.max_frame_ms);

		ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
		                         ImGui::GetWindowContentRegionMax().x -
		                         ImGui::CalcTextSize("Resume").x - ImGui::CalcTextSize("Reset").x -
		                         ImGui::GetStyle().FramePadding.x * 4.f - ImGui::GetStyle().ItemSpacing.x -
		                         ImGui::GetTextLineHeight() * 2.f - ImGui::GetStyle().ItemInnerSpacing.x * 2.f));
		if (icons::ButtonWithLabel(g_paused ? "Resume" : "Pause", g_paused ? icons::Icon::Play : icons::Icon::Pause))
			g_paused = !g_paused;
		ImGui::SameLine();
		if (icons::ButtonWithLabel("Reset", icons::Icon::Clear))
		{
			lynx::profiler::Reset();
			g_snapshot = {};
		}

		DrawFrameGraph(g_snapshot.frame_ms, ImGui::GetTextLineHeight() * 5.f);

		// --- Zones -----------------------------------------------------------
		ImGui::Checkbox("Sort by time", &g_sort_by_time);
		ImGui::SameLine();
		ImGui::TextDisabled("(LYNX_PROFILE_SCOPE zones of the main thread, ms)");

		std::vector<lynx::profiler::ZoneStats> zones = g_snapshot.zones;
		if (g_sort_by_time)
			std::stable_sort(zones.begin(), zones.end(),
			                 [](const auto& a, const auto& b) { return a.average_ms > b.average_ms; });

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
		                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
		if (ImGui::BeginTable("##zones", 5, flags, ImVec2(0.f, ImGui::GetContentRegionAvail().y)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Average");
			ImGui::TableSetupColumn("Last");
			ImGui::TableSetupColumn("Max (2 s)");
			ImGui::TableSetupColumn("Calls");
			ImGui::TableHeadersRow();

			const double frame = std::max(average, 0.001);
			for (const auto& zone : zones)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				// Share of the frame, drawn behind the name.
				const ImVec2 cell = ImGui::GetCursorScreenPos();
				const float width = ImGui::GetContentRegionAvail().x;
				const float share = static_cast<float>(std::clamp(zone.average_ms / frame, 0.0, 1.0));
				ImVec4 bar = ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram);
				bar.w *= 0.35f;
				ImGui::GetWindowDrawList()->AddRectFilled(
					cell, ImVec2(cell.x + width * share, cell.y + ImGui::GetTextLineHeight()),
					ImGui::ColorConvertFloat4ToU32(bar));

				if (!g_sort_by_time)
					ImGui::Indent(static_cast<float>(zone.depth) * ImGui::GetStyle().IndentSpacing * 0.6f + 0.001f);
				ImGui::TextUnformatted(zone.name.c_str());
				if (!g_sort_by_time)
					ImGui::Unindent(static_cast<float>(zone.depth) * ImGui::GetStyle().IndentSpacing * 0.6f + 0.001f);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s\n%.0f %% of the frame (average)", zone.name.c_str(), share * 100.f);

				ImGui::TableNextColumn();
				ImGui::Text("%.3f", zone.average_ms);
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", zone.last_ms);
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", zone.max_ms);
				ImGui::TableNextColumn();
				ImGui::Text("%d", zone.calls);
			}

			if (zones.empty())
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("Recording... (zones appear after one frame)");
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}
}
