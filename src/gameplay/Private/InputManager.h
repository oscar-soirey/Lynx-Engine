#pragma once

#include <vector>
#include "../Input.h"

namespace lynx
{
    // Config / event injection API.
    void LYNX_API LoadConfigFile(const char* path);
    void LYNX_API InjectKeyDown(int key);
    void LYNX_API InjectKeyUp(int key);

    void LYNX_API InjectMouseButtonDown(int button);
    void LYNX_API InjectMouseButtonUp(int button);
    void LYNX_API InjectMouseMove(float dx, float dy);
    void LYNX_API InjectMouseWheel(float delta);

    // Sets (does not accumulate) the current value of an analog input such as
    // GAMEPAD_LEFT_X or GAMEPAD_RIGHT_TRIGGER. The host application (the exe,
    // which owns GLFW) polls the gamepad and calls this every frame.
    void LYNX_API InjectAnalog(int key, float value);

    // Internal frame management.
    void InputTick();

    const std::vector<int>& GetPressedKeys();
    const std::vector<int>& GetJustPressedKeys();
    const std::vector<int>& GetJustReleasedKeys();

    float GetAnalogValue(int key);

    const std::vector<int>& GetActionKeys(const char* name);
    const std::vector<action_t>& GetAxisActions(const char* name);
}
