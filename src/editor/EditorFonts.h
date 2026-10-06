#pragma once

// =============================================================================
// Editor fonts
// -----------------------------------------------------------------------------
// fonts/ next to the editor executable (copied there by CMake), or in the
// current folder :
//
//   fonts/VCR-OSD-MONO.ttf              editor (every window, code editors)
//   (Lynxie's messages use the editor font, at the normal size)
//   fonts/JetBrainsMono-Regular.ttf     code editors (+ -Bold, -Italic,
//                                       -BoldItalic : keywords, comments)
//
// The characters a font does not have (accents, symbols) come from a fallback
// merged into it : normal-font.ttf (the old editor font) if it is there,
// otherwise a Windows font (Consolas / Segoe UI). A missing file is skipped :
// the editor font falls back to normal-font.ttf, then to ImGui's own font ;
// Lynxie falls back to the editor font.
// =============================================================================

#include <imgui/imgui.h>

namespace lynx::editor::fonts
{
	// After ImGui::CreateContext, before the first frame. `size` : pixel size
	// of the editor font (Lynxie's font is a bit smaller : it is wider).
	void Load(float size);

	ImFont* Editor();          // never nullptr after Load (ImGui default font at worst)

	// nullptr when the file is missing (Lynxie then uses the editor font).
	ImFont* Lynxie();
	ImFont* LynxieBold();
	ImFont* LynxieItalic();
	ImFont* LynxieBoldItalic();

	// Lynxie's font : none any more (OpenDyslexic dropped), these do nothing
	// and Lynxie() / LynxieBold()... return nullptr. Kept for the callers.
	void PushLynxie();
	void PopLynxie();

	// Size Lynxie's text uses inside PushLynxie (same as the bold / italic ones).
	float LynxieScale();

	// Code editors : JetBrains Mono. Code() : the editor font when the file is
	// missing ; the others are nullptr when missing.
	ImFont* Code();
	ImFont* CodeBold();
	ImFont* CodeItalic();
	ImFont* CodeBoldItalic();

	// Size of the code font, relative to the editor font size.
	float CodeScale();
}
