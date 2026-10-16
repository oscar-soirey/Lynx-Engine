// Lynxie's icon in the Commands window (raven/CommandsHost.cpp) : the real one
// (LynxieIcon.cpp) is an HRL texture of the editor's OpenGL context, which the
// ImGui window of Commands does not share. Until Commands is in Raven, the
// icon is replaced by an empty space of the same size and text buttons.
#include "../LynxieIcon.h"

namespace lynx::editor::lynxie_icon
{
	bool Ready() { return false; }
	void Draw(float height, ImVec4) { ImGui::Dummy(ImVec2(height, height)); }
	void DrawAt(ImDrawList*, const ImVec2&, float, ImU32) {}
	bool Button(const char* id, float) { return ImGui::SmallButton(id); }
	float WidthFor(float height) { return height; }
}
