#include "InputManager.h"
#include "../InputNames.h"

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

// Input code from a json value : "KEY_SPACE", "GAMEPAD_A", "32" or 32.
// 0 when unknown (see InputNames.h).
static int ParseInputCode(const nlohmann::json& value)
{
    if (value.is_number_integer())
        return value.get<int>();

    if (!value.is_string())
        return 0;

    const std::string text = value.get<std::string>();
    const int code = lynx::input_names::CodeOf(text);

    if (code == 0)
        std::fprintf(stderr, "[Input] unknown input '%s'\n", text.c_str());

    return code;
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

        // A (re)load replaces every mapping : the editor reloads the file
        // after each save.
        action_mappings.clear();
        axis_mappings.clear();

        if (j.contains("action-mappings") && j["action-mappings"].is_object())
        {
            for (const auto& [name, keys] : j["action-mappings"].items())
            {
                std::vector<int> keycodes;

                if (keys.is_array())
                {
                    for (const auto& k : keys)
                    {
                        if (const int code = ParseInputCode(k))
                            keycodes.emplace_back(code);
                    }
                }

                action_mappings[name] = std::move(keycodes);
            }
        }

        if (j.contains("axis-mappings") && j["axis-mappings"].is_object())
        {
            for (const auto& [name, actions] : j["axis-mappings"].items())
            {
                std::vector<action_t> acts;

                if (actions.is_array())
                {
                    for (const auto& a : actions)
                    {
                        if (!a.is_object() || !a.contains("key"))
                            continue;

                        action_t act;
                        act.key = ParseInputCode(a["key"]);
                        act.value = (a.contains("scale") && a["scale"].is_number())
                            ? a["scale"].get<float>()
                            : 1.f;

                        if (act.key != 0)
                            acts.emplace_back(act);
                    }
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