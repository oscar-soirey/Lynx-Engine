#include "ShipGame.h"

#include "GameBuild.h"
#include "../host/GameProject.h"
#include "../core/Plugins.h"

#include <imgui/imgui.h>

// Implementation compiled in EditorMain.cpp (STB_IMAGE_IMPLEMENTATION).
#include "../../third-party/stb/stb_image.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::ship_game
{
	namespace
	{
		enum class State
		{
			Idle,        // window : settings
			Building,    // compiling the game DLL
			Packing,     // worker thread : archive, copies
			Done,
			Failed
		};

		fs::path g_root;
		std::string g_project_name;

		bool g_open = false;
		State g_state = State::Idle;
		char g_output[1024] = {};
		char g_game_name[128] = {};
		char g_icon[1024] = {};             // .ico / .png, empty = icon of LynxRuntime.exe
		bool g_hide_console = true;
		bool g_compile = true;
		bool g_use_release = true;          // engine of build-release/ when it exists
		bool g_build_started = false;

		std::mutex g_mutex;
		std::string g_status;               // under g_mutex
		std::vector<std::string> g_log;     // under g_mutex
		std::atomic<float> g_progress{ 0.f };
		std::atomic<bool> g_worker_done{ false };
		std::atomic<bool> g_worker_ok{ false };
		std::thread g_worker;

		void Log(const std::string& line)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_log.push_back(line);
			std::cout << "[SHIP] " << line << "\n";
		}

		void Status(const std::string& text)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_status = text;
		}

		std::string ToUtf8(const fs::path& path)
		{
			const auto text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		fs::path FromUtf8(const std::string& text)
		{
			return fs::path(std::u8string(text.begin(), text.end()));
		}

		// "My Game!" -> "MyGame" (a file name).
		std::string SafeFileName(const std::string& name)
		{
			std::string out;
			for (char c : name)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (std::isalnum(u) || c == '-' || c == '_' || c == ' ' || u >= 0x80)
					out += c;
			}
			while (!out.empty() && out.back() == ' ')
				out.pop_back();
			while (!out.empty() && out.front() == ' ')
				out.erase(out.begin());
			return out.empty() ? std::string("Game") : out;
		}

		// ---------------------------------------------------------------------
		// Zip (stored, no compression : PhysFS reads it, and it is fast)
		// ---------------------------------------------------------------------

		uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
		{
			static uint32_t table[256];
			static bool ready = false;
			if (!ready)
			{
				for (uint32_t i = 0; i < 256; ++i)
				{
					uint32_t c = i;
					for (int k = 0; k < 8; ++k)
						c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					table[i] = c;
				}
				ready = true;
			}
			crc = ~crc;
			for (size_t i = 0; i < size; ++i)
				crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
			return ~crc;
		}

		void Put16(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(v >> 8));
		}

		void Put32(std::vector<uint8_t>& out, uint32_t v)
		{
			Put16(out, v & 0xFFFF);
			Put16(out, v >> 16);
		}

		// Every file of `folder` (recursive) into a zip, paths relative to it.
		bool BuildZip(const fs::path& folder, std::vector<uint8_t>& zip, std::string& error)
		{
			struct Entry
			{
				std::string name;
				uint32_t crc = 0;
				uint32_t size = 0;
				uint32_t offset = 0;
			};

			std::vector<fs::path> files;
			std::error_code ec;
			for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
			     !ec && it != end; it.increment(ec))
			{
				std::error_code e;
				if (it->is_regular_file(e))
					files.push_back(it->path());
			}
			if (ec)
			{
				error = "Could not list " + ToUtf8(folder) + " : " + ec.message();
				return false;
			}
			std::sort(files.begin(), files.end());

			std::vector<Entry> entries;
			size_t done = 0;
			for (const fs::path& file : files)
			{
				std::ifstream in(file, std::ios::binary);
				if (!in)
				{
					error = "Could not read " + ToUtf8(file);
					return false;
				}
				std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				if (data.size() > 0xFFFFFFF0u || zip.size() > 0xF0000000u)
				{
					error = "The assets are too big for the archive (4 GB)";
					return false;
				}

				Entry entry;
				entry.name = ToUtf8(fs::relative(file, folder, ec));
				std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
				entry.crc = Crc32(data.data(), data.size());
				entry.size = static_cast<uint32_t>(data.size());
				entry.offset = static_cast<uint32_t>(zip.size());

				// Local file header
				Put32(zip, 0x04034b50);
				Put16(zip, 10);              // version needed
				Put16(zip, 0x0800);          // UTF-8 names
				Put16(zip, 0);               // stored
				Put16(zip, 0); Put16(zip, 0x21);   // time, date (1980-01-01)
				Put32(zip, entry.crc);
				Put32(zip, entry.size);
				Put32(zip, entry.size);
				Put16(zip, static_cast<uint32_t>(entry.name.size()));
				Put16(zip, 0);
				zip.insert(zip.end(), entry.name.begin(), entry.name.end());
				zip.insert(zip.end(), data.begin(), data.end());

				entries.push_back(std::move(entry));
				++done;
				g_progress = 0.1f + 0.5f * static_cast<float>(done) / static_cast<float>(files.size());
			}

			const uint32_t directory_offset = static_cast<uint32_t>(zip.size());
			for (const Entry& entry : entries)
			{
				Put32(zip, 0x02014b50);
				Put16(zip, 20);              // made by
				Put16(zip, 10);              // needed
				Put16(zip, 0x0800);
				Put16(zip, 0);
				Put16(zip, 0); Put16(zip, 0x21);
				Put32(zip, entry.crc);
				Put32(zip, entry.size);
				Put32(zip, entry.size);
				Put16(zip, static_cast<uint32_t>(entry.name.size()));
				Put16(zip, 0);               // extra
				Put16(zip, 0);               // comment
				Put16(zip, 0);               // disk
				Put16(zip, 0);               // internal attributes
				Put32(zip, 0);               // external attributes
				Put32(zip, entry.offset);
				zip.insert(zip.end(), entry.name.begin(), entry.name.end());
			}
			const uint32_t directory_size = static_cast<uint32_t>(zip.size()) - directory_offset;

			Put32(zip, 0x06054b50);
			Put16(zip, 0);
			Put16(zip, 0);
			Put16(zip, static_cast<uint32_t>(entries.size()));
			Put16(zip, static_cast<uint32_t>(entries.size()));
			Put32(zip, directory_size);
			Put32(zip, directory_offset);
			Put16(zip, 0);

			if (entries.size() > 0xFFFF)
			{
				error = "Too many asset files for the archive (65535)";
				return false;
			}
			Log(std::to_string(entries.size()) + " asset files packed (" +
			    std::to_string(zip.size() / 1024) + " KB)");
			return true;
		}

		// Windows executable : console subsystem -> GUI (no console window).
		bool SetGuiSubsystem(std::vector<uint8_t>& exe)
		{
			if (exe.size() < 0x40 || exe[0] != 'M' || exe[1] != 'Z')
				return false;
			const uint32_t pe = exe[0x3C] | (exe[0x3D] << 8) | (exe[0x3E] << 16) | (static_cast<uint32_t>(exe[0x3F]) << 24);
			const size_t subsystem = static_cast<size_t>(pe) + 24 + 68;   // optional header + 68 (PE32 and PE32+)
			if (subsystem + 2 > exe.size() || std::memcmp(&exe[pe], "PE\0\0", 4) != 0)
				return false;
			exe[subsystem] = 2;       // IMAGE_SUBSYSTEM_WINDOWS_GUI
			exe[subsystem + 1] = 0;
			return true;
		}

		bool ReadFile(const fs::path& path, std::vector<uint8_t>& out)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
				return false;
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return true;
		}

		bool CopyInto(const fs::path& file, const fs::path& folder, std::string& error)
		{
			std::error_code ec;
			fs::copy_file(file, folder / file.filename(), fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				error = "Could not copy " + ToUtf8(file.filename()) + " : " + ec.message();
				return false;
			}
			return true;
		}

		// ---------------------------------------------------------------------
		// C++ runtime of the compiler (MinGW) : libLynx.dll, the executable,
		// the game DLL and the plugins need them. On the developer's PC they
		// come from the PATH (MinGW's bin) : a shipped game must carry them,
		// else it does not start on another PC.
		// ---------------------------------------------------------------------

		// Folder of the C++ compiler used for the engine (its CMakeCache.txt).
		fs::path CompilerBinFolder(const fs::path& engine_dir)
		{
			std::ifstream cache(engine_dir / "CMakeCache.txt");
			std::string line;
			while (std::getline(cache, line))
			{
				const std::string key = "CMAKE_CXX_COMPILER:FILEPATH=";
				if (line.rfind(key, 0) == 0)
					return fs::path(line.substr(key.size())).parent_path();
			}
			return {};
		}

		// Copies libstdc++ / libgcc / winpthread next to the game. Found next
		// to the editor, in the compiler's folder or in the PATH. Built with
		// MSVC (none of them anywhere) : nothing to do.
		int CopyCompilerRuntime(const fs::path& engine_dir, const fs::path& output, std::string& warnings)
		{
			std::vector<fs::path> folders = { engine_dir };
			if (fs::path compiler = CompilerBinFolder(engine_dir); !compiler.empty())
				folders.push_back(compiler);
			if (const char* path_env = std::getenv("PATH"))
			{
				std::string paths = path_env;
				size_t start = 0;
				while (start <= paths.size())
				{
					const size_t end = paths.find(';', start);
					const std::string dir = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
					if (!dir.empty())
						folders.emplace_back(dir);
					if (end == std::string::npos)
						break;
					start = end + 1;
				}
			}

			int copied = 0;
			bool any_found = false;
			std::vector<std::string> missing;
			for (const char* name : { "libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll" })
			{
				bool found = false;
				for (const fs::path& folder : folders)
				{
					std::error_code ec;
					const fs::path file = folder / name;
					if (!fs::is_regular_file(file, ec))
						continue;
					found = true;
					fs::copy_file(file, output / name, fs::copy_options::overwrite_existing, ec);
					if (!ec)
						++copied;
					break;
				}
				any_found = any_found || found;
				if (!found)
					missing.emplace_back(name);
			}
			// Some found, some not : MinGW without one of them is suspicious.
			if (any_found && !missing.empty())
				for (const std::string& m : missing)
					warnings += "Warning : " + m + " not found (the game may not start on another PC)\n";
			return copied;
		}

		// ---------------------------------------------------------------------
		// Engine binaries : Release (build-release.bat) or the editor's
		// ---------------------------------------------------------------------

		// <engine>/build-release (next to build/, the folder of the editor).
		// Multi-config generators put the files in a Release/ sub-folder.
		fs::path FindReleaseEngineDir()
		{
			const fs::path editor_dir = host::GetEditorDirectory();
			const fs::path release = editor_dir.parent_path() / "build-release";
			const fs::path candidates[] = { release, release / "Release", editor_dir / "build-release" };

			std::error_code ec;
			for (const fs::path& dir : candidates)
			{
				if (fs::is_regular_file(dir / "LynxRuntime.exe", ec))
					return dir;
			}
			return {};
		}

		// The Release engine is older than the editor's : the engine was
		// changed since build-release.bat (the game DLL may not match it).
		bool ReleaseEngineOutdated(const fs::path& release_dir)
		{
			std::error_code ec1, ec2;
			const auto release_time = fs::last_write_time(release_dir / "LynxRuntime.exe", ec1);
			const auto editor_time = fs::last_write_time(host::GetEditorDirectory() / "LynxRuntime.exe", ec2);
			return !ec1 && !ec2 && release_time < editor_time;
		}


		// ---------------------------------------------------------------------
		// Executable icon (.ico or any image stb_image reads : png, jpg...)
		// ---------------------------------------------------------------------

		struct IconImage
		{
			int width = 0;
			int height = 0;
			uint16_t bit_count = 32;
			std::vector<uint8_t> data;    // RT_ICON data : DIB or PNG
		};

		uint16_t Get16(const std::vector<uint8_t>& d, size_t at)
		{
			return static_cast<uint16_t>(d[at] | (d[at + 1] << 8));
		}

		uint32_t Get32(const std::vector<uint8_t>& d, size_t at)
		{
			return Get16(d, at) | (static_cast<uint32_t>(Get16(d, at + 2)) << 16);
		}

		// .ico : the images are copied as they are (DIB or PNG).
		bool ParseIco(const std::vector<uint8_t>& file, std::vector<IconImage>& out, std::string& error)
		{
			if (file.size() < 6 || Get16(file, 0) != 0 || Get16(file, 2) != 1)
			{
				error = "not a valid .ico file";
				return false;
			}

			const uint16_t count = Get16(file, 4);
			if (count == 0 || file.size() < 6u + 16u * count)
			{
				error = "the .ico file has no image";
				return false;
			}

			for (uint16_t i = 0; i < count; ++i)
			{
				const size_t entry = 6u + 16u * i;
				IconImage image;
				image.width = file[entry] == 0 ? 256 : file[entry];
				image.height = file[entry + 1] == 0 ? 256 : file[entry + 1];
				image.bit_count = Get16(file, entry + 6);
				const uint32_t size = Get32(file, entry + 8);
				const uint32_t offset = Get32(file, entry + 12);

				if (size == 0 || static_cast<uint64_t>(offset) + size > file.size())
				{
					error = "the .ico file is damaged";
					return false;
				}

				if (image.bit_count == 0)
					image.bit_count = 32;

				image.data.assign(file.begin() + offset, file.begin() + offset + size);
				out.push_back(std::move(image));
			}
			return true;
		}

		// Box filter (downscale), nearest pixel (upscale). RGBA.
		std::vector<uint8_t> ResizeRgba(const uint8_t* src, int sw, int sh, int dw, int dh)
		{
			std::vector<uint8_t> out(static_cast<size_t>(dw) * dh * 4);

			for (int y = 0; y < dh; ++y)
			{
				const int y0 = y * sh / dh;
				const int y1 = std::max(y0 + 1, (y + 1) * sh / dh);

				for (int x = 0; x < dw; ++x)
				{
					const int x0 = x * sw / dw;
					const int x1 = std::max(x0 + 1, (x + 1) * sw / dw);

					// Colors weighted by alpha : no dark fringe on the edges.
					double r = 0, g = 0, b = 0, a = 0;
					int n = 0;
					for (int sy = y0; sy < y1; ++sy)
					{
						for (int sx = x0; sx < x1; ++sx)
						{
							const uint8_t* p = src + (static_cast<size_t>(sy) * sw + sx) * 4;
							const double alpha = p[3] / 255.0;
							r += p[0] * alpha;
							g += p[1] * alpha;
							b += p[2] * alpha;
							a += p[3];
							++n;
						}
					}

					uint8_t* d = &out[(static_cast<size_t>(y) * dw + x) * 4];
					const double total_alpha = a / 255.0;
					d[0] = static_cast<uint8_t>(total_alpha > 0 ? std::min(255.0, r / total_alpha + 0.5) : 0);
					d[1] = static_cast<uint8_t>(total_alpha > 0 ? std::min(255.0, g / total_alpha + 0.5) : 0);
					d[2] = static_cast<uint8_t>(total_alpha > 0 ? std::min(255.0, b / total_alpha + 0.5) : 0);
					d[3] = static_cast<uint8_t>(a / n + 0.5);
				}
			}
			return out;
		}

		// 32-bit DIB as stored in an icon : header (height x2), BGRA bottom-up,
		// then the 1-bit AND mask (unused with alpha, all 0).
		std::vector<uint8_t> MakeIconDib(const std::vector<uint8_t>& rgba, int w, int h)
		{
			const uint32_t mask_row = static_cast<uint32_t>((w + 31) / 32) * 4;
			const uint32_t image_size = static_cast<uint32_t>(w * h * 4) + mask_row * h;

			std::vector<uint8_t> dib;
			Put32(dib, 40);                          // biSize
			Put32(dib, static_cast<uint32_t>(w));
			Put32(dib, static_cast<uint32_t>(h * 2));
			Put16(dib, 1);                           // planes
			Put16(dib, 32);                          // bit count
			Put32(dib, 0);                           // BI_RGB
			Put32(dib, image_size);
			Put32(dib, 0); Put32(dib, 0); Put32(dib, 0); Put32(dib, 0);

			for (int y = h - 1; y >= 0; --y)
			{
				for (int x = 0; x < w; ++x)
				{
					const uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
					dib.push_back(p[2]);
					dib.push_back(p[1]);
					dib.push_back(p[0]);
					dib.push_back(p[3]);
				}
			}

			dib.insert(dib.end(), static_cast<size_t>(mask_row) * h, 0);
			return dib;
		}

		// Any image -> the usual Windows sizes (square : centered, transparent).
		bool ImageToIcon(const std::vector<uint8_t>& file, std::vector<IconImage>& out, std::string& error)
		{
			int w = 0, h = 0;
			stbi_uc* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, nullptr, 4);
			if (!pixels)
			{
				error = std::string("could not read the image (") + stbi_failure_reason() + ")";
				return false;
			}

			// Square canvas, the image centered.
			const int side = std::max(w, h);
			std::vector<uint8_t> square(static_cast<size_t>(side) * side * 4, 0);
			for (int y = 0; y < h; ++y)
			{
				std::memcpy(&square[(static_cast<size_t>(y + (side - h) / 2) * side + (side - w) / 2) * 4],
				            pixels + static_cast<size_t>(y) * w * 4, static_cast<size_t>(w) * 4);
			}
			stbi_image_free(pixels);

			for (int size : { 256, 128, 64, 48, 32, 24, 16 })
			{
				IconImage image;
				image.width = size;
				image.height = size;
				image.data = MakeIconDib(ResizeRgba(square.data(), side, side, size, size), size, size);
				out.push_back(std::move(image));
			}
			return true;
		}

		bool LoadIconFile(const fs::path& path, std::vector<IconImage>& out, std::string& error)
		{
			std::vector<uint8_t> file;
			if (!ReadFile(path, file) || file.empty())
			{
				error = "could not read " + ToUtf8(path);
				return false;
			}

			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (extension == ".ico")
				return ParseIco(file, out, error);
			return ImageToIcon(file, out, error);
		}

#ifdef _WIN32
		struct ResourceId
		{
			std::wstring name;     // empty : integer id
			WORD id = 0;
			WORD language = 0;
			LPCWSTR type = nullptr;

			LPCWSTR Name() const { return name.empty() ? MAKEINTRESOURCEW(id) : name.c_str(); }
		};

		BOOL CALLBACK CollectLanguage(HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR param)
		{
			reinterpret_cast<std::vector<WORD>*>(param)->push_back(language);
			return TRUE;
		}

		BOOL CALLBACK CollectName(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
		{
			auto* list = reinterpret_cast<std::vector<ResourceId>*>(param);

			std::vector<WORD> languages;
			EnumResourceLanguagesW(module, type, name, CollectLanguage, reinterpret_cast<LONG_PTR>(&languages));

			for (WORD language : languages)
			{
				ResourceId resource;
				resource.type = type;
				resource.language = language;
				if (IS_INTRESOURCE(name))
					resource.id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
				else
					resource.name = name;
				list->push_back(std::move(resource));
			}
			return TRUE;
		}

		// Replaces every icon of `exe` with `images`, as the group "GLFW_ICON" :
		// Explorer shows it, and GLFW uses it for the window and the taskbar.
		// Before the archive is appended (UpdateResource rewrites the file).
		bool WriteExeIcon(const fs::path& exe, const std::vector<IconImage>& images, std::string& error)
		{
			std::vector<ResourceId> old_resources;
			if (HMODULE module = LoadLibraryExW(exe.wstring().c_str(), nullptr,
			                                    LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE))
			{
				EnumResourceNamesW(module, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */, CollectName,
				                   reinterpret_cast<LONG_PTR>(&old_resources));
				EnumResourceNamesW(module, MAKEINTRESOURCEW(3) /* RT_ICON */, CollectName,
				                   reinterpret_cast<LONG_PTR>(&old_resources));
				FreeLibrary(module);   // before BeginUpdateResource : the file must not be in use
			}

			HANDLE update = BeginUpdateResourceW(exe.wstring().c_str(), FALSE);
			if (!update)
			{
				error = "could not open the executable to change its icon (error " +
				        std::to_string(GetLastError()) + ")";
				return false;
			}

			for (const ResourceId& resource : old_resources)
				UpdateResourceW(update, resource.type, resource.Name(), resource.language, nullptr, 0);

			const WORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

			// GRPICONDIR + GRPICONDIRENTRY (14 bytes each, ids 1..n).
			std::vector<uint8_t> group;
			Put16(group, 0);
			Put16(group, 1);
			Put16(group, static_cast<uint32_t>(images.size()));

			bool ok = true;
			for (size_t i = 0; i < images.size() && ok; ++i)
			{
				const IconImage& image = images[i];
				const WORD id = static_cast<WORD>(i + 1);

				ok = UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(id), language,
				                     const_cast<uint8_t*>(image.data.data()),
				                     static_cast<DWORD>(image.data.size())) != FALSE;

				group.push_back(static_cast<uint8_t>(image.width >= 256 ? 0 : image.width));
				group.push_back(static_cast<uint8_t>(image.height >= 256 ? 0 : image.height));
				group.push_back(0);                  // colors
				group.push_back(0);                  // reserved
				Put16(group, 1);                     // planes
				Put16(group, image.bit_count);
				Put32(group, static_cast<uint32_t>(image.data.size()));
				Put16(group, id);
			}

			if (ok)
			{
				ok = UpdateResourceW(update, MAKEINTRESOURCEW(14), L"GLFW_ICON", language,
				                     group.data(), static_cast<DWORD>(group.size())) != FALSE;
			}

			if (!EndUpdateResourceW(update, ok ? FALSE : TRUE) || !ok)
			{
				error = "could not write the icon into the executable (error " +
				        std::to_string(GetLastError()) + ")";
				return false;
			}
			return true;
		}
#else
		bool WriteExeIcon(const fs::path&, const std::vector<IconImage>&, std::string& error)
		{
			error = "changing the icon of an executable needs Windows";
			return false;
		}
#endif


		// ---------------------------------------------------------------------
		// Settings of the project (<project>/.lynx/ship.cfg)
		// ---------------------------------------------------------------------

		fs::path SettingsFile()
		{
			return g_root / ".lynx" / "ship.cfg";
		}

		void LoadSettings()
		{
			std::ifstream in(SettingsFile());
			std::string line;
			while (std::getline(in, line))
			{
				const size_t equal = line.find('=');
				if (equal == std::string::npos)
					continue;
				const std::string key = line.substr(0, equal);
				const std::string value = line.substr(equal + 1);

				if (key == "name") std::snprintf(g_game_name, sizeof(g_game_name), "%s", value.c_str());
				else if (key == "folder") std::snprintf(g_output, sizeof(g_output), "%s", value.c_str());
				else if (key == "icon") std::snprintf(g_icon, sizeof(g_icon), "%s", value.c_str());
				else if (key == "hide_console") g_hide_console = value == "1";
				else if (key == "compile") g_compile = value == "1";
				else if (key == "release") g_use_release = value == "1";
			}
		}

		void SaveSettings()
		{
			std::error_code ec;
			fs::create_directories(SettingsFile().parent_path(), ec);
			std::ofstream out(SettingsFile(), std::ios::trunc);
			out << "name=" << g_game_name << "\n"
			    << "folder=" << g_output << "\n"
			    << "icon=" << g_icon << "\n"
			    << "hide_console=" << (g_hide_console ? 1 : 0) << "\n"
			    << "compile=" << (g_compile ? 1 : 0) << "\n"
			    << "release=" << (g_use_release ? 1 : 0) << "\n";
		}


		// ---------------------------------------------------------------------
		// Worker : archive + executable + dependencies
		// ---------------------------------------------------------------------

		void Pack(fs::path root, fs::path output, std::string game_name, bool hide_console,
		          fs::path engine_dir, fs::path icon)
		{
			std::string error;
			auto fail = [&](const std::string& message)
			{
				Log("FAILED : " + message);
				Status("Failed : " + message);
				g_worker_ok = false;
				g_worker_done = true;
			};

			// Engine binaries : build-release/ (Release) or the editor's folder.
			const fs::path runtime = engine_dir / "LynxRuntime.exe";
			std::error_code ec;

			if (!fs::is_regular_file(runtime, ec))
				return fail("LynxRuntime.exe not found in " + ToUtf8(engine_dir));
			Log("Engine : " + ToUtf8(engine_dir));

			// Icon first : a bad file stops before anything is written.
			std::vector<IconImage> icon_images;
			if (!icon.empty() && !LoadIconFile(icon, icon_images, error))
				return fail("icon : " + error);

			const auto modules = host::FindGameModules(root / "build", 1);
			fs::path game_dll = modules.empty() ? fs::path() : modules.front();
			if (game_dll.empty())
			{
				// Project without C++ : the generic game DLL of the engine.
				const fs::path script_game = engine_dir / "scriptgame" / "LynxScriptGame.dll";
				if (!host::CanBuildProject(root) && fs::is_regular_file(script_game, ec))
					game_dll = script_game;
				else
					return fail("no game DLL in build/ (the build failed ?)");
			}

			fs::create_directories(output, ec);
			if (!fs::is_directory(output, ec))
				return fail("could not create the folder " + ToUtf8(output));

			// 1. Assets -> zip
			Status("Packing the assets...");
			g_progress = 0.1f;
			std::vector<uint8_t> archive;
			if (!BuildZip(root / "assets", archive, error))
				return fail(error);

			// 2. Executable = runtime + archive
			Status("Writing the executable...");
			std::vector<uint8_t> exe;
			if (!ReadFile(runtime, exe))
				return fail("could not read LynxRuntime.exe");
			if (hide_console && !SetGuiSubsystem(exe))
				Log("Warning : could not remove the console window (not a Windows executable ?)");

			const fs::path exe_path = output / FromUtf8(game_name + ".exe");
			{
				std::ofstream out(exe_path, std::ios::binary | std::ios::trunc);
				if (!out)
					return fail("could not write " + ToUtf8(exe_path) + " (is the game running ?)");
				out.write(reinterpret_cast<const char*>(exe.data()), static_cast<std::streamsize>(exe.size()));
				if (!out)
					return fail("could not write " + ToUtf8(exe_path) + " (disk full ?)");
			}

			// The icon goes in the resources of the executable, BEFORE the
			// archive (UpdateResource rewrites the file and drops what follows
			// the executable).
			if (!icon_images.empty())
			{
				if (!WriteExeIcon(exe_path, icon_images, error))
					return fail(error);
				Log("Icon : " + ToUtf8(icon.filename()) + " (" + std::to_string(icon_images.size()) + " sizes)");
			}

			{
				std::ofstream out(exe_path, std::ios::binary | std::ios::app);
				out.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
				if (!out)
					return fail("could not write " + ToUtf8(exe_path) + " (disk full ?)");
			}
			Log("Executable : " + ToUtf8(exe_path));
			g_progress = 0.75f;

			// 3. Dependencies : the engine DLLs (everything next to the editor),
			//    the game DLL, the key bindings.
			Status("Copying the dependencies...");
			int dll_count = 0;
			for (const auto& entry : fs::directory_iterator(engine_dir, ec))
			{
				std::error_code e;
				if (!entry.is_regular_file(e))
					continue;
				std::string extension = entry.path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(),
				               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (extension != ".dll")
					continue;
				if (!CopyInto(entry.path(), output, error))
					return fail(error);
				++dll_count;
			}
			if (!CopyInto(game_dll, output, error))
				return fail(error);
			Log(std::to_string(dll_count) + " engine DLLs + " + ToUtf8(game_dll.filename()));

			{
				std::string warnings;
				const int runtime_count = CopyCompilerRuntime(engine_dir, output, warnings);
				if (runtime_count > 0)
					Log(std::to_string(runtime_count) + " C++ runtime DLLs (MinGW)");
				if (!warnings.empty())
					Log(warnings);
			}

			if (fs::is_regular_file(root / "input.json", ec) && !CopyInto(root / "input.json", output, error))
				return fail(error);

			// Default font of the widgets (texts without a font) : the editor
			// font, like the Widget Editor preview.
			{
				const fs::path font = engine_dir / "fonts" / "VCR-OSD-MONO.ttf";
				if (fs::is_regular_file(font, ec))
				{
					fs::create_directories(output / "fonts", ec);
					if (!CopyInto(font, output / "fonts", error))
						return fail(error);
				}
			}

			// 4. Plugins : the runtime module (+ plugin.json, assets/) of every
			//    enabled plugin -> <game>/plugins/<Name>/, and a plugins.json
			//    that enables them. The editor modules stay behind.
			{
				Status("Copying the plugins...");
				fs::remove_all(output / "plugins", ec);
				std::vector<std::string> enabled;
				int plugin_count = 0;
				for (const lynx::plugins::PluginInfo& p : lynx::plugins::GetPlugins())
				{
					if (!p.enabled || p.runtime_dll.empty())
						continue;
					if (!fs::is_regular_file(p.runtime_dll, ec))
					{
						Log("Warning : plugin " + p.name + " not built, not shipped (" + ToUtf8(p.runtime_dll) + ")");
						continue;
					}
					const fs::path dest = output / "plugins" / FromUtf8(p.name);
					const fs::path dll_rel = p.runtime_dll.lexically_relative(p.folder);
					// The plugin built with the shipped engine (build-release/plugins
					// for the Release engine) : a plugin of another build of the
					// engine does not match its classes and factory.
					fs::path source = p.runtime_dll;
					const fs::path same_engine = engine_dir / "plugins" / FromUtf8(p.name) / dll_rel;
					if (p.engine_plugin && fs::is_regular_file(same_engine, ec))
						source = same_engine;
					fs::create_directories(dest / dll_rel.parent_path(), ec);
					fs::copy_file(source, dest / dll_rel, fs::copy_options::overwrite_existing, ec);
					if (ec)
						return fail("could not copy the plugin " + p.name + " : " + ec.message());

					// plugin.json without the editor module.
					{
						std::ofstream json(dest / "plugin.json", std::ios::trunc);
						json << "{\n  \"name\": \"" << p.name << "\",\n  \"version\": \"" << p.version
						     << "\",\n  \"runtime\": \"" << dll_rel.generic_string() << "\"\n}\n";
					}
					if (fs::is_directory(p.folder / "assets", ec))
						fs::copy(p.folder / "assets", dest / "assets",
						         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
					enabled.push_back(p.name);
					++plugin_count;
				}

				std::ofstream settings(output / "plugins.json", std::ios::trunc);
				settings << "{";
				for (size_t i = 0; i < enabled.size(); ++i)
					settings << (i ? ", " : " ") << "\"" << enabled[i] << "\": true";
				settings << " }\n";
				Log(std::to_string(plugin_count) + " plugin(s)");
			}

			// An old assets.pak would be read instead of nothing : removed.
			fs::remove(output / "assets.pak", ec);

			g_progress = 1.f;
			Status("Done : " + ToUtf8(exe_path.filename()));
			Log("Done.");
			g_worker_ok = true;
			g_worker_done = true;
		}

#ifdef _WIN32
		bool BrowseFolder(std::string& path)
		{
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			BROWSEINFOW info{};
			info.lpszTitle = L"Folder of the shipped game";
			info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
			PIDLIST_ABSOLUTE list = SHBrowseForFolderW(&info);
			if (!list)
				return false;
			wchar_t buffer[MAX_PATH] = {};
			const bool ok = SHGetPathFromIDListW(list, buffer) != 0;
			CoTaskMemFree(list);
			if (ok)
				path = ToUtf8(fs::path(buffer));
			return ok;
		}

		void OpenFolder(const fs::path& folder)
		{
			ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}

		// comdlg32 loaded at run time : no new library to link.
		bool BrowseIcon(std::string& path)
		{
			HMODULE comdlg = LoadLibraryW(L"comdlg32.dll");
			if (!comdlg)
				return false;

			using GetOpenFileNameFn = BOOL (WINAPI*)(LPOPENFILENAMEW);
			auto get_open_file_name = reinterpret_cast<GetOpenFileNameFn>(
				reinterpret_cast<void*>(GetProcAddress(comdlg, "GetOpenFileNameW")));

			bool picked = false;
			if (get_open_file_name)
			{
				wchar_t buffer[MAX_PATH] = {};
				const std::wstring initial = (g_root / "assets").wstring();

				OPENFILENAMEW info{};
				info.lStructSize = sizeof(info);
				info.hwndOwner = GetActiveWindow();
				info.lpstrFilter = L"Icons and images (*.ico, *.png, *.jpg, *.bmp, *.tga)\0*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All files\0*.*\0";
				info.lpstrFile = buffer;
				info.nMaxFile = MAX_PATH;
				info.lpstrInitialDir = initial.c_str();
				info.lpstrTitle = L"Icon of the game";
				info.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

				if (get_open_file_name(&info))
				{
					path = ToUtf8(fs::path(buffer));
					picked = true;
				}
			}

			FreeLibrary(comdlg);
			return picked;
		}
#else
		bool BrowseFolder(std::string&) { return false; }
		void OpenFolder(const fs::path&) {}
		bool BrowseIcon(std::string&) { return false; }
#endif

		void StartPacking()
		{
			if (g_worker.joinable())
				g_worker.join();
			g_worker_done = false;
			g_worker_ok = false;
			g_state = State::Packing;

			const fs::path release_dir = FindReleaseEngineDir();
			bool use_release = g_use_release && !release_dir.empty();
			// A Release engine older than the editor's misses the changes made
			// since (scripts, fixes) : the game ran in the editor, then broke
			// once shipped. The editor's engine is shipped instead.
			if (use_release && ReleaseEngineOutdated(release_dir))
			{
				use_release = false;
				Log("Warning : the Release engine (build-release) is older than the editor's : the editor's engine "
				    "is shipped instead. Run build-release.bat again for an optimized game.");
			}
			const fs::path engine_dir = use_release ? release_dir : host::GetEditorDirectory();
			Log(std::string("Engine : ") + (use_release ? "Release (" : "editor build (") + ToUtf8(engine_dir) + ")");

			g_worker = std::thread(Pack, g_root, FromUtf8(g_output), SafeFileName(g_game_name), g_hide_console,
			                       engine_dir, FromUtf8(g_icon));
		}
	}


	void SetProject(const fs::path& project_root, const std::string& project_name)
	{
		const bool changed = g_root != project_root;
		g_root = project_root;
		g_project_name = project_name;
		if (changed)
		{
			std::snprintf(g_game_name, sizeof(g_game_name), "%s", SafeFileName(project_name).c_str());
			const std::string output = ToUtf8(project_root / "Shipped");
			std::snprintf(g_output, sizeof(g_output), "%s", output.c_str());

			// Default icon : the one of the game (assets/icon.png), if any.
			std::error_code ec;
			const fs::path default_icon = project_root / "assets" / "icon.png";
			g_icon[0] = '\0';
			if (fs::is_regular_file(default_icon, ec))
				std::snprintf(g_icon, sizeof(g_icon), "%s", ToUtf8(default_icon).c_str());

			// Last settings of this project.
			LoadSettings();
		}
	}

	void Open()
	{
		g_open = true;
		if (g_state == State::Done || g_state == State::Failed)
			g_state = State::Idle;
	}

	bool IsBusy()
	{
		return g_state == State::Building || g_state == State::Packing;
	}

	void Draw(const std::function<void()>& save_level)
	{
		// --- Progress of the steps (even with the window closed) -------------
		if (g_state == State::Building)
		{
			if (!g_build_started)
			{
				g_build_started = game_build::Start() || game_build::IsRunning();
				if (!g_build_started)
				{
					Log("FAILED : could not start the build (Build.bat / CMakeLists.txt ?)");
					Status("Failed : the build could not start");
					g_state = State::Failed;
				}
			}
			else if (!game_build::IsRunning())
			{
				if (game_build::LastBuildSucceeded())
				{
					Log("Game compiled.");
					StartPacking();
				}
				else
				{
					Log("FAILED : the build failed (see the Build window)");
					Status("Failed : the game does not compile");
					g_state = State::Failed;
				}
			}
		}
		else if (g_state == State::Packing && g_worker_done)
		{
			if (g_worker.joinable())
				g_worker.join();
			g_state = g_worker_ok ? State::Done : State::Failed;
		}

		if (!g_open)
			return;

		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.f, 0.f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Ship Game", &g_open, ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		const bool busy = IsBusy();
		ImGui::BeginDisabled(busy);

		ImGui::TextWrapped("Makes a game that runs without the editor : one executable (the assets packed inside) "
		                   "and its DLLs, in the folder below.");
		ImGui::Spacing();

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Game name");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText("##ShipName", g_game_name, sizeof(g_game_name));

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Folder");
		ImGui::SameLine();
		const float browse_width = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.f;
		ImGui::SetNextItemWidth(std::max(50.f, ImGui::GetContentRegionAvail().x - browse_width - ImGui::GetStyle().ItemSpacing.x));
		ImGui::InputText("##ShipFolder", g_output, sizeof(g_output));
		ImGui::SameLine();
		if (ImGui::Button("Browse..."))
		{
			std::string path;
			if (BrowseFolder(path))
				std::snprintf(g_output, sizeof(g_output), "%s", path.c_str());
		}

		// Icon of the executable (Explorer, taskbar, window).
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Icon");
		ImGui::SameLine();
		const float icon_buttons = browse_width * 2.f + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(std::max(50.f, ImGui::GetContentRegionAvail().x - icon_buttons - ImGui::GetStyle().ItemSpacing.x));
		ImGui::InputTextWithHint("##ShipIcon", "(icon of LynxRuntime.exe)", g_icon, sizeof(g_icon));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(".ico (used as it is) or an image : png, jpg, bmp, tga\n(converted to 16 - 256 px, centered on a square).");
		ImGui::SameLine();
		if (ImGui::Button("Browse...##Icon"))
		{
			std::string path;
			if (BrowseIcon(path))
				std::snprintf(g_icon, sizeof(g_icon), "%s", path.c_str());
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(g_icon[0] == '\0');
		if (ImGui::Button("Clear", ImVec2(browse_width, 0.f)))
			g_icon[0] = '\0';
		ImGui::EndDisabled();

		// Icon file checked right away (the error is shown here, not at the end).
		if (g_icon[0] != '\0')
		{
			std::error_code ec;
			if (!fs::is_regular_file(FromUtf8(g_icon), ec))
				ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.15f, 1.f), "Icon file not found");
		}

		// Engine : Release (build-release.bat) or the editor's (Debug).
		const fs::path release_dir = FindReleaseEngineDir();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Engine");
		ImGui::SameLine();
		if (release_dir.empty())
		{
			ImGui::TextDisabled("editor build (no build-release : run build-release.bat for an optimized game)");
		}
		else
		{
			if (ImGui::RadioButton("Release", g_use_release))
				g_use_release = true;
			ImGui::SameLine();
			if (ImGui::RadioButton("Editor build", !g_use_release))
				g_use_release = false;

			if (g_use_release && ReleaseEngineOutdated(release_dir))
			{
				ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1.f),
				                   "The Release engine is older than the editor's : the editor build will be shipped.\n"
				                   "Run build-release.bat again for an optimized game.");
			}
		}

		ImGui::Checkbox("Compile the game first", &g_compile);
		ImGui::SameLine();
		ImGui::Checkbox("No console window", &g_hide_console);

		ImGui::Spacing();
		const bool can_ship = g_output[0] != '\0' && g_game_name[0] != '\0' && !g_root.empty();
		ImGui::BeginDisabled(!can_ship);
		if (ImGui::Button("Ship", ImVec2(-FLT_MIN, 0.f)))
		{
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_log.clear();
			}
			g_progress = 0.f;
			SaveSettings();
			Status("Saving the level...");
			if (save_level)
				save_level();
			Log("Shipping " + SafeFileName(g_game_name) + " to " + g_output);

			// Nothing to compile in a project without C++.
			if (g_compile && host::CanBuildProject(g_root))
			{
				Status("Compiling the game...");
				g_build_started = false;
				g_state = State::Building;
			}
			else
			{
				StartPacking();
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		// --- State ------------------------------------------------------------
		std::string status;
		std::vector<std::string> log;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			status = g_status;
			log = g_log;
		}

		if (g_state != State::Idle || !log.empty())
		{
			ImGui::Separator();
			const float progress = g_state == State::Building ? 0.05f : g_progress.load();
			ImGui::ProgressBar(g_state == State::Done ? 1.f : progress, ImVec2(-FLT_MIN, 0.f), status.c_str());

			ImGui::BeginChild("##ShipLog", ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 6.f), true);
			for (const std::string& line : log)
			{
				if (line.rfind("FAILED", 0) == 0)
					ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.15f, 1.f), "%s", line.c_str());
				else
					ImGui::TextUnformatted(line.c_str());
			}
			ImGui::EndChild();

			if (g_state == State::Done && ImGui::Button("Open the folder"))
				OpenFolder(FromUtf8(g_output));
		}

		ImGui::End();
	}
}
