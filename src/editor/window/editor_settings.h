#pragma once

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include "../ui/editor_window.h"
#include "../common.h"

namespace hn::editor
{
	class EDITOR_API SettingSection : public QWidget {
	public:
		SettingSection(const char* name)
		{
			layout = new QVBoxLayout(this);

			auto *header = new QLabel(name);
			header->setStyleSheet(
				"font-size: 20px;"
				"font-weight: bold;"
			);
			layout->addWidget(header);
		}

	protected:
		QVBoxLayout* layout=nullptr;
	};



	EDITOR_API void AddSettingSection(const char* name, SettingSection* section);

	class SettingsWindow : public EditorWindow
	{
	public:
		explicit SettingsWindow(QWidget *parent = nullptr);
	};
}
