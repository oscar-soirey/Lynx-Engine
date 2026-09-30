#include "lua_syntax_highlighter.h"

namespace hn::editor
{
  LuaSyntaxHighlighter::LuaSyntaxHighlighter(QTextDocument* parent)
   : GenericSyntaxHighlighter(parent)
  {
   const QTextCharFormat keyword_format = MakeFormat(QColor(197, 134, 192), true);
   AddKeywordRule({
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while"
   }, keyword_format);

   const QTextCharFormat builtin_format = MakeFormat(QColor(86, 182, 194));
   AddKeywordRule({
    "print", "pairs", "ipairs", "type", "tostring", "tonumber", "require",
    "pcall", "xpcall", "error", "assert", "setmetatable", "getmetatable",
    "table", "string", "math", "os", "io", "self"
   }, builtin_format);

   // Appel de fonction : identifiant suivi d'une parenthèse.
   const QTextCharFormat function_call_format = MakeFormat(QColor(220, 220, 170));
   AddRule("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()", function_call_format);

   const QTextCharFormat number_format = MakeFormat(QColor(181, 206, 168));
   AddRule("\\b[0-9]+(\\.[0-9]+)?\\b", number_format);

   const QTextCharFormat string_format = MakeFormat(QColor(206, 145, 120));
   AddRule("\".*?\"", string_format);
   AddRule("'.*?'", string_format);
   AddRule("\\[\\[.*?\\]\\]", string_format);

   // Commentaire simple : "--" pas suivi de "[[" (sinon c'est un bloc).
   const QTextCharFormat comment_format = MakeFormat(QColor(106, 153, 85), false, true);
   AddRule("--(?!\\[\\[).*", comment_format);

   SetMultiLineCommentDelimiters(QRegularExpression("--\\[\\["), QRegularExpression("\\]\\]"), comment_format);
  }
}
