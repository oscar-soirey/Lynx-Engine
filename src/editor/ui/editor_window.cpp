#include "editor_window.h"

#include <QCloseEvent>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include "title_bar.h"

namespace hn::editor
{
	EditorWindow::EditorWindow(const char* title)
	{
		//Configuration

		setWindowTitle(title);
		setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
		setWindowIcon(QIcon("Editor/logo.png"));


		central = new QWidget();
		layout = new QVBoxLayout(central);

		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);



		//Adding border
		central->setObjectName("central");
		setStyleSheet(
				"#central {"
				"    border: 1px solid #3f3f3f;"
				"}"
		);


		setCentralWidget(central);



		// =========================
		// Title bar
		// =========================

		title_bar_ = new TitleBar(this, title);

		layout->addWidget(title_bar_);
	}


	bool EditorWindow::ShouldClose() const
	{
		return should_close_;
	}

	void EditorWindow::closeEvent(QCloseEvent *event)
	{
		should_close_ = true;
		event->accept();
	}

	TitleBar *EditorWindow::GetTitleBar() const
	{
		return title_bar_;
	}
}
