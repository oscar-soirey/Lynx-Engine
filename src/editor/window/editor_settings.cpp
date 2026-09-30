#include "editor_settings.h"

#include "setting_sections/general.h"

#include <QListWidget>
#include <QStackedWidget>

#include <vector>
#include <string>

static std::vector<std::pair<std::string, hn::editor::SettingSection*>> sections_;

namespace hn::editor
{
	void AddSettingSection(const char *name, SettingSection *section)
	{
		sections_.emplace_back(name, section);
	}

	SettingsWindow::SettingsWindow(QWidget *parent)
				:EditorWindow("Settings")
	{
		resize(700, 550);

		auto* h_layout = new QHBoxLayout();

		auto* sections = new QListWidget();
		auto* pages = new QStackedWidget();



		AddSettingSection("General", new GeneralSection());
		AddSettingSection("Appearance", new SettingSection("Appearance"));
		AddSettingSection("Editor", new SettingSection("Editor"));
		AddSettingSection("Graphics", new SettingSection("Graphics"));
		AddSettingSection("Audio", new SettingSection("Audio"));

		// Sections
		for (const auto& [n, s]: sections_)
		{
			sections->addItem(n.c_str());
			pages->addWidget(s);
		}


		// Largeur de la barre de gauche
		sections->setFixedWidth(180);

		h_layout->addWidget(sections);
		h_layout->addWidget(pages);

		layout->addLayout(h_layout);

		// Changement de page
		connect(
				sections,
				&QListWidget::currentRowChanged,
				pages,
				&QStackedWidget::setCurrentIndex
		);

		sections->setCurrentRow(0);
	}
}
