#include "InputManager.h"

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <json/json.hpp>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdio>

static std::vector<int> keys_pressed;
static std::vector<int> keys_just_pressed;
static std::vector<int> keys_just_released;

static std::unordered_map<int, float> analog_values;

static std::unordered_map<std::string, std::vector<int>> action_mappings;
static std::unordered_map<std::string, std::vector<lynx::action_t>> axis_mappings;

static std::string read_file(const char* path)
{
    std::ifstream file(path);

    if (!file.is_open())
        return {};

    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };
}

static int ParseInputCode(const std::string& value)
{
    // Keep support for the old numeric format.
    char* end = nullptr;
    const long numeric = std::strtol(value.c_str(), &end, 10);

    if (end && *end == '\0')
        return static_cast<int>(numeric);

    using namespace lynx;

    static const std::unordered_map<std::string, int> names =
    {
        {"MOUSE_LEFT", MOUSE_LEFT},
        {"MOUSE_RIGHT", MOUSE_RIGHT},
        {"MOUSE_MIDDLE", MOUSE_MIDDLE},
        {"MOUSE_BUTTON_4", MOUSE_BUTTON_4},
        {"MOUSE_BUTTON_5", MOUSE_BUTTON_5},
        {"MOUSE_BUTTON_6", MOUSE_BUTTON_6},
        {"MOUSE_BUTTON_7", MOUSE_BUTTON_7},
        {"MOUSE_BUTTON_8", MOUSE_BUTTON_8},

        {"MOUSE_X", MOUSE_X},
        {"MOUSE_Y", MOUSE_Y},
        {"MOUSE_WHEEL", MOUSE_WHEEL},

        {"GAMEPAD_A", GAMEPAD_A},
        {"GAMEPAD_B", GAMEPAD_B},
        {"GAMEPAD_X", GAMEPAD_X},
        {"GAMEPAD_Y", GAMEPAD_Y},
        {"GAMEPAD_LEFT_BUMPER", GAMEPAD_LEFT_BUMPER},
        {"GAMEPAD_RIGHT_BUMPER", GAMEPAD_RIGHT_BUMPER},
        {"GAMEPAD_BACK", GAMEPAD_BACK},
        {"GAMEPAD_START", GAMEPAD_START},
        {"GAMEPAD_GUIDE", GAMEPAD_GUIDE},
        {"GAMEPAD_LEFT_THUMB", GAMEPAD_LEFT_THUMB},
        {"GAMEPAD_RIGHT_THUMB", GAMEPAD_RIGHT_THUMB},
        {"GAMEPAD_DPAD_UP", GAMEPAD_DPAD_UP},
        {"GAMEPAD_DPAD_RIGHT", GAMEPAD_DPAD_RIGHT},
        {"GAMEPAD_DPAD_DOWN", GAMEPAD_DPAD_DOWN},
        {"GAMEPAD_DPAD_LEFT", GAMEPAD_DPAD_LEFT},

        {"GAMEPAD_LEFT_X", GAMEPAD_LEFT_X},
        {"GAMEPAD_LEFT_Y", GAMEPAD_LEFT_Y},
        {"GAMEPAD_RIGHT_X", GAMEPAD_RIGHT_X},
        {"GAMEPAD_RIGHT_Y", GAMEPAD_RIGHT_Y},
        {"GAMEPAD_LEFT_TRIGGER", GAMEPAD_LEFT_TRIGGER},
        {"GAMEPAD_RIGHT_TRIGGER", GAMEPAD_RIGHT_TRIGGER}
    };

    auto it = names.find(value);

    return it != names.end() ? it->second : 0;
}

namespace lynx
{
    void LoadConfigFile(const char* path)
    {
        std::string data = read_file(path);

        if (data.empty())
        {
            std::fprintf(stderr,
                         "[Input] cannot read '%s' (missing, empty, or wrong working directory)\n",
                         path);
            return;
        }

        nlohmann::json j = nlohmann::json::parse(data, nullptr, false);

        if (j.is_discarded())
        {
            std::fprintf(stderr, "[Input] '%s' is not valid JSON\n", path);
            return;
        }

        if (j.contains("action-mappings"))
        {
            for (const auto& [name, keys] : j["action-mappings"].items())
            {
                std::vector<int> keycodes;

                for (const auto& k : keys)
                {
                    keycodes.emplace_back(
                        ParseInputCode(k.get<std::string>())
                    );
                }

                action_mappings[name] = std::move(keycodes);
            }
        }

        if (j.contains("axis-mappings"))
        {
            for (const auto& [name, actions] : j["axis-mappings"].items())
            {
                std::vector<action_t> acts;

                for (const auto& a : actions)
                {
                    action_t act;

                    act.key = ParseInputCode(
                        a["key"].get<std::string>()
                    );

                    act.value = a["scale"].get<float>();

                    acts.emplace_back(act);
                }

                axis_mappings[name] = std::move(acts);
            }
        }
    }

    void InputTick()
    {
        keys_just_pressed.clear();
        keys_just_released.clear();

        // Mouse motion is a per-frame input.
        analog_values[MOUSE_X] = 0.f;
        analog_values[MOUSE_Y] = 0.f;
        analog_values[MOUSE_WHEEL] = 0.f;
    }

    void InjectKeyDown(int key)
    {
        bool contains =
            std::find(
                keys_pressed.begin(),
                keys_pressed.end(),
                key
            ) != keys_pressed.end();

        if (!contains)
        {
            keys_pressed.emplace_back(key);
            keys_just_pressed.emplace_back(key);
        }
    }

    void InjectKeyUp(int key)
    {
        auto it = std::find(
            keys_pressed.begin(),
            keys_pressed.end(),
            key
        );

        if (it != keys_pressed.end())
        {
            keys_pressed.erase(it);
            keys_just_released.emplace_back(key);
        }
    }

    void InjectMouseButtonDown(int button)
    {
        InjectKeyDown(INPUT_MOUSE_BUTTON_BASE + button);
    }

    void InjectMouseButtonUp(int button)
    {
        InjectKeyUp(INPUT_MOUSE_BUTTON_BASE + button);
    }

    void InjectMouseMove(float dx, float dy)
    {
        analog_values[MOUSE_X] += dx;
        analog_values[MOUSE_Y] += dy;
    }

    void InjectMouseWheel(float delta)
    {
        analog_values[MOUSE_WHEEL] += delta;
    }

    void InjectAnalog(int key, float value)
    {
        analog_values[key] = value;
    }

    const std::vector<int>& GetPressedKeys()
    {
        return keys_pressed;
    }

    const std::vector<int>& GetJustPressedKeys()
    {
        return keys_just_pressed;
    }

    const std::vector<int>& GetJustReleasedKeys()
    {
        return keys_just_released;
    }

    float GetAnalogValue(int key)
    {
        auto it = analog_values.find(key);

        return it != analog_values.end()
            ? it->second
            : 0.f;
    }

    const std::vector<int>& GetActionKeys(const char* name)
    {
        static const std::vector<int> empty;

        auto it = action_mappings.find(name);

        if (it == action_mappings.end())
            return empty;

        return it->second;
    }

    const std::vector<action_t>& GetAxisActions(const char* name)
    {
        static const std::vector<action_t> empty;

        auto it = axis_mappings.find(name);

        if (it == axis_mappings.end())
            return empty;

        return it->second;
    }
}