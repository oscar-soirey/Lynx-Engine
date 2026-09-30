#include "general.h"

#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>
#include <QComboBox>
#include <QWidget>

namespace hn::editor
{
	GeneralSection::GeneralSection(): SettingSection("General")
	{
		layout->addWidget(new QLabel("Language"));

		auto* language = new QComboBox();
		language->addItem("English");
		language->addItem("Français");

		layout->addWidget(language);

		layout->addStretch();
	}
}
