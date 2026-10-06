#include "ShellIntegration.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601   // RegDeleteTreeW
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <string>

namespace lynx::editor::shell_integration
{
	namespace
	{
		// Folder : Directory\shell ; empty part of an open folder : Directory\Background\shell.
		const wchar_t* const kKeys[] = {
			L"Software\\Classes\\Directory\\shell\\Lynx",
			L"Software\\Classes\\Directory\\Background\\shell\\Lynx",
		};

		bool SetString(HKEY key, const wchar_t* name, const std::wstring& value)
		{
			return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
			                      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
		}

		std::wstring EditorExe()
		{
			std::wstring path(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
				if (n == 0)
					return {};
				if (n < path.size())
				{
					path.resize(n);
					return path;
				}
				path.resize(path.size() * 2);
			}
		}
	}

	bool Register()
	{
		const std::wstring exe = EditorExe();
		if (exe.empty())
			return false;

		const std::wstring command = L"\"" + exe + L"\" \"%V\"";
		bool ok = true;

		for (const wchar_t* path : kKeys)
		{
			HKEY key = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
			{
				ok = false;
				continue;
			}
			ok &= SetString(key, nullptr, L"Open with Lynx");
			ok &= SetString(key, L"Icon", L"\"" + exe + L"\",0");
			RegCloseKey(key);

			HKEY cmd = nullptr;
			const std::wstring cmd_path = std::wstring(path) + L"\\command";
			if (RegCreateKeyExW(HKEY_CURRENT_USER, cmd_path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &cmd, nullptr) != ERROR_SUCCESS)
			{
				ok = false;
				continue;
			}
			ok &= SetString(cmd, nullptr, command);
			RegCloseKey(cmd);
		}

		// Explorer reads the menu again.
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
		return ok;
	}

	void Unregister()
	{
		for (const wchar_t* path : kKeys)
			RegDeleteTreeW(HKEY_CURRENT_USER, path);
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
	}
}
#else
namespace lynx::editor::shell_integration
{
	bool Register() { return false; }
	void Unregister() {}
}
#endif
