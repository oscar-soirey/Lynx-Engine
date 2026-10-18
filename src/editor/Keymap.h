#pragma once

// =============================================================================
// Keyboard shortcuts of the editor (Options > Keyboard)
// -----------------------------------------------------------------------------
// Every shortcut of the level editor is an Action with a default chord
// (key + Ctrl / Shift / Alt). The user's chords are saved in
//   Windows : %APPDATA%/Lynx/keybindings.cfg
//   others  : ~/.config/lynx/keybindings.cfg
// (one line per changed action : "save ctrl+s"), the same for every project.
//
// The editor asks Pressed(action) (went down this frame) or Down(action)
// (held : camera movement). Keys are GLFW codes (lynx::editor::input).
//
// Modifiers : Ctrl and Alt must be exactly as in the chord ; Shift is required
// when the chord has it, otherwise free (Shift is the fast camera, Ctrl+Shift+P
// is the command palette in "commands" mode...).
//
// The editors in tabs (graph editors, Widget Editor) keep their own Ctrl+S /
// Ctrl+Z... while they have the keyboard.
// =============================================================================

#include <Raven.h>

#include <string>

namespace lynx::editor::keymap
{
	enum class Action
	{
		Save = 0,
		Undo,
		DeleteSelection,
		CommandPalette,
		Play,
		Simulate,
		TogglePaint,
		CycleGizmo,
		PickVoxel,
		CollisionDebug,
		CameraForward,
		CameraBack,
		CameraLeft,
		CameraRight,
		CameraUp,
		CameraDown,
		Count
	};

	struct Chord
	{
		int key = 0;          // GLFW key code, 0 : none
		bool ctrl = false;
		bool shift = false;
		bool alt = false;

		bool operator==(const Chord& o) const
			{ return key == o.key && ctrl == o.ctrl && shift == o.shift && alt == o.alt; }
		bool operator!=(const Chord& o) const { return !(*this == o); }
	};

	/** Reads keybindings.cfg (once, at the start of the editor). */
	void Load();

	const Chord& Get(Action action);
	const Chord& Default(Action action);
	void Set(Action action, const Chord& chord);   // saved at once

	/** Held now (camera movement...). */
	bool Down(Action action);
	/** Went down this frame. */
	bool Pressed(Action action);

	/** "Ctrl+S", "F3", "" when the action has no key. */
	std::string Text(Action action);
	std::string ChordText(const Chord& chord);

	const char* Label(Action action);
	const char* Category(Action action);

	/** The Keyboard page of the Options window (widgets of windows/options.xml). */
	void BindPage(Rid window);

	/** Once per editor frame : the "press a key" capture of the Keyboard page. */
	void Update();

	/** A key is being captured : the shortcuts wait. */
	bool Capturing();
}
