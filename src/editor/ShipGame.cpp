#include "ShipGame.h"

#include "GameBuild.h"
#include "../host/GameProject.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
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
		bool g_hide_console = true;
		bool g_compile = true;
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
		// Worker : archive + executable + dependencies
		// ---------------------------------------------------------------------

		void Pack(fs::path root, fs::path output, std::string game_name, bool hide_console)
		{
			std::string error;
			auto fail = [&](const std::string& message)
			{
				Log("FAILED : " + message);
				Status("Failed : " + message);
				g_worker_ok = false;
				g_worker_done = true;
			};

			const fs::path editor_dir = host::GetEditorDirectory();
			const fs::path runtime = editor_dir / "LynxRuntime.exe";
			std::error_code ec;

			if (!fs::is_regular_file(runtime, ec))
				return fail("LynxRuntime.exe not found next to the editor (" + ToUtf8(editor_dir) + ")");

			const auto modules = host::FindGameModules(root / "build", 1);
			if (modules.empty())
				return fail("no game DLL in build/ (the build failed ?)");
			const fs::path game_dll = modules.front();

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
			for (const auto& entry : fs::directory_iterator(editor_dir, ec))
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

			if (fs::is_regular_file(root / "input.json", ec) && !CopyInto(root / "input.json", output, error))
				return fail(error);

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
#else
		bool BrowseFolder(std::string&) { return false; }
		void OpenFolder(const fs::path&) {}
#endif

		void StartPacking()
		{
			if (g_worker.joinable())
				g_worker.join();
			g_worker_done = false;
			g_worker_ok = false;
			g_state = State::Packing;
			g_worker = std::thread(Pack, g_root, FromUtf8(g_output), SafeFileName(g_game_name), g_hide_console);
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
			Status("Saving the level...");
			if (save_level)
				save_level();
			Log("Shipping " + SafeFileName(g_game_name) + " to " + g_output);

			if (g_compile)
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
