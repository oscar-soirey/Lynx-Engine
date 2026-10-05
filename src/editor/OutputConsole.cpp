#include "OutputConsole.h"
#include "EditorIcons.h"

#include <imgui/imgui.h>
#include <hrl/hrl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::output_console
{
	namespace
	{
		enum class Kind { Normal, Warning, Error };

		struct Line
		{
			std::string text;
			Kind kind = Kind::Normal;
		};

		constexpr size_t kMaxLines = 20000;

		std::mutex g_mutex;
		std::deque<Line> g_lines;
		std::string g_partial;
		std::ofstream g_file;
		bool g_scroll = false;
		bool g_started = false;

		// Lines waiting to be shown on the screen of the scene (HRL screen
		// messages, like Unreal's on-screen debug messages). Filled by any
		// thread, emptied on the main thread by FlushToScreen().
		constexpr size_t kMaxScreenQueue = 64;
		constexpr size_t kScreenLinesPerFrame = 6;
		constexpr size_t kMaxScreenLineLength = 180;
		const char* const kSettingsFile = "editor_console.txt";
		std::deque<Line> g_screen_queue;
		size_t g_screen_dropped = 0;
		bool g_on_screen = true;
		bool g_on_screen_errors_only = false;
		bool g_settings_loaded = false;

		void LoadSettings()
		{
			g_settings_loaded = true;
			std::ifstream file(kSettingsFile);
			int on_screen = 1, errors_only = 0;
			if (file >> on_screen)
				g_on_screen = on_screen != 0;
			if (file >> errors_only)
				g_on_screen_errors_only = errors_only != 0;
		}

		void SaveSettings()
		{
			std::ofstream file(kSettingsFile, std::ios::trunc);
			if (file)
				file << (g_on_screen ? 1 : 0) << ' ' << (g_on_screen_errors_only ? 1 : 0) << '\n';
		}

		// UI
		bool g_auto_scroll = true;
		bool g_show_normal = true;
		bool g_show_warnings = true;
		bool g_show_errors = true;
		char g_filter[128] = {};

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		Kind Classify(const std::string& text)
		{
			const std::string lower = Lower(text);
			if (lower.find("error") != std::string::npos || lower.find("erreur") != std::string::npos ||
			    lower.find("failed") != std::string::npos || lower.find("exception") != std::string::npos ||
			    lower.find("traceback") != std::string::npos)
				return Kind::Error;
			if (lower.find("warning") != std::string::npos || lower.find("warn") != std::string::npos)
				return Kind::Warning;
			return Kind::Normal;
		}

		// Under g_mutex.
		void AddLine(std::string text)
		{
			if (g_file.is_open())
			{
				g_file << text << '\n';
				g_file.flush();
			}
			const Kind kind = Classify(text);

			const bool blank = text.find_first_not_of(" \t") == std::string::npos;
			if (g_on_screen && !blank && (!g_on_screen_errors_only || kind != Kind::Normal))
			{
				if (g_screen_queue.size() >= kMaxScreenQueue)
				{
					g_screen_queue.pop_front();
					++g_screen_dropped;
				}
				g_screen_queue.push_back({ text, kind });
			}

			g_lines.push_back({ std::move(text), kind });
			while (g_lines.size() > kMaxLines)
				g_lines.pop_front();
			g_scroll = true;
		}

		// Under g_mutex : raw bytes from the pipe -> lines.
		void Feed(const char* data, size_t size)
		{
			for (size_t i = 0; i < size; ++i)
			{
				const char c = data[i];
				if (c == '\r')
					continue;
				if (c == '\n')
				{
					AddLine(std::move(g_partial));
					g_partial.clear();
				}
				else
				{
					g_partial += c;
				}
			}
		}

		fs::path ExecutableDirectory()
		{
#ifdef _WIN32
			wchar_t buffer[MAX_PATH];
			const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
			if (length > 0 && length < MAX_PATH)
				return fs::path(buffer).parent_path();
#endif
			std::error_code ec;
			return fs::current_path(ec);
		}
	}

	void Start()
	{
		if (g_started)
			return;
		g_started = true;

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_file.open(ExecutableDirectory() / "editor_output.log", std::ios::trunc);
		}

#ifdef _WIN32
		HANDLE read_end = nullptr;
		HANDLE write_end = nullptr;
		SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };   // inherited : child processes print here too
		if (!CreatePipe(&read_end, &write_end, &security, 1 << 16))
			return;
		SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

		SetStdHandle(STD_OUTPUT_HANDLE, write_end);
		SetStdHandle(STD_ERROR_HANDLE, write_end);

		// Without a console (-mwindows), stdout / stderr have no valid file
		// descriptor : give them one, then point it to the pipe.
		if (_fileno(stdout) < 0)
			freopen("NUL", "w", stdout);
		if (_fileno(stderr) < 0)
			freopen("NUL", "w", stderr);

		const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(write_end), _O_TEXT | _O_WRONLY);
		if (fd >= 0)
		{
			_dup2(fd, _fileno(stdout));
			_dup2(fd, _fileno(stderr));
		}

		std::thread([read_end]()
		{
			char buffer[4096];
			DWORD read = 0;
			while (ReadFile(read_end, buffer, sizeof(buffer), &read, nullptr) && read > 0)
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				Feed(buffer, read);
			}
		}).detach();
#else
		int fds[2];
		if (pipe(fds) != 0)
			return;
		dup2(fds[1], STDOUT_FILENO);
		dup2(fds[1], STDERR_FILENO);
		close(fds[1]);
		const int read_end = fds[0];

		std::thread([read_end]()
		{
			char buffer[4096];
			ssize_t read_size = 0;
			while ((read_size = read(read_end, buffer, sizeof(buffer))) > 0)
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				Feed(buffer, static_cast<size_t>(read_size));
			}
		}).detach();
#endif

		// Lines appear at once (no buffering until exit / crash).
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		std::setvbuf(stderr, nullptr, _IONBF, 0);
	}

	void Write(const std::string& line)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		AddLine(line);
	}

	void FlushToScreen(unsigned int scene)
	{
		if (!g_settings_loaded)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			LoadSettings();
		}

		std::vector<Line> lines;
		size_t dropped = 0;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (!g_on_screen || scene == HRL_INVALID_ID)
			{
				g_screen_queue.clear();
				g_screen_dropped = 0;
				return;
			}
			// A burst of lines : a few per frame, the oldest are skipped
			// (all of them stay in the Console window).
			while (!g_screen_queue.empty() && lines.size() < kScreenLinesPerFrame)
			{
				lines.push_back(std::move(g_screen_queue.front()));
				g_screen_queue.pop_front();
			}
			dropped = g_screen_dropped;
			g_screen_dropped = 0;
		}

		if (dropped > 0)
		{
			HRL_SetScreenMessageTextColor(scene, 0.75f, 0.75f, 0.75f, 1.f);
			HRL_AddScreenMessage(scene, 4.f, "... %u more lines in the Console window", static_cast<unsigned>(dropped));
		}

		for (const Line& line : lines)
		{
			float duration = 4.f;
			switch (line.kind)
			{
				case Kind::Error:
					HRL_SetScreenMessageTextColor(scene, 1.f, 0.36f, 0.32f, 1.f);
					duration = 8.f;
					break;
				case Kind::Warning:
					HRL_SetScreenMessageTextColor(scene, 1.f, 0.80f, 0.28f, 1.f);
					duration = 6.f;
					break;
				default:
					HRL_SetScreenMessageTextColor(scene, 0.55f, 0.85f, 1.f, 1.f);
					break;
			}

			std::string text = line.text;
			for (char& c : text)
				if (c == '\t')
					c = ' ';
			if (text.size() > kMaxScreenLineLength)
			{
				size_t cut = kMaxScreenLineLength;
				while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
					--cut;   // not in the middle of a UTF-8 character
				text = text.substr(0, cut) + "...";
			}
			HRL_AddScreenMessage(scene, duration, "%s", text.c_str());
		}

		// Back to white for the messages of the game itself.
		if (dropped > 0 || !lines.empty())
			HRL_SetScreenMessageTextColor(scene, 1.f, 1.f, 1.f, 1.f);
	}

	void Draw(bool* open)
	{
		if (open && !*open)
			return;

		ImGui::SetNextWindowSize(ImVec2(760.f, 320.f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Console", open))
		{
			ImGui::End();
			return;
		}

		std::lock_guard<std::mutex> lock(g_mutex);

		int warnings = 0;
		int errors = 0;
		for (const Line& line : g_lines)
		{
			warnings += line.kind == Kind::Warning;
			errors += line.kind == Kind::Error;
		}

		if (icons::ButtonWithLabel("Clear", icons::Icon::Clear))
		{
			g_lines.clear();
			g_partial.clear();
		}
		ImGui::SameLine();
		if (icons::ButtonWithLabel("Copy", icons::Icon::Copy))
		{
			std::string all;
			for (const Line& line : g_lines)
				all += line.text + "\n";
			ImGui::SetClipboardText(all.c_str());
		}
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &g_auto_scroll);
		ImGui::SameLine();
		if (ImGui::Checkbox("On screen", &g_on_screen))
			SaveSettings();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Also show the new lines over the scene (viewport and game), for a few seconds.");
		if (g_on_screen)
		{
			ImGui::SameLine();
			if (ImGui::Checkbox("Errors only", &g_on_screen_errors_only))
				SaveSettings();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("On screen : only the warnings and the errors.");
		}
		ImGui::SameLine();
		ImGui::Checkbox("Log", &g_show_normal);
		ImGui::SameLine();
		const std::string warnings_label = "Warnings (" + std::to_string(warnings) + ")";
		ImGui::Checkbox(warnings_label.c_str(), &g_show_warnings);
		ImGui::SameLine();
		const std::string errors_label = "Errors (" + std::to_string(errors) + ")";
		ImGui::Checkbox(errors_label.c_str(), &g_show_errors);
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		icons::Draw(icons::Icon::Search, 0.f, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x));
		ImGui::InputTextWithHint("##ConsoleFilter", "Filter", g_filter, sizeof(g_filter));

		ImGui::BeginChild("##ConsoleLines", ImVec2(0.f, 0.f), true, ImGuiWindowFlags_HorizontalScrollbar);

		const std::string filter = Lower(g_filter);
		const bool filtered = !filter.empty() || !g_show_normal || !g_show_warnings || !g_show_errors;

		auto visible = [&](const Line& line)
		{
			if ((line.kind == Kind::Normal && !g_show_normal) ||
			    (line.kind == Kind::Warning && !g_show_warnings) ||
			    (line.kind == Kind::Error && !g_show_errors))
				return false;
			return filter.empty() || Lower(line.text).find(filter) != std::string::npos;
		};

		auto draw_line = [](const Line& line)
		{
			if (line.kind == Kind::Error)
				ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.f), "%s", line.text.c_str());
			else if (line.kind == Kind::Warning)
				ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.40f, 1.f), "%s", line.text.c_str());
			else
				ImGui::TextUnformatted(line.text.c_str());
		};

		if (filtered)
		{
			for (const Line& line : g_lines)
				if (visible(line))
					draw_line(line);
		}
		else
		{
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(g_lines.size()));
			while (clipper.Step())
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
					draw_line(g_lines[static_cast<size_t>(i)]);
		}

		if (g_lines.empty())
			ImGui::TextDisabled("Editor output (printf, std::cout, errors...) appears here.");

		if (g_scroll && g_auto_scroll)
			ImGui::SetScrollHereY(1.f);
		g_scroll = false;

		ImGui::EndChild();
		ImGui::End();
	}
}
