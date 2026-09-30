#include "code_editor.h"

#include <QPainter>
#include <QTextBlock>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QFont>
#include <QPalette>

namespace hn::editor
{
  namespace
  {
    // Widget interne, non exposé : la marge où sont dessinés les numéros
    // de ligne. Il délègue tout le dessin à CodeEditor.
    class LineNumberArea : public QWidget
    {
    public:
     explicit LineNumberArea(CodeEditor* editor)
      : QWidget(editor), code_editor_(editor)
     {
     }

     QSize sizeHint() const override
     {
      return QSize(code_editor_->LineNumberAreaWidth(), 0);
     }

    protected:
     void paintEvent(QPaintEvent* event) override
     {
      code_editor_->LineNumberAreaPaintEvent(event);
     }

    private:
     CodeEditor* code_editor_ = nullptr;
    };
  }

  CodeEditor::CodeEditor(QWidget* parent)
   : QPlainTextEdit(parent)
  {
   line_number_area_ = new LineNumberArea(this);

   setLineWrapMode(QPlainTextEdit::NoWrap);
   setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);

   QFont mono_font;
   mono_font.setFamilies({"Cascadia Code", "Cascadia Mono", "Consolas", "Courier New"});
   mono_font.setStyleHint(QFont::Monospace);
   mono_font.setPointSize(10);
   setFont(mono_font);

   // Thème sombre forcé via stylesheet plutôt que QPalette : si l'application
   // a un thème QSS global (setStyleSheet sur QApplication), il prend le pas
   // sur la palette pour les widgets qu'il cible. Une règle QSS posée
   // directement sur ce widget est en revanche prioritaire sur la feuille
   // de style globale pour ce même widget.
   setStyleSheet(
    "QPlainTextEdit {"
    "  background-color: #1e1e1e;"
    "  color: #d4d4d4;"
    "  selection-background-color: #264f78;"
    "  selection-color: #ffffff;"
    "  border: none;"
    "}"
   );

   connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::UpdateLineNumberAreaWidth);
   connect(this, &CodeEditor::updateRequest, this, &CodeEditor::UpdateLineNumberArea);
   connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::HighlightCurrentLine);

   UpdateLineNumberAreaWidth(0);
   HighlightCurrentLine();
  }

  int CodeEditor::LineNumberAreaWidth() const
  {
   int digits = 1;
   int max_value = qMax(1, blockCount());
   while (max_value >= 10)
   {
    max_value /= 10;
    ++digits;
   }

   return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
  }

  void CodeEditor::UpdateLineNumberAreaWidth(int /*new_block_count*/)
  {
   setViewportMargins(LineNumberAreaWidth(), 0, 0, 0);
  }

  void CodeEditor::UpdateLineNumberArea(const QRect& rect, int dy)
  {
   if (dy != 0)
    line_number_area_->scroll(0, dy);
   else
    line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());

   if (rect.contains(viewport()->rect()))
    UpdateLineNumberAreaWidth(0);
  }

  void CodeEditor::resizeEvent(QResizeEvent* event)
  {
   QPlainTextEdit::resizeEvent(event);

   const QRect cr = contentsRect();
   line_number_area_->setGeometry(QRect(cr.left(), cr.top(), LineNumberAreaWidth(), cr.height()));
  }

  void CodeEditor::HighlightCurrentLine()
  {
   QList<QTextEdit::ExtraSelection> extra_selections;

   if (!isReadOnly())
   {
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(60, 60, 60));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    extra_selections.append(selection);
   }

   setExtraSelections(extra_selections);
  }

  void CodeEditor::LineNumberAreaPaintEvent(QPaintEvent* event)
  {
   QPainter painter(line_number_area_);
   painter.fillRect(event->rect(), QColor(45, 45, 45));

   QTextBlock block = firstVisibleBlock();
   int block_number = block.blockNumber();
   int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
   int bottom = top + qRound(blockBoundingRect(block).height());

   while (block.isValid() && top <= event->rect().bottom())
   {
    if (block.isVisible() && bottom >= event->rect().top())
    {
     const QString number = QString::number(block_number + 1);
     painter.setPen(QColor(150, 150, 150));
     painter.drawText(0, top, line_number_area_->width() - 6, fontMetrics().height(),
      Qt::AlignRight, number);
    }

    block = block.next();
    top = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
    ++block_number;
   }
  }

  void CodeEditor::keyPressEvent(QKeyEvent* event)
  {
   // Tab -> 2 espaces, plus cohérent pour du Lua que d'insérer '\t'.
   if (event->key() == Qt::Key_Tab)
   {
    insertPlainText("  ");
    return;
   }

   // Auto-indentation : la nouvelle ligne reprend l'indentation de la précédente.
   if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
   {
    const QString current_line = textCursor().block().text();
    QString indentation;
    for (const QChar& c : current_line)
    {
     if (c == ' ' || c == '\t')
      indentation += c;
     else
      break;
    }

    QPlainTextEdit::keyPressEvent(event);
    insertPlainText(indentation);
    return;
   }

   QPlainTextEdit::keyPressEvent(event);
  }

  void CodeEditor::wheelEvent(QWheelEvent* event)
  {
   if (event->modifiers() & Qt::ControlModifier)
   {
    const int delta = event->angleDelta().y();
    if (delta > 0)
     zoomIn(1);
    else if (delta < 0 && font().pointSize() > 6)
     zoomOut(1);

    event->accept();
    return;
   }

   QPlainTextEdit::wheelEvent(event);
  }
}