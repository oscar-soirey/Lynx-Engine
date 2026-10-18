#include "Keymap.h"

#include "raven/RavenUi.h"

#include <glfw/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace lynx::editor::keymap
{
	namespace
	{
		constexpr int kCount = static_cast<int>(Action::Count);

		struct Info
		{
			const char* id;        // name in keybindings.cfg
			const char* label;
			const char* category;
			Chord chord;           // default
		};

		// Same order as Action.
		const Info kInfo[kCount] = {
			{ "save",            "Save the level",                  "Level",  { GLFW_KEY_S, true, false, false } },
			{ "undo",            "Undo",                            "Level",  { GLFW_KEY_Z, true, false, false } },
			{ "delete",          "Delete the selected actors",      "Level",  { GLFW_KEY_DELETE, false, false, false } },
			{ "command_palette", "Command palette (Shift : commands)", "Editor", { GLFW_KEY_P, true, false, false } },
			{ "play",            "Play / Stop",                     "Game",   { GLFW_KEY_F3, false, false, false } },
			{ "simulate",        "Simulate / Stop",                 "Game",   { GLFW_KEY_S, false, false, true } },
			{ "toggle_paint",    "Paint / Select mode",             "Tools",  { GLFW_KEY_TAB, false, false, false } },
			{ "cycle_gizmo",     "Gizmo : translate, rotate, scale", "Tools", { GLFW_KEY_SPACE, false, false, false } },
			{ "pick_voxel",      "Pick the voxel type under the mouse", "Tools", { GLFW_KEY_C, false, false, false } },
			{ "collision_debug", "Show the collisions",             "Tools",  { GLFW_KEY_F4, false, false, false } },
			{ "camera_forward",  "Camera forward / up (2D)",        "Camera", { GLFW_KEY_W, false, false, false } },
			{ "camera_back",     "Camera back / down (2D)",         "Camera", { GLFW_KEY_S, false, false, false } },
			{ "camera_left",     "Camera left",                     "Camera", { GLFW_KEY_A, false, false, false } },
			{ "camera_right",    "Camera right",                    "Camera", { GLFW_KEY_D, false, false, false } },
			{ "camera_up",       "Camera up (3D)",                  "Camera", { GLFW_KEY_E, false, false, false } },
			{ "camera_down",     "Camera down (3D)",                "Camera", { GLFW_KEY_Q, false, false, false } },
		};

		std::array<Chord, kCount> g_chords = []()
		{
			std::array<Chord, kCount> chords{};
			for (int i = 0; i < kCount; ++i)
				chords[static_cast<size_t>(i)] = kInfo[i].chord;
			return chords;
		}();

		// Options > Keyboard
		Rid g_list = R_INVALID_ID;
		Rid g_hint = R_INVALID_ID;
		std::array<Rid, kCount> g_buttons{};
		std::array<Rid, kCount> g_resets{};
		int g_capture = -1;            // action waiting for its key (-1 : none)

		std::filesystem::path FilePath()
		{
			std::filesystem::path dir;
#ifdef _WIN32
			if (const char* appdata = std::getenv("APPDATA"))
				dir = std::filesystem::path(appdata) / "Lynx";
#else
			if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
				dir = std::filesystem::path(xdg) / "lynx";
			else if (const char* home = std::getenv("HOME"))
				dir = std::filesystem::path(home) / ".config" / "lynx";
#endif
			if (dir.empty())
				dir = std::filesystem::current_path();
			return dir / "keybindings.cfg";
		}

		// ---- key names ----------------------------------------------------------

		std::string KeyName(int key)
		{
			if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
				return std::string(1, static_cast<char>('A' + (key - GLFW_KEY_A)));
			if (key >= GLFW_KEY_0 && key <= GLFW_KEY_0 + 9)
				return std::string(1, static_cast<char>('0' + (key - GLFW_KEY_0)));
			if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25)
				return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
			if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
				return "Num" + std::to_string(key - GLFW_KEY_KP_0);
			switch (key)
			{
			case GLFW_KEY_SPACE: return "Space";
			case GLFW_KEY_APOSTROPHE: return "'";
			case GLFW_KEY_COMMA: return ",";
			case GLFW_KEY_MINUS: return "-";
			case GLFW_KEY_PERIOD: return ".";
			case GLFW_KEY_SLASH: return "/";
			case GLFW_KEY_SEMICOLON: return ";";
			case GLFW_KEY_EQUAL: return "=";
			case GLFW_KEY_LEFT_BRACKET: return "[";
			case GLFW_KEY_BACKSLASH: return "\\";
			case GLFW_KEY_RIGHT_BRACKET: return "]";
			case GLFW_KEY_GRAVE_ACCENT: return "`";
			case GLFW_KEY_ESCAPE: return "Escape";
			case GLFW_KEY_ENTER: return "Enter";
			case GLFW_KEY_TAB: return "Tab";
			case GLFW_KEY_BACKSPACE: return "Backspace";
			case GLFW_KEY_INSERT: return "Insert";
			case GLFW_KEY_DELETE: return "Delete";
			case GLFW_KEY_RIGHT: return "Right";
			case GLFW_KEY_LEFT: return "Left";
			case GLFW_KEY_DOWN: return "Down";
			case GLFW_KEY_UP: return "Up";
			case GLFW_KEY_PAGE_UP: return "PageUp";
			case GLFW_KEY_PAGE_DOWN: return "PageDown";
			case GLFW_KEY_HOME: return "Home";
			case GLFW_KEY_END: return "End";
			case GLFW_KEY_CAPS_LOCK: return "CapsLock";
			case GLFW_KEY_PRINT_SCREEN: return "PrintScreen";
			case GLFW_KEY_PAUSE: return "Pause";
			case GLFW_KEY_KP_DECIMAL: return "Num.";
			case GLFW_KEY_KP_DIVIDE: return "Num/";
			case GLFW_KEY_KP_MULTIPLY: return "Num*";
			case GLFW_KEY_KP_SUBTRACT: return "Num-";
			case GLFW_KEY_KP_ADD: return "Num+";
			case GLFW_KEY_KP_ENTER: return "NumEnter";
			case GLFW_KEY_KP_EQUAL: return "Num=";
			case GLFW_KEY_MENU: return "Menu";
			default: return key > 0 ? "Key" + std::to_string(key) : std::string();
			}
		}

		int KeyFromName(const std::string& name)
		{
			if (name.empty() || name == "none")
				return 0;
			for (int key = 1; key <= GLFW_KEY_LAST; ++key)
			{
				const std::string n = KeyName(key);
				if (!n.empty() && n.size() == name.size() &&
				    std::equal(n.begin(), n.end(), name.begin(), [](char a, char b)
				    { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
					return key;
			}
			if (name.rfind("Key", 0) == 0)
				return std::atoi(name.c_str() + 3);
			return 0;
		}

		// "ctrl+shift+s" -> chord ; false when it can not be read.
		bool Parse(const std::string& text, Chord& out)
		{
			Chord c;
			std::string part;
			std::stringstream in(text);
			std::vector<std::string> parts;
			while (std::getline(in, part, '+'))
				parts.push_back(part);
			if (!text.empty() && text.back() == '+')   // the key "+" ... is "Num+" here : never
				return false;
			for (size_t i = 0; i < parts.size(); ++i)
			{
				std::string p = parts[i];
				std::string lower = p;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
				if (i + 1 < parts.size() && lower == "ctrl") c.ctrl = true;
				else if (i + 1 < parts.size() && lower == "shift") c.shift = true;
				else if (i + 1 < parts.size() && lower == "alt") c.alt = true;
				else if (i + 1 == parts.size()) c.key = KeyFromName(p);
				else return false;
			}
			out = c;
			return true;
		}

		void Save()
		{
			const std::filesystem::path path = FilePath();
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream out(path, std::ios::trunc);
			if (!out)
			{
				std::cerr << "[KEYS] Could not write " << path.string() << "\n";
				return;
			}
			out << "# Lynx editor shortcuts (only the ones changed from the defaults)\n";
			for (int i = 0; i < kCount; ++i)
			{
				const Chord& c = g_chords[static_cast<size_t>(i)];
				if (c == kInfo[i].chord)
					continue;
				std::string text = ChordText(c);
				out << kInfo[i].id << " " << (text.empty() ? std::string("none") : text) << "\n";
			}
		}

		bool ModifiersMatch(const Chord& c)
		{
			if (input::Ctrl() != c.ctrl || input::Alt() != c.alt)
				return false;
			return !c.shift || input::Shift();
		}

		bool IsModifierKey(int key)
		{
			return key >= GLFW_KEY_LEFT_SHIFT && key <= GLFW_KEY_RIGHT_SUPER;
		}

		// ---- Options > Keyboard -------------------------------------------------

		void Refresh()
		{
			if (g_list == R_INVALID_ID)
				return;
			for (int i = 0; i < kCount; ++i)
			{
				const Action a = static_cast<Action>(i);
				const Rid button = g_buttons[static_cast<size_t>(i)];
				if (button == R_INVALID_ID)
					continue;
				std::string text = g_capture == i ? "Press a key... (Esc : cancel)" : Text(a);
				if (text.empty())
					text = "(none)";
				ui::SetText(button, text);
				ui::SetClass(button, "active", g_capture == i);

				// The same chord on another action : shown.
				std::string conflict;
				const Chord& c = Get(a);
				for (int j = 0; j < kCount && c.key; ++j)
					if (j != i && Get(static_cast<Action>(j)) == c)
						conflict = std::string("Also used by : ") + kInfo[j].label;
				ui::SetTooltip(button, conflict.empty() ? std::string("Click, then press the new key (with Ctrl / Shift / Alt)") : conflict);
				ui::SetClass(button, "warning", !conflict.empty());
				ui::SetEnabled(g_resets[static_cast<size_t>(i)], Get(a) != Default(a));
			}
			ui::SetText(g_hint, g_capture >= 0 ? std::string("Press the keys for \"") + kInfo[g_capture].label + "\"..." : std::string());
		}
	}

	void Load()
	{
		std::ifstream in(FilePath());
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty() || line[0] == '#')
				continue;
			std::stringstream fields(line);
			std::string id, chord;
			fields >> id >> chord;
			for (int i = 0; i < kCount; ++i)
				if (id == kInfo[i].id)
				{
					Chord c;
					if (chord == "none")
						g_chords[static_cast<size_t>(i)] = Chord{};
					else if (Parse(chord, c))
						g_chords[static_cast<size_t>(i)] = c;
				}
		}
	}

	const Chord& Get(Action action)
	{
		return g_chords[static_cast<size_t>(action)];
	}

	const Chord& Default(Action action)
	{
		return kInfo[static_cast<int>(action)].chord;
	}

	void Set(Action action, const Chord& chord)
	{
		g_chords[static_cast<size_t>(action)] = chord;
		Save();
		Refresh();
	}

	bool Down(Action action)
	{
		const Chord& c = Get(action);
		return c.key != 0 && g_capture < 0 && input::Key(c.key) && ModifiersMatch(c);
	}

	bool Pressed(Action action)
	{
		const Chord& c = Get(action);
		return c.key != 0 && g_capture < 0 && input::KeyPressed(c.key) && ModifiersMatch(c);
	}

	std::string ChordText(const Chord& c)
	{
		if (!c.key)
			return {};
		std::string text;
		if (c.ctrl) text += "Ctrl+";
		if (c.shift) text += "Shift+";
		if (c.alt) text += "Alt+";
		return text + KeyName(c.key);
	}

	std::string Text(Action action)
	{
		return ChordText(Get(action));
	}

	const char* Label(Action action)
	{
		return kInfo[static_cast<int>(action)].label;
	}

	const char* Category(Action action)
	{
		return kInfo[static_cast<int>(action)].category;
	}

	bool Capturing()
	{
		return g_capture >= 0;
	}

	void HookWindow(Rid window);

	void BindPage(Rid window)
	{
		g_list = ui::Find(window, "keys.list");
		g_hint = ui::Find(window, "keys.hint");
		if (g_list == R_INVALID_ID)
			return;

		static const char kRow[] =
			"<HBox Spacing='6' Slot.Padding='0,1'>"
			"  <Label Name='label' Slot.Fill='1' Slot.VAlign='Center'/>"
			"  <Button Name='chord' Width='190' Focusable='false'/>"
			"  <Button Name='clear' Text='Clear' Class='tool' Tooltip='No key for this action'/>"
			"  <Button Name='reset' Text='Default' Class='tool'/>"
			"</HBox>";

		Rwidget_clear_children(g_list);
		std::string category;
		for (int i = 0; i < kCount; ++i)
		{
			const Action a = static_cast<Action>(i);
			if (category != kInfo[i].category)
			{
				category = kInfo[i].category;
				std::string upper = category;
				std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
				Rid title = Rwidget_add_child(g_list, Rlabel_create(upper.c_str()));
				Rwidget_add_class(title, "section");
				Rslot_set_padding(title, 0.f, 8.f, 0.f, 2.f);
			}
			Rid row = Rwidget_create_from_xml(kRow, sizeof(kRow) - 1);
			if (row == R_INVALID_ID)
				continue;
			Rwidget_add_child(g_list, row);
			ui::SetText(Rwidget_find(row, "label"), kInfo[i].label);
			const Rid chord = Rwidget_find(row, "chord");
			g_buttons[static_cast<size_t>(i)] = chord;
			g_resets[static_cast<size_t>(i)] = Rwidget_find(row, "reset");
			ui::SetTooltip(g_resets[static_cast<size_t>(i)], "Back to " + (ChordText(kInfo[i].chord).empty() ? std::string("none") : ChordText(kInfo[i].chord)));
			ui::OnClick(chord, [i]()
			{
				g_capture = g_capture == i ? -1 : i;
				Refresh();
			});
			ui::OnClick(Rwidget_find(row, "clear"), [a]() { g_capture = -1; Set(a, Chord{}); });
			ui::OnClick(g_resets[static_cast<size_t>(i)], [a]() { g_capture = -1; Set(a, Default(a)); });
		}

		HookWindow(Rwidget_get_window(g_list));

		ui::OnClick(ui::Find(window, "keys.reset_all"), []()
		{
			g_capture = -1;
			for (int i = 0; i < kCount; ++i)
				g_chords[static_cast<size_t>(i)] = kInfo[i].chord;
			Save();
			Refresh();
		});
		Refresh();
	}

	namespace
	{
		// Keys typed in the Options window (a window of its own : the input
		// state of the editor only sees the main window).
		int OnOptionsEvent(Rid, const REvent* event, void*)
		{
			if (g_capture < 0 || !event || event->type != R_EVENT_KEY_DOWN)
				return R_FALSE;
			const int key = input::ToGlfw(event->key);
			if (key <= 0 || IsModifierKey(key))
				return R_TRUE;   // Ctrl / Shift / Alt alone : wait for the key
			const int action = g_capture;
			g_capture = -1;
			if (key == GLFW_KEY_ESCAPE)
			{
				Refresh();
				return R_TRUE;
			}
			Chord c;
			c.key = key;
			c.ctrl = (event->modifiers & R_MOD_CTRL) != 0;
			c.shift = (event->modifiers & R_MOD_SHIFT) != 0;
			c.alt = (event->modifiers & R_MOD_ALT) != 0;
			Set(static_cast<Action>(action), c);
			return R_TRUE;   // the key is not given to the widgets
		}
	}

	void HookWindow(Rid window)
	{
		if (window != R_INVALID_ID && window != input::Window())
			Rwindow_set_event_hook(window, OnOptionsEvent, nullptr);
	}

	void Update()
	{
		// The editor window lost the capture (another window took the focus...) : nothing to do,
		// a click on the button again cancels it.
	}
}
