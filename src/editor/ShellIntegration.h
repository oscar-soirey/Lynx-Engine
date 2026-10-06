#pragma once

// =============================================================================
// Windows Explorer : "Open with Lynx" in the right-click menu of a folder
// -----------------------------------------------------------------------------
// Right click ON a folder, or in the empty part of an open folder : starts
// this editor with that folder as project (LynxEditor.exe "<folder>"). A
// folder that is not a Lynx project opens the project browser with a message.
//
// Written in the registry of the current user (HKCU\Software\Classes, no
// administrator rights) at every start of the editor, so the menu always
// starts the last editor launched. Windows 11 : under "Show more options".
// =============================================================================

namespace lynx::editor::shell_integration
{
	/** Adds / updates the menu. false : could not write the registry (or not Windows). */
	bool Register();

	/** Removes the menu. */
	void Unregister();
}
