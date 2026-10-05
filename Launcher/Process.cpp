#include "Process.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <shellapi.h>
#endif

namespace
{
	// Découpe un flux en lignes ; '\r' et '\n' terminent une ligne.
	struct LineSplitter
	{
		const std::function<void(const std::string&)>& callback;
		std::string current;

		void Feed(const char* data, size_t size)
		{
			for (size_t i = 0; i < size; ++i)
			{
				char c = data[i];
				if (c == '\n' || c == '\r')
					Flush();
				else
					current.push_back(c);
			}
		}
		void Flush()
		{
			if (!current.empty() && callback)
				callback(current);
			current.clear();
		}
	};
}

namespace Process
{
#ifdef _WIN32
	int Run(const std::string& command, const std::function<void(const std::string&)>& on_line)
	{
		SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
		HANDLE read_pipe = nullptr, write_pipe = nullptr;
		if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0))
			return -1;
		SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOA si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = write_pipe;
		si.hStdError = write_pipe;
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

		PROCESS_INFORMATION pi{};
		std::string cmd = "cmd.exe /S /C \"" + command + "\"";
		std::vector<char> buffer(cmd.begin(), cmd.end());
		buffer.push_back('\0');

		BOOL ok = CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, TRUE,
		                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		CloseHandle(write_pipe);
		if (!ok)
		{
			CloseHandle(read_pipe);
			return -1;
		}

		LineSplitter splitter{ on_line, {} };
		char chunk[4096];
		DWORD read = 0;
		while (ReadFile(read_pipe, chunk, sizeof(chunk), &read, nullptr) && read > 0)
			splitter.Feed(chunk, read);
		splitter.Flush();

		WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD exit_code = 0;
		GetExitCodeProcess(pi.hProcess, &exit_code);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(read_pipe);
		return static_cast<int>(exit_code);
	}

	bool Spawn(const std::string& command)
	{
		STARTUPINFOA si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		std::string cmd = "cmd.exe /S /C \"" + command + "\"";
		std::vector<char> buffer(cmd.begin(), cmd.end());
		buffer.push_back('\0');
		BOOL ok = CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, FALSE,
		                         CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi);
		if (!ok)
			return false;
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return true;
	}

	static std::string ReadRegistryPath(HKEY root, const char* key)
	{
		HKEY handle;
		if (RegOpenKeyExA(root, key, 0, KEY_READ, &handle) != ERROR_SUCCESS)
			return {};
		DWORD type = 0, size = 0;
		std::string value;
		if (RegQueryValueExA(handle, "Path", nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0)
		{
			value.resize(size);
			RegQueryValueExA(handle, "Path", nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size);
			value.resize(strlen(value.c_str()));
		}
		RegCloseKey(handle);

		char expanded[32767];
		DWORD length = ExpandEnvironmentStringsA(value.c_str(), expanded, sizeof(expanded));
		return length > 0 && length <= sizeof(expanded) ? std::string(expanded) : value;
	}

	void RefreshPath()
	{
		std::string machine = ReadRegistryPath(HKEY_LOCAL_MACHINE,
			"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
		std::string user = ReadRegistryPath(HKEY_CURRENT_USER, "Environment");
		std::string path = machine;
		if (!user.empty())
			path += (path.empty() ? "" : ";") + user;
		if (!path.empty())
			SetEnvironmentVariableA("PATH", path.c_str());
	}

	void OpenUrl(const std::string& url)
	{
		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
#else
	int Run(const std::string& command, const std::function<void(const std::string&)>& on_line)
	{
		std::string cmd = "(" + command + ") 2>&1";
		FILE* pipe = popen(cmd.c_str(), "r");
		if (!pipe)
			return -1;
		LineSplitter splitter{ on_line, {} };
		char chunk[4096];
		size_t read;
		while ((read = fread(chunk, 1, sizeof(chunk), pipe)) > 0)
			splitter.Feed(chunk, read);
		splitter.Flush();
		int status = pclose(pipe);
		if (status == -1)
			return -1;
		return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	}

	bool Spawn(const std::string& command)
	{
		std::string cmd = "nohup " + command + " >/dev/null 2>&1 &";
		return std::system(cmd.c_str()) == 0;
	}

	void RefreshPath() {}

	void OpenUrl(const std::string& url)
	{
		std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
		std::system(cmd.c_str());
	}
#endif

	std::string Capture(const std::string& command, int* exit_code)
	{
		std::string output;
		int code = Run(command, [&](const std::string& line) { output += line + "\n"; });
		if (exit_code)
			*exit_code = code;
		return output;
	}
}
