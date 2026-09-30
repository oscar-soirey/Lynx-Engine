#pragma once

#include <QPlainTextEdit>

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QResizeEvent;
class QKeyEvent;
class QWheelEvent;
QT_END_NAMESPACE

namespace hn::editor
{
 // Widget d'édition de texte générique : numéros de ligne, indentation
 // automatique, surlignage de la ligne courante. Ne connaît rien au
 // langage édité (voir syntax_highlighter.h pour la coloration).
 class CodeEditor : public QPlainTextEdit
 {
  Q_OBJECT

 public:
  explicit CodeEditor(QWidget* parent = nullptr);

  void LineNumberAreaPaintEvent(QPaintEvent* event);
  int LineNumberAreaWidth() const;

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private slots:
  void UpdateLineNumberAreaWidth(int new_block_count);
  void HighlightCurrentLine();
  void UpdateLineNumberArea(const QRect& rect, int dy);

 private:
  QWidget* line_number_area_ = nullptr;
 };
}