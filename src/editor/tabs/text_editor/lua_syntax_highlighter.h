#pragma once

#include "syntax_highlighter.h"

namespace hn::editor
{
  // Coloration syntaxique pour le Lua (mots-clés, chaînes, nombres,
  // commentaires simples et multi-lignes, appels de fonction).
  class LuaSyntaxHighlighter : public GenericSyntaxHighlighter
  {
   Q_OBJECT

  public:
   explicit LuaSyntaxHighlighter(QTextDocument* parent = nullptr);
  };
}
