// Le launcher réutilise le rendu Markdown de l'éditeur (src/editor/MarkdownText.cpp) pour les notes
// de version et les descriptions. Ce fichier remplace les polices et la sélection de texte de
// l'éditeur : une seule police (celle du launcher), pas de sélection.
#include "../src/editor/EditorFonts.h"
#include "../src/editor/TextSelection.h"

namespace lynx::editor::fonts
{
	ImFont* Editor() { return ImGui::GetFont(); }
	ImFont* Lynxie() { return nullptr; }
	ImFont* LynxieBold() { return nullptr; }
	ImFont* LynxieItalic() { return nullptr; }
	ImFont* LynxieBoldItalic() { return nullptr; }
}

namespace lynx::editor::text_selection
{
	void Record(ImFont*, float, const ImVec2&, const char*, const char*) {}
}
