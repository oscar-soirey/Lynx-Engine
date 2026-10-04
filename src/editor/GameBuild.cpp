#include "GameBuild.h"

#include <imgui/imgui.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::game_build
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		fs::path g_root;
		fs::path g_module;

		// ---------------------------------------------------------------------
		// Build process
		// ---------------------------------------------------------------------

		struct Build
		{
#ifdef _WIN32
			HANDLE process = nullptr;
			HANDLE job = nullptr;      // kills the whole tree (cmd, cmake, make, g++...)
			HANDLE read_pipe = nullptr;
#endif
			std::thread reader;
			std::atomic<bool> reader_done{ true };

			std::mutex mutex;
			std::vector<std::string> lines;   // output, complete lines
			std::string partial;              // last line, not finished yet

			bool running = false;
			bool finished_pending = false;
			bool last_success = false;
			bool has_result = false;
			int exit_code = 0;

			Clock::time_point start_time;
			Clock::time_point end_time;
		};

		Build g_build;
		bool g_window_open = false;
		bool g_scroll_to_bottom = false;

		void AppendOutput(const char* data, size_t size)
		{
			std::lock_guard<std::mutex> lock(g_build.mutex);

			for (size_t i = 0; i < size; ++i)
			{
				const char c = data[i];

				if (c == '\r')
					continue;

				if (c == '\n')
				{
					g_build.lines.push_back(std::move(g_build.partial));
					g_build.partial.clear();
				}
				else
				{
					g_build.partial += c;
				}
			}
		}

		void AppendLine(const std::string& line)
		{
			std::lock_guard<std::mutex> lock(g_build.mutex);

			if (!g_build.partial.empty())
			{
				g_build.lines.push_back(std::move(g_build.partial));
				g_build.partial.clear();
			}

			g_build.lines.push_back(line);
		}

		// ---------------------------------------------------------------------
		// Watcher
		// ---------------------------------------------------------------------

		fs::file_time_type g_loaded_time{};
		bool g_has_loaded_time = false;

		fs::file_time_type g_seen_time{};
		Clock::time_point g_seen_at{};
		bool g_change_pending = false;

		Clock::time_point g_last_check{};

		bool ContainsNoCase(const std::string& text, const char* word)
		{
			const size_t n = std::char_traits<char>::length(word);

			if (text.size() < n)
				return false;

			for (size_t i = 0; i + n <= text.size(); ++i)
			{
				size_t j = 0;

				while (j < n && std::tolower(static_cast<unsigned char>(text[i + j])) == word[j])
					++j;

				if (j == n)
					return true;
			}

			return false;
		}
	}


	// =========================================================================
	// Shadow copy
	// =========================================================================

	fs::path MakeShadowCopy(const fs::path& dll, std::string& error)
	{
		static int counter = 0;
		static bool cleaned = false;

		std::error_code ec;
		fs::path base = fs::temp_directory_path(ec);

		if (ec)
		{
			error = "No temp folder : " + ec.message();
			return {};
		}

		base /= "LynxEditor";

#ifdef _WIN32
		const std::string process_id = std::to_string(GetCurrentProcessId());
#else
		const std::string process_id = "0";
#endif

		// Copies left by previous editors (the loaded ones are locked : the
		// removal fails silently for them).
		if (!cleaned)
		{
			cleaned = true;

			for (const auto& entry : fs::directory_iterator(base, ec))
			{
				std::error_code remove_error;

				if (entry.path().filename() != process_id)
					fs::remove_all(entry.path(), remove_error);
			}
		}

		const fs::path dir = base / process_id;
		fs::create_directories(dir, ec);

		if (ec)
		{
			error = "Could not create " + dir.string() + " : " + ec.message();
			return {};
		}

		const fs::path target =
			dir / (dll.stem().string() + "_" + std::to_string(++counter) + dll.extension().string());

		// The linker may still hold the file a few moments.
		for (int attempt = 0; attempt < 20; ++attempt)
		{
			ec.clear();

			if (fs::copy_file(dll, target, fs::copy_options::overwrite_existing, ec))
				return target;

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		error = "Could not copy " + dll.string() + " : " + ec.message();
		return {};
	}


	// =========================================================================
	// Build
	// =========================================================================

	void Init(const fs::path& project_root, const fs::path& module_path)
	{
		g_root = project_root;
		g_module = module_path;
	}


	bool IsRunning()
	{
		return g_build.running;
	}


	bool Start()
	{
		if (g_build.running)
			return true;

		g_window_open = true;
		g_scroll_to_bottom = true;

		{
			std::lock_guard<std::mutex> lock(g_build.mutex);
			g_build.lines.clear();
			g_build.partial.clear();
		}

		g_build.has_result = false;

#ifdef _WIN32
		if (g_build.reader.joinable())
			g_build.reader.join();

		// Build.bat of the project if there is one, else CMake directly.
		const fs::path build_bat = g_root / "Build.bat";
		std::error_code ec;

		std::wstring command;

		if (fs::is_regular_file(build_bat, ec))
		{
			AppendLine("> " + build_bat.string());
			command = L"cmd.exe /d /s /c \"\"" + build_bat.wstring() + L"\"\"";
		}
		else
		{
			if (fs::is_regular_file(g_root / "build" / "CMakeCache.txt", ec))
			{
				AppendLine("> cmake --build build   (no Build.bat in the project)");
				command = L"cmd.exe /d /s /c \"cmake --build build\"";
			}
			else
			{
				// First build : configure the project first.
				AppendLine("> cmake -S . -B build && cmake --build build   (no Build.bat in the project)");
				command = L"cmd.exe /d /s /c \"cmake -S . -B build && cmake --build build\"";
			}
		}

		SECURITY_ATTRIBUTES inherit{};
		inherit.nLength = sizeof(inherit);
		inherit.bInheritHandle = TRUE;

		HANDLE read_pipe = nullptr;
		HANDLE write_pipe = nullptr;

		if (!CreatePipe(&read_pipe, &write_pipe, &inherit, 0))
		{
			AppendLine("ERROR: CreatePipe failed");
			return false;
		}

		// Only the child gets the write end.
		SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

		// No input : a "pause" in Build.bat returns immediately.
		HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		                           &inherit, OPEN_EXISTING, 0, nullptr);

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = input;
		startup.hStdOutput = write_pipe;
		startup.hStdError = write_pipe;

		PROCESS_INFORMATION info{};
		const std::wstring directory = g_root.wstring();

		const BOOL created = CreateProcessW(
			nullptr,
			command.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED,
			nullptr,
			directory.c_str(),
			&startup,
			&info);

		CloseHandle(write_pipe);

		if (input != INVALID_HANDLE_VALUE)
			CloseHandle(input);

		if (!created)
		{
			CloseHandle(read_pipe);
			AppendLine("ERROR: could not start the build (error " + std::to_string(GetLastError()) + ")");
			g_build.has_result = true;
			g_build.last_success = false;
			return false;
		}

		// Job : "Cancel" kills cmd AND everything it started.
		g_build.job = CreateJobObjectW(nullptr, nullptr);

		if (g_build.job)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(g_build.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
			AssignProcessToJobObject(g_build.job, info.hProcess);
		}

		ResumeThread(info.hThread);
		CloseHandle(info.hThread);

		g_build.process = info.hProcess;
		g_build.read_pipe = read_pipe;
		g_build.running = true;
		g_build.start_time = Clock::now();
		g_build.reader_done = false;

		g_build.reader = std::thread([read_pipe]()
		{
			char buffer[4096];
			DWORD read = 0;

			while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
				AppendOutput(buffer, read);

			g_build.reader_done = true;
		});

		std::cout << "[BUILD] Started\n";
		return true;
#else
		AppendLine("Building from the editor is only available on Windows.");
		g_build.has_result = true;
		g_build.last_success = false;
		return false;
#endif
	}


	void Cancel()
	{
#ifdef _WIN32
		if (!g_build.running)
			return;

		AppendLine("--- Cancelled ---");

		if (g_build.job)
			TerminateJobObject(g_build.job, 1);
		else if (g_build.process)
			TerminateProcess(g_build.process, 1);
#endif
	}


	bool PollFinished(bool& success)
	{
#ifdef _WIN32
		if (g_build.running && g_build.process)
		{
			// Done when the process exited AND its whole output was read.
			if (WaitForSingleObject(g_build.process, 0) == WAIT_OBJECT_0 && g_build.reader_done)
			{
				DWORD code = 1;
				GetExitCodeProcess(g_build.process, &code);

				if (g_build.reader.joinable())
					g_build.reader.join();

				CloseHandle(g_build.process);
				CloseHandle(g_build.read_pipe);

				if (g_build.job)
					CloseHandle(g_build.job);

				g_build.process = nullptr;
				g_build.read_pipe = nullptr;
				g_build.job = nullptr;

				g_build.running = false;
				g_build.exit_code = static_cast<int>(code);
				g_build.last_success = (code == 0);
				g_build.has_result = true;
				g_build.finished_pending = true;
				g_build.end_time = Clock::now();

				const double seconds =
					std::chrono::duration<double>(g_build.end_time - g_build.start_time).count();

				char summary[128];

				if (code == 0)
					std::snprintf(summary, sizeof(summary), "--- Build succeeded (%.1f s) ---", seconds);
				else
					std::snprintf(summary, sizeof(summary), "--- Build FAILED (exit code %lu) ---",
					              static_cast<unsigned long>(code));

				AppendLine(summary);
				std::cout << "[BUILD] " << summary << "\n";

				// Show the errors.
				if (code != 0)
					g_window_open = true;
			}
		}
#endif

		if (!g_build.finished_pending)
			return false;

		g_build.finished_pending = false;
		success = g_build.last_success;
		return true;
	}


	std::string ToolbarStatus()
	{
		if (g_build.running)
		{
			static const char* const dots[] = { "", ".", "..", "..." };
			const double t = std::chrono::duration<double>(Clock::now() - g_build.start_time).count();
			return std::string("Building") + dots[static_cast<int>(t * 3.0) % 4];
		}

		if (!g_build.has_result)
			return {};

		const double since = std::chrono::duration<double>(Clock::now() - g_build.end_time).count();

		if (!g_build.last_success)
			return "Build failed";

		return since < 4.0 ? "Build succeeded" : std::string();
	}


	void Shutdown()
	{
#ifdef _WIN32
		if (g_build.running)
		{
			Cancel();

			if (g_build.process)
				WaitForSingleObject(g_build.process, 5000);
		}
#endif

		// The reader ends when the pipe is closed (process tree killed).
		if (g_build.reader.joinable())
			g_build.reader.join();
	}


	bool& WindowOpen()
	{
		return g_window_open;
	}


	void DrawOutput()
	{
		ImGui::BeginChild("##BuildOutput", ImVec2(0.f, 0.f), true, ImGuiWindowFlags_HorizontalScrollbar);

		{
			std::lock_guard<std::mutex> lock(g_build.mutex);

			const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;
			const int count = static_cast<int>(g_build.lines.size());

			ImGuiListClipper clipper;
			clipper.Begin(count + (g_build.partial.empty() ? 0 : 1));

			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
				{
					const std::string& line = i < count ? g_build.lines[i] : g_build.partial;

					ImVec4 color;
					bool colored = true;

					if (ContainsNoCase(line, "error") || ContainsNoCase(line, "failed"))
						color = ImVec4(0.95f, 0.45f, 0.45f, 1.f);
					else if (ContainsNoCase(line, "warning"))
						color = ImVec4(0.95f, 0.80f, 0.35f, 1.f);
					else if (ContainsNoCase(line, "succeeded"))
						color = ImVec4(0.45f, 0.85f, 0.45f, 1.f);
					else
						colored = false;

					if (colored)
						ImGui::PushStyleColor(ImGuiCol_Text, color);

					ImGui::TextUnformatted(line.c_str());

					if (colored)
						ImGui::PopStyleColor();
				}
			}

			// Follow the output while the view is at the bottom.
			if (g_scroll_to_bottom || (at_bottom && g_build.running))
			{
				ImGui::SetScrollHereY(1.f);
				g_scroll_to_bottom = false;
			}
		}

		ImGui::EndChild();
	}


	std::vector<std::string> OutputTail(size_t max_lines)
	{
		std::lock_guard<std::mutex> lock(g_build.mutex);

		std::vector<std::string> lines;
		const size_t total = g_build.lines.size() + (g_build.partial.empty() ? 0 : 1);
		const size_t first = total > max_lines ? total - max_lines : 0;

		for (size_t i = first; i < g_build.lines.size(); ++i)
			lines.push_back(g_build.lines[i]);

		if (!g_build.partial.empty())
			lines.push_back(g_build.partial);

		return lines;
	}


	bool LastBuildSucceeded()
	{
		return g_build.has_result && g_build.last_success;
	}


	void DrawWindow()
	{
		if (!g_window_open)
			return;

		ImGui::SetNextWindowSize(ImVec2(820.f, 420.f), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Build", &g_window_open))
		{
			ImGui::End();
			return;
		}

		ImGui::BeginDisabled(g_build.running);

		if (ImGui::Button("Compile"))
			Start();

		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!g_build.running);

		if (ImGui::Button("Cancel"))
			Cancel();

		ImGui::EndDisabled();

		ImGui::SameLine();

		if (ImGui::Button("Copy"))
		{
			std::string all;
			{
				std::lock_guard<std::mutex> lock(g_build.mutex);

				for (const std::string& line : g_build.lines)
					all += line + "\n";

				all += g_build.partial;
			}

			ImGui::SetClipboardText(all.c_str());
		}

		ImGui::SameLine();

		const std::string status = ToolbarStatus();
		ImGui::TextDisabled("%s", status.c_str());

		ImGui::Separator();

		DrawOutput();

		ImGui::End();
	}


	// =========================================================================
	// Watch build/<game>.dll
	// =========================================================================

	void MarkModuleLoaded()
	{
		std::error_code ec;
		g_loaded_time = fs::last_write_time(g_module, ec);
		g_has_loaded_time = !ec;
		g_change_pending = false;
	}


	bool ModuleChangedOnDisk()
	{
		if (!g_has_loaded_time || g_build.running)
			return false;

		// A stat twice per second is enough.
		const Clock::time_point now = Clock::now();

		if (now - g_last_check < std::chrono::milliseconds(500))
			return false;

		g_last_check = now;

		std::error_code ec;
		const fs::file_time_type time = fs::last_write_time(g_module, ec);

		if (ec || time == g_loaded_time)
		{
			g_change_pending = false;
			return false;
		}

		// Changed : wait until the file stays the same for one second.
		if (!g_change_pending || time != g_seen_time)
		{
			g_change_pending = true;
			g_seen_time = time;
			g_seen_at = now;
			return false;
		}

		return now - g_seen_at >= std::chrono::seconds(1);
	}
}
