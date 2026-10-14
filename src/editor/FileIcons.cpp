#include "FileIcons.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>

namespace lynx::editor::file_icons
{
	namespace
	{
		bool Is(const std::string& extension, std::initializer_list<const char*> list)
		{
			for (const char* e : list)
				if (extension == e)
					return true;
			return false;
		}
	}


	Kind ForFile(const std::filesystem::path& path, bool is_directory)
	{
		if (is_directory)
			return Kind::Folder;

		std::string name = path.filename().string();
		std::string extension = path.extension().string();
		auto lower = [](std::string& s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		};
		lower(name);
		lower(extension);

		if (name == "voxels.json")
			return Kind::Voxels;
		if (extension == ".js" || extension == ".mjs" || extension == ".ts")
			return Kind::Script;
		if (extension == ".py")
			return Kind::Python;
		if (Is(extension, { ".cpp", ".cc", ".c", ".h", ".hpp", ".inl" }))
			return Kind::Cpp;
		if (extension == ".json")
			return Kind::Data;
		if (extension == ".level" || extension == ".xml" || extension == ".hrlv")
			return Kind::Level;
		if (Is(extension, { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr" }))
			return Kind::Image;
		if (Is(extension, { ".wav", ".mp3", ".flac", ".ogg" }))
			return Kind::Sound;
		if (Is(extension, { ".ttf", ".otf", ".fnt" }))
			return Kind::Font;
		if (extension == ".widget")
			return Kind::Widget;
		if (extension == ".animgraph")
			return Kind::AnimGraph;
		if (extension == ".bt")
			return Kind::BehaviorTree;
		if (Is(extension, { ".ini", ".cfg", ".toml", ".yaml", ".yml" }))
			return Kind::Config;
		if (Is(extension, { ".glsl", ".hlsl", ".vert", ".frag", ".lua", ".cs", ".sh", ".bat" }))
			return Kind::Code;
		if (Is(extension, { ".html", ".htm", ".svg" }))
			return Kind::Markup;
		if (Is(extension, { ".txt", ".md", ".log" }))
			return Kind::Text;
		return Kind::File;
	}

	const char* Describe(Kind kind)
	{
		switch (kind)
		{
		case Kind::Folder: return "Folder";
		case Kind::Text: return "Text";
		case Kind::Script: return "JavaScript";
		case Kind::Python: return "Python";
		case Kind::Cpp: return "C / C++";
		case Kind::Data: return "JSON data";
		case Kind::Level: return "Level";
		case Kind::Image: return "Image";
		case Kind::Sound: return "Sound";
		case Kind::Font: return "Font";
		case Kind::Widget: return "Widget (UI)";
		case Kind::AnimGraph: return "Anim Graph";
		case Kind::BehaviorTree: return "Behavior Tree";
		case Kind::Voxels: return "Voxel types";
		case Kind::Config: return "Settings";
		case Kind::Code: return "Code";
		case Kind::Markup: return "Markup";
		default: return "File";
		}
	}
}
