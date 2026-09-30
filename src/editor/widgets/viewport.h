#pragma once

#include <QWidget>

namespace hn::editor
{
	class ViewportWidget : public QWidget
	{
	public:
		explicit ViewportWidget(QWidget* parent = nullptr)
				: QWidget(parent)
		{
			setAttribute(Qt::WA_NativeWindow);
			setAttribute(Qt::WA_OpaquePaintEvent);
		}

		WId nativeHandle() const
		{
			return winId();
		}
	};
}