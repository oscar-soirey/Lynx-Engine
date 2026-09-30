#include "syntax_highlighter.h"

namespace hn::editor
{
  GenericSyntaxHighlighter::GenericSyntaxHighlighter(QTextDocument* parent)
   : QSyntaxHighlighter(parent)
  {
  }

  QTextCharFormat GenericSyntaxHighlighter::MakeFormat(const QColor& color, bool bold, bool italic)
  {
   QTextCharFormat format;
   format.setForeground(color);

   if (bold)
    format.setFontWeight(QFont::Bold);

   if (italic)
    format.setFontItalic(true);

   return format;
  }

  void GenericSyntaxHighlighter::AddRule(const QString& pattern, const QTextCharFormat& format)
  {
   HighlightingRule rule;
   rule.pattern = QRegularExpression(pattern);
   rule.format = format;
   rules_.append(rule);
  }

  void GenericSyntaxHighlighter::AddKeywordRule(const QStringList& keywords, const QTextCharFormat& format)
  {
   for (const QString& keyword : keywords)
    AddRule(QStringLiteral("\\b%1\\b").arg(keyword), format);
  }

  void GenericSyntaxHighlighter::SetMultiLineCommentDelimiters(const QRegularExpression& start_pattern,
                                                                 const QRegularExpression& end_pattern,
                                                                 const QTextCharFormat& format)
  {
   has_multiline_comments_ = true;
   comment_start_pattern_ = start_pattern;
   comment_end_pattern_ = end_pattern;
   multiline_comment_format_ = format;
  }

  void GenericSyntaxHighlighter::highlightBlock(const QString& text)
  {
   // Règles "simples" : appliquées indépendamment, dans l'ordre d'ajout.
   // Une règle ajoutée après en écrase une autre sur les zones qui se
   // chevauchent (utile pour faire passer les commentaires par-dessus).
   for (const HighlightingRule& rule : rules_)
   {
    QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
    while (it.hasNext())
    {
     const QRegularExpressionMatch match = it.next();
     setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
   }

   if (!has_multiline_comments_)
    return;

   // Gestion des commentaires multi-lignes via l'état du bloc précédent
   // (mécanisme standard de QSyntaxHighlighter).
   setCurrentBlockState(0);

   int start_index = 0;
   if (previousBlockState() != 1)
    start_index = text.indexOf(comment_start_pattern_);

   while (start_index >= 0)
   {
    const QRegularExpressionMatch end_match = comment_end_pattern_.match(text, start_index);
    const int end_index = end_match.capturedStart();
    int comment_length = 0;

    if (end_index == -1)
    {
     setCurrentBlockState(1);
     comment_length = text.length() - start_index;
    }
    else
    {
     comment_length = end_index - start_index + end_match.capturedLength();
    }

    setFormat(start_index, comment_length, multiline_comment_format_);
    start_index = text.indexOf(comment_start_pattern_, start_index + comment_length);
   }
  }
}
