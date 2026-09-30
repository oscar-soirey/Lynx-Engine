#include "Input.h"

#include "Private/InputManager.h"

#include <vector>
#include <algorithm>
#include <cmath>

namespace lynx
{
    InputAction::InputAction(const char *name):
        keys_(GetActionKeys(name))
    { }

    bool InputAction::IsPressed()
    {
        const auto& just = GetJustPressedKeys();

        for (int k : keys_)
        {
            if (std::find(just.begin(), just.end(), k) != just.end())
                return true;
        }

        return false;
    }

    bool InputAction::IsHeld()
    {
        const auto& pressed = GetPressedKeys();

        for (int k : keys_)
        {
            if (std::find(pressed.begin(), pressed.end(), k) != pressed.end())
                return true;
        }

        return false;
    }

    bool InputAction::IsReleased()
    {
        const auto& just = GetJustReleasedKeys();

        for (int k : keys_)
        {
            if (std::find(just.begin(), just.end(), k) != just.end())
                return true;
        }

        return false;
    }

    InputAxis1D::InputAxis1D(const char *name):
        actions_(GetAxisActions(name))
    { }

    float InputAxis1D::GetValue()
    {
        float val = 0.f;
        const auto& pressed = GetPressedKeys();

        for (const auto& a : actions_)
        {
            // Analog devices (mouse movement / gamepad axes).
            const float analog = GetAnalogValue(a.key);
            if (analog != 0.f)
            {
                val += analog * a.value;
                continue;
            }

            // Digital inputs (keyboard / mouse buttons / gamepad buttons).
            if (std::find(pressed.begin(), pressed.end(), a.key) != pressed.end())
                val += a.value;
        }

        return std::clamp(val, -1.f, 1.f);
    }
}
