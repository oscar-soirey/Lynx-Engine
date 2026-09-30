#pragma once

#include <QWidget>
#include "../common.h"
#include "../utils/editor_dispatcher.h"

class QPushButton;
class QHBoxLayout;

namespace hn::editor
{
class EDITOR_API TitleBar : public QWidget
{
public:
  explicit TitleBar(QWidget *parent, const char* title);

	void SetControlNone();
	void SetControlMinimal();
	void SetControlMax();

	QMenu* AddMenu(const char* name, const char* icon_path="");
	QAction* AddAction(QMenu* parent, const char* name, const char* icon_path="", bool checkable=false, const EditorDispatcher<bool>& checked={});
	void AddSeparator(QMenu* menu);
	void AddCategory(QMenu *menu, const char* name, const char* icon_path="");

protected:

  void mouseDoubleClickEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:

	QHBoxLayout* layout=nullptr;
	QHBoxLayout* menu_layout=nullptr;

  void toggleMaximized();

  bool dragging_ = false;
  QPoint dragPosition_;
	QPoint dragStartPosition_;
	int dragStartOffsetY_ = 0;

	QPushButton* minimize=nullptr;
	QPushButton* maximize=nullptr;
	QPushButton* close=nullptr;
};
}