#include "loading_window.h"

#include <QLabel>
#include <QMovie>
#include <QVBoxLayout>

#include "../ui/title_bar.h"

namespace hn::editor
{
	LoadingWindow::LoadingWindow()
		: EditorWindow("")
	{
		setWindowFlags(
				Qt::Dialog |
				Qt::FramelessWindowHint |
				Qt::WindowStaysOnTopHint
		);

		GetTitleBar()->SetControlNone();

		setWindowModality(Qt::ApplicationModal);

		resize(200, 200);

		auto* label = new QLabel(this);

		auto* movie = new QMovie("Editor/Icons/loading.gif");
		movie->setScaledSize({100,100});

		qDebug() << "Movie valid:" << movie->isValid();

		label->setMovie(movie);

		movie->start();

		qDebug() << QMovie::supportedFormats();
		qDebug() << QCoreApplication::libraryPaths();

		layout->addWidget(label, 0, Qt::AlignCenter);
	}
}
