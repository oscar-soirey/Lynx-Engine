#include "ExternalIde.h"

#include <cstdlib>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cwctype>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::external_ide
{
	const char* Name(Ide ide)
	{
		switch (ide)
		{
		case Ide::VisualStudio: return "Visual Studio";
		case Ide::VsCode:       return "VS Code";
		case Ide::CLion:        return "CLion";
		default:                return "?";
		}
	}

#ifdef _WIN32
	namespace
	{
		fs::path EnvPath(const wchar_t* name)
		{
			const wchar_t* v = _wgetenv(name);
			return v && *v ? fs::path(v) : fs::path();
		}

		bool Exists(const fs::path& p)
		{
			std::error_code ec;
			return !p.empty() && fs::is_regular_file(p, ec);
		}

		std::wstring Quote(const fs::path& p)
		{
			return L"\"" + p.wstring() + L"\"";
		}

		// Starts `exe` (or a .cmd) with the folder as argument, in the folder.
		bool Launch(const fs::path& exe, const fs::path& folder)
		{
			const std::wstring args = Quote(folder);
			const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
				nullptr, L"open", exe.wstring().c_str(), args.c_str(), folder.wstring().c_str(),
				exe.extension() == L".cmd" || exe.extension() == L".bat" ? SW_HIDE : SW_SHOWNORMAL));
			return result > 32;
		}

		// "code" / "clion" of the PATH (they are .cmd scripts) : through cmd, hidden.
		bool LaunchFromPath(const wchar_t* command, const fs::path& folder)
		{
			const std::wstring args = L"/c " + std::wstring(command) + L" " + Quote(folder);
			const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
				nullptr, L"open", L"cmd.exe", args.c_str(), folder.wstring().c_str(), SW_HIDE));
			return result > 32;
		}

		bool OnPath(const wchar_t* name)
		{
			wchar_t found[MAX_PATH];
			for (const wchar_t* ext : { L".cmd", L".exe", L".bat" })
				if (SearchPathW(nullptr, name, ext, MAX_PATH, found, nullptr) > 0)
					return true;
			return false;
		}

		// Output of a command (vswhere), first line.
		std::wstring FirstLineOf(const std::wstring& command)
		{
			std::wstring out;
			FILE* pipe = _wpopen(command.c_str(), L"rt");
			if (!pipe)
				return out;
			wchar_t buffer[1024];
			if (fgetws(buffer, 1024, pipe))
				out = buffer;
			_pclose(pipe);
			while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' '))
				out.pop_back();
			return out;
		}

		fs::path FindVisualStudio()
		{
			const fs::path vswhere = EnvPath(L"ProgramFiles(x86)") / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
			if (Exists(vswhere))
			{
				const fs::path devenv = FirstLineOf(L"\"" + Quote(vswhere) +
					L" -latest -prerelease -property productPath\"");
				if (Exists(devenv))
					return devenv;
			}
			// Without vswhere : the usual folders.
			for (const fs::path& base : { EnvPath(L"ProgramFiles"), EnvPath(L"ProgramFiles(x86)") })
			{
				std::error_code ec;
				const fs::path root = base / "Microsoft Visual Studio";
				if (base.empty() || !fs::is_directory(root, ec))
					continue;
				for (const auto& year : fs::directory_iterator(root, ec))
					for (const auto& edition : fs::directory_iterator(year.path(), ec))
					{
						const fs::path devenv = edition.path() / "Common7" / "IDE" / "devenv.exe";
						if (Exists(devenv))
							return devenv;
					}
			}
			return {};
		}

		fs::path FindVsCode()
		{
			const fs::path candidates[] = {
				EnvPath(L"LOCALAPPDATA") / "Programs" / "Microsoft VS Code" / "Code.exe",
				EnvPath(L"ProgramFiles") / "Microsoft VS Code" / "Code.exe",
				EnvPath(L"ProgramFiles(x86)") / "Microsoft VS Code" / "Code.exe",
			};
			for (const fs::path& p : candidates)
				if (Exists(p))
					return p;
			return {};
		}

		// Newest clion64.exe under `root` (a few levels : Toolbox apps/CLion/ch-0/<build>/bin).
		void SearchCLionExe(const fs::path& root, int depth, fs::path& best)
		{
			std::error_code ec;
			if (root.empty() || depth < 0 || !fs::is_directory(root, ec))
				return;
			for (const fs::path& exe : { root / "bin" / "clion64.exe", root / "clion64.exe" })
				if (Exists(exe) && (best.empty() || exe > best))
					best = exe;
			for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
			{
				std::error_code ec2;
				if (entry.is_directory(ec2) && !entry.is_symlink(ec2))
					SearchCLionExe(entry.path(), depth - 1, best);
			}
		}

		// Folders whose name starts with "CLion" in `base`.
		void SearchCLionFolders(const fs::path& base, fs::path& best)
		{
			std::error_code ec;
			if (base.empty() || !fs::is_directory(base, ec))
				return;
			for (const auto& entry : fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec))
			{
				std::wstring name = entry.path().filename().wstring();
				for (wchar_t& c : name) c = static_cast<wchar_t>(towlower(c));
				if (name.rfind(L"clion", 0) == 0)
					SearchCLionExe(entry.path(), 3, best);
			}
		}

		std::wstring RegString(HKEY key, const wchar_t* sub, const wchar_t* value)
		{
			wchar_t buffer[2048];
			DWORD size = sizeof(buffer);
			if (RegGetValueW(key, sub, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
				return buffer;
			return {};
		}

		// Installers register CLion in "Uninstall" (InstallLocation) and as an
		// application (Applications\clion64.exe\shell\open\command).
		void SearchCLionRegistry(fs::path& best)
		{
			for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE })
			{
				for (const wchar_t* path : { L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
				                             L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" })
				{
					HKEY key;
					if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
						continue;
					wchar_t name[256];
					for (DWORD i = 0;; ++i)
					{
						DWORD len = 256;
						if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
							break;
						std::wstring display = RegString(key, name, L"DisplayName");
						for (wchar_t& c : display) c = static_cast<wchar_t>(towlower(c));
						if (display.find(L"clion") == std::wstring::npos)
							continue;
						SearchCLionExe(RegString(key, name, L"InstallLocation"), 0, best);
						// DisplayIcon : "...\bin\clion64.exe" (sometimes with ",0").
						std::wstring icon = RegString(key, name, L"DisplayIcon");
						if (const size_t comma = icon.find(L','); comma != std::wstring::npos)
							icon.resize(comma);
						if (!icon.empty() && icon.front() == L'"')
							icon = icon.substr(1, icon.find(L'"', 1) - 1);
						if (Exists(icon) && (best.empty() || fs::path(icon) > best))
							best = icon;
					}
					RegCloseKey(key);
				}
			}

			std::wstring command = RegString(HKEY_CLASSES_ROOT, L"Applications\\clion64.exe\\shell\\open\\command", nullptr);
			if (!command.empty() && command.front() == L'"')
			{
				const fs::path exe = command.substr(1, command.find(L'"', 1) - 1);
				if (Exists(exe) && best.empty())
					best = exe;
			}
		}

		fs::path FindCLion()
		{
			fs::path best;

			// Installers (any drive / folder).
			SearchCLionRegistry(best);
			if (!best.empty())
				return best;

			// JetBrains Toolbox : apps/CLion/ch-0/<build>/bin, Programs/CLion
			// (Toolbox 2), or its scripts.
			const fs::path local = EnvPath(L"LOCALAPPDATA");
			SearchCLionFolders(local / "JetBrains" / "Toolbox" / "apps", best);
			SearchCLionFolders(local / "Programs", best);
			// Standalone : Program Files/JetBrains/CLion 20xx.x.
			SearchCLionFolders(EnvPath(L"ProgramFiles") / "JetBrains", best);
			SearchCLionFolders(EnvPath(L"ProgramFiles(x86)") / "JetBrains", best);
			if (!best.empty())
				return best;

			const fs::path toolbox = local / "JetBrains" / "Toolbox" / "scripts" / "clion.cmd";
			if (Exists(toolbox))
				return toolbox;
			return {};
		}
	}

	bool Open(Ide ide, const fs::path& folder, std::string& error)
	{
		std::error_code ec;
		const fs::path dir = fs::absolute(folder, ec);
		if (!fs::is_directory(dir, ec))
		{
			error = "The game folder does not exist : " + dir.string();
			return false;
		}

		fs::path exe;
		const wchar_t* path_command = nullptr;
		switch (ide)
		{
		case Ide::VisualStudio: exe = FindVisualStudio(); path_command = L"devenv"; break;
		case Ide::VsCode:       exe = FindVsCode();       path_command = L"code";   break;
		case Ide::CLion:        exe = FindCLion();        path_command = L"clion";  break;
		default: break;
		}

		if (!exe.empty())
		{
			if (Launch(exe, dir))
				return true;
			error = "Could not start " + exe.string();
			return false;
		}

		if (path_command && OnPath(path_command))
		{
			if (LaunchFromPath(path_command, dir))
				return true;
		}

		error = std::string(Name(ide)) + " was not found on this computer.";
		return false;
	}
#else
	bool Open(Ide ide, const fs::path&, std::string& error)
	{
		error = std::string("Open with ") + Name(ide) + " : Windows only.";
		return false;
	}
#endif
}
