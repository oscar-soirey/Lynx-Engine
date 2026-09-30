#include "main_tab.h"

#include <iostream>
#include <QDockWidget>
#include <QLabel>
#include <QWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>

namespace hn::editor
{
	MainTab::MainTab(QWidget* parent): WindowTab(parent)
	{
		auto *content = new QWidget();
		auto *layout = new QVBoxLayout(content);

		auto *label = new QLabel("Bienvenue dans Horizon-Next !");
		auto *button = new QPushButton("Clique-moi");

		layout->addWidget(label);
		layout->addWidget(button);

		setCentralWidget(content);

		// =========================
		// Inspector
		// =========================

		auto *inspector = new QDockWidget("Inspector", this);

		inspector->setMinimumWidth(400);
		auto *inspectorWidget = new QWidget();

		auto *inspectorlayout = new QVBoxLayout(inspectorWidget);
		inspectorlayout->setAlignment(Qt::AlignTop);

		inspectorlayout->addWidget(new QPushButton("Bouton 1"));
		inspectorlayout->addWidget(new QPushButton("Bouton 2"));
		inspectorlayout->addWidget(new QLabel("Texte"));

		inspectorlayout->addStretch();

		inspector->setWidget(inspectorWidget);

		addDockWidget(
			Qt::RightDockWidgetArea,
			inspector
		);

		connect(button, &QPushButton::clicked, [label]
		{
			label->setText("Le bouton a été cliqué !");
			std::cout << "Le bouton a été cliqué" << std::endl;
			printf("Le bouton a été cliqué mais en printf");
		});
	}
}
