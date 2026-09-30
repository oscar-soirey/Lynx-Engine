#include "common.h"

#include "window/main_window.h"

#include <filesystem>
#include <fstream>

#include <json/json.hpp>

#include "window/loading_window.h"

namespace
{
	hn::editor::EditorRessources* ressources= new hn::editor::EditorRessources();
}

namespace hn::editor
{
	EditorRessources* GetEditorRessources()
	{
		return ressources;
	}

	void InitSetEditorApplication(EditorApplication *app)
	{
		if (ressources->application)
		{
			return;
		}
		ressources->application = app;
	}

	EditorApplication *GetApplication()
	{
		return ressources->application;
	}

	void InitSetMainWindow(EditorMain *window)
	{
		if (ressources->main_window)
		{
			return;
		}
		ressources->main_window = window;
	}

	EditorMain *GetMainWindow()
	{
		return ressources->main_window;
	}


	int OpenProjectRessource(const char *path)
	{

	}

	CommonProject* GetCurrentProject()
	{
		return ressources->current_project;
	}


	std::string EditorGetFileContent(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file)
		{
			std::cout << "Failed to open file: " << path.string() << std::endl;
			return {};
		}

		std::ostringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}
}
