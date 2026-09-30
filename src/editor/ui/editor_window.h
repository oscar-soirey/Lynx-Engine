#pragma once

#include <QMainWindow>
#include "../common.h"

class QVBoxLayout;

namespace hn::editor
{
	class TitleBar;

	class EDITOR_API EditorWindow : public QMainWindow {
	public:
		EditorWindow(const char* title);

		bool ShouldClose() const;

		TitleBar* GetTitleBar() const;

	protected:

		void closeEvent(QCloseEvent *event) override;

		QWidget* central=nullptr;
		QVBoxLayout* layout=nullptr;

	private:
		bool should_close_ = false;

		TitleBar* title_bar_=nullptr;


		//handle window resize
		enum class ResizeDirection
		{
			None,
			Left,
			Right,
			Top,
			Bottom,
			TopLeft,
			TopRight,
			BottomLeft,
			BottomRight
		};

		ResizeDirection resizeDirection_ = ResizeDirection::None;
		bool resizing_ = false;

		QPoint resizeStartPosition_;
		QRect resizeStartGeometry_;

		int resizeBorder_ = 6;
	};
}
