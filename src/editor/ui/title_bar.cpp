#include "title_bar.h"

#include <iostream>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QFileDialog>
#include <QMouseEvent>
#include <QPlainTextEdit>

#include "application.h"
#include "../window/main_window.h"
#include "../tabs/main_tab.h"
#include "../window/editor_settings.h"

namespace hn::editor
{
	TitleBar::TitleBar(QWidget *parent, const char* title)
    : QWidget(parent)
	{

    setFixedHeight(35);

    layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(2);

  	auto *image = new QLabel();

  	QPixmap pixmap("Editor/logo.png");
  	image->setPixmap(pixmap);
  	image->setFixedSize(35,35);
  	image->setScaledContents(true);

  	layout->addWidget(image);

    auto *title_widget = new QLabel(title);
		layout->addWidget(title_widget);


		//Create menu layout
		menu_layout = new QHBoxLayout();
		menu_layout->setContentsMargins(0, 0, 0, 0);
		menu_layout->setSpacing(2);
		layout->addLayout(menu_layout);



    // =========================
    // Window buttons
    // =========================

    minimize = new QPushButton();
    maximize = new QPushButton();
    close = new QPushButton();

  	minimize->setIcon(QIcon("Editor/Icons/minimize.png"));
  	maximize->setIcon(QIcon("Editor/Icons/maximize.png"));
  	close->setIcon(QIcon("Editor/Icons/close.png"));

  	minimize->setIconSize(QSize(14, 14));
  	maximize->setIconSize(QSize(14, 14));
  	close->setIconSize(QSize(14, 14));

  	minimize->setFixedSize(35, 30);
  	maximize->setFixedSize(35, 30);
  	close->setFixedSize(35, 30);

    layout->addStretch();

		SetControlMax();

    // =========================
    // Buttons
    // =========================

    connect(close, &QPushButton::clicked, [this]()
    {
      window()->close();
    });

    connect(minimize, &QPushButton::clicked, [this]()
    {
      window()->showMinimized();
    });

    connect(maximize, &QPushButton::clicked, [this]()
    {
      toggleMaximized();
    });
  }


	void TitleBar::SetControlNone()
	{
		layout->removeWidget(minimize);
		layout->removeWidget(maximize);
		layout->removeWidget(close);
	}

	void TitleBar::SetControlMinimal()
	{
		layout->removeWidget(minimize);
		layout->removeWidget(maximize);
    layout->addWidget(close);
	}

	void TitleBar::SetControlMax()
	{
		layout->addWidget(minimize);
		layout->addWidget(maximize);
		layout->addWidget(close);
	}


	void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
	{
		if (event->button() == Qt::LeftButton)
		{
			toggleMaximized();
		}

		QWidget::mouseDoubleClickEvent(event);
	}

	void TitleBar::mousePressEvent(QMouseEvent *event)
	{
		if (event->button() == Qt::LeftButton)
		{
			dragging_ = true;

			dragStartPosition_ =
				event->globalPosition().toPoint();

			dragStartOffsetY_ =
				event->position().toPoint().y();

			dragPosition_ =
				event->globalPosition().toPoint() -
				window()->frameGeometry().topLeft();
		}

		QWidget::mousePressEvent(event);
	}

	void TitleBar::mouseMoveEvent(QMouseEvent *event)
	{
		if (!dragging_)
		{
			QWidget::mouseMoveEvent(event);
			return;
		}

		QPoint currentPosition = event->globalPosition().toPoint();

		// Si la fenêtre est maximisée et qu'on commence à la déplacer
		if (window()->isMaximized())
		{
			// Pourcentage horizontal de la souris dans la fenêtre
			double xRatio =
				static_cast<double>(dragStartPosition_.x()) /
				window()->width();

			// On démaximise
			window()->showNormal();

			// Nouvelle position pour garder la souris
			// approximativement au même endroit dans la title bar
			int x =
				currentPosition.x() -
				static_cast<int>(window()->width() * xRatio);

			int y =
				currentPosition.y() -
				dragStartOffsetY_;

			window()->move(x, y);

			// On ne veut plus utiliser l'ancien dragPosition_
			dragPosition_ =
				currentPosition -
				window()->frameGeometry().topLeft();

			dragStartPosition_ = currentPosition;
		}
		else
		{
			window()->move(
				currentPosition - dragPosition_
			);
		}

		QWidget::mouseMoveEvent(event);
	}

	void TitleBar::mouseReleaseEvent(QMouseEvent *event)
	{
		if (event->button() == Qt::LeftButton)
			dragging_ = false;

		QWidget::mouseReleaseEvent(event);
	}

	void TitleBar::toggleMaximized()
	{
		if (window()->isMaximized())
			window()->showNormal();
		else
			window()->showMaximized();
	}


	QMenu* TitleBar::AddMenu(const char *name, const char *icon_path)
	{
		auto *button = new QPushButton(name);
		button->setFlat(true);

		auto *menu = new QMenu(this);


		if (!std::string(icon_path).empty())
		{
			button->setIcon(QIcon(icon_path));
		}

		button->setMenu(menu);

		menu_layout->addWidget(button);

		return menu;
	}

	QAction* TitleBar::AddAction(QMenu *parent, const char *name, const char* icon_path, bool checkable, const EditorDispatcher<bool>& checked)
	{
		if (parent)
		{
			auto* action = parent->addAction(name);

			action->setIcon(QIcon(icon_path));

			action->setCheckable(checkable);
			if (checkable)
			{
				action->setChecked(false);
				connect(action, &QAction::triggered, [action, checked]
				{
					checked.Call(action->isChecked());
				});
			}

			return action;
		}
		return nullptr;
	}

	void TitleBar::AddSeparator(QMenu *menu)
	{
		menu->addSeparator();
	}

	void TitleBar::AddCategory(QMenu *menu, const char *name, const char *icon_path)
	{
		if (!menu)
			return;

		menu->addSection(name);
	}
}
