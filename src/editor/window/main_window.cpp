#include "main_window.h"

#include <iostream>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QMainWindow>
#include <QVBoxLayout>

#include "editor_settings.h"
#include "../tabs/main_tab.h"
#include "../ui/title_bar.h"
#include "../widgets/content_browser.h"

namespace hn::editor
{
	EditorMain::EditorMain(): EditorWindow("Horizon-Next 2026 [beta]")
	{
		resize(1300, 900);

		/** Title bar */

		// =========================
		// File
		// =========================

		file_menu_ = GetTitleBar()->AddMenu("File", "");

		GetTitleBar()->AddAction(file_menu_, "New");

		auto *openAction = GetTitleBar()->AddAction(file_menu_, "Open");

		connect(openAction, &QAction::triggered, [this]()
		{
			QString file = QFileDialog::getOpenFileName(
				window(),
				"Open File",
				"",
				"All Files (*.*)"
			);

			if (!file.isEmpty())
			{
				std::cout << file.toStdString() << std::endl;
			}
		});

		GetTitleBar()->AddAction(file_menu_, "Save");

		GetTitleBar()->AddSeparator(file_menu_);

		auto *exitAction = GetTitleBar()->AddAction(file_menu_, "Exit");

		connect(exitAction, &QAction::triggered, [this]()
		{
			window()->close();
		});

		GetTitleBar()->AddSeparator(file_menu_);

		auto* settingsAction = GetTitleBar()->AddAction(file_menu_, "Editor Settings", "Editor/Icons/settings.png");
		connect(settingsAction, &QAction::triggered, [this]()
		{
			auto* settings = new SettingsWindow();
			settings->show();
		});

		auto* downloadPluginsAction = GetTitleBar()->AddAction(file_menu_, "Download Plugins", "Editor/Icons/plugin.png");
		connect(downloadPluginsAction, &QAction::triggered, [this]()
		{
		});

		// =========================
		// Edit
		// =========================

		edit_menu_ = GetTitleBar()->AddMenu("Edit", "");

		GetTitleBar()->AddAction(edit_menu_, "Undo");
		GetTitleBar()->AddAction(edit_menu_, "Redo");

		GetTitleBar()->AddSeparator(edit_menu_);

		GetTitleBar()->AddAction(edit_menu_, "Duplicate");
		GetTitleBar()->AddAction(edit_menu_, "Delete");


		//Window
		window_menu_ = GetTitleBar()->AddMenu("Window", "");

		EditorDispatcher<bool> ED_ContentBrowser;
		ED_ContentBrowser.Subscribe([this](bool visible)
		{
			if (visible)
			{
				content_browser_ = new ContentBrowser(this);
				GetMainTab()->addDockWidget(
						Qt::BottomDockWidgetArea,
						content_browser_
				);
			}
			else
			{
				delete content_browser_;
				content_browser_ = nullptr;
			}
		});
		GetTitleBar()->AddAction(window_menu_, "Content Browser",
			"Editor/Icons/file-explorer.png", true, ED_ContentBrowser);



		// =========================
		// Tabs
		// =========================

		tabs_ = new QTabWidget();
		layout->addWidget(tabs_);


		//Main tab
		main_tab_ = new MainTab(this);
		tabs_->addTab(main_tab_, "Main editor");
	}

	QTabWidget *EditorMain::GetTabWidget() const
	{
		return tabs_;
	}

	MainTab *EditorMain::GetMainTab() const
	{
		return main_tab_;
	}





	QMenu *EditorMain::GetFileMenu() const
	{
		return file_menu_;
	}

	QMenu *EditorMain::GetEditMenu() const
	{
		return edit_menu_;
	}
}
