#include "ProjectTemplates.h"

#include "../host/GameProject.h"

#include <hrl/hrl.h>
#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace lynx::editor::project_templates
{
	namespace
	{
		constexpr const char* kCommonFolder = "_common";
		constexpr const char* kTemplateFile = "template.json";
		constexpr const char* kStarterTerrainFile = ".lynx/starter_terrain.txt";

		// UTF-8 <-> path (C++20 : u8string() is a std::u8string). Accents in a
		// project name or a folder must survive on Windows.
		std::string ToUtf8(const fs::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		fs::path FromUtf8(const std::string& text)
		{
			return fs::path(std::u8string(text.begin(), text.end()));
		}

		std::string ReadText(const fs::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
		{
			if (from.empty())
				return;
			for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size()))
				text.replace(pos, from.size(), to);
		}

		// Files whose content gets the {{...}} replacements (the others are copied as is).
		bool IsTextFile(const fs::path& path)
		{
			static const char* const kText[] = {
				".cpp", ".h", ".hpp", ".inl", ".c", ".txt", ".md", ".json", ".xml", ".js",
				".py", ".bat", ".cmd", ".cmake", ".cfg", ".ini", ".gitignore", ".gitattributes",
			};
			std::string extension = path.extension().string();
			if (extension.empty())
				extension = path.filename().string();   // ".gitignore" has no extension
			std::transform(extension.begin(), extension.end(), extension.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return std::find(std::begin(kText), std::end(kText), extension) != std::end(kText) ||
			       path.filename() == "CMakeLists.txt";
		}

		struct Replacements
		{
			std::string name;
			std::string title;

			std::string Apply(std::string text) const
			{
				ReplaceAll(text, "{{PROJECT_NAME}}", name);
				ReplaceAll(text, "{{PROJECT_TITLE}}", title);
				return text;
			}
		};

		// Copies `source` into `target` (files of the same path are replaced).
		bool CopyTree(const fs::path& source, const fs::path& target, const Replacements& replacements,
		              std::string& error)
		{
			std::error_code ec;
			if (!fs::is_directory(source, ec))
				return true;

			for (auto it = fs::recursive_directory_iterator(source, ec); !ec && it != fs::recursive_directory_iterator();
			     it.increment(ec))
			{
				const fs::path relative = fs::relative(it->path(), source, ec);
				if (ec)
					break;

				// template.json describes the template : not part of the project.
				if (relative == kTemplateFile)
					continue;

				const fs::path destination = target / FromUtf8(replacements.Apply(ToUtf8(relative)));

				if (it->is_directory(ec))
				{
					fs::create_directories(destination, ec);
					continue;
				}

				fs::create_directories(destination.parent_path(), ec);

				if (IsTextFile(it->path()))
				{
					std::ofstream file(destination, std::ios::binary | std::ios::trunc);
					const std::string text = replacements.Apply(ReadText(it->path()));
					file.write(text.data(), static_cast<std::streamsize>(text.size()));
					if (!file)
					{
						error = "Could not write " + destination.string();
						return false;
					}
				}
				else
				{
					fs::copy_file(it->path(), destination, fs::copy_options::overwrite_existing, ec);
				}

				if (ec)
				{
					error = "Could not copy " + it->path().string() + " : " + ec.message();
					return false;
				}
			}

			if (ec)
			{
				error = "Could not read the template " + source.string() + " : " + ec.message();
				return false;
			}
			return true;
		}
	}


	fs::path TemplatesFolder()
	{
		return host::GetEditorDirectory() / "templates";
	}


	std::vector<Template> List(std::string& error)
	{
		std::vector<Template> templates;
		error.clear();

		const fs::path folder = TemplatesFolder();
		std::error_code ec;

		if (!fs::is_directory(folder, ec))
		{
			error = "No templates folder next to the editor :\n" + folder.string();
			return templates;
		}

		for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec))
		{
			if (!entry.is_directory(ec) || entry.path().filename() == kCommonFolder)
				continue;

			Template t;
			t.id = entry.path().filename().string();
			t.name = t.id;
			t.folder = entry.path();

			const Json info = Json::parse(ReadText(entry.path() / kTemplateFile), nullptr, false, true);
			if (info.is_object())
			{
				if (info.contains("name") && info["name"].is_string())
					t.name = info["name"].get<std::string>();
				if (info.contains("description") && info["description"].is_string())
					t.description = info["description"].get<std::string>();
				if (info.contains("order") && info["order"].is_number_integer())
					t.order = info["order"].get<int>();
			}

			templates.push_back(std::move(t));
		}

		std::sort(templates.begin(), templates.end(), [](const Template& a, const Template& b)
		{
			return a.order != b.order ? a.order < b.order : a.name < b.name;
		});

		if (templates.empty())
			error = "The templates folder is empty :\n" + folder.string();

		return templates;
	}


	std::string ToIdentifier(const std::string& title)
	{
		// Latin-1 letters (UTF-8 "C3 xx") -> ASCII : "Héros" -> "Heros".
		auto latin = [](unsigned char second) -> char
		{
			const unsigned char c = static_cast<unsigned char>(second + 0x40);   // U+00C0..U+00FF
			if (c >= 0xC0 && c <= 0xC5) return 'A';  if (c == 0xC7) return 'C';
			if (c >= 0xC8 && c <= 0xCB) return 'E';  if (c >= 0xCC && c <= 0xCF) return 'I';
			if (c == 0xD1) return 'N';               if (c >= 0xD2 && c <= 0xD6) return 'O';
			if (c >= 0xD9 && c <= 0xDC) return 'U';  if (c == 0xDD) return 'Y';
			if (c >= 0xE0 && c <= 0xE5) return 'a';  if (c == 0xE7) return 'c';
			if (c >= 0xE8 && c <= 0xEB) return 'e';  if (c >= 0xEC && c <= 0xEF) return 'i';
			if (c == 0xF1) return 'n';               if (c >= 0xF2 && c <= 0xF6) return 'o';
			if (c >= 0xF9 && c <= 0xFC) return 'u';  if (c == 0xFD || c == 0xFF) return 'y';
			return 0;
		};

		std::string id;
		bool upper_next = true;

		for (size_t i = 0; i < title.size(); ++i)
		{
			unsigned char c = static_cast<unsigned char>(title[i]);

			if (c == 0xC3 && i + 1 < title.size())
				c = static_cast<unsigned char>(latin(static_cast<unsigned char>(title[++i])));
			else if (c >= 0x80)
				continue;   // other non-ASCII characters : dropped, the word goes on

			if (c != 0 && std::isalnum(c))
			{
				id += upper_next ? static_cast<char>(std::toupper(c)) : static_cast<char>(c);
				upper_next = false;
			}
			else
			{
				upper_next = true;   // space, '-', '_'... : new word ("mon jeu" -> "MonJeu")
			}
		}

		if (!id.empty() && std::isdigit(static_cast<unsigned char>(id[0])))
			id = "Game" + id;

		return id;
	}


	fs::path ProjectFolder(const fs::path& parent, const std::string& title)
	{
		std::string name;
		for (char c : title)
			if (std::string("<>:\"/\\|?*").find(c) == std::string::npos && static_cast<unsigned char>(c) >= 32)
				name += c;

		// No trailing spaces / dots (Windows removes them).
		while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
			name.pop_back();
		while (!name.empty() && name.front() == ' ')
			name.erase(name.begin());

		return name.empty() ? fs::path() : parent / FromUtf8(name);
	}


	bool Create(const Template& tmpl, const fs::path& parent, const std::string& title,
	            fs::path& out_root, std::string& error)
	{
		error.clear();

		const std::string name = ToIdentifier(title);
		if (name.empty())
		{
			error = "The name needs at least one letter or digit (A-Z, 0-9).";
			return false;
		}

		if (parent.empty())
		{
			error = "Choose where to create the project.";
			return false;
		}

		const fs::path root = ProjectFolder(parent, title);
		if (root.empty())
		{
			error = "This name cannot be a folder name.";
			return false;
		}

		std::error_code ec;
		if (fs::exists(root, ec) && !(fs::is_directory(root, ec) && fs::is_empty(root, ec)))
		{
			error = "This folder already exists and is not empty :\n" + root.string();
			return false;
		}

		fs::create_directories(root, ec);
		if (ec)
		{
			error = "Could not create " + root.string() + " : " + ec.message();
			return false;
		}

		const Replacements replacements{ name, title };

		if (!CopyTree(TemplatesFolder() / kCommonFolder, root, replacements, error) ||
		    !CopyTree(tmpl.folder, root, replacements, error))
			return false;

		// What the project browser checks (InspectProject).
		fs::create_directories(root / "assets", ec);
		fs::create_directories(root / "build", ec);

		std::cout << "[PROJECT] Created \"" << title << "\" (" << tmpl.name << ") in " << root.string() << "\n";

		out_root = root;
		return true;
	}


	bool ApplyStarterTerrain(uint32_t scene, const std::string& world_file)
	{
		std::error_code ec;
		if (!fs::is_regular_file(kStarterTerrainFile, ec))
			return false;

		std::istringstream lines(ReadText(kStarterTerrainFile));
		std::string line;
		int rectangles = 0;
		long long voxels = 0;

		HRL_BeginVoxelEdit(scene);

		while (std::getline(lines, line))
		{
			const size_t comment = line.find('#');
			if (comment != std::string::npos)
				line.erase(comment);

			std::istringstream fields(line);
			float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
			int type = 0;
			if (!(fields >> x0 >> y0 >> x1 >> y1 >> type))
				continue;

			float vx0 = 0.f, vy0 = 0.f, vx1 = 0.f, vy1 = 0.f;
			if (HRL_WorldToVoxelCoordinates(scene, std::min(x0, x1), std::min(y0, y1), &vx0, &vy0) != HRL_TRUE ||
			    HRL_WorldToVoxelCoordinates(scene, std::max(x0, x1), std::max(y0, y1), &vx1, &vy1) != HRL_TRUE)
				continue;

			// Cells whose center is inside the rectangle.
			const int cx0 = static_cast<int>(std::floor(vx0 + 0.5f));
			const int cy0 = static_cast<int>(std::floor(vy0 + 0.5f));
			const int cx1 = static_cast<int>(std::floor(vx1 + 0.5f));
			const int cy1 = static_cast<int>(std::floor(vy1 + 0.5f));

			for (int y = cy0; y < cy1; ++y)
				for (int x = cx0; x < cx1; ++x)
					HRL_SetVoxelType(scene, x, y, static_cast<uint32_t>(std::clamp(type, 0, 255)));

			voxels += static_cast<long long>(std::max(0, cx1 - cx0)) * std::max(0, cy1 - cy0);
			++rectangles;
		}

		HRL_EndVoxelEdit(scene);

		if (rectangles > 0)
			HRL_SaveVoxelWorldAllFile(scene, world_file.c_str());

		fs::remove(kStarterTerrainFile, ec);

		std::cout << "[PROJECT] Starter terrain : " << rectangles << " rectangle(s), " << voxels
		          << " voxels painted, saved to " << world_file << "\n";
		return rectangles > 0;
	}
}
