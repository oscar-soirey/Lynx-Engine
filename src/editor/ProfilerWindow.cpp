#include "ProfilerWindow.h"

#include "EditorIcons.h"
#include "../core/Profiler.h"
#include "../host/GameProject.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>
#include <vector>
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


		using lynx::profiler::ZoneStats;

		bool g_hide_idle = true;
		bool g_freeze_on_spike = false;
		int g_seen_spikes = 0;
		char g_filter[128] = "";

		const ImVec4 kOk(0.25f, 0.62f, 0.28f, 1.f);
		const ImVec4 kWarn(0.90f, 0.62f, 0.15f, 1.f);
		const ImVec4 kBad(0.85f, 0.22f, 0.20f, 1.f);

		bool ContainsNoCase(const std::string& text, const char* needle)
		{
			if (!needle || !*needle)
				return true;
			const std::string n = needle;
			auto it = std::search(text.begin(), text.end(), n.begin(), n.end(),
			                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
			return it != text.end();
		}

		std::string PathOf(const std::vector<ZoneStats>& zones, int index)
		{
			std::string path;
			for (int i = index; i >= 0; i = zones[static_cast<size_t>(i)].parent)
				path = zones[static_cast<size_t>(i)].name + (path.empty() ? "" : " > " + path);
			return path;
		}

		// Colour of a time : green below 1 ms, orange from 2 ms / 10 % of the
		// frame, red from 5 ms / 30 %.
		ImVec4 TimeColor(double ms, double frame)
		{
			const double share = frame > 0.0 ? ms / frame : 0.0;
			if (ms >= 5.0 || share >= 0.30) return kBad;
			if (ms >= 2.0 || share >= 0.10) return kWarn;
			return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}

		void TimeCell(double ms, double frame)
		{
			ImGui::TableNextColumn();
			if (ms < 0.0005)
				ImGui::TextDisabled("-");
			else
				ImGui::TextColored(TimeColor(ms, frame), "%.3f", ms);
		}

		// Frame time graph with the 60 fps (16.7 ms), 30 fps (33.3 ms) and
		// spike threshold lines.
		void DrawFrameGraph(const std::vector<float>& frames, float height, float spike_ms)
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

			const float bar = size.x / 300.f;
			const float start_x = max.x - bar * static_cast<float>(frames.size());
			for (size_t i = 0; i < frames.size(); ++i)
			{
				const float ms = frames[i];
				const ImVec4 color = ms <= 16.8f ? kOk : ms <= 33.4f ? Mix(kOk, kWarn, (ms - 16.8f) / 16.6f) : kBad;
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
			if (spike_ms > 0.f && spike_ms <= top)
			{
				const float y = y_of(spike_ms);
				draw->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), ImGui::ColorConvertFloat4ToU32(ImVec4(kBad.x, kBad.y, kBad.z, 0.6f)), 1.f);
				draw->AddText(ImVec2(max.x - ImGui::CalcTextSize("spike").x - 4.f, y - ImGui::GetTextLineHeight()),
				              ImGui::ColorConvertFloat4ToU32(kBad), "spike");
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

		// Children of each zone sorted by time (the biggest first), roots too.
		std::vector<int> SortedTreeOrder(const std::vector<ZoneStats>& zones, bool by_time)
		{
			std::vector<std::vector<int>> children(zones.size() + 1);
			for (size_t i = 0; i < zones.size(); ++i)
			{
				const int parent = zones[i].parent;
				children[parent >= 0 ? static_cast<size_t>(parent) : zones.size()].push_back(static_cast<int>(i));
			}
			if (by_time)
				for (auto& list : children)
					std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
						return zones[static_cast<size_t>(a)].average_ms > zones[static_cast<size_t>(b)].average_ms; });

			std::vector<int> order;
			order.reserve(zones.size());
			std::vector<int> stack(children[zones.size()].rbegin(), children[zones.size()].rend());
			while (!stack.empty())
			{
				const int i = stack.back();
				stack.pop_back();
				order.push_back(i);
				const auto& kids = children[static_cast<size_t>(i)];
				stack.insert(stack.end(), kids.rbegin(), kids.rend());
			}
			return order;
		}

		bool Idle(const ZoneStats& z)
		{
			return z.calls == 0 && z.average_ms < 0.001;
		}

		// The zone tree in a table. `spike` : values of one frame (no average, no max).
		void DrawTree(const char* id, const std::vector<ZoneStats>& zones, double frame, bool spike)
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
			                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
			if (!ImGui::BeginTable(id, spike ? 4 : 6, flags, ImVec2(0.f, ImGui::GetContentRegionAvail().y)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(spike ? "Time" : "Average");
			ImGui::TableSetupColumn("Self");
			if (!spike)
			{
				ImGui::TableSetupColumn("Last");
				ImGui::TableSetupColumn("Max (2 s)");
			}
			ImGui::TableSetupColumn("Calls");
			ImGui::TableHeadersRow();

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Self : time of the zone without its child zones.\n"
				                  "The biggest Self values are where the time goes.");

			const bool filtering = g_filter[0] != 0;
			const std::vector<int> order = SortedTreeOrder(zones, g_sort_by_time && !filtering);
			frame = std::max(frame, 0.001);

			int open_depth = 0;          // tree nodes pushed
			int skip_below = -1;         // depth of a closed node : its subtree is hidden
			for (int index : order)
			{
				const ZoneStats& z = zones[static_cast<size_t>(index)];

				if (filtering)
				{
					if (!ContainsNoCase(z.name, g_filter))
						continue;
				}
				else
				{
					if (skip_below >= 0)
					{
						if (z.depth > skip_below)
							continue;
						skip_below = -1;
					}
					while (open_depth > z.depth)
					{
						ImGui::TreePop();
						--open_depth;
					}
					if (!spike && g_hide_idle && Idle(z))
					{
						if (z.has_children)
							skip_below = z.depth;
						continue;
					}
				}

				const double value = spike ? z.last_ms : z.average_ms;
				const double self = spike ? z.self_last_ms : z.self_average_ms;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				// Share of the frame, drawn behind the name.
				const ImVec2 cell = ImGui::GetCursorScreenPos();
				const float width = ImGui::GetContentRegionAvail().x;
				const float share = static_cast<float>(std::clamp(value / frame, 0.0, 1.0));
				const float self_share = static_cast<float>(std::clamp(self / frame, 0.0, 1.0));
				ImVec4 bar = ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram);
				bar.w *= 0.22f;
				ImGui::GetWindowDrawList()->AddRectFilled(
					cell, ImVec2(cell.x + width * share, cell.y + ImGui::GetTextLineHeight()),
					ImGui::ColorConvertFloat4ToU32(bar));
				bar.w *= 2.f;
				ImGui::GetWindowDrawList()->AddRectFilled(
					cell, ImVec2(cell.x + width * self_share, cell.y + ImGui::GetTextLineHeight()),
					ImGui::ColorConvertFloat4ToU32(bar));

				if (filtering)
				{
					ImGui::TextUnformatted(z.name.c_str());
				}
				else
				{
					ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
					if (!z.has_children)
						node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
					if (z.depth < 3)
						node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
					// ID : the index (two zones can have the same name under one parent... not
					// in practice, but the index is unique).
					const bool node_open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(index)), node_flags, "%s", z.name.c_str());
					if (z.has_children)
					{
						if (node_open)
							++open_depth;
						else
							skip_below = z.depth;
					}
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s\n%.0f %% of the frame (%.0f %% self)", PathOf(zones, index).c_str(),
					                  share * 100.f, self_share * 100.f);

				TimeCell(value, frame);
				TimeCell(self, frame);
				if (!spike)
				{
					TimeCell(z.last_ms, frame);
					TimeCell(z.max_ms, frame);
				}
				ImGui::TableNextColumn();
				ImGui::Text("%d", z.calls);
			}
			while (open_depth > 0)
			{
				ImGui::TreePop();
				--open_depth;
			}

			if (zones.empty())
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled(spike ? "No spike yet." : "Recording... (zones appear after one frame)");
			}
			ImGui::EndTable();
		}

		// The same name under several parents (a JS class called from
		// several places...) is added up : where the time goes, in one list.
		struct HotSpot
		{
			std::string name;
			double self_ms = 0.0;
			double total_ms = 0.0;
			double max_ms = 0.0;
			int calls = 0;
			int places = 0;
		};

		std::vector<HotSpot> HotSpots(const std::vector<ZoneStats>& zones)
		{
			std::vector<HotSpot> spots;
			for (const ZoneStats& z : zones)
			{
				auto it = std::find_if(spots.begin(), spots.end(), [&](const HotSpot& h) { return h.name == z.name; });
				if (it == spots.end())
				{
					spots.push_back({ z.name });
					it = spots.end() - 1;
				}
				it->self_ms += z.self_average_ms;
				it->total_ms += z.average_ms;
				it->max_ms = std::max(it->max_ms, z.max_ms);
				it->calls += z.calls;
				++it->places;
			}
			std::sort(spots.begin(), spots.end(), [](const HotSpot& a, const HotSpot& b) { return a.self_ms > b.self_ms; });
			return spots;
		}

		void DrawHotSpots(const std::vector<ZoneStats>& zones, double frame)
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
			                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
			if (!ImGui::BeginTable("##hotspots", 5, flags, ImVec2(0.f, ImGui::GetContentRegionAvail().y)))
				return;
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Zone (by self time)", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Self");
			ImGui::TableSetupColumn("Total");
			ImGui::TableSetupColumn("Max (2 s)");
			ImGui::TableSetupColumn("Calls");
			ImGui::TableHeadersRow();

			frame = std::max(frame, 0.001);
			for (const HotSpot& h : HotSpots(zones))
			{
				if (!ContainsNoCase(h.name, g_filter))
					continue;
				if (g_hide_idle && h.calls == 0 && h.self_ms < 0.001)
					continue;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const ImVec2 cell = ImGui::GetCursorScreenPos();
				const float width = ImGui::GetContentRegionAvail().x;
				const float share = static_cast<float>(std::clamp(h.self_ms / frame, 0.0, 1.0));
				ImVec4 bar = ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram);
				bar.w *= 0.45f;
				ImGui::GetWindowDrawList()->AddRectFilled(
					cell, ImVec2(cell.x + width * share, cell.y + ImGui::GetTextLineHeight()),
					ImGui::ColorConvertFloat4ToU32(bar));
				ImGui::TextUnformatted(h.name.c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s\n%.0f %% of the frame in this zone itself\n%d place(s) in the tree",
					                  h.name.c_str(), share * 100.f, h.places);

				TimeCell(h.self_ms, frame);
				TimeCell(h.total_ms, frame);
				TimeCell(h.max_ms, frame);
				ImGui::TableNextColumn();
				ImGui::Text("%d", h.calls);
			}
			ImGui::EndTable();
		}

		void DrawCounters(const std::vector<lynx::profiler::Counter>& counters)
		{
			if (counters.empty())
				return;
			bool first = true;
			for (const auto& c : counters)
			{
				if (!first)
					ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x * 3.f);
				first = false;
				ImGui::TextDisabled("%s", c.name.c_str());
				ImGui::SameLine();
				if (c.value == static_cast<double>(static_cast<long long>(c.value)))
					ImGui::Text("%lld%s", static_cast<long long>(c.value), c.per_frame ? "/f" : "");
				else
					ImGui::Text("%.2f%s", c.value, c.per_frame ? "/f" : "");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(c.per_frame ? "%s : during the last frame" : "%s", c.name.c_str());
				// Wrap before the edge.
				if (ImGui::GetItemRectMax().x > ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 150.f)
					first = true;
			}
		}

		// Text for the clipboard : to paste in an issue or a chat.
		std::string Report(const lynx::profiler::Snapshot& s)
		{
			std::string out;
			char line[512];
			std::snprintf(line, sizeof(line), "Lynx profiler : %.2f ms average (%.0f fps), worst %.2f ms\n",
			              s.average_frame_ms, s.average_frame_ms > 0.0 ? 1000.0 / s.average_frame_ms : 0.0, s.max_frame_ms);
			out += line;
			for (const auto& c : s.counters)
			{
				std::snprintf(line, sizeof(line), "  %s = %g%s\n", c.name.c_str(), c.value, c.per_frame ? " /frame" : "");
				out += line;
			}
			out += "\nZones (average ms, self ms, max ms, calls) :\n";
			for (int i : SortedTreeOrder(s.zones, true))
			{
				const ZoneStats& z = s.zones[static_cast<size_t>(i)];
				if (Idle(z))
					continue;
				std::snprintf(line, sizeof(line), "%*s%-*s %8.3f %8.3f %8.3f %6d\n", z.depth * 2, "",
				              std::max(1, 48 - z.depth * 2), z.name.c_str(), z.average_ms, z.self_average_ms, z.max_ms, z.calls);
				out += line;
			}
			out += "\nHot spots (self ms) :\n";
			int n = 0;
			for (const HotSpot& h : HotSpots(s.zones))
			{
				if (n++ >= 25 || h.self_ms < 0.01)
					break;
				std::snprintf(line, sizeof(line), "  %-48s %8.3f  (%d calls)\n", h.name.c_str(), h.self_ms, h.calls);
				out += line;
			}
			if (s.spike.valid)
			{
				std::snprintf(line, sizeof(line), "\nLast spike : %.2f ms, %.0f s ago (%d spikes)\n",
				              s.spike.frame_ms, s.spike.seconds_ago, s.spike.count);
				out += line;
				for (const ZoneStats& z : s.spike.zones)
				{
					if (z.last_ms < 0.05)
						continue;
					std::snprintf(line, sizeof(line), "%*s%-*s %8.3f %8.3f %6d\n", z.depth * 2 + 2, "",
					              std::max(1, 46 - z.depth * 2), z.name.c_str(), z.last_ms, z.self_last_ms, z.calls);
					out += line;
				}
			}
			return out;
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

		ImGui::SetNextWindowSize(ImVec2(720.f, 620.f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Profiler", open))
		{
			lynx::profiler::SetEnabled(false);
			ImGui::End();
			return;
		}
		lynx::profiler::SetEnabled(!g_paused);

		if (!g_paused)
		{
			lynx::profiler::GetSnapshot(g_snapshot);
			// Freeze on spike : the window stops on the frame that lagged.
			if (g_freeze_on_spike && g_snapshot.spike.count > g_seen_spikes)
				g_paused = true;
			g_seen_spikes = g_snapshot.spike.count;
		}

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
		if (g_paused)
		{
			ImGui::SameLine();
			ImGui::TextColored(kWarn, "PAUSED");
		}

		ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
		                         ImGui::GetWindowContentRegionMax().x -
		                         ImGui::CalcTextSize("Resume").x - ImGui::CalcTextSize("Reset").x - ImGui::CalcTextSize("Copy").x -
		                         ImGui::GetStyle().FramePadding.x * 6.f - ImGui::GetStyle().ItemSpacing.x * 2.f -
		                         ImGui::GetTextLineHeight() * 3.f - ImGui::GetStyle().ItemInnerSpacing.x * 3.f));
		if (icons::ButtonWithLabel(g_paused ? "Resume" : "Pause", g_paused ? icons::Icon::Play : icons::Icon::Pause))
			g_paused = !g_paused;
		ImGui::SameLine();
		if (icons::ButtonWithLabel("Reset", icons::Icon::Clear))
		{
			lynx::profiler::Reset();
			g_snapshot = {};
			g_seen_spikes = 0;
		}
		ImGui::SameLine();
		if (icons::ButtonWithLabel("Copy", icons::Icon::Copy))
			ImGui::SetClipboardText(Report(g_snapshot).c_str());
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Copy the measures as text (zones, hot spots, last spike, counters).");

		float spike_ms = static_cast<float>(lynx::profiler::GetSpikeThreshold());
		DrawFrameGraph(g_snapshot.frame_ms, ImGui::GetTextLineHeight() * 5.f, spike_ms);

		DrawCounters(g_snapshot.counters);

		// --- Options ---------------------------------------------------------
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
		ImGui::InputTextWithHint("##filter", "Filter zones", g_filter, sizeof(g_filter));
		ImGui::SameLine();
		ImGui::Checkbox("Sort by time", &g_sort_by_time);
		ImGui::SameLine();
		ImGui::Checkbox("Hide idle", &g_hide_idle);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Hide the zones not called lately (destroyed actors, finished effects...).");
		ImGui::SameLine();
		bool detailed = lynx::profiler::IsDetailed();
		if (ImGui::Checkbox("Fine zones", &detailed))
		{
			lynx::profiler::SetDetailed(detailed);
			lynx::profiler::Reset();
			g_snapshot = {};
			g_seen_spikes = 0;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("One zone per component type, per JS class method (Lueur.Update...) and per script.\n"
			                  "Costs a little while recording : turn it off to measure the frame time alone.");

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
		if (ImGui::DragFloat("Spike (ms)", &spike_ms, 0.5f, 0.f, 1000.f, "%.0f"))
			lynx::profiler::SetSpikeThreshold(spike_ms);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("A frame slower than this is kept (tab \"Last spike\"). 0 : off.");
		ImGui::SameLine();
		ImGui::Checkbox("Pause on spike", &g_freeze_on_spike);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("The window pauses on the next spike : read it calmly, then Resume.");

		// --- Zones -----------------------------------------------------------
		const lynx::profiler::Spike& spike = g_snapshot.spike;
		char spike_label[96];
		if (spike.valid)
			std::snprintf(spike_label, sizeof(spike_label), "Last spike (%.0f ms, %.0f s ago)###spike", spike.frame_ms, spike.seconds_ago);
		else
			std::snprintf(spike_label, sizeof(spike_label), "Last spike###spike");

		if (ImGui::BeginTabBar("##profiler_tabs"))
		{
			if (ImGui::BeginTabItem("Tree"))
			{
				DrawTree("##zones", g_snapshot.zones, average, false);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Hot spots"))
			{
				DrawHotSpots(g_snapshot.zones, average);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(spike_label))
			{
				if (spike.valid)
					ImGui::TextDisabled("The zones of the last frame slower than %.0f ms (%d spikes since Reset).", spike_ms, spike.count);
				DrawTree("##spike", spike.zones, spike.frame_ms, true);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}
