#include "window_tab.h"

namespace hn::editor
{
	WindowTab::WindowTab(QWidget* parent): parent_window_(parent)
	{

	}

	QWidget *WindowTab::GetParent() const
	{
		return parent_window_;
	}
}
