#include "ScriptRunner.h"

#include "CommandServer.h"

#include <json/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::script_runner
{
	namespace
	{
		fs::path g_root;
		fs::path g_python_dir;
		std::string g_python = "python";

		fs::path g_script;

		std::mutex g_mutex;
		std::vector<std::string> g_lines;
		std::string g_partial;

		std::thread g_reader;
		std::atomic<bool> g_reader_done{ true };
		bool g_running = false;

#ifdef _WIN32
		HANDLE g_process = nullptr;
		HANDLE g_job = nullptr;
#else
		pid_t g_pid = -1;
		int g_exit_status = 0;
		bool g_exited = false;
#endif

		constexpr size_t kMaxLines = 20000;

		void TrimLines()
		{
			if (g_lines.size() > kMaxLines)
				g_lines.erase(g_lines.begin(), g_lines.begin() + static_cast<long>(g_lines.size() - kMaxLines));
		}

		void AppendOutput(const char* data, size_t size)
		{
			std::lock_guard<std::mutex> lock(g_mutex);

			for (size_t i = 0; i < size; ++i)
			{
				const char c = data[i];

				if (c == '\r')
					continue;

				if (c == '\n')
				{
					g_lines.push_back(std::move(g_partial));
					g_partial.clear();
				}
				else
				{
					g_partial += c;
				}
			}

			TrimLines();
		}

		fs::path SettingsFile()
		{
			return g_root / ".lynx" / "commands.json";
		}

		void LoadSettings()
		{
			try
			{
				std::ifstream file(SettingsFile());

				if (!file)
					return;

				const nlohmann::json settings = nlohmann::json::parse(file);
				const std::string python = settings.value("python", std::string());

				if (!python.empty())
					g_python = python;
			}
			catch (...)
			{
			}
		}

		void SetEnv(const char* name, const std::string& value)
		{
#ifdef _WIN32
			const int length = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
			std::wstring wide_name(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name.data(), length);

			const int value_length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
			std::wstring wide_value(static_cast<size_t>(value_length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide_value.data(), value_length);

			SetEnvironmentVariableW(wide_name.c_str(), wide_value.c_str());
#else
			setenv(name, value.c_str(), 1);
#endif
		}

		std::string GetEnv(const char* name)
		{
			const char* value = std::getenv(name);
			return value ? std::string(value) : std::string();
		}

		// Environment inherited by the script (set in the editor process : the
		// values only matter to the children).
		void PrepareEnvironment()
		{
			static std::string original_python_path = GetEnv("PYTHONPATH");
			static bool saved = false;

			if (!saved)
				saved = true;

#ifdef _WIN32
			const char separator = ';';
#else
			const char separator = ':';
#endif

			std::string python_path = g_python_dir.string();

			if (!original_python_path.empty())
				python_path += separator + original_python_path;

			SetEnv("PYTHONPATH", python_path);
			SetEnv("PYTHONUNBUFFERED", "1");
			SetEnv("PYTHONIOENCODING", "utf-8");
			SetEnv("LYNX_PROJECT", g_root.string());
			SetEnv("LYNX_EDITOR_PORT", std::to_string(command_server::Port()));
			SetEnv("LYNX_EDITOR_TOKEN", command_server::Token());
		}

#ifdef _WIN32
		std::wstring Widen(const std::string& text)
		{
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);

			if (length <= 0)
				return {};

			std::wstring wide(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
			wide.resize(static_cast<size_t>(length - 1));
			return wide;
		}

		// "py -3" style executables are kept as they are ; a path gets quotes.
		std::wstring QuoteExecutable(const std::string& python)
		{
			if (python.find(' ') != std::string::npos &&
				python.find('"') == std::string::npos &&
				!fs::exists(fs::path(Widen(python))))
			{
				return Widen(python);   // "py -3"
			}

			return L"\"" + Widen(python) + L"\"";
		}
#endif
	}


	void Init(const fs::path& project_root, const fs::path& python_dir)
	{
		g_root = project_root;
		g_python_dir = python_dir;
		LoadSettings();
	}


	std::string& PythonExecutable()
	{
		return g_python;
	}


	void SaveSettings()
	{
		std::error_code ec;
		fs::create_directories(SettingsFile().parent_path(), ec);

		nlohmann::json settings = { { "python", g_python } };
		std::ofstream file(SettingsFile(), std::ios::trunc);
		file << settings.dump(4) << "\n";
	}


	fs::path ScriptsFolder()
	{
		return g_root / "commands";
	}


	const fs::path& PythonFolder()
	{
		return g_python_dir;
	}


	bool IsRunning()
	{
		return g_running;
	}


	const fs::path& CurrentScript()
	{
		return g_script;
	}


	bool Run(const fs::path& script, const std::string& arguments)
	{
		if (g_running)
			return false;

		if (g_reader.joinable())
			g_reader.join();

		ClearOutput();

		g_script = script;

		std::error_code ec;
		const fs::path relative = fs::relative(script, g_root, ec);

		AppendOutputLine("> " + g_python + " " + (ec ? script : relative).generic_string() +
		                 (arguments.empty() ? "" : " " + arguments));

		if (!command_server::IsRunning())
			AppendOutputLine("WARNING: the command server is not running : the script cannot reach the editor.");

		PrepareEnvironment();

#ifdef _WIN32
		std::wstring command =
			QuoteExecutable(g_python) + L" -u \"" + script.wstring() + L"\"";

		if (!arguments.empty())
			command += L" " + Widen(arguments);

		SECURITY_ATTRIBUTES inherit{};
		inherit.nLength = sizeof(inherit);
		inherit.bInheritHandle = TRUE;

		HANDLE read_pipe = nullptr;
		HANDLE write_pipe = nullptr;

		if (!CreatePipe(&read_pipe, &write_pipe, &inherit, 0))
		{
			AppendOutputLine("ERROR: CreatePipe failed");
			return false;
		}

		SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

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
			nullptr, command.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(),
			&startup, &info);

		CloseHandle(write_pipe);

		if (input != INVALID_HANDLE_VALUE)
			CloseHandle(input);

		if (!created)
		{
			const DWORD error = GetLastError();
			CloseHandle(read_pipe);

			AppendOutputLine("ERROR: could not start Python (error " + std::to_string(error) + ").");
			AppendOutputLine("Install Python 3 (python.org, check \"Add to PATH\"), or set its path in the "
			                 "\"Python\" field of the Commands window (ex : py, or C:/Python312/python.exe).");
			return false;
		}

		g_job = CreateJobObjectW(nullptr, nullptr);

		if (g_job)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
			AssignProcessToJobObject(g_job, info.hProcess);
		}

		ResumeThread(info.hThread);
		CloseHandle(info.hThread);

		g_process = info.hProcess;
		g_running = true;
		g_reader_done = false;

		g_reader = std::thread([read_pipe]()
		{
			char buffer[4096];
			DWORD read = 0;

			while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
				AppendOutput(buffer, read);

			CloseHandle(read_pipe);
			g_reader_done = true;
		});

		return true;
#else
		int pipe_fds[2];

		if (pipe(pipe_fds) != 0)
		{
			AppendOutputLine("ERROR: pipe failed");
			return false;
		}

		const pid_t pid = fork();

		if (pid < 0)
		{
			close(pipe_fds[0]);
			close(pipe_fds[1]);
			AppendOutputLine("ERROR: fork failed");
			return false;
		}

		if (pid == 0)
		{
			// Child.
			setpgid(0, 0);

			const int null_fd = open("/dev/null", O_RDONLY);

			if (null_fd >= 0)
				dup2(null_fd, 0);

			dup2(pipe_fds[1], 1);
			dup2(pipe_fds[1], 2);
			close(pipe_fds[0]);
			close(pipe_fds[1]);

			if (chdir(g_root.c_str()) != 0)
				_exit(126);

			std::vector<std::string> args = { g_python, "-u", script.string() };

			// Arguments : split on spaces (no quoting on this platform).
			std::string current;

			for (char c : arguments)
			{
				if (c == ' ')
				{
					if (!current.empty())
						args.push_back(current);

					current.clear();
				}
				else
				{
					current += c;
				}
			}

			if (!current.empty())
				args.push_back(current);

			std::vector<char*> argv;

			for (std::string& arg : args)
				argv.push_back(arg.data());

			argv.push_back(nullptr);

			execvp(argv[0], argv.data());
			std::fprintf(stderr, "ERROR: could not start %s\n", g_python.c_str());
			_exit(127);
		}

		close(pipe_fds[1]);

		const int read_fd = pipe_fds[0];

		g_pid = pid;
		g_exited = false;
		g_running = true;
		g_reader_done = false;

		g_reader = std::thread([read_fd]()
		{
			char buffer[4096];

			for (;;)
			{
				const ssize_t count = read(read_fd, buffer, sizeof(buffer));

				if (count <= 0)
					break;

				AppendOutput(buffer, static_cast<size_t>(count));
			}

			close(read_fd);
			g_reader_done = true;
		});

		return true;
#endif
	}


	void Stop()
	{
		if (!g_running)
			return;

		AppendOutputLine("--- Stopped ---");

#ifdef _WIN32
		if (g_job)
			TerminateJobObject(g_job, 1);
		else if (g_process)
			TerminateProcess(g_process, 1);
#else
		if (g_pid > 0)
			kill(-g_pid, SIGKILL);
#endif
	}


	bool PollFinished(int& exit_code)
	{
		if (!g_running)
			return false;

#ifdef _WIN32
		if (WaitForSingleObject(g_process, 0) != WAIT_OBJECT_0 || !g_reader_done)
			return false;

		DWORD code = 1;
		GetExitCodeProcess(g_process, &code);

		CloseHandle(g_process);
		g_process = nullptr;

		if (g_job)
			CloseHandle(g_job);

		g_job = nullptr;
		exit_code = static_cast<int>(code);
#else
		if (!g_exited)
		{
			int status = 0;

			if (waitpid(g_pid, &status, WNOHANG) != g_pid)
				return false;

			g_exited = true;
			g_exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
		}

		if (!g_reader_done)
			return false;

		g_pid = -1;
		exit_code = g_exit_status;
#endif

		if (g_reader.joinable())
			g_reader.join();

		g_running = false;

		{
			std::lock_guard<std::mutex> lock(g_mutex);

			if (!g_partial.empty())
			{
				g_lines.push_back(std::move(g_partial));
				g_partial.clear();
			}
		}

		AppendOutputLine(exit_code == 0
			? "--- Done ---"
			: "--- Failed (exit code " + std::to_string(exit_code) + ") ---");

		return true;
	}


	void ForEachLine(const std::function<void(const std::string&)>& fn)
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		for (const std::string& line : g_lines)
			fn(line);

		if (!g_partial.empty())
			fn(g_partial);
	}


	size_t LineCount()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_lines.size() + (g_partial.empty() ? 0 : 1);
	}


	void ClearOutput()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_lines.clear();
		g_partial.clear();
	}


	void AppendOutputLine(const std::string& line)
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		if (!g_partial.empty())
		{
			g_lines.push_back(std::move(g_partial));
			g_partial.clear();
		}

		g_lines.push_back(line);
		TrimLines();
	}


	void Shutdown()
	{
		if (g_running)
		{
			Stop();

#ifdef _WIN32
			if (g_process)
				WaitForSingleObject(g_process, 3000);
#else
			if (g_pid > 0)
				waitpid(g_pid, nullptr, 0);
#endif
		}

		if (g_reader.joinable())
			g_reader.join();

#ifdef _WIN32
		if (g_process)
			CloseHandle(g_process);

		if (g_job)
			CloseHandle(g_job);

		g_process = nullptr;
		g_job = nullptr;
#endif

		g_running = false;
	}
}
