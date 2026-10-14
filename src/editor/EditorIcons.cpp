#include "EditorIcons.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace lynx::editor::icons
{
	Icon ForFile(const char* path, bool is_directory)
	{
		if (is_directory)
			return Icon::Folder;
		std::string extension = std::filesystem::path(path ? path : "").extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		for (const char* e : { ".js", ".py", ".cpp", ".h", ".hpp", ".json", ".xml", ".glsl", ".txt", ".md", ".ini", ".cfg" })
			if (extension == e)
				return (extension == ".txt" || extension == ".md") ? Icon::File : Icon::FileCode;
		for (const char* e : { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd" })
			if (extension == e)
				return Icon::FileImage;
		for (const char* e : { ".wav", ".mp3", ".flac", ".ogg" })
			if (extension == e)
				return Icon::FileSound;
		return Icon::File;
	}
}
