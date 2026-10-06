#include "ProjectInfo.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <future>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::project_info
{
	namespace
	{
		std::mutex g_mutex;
		Project g_project;
		bool g_has_project = false;
		Stats g_stats;
		bool g_has_stats = false;
		std::future<Stats> g_scan;
		double g_scan_time = -1000.0;   // ImGui time of the last scan start

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		long long ToUnix(fs::file_time_type t)
		{
			using namespace std::chrono;
			const auto system = time_point_cast<seconds>(t - fs::file_time_type::clock::now() + system_clock::now());
			return system.time_since_epoch().count();
		}

		long long CreationTime(const fs::path& path)
		{
#ifdef _WIN32
			WIN32_FILE_ATTRIBUTE_DATA data{};
			if (!GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &data))
				return 0;
			ULARGE_INTEGER t;
			t.LowPart = data.ftCreationTime.dwLowDateTime;
			t.HighPart = data.ftCreationTime.dwHighDateTime;
			// 100 ns since 1601 -> seconds since 1970
			return static_cast<long long>(t.QuadPart / 10000000ULL) - 11644473600LL;
#else
			struct stat st{};
			if (stat(path.string().c_str(), &st) != 0)
				return 0;
#if defined(__APPLE__)
			return static_cast<long long>(st.st_birthtime);
#else
			return static_cast<long long>(st.st_ctime);   // no birth time : status change
#endif
#endif
		}

		bool Inside(const fs::path& path, const fs::path& dir)
		{
			if (dir.empty())
				return false;
			auto d = dir.begin();
			auto p = path.begin();
			for (; d != dir.end(); ++d, ++p)
			{
				if (d->empty())
					continue;
				if (p == path.end() || *p != *d)
					return false;
			}
			return true;
		}

		void Row(const char* label, const std::string& value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("%s", label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value.c_str());
		}
	}


	Stats Scan(const fs::path& root, const fs::path& build_dir)
	{
		Stats s;
		s.created = CreationTime(root);
		std::error_code ec;
		const fs::path assets = root / "assets";
		fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
		const fs::recursive_directory_iterator end;
		for (; !ec && it != end; it.increment(ec))
		{
			const fs::directory_entry& entry = *it;
			const fs::path& path = entry.path();
			const std::string name = path.filename().string();
			std::error_code e2;
			if (entry.is_directory(e2))
			{
				// .git, .vs... : not part of the project's content (but counted in the size).
				++s.folders;
				continue;
			}
			if (!entry.is_regular_file(e2))
				continue;
			const std::uintmax_t size = entry.file_size(e2);
			if (e2)
				continue;
			++s.files;
			s.bytes += size;
			const bool in_build = Inside(path, build_dir);
			const bool in_assets = Inside(path, assets);
			if (in_build)
				s.build_bytes += size;
			if (in_assets)
				s.assets_bytes += size;

			bool hidden = false;
			for (const fs::path& part : fs::relative(path, root, e2))
				if (!part.empty() && part.string()[0] == '.')
					hidden = true;
			if (!in_build && !hidden)
			{
				const long long t = ToUnix(entry.last_write_time(e2));
				if (!e2 && t > s.modified)
				{
					s.modified = t;
					s.last_file = fs::relative(path, root, e2).generic_string();
				}
			}
			if (in_build || hidden)
				continue;

			const std::string ext = Lower(path.extension().string());
			if (ext == ".js" || ext == ".py")
				++s.scripts;
			else if (ext == ".xml" && in_assets)
				++s.levels;
			else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".gif")
				++s.images;
			else if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac")
				++s.sounds;
			else if (ext == ".bt" || ext == ".animgraph" || ext == ".widget")
				++s.graphs;
		}
		return s;
	}


	std::string FormatBytes(std::uintmax_t bytes)
	{
		const char* units[] = { "bytes", "KB", "MB", "GB", "TB" };
		double v = static_cast<double>(bytes);
		int u = 0;
		while (v >= 1024.0 && u < 4)
		{
			v /= 1024.0;
			++u;
		}
		char text[32];
		if (u == 0)
			std::snprintf(text, sizeof(text), "%llu bytes", static_cast<unsigned long long>(bytes));
		else
			std::snprintf(text, sizeof(text), v < 10.0 ? "%.1f %s" : "%.0f %s", v, units[u]);
		return text;
	}


	std::string FormatDate(long long unix_time, long long now)
	{
		if (unix_time <= 0)
			return "unknown";
		const std::time_t t = static_cast<std::time_t>(unix_time);
		std::tm tm{};
#ifdef _WIN32
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		char date[32];
		std::strftime(date, sizeof(date), "%d/%m/%Y %H:%M", &tm);

		const long long ago = std::max(0LL, now - unix_time);
		char rel[48];
		if (ago < 60)
			std::snprintf(rel, sizeof(rel), "just now");
		else if (ago < 3600)
			std::snprintf(rel, sizeof(rel), "%lld min ago", ago / 60);
		else if (ago < 86400)
			std::snprintf(rel, sizeof(rel), "%lld hour%s ago", ago / 3600, ago / 3600 > 1 ? "s" : "");
		else if (ago < 86400LL * 60)
			std::snprintf(rel, sizeof(rel), "%lld day%s ago", ago / 86400, ago / 86400 > 1 ? "s" : "");
		else if (ago < 86400LL * 730)
			std::snprintf(rel, sizeof(rel), "%lld months ago", ago / (86400LL * 30));
		else
			std::snprintf(rel, sizeof(rel), "%lld years ago", ago / (86400LL * 365));
		return std::string(date) + "  (" + rel + ")";
	}


	void SetProject(const Project& project)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_project = project;
		g_has_project = true;
		g_has_stats = false;
		g_scan_time = -1000.0;
	}


	void DrawTooltip()
	{
		if (!g_has_project)
		{
			ImGui::TextDisabled("No project");
			return;
		}

		// Background scan : started when the tooltip shows up (again after 20 s).
		const double time = ImGui::GetTime();
		if (g_scan.valid() && g_scan.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			g_stats = g_scan.get();
			g_has_stats = true;
		}
		if (!g_scan.valid() && time - g_scan_time > 20.0)
		{
			g_scan_time = time;
			const fs::path root = g_project.root;
			const fs::path build = g_project.build_dir;
			g_scan = std::async(std::launch::async, [root, build] { return Scan(root, build); });
		}

		const Project& p = g_project;
		ImGui::TextUnformatted(p.name.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("Lynx project");
		ImGui::Separator();

		const long long now = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();

		if (ImGui::BeginTable("##project_info", 2, ImGuiTableFlags_SizingFixedFit))
		{
			Row("Folder", p.root.string());
			if (g_has_stats)
			{
				const Stats& s = g_stats;
				Row("Created", FormatDate(s.created, now));
				Row("Modified", FormatDate(s.modified, now) + (s.last_file.empty() ? "" : "  - " + s.last_file));
				Row("Size", FormatBytes(s.bytes) + "   (assets " + FormatBytes(s.assets_bytes) + ", build " +
				            FormatBytes(s.build_bytes) + ")");
				Row("Content", std::to_string(s.files) + (s.files == 1 ? " file in " : " files in ") + std::to_string(s.folders) +
				               (s.folders == 1 ? " folder" : " folders"));
				auto count = [](int n, const char* one, const char* many)
				{
					return std::to_string(n) + " " + (n == 1 ? one : many);
				};
				Row("", count(s.scripts, "script", "scripts") + ", " + count(s.levels, "level", "levels") + ", " +
				        count(s.images, "image", "images") + ", " + count(s.sounds, "sound", "sounds") + ", " +
				        count(s.graphs, "graph / widget", "graphs / widgets"));
			}
			else
				Row("", "Reading the folder...");
			std::error_code ec;
			if (!p.module_path.empty())
			{
				std::string module = p.module_path.filename().string();
				const auto t = fs::last_write_time(p.module_path, ec);
				if (!ec)
					module += "  - built " + FormatDate(ToUnix(t), now);
				Row("Game module", module);
			}
			else
				Row("Game module", "not built yet");
			if (!p.engine_version.empty())
				Row("Engine", "Lynx " + p.engine_version);
			ImGui::EndTable();
		}
	}


	void Shutdown()
	{
		if (g_scan.valid())
			g_scan.wait();
	}
}
