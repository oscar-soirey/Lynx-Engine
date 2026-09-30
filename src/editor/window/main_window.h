#pragma once

#include "../ui/editor_window.h"

#include "../common.h"

namespace hn::editor
{
	class ContentBrowser;
	class MainTab;
	class ConsoleWidget;

	class EDITOR_API EditorMain : public EditorWindow
	{
	public:
		EditorMain();

		QTabWidget* GetTabWidget() const;

		MainTab* GetMainTab() const;



		QMenu* GetFileMenu() const;
		QMenu* GetEditMenu() const;
		QMenu* GetWindowMenu() const;


	private:
		QTabWidget *tabs_ = nullptr;
		MainTab* main_tab_=nullptr;


		//Default actions
		QMenu* file_menu_=nullptr;
		QMenu* edit_menu_=nullptr;
		QMenu* window_menu_=nullptr;


		//
		ConsoleWidget* console_dock=nullptr;
		ContentBrowser* content_browser_=nullptr;
	};
}
