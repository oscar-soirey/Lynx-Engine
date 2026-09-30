#include "application.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>
#include <QFontDatabase>

#include <iostream>

namespace hn::editor
{
	EditorApplication::EditorApplication(int argc, char *argv[])
	{
		app_ = new QApplication(argc, argv);


		QApplication::setStyle(QStyleFactory::create("Fusion"));

		QFile file("Editor/Styles/style.qss");

		if (file.open(QFile::ReadOnly | QFile::Text))
		{
			app_->setStyleSheet(file.readAll());
		}

		int fontId = QFontDatabase::addApplicationFont("Editor/Fonts/Ubuntu-Regular.ttf");

		if (fontId == -1)
		{
			std::cout << "Error while loading editor: main font is not valid" << std::endl;
		}
		QString family = QFontDatabase::applicationFontFamilies(fontId).at(0);
		QApplication::setFont(QFont(family));
	}

	QApplication* EditorApplication::GetApplication()
	{
		return app_;
	}

	std::string EditorApplication::GetFullMessageConsole()
	{
		return app_messages_;
	}

	void EditorApplication::AppendMessage(const char *msg)
	{
		app_messages_.append(std::string(msg) + '\n');
		ED_MessageReceived.Call();
	}
}
