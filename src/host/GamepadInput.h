#pragma once

// -----------------------------------------------------------------------------
// Gamepad polling, compiled INTO THE ENGINE EXECUTABLES (src/editor,
// src/runtime), next to the GLFW instance
// that owns the window and called glfwInit().
//
// lynx.dll must not call any glfw* function: the dll would have its own copy of
// GLFW's global state, on which glfwInit() was never called, so glfwJoystick*()
// would silently report "no joystick". The exe therefore polls the pad and
// feeds the engine through the exported injection API, exactly like keyboard
// and mouse events.
// -----------------------------------------------------------------------------

#include <glfw/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "../gameplay/Private/InputManager.h"

// Set to 1 to dump the raw state of every joystick once per second.
#ifndef LYNX_GAMEPAD_DEBUG
#define LYNX_GAMEPAD_DEBUG 0
#endif

namespace lynx
{
namespace gamepad
{
    constexpr float kStickDeadzone  = 0.15f;
    constexpr float kTriggerPress   = 0.5f;
    constexpr float kTriggerRelease = 0.3f;

    inline std::array<unsigned char, GLFW_GAMEPAD_BUTTON_LAST + 1> previous_buttons{};
    inline bool trigger_down[2] = { false, false };
    inline int  last_jid = -1;

    // Per-axis deadzone, rescaled so the output still reaches +/-1.
    inline float ApplyDeadzone(float v)
    {
        const float a = std::fabs(v);

        if (a < kStickDeadzone)
            return 0.f;

        const float s = (a - kStickDeadzone) / (1.f - kStickDeadzone);

        return std::copysign(std::min(s, 1.f), v);
    }

    inline void SetButton(int button, bool is_down)
    {
        const bool was_down = previous_buttons[button] == GLFW_PRESS;

        if (is_down && !was_down)
            InjectKeyDown(INPUT_GAMEPAD_BUTTON_BASE + button);
        else if (!is_down && was_down)
            InjectKeyUp(INPUT_GAMEPAD_BUTTON_BASE + button);

        previous_buttons[button] = is_down ? GLFW_PRESS : GLFW_RELEASE;
    }

    // Triggers are analog axes, but a mapping such as
    // "attack": ["GAMEPAD_RIGHT_TRIGGER"] is looked up as a *button* by
    // InputAction. Expose each trigger as a virtual button too (hysteresis).
    inline void SetTrigger(int index, int key, float value)
    {
        InjectAnalog(key, value);

        if (!trigger_down[index] && value >= kTriggerPress)
        {
            trigger_down[index] = true;
            InjectKeyDown(key);
        }
        else if (trigger_down[index] && value <= kTriggerRelease)
        {
            trigger_down[index] = false;
            InjectKeyUp(key);
        }
    }

    inline void ClearAnalog()
    {
        InjectAnalog(GAMEPAD_LEFT_X, 0.f);
        InjectAnalog(GAMEPAD_LEFT_Y, 0.f);
        InjectAnalog(GAMEPAD_RIGHT_X, 0.f);
        InjectAnalog(GAMEPAD_RIGHT_Y, 0.f);
        InjectAnalog(GAMEPAD_LEFT_TRIGGER, 0.f);
        InjectAnalog(GAMEPAD_RIGHT_TRIGGER, 0.f);
    }

    // Releases everything so a disconnect, a device switch or a blocked frame
    // can never leave a stuck input.
    inline void ReleaseAll()
    {
        for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i)
            SetButton(i, false);

        if (trigger_down[0]) InjectKeyUp(GAMEPAD_LEFT_TRIGGER);
        if (trigger_down[1]) InjectKeyUp(GAMEPAD_RIGHT_TRIGGER);

        trigger_down[0] = trigger_down[1] = false;

        ClearAnalog();
    }

#if LYNX_GAMEPAD_DEBUG
    inline void DebugDump(int jid)
    {
        static double next_log = 0.0;
        const double now = glfwGetTime();

        if (now < next_log)
            return;

        next_log = now + 1.0;

        std::printf("---- [Gamepad] debug ----\n");

        for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; ++i)
        {
            if (!glfwJoystickPresent(i))
                continue;

            int ac = 0, bc = 0;
            const float* a = glfwGetJoystickAxes(i, &ac);
            const unsigned char* b = glfwGetJoystickButtons(i, &bc);

            std::printf("joy %d%s: '%s' gamepad=%d axes=%d buttons=%d\n",
                        i, i == jid ? " (USED)" : "",
                        glfwGetJoystickName(i),
                        glfwJoystickIsGamepad(i), ac, bc);

            std::printf("   axes:");
            for (int k = 0; a && k < ac; ++k)
                std::printf(" %.2f", a[k]);

            std::printf("\n   pressed:");
            for (int k = 0; b && k < bc; ++k)
                if (b[k]) std::printf(" %d", k);
            std::printf("\n");
        }

        if (jid < 0)
            std::printf("no joystick seen by GLFW\n");
    }
#endif

    // Call once per frame from the exe, after glfwPollEvents() and before
    // engine->ProgressOneFrame(). `blocked` mirrors editor::BlockGameInput():
    // while true the pad is ignored, like the keyboard and the mouse.
    inline void Poll(bool blocked = false)
    {
        if (blocked)
        {
            ReleaseAll();
            return;
        }

        // Prefer a device GLFW recognises as a gamepad (the virtual Xbox 360
        // pad created by DS4Windows) over the first raw joystick found.
        int  jid = -1;
        bool is_gamepad = false;

        for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; ++i)
        {
            if (!glfwJoystickPresent(i))
                continue;

            const bool gp = glfwJoystickIsGamepad(i) == GLFW_TRUE;

            if (jid < 0 || (gp && !is_gamepad))
            {
                jid = i;
                is_gamepad = gp;
            }

            if (gp)
                break;
        }

        if (jid != last_jid)
        {
            ReleaseAll();
            last_jid = jid;

            if (jid >= 0)
            {
                std::printf("[Gamepad] using joystick %d: %s (gamepad=%d)\n",
                            jid, glfwGetJoystickName(jid), is_gamepad ? 1 : 0);
            }
            else
            {
                std::printf("[Gamepad] disconnected\n");
            }
        }

#if LYNX_GAMEPAD_DEBUG
        DebugDump(jid);
#endif

        if (jid < 0)
            return;

        // PATH 1: standardized GLFW gamepad mapping.
        if (is_gamepad)
        {
            GLFWgamepadstate state{};

            if (glfwGetGamepadState(jid, &state) == GLFW_TRUE)
            {
                for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i)
                    SetButton(i, state.buttons[i] == GLFW_PRESS);

                InjectAnalog(GAMEPAD_LEFT_X,  ApplyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]));
                InjectAnalog(GAMEPAD_LEFT_Y,  ApplyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]));
                InjectAnalog(GAMEPAD_RIGHT_X, ApplyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]));
                InjectAnalog(GAMEPAD_RIGHT_Y, ApplyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]));

                // GLFW reports triggers in [-1, 1] -> convert to [0, 1].
                SetTrigger(0, GAMEPAD_LEFT_TRIGGER,
                           (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.f) * 0.5f);
                SetTrigger(1, GAMEPAD_RIGHT_TRIGGER,
                           (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.f) * 0.5f);

                return;
            }
        }

        // PATH 2: raw joystick fallback (XInput-like layout, approximate).
        int axis_count = 0, button_count = 0, hat_count = 0;

        const float* axes = glfwGetJoystickAxes(jid, &axis_count);
        const unsigned char* buttons = glfwGetJoystickButtons(jid, &button_count);
        const unsigned char* hats = glfwGetJoystickHats(jid, &hat_count);

        auto raw_button = [&](int index) -> bool
        {
            return buttons && index < button_count && buttons[index] == GLFW_PRESS;
        };

        SetButton(GLFW_GAMEPAD_BUTTON_A, raw_button(0));
        SetButton(GLFW_GAMEPAD_BUTTON_B, raw_button(1));
        SetButton(GLFW_GAMEPAD_BUTTON_X, raw_button(2));
        SetButton(GLFW_GAMEPAD_BUTTON_Y, raw_button(3));
        SetButton(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, raw_button(4));
        SetButton(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, raw_button(5));
        SetButton(GLFW_GAMEPAD_BUTTON_BACK, raw_button(6));
        SetButton(GLFW_GAMEPAD_BUTTON_START, raw_button(7));
        SetButton(GLFW_GAMEPAD_BUTTON_LEFT_THUMB, raw_button(8));
        SetButton(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB, raw_button(9));

        // D-pad is a hat on raw devices, not buttons.
        const unsigned char hat = (hats && hat_count > 0) ? hats[0] : 0;
        SetButton(GLFW_GAMEPAD_BUTTON_DPAD_UP,    (hat & GLFW_HAT_UP) != 0);
        SetButton(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, (hat & GLFW_HAT_RIGHT) != 0);
        SetButton(GLFW_GAMEPAD_BUTTON_DPAD_DOWN,  (hat & GLFW_HAT_DOWN) != 0);
        SetButton(GLFW_GAMEPAD_BUTTON_DPAD_LEFT,  (hat & GLFW_HAT_LEFT) != 0);

        if (!axes || axis_count <= 0)
            return;

        auto raw_axis = [&](int index) -> float
        {
            return (index >= 0 && index < axis_count) ? axes[index] : 0.f;
        };

        InjectAnalog(GAMEPAD_LEFT_X,  ApplyDeadzone(raw_axis(0)));
        InjectAnalog(GAMEPAD_LEFT_Y,  ApplyDeadzone(raw_axis(1)));
        InjectAnalog(GAMEPAD_RIGHT_X, ApplyDeadzone(raw_axis(2)));
        InjectAnalog(GAMEPAD_RIGHT_Y, ApplyDeadzone(raw_axis(3)));

        // Some devices report triggers in [-1, 1], others in [0, 1].
        auto trigger_value = [&](int index) -> float
        {
            const float v = raw_axis(index);

            if (v <= -0.99f) return 0.f;
            if (v < 0.f)     return (v + 1.f) * 0.5f;

            return std::clamp(v, 0.f, 1.f);
        };

        if (axis_count > 4) SetTrigger(0, GAMEPAD_LEFT_TRIGGER,  trigger_value(4));
        if (axis_count > 5) SetTrigger(1, GAMEPAD_RIGHT_TRIGGER, trigger_value(5));
    }
}
}
