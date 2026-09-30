#pragma once

#include <QMainWindow>

namespace hn::editor
{
	class WindowTab : public QMainWindow {
	public:
		WindowTab(QWidget* parent);
		QWidget* GetParent() const;

	private:
		QWidget* parent_window_=nullptr;
	};
}
