#ifndef HGE_INPUT_H
#define HGE_INPUT_H

#include <vector>
#include <string>
#include <cstdint>

#include "../core/Common.h"

namespace lynx
{
    // Input codes used by the mapping system.
    // Keyboard keys keep their GLFW key codes.
    constexpr int INPUT_MOUSE_BUTTON_BASE   = 1000;
    constexpr int INPUT_GAMEPAD_BUTTON_BASE = 2000;
    constexpr int INPUT_MOUSE_AXIS_BASE     = 3000;
    constexpr int INPUT_GAMEPAD_AXIS_BASE   = 4000;

    enum MouseInput
    {
        MOUSE_LEFT   = INPUT_MOUSE_BUTTON_BASE + 0,
        MOUSE_RIGHT  = INPUT_MOUSE_BUTTON_BASE + 1,
        MOUSE_MIDDLE = INPUT_MOUSE_BUTTON_BASE + 2,
        MOUSE_BUTTON_4 = INPUT_MOUSE_BUTTON_BASE + 3,
        MOUSE_BUTTON_5 = INPUT_MOUSE_BUTTON_BASE + 4,
        MOUSE_BUTTON_6 = INPUT_MOUSE_BUTTON_BASE + 5,
        MOUSE_BUTTON_7 = INPUT_MOUSE_BUTTON_BASE + 6,
        MOUSE_BUTTON_8 = INPUT_MOUSE_BUTTON_BASE + 7,

        MOUSE_X = INPUT_MOUSE_AXIS_BASE + 0,
        MOUSE_Y = INPUT_MOUSE_AXIS_BASE + 1,
        MOUSE_WHEEL = INPUT_MOUSE_AXIS_BASE + 2
    };

    enum GamepadInput
    {
        GAMEPAD_A = INPUT_GAMEPAD_BUTTON_BASE + 0,
        GAMEPAD_B = INPUT_GAMEPAD_BUTTON_BASE + 1,
        GAMEPAD_X = INPUT_GAMEPAD_BUTTON_BASE + 2,
        GAMEPAD_Y = INPUT_GAMEPAD_BUTTON_BASE + 3,
        GAMEPAD_LEFT_BUMPER = INPUT_GAMEPAD_BUTTON_BASE + 4,
        GAMEPAD_RIGHT_BUMPER = INPUT_GAMEPAD_BUTTON_BASE + 5,
        GAMEPAD_BACK = INPUT_GAMEPAD_BUTTON_BASE + 6,
        GAMEPAD_START = INPUT_GAMEPAD_BUTTON_BASE + 7,
        GAMEPAD_GUIDE = INPUT_GAMEPAD_BUTTON_BASE + 8,
        GAMEPAD_LEFT_THUMB = INPUT_GAMEPAD_BUTTON_BASE + 9,
        GAMEPAD_RIGHT_THUMB = INPUT_GAMEPAD_BUTTON_BASE + 10,
        GAMEPAD_DPAD_UP = INPUT_GAMEPAD_BUTTON_BASE + 11,
        GAMEPAD_DPAD_RIGHT = INPUT_GAMEPAD_BUTTON_BASE + 12,
        GAMEPAD_DPAD_DOWN = INPUT_GAMEPAD_BUTTON_BASE + 13,
        GAMEPAD_DPAD_LEFT = INPUT_GAMEPAD_BUTTON_BASE + 14,

        GAMEPAD_LEFT_X = INPUT_GAMEPAD_AXIS_BASE + 0,
        GAMEPAD_LEFT_Y = INPUT_GAMEPAD_AXIS_BASE + 1,
        GAMEPAD_RIGHT_X = INPUT_GAMEPAD_AXIS_BASE + 2,
        GAMEPAD_RIGHT_Y = INPUT_GAMEPAD_AXIS_BASE + 3,
        GAMEPAD_LEFT_TRIGGER = INPUT_GAMEPAD_AXIS_BASE + 4,
        GAMEPAD_RIGHT_TRIGGER = INPUT_GAMEPAD_AXIS_BASE + 5
    };

    class LYNX_API InputAction {
    public:
        InputAction(const char* name);
        ~InputAction()=default;

        bool IsPressed();
        bool IsHeld();
        bool IsReleased();

    private:
        std::vector<int> keys_;
    };

    typedef struct {
        int key;
        float value;
    } action_t;

    class LYNX_API InputAxis1D {
    public:
        InputAxis1D(const char* name);
        ~InputAxis1D()=default;

        float GetValue();

    private:
        std::vector<action_t> actions_;
    };
}

#endif
