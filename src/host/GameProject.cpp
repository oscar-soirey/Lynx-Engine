#include "GameProject.h"

#include "../core/Engine.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace lynx::host
{
	namespace
	{
		constexpr const char* kRecentProjectsFile = "editor_recent_projects.txt";
		constexpr size_t kMaxRecentProjects = 12;
		constexpr const char* kGameModuleSymbol = "FactoryRegisterClasses";

		// ---------------------------------------------------------------------
		// Minimal PE (Windows .dll) export table reader
		// ---------------------------------------------------------------------

		uint16_t ReadU16(const std::vector<uint8_t>& d, size_t at)
		{
			return static_cast<uint16_t>(d[at] | (d[at + 1] << 8));
		}

		uint32_t ReadU32(const std::vector<uint8_t>& d, size_t at)
		{
			return static_cast<uint32_t>(d[at])
				| (static_cast<uint32_t>(d[at + 1]) << 8)
				| (static_cast<uint32_t>(d[at + 2]) << 16)
				| (static_cast<uint32_t>(d[at + 3]) << 24);
		}

		struct PeSection
		{
			uint32_t virtual_address;
			uint32_t virtual_size;
			uint32_t raw_size;
			uint32_t raw_offset;
		};

		// RVA -> file offset. Returns false when the RVA is in no section.
		bool RvaToOffset(
			const std::vector<PeSection>& sections,
			uint32_t rva,
			size_t& offset)
		{
			for (const PeSection& s : sections)
			{
				const uint32_t size = std::max(s.virtual_size, s.raw_size);

				if (rva >= s.virtual_address && rva < s.virtual_address + size)
				{
					const uint32_t delta = rva - s.virtual_address;

					if (delta >= s.raw_size)
						return false;   // not stored in the file (bss...)

					offset = static_cast<size_t>(s.raw_offset) + delta;
					return true;
				}
			}

			return false;
		}

		bool ReadWholeFile(const fs::path& path, std::vector<uint8_t>& data)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);

			if (!file)
				return false;

			const std::streamsize size = file.tellg();

			// A game DLL is far below that ; refuse absurd files.
			if (size <= 0 || size > (512ll << 20))
				return false;

			data.resize(static_cast<size_t>(size));
			file.seekg(0, std::ios::beg);

			return static_cast<bool>(
				file.read(reinterpret_cast<char*>(data.data()), size));
		}

		std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		void FindGameModulesIn(
			const fs::path& dir,
			int depth,
			std::vector<fs::path>& out)
		{
			std::error_code error;

			if (!fs::is_directory(dir, error))
				return;

			for (const auto& entry : fs::directory_iterator(
					 dir, fs::directory_options::skip_permission_denied, error))
			{
				std::error_code entry_error;

				if (entry.is_directory(entry_error))
				{
					// Skip CMake internals, they never hold the final DLL.
					const std::string name = entry.path().filename().string();

					if (depth > 0 && name != "CMakeFiles" && name.rfind('.', 0) != 0)
						FindGameModulesIn(entry.path(), depth - 1, out);

					continue;
				}

				if (!entry.is_regular_file(entry_error))
					continue;

				if (ToLower(entry.path().extension().string()) != ".dll")
					continue;

				if (DllExportsSymbol(entry.path(), kGameModuleSymbol))
					out.push_back(entry.path());
			}
		}

		fs::path RecentProjectsFile()
		{
			return GetEditorDirectory() / kRecentProjectsFile;
		}

		void SaveRecentProjects(const std::vector<fs::path>& projects)
		{
			std::ofstream file(RecentProjectsFile(), std::ios::trunc);

			if (!file)
			{
				std::cerr << "[PROJECT] Could not write "
				          << RecentProjectsFile().string() << "\n";
				return;
			}

			for (const fs::path& p : projects)
				file << p.string() << "\n";
		}

		bool SamePath(const fs::path& a, const fs::path& b)
		{
			std::error_code error;

			if (fs::equivalent(a, b, error))
				return true;

			// equivalent() fails when a folder no longer exists.
			return ToLower(a.lexically_normal().generic_string()) ==
			       ToLower(b.lexically_normal().generic_string());
		}
	}


	std::vector<fs::path> FindGameModules(const fs::path& dir, int depth)
	{
		std::vector<fs::path> modules;
		FindGameModulesIn(dir, depth, modules);

		// Plugin modules (LYNX_PLUGIN) also export FactoryRegisterClasses :
		// they are never the game.
		std::erase_if(modules, [](const fs::path& m) { return DllExportsSymbol(m, "LynxPlugin_Info"); });

		// Most recently built first.
		std::stable_sort(modules.begin(), modules.end(),
			[](const fs::path& a, const fs::path& b)
			{
				std::error_code ea;
				std::error_code eb;
				const auto ta = fs::last_write_time(a, ea);
				const auto tb = fs::last_write_time(b, eb);

				if (ea || eb)
					return false;

				return ta > tb;
			});

		return modules;
	}


	bool DllExportsSymbol(const fs::path& dll_path, const char* symbol)
	{
		std::vector<uint8_t> d;

		if (!symbol || !ReadWholeFile(dll_path, d))
			return false;

		// DOS header : "MZ", e_lfanew at 0x3C.
		if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z')
			return false;

		const size_t pe = ReadU32(d, 0x3C);

		// "PE\0\0" + COFF header (20 bytes).
		if (pe + 24 > d.size() ||
			d[pe] != 'P' || d[pe + 1] != 'E' || d[pe + 2] != 0 || d[pe + 3] != 0)
		{
			return false;
		}

		const size_t coff = pe + 4;
		const uint16_t section_count = ReadU16(d, coff + 2);
		const uint16_t optional_size = ReadU16(d, coff + 16);
		const size_t optional = coff + 20;

		if (optional + optional_size > d.size() || optional_size < 2)
			return false;

		// PE32 (0x10B) or PE32+ (0x20B) : the data directories do not start
		// at the same place.
		const uint16_t magic = ReadU16(d, optional);
		size_t rva_count_at = 0;
		size_t directories_at = 0;

		if (magic == 0x10B)
		{
			rva_count_at = optional + 92;
			directories_at = optional + 96;
		}
		else if (magic == 0x20B)
		{
			rva_count_at = optional + 108;
			directories_at = optional + 112;
		}
		else
		{
			return false;
		}

		if (directories_at + 8 > optional + optional_size)
			return false;

		if (ReadU32(d, rva_count_at) < 1)
			return false;   // no export directory entry

		const uint32_t export_rva = ReadU32(d, directories_at);
		const uint32_t export_size = ReadU32(d, directories_at + 4);

		if (export_rva == 0 || export_size == 0)
			return false;

		// Section table.
		const size_t sections_at = optional + optional_size;

		if (sections_at + static_cast<size_t>(section_count) * 40 > d.size())
			return false;

		std::vector<PeSection> sections;
		sections.reserve(section_count);

		for (uint16_t i = 0; i < section_count; ++i)
		{
			const size_t s = sections_at + static_cast<size_t>(i) * 40;

			sections.push_back({
				ReadU32(d, s + 12),   // VirtualAddress
				ReadU32(d, s + 8),    // VirtualSize
				ReadU32(d, s + 16),   // SizeOfRawData
				ReadU32(d, s + 20)    // PointerToRawData
			});
		}

		size_t export_at = 0;

		if (!RvaToOffset(sections, export_rva, export_at) || export_at + 40 > d.size())
			return false;

		const uint32_t name_count = ReadU32(d, export_at + 24);
		const uint32_t names_rva = ReadU32(d, export_at + 32);

		size_t names_at = 0;

		if (name_count == 0 || !RvaToOffset(sections, names_rva, names_at))
			return false;

		const size_t symbol_length = std::strlen(symbol);

		for (uint32_t i = 0; i < name_count; ++i)
		{
			const size_t entry = names_at + static_cast<size_t>(i) * 4;

			if (entry + 4 > d.size())
				return false;

			size_t name_at = 0;

			if (!RvaToOffset(sections, ReadU32(d, entry), name_at))
				continue;

			if (name_at + symbol_length + 1 > d.size())
				continue;

			if (std::memcmp(&d[name_at], symbol, symbol_length) == 0 &&
				d[name_at + symbol_length] == 0)
			{
				return true;
			}
		}

		return false;
	}


	bool InspectProject(const fs::path& folder, GameProject& out, std::string& error)
	{
		out = GameProject{};
		error.clear();

		if (folder.empty())
		{
			error = "No folder selected.";
			return false;
		}

		std::error_code ec;
		fs::path root = fs::absolute(folder, ec);

		if (ec)
			root = folder;

		root = root.lexically_normal();

		// "C:/Games/MyGame/" -> keep "C:/Games/MyGame" (filename() not empty).
		if (!root.has_filename() && root.has_parent_path() && root != root.root_path())
			root = root.parent_path();

		if (!fs::is_directory(root, ec))
		{
			error = "Folder not found:\n" + root.string();
			return false;
		}

		out.root = root;
		out.name = root.filename().string();
		out.assets_dir = root / "assets";
		out.build_dir = root / "build";

		if (!fs::is_directory(out.assets_dir, ec))
		{
			error = "This folder has no \"assets\" folder:\n" + out.assets_dir.string();
			return false;
		}

		// build/*.dll and build/<config>/*.dll
		if (fs::is_directory(out.build_dir, ec))
			out.modules = FindGameModules(out.build_dir, 1);

		if (out.modules.empty())
		{
			// Not built yet : fine if the editor can compile it when opening.
			if (CanBuildProject(root))
				return true;

			// No C++ at all : the generic game DLL of the engine.
			const fs::path script_game = GetScriptGameModule();
			if (fs::is_regular_file(script_game, ec))
			{
				out.module_path = script_game;
				out.script_only = true;
				return true;
			}

			error = "No game DLL in \"build\" (a DLL exporting FactoryRegisterClasses,\n"
			        "see LYNX_LINK_MODULE), and no Build.bat / CMakeLists.txt to build it.";
			return false;
		}

		out.module_path = out.modules.front();

		return true;
	}


	fs::path GetScriptGameModule()
	{
		return GetEditorDirectory() / "scriptgame" / "LynxScriptGame.dll";
	}


	bool CanBuildProject(const fs::path& root)
	{
		std::error_code ec;

		return fs::is_regular_file(root / "Build.bat", ec) ||
		       fs::is_regular_file(root / "CMakeLists.txt", ec);
	}


	namespace
	{
		bool IsSourceFile(const fs::path& file)
		{
			std::string ext = file.extension().string();

			for (char& c : ext)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

			static const char* const kExtensions[] = {
				".cpp", ".cxx", ".cc", ".c", ".h", ".hpp", ".hxx", ".inl", ".cmake", ".rc",
			};

			for (const char* e : kExtensions)
			{
				if (ext == e)
					return true;
			}

			return file.filename() == "CMakeLists.txt";
		}

		// Newest game source (src/, include/, CMake files of the root). `found`
		// false = no source at all.
		fs::file_time_type NewestSourceTime(const fs::path& root, fs::path& newest_file, bool& found)
		{
			found = false;
			fs::file_time_type newest{};

			const auto consider = [&](const fs::path& file)
			{
				std::error_code ec;

				if (!IsSourceFile(file) || !fs::is_regular_file(file, ec))
					return;

				const auto time = fs::last_write_time(file, ec);

				if (ec)
					return;

				if (!found || time > newest)
				{
					newest = time;
					newest_file = file;
					found = true;
				}
			};

			std::error_code ec;

			for (const auto& entry : fs::directory_iterator(root, ec))
				consider(entry.path());

			for (const char* folder : { "src", "include", "source", "cmake" })
			{
				const fs::path dir = root / folder;
				ec.clear();

				if (!fs::is_directory(dir, ec))
					continue;

				fs::recursive_directory_iterator it(
					dir, fs::directory_options::skip_permission_denied, ec);

				for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
					consider(it->path());
			}

			return newest;
		}
	}


	std::string GetBuildReason(const GameProject& project, bool& blocking)
	{
		blocking = false;
		std::error_code ec;

		// Generic DLL of the engine : built with it, never compiled here.
		if (project.script_only)
			return {};

		if (project.module_path.empty() || !fs::is_regular_file(project.module_path, ec))
		{
			blocking = true;
			return "The game has not been compiled yet (no game DLL in build/).";
		}

		const auto module_time = fs::last_write_time(project.module_path, ec);

		if (ec)
			return {};

		// Older than the engine : the engine classes it embeds may not match
		// anymore -> crash. Never loaded as it is.
		const fs::path engine_module = GetEngineModulePath();

		if (!engine_module.empty())
		{
			std::error_code engine_error;
			const auto engine_time = fs::last_write_time(engine_module, engine_error);

			if (!engine_error && module_time < engine_time)
			{
				blocking = true;
				return "The engine was rebuilt after the game: the game DLL must be compiled again\n"
				       "against this engine (loading it as it is would crash).";
			}
		}

		// Sources edited since the last build : the DLL works, but is old.
		fs::path newest_file;
		bool found = false;
		const auto source_time = NewestSourceTime(project.root, newest_file, found);

		if (found && module_time < source_time)
		{
			std::error_code relative_error;
			const fs::path relative = fs::relative(newest_file, project.root, relative_error);

			return "The game sources changed since the last build (" +
			       (relative_error ? newest_file : relative).generic_string() + ").";
		}

		return {};
	}


	fs::path GetEngineModulePath()
	{
#ifdef _WIN32
		HMODULE engine_module = nullptr;

		// The module that contains lynx::Engine::Get = the engine DLL.
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(reinterpret_cast<void*>(&lynx::Engine::Get)),
				&engine_module))
		{
			return {};
		}

		std::wstring buffer(MAX_PATH, L'\0');
		const DWORD length = GetModuleFileNameW(
			engine_module, buffer.data(), static_cast<DWORD>(buffer.size()));

		if (length == 0 || length >= buffer.size())
			return {};

		buffer.resize(length);
		return fs::path(buffer);
#else
		return {};
#endif
	}


	std::string CheckModuleUpToDate(const fs::path& game_module)
	{
		const fs::path engine_module = GetEngineModulePath();

		if (engine_module.empty() || game_module.empty())
			return {};

		std::error_code engine_error;
		std::error_code game_error;

		const auto engine_time = fs::last_write_time(engine_module, engine_error);
		const auto game_time = fs::last_write_time(game_module, game_error);

		if (engine_error || game_error || game_time >= engine_time)
			return {};

		return
			"The game DLL is older than the engine:\n"
			"  " + game_module.string() + "\n"
			"  " + engine_module.string() + "\n\n"
			"It was not rebuilt after the last engine change. If an engine class it\n"
			"uses changed (InputAction, Actor...), the game will crash at random\n"
			"places (ex : when stopping Play).\n\n"
			"Rebuild the game, then use \"Reload Game\".";
	}


	fs::path GetEditorDirectory()
	{
#ifdef _WIN32
		std::wstring buffer(MAX_PATH, L'\0');

		for (;;)
		{
			const DWORD length = GetModuleFileNameW(
				nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

			if (length == 0)
				break;

			if (length < buffer.size())
			{
				buffer.resize(length);
				return fs::path(buffer).parent_path();
			}

			buffer.resize(buffer.size() * 2);
		}
#endif
		static const fs::path startup_directory = fs::current_path();
		return startup_directory;
	}


	std::vector<fs::path> LoadRecentProjects()
	{
		std::vector<fs::path> projects;
		std::ifstream file(RecentProjectsFile());

		if (!file)
			return projects;

		std::string line;

		while (std::getline(file, line))
		{
			while (!line.empty() &&
				   (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			{
				line.pop_back();
			}

			if (line.empty())
				continue;

			const fs::path p(line);

			const bool duplicate = std::any_of(projects.begin(), projects.end(),
				[&](const fs::path& other) { return SamePath(other, p); });

			if (!duplicate)
				projects.push_back(p);
		}

		return projects;
	}


	void AddRecentProject(const fs::path& root)
	{
		std::vector<fs::path> projects = LoadRecentProjects();

		projects.erase(
			std::remove_if(projects.begin(), projects.end(),
				[&](const fs::path& p) { return SamePath(p, root); }),
			projects.end());

		projects.insert(projects.begin(), root);

		if (projects.size() > kMaxRecentProjects)
			projects.resize(kMaxRecentProjects);

		SaveRecentProjects(projects);
	}


	void RemoveRecentProject(const fs::path& root)
	{
		std::vector<fs::path> projects = LoadRecentProjects();

		projects.erase(
			std::remove_if(projects.begin(), projects.end(),
				[&](const fs::path& p) { return SamePath(p, root); }),
			projects.end());

		SaveRecentProjects(projects);
	}


	bool LaunchEditorProcess(const fs::path& project)
	{
#ifdef _WIN32
		std::wstring exe(MAX_PATH, L'\0');
		DWORD length = 0;

		for (;;)
		{
			length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));

			if (length == 0)
				return false;

			if (length < exe.size())
				break;

			exe.resize(exe.size() * 2);
		}

		exe.resize(length);

		std::wstring command_line = L"\"" + exe + L"\"";

		if (!project.empty())
			command_line += L" \"" + project.wstring() + L"\"";

		const std::wstring working_directory = GetEditorDirectory().wstring();

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};

		if (!CreateProcessW(
				exe.c_str(),
				command_line.data(),
				nullptr,
				nullptr,
				FALSE,
				0,
				nullptr,
				working_directory.c_str(),
				&startup,
				&process))
		{
			std::cerr << "[PROJECT] Could not start a new editor (error "
			          << GetLastError() << ")\n";
			return false;
		}

		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
#else
		(void)project;
		return false;
#endif
	}
}
