#pragma once

// =============================================================================
// Input names
// -----------------------------------------------------------------------------
// Names used in input.json for every input code (Input.h) :
//   keyboard  KEY_SPACE, KEY_A, KEY_LEFT_SHIFT...   (code = GLFW key code)
//   mouse     MOUSE_LEFT, MOUSE_X, MOUSE_WHEEL...
//   gamepad   GAMEPAD_A, GAMEPAD_LEFT_X, GAMEPAD_RIGHT_TRIGGER...
// Plain numbers ("32") are still accepted (old format).
//
// Header only : shared by the engine (input.json loading) and the editor
// (input settings window).
//
// Keyboard codes are GLFW key codes : they name the PHYSICAL key of a US
// keyboard (KEY_Q is the key labelled "A" on an AZERTY keyboard).
// =============================================================================

#include <cstdlib>
#include <string>
#include <string_view>

#include "Input.h"

namespace lynx::input_names
{
    enum class InputDevice
    {
        Keyboard,
        Mouse,
        Gamepad
    };

    struct InputName
    {
        const char* name;
        int code;
        InputDevice device;
    };

    inline constexpr InputName kInputNames[] =
    {
        { "KEY_SPACE", 32, InputDevice::Keyboard },
        { "KEY_APOSTROPHE", 39, InputDevice::Keyboard },
        { "KEY_COMMA", 44, InputDevice::Keyboard },
        { "KEY_MINUS", 45, InputDevice::Keyboard },
        { "KEY_PERIOD", 46, InputDevice::Keyboard },
        { "KEY_SLASH", 47, InputDevice::Keyboard },
        { "KEY_0", 48, InputDevice::Keyboard },
        { "KEY_1", 49, InputDevice::Keyboard },
        { "KEY_2", 50, InputDevice::Keyboard },
        { "KEY_3", 51, InputDevice::Keyboard },
        { "KEY_4", 52, InputDevice::Keyboard },
        { "KEY_5", 53, InputDevice::Keyboard },
        { "KEY_6", 54, InputDevice::Keyboard },
        { "KEY_7", 55, InputDevice::Keyboard },
        { "KEY_8", 56, InputDevice::Keyboard },
        { "KEY_9", 57, InputDevice::Keyboard },
        { "KEY_SEMICOLON", 59, InputDevice::Keyboard },
        { "KEY_EQUAL", 61, InputDevice::Keyboard },
        { "KEY_A", 65, InputDevice::Keyboard },
        { "KEY_B", 66, InputDevice::Keyboard },
        { "KEY_C", 67, InputDevice::Keyboard },
        { "KEY_D", 68, InputDevice::Keyboard },
        { "KEY_E", 69, InputDevice::Keyboard },
        { "KEY_F", 70, InputDevice::Keyboard },
        { "KEY_G", 71, InputDevice::Keyboard },
        { "KEY_H", 72, InputDevice::Keyboard },
        { "KEY_I", 73, InputDevice::Keyboard },
        { "KEY_J", 74, InputDevice::Keyboard },
        { "KEY_K", 75, InputDevice::Keyboard },
        { "KEY_L", 76, InputDevice::Keyboard },
        { "KEY_M", 77, InputDevice::Keyboard },
        { "KEY_N", 78, InputDevice::Keyboard },
        { "KEY_O", 79, InputDevice::Keyboard },
        { "KEY_P", 80, InputDevice::Keyboard },
        { "KEY_Q", 81, InputDevice::Keyboard },
        { "KEY_R", 82, InputDevice::Keyboard },
        { "KEY_S", 83, InputDevice::Keyboard },
        { "KEY_T", 84, InputDevice::Keyboard },
        { "KEY_U", 85, InputDevice::Keyboard },
        { "KEY_V", 86, InputDevice::Keyboard },
        { "KEY_W", 87, InputDevice::Keyboard },
        { "KEY_X", 88, InputDevice::Keyboard },
        { "KEY_Y", 89, InputDevice::Keyboard },
        { "KEY_Z", 90, InputDevice::Keyboard },
        { "KEY_LEFT_BRACKET", 91, InputDevice::Keyboard },
        { "KEY_BACKSLASH", 92, InputDevice::Keyboard },
        { "KEY_RIGHT_BRACKET", 93, InputDevice::Keyboard },
        { "KEY_GRAVE_ACCENT", 96, InputDevice::Keyboard },
        { "KEY_WORLD_1", 161, InputDevice::Keyboard },
        { "KEY_WORLD_2", 162, InputDevice::Keyboard },
        { "KEY_ESCAPE", 256, InputDevice::Keyboard },
        { "KEY_ENTER", 257, InputDevice::Keyboard },
        { "KEY_TAB", 258, InputDevice::Keyboard },
        { "KEY_BACKSPACE", 259, InputDevice::Keyboard },
        { "KEY_INSERT", 260, InputDevice::Keyboard },
        { "KEY_DELETE", 261, InputDevice::Keyboard },
        { "KEY_RIGHT", 262, InputDevice::Keyboard },
        { "KEY_LEFT", 263, InputDevice::Keyboard },
        { "KEY_DOWN", 264, InputDevice::Keyboard },
        { "KEY_UP", 265, InputDevice::Keyboard },
        { "KEY_PAGE_UP", 266, InputDevice::Keyboard },
        { "KEY_PAGE_DOWN", 267, InputDevice::Keyboard },
        { "KEY_HOME", 268, InputDevice::Keyboard },
        { "KEY_END", 269, InputDevice::Keyboard },
        { "KEY_CAPS_LOCK", 280, InputDevice::Keyboard },
        { "KEY_SCROLL_LOCK", 281, InputDevice::Keyboard },
        { "KEY_NUM_LOCK", 282, InputDevice::Keyboard },
        { "KEY_PRINT_SCREEN", 283, InputDevice::Keyboard },
        { "KEY_PAUSE", 284, InputDevice::Keyboard },
        { "KEY_F1", 290, InputDevice::Keyboard },
        { "KEY_F2", 291, InputDevice::Keyboard },
        { "KEY_F3", 292, InputDevice::Keyboard },
        { "KEY_F4", 293, InputDevice::Keyboard },
        { "KEY_F5", 294, InputDevice::Keyboard },
        { "KEY_F6", 295, InputDevice::Keyboard },
        { "KEY_F7", 296, InputDevice::Keyboard },
        { "KEY_F8", 297, InputDevice::Keyboard },
        { "KEY_F9", 298, InputDevice::Keyboard },
        { "KEY_F10", 299, InputDevice::Keyboard },
        { "KEY_F11", 300, InputDevice::Keyboard },
        { "KEY_F12", 301, InputDevice::Keyboard },
        { "KEY_F13", 302, InputDevice::Keyboard },
        { "KEY_F14", 303, InputDevice::Keyboard },
        { "KEY_F15", 304, InputDevice::Keyboard },
        { "KEY_F16", 305, InputDevice::Keyboard },
        { "KEY_F17", 306, InputDevice::Keyboard },
        { "KEY_F18", 307, InputDevice::Keyboard },
        { "KEY_F19", 308, InputDevice::Keyboard },
        { "KEY_F20", 309, InputDevice::Keyboard },
        { "KEY_F21", 310, InputDevice::Keyboard },
        { "KEY_F22", 311, InputDevice::Keyboard },
        { "KEY_F23", 312, InputDevice::Keyboard },
        { "KEY_F24", 313, InputDevice::Keyboard },
        { "KEY_F25", 314, InputDevice::Keyboard },
        { "KEY_KP_0", 320, InputDevice::Keyboard },
        { "KEY_KP_1", 321, InputDevice::Keyboard },
        { "KEY_KP_2", 322, InputDevice::Keyboard },
        { "KEY_KP_3", 323, InputDevice::Keyboard },
        { "KEY_KP_4", 324, InputDevice::Keyboard },
        { "KEY_KP_5", 325, InputDevice::Keyboard },
        { "KEY_KP_6", 326, InputDevice::Keyboard },
        { "KEY_KP_7", 327, InputDevice::Keyboard },
        { "KEY_KP_8", 328, InputDevice::Keyboard },
        { "KEY_KP_9", 329, InputDevice::Keyboard },
        { "KEY_KP_DECIMAL", 330, InputDevice::Keyboard },
        { "KEY_KP_DIVIDE", 331, InputDevice::Keyboard },
        { "KEY_KP_MULTIPLY", 332, InputDevice::Keyboard },
        { "KEY_KP_SUBTRACT", 333, InputDevice::Keyboard },
        { "KEY_KP_ADD", 334, InputDevice::Keyboard },
        { "KEY_KP_ENTER", 335, InputDevice::Keyboard },
        { "KEY_KP_EQUAL", 336, InputDevice::Keyboard },
        { "KEY_LEFT_SHIFT", 340, InputDevice::Keyboard },
        { "KEY_LEFT_CONTROL", 341, InputDevice::Keyboard },
        { "KEY_LEFT_ALT", 342, InputDevice::Keyboard },
        { "KEY_LEFT_SUPER", 343, InputDevice::Keyboard },
        { "KEY_RIGHT_SHIFT", 344, InputDevice::Keyboard },
        { "KEY_RIGHT_CONTROL", 345, InputDevice::Keyboard },
        { "KEY_RIGHT_ALT", 346, InputDevice::Keyboard },
        { "KEY_RIGHT_SUPER", 347, InputDevice::Keyboard },
        { "KEY_MENU", 348, InputDevice::Keyboard },
        { "MOUSE_LEFT", MOUSE_LEFT, InputDevice::Mouse },
        { "MOUSE_RIGHT", MOUSE_RIGHT, InputDevice::Mouse },
        { "MOUSE_MIDDLE", MOUSE_MIDDLE, InputDevice::Mouse },
        { "MOUSE_BUTTON_4", MOUSE_BUTTON_4, InputDevice::Mouse },
        { "MOUSE_BUTTON_5", MOUSE_BUTTON_5, InputDevice::Mouse },
        { "MOUSE_BUTTON_6", MOUSE_BUTTON_6, InputDevice::Mouse },
        { "MOUSE_BUTTON_7", MOUSE_BUTTON_7, InputDevice::Mouse },
        { "MOUSE_BUTTON_8", MOUSE_BUTTON_8, InputDevice::Mouse },
        { "MOUSE_X", MOUSE_X, InputDevice::Mouse },
        { "MOUSE_Y", MOUSE_Y, InputDevice::Mouse },
        { "MOUSE_WHEEL", MOUSE_WHEEL, InputDevice::Mouse },
        { "GAMEPAD_A", GAMEPAD_A, InputDevice::Gamepad },
        { "GAMEPAD_B", GAMEPAD_B, InputDevice::Gamepad },
        { "GAMEPAD_X", GAMEPAD_X, InputDevice::Gamepad },
        { "GAMEPAD_Y", GAMEPAD_Y, InputDevice::Gamepad },
        { "GAMEPAD_LEFT_BUMPER", GAMEPAD_LEFT_BUMPER, InputDevice::Gamepad },
        { "GAMEPAD_RIGHT_BUMPER", GAMEPAD_RIGHT_BUMPER, InputDevice::Gamepad },
        { "GAMEPAD_BACK", GAMEPAD_BACK, InputDevice::Gamepad },
        { "GAMEPAD_START", GAMEPAD_START, InputDevice::Gamepad },
        { "GAMEPAD_GUIDE", GAMEPAD_GUIDE, InputDevice::Gamepad },
        { "GAMEPAD_LEFT_THUMB", GAMEPAD_LEFT_THUMB, InputDevice::Gamepad },
        { "GAMEPAD_RIGHT_THUMB", GAMEPAD_RIGHT_THUMB, InputDevice::Gamepad },
        { "GAMEPAD_DPAD_UP", GAMEPAD_DPAD_UP, InputDevice::Gamepad },
        { "GAMEPAD_DPAD_RIGHT", GAMEPAD_DPAD_RIGHT, InputDevice::Gamepad },
        { "GAMEPAD_DPAD_DOWN", GAMEPAD_DPAD_DOWN, InputDevice::Gamepad },
        { "GAMEPAD_DPAD_LEFT", GAMEPAD_DPAD_LEFT, InputDevice::Gamepad },
        { "GAMEPAD_LEFT_X", GAMEPAD_LEFT_X, InputDevice::Gamepad },
        { "GAMEPAD_LEFT_Y", GAMEPAD_LEFT_Y, InputDevice::Gamepad },
        { "GAMEPAD_RIGHT_X", GAMEPAD_RIGHT_X, InputDevice::Gamepad },
        { "GAMEPAD_RIGHT_Y", GAMEPAD_RIGHT_Y, InputDevice::Gamepad },
        { "GAMEPAD_LEFT_TRIGGER", GAMEPAD_LEFT_TRIGGER, InputDevice::Gamepad },
        { "GAMEPAD_RIGHT_TRIGGER", GAMEPAD_RIGHT_TRIGGER, InputDevice::Gamepad },
    };

    // Analog axes (they have a value, they are never "pressed"). The gamepad
    // triggers are both : an axis, and a virtual button past half travel.
    inline bool IsAxis(int code)
    {
        return code >= INPUT_MOUSE_AXIS_BASE;
    }

    inline bool IsTrigger(int code)
    {
        return code == GAMEPAD_LEFT_TRIGGER || code == GAMEPAD_RIGHT_TRIGGER;
    }

    // Name of a code ("KEY_SPACE"), nullptr if unknown.
    inline const char* NameOf(int code)
    {
        for (const InputName& n : kInputNames)
        {
            if (n.code == code)
                return n.name;
        }

        return nullptr;
    }

    // Name -> code. Accepts the old numeric format. 0 = unknown.
    inline int CodeOf(std::string_view text)
    {
        if (text.empty())
            return 0;

        const std::string value(text);

        char* end = nullptr;
        const long numeric = std::strtol(value.c_str(), &end, 10);

        if (end && end != value.c_str() && *end == '\0')
            return static_cast<int>(numeric);

        for (const InputName& n : kInputNames)
        {
            if (value == n.name)
                return n.code;
        }

        return 0;
    }

    // Name to write in input.json (unknown codes stay numeric).
    inline std::string ToConfigString(int code)
    {
        if (const char* name = NameOf(code))
            return name;

        return std::to_string(code);
    }
}
