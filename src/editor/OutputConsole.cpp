#include "OutputConsole.h"
#include <hrl/hrl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
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
			uint64_t sequence = 0;   // 1, 2, 3... (GetSince)
		};

		uint64_t g_sequence = 0;

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

		FixHandler g_fix_handler;

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
				g_screen_queue.push_back({ text, kind, 0 });
			}

			g_lines.push_back({ std::move(text), kind, ++g_sequence });
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

	void SetFixHandler(FixHandler handler)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_fix_handler = std::move(handler);
	}

	std::string BuildFixPrompt(const std::vector<std::string>& lines, size_t error_index, std::string* display)
	{
		if (error_index >= lines.size())
			return {};

		// The error, then its stack ("    at Update (classes/Wanderer.js:12:5)") and the
		// lines that continue it (indented), at most 14.
		std::string block = lines[error_index];
		size_t end = error_index + 1;
		while (end < lines.size() && end < error_index + 14)
		{
			const std::string& next = lines[end];
			const size_t first = next.find_first_not_of(" \t");
			const bool stack = first != std::string::npos &&
			                   (next.compare(first, 3, "at ") == 0 || first > 0);
			if (!stack)
				break;
			block += "\n" + next;
			++end;
		}
		// A few lines before : what the game was doing (the error is often explained there).
		std::string before;
		for (size_t i = error_index >= 3 ? error_index - 3 : 0; i < error_index; ++i)
			if (lines[i].find_first_not_of(" \t") != std::string::npos)
				before += lines[i] + "\n";

		// Script file and line : "Wanderer.js:12", "classes/Wanderer.js:12:5".
		std::string file, line_number;
		static const std::regex location(R"(([A-Za-z0-9_./\\-]+\.js):(\d+))");
		std::smatch match;
		if (std::regex_search(block, match, location))
		{
			file = match.str(1);
			line_number = match.str(2);
		}

		std::string prompt =
			"The game printed this error in the Console. Find the cause and fix it in the code of the project "
			"(SEARCH / REPLACE blocks). If the cause is not in the code (a missing asset, a setting), explain "
			"what to change instead.\n```\n" + block + "\n```\n";
		if (!before.empty())
			prompt += "Console lines just before :\n```\n" + before + "```\n";
		if (!file.empty())
			prompt += "Script : " + file + (line_number.empty() ? "" : ", line " + line_number) + ".\n";

		if (display)
		{
			std::string first = lines[error_index];
			if (first.size() > 140)
				first = first.substr(0, 137) + "...";
			*display = "Fix this error : `" + first + "`" + (file.empty() ? "" : " (" + file + (line_number.empty() ? "" : ":" + line_number) + ")");
		}
		return prompt;
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

	uint64_t LastSequence()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_sequence;
	}

	void GetSince(uint64_t after, std::vector<ConsoleLine>& out, size_t max_lines)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		size_t first = g_lines.size();
		while (first > 0 && g_lines[first - 1].sequence > after && g_lines.size() - first < max_lines)
			--first;
		for (size_t i = first; i < g_lines.size(); ++i)
			out.push_back({ g_lines[i].text, static_cast<int>(g_lines[i].kind), g_lines[i].sequence });
	}

	std::vector<std::string> AllText()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		std::vector<std::string> lines;
		lines.reserve(g_lines.size());
		for (const Line& line : g_lines)
			lines.push_back(line.text);
		return lines;
	}

	void Clear()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_lines.clear();
	}

	bool OnScreen()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_settings_loaded)
			LoadSettings();
		return g_on_screen;
	}

	bool OnScreenErrorsOnly()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_settings_loaded)
			LoadSettings();
		return g_on_screen_errors_only;
	}

	void SetOnScreen(bool on, bool errors_only)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_settings_loaded = true;
		g_on_screen = on;
		g_on_screen_errors_only = errors_only;
		SaveSettings();
	}

	bool AskFix(const std::vector<std::string>& lines, size_t error_index)
	{
		FixHandler handler;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			handler = g_fix_handler;
		}
		if (!handler)
			return false;
		std::string display;
		const std::string prompt = BuildFixPrompt(lines, error_index, &display);
		if (prompt.empty())
			return false;
		handler(prompt, display);
		return true;
	}

	bool CanFix()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return static_cast<bool>(g_fix_handler);
	}
}
