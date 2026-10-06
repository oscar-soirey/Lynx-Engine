#include "ExternalIde.h"

#include "../host/GameProject.h"
#include "../core/Engine.h"

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
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

	namespace
	{
		fs::path FindExe(Ide ide)
		{
			switch (ide)
			{
			case Ide::VisualStudio: return FindVisualStudio();
			case Ide::VsCode:       return FindVsCode();
			case Ide::CLion:        return FindCLion();
			default:                return {};
			}
		}

		const wchar_t* PathCommand(Ide ide)
		{
			switch (ide)
			{
			case Ide::VisualStudio: return L"devenv";
			case Ide::VsCode:       return L"code";
			case Ide::CLion:        return L"clion";
			default:                return nullptr;
			}
		}

		// Icon of an .exe as RGBA pixels (top-down). false : no icon.
		bool ExtractIconRgba(const fs::path& exe, int size, std::vector<uint8_t>& rgba)
		{
			HICON icon = nullptr;
			if (PrivateExtractIconsW(exe.wstring().c_str(), 0, size, size, &icon, nullptr, 1, 0) == 0 || !icon)
				return false;

			BITMAPINFO info = {};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = size;
			info.bmiHeader.biHeight = -size;   // top-down
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;

			void* bits = nullptr;
			HDC screen = GetDC(nullptr);
			HDC dc = CreateCompatibleDC(screen);
			HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
			bool ok = false;

			if (dib && bits)
			{
				HGDIOBJ old = SelectObject(dc, dib);
				std::memset(bits, 0, static_cast<size_t>(size) * size * 4);
				DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
				SelectObject(dc, old);

				const uint8_t* bgra = static_cast<const uint8_t*>(bits);
				rgba.resize(static_cast<size_t>(size) * size * 4);
				bool any_alpha = false;
				for (int i = 0; i < size * size; ++i)
				{
					rgba[i * 4 + 0] = bgra[i * 4 + 2];
					rgba[i * 4 + 1] = bgra[i * 4 + 1];
					rgba[i * 4 + 2] = bgra[i * 4 + 0];
					rgba[i * 4 + 3] = bgra[i * 4 + 3];
					any_alpha = any_alpha || bgra[i * 4 + 3] != 0;
				}
				// Old icons without alpha : opaque where something was drawn.
				if (!any_alpha)
					for (int i = 0; i < size * size; ++i)
						rgba[i * 4 + 3] = (rgba[i * 4] | rgba[i * 4 + 1] | rgba[i * 4 + 2]) ? 255 : 0;
				ok = true;
			}

			if (dib) DeleteObject(dib);
			DeleteDC(dc);
			ReleaseDC(nullptr, screen);
			DestroyIcon(icon);
			return ok;
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

	namespace
	{
		fs::path FindExe(Ide) { return {}; }
		bool ExtractIconRgba(const fs::path&, int, std::vector<uint8_t>&) { return false; }
	}
#endif

	// =========================================================================
	// Split button support : last used, detection, icons
	// =========================================================================

	namespace
	{
		constexpr int kCount = static_cast<int>(Ide::Count);
		constexpr int kIconSize = 48;

		struct Detected
		{
			bool installed = false;
			std::vector<uint8_t> rgba;   // kIconSize x kIconSize, empty : no icon
		};

		std::mutex g_mutex;
		std::array<Detected, kCount> g_detected;
		std::atomic<bool> g_done{ false };
		bool g_started = false;

		std::array<HRL_id, kCount> g_textures = [] { std::array<HRL_id, kCount> a; a.fill(HRL_INVALID_ID); return a; }();
		std::array<bool, kCount> g_texture_made = {};

		bool g_last_loaded = false;
		Ide g_last = Ide::VsCode;

		fs::path LastUsedFile()
		{
			return host::GetEditorDirectory() / "open_with.txt";
		}

		void StartDetection()
		{
			if (g_started)
				return;
			g_started = true;

			std::thread([]
			{
				for (int i = 0; i < kCount; ++i)
				{
					Detected d;
					const fs::path exe = FindExe(static_cast<Ide>(i));
#ifdef _WIN32
					d.installed = !exe.empty() || OnPath(PathCommand(static_cast<Ide>(i)));
#endif
					// The JetBrains Toolbox script (.cmd) has no icon.
					if (!exe.empty() && exe.extension() == ".exe")
						ExtractIconRgba(exe, kIconSize, d.rgba);

					std::lock_guard<std::mutex> lock(g_mutex);
					g_detected[i] = std::move(d);
				}
				g_done = true;
			}).detach();
		}

		// --- Minimal PNG writer (stored deflate) : HRL_CreateTexture takes an
		//     encoded image. -----------------------------------------------

		uint32_t Crc(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu)
		{
			for (size_t i = 0; i < len; ++i)
			{
				crc ^= data[i];
				for (int k = 0; k < 8; ++k)
					crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
			}
			return crc;
		}

		void Put32(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v >> 24));
			out.push_back(static_cast<uint8_t>(v >> 16));
			out.push_back(static_cast<uint8_t>(v >> 8));
			out.push_back(static_cast<uint8_t>(v));
		}

		void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
		{
			Put32(out, static_cast<uint32_t>(data.size()));
			const size_t start = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), data.begin(), data.end());
			Put32(out, Crc(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu);
		}

		std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& rgba, int w, int h)
		{
			std::vector<uint8_t> raw;
			raw.reserve(static_cast<size_t>(h) * (w * 4 + 1));
			for (int y = 0; y < h; ++y)
			{
				raw.push_back(0);   // filter : none
				raw.insert(raw.end(), rgba.begin() + static_cast<size_t>(y) * w * 4,
				           rgba.begin() + static_cast<size_t>(y + 1) * w * 4);
			}

			// zlib : stored blocks (no compression), then Adler-32.
			std::vector<uint8_t> z = { 0x78, 0x01 };
			for (size_t pos = 0; pos < raw.size() || pos == 0;)
			{
				const size_t n = std::min<size_t>(65535, raw.size() - pos);
				const bool last = pos + n >= raw.size();
				z.push_back(last ? 1 : 0);
				z.push_back(static_cast<uint8_t>(n));
				z.push_back(static_cast<uint8_t>(n >> 8));
				z.push_back(static_cast<uint8_t>(~n));
				z.push_back(static_cast<uint8_t>(~n >> 8));
				z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
				pos += n;
				if (last)
					break;
			}
			uint32_t a = 1, b = 0;
			for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
			Put32(z, (b << 16) | a);

			std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
			std::vector<uint8_t> ihdr;
			Put32(ihdr, static_cast<uint32_t>(w));
			Put32(ihdr, static_cast<uint32_t>(h));
			ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });   // 8 bits, RGBA
			Chunk(png, "IHDR", ihdr);
			Chunk(png, "IDAT", z);
			Chunk(png, "IEND", {});
			return png;
		}
	}

	Ide LastUsed()
	{
		if (!g_last_loaded)
		{
			g_last_loaded = true;
			std::ifstream in(LastUsedFile());
			int v = -1;
			if (in >> v && v >= 0 && v < kCount)
				g_last = static_cast<Ide>(v);
		}
		return g_last;
	}

	void SetLastUsed(Ide ide)
	{
		g_last_loaded = true;
		g_last = ide;
		std::ofstream out(LastUsedFile(), std::ios::trunc);
		if (out)
			out << static_cast<int>(ide) << "\n";
	}

	bool DetectionDone()
	{
		StartDetection();
		return g_done;
	}

	bool IsInstalled(Ide ide)
	{
		if (!DetectionDone())
			return false;
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_detected[static_cast<int>(ide)].installed;
	}

	unsigned int IconTexture(Ide ide)
	{
		const int i = static_cast<int>(ide);
		if (i < 0 || i >= kCount || !DetectionDone())
			return 0;

		// The texture is made on the main thread (HRL / OpenGL).
		if (!g_texture_made[i])
		{
			g_texture_made[i] = true;
			std::vector<uint8_t> rgba;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				rgba = g_detected[i].rgba;
			}
			if (!rgba.empty())
			{
				const std::vector<uint8_t> png = EncodePng(rgba, kIconSize, kIconSize);
				g_textures[i] = HRL_CreateTexture(reinterpret_cast<const char*>(png.data()), png.size());
				// A new texture : the sprites are drawn again (see RequestRenderRefresh).
				if (Engine* engine = Engine::Get())
					engine->RequestRenderRefresh();
			}
		}

		return g_textures[i] != HRL_INVALID_ID ? HRL_GL_GetTextureGL_ID(g_textures[i]) : 0;
	}
}
