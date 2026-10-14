#include "ProfilerWindow.h"

#include "raven/RavenUi.h"
#include "../core/Profiler.h"
#include "../host/GameProject.h"

#include <Raven.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

// =============================================================================
// Profiler window (Raven : windows/profiler.xml). The tables of zones are
// painted (Custom widgets) : they change every frame.
// =============================================================================

namespace fs = std::filesystem;

namespace lynx::editor::profiler_window
{
	namespace
	{
		using lynx::profiler::ZoneStats;

		bool g_paused = false;
		bool g_sort_by_time = false;
		bool g_hide_idle = true;
		bool g_freeze_on_spike = false;
		int g_seen_spikes = 0;
		std::string g_filter;
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


		bool Idle(const ZoneStats& z)
		{
			return z.calls == 0 && z.average_ms < 0.001;
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
		// ---------------------------------------------------------------------
		// Painted tables
		// ---------------------------------------------------------------------

		constexpr float kRowHeight = 20.f;
		constexpr float kHeaderHeight = 22.f;

		struct Rgba { float r, g, b, a; };
		constexpr Rgba kOk{ 0.25f, 0.62f, 0.28f, 1.f };
		constexpr Rgba kWarn{ 0.90f, 0.62f, 0.15f, 1.f };
		constexpr Rgba kBad{ 0.85f, 0.22f, 0.20f, 1.f };
		constexpr Rgba kText{ 0.90f, 0.90f, 0.90f, 1.f };
		constexpr Rgba kDim{ 0.55f, 0.55f, 0.58f, 1.f };
		constexpr Rgba kBar{ 0.88f, 0.63f, 0.19f, 1.f };

		// Colour of a time : plain below 2 ms, orange from 2 ms / 10 % of the
		// frame, red from 5 ms / 30 %.
		Rgba TimeColor(double ms, double frame)
		{
			const double share = frame > 0.0 ? ms / frame : 0.0;
			if (ms >= 5.0 || share >= 0.30) return kBad;
			if (ms >= 2.0 || share >= 0.10) return kWarn;
			return kText;
		}

		void Text(RPainter* p, float x, float y, const std::string& text, Rgba c)
		{
			Rdraw_text(p, R_INVALID_ID, 0.f, x, y, text.c_str(), c.r, c.g, c.b, c.a);
		}

		std::string Ms(double ms)
		{
			if (ms < 0.0005)
				return "-";
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%.3f", ms);
			return buffer;
		}

		struct Row
		{
			std::string name;
			std::string key;        // path : the state of the node
			int depth = 0;
			bool has_children = false;
			bool open = false;
			double value[5] = {};   // the columns after the name
			int calls = 0;
			double share = 0.0, self_share = 0.0;
		};

		struct Table
		{
			Rid widget = R_INVALID_ID;
			std::vector<const char*> columns;   // after the name ; the last one : Calls
			std::vector<Row> rows;
			double frame = 1.0;
			std::string empty;
			bool tree = true;
		};

		Table g_tree, g_hot, g_spike;
		std::set<std::string> g_closed;   // nodes closed by the user
		std::set<std::string> g_opened;   // nodes deeper than 3 opened by the user

		bool IsOpen(const std::string& key, int depth)
		{
			if (g_closed.count(key))
				return false;
			return depth < 3 || g_opened.count(key);
		}

		void FillTree(Table& t, const std::vector<ZoneStats>& zones, double frame, bool spike)
		{
			t.rows.clear();
			t.frame = std::max(frame, 0.001);
			const bool filtering = !g_filter.empty();
			int skip_below = -1;
			for (int index : SortedTreeOrder(zones, g_sort_by_time && !filtering))
			{
				const ZoneStats& z = zones[static_cast<size_t>(index)];
				if (filtering)
				{
					if (!ContainsNoCase(z.name, g_filter.c_str()))
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
					if (!spike && g_hide_idle && Idle(z))
					{
						if (z.has_children)
							skip_below = z.depth;
						continue;
					}
				}
				Row r;
				r.name = z.name;
				r.key = PathOf(zones, index);
				r.depth = filtering ? 0 : z.depth;
				r.has_children = !filtering && z.has_children;
				r.open = r.has_children && IsOpen(r.key, z.depth);
				const double value = spike ? z.last_ms : z.average_ms;
				const double self = spike ? z.self_last_ms : z.self_average_ms;
				r.value[0] = value;
				r.value[1] = self;
				r.value[2] = z.last_ms;
				r.value[3] = z.max_ms;
				r.calls = z.calls;
				r.share = std::clamp(value / t.frame, 0.0, 1.0);
				r.self_share = std::clamp(self / t.frame, 0.0, 1.0);
				t.rows.push_back(std::move(r));
				if (z.has_children && !filtering && !t.rows.back().open)
					skip_below = z.depth;
			}
		}

		void FillHotSpots(Table& t, const std::vector<ZoneStats>& zones, double frame)
		{
			t.rows.clear();
			t.frame = std::max(frame, 0.001);
			for (const HotSpot& h : HotSpots(zones))
			{
				if (!ContainsNoCase(h.name, g_filter.c_str()))
					continue;
				if (g_hide_idle && h.calls == 0 && h.self_ms < 0.001)
					continue;
				Row r;
				r.name = h.name;
				r.key = h.name;
				r.value[0] = h.self_ms;
				r.value[1] = h.total_ms;
				r.value[2] = h.max_ms;
				r.calls = h.calls;
				r.self_share = r.share = std::clamp(h.self_ms / t.frame, 0.0, 1.0);
				t.rows.push_back(std::move(r));
			}
		}

		void PaintTable(const Table& t, RPainter* p, float width, float)
		{
			const int columns = static_cast<int>(t.columns.size());
			const float col_w = 74.f;
			const float name_w = std::max(120.f, width - col_w * static_cast<float>(columns));

			Rdraw_rect(p, 0.f, 0.f, width, kHeaderHeight, 0.f, 0.f, 0.f, 0.f, 0.25f);
			Text(p, 6.f, 3.f, t.tree ? "Zone" : "Zone (by self time)", kDim);
			for (int c = 0; c < columns; ++c)
				Text(p, name_w + col_w * static_cast<float>(c) + 4.f, 3.f, t.columns[static_cast<size_t>(c)], kDim);

			if (t.rows.empty())
				Text(p, 6.f, kHeaderHeight + 4.f, t.empty, kDim);

			float y = kHeaderHeight;
			bool odd = false;
			for (const Row& r : t.rows)
			{
				if (odd)
					Rdraw_rect(p, 0.f, y, width, kRowHeight, 0.f, 1.f, 1.f, 1.f, 0.03f);
				odd = !odd;
				// Share of the frame, behind the name (the self part darker).
				Rdraw_rect(p, 0.f, y + 1.f, name_w * static_cast<float>(r.share), kRowHeight - 2.f, 0.f, kBar.r, kBar.g, kBar.b, 0.18f);
				Rdraw_rect(p, 0.f, y + 1.f, name_w * static_cast<float>(r.self_share), kRowHeight - 2.f, 0.f, kBar.r, kBar.g, kBar.b, 0.35f);
				const float indent = 6.f + 14.f * static_cast<float>(r.depth);
				if (r.has_children)
					Text(p, indent, y + 2.f, r.open ? "v" : ">", kDim);
				Text(p, indent + 14.f, y + 2.f, r.name, kText);
				for (int c = 0; c < columns - 1; ++c)
				{
					const double v = r.value[c];
					Text(p, name_w + col_w * static_cast<float>(c) + 4.f, y + 2.f, Ms(v), v < 0.0005 ? kDim : TimeColor(v, t.frame));
				}
				Text(p, name_w + col_w * static_cast<float>(columns - 1) + 4.f, y + 2.f, std::to_string(r.calls), kText);
				y += kRowHeight;
			}
		}

		void BindTable(Table& t, Rid widget)
		{
			t.widget = widget;
			Table* table = &t;
			ui::OnPaint(widget, [table](RPainter* p, float w, float h) { PaintTable(*table, p, w, h); });
			// A click on a node opens / closes it.
			ui::OnEvent(widget, [table](const REvent& e)
			{
				if (e.type != R_EVENT_MOUSE_DOWN || e.button != R_MOUSE_LEFT || !table->tree)
					return false;
				const int row = static_cast<int>((e.local_y - kHeaderHeight) / kRowHeight);
				if (row < 0 || row >= static_cast<int>(table->rows.size()))
					return false;
				const Row& r = table->rows[static_cast<size_t>(row)];
				if (!r.has_children)
					return false;
				if (r.open)
				{
					g_closed.insert(r.key);
					g_opened.erase(r.key);
				}
				else
				{
					g_closed.erase(r.key);
					g_opened.insert(r.key);
				}
				return true;
			});
		}

		void Resize(Table& t)
		{
			Rwidget_set_size(t.widget, -1.f, kHeaderHeight + kRowHeight * static_cast<float>(std::max<size_t>(1, t.rows.size())) + 4.f);
			Rwidget_invalidate(t.widget);
		}

		// Frame time graph with the 60 fps / 30 fps / spike lines.
		void PaintGraph(RPainter* p, float width, float height)
		{
			Rdraw_rect(p, 0.f, 0.f, width, height, 4.f, 0.f, 0.f, 0.f, 0.3f);
			const std::vector<float>& frames = g_snapshot.frame_ms;
			if (frames.empty())
				return;
			float top = 33.3f * 1.25f;
			for (float f : frames)
				top = std::max(top, f * 1.1f);
			top = std::min(top, 250.f);
			auto y_of = [&](float ms) { return height - std::min(ms, top) / top * height; };
			const float bar = width / 300.f;
			const float start_x = width - bar * static_cast<float>(frames.size());
			for (size_t i = 0; i < frames.size(); ++i)
			{
				const float ms = frames[i];
				const Rgba c = ms <= 16.8f ? kOk : ms <= 33.4f ? kWarn : kBad;
				const float x = start_x + bar * static_cast<float>(i);
				Rdraw_rect(p, x, y_of(ms), std::max(1.f, bar - 0.5f), height - y_of(ms), 0.f, c.r, c.g, c.b, c.a);
			}
			for (const auto& [ms, label] : { std::pair<float, const char*>{ 16.7f, "60 fps" }, { 33.3f, "30 fps" } })
			{
				if (ms > top)
					continue;
				const float y = y_of(ms);
				Rdraw_line(p, 0.f, y, width, y, 1.f, kDim.r, kDim.g, kDim.b, 0.8f);
				Text(p, 4.f, y - 16.f, label, kDim);
			}
			const float spike_ms = static_cast<float>(lynx::profiler::GetSpikeThreshold());
			if (spike_ms > 0.f && spike_ms <= top)
			{
				const float y = y_of(spike_ms);
				Rdraw_line(p, 0.f, y, width, y, 1.f, kBad.r, kBad.g, kBad.b, 0.6f);
				Text(p, width - 46.f, y - 16.f, "spike", kBad);
			}
		}

		// ---------------------------------------------------------------------
		// Window
		// ---------------------------------------------------------------------

		Rid g_window = R_INVALID_ID;
		bool g_shown = false;
		Rid g_graph = R_INVALID_ID, g_fps = R_INVALID_ID, g_paused_label = R_INVALID_ID, g_pause = R_INVALID_ID;
		Rid g_counters = R_INVALID_ID, g_tracy = R_INVALID_ID, g_tracy_open = R_INVALID_ID, g_tracy_get = R_INVALID_ID;
		Rid g_spike_ms = R_INVALID_ID, g_spike_info = R_INVALID_ID, g_tabs = R_INVALID_ID;
		fs::path g_tracy_path;

		int OnClose(Rid window, void*)
		{
			Rwindow_hide(window);
			g_shown = false;
			lynx::profiler::SetEnabled(false);
			return R_FALSE;
		}

		void Create()
		{
			if (g_window != R_INVALID_ID)
				return;
			g_window = ui::LoadWindow("windows/profiler.xml");
			if (g_window == R_INVALID_ID)
				return;
			auto find = [](const char* n) { return ui::Find(g_window, n); };
			g_graph = find("pf.graph");
			g_fps = find("pf.fps");
			g_paused_label = find("pf.paused");
			g_pause = find("pf.pause");
			g_counters = find("pf.counters");
			g_tracy = find("pf.tracy");
			g_tracy_open = find("pf.tracy.open");
			g_tracy_get = find("pf.tracy.get");
			g_spike_ms = find("pf.spike");
			g_spike_info = find("pf.spike.info");
			g_tabs = find("pf.tabs");

			g_tree.columns = { "Average", "Self", "Last", "Max (2 s)", "Calls" };
			g_tree.empty = "Recording... (zones appear after one frame)";
			g_hot.columns = { "Self", "Total", "Max (2 s)", "Calls" };
			g_hot.tree = false;
			g_hot.empty = "Nothing yet.";
			g_spike.columns = { "Time", "Self", "Calls" };
			g_spike.empty = "No spike yet.";
			BindTable(g_tree, find("pf.tree"));
			BindTable(g_hot, find("pf.hot"));
			BindTable(g_spike, find("pf.spike.table"));
			ui::OnPaint(g_graph, PaintGraph);

			g_tracy_path = FindTracy();
			ui::OnClick(g_tracy_open, []() { Open(g_tracy_path); });
			ui::OnClick(g_tracy_get, []()
			{
				g_tracy_path = FindTracy();
				if (g_tracy_path.empty())
					OpenUrl(kTracyReleases);
			});
			ui::OnClick(g_pause, []() { g_paused = !g_paused; });
			ui::OnClick(find("pf.reset"), []()
			{
				lynx::profiler::Reset();
				g_snapshot = {};
				g_seen_spikes = 0;
			});
			ui::OnClick(find("pf.copy"), []() { Rclipboard_set_text(Report(g_snapshot).c_str()); });
			ui::OnText(find("pf.filter"), [](const std::string& text) { g_filter = text; });
			ui::OnToggle(find("pf.sort"), [](bool on) { g_sort_by_time = on; });
			ui::OnToggle(find("pf.idle"), [](bool on) { g_hide_idle = on; });
			ui::SetChecked(find("pf.idle"), g_hide_idle);
			Rid fine = find("pf.fine");
			ui::SetChecked(fine, lynx::profiler::IsDetailed());
			ui::OnToggle(fine, [](bool on)
			{
				lynx::profiler::SetDetailed(on);
				lynx::profiler::Reset();
				g_snapshot = {};
				g_seen_spikes = 0;
			});
			Rspin_box_set_value(g_spike_ms, lynx::profiler::GetSpikeThreshold());
			ui::OnValue(g_spike_ms, [](double v) { lynx::profiler::SetSpikeThreshold(static_cast<float>(v)); });
			ui::OnToggle(find("pf.freeze"), [](bool on) { g_freeze_on_spike = on; });
			Rwindow_set_close_callback(g_window, OnClose, nullptr);
		}
	}


	void Show()
	{
		Create();
		if (g_window == R_INVALID_ID)
			return;
		Rwindow_show(g_window);
		g_shown = true;
	}


	bool IsOpen()
	{
		return g_shown;
	}


	void Update()
	{
		// The built-in profiler records only while the window is open.
		if (!g_shown || g_window == R_INVALID_ID)
		{
			lynx::profiler::SetEnabled(false);
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

		// Tracy.
		const bool available = lynx::profiler::TracyAvailable();
		const bool connected = lynx::profiler::TracyConnected();
		ui::SetText(g_tracy, !available ? "Tracy : not compiled in (CMake option LYNX_PROFILER_TRACY)"
		                     : connected ? "Tracy : connected, recording"
		                                 : "Tracy : waiting for a connection (on demand, no cost until then)");
		ui::SetClass(g_tracy, "success", connected);
		ui::SetVisible(g_tracy_open, available && !g_tracy_path.empty());
		ui::SetTooltip(g_tracy_open, g_tracy_path.string() + "\nThen \"Connect\" to 127.0.0.1 in Tracy.");
		ui::SetVisible(g_tracy_get, available && g_tracy_path.empty());

		// Frame.
		const double average = g_snapshot.average_frame_ms;
		char fps[128];
		std::snprintf(fps, sizeof(fps), "%.1f fps    frame %.2f ms (average)   worst %.2f ms",
		              average > 0.0 ? 1000.0 / average : 0.0, average, g_snapshot.max_frame_ms);
		ui::SetText(g_fps, fps);
		ui::SetVisible(g_paused_label, g_paused);
		ui::SetText(g_pause, g_paused ? "Resume" : "Pause");
		Rwidget_invalidate(g_graph);

		std::string counters;
		for (const auto& c : g_snapshot.counters)
		{
			char line[160];
			if (c.value == static_cast<double>(static_cast<long long>(c.value)))
				std::snprintf(line, sizeof(line), "%s %lld%s     ", c.name.c_str(), static_cast<long long>(c.value), c.per_frame ? "/f" : "");
			else
				std::snprintf(line, sizeof(line), "%s %.2f%s     ", c.name.c_str(), c.value, c.per_frame ? "/f" : "");
			counters += line;
		}
		ui::SetText(g_counters, counters);
		ui::SetVisible(g_counters, !counters.empty());

		// Tables : only the tab shown.
		const lynx::profiler::Spike& spike = g_snapshot.spike;
		char spike_info[160];
		if (spike.valid)
			std::snprintf(spike_info, sizeof(spike_info), "Last spike : %.0f ms, %.0f s ago (%d spikes since Reset).",
			              spike.frame_ms, spike.seconds_ago, spike.count);
		else
			std::snprintf(spike_info, sizeof(spike_info), "No frame slower than %.0f ms yet.", lynx::profiler::GetSpikeThreshold());
		ui::SetText(g_spike_info, spike_info);

		switch (Rtab_view_get_active(g_tabs))
		{
		case 0: FillTree(g_tree, g_snapshot.zones, average, false); Resize(g_tree); break;
		case 1: FillHotSpots(g_hot, g_snapshot.zones, average); Resize(g_hot); break;
		default: FillTree(g_spike, spike.zones, spike.frame_ms, true); Resize(g_spike); break;
		}
	}
}
