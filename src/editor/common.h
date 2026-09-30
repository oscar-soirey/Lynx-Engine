#pragma once
#include <filesystem>
#include <iostream>

#ifdef EDITOR_BUILD
#define EDITOR_API __declspec(dllexport)
#else
#define EDITOR_API __declspec(dllimport)
#endif

#include <string>

namespace hn::editor
{
	namespace fs = std::filesystem;

	class EditorApplication;
	class EditorMain;

	struct EDITOR_API CommonProject {
		std::string path;
		std::string ressources_path;
	};

	struct EDITOR_API EditorRessources {

		EditorApplication* application=nullptr;
		EditorMain* main_window=nullptr;
		CommonProject* current_project=nullptr;
	};

	EditorRessources* GetEditorRessources();

	void InitSetEditorApplication(EditorApplication* app);
	EDITOR_API EditorApplication* GetApplication();

	void InitSetMainWindow(EditorMain* window);
	EDITOR_API EditorMain* GetMainWindow();

	//This function opens a new project and close the current
	int OpenProjectRessource(const char* path);
	EDITOR_API CommonProject* GetCurrentProject();


	EDITOR_API std::string EditorGetFileContent(const std::filesystem::path& path);
}