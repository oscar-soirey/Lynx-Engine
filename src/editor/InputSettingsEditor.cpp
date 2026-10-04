#include "InputSettingsEditor.h"

#include <imgui/imgui.h>
#include <glfw/glfw3.h>
#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "../gameplay/InputNames.h"
#include "../gameplay/Private/InputManager.h"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;   // keeps the order of the file

namespace lynx::editor::input_settings
{
	namespace
	{
		using input_names::InputDevice;

		// ---------------------------------------------------------------------
		// Data
		// ---------------------------------------------------------------------

		struct Binding
		{
			int code = 0;          // 0 = None
			std::string raw;       // unknown name read from the file (kept as is)
			float scale = 1.f;     // axis mappings only
		};

		struct Mapping
		{
			std::string name;
			std::vector<Binding> bindings;
			bool open = true;      // tree node state
		};

		enum class MappingKind
		{
			Action,
			Axis
		};

		struct Capture
		{
			bool active = false;
			MappingKind kind = MappingKind::Action;
			int mapping = -1;
			int binding = -1;

			// Gamepad state when the capture started : a button already held
			// is ignored until released.
			unsigned char start_buttons[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
			bool start_axes_active[GLFW_GAMEPAD_AXIS_LAST + 1] = {};
		};

		struct State
		{
			GLFWwindow* window = nullptr;
			fs::path file;

			std::vector<Mapping> actions;
			std::vector<Mapping> axes;

			// Other top-level entries of input.json, written back untouched.
			json extra = json::object();

			std::string error;       // load / save problem
			std::string status;      // "Saved" ...
			double status_time = 0.0;

			Capture capture;
			bool open_capture_popup = false;

			// After a capture : shortcuts stay blocked until every key and
			// mouse button is released.
			bool wait_release = false;

			// ImGui frame of the last Draw() : a capture stops when the
			// window is not drawn anymore (Play hides the editor windows).
			int drawn_frame = -1;
		};

		State g;


		// ---------------------------------------------------------------------
		// Names
		// ---------------------------------------------------------------------

		std::string Prettify(const char* config_name)
		{
			// "KEY_LEFT_SHIFT" -> "Left Shift", "GAMEPAD_A" -> "Gamepad A"
			std::string text = config_name;

			if (text.rfind("KEY_", 0) == 0)
				text = text.substr(4);

			std::string out;
			bool word_start = true;

			for (char c : text)
			{
				if (c == '_')
				{
					out += ' ';
					word_start = true;
					continue;
				}

				const unsigned char u = static_cast<unsigned char>(c);
				out += word_start
					? static_cast<char>(std::toupper(u))
					: static_cast<char>(std::tolower(u));
				word_start = false;
			}

			return out;
		}

		// Name shown in the UI. Printable keys use the keyboard layout
		// ("A" on AZERTY for KEY_Q), like the label on the key.
		std::string DisplayName(const Binding& b)
		{
			if (b.code == 0)
				return b.raw.empty() ? "None" : b.raw + " (unknown)";

			if (b.code >= 32 && b.code < 256)
			{
				if (const char* layout = glfwGetKeyName(b.code, 0))
				{
					std::string upper = layout;

					for (char& c : upper)
						c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

					if (!upper.empty())
						return upper;
				}
			}

			if (const char* name = input_names::NameOf(b.code))
				return Prettify(name);

			return "Code " + std::to_string(b.code);
		}

		std::string DisplayName(int code)
		{
			Binding b;
			b.code = code;
			return DisplayName(b);
		}

		std::string ToLower(std::string s)
		{
			for (char& c : s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		// Inputs allowed in a mapping : an action needs something that can be
		// "pressed" (axes are excluded, except the triggers).
		bool AllowedIn(MappingKind kind, int code)
		{
			if (kind == MappingKind::Axis)
				return true;

			return !input_names::IsAxis(code) || input_names::IsTrigger(code);
		}


		// ---------------------------------------------------------------------
		// File
		// ---------------------------------------------------------------------

		Binding ParseBinding(const json& value)
		{
			Binding b;

			if (value.is_number_integer())
			{
				b.code = value.get<int>();
			}
			else if (value.is_string())
			{
				const std::string text = value.get<std::string>();
				b.code = input_names::CodeOf(text);

				if (b.code == 0)
					b.raw = text;
			}

			return b;
		}

		void Load()
		{
			g.actions.clear();
			g.axes.clear();
			g.extra = json::object();
			g.error.clear();
			g.capture = Capture{};

			std::error_code ec;

			if (!fs::is_regular_file(g.file, ec))
				return;   // new project : empty settings, created on first save

			std::ifstream in(g.file);
			const std::string text{
				std::istreambuf_iterator<char>(in),
				std::istreambuf_iterator<char>()
			};

			json doc = json::parse(text, nullptr, false);

			if (doc.is_discarded() || !doc.is_object())
			{
				g.error = "input.json is not valid JSON : fix it by hand, then \"Reload\".";
				return;
			}

			for (auto it = doc.begin(); it != doc.end(); ++it)
			{
				if (it.key() == "action-mappings" && it.value().is_object())
				{
					for (auto a = it.value().begin(); a != it.value().end(); ++a)
					{
						Mapping m;
						m.name = a.key();

						if (a.value().is_array())
						{
							for (const auto& k : a.value())
								m.bindings.push_back(ParseBinding(k));
						}

						g.actions.push_back(std::move(m));
					}
				}
				else if (it.key() == "axis-mappings" && it.value().is_object())
				{
					for (auto a = it.value().begin(); a != it.value().end(); ++a)
					{
						Mapping m;
						m.name = a.key();

						if (a.value().is_array())
						{
							for (const auto& k : a.value())
							{
								if (!k.is_object() || !k.contains("key"))
									continue;

								Binding b = ParseBinding(k["key"]);

								if (k.contains("scale") && k["scale"].is_number())
									b.scale = k["scale"].get<float>();

								m.bindings.push_back(std::move(b));
							}
						}

						g.axes.push_back(std::move(m));
					}
				}
				else
				{
					g.extra[it.key()] = it.value();
				}
			}
		}

		json BindingValue(const Binding& b)
		{
			if (b.code != 0)
				return input_names::ToConfigString(b.code);

			return b.raw;
		}

		// Empty or duplicated names cannot be written (json object keys).
		bool Validate(std::string& error)
		{
			const auto check = [&](const std::vector<Mapping>& list, const char* what)
			{
				for (size_t i = 0; i < list.size(); ++i)
				{
					if (list[i].name.empty())
					{
						error = std::string("An ") + what + " has no name.";
						return false;
					}

					for (size_t j = 0; j < i; ++j)
					{
						if (list[j].name == list[i].name)
						{
							error = std::string("Two ") + what + "s are called \"" +
							        list[i].name + "\".";
							return false;
						}
					}
				}

				return true;
			};

			return check(g.actions, "action") && check(g.axes, "axis");
		}

		// Writes input.json and reloads it in the engine.
		void Save()
		{
			std::string error;

			if (!Validate(error))
			{
				g.error = error + " Not saved.";
				return;
			}

			json doc = json::object();

			json actions = json::object();

			for (const Mapping& m : g.actions)
			{
				json keys = json::array();

				for (const Binding& b : m.bindings)
				{
					if (b.code != 0 || !b.raw.empty())
						keys.push_back(BindingValue(b));
				}

				actions[m.name] = std::move(keys);
			}

			json axes = json::object();

			for (const Mapping& m : g.axes)
			{
				json keys = json::array();

				for (const Binding& b : m.bindings)
				{
					if (b.code == 0 && b.raw.empty())
						continue;

					json entry = json::object();
					entry["key"] = BindingValue(b);
					entry["scale"] = b.scale;
					keys.push_back(std::move(entry));
				}

				axes[m.name] = std::move(keys);
			}

			doc["action-mappings"] = std::move(actions);
			doc["axis-mappings"] = std::move(axes);

			for (auto it = g.extra.begin(); it != g.extra.end(); ++it)
				doc[it.key()] = it.value();

			std::ofstream out(g.file, std::ios::trunc);

			if (!out)
			{
				g.error = "Could not write " + g.file.string();
				return;
			}

			out << doc.dump(4) << "\n";
			out.close();

			g.error.clear();
			g.status = "Saved";
			g.status_time = ImGui::GetTime();

			// The game uses the new bindings right away (InputAction /
			// InputAxis1D look them up by name).
			lynx::LoadConfigFile(g.file.string().c_str());
		}


		// ---------------------------------------------------------------------
		// Capture
		// ---------------------------------------------------------------------

		Binding* CaptureTarget()
		{
			std::vector<Mapping>& list =
				g.capture.kind == MappingKind::Action ? g.actions : g.axes;

			if (g.capture.mapping < 0 || g.capture.mapping >= static_cast<int>(list.size()))
				return nullptr;

			Mapping& m = list[g.capture.mapping];

			if (g.capture.binding < 0 || g.capture.binding >= static_cast<int>(m.bindings.size()))
				return nullptr;

			return &m.bindings[g.capture.binding];
		}

		int FindGamepad()
		{
			for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
			{
				if (glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid))
					return jid;
			}

			return -1;
		}

		// Value of a gamepad axis as the game sees it (triggers in [0, 1]).
		float GamepadAxisValue(const GLFWgamepadstate& state, int axis)
		{
			const float v = state.axes[axis];

			if (axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER || axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER)
				return (v + 1.f) * 0.5f;

			return v;
		}

		bool AxisActive(const GLFWgamepadstate& state, int axis)
		{
			return std::fabs(GamepadAxisValue(state, axis)) > 0.6f;
		}

		void StartCapture(MappingKind kind, int mapping, int binding)
		{
			g.capture = Capture{};
			g.capture.active = true;
			g.capture.kind = kind;
			g.capture.mapping = mapping;
			g.capture.binding = binding;
			g.open_capture_popup = true;

			GLFWgamepadstate state{};
			const int jid = FindGamepad();

			if (jid >= 0 && glfwGetGamepadState(jid, &state))
			{
				for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i)
					g.capture.start_buttons[i] = state.buttons[i];

				for (int i = 0; i <= GLFW_GAMEPAD_AXIS_LAST; ++i)
					g.capture.start_axes_active[i] = AxisActive(state, i);
			}
		}

		void EndCapture()
		{
			g.capture.active = false;
			g.wait_release = true;
		}

		// Assigns `code` to the binding being captured, if allowed there.
		bool Assign(int code)
		{
			if (!g.capture.active || !AllowedIn(g.capture.kind, code))
				return false;

			if (Binding* target = CaptureTarget())
			{
				target->code = code;
				target->raw.clear();
				Save();
			}

			EndCapture();
			return true;
		}

		bool AnyKeyOrButtonHeld()
		{
			if (!g.window)
				return false;

			for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
			{
				if (glfwGetKey(g.window, key) == GLFW_PRESS)
					return true;
			}

			for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button)
			{
				if (glfwGetMouseButton(g.window, button) == GLFW_PRESS)
					return true;
			}

			return false;
		}


		// ---------------------------------------------------------------------
		// Widgets
		// ---------------------------------------------------------------------

		// Key button (click = capture) + list of every input.
		// Returns true when the binding changed through the list.
		bool KeySelector(MappingKind kind, int mapping, int binding, Binding& b)
		{
			bool changed = false;

			const bool capturing =
				g.capture.active &&
				g.capture.kind == kind &&
				g.capture.mapping == mapping &&
				g.capture.binding == binding;

			const float width = ImGui::GetFontSize() * 11.f;

			std::string label = capturing ? "Press a key..." : DisplayName(b);
			label += "##key";

			const bool unknown = (b.code == 0 && !b.raw.empty());

			if (capturing)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			else if (unknown)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.f));

			if (ImGui::Button(label.c_str(), ImVec2(width, 0.f)))
				StartCapture(kind, mapping, binding);

			if (capturing || unknown)
				ImGui::PopStyleColor();

			if (ImGui::IsItemHovered())
			{
				const std::string config =
					b.code != 0 ? input_names::ToConfigString(b.code) : (b.raw.empty() ? "None" : b.raw);

				ImGui::SetTooltip("%s\nClick, then press the key to use.", config.c_str());
			}

			ImGui::SameLine(0.f, 2.f);

			if (ImGui::BeginCombo("##list", nullptr,
					ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLarge | ImGuiComboFlags_PopupAlignLeft))
			{
				static char filter[64] = {};

				if (ImGui::IsWindowAppearing())
				{
					filter[0] = '\0';
					ImGui::SetKeyboardFocusHere();
				}

				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
				ImGui::InputTextWithHint("##filter", "Search...", filter, sizeof(filter));

				const std::string needle = ToLower(filter);

				const InputDevice devices[] = { InputDevice::Keyboard, InputDevice::Mouse, InputDevice::Gamepad };
				const char* device_names[] = { "Keyboard", "Mouse", "Gamepad" };

				for (int d = 0; d < 3; ++d)
				{
					bool header_done = false;

					for (const auto& entry : input_names::kInputNames)
					{
						if (entry.device != devices[d] || !AllowedIn(kind, entry.code))
							continue;

						const std::string shown = DisplayName(entry.code);

						if (!needle.empty() &&
							ToLower(shown).find(needle) == std::string::npos &&
							ToLower(entry.name).find(needle) == std::string::npos)
						{
							continue;
						}

						if (!header_done)
						{
							ImGui::SeparatorText(device_names[d]);
							header_done = true;
						}

						const std::string item = shown + "##" + entry.name;

						if (ImGui::Selectable(item.c_str(), entry.code == b.code))
						{
							b.code = entry.code;
							b.raw.clear();
							changed = true;
						}

						if (entry.code == b.code && ImGui::IsWindowAppearing())
							ImGui::SetScrollHereY();
					}
				}

				ImGui::EndCombo();
			}

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Choose in the list");

			return changed;
		}

		std::string UniqueName(const std::vector<Mapping>& list, const char* base)
		{
			for (int i = 0; ; ++i)
			{
				const std::string candidate = i == 0 ? base : std::string(base) + "_" + std::to_string(i);

				const bool taken = std::any_of(list.begin(), list.end(),
					[&](const Mapping& m) { return m.name == candidate; });

				if (!taken)
					return candidate;
			}
		}

		// Action or axis list. Returns true when something changed.
		bool DrawMappings(MappingKind kind)
		{
			std::vector<Mapping>& list = kind == MappingKind::Action ? g.actions : g.axes;
			const bool is_axis = kind == MappingKind::Axis;

			bool changed = false;
			int remove_mapping = -1;

			const float small_button = ImGui::GetFrameHeight();

			for (int i = 0; i < static_cast<int>(list.size()); ++i)
			{
				Mapping& m = list[i];
				ImGui::PushID(i);

				// Duplicated name : shown in red.
				const bool duplicate = std::count_if(list.begin(), list.end(),
					[&](const Mapping& other) { return other.name == m.name; }) > 1;

				ImGui::SetNextItemOpen(m.open);
				m.open = ImGui::TreeNodeEx("##mapping",
					ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);

				ImGui::SameLine();

				// Name : applied when the field loses focus (not at each letter).
				char name[128];
				std::snprintf(name, sizeof(name), "%s", m.name.c_str());

				const bool invalid_name = duplicate || m.name.empty();

				if (invalid_name)
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.12f, 0.12f, 1.f));

				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.f);

				if (ImGui::InputText("##name", name, sizeof(name)))
					m.name = name;

				if (invalid_name)
					ImGui::PopStyleColor();

				if (ImGui::IsItemDeactivatedAfterEdit())
					changed = true;

				if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
				{
					ImGui::SetTooltip(
						is_axis ? "Name used in the code : lynx::InputAxis1D(\"%s\")"
						        : "Name used in the code : lynx::InputAction(\"%s\")",
						m.name.c_str());
				}

				ImGui::SameLine();

				if (ImGui::Button("+", ImVec2(small_button, 0.f)))
				{
					Binding b;
					m.bindings.push_back(b);
					m.open = true;
					StartCapture(kind, i, static_cast<int>(m.bindings.size()) - 1);
				}

				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Add a key");

				ImGui::SameLine();

				if (ImGui::Button("X", ImVec2(small_button, 0.f)))
					remove_mapping = i;

				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(is_axis ? "Delete this axis" : "Delete this action");

				if (m.open)
				{
					int remove_binding = -1;

					if (m.bindings.empty())
						ImGui::TextDisabled("No key : use + to add one.");

					for (int j = 0; j < static_cast<int>(m.bindings.size()); ++j)
					{
						Binding& b = m.bindings[j];
						ImGui::PushID(j);

						if (KeySelector(kind, i, j, b))
							changed = true;

						if (is_axis)
						{
							ImGui::SameLine();
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);

							ImGui::DragFloat("##scale", &b.scale, 0.05f, -10.f, 10.f, "Scale %.2f");

							if (ImGui::IsItemDeactivatedAfterEdit())
								changed = true;

							if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
								ImGui::SetTooltip("Value added to the axis while this input is used\n(multiplies the value of an analog input).");
						}

						ImGui::SameLine();

						if (ImGui::Button("X", ImVec2(small_button, 0.f)))
							remove_binding = j;

						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Remove this key");

						ImGui::PopID();
					}

					if (remove_binding >= 0)
					{
						g.capture.active = false;
						m.bindings.erase(m.bindings.begin() + remove_binding);
						changed = true;
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}

			if (remove_mapping >= 0)
			{
				g.capture.active = false;
				list.erase(list.begin() + remove_mapping);
				changed = true;
			}

			ImGui::Spacing();

			if (ImGui::Button(is_axis ? "+ Add axis" : "+ Add action"))
			{
				Mapping m;
				m.name = UniqueName(list, is_axis ? "NewAxis" : "NewAction");
				list.push_back(std::move(m));
				changed = true;
			}

			return changed;
		}

		void DrawCapturePopup()
		{
			if (g.open_capture_popup)
			{
				ImGui::OpenPopup("Press a key##InputCapture");
				g.open_capture_popup = false;
			}

			if (ImGui::BeginPopupModal("Press a key##InputCapture", nullptr,
					ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
			{
				if (!g.capture.active)
				{
					ImGui::CloseCurrentPopup();
				}
				else
				{
					ImGui::TextUnformatted("Press a key, a mouse button or a gamepad button.");

					if (g.capture.kind == MappingKind::Axis)
						ImGui::TextUnformatted("Axis : the mouse wheel and the gamepad sticks work too.");

					ImGui::TextDisabled("Escape : cancel");
				}

				ImGui::EndPopup();
			}
		}
	}


	void Init(GLFWwindow* window, const fs::path& file)
	{
		g.window = window;
		g.file = file;
		Load();
	}


	void Update()
	{
		if (g.wait_release && !g.capture.active && !AnyKeyOrButtonHeld())
			g.wait_release = false;

		if (!g.capture.active)
			return;

		if (g.drawn_frame < ImGui::GetFrameCount())
		{
			EndCapture();
			return;
		}

		// The binding disappeared (deleted, reloaded...) : stop.
		if (!CaptureTarget())
		{
			EndCapture();
			return;
		}

		// Gamepad : polled here (GLFW has no gamepad events).
		const int jid = FindGamepad();
		GLFWgamepadstate state{};

		if (jid < 0 || !glfwGetGamepadState(jid, &state))
			return;

		for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i)
		{
			if (!state.buttons[i])
			{
				g.capture.start_buttons[i] = GLFW_RELEASE;
				continue;
			}

			if (g.capture.start_buttons[i])
				continue;   // was already held when the capture started

			if (Assign(INPUT_GAMEPAD_BUTTON_BASE + i))
				return;
		}

		for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis)
		{
			const bool active = AxisActive(state, axis);

			if (!active)
			{
				g.capture.start_axes_active[axis] = false;
				continue;
			}

			if (g.capture.start_axes_active[axis])
				continue;

			if (Assign(INPUT_GAMEPAD_AXIS_BASE + axis))
				return;
		}
	}


	void Draw(bool* open)
	{
		if (!open || !*open)
		{
			if (g.capture.active)
				EndCapture();

			return;
		}

		g.drawn_frame = ImGui::GetFrameCount();

		ImGui::SetNextWindowSize(ImVec2(620.f, 640.f), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Input Settings", open))
		{
			ImGui::End();

			if (g.capture.active)
				EndCapture();

			return;
		}

		// Header : file, reload, status.
		ImGui::TextDisabled("%s", g.file.filename().string().c_str());

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", g.file.string().c_str());

		ImGui::SameLine();

		if (ImGui::SmallButton("Reload"))
		{
			Load();
			lynx::LoadConfigFile(g.file.string().c_str());
		}

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Read input.json again (after editing it by hand).");

		if (!g.status.empty() && ImGui::GetTime() - g.status_time < 2.0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", g.status.c_str());
		}

		ImGui::TextDisabled("Changes are saved and applied immediately.");

		if (!g.error.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.f));
			ImGui::TextWrapped("%s", g.error.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Separator();

		bool changed = false;

		ImGui::PushID("actions");

		if (ImGui::CollapsingHeader("Action Mappings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Buttons : pressed / held / released (lynx::InputAction).");
			changed |= DrawMappings(MappingKind::Action);
		}

		ImGui::PopID();

		ImGui::Spacing();

		ImGui::PushID("axes");

		if (ImGui::CollapsingHeader("Axis Mappings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Values between -1 and 1 (lynx::InputAxis1D) : sum of input x scale.");
			changed |= DrawMappings(MappingKind::Axis);
		}

		ImGui::PopID();

		DrawCapturePopup();

		ImGui::End();

		if (changed)
			Save();
	}


	bool OnKey(int key, int action)
	{
		if (!g.capture.active)
			return false;

		if (action != GLFW_PRESS)
			return true;   // swallow repeats / releases during the capture

		if (key == GLFW_KEY_ESCAPE)
		{
			// Cancel : a new binding that never got a key is removed.
			if (Binding* target = CaptureTarget())
			{
				if (target->code == 0 && target->raw.empty())
				{
					std::vector<Mapping>& list =
						g.capture.kind == MappingKind::Action ? g.actions : g.axes;

					auto& bindings = list[g.capture.mapping].bindings;
					bindings.erase(bindings.begin() + g.capture.binding);
				}
			}

			EndCapture();
			return true;
		}

		if (key == GLFW_KEY_UNKNOWN)
			return true;

		Assign(key);
		return true;
	}


	bool OnMouseButton(int button, int action)
	{
		if (!g.capture.active)
			return false;

		if (action == GLFW_PRESS)
			Assign(INPUT_MOUSE_BUTTON_BASE + button);

		return true;
	}


	bool OnScroll(double yoffset)
	{
		if (!g.capture.active)
			return false;

		if (yoffset != 0.0 && g.capture.kind == MappingKind::Axis)
			Assign(MOUSE_WHEEL);

		return true;
	}


	bool BlocksEditorShortcuts()
	{
		return g.capture.active || g.wait_release;
	}
}
