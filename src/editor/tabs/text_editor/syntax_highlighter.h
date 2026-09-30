#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>
#include <QStringList>
#include <QColor>
#include <QFont>

namespace hn::editor
{
  // Base commune pour tous les highlighters. Un langage se définit en
  // dérivant cette classe et en appelant AddRule / AddKeywordRule /
  // SetMultiLineCommentDelimiters dans son constructeur. Voir
  // lua_syntax_highlighter.h pour un exemple concret.
  class GenericSyntaxHighlighter : public QSyntaxHighlighter
  {
   Q_OBJECT

  public:
   explicit GenericSyntaxHighlighter(QTextDocument* parent = nullptr);
   ~GenericSyntaxHighlighter() override = default;

   static QTextCharFormat MakeFormat(const QColor& color, bool bold = false, bool italic = false);

  protected:
   void highlightBlock(const QString& text) override;

   // Règle simple : un pattern regex appliqué sur chaque bloc de texte.
   void AddRule(const QString& pattern, const QTextCharFormat& format);

   // Raccourci pour une liste de mots-clés (ajoute automatiquement \b...\b).
   void AddKeywordRule(const QStringList& keywords, const QTextCharFormat& format);

   // Commentaires multi-lignes (ex: --[[ ... ]] en Lua, /* ... */ en C).
   void SetMultiLineCommentDelimiters(const QRegularExpression& start_pattern,
                                       const QRegularExpression& end_pattern,
                                       const QTextCharFormat& format);

  private:
   struct HighlightingRule
   {
    QRegularExpression pattern;
    QTextCharFormat format;
   };

   QVector<HighlightingRule> rules_;

   bool has_multiline_comments_ = false;
   QRegularExpression comment_start_pattern_;
   QRegularExpression comment_end_pattern_;
   QTextCharFormat multiline_comment_format_;
  };
}
