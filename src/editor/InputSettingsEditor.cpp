#include "InputSettingsEditor.h"

#include "raven/RavenUi.h"

#include <Raven.h>
#include <glfw/glfw3.h>
#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

			// Raven window (windows/input_settings.xml) and its widgets.
			Rid window = R_INVALID_ID;
			Rid status_label = R_INVALID_ID;
			Rid error_label = R_INVALID_ID;
			Rid capture_label = R_INVALID_ID;
			Rid actions_list = R_INVALID_ID;
			Rid axes_list = R_INVALID_ID;
			bool rebuild = true;
			bool shown = false;
			std::unique_ptr<ui::Menu> menu;
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

		void RefreshStatus();
		void RefreshCapture();

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
			g.status_time = glfwGetTime();
			RefreshStatus();

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
			RefreshCapture();

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
			RefreshCapture();
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

		// Every key / mouse button released (the editor window and this one).
		bool AnyKeyOrButtonHeld()
		{
			for (Rid w : { input::Window(), g.window })
			{
				if (w == R_INVALID_ID || !Rwindow_is_valid(w))
					continue;
				for (int b = 0; b < 3; ++b)
					if (Rwindow_is_mouse_button_down(w, static_cast<RMouseButton>(b)))
						return true;
				for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
				{
					const RKey k = input::FromGlfw(key);
					if (k != R_KEY_UNKNOWN && Rwindow_is_key_down(w, k))
						return true;
				}
			}
			return false;
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

		// ---------------------------------------------------------------------
		// Interface (Raven : windows/input_settings.xml)
		// ---------------------------------------------------------------------

		bool IsCapturing(MappingKind kind, int mapping, int binding)
		{
			return g.capture.active && g.capture.kind == kind && g.capture.mapping == mapping &&
			       g.capture.binding == binding;
		}

		void RefreshStatus()
		{
			if (g.window == R_INVALID_ID)
				return;
			ui::SetText(g.status_label, (!g.status.empty() && glfwGetTime() - g.status_time < 2.0) ? g.status : "");
			ui::SetText(g.error_label, g.error);
			ui::SetVisible(g.error_label, !g.error.empty());
		}

		void RefreshCapture()
		{
			if (g.window == R_INVALID_ID)
				return;
			ui::SetVisible(g.capture_label, g.capture.active);
			ui::SetText(g.capture_label, g.capture.kind == MappingKind::Axis
				? "Press a key, a mouse button or a gamepad button (the mouse wheel and the sticks work too).  Escape : cancel"
				: "Press a key, a mouse button or a gamepad button.  Escape : cancel");
			g.rebuild = true;   // the button of the binding says "Press a key..."
		}

		// The list of every input of the device, for a binding.
		void InputMenu(MappingKind kind, int mapping, int binding)
		{
			g.menu = std::make_unique<ui::Menu>(g.window);
			const InputDevice devices[] = { InputDevice::Keyboard, InputDevice::Mouse, InputDevice::Gamepad };
			const char* device_names[] = { "Keyboard", "Mouse", "Gamepad" };
			std::vector<Mapping>& list = kind == MappingKind::Action ? g.actions : g.axes;
			const int current = list[mapping].bindings[binding].code;
			for (int d = 0; d < 3; ++d)
			{
				ui::Menu& sub = g.menu->Sub(device_names[d]);
				for (const auto& entry : input_names::kInputNames)
				{
					if (entry.device != devices[d] || !AllowedIn(kind, entry.code))
						continue;
					const int code = entry.code;
					sub.Check(DisplayName(code), code == current, [kind, mapping, binding, code]()
					{
						std::vector<Mapping>& l = kind == MappingKind::Action ? g.actions : g.axes;
						if (mapping < static_cast<int>(l.size()) && binding < static_cast<int>(l[mapping].bindings.size()))
						{
							l[mapping].bindings[binding].code = code;
							l[mapping].bindings[binding].raw.clear();
							Save();
							g.rebuild = true;
						}
					});
				}
			}
			g.menu->Open();
		}

		Rid MakeButton(Rid parent, const std::string& text, const char* tip, float width = 0.f)
		{
			Rid b = Rbutton_create(text.c_str());
			Rwidget_set_focusable(b, R_FALSE);
			if (width > 0.f)
				Rwidget_set_size(b, width, -1.f);
			if (tip)
				Rwidget_set_tooltip(b, tip);
			Rwidget_add_child(parent, b);
			return b;
		}

		void BuildList(MappingKind kind)
		{
			std::vector<Mapping>& list = kind == MappingKind::Action ? g.actions : g.axes;
			const bool is_axis = kind == MappingKind::Axis;
			Rid box = is_axis ? g.axes_list : g.actions_list;
			Rwidget_clear_children(box);

			for (int i = 0; i < static_cast<int>(list.size()); ++i)
			{
				Mapping& m = list[i];
				const bool duplicate = std::count_if(list.begin(), list.end(),
					[&](const Mapping& other) { return other.name == m.name; }) > 1;

				Rid card = Rwidget_create(R_WIDGET_VBOX);
				Rwidget_add_class(card, "card");
				Rwidget_add_child(box, card);

				// Name, + (add a key), X (delete).
				Rid row = Rwidget_create(R_WIDGET_HBOX);
				Rwidget_add_child(card, row);
				Rid expand = MakeButton(row, m.open ? "v" : ">", "Show / hide the keys", 26.f);
				Rid name = Rwidget_create(R_WIDGET_TEXT_INPUT);
				Rtext_input_set_text(name, m.name.c_str());
				Rwidget_set_tooltip(name, (std::string(is_axis ? "Name used in the code : lynx::InputAxis1D(\"" :
				                                                 "Name used in the code : lynx::InputAction(\"") + m.name + "\")").c_str());
				if (duplicate || m.name.empty())
					Rwidget_add_class(name, "error");
				Rwidget_add_child(row, name);
				Rslot_set_fill(name, 1.f);
				Rid add = MakeButton(row, "+", "Add a key", 28.f);
				Rid remove = MakeButton(row, "X", is_axis ? "Delete this axis" : "Delete this action", 28.f);

				ui::OnClick(expand, [kind, i]()
				{
					std::vector<Mapping>& l = kind == MappingKind::Action ? g.actions : g.axes;
					l[i].open = !l[i].open;
					g.rebuild = true;
				});
				ui::OnCommit(name, [kind, i](const std::string& text)
				{
					std::vector<Mapping>& l = kind == MappingKind::Action ? g.actions : g.axes;
					if (i < static_cast<int>(l.size()) && l[i].name != text)
					{
						l[i].name = text;
						Save();
						g.rebuild = true;
					}
				});
				ui::OnClick(add, [kind, i]()
				{
					std::vector<Mapping>& l = kind == MappingKind::Action ? g.actions : g.axes;
					l[i].bindings.push_back(Binding{});
					l[i].open = true;
					StartCapture(kind, i, static_cast<int>(l[i].bindings.size()) - 1);
				});
				ui::OnClick(remove, [kind, i]()
				{
					std::vector<Mapping>& l = kind == MappingKind::Action ? g.actions : g.axes;
					g.capture.active = false;
					l.erase(l.begin() + i);
					Save();
					g.rebuild = true;
				});

				if (!m.open)
					continue;

				if (m.bindings.empty())
				{
					Rid none = Rlabel_create("No key : use + to add one.");
					Rwidget_add_class(none, "dim");
					Rwidget_add_child(card, none);
				}

				for (int j = 0; j < static_cast<int>(m.bindings.size()); ++j)
				{
					Binding& b = m.bindings[j];
					Rid line = Rwidget_create(R_WIDGET_HBOX);
					Rslot_set_padding(line, 30.f, 0.f, 0.f, 0.f);
					Rwidget_add_child(card, line);

					const bool capturing = IsCapturing(kind, i, j);
					const std::string config = b.code != 0 ? input_names::ToConfigString(b.code) : (b.raw.empty() ? "None" : b.raw);
					Rid key = MakeButton(line, capturing ? "Press a key..." : DisplayName(b),
					                     (config + "\nClick, then press the key to use.").c_str(), 170.f);
					if (capturing)
						Rwidget_add_class(key, "active");
					else if (b.code == 0 && !b.raw.empty())
						Rwidget_add_class(key, "error");
					Rid choose = MakeButton(line, "v", "Choose in the list", 24.f);
					ui::OnClick(key, [kind, i, j]() { StartCapture(kind, i, j); });
					ui::OnClick(choose, [kind, i, j]() { InputMenu(kind, i, j); });

					if (is_axis)
					{
						Rid scale = Rwidget_create(R_WIDGET_SPIN_BOX);
						Rspin_box_set_range(scale, -10.0, 10.0);
						Rspin_box_set_step(scale, 0.05);
						Rspin_box_set_decimals(scale, 2);
						Rspin_box_set_value(scale, b.scale);
						Rwidget_set_size(scale, 90.f, -1.f);
						Rwidget_set_tooltip(scale, "Value added to the axis while this input is used\n(multiplies the value of an analog input).");
						Rwidget_add_child(line, scale);
						ui::OnValue(scale, [kind, i, j](double v)
						{
							std::vector<Mapping>& l = kind == MappingKind::Action ? g.actions : g.axes;
							if (i < static_cast<int>(l.size()) && j < static_cast<int>(l[i].bindings.size()) &&
							    l[i].bindings[j].scale != static_cast<float>(v))
							{
								l[i].bindings[j].scale = static_cast<float>(v);
								Save();
							}
						});
					}

					Rid x = MakeButton(line, "X", "Remove this key", 28.f);
					ui::OnClick(x, [kind, i, j]()
					{
						std::vector<Mapping>& l = kind == MappingKind::Action ? g.actions : g.axes;
						g.capture.active = false;
						l[i].bindings.erase(l[i].bindings.begin() + j);
						Save();
						g.rebuild = true;
					});
				}
			}
		}

		int OnWindowEvent(Rid, const REvent* event, void*)
		{
			// The keys of the capture, when this window has the keyboard.
			if (!g.capture.active)
				return R_FALSE;
			switch (event->type)
			{
			case R_EVENT_KEY_DOWN:
				return OnKey(input::ToGlfw(event->key), event->repeat ? GLFW_REPEAT : GLFW_PRESS) ? R_TRUE : R_FALSE;
			case R_EVENT_KEY_UP:
				return OnKey(input::ToGlfw(event->key), GLFW_RELEASE) ? R_TRUE : R_FALSE;
			case R_EVENT_MOUSE_DOWN:
				// A click on the button of the binding started the capture : the next click binds.
				return OnMouseButton(static_cast<int>(event->button), GLFW_PRESS) ? R_TRUE : R_FALSE;
			case R_EVENT_MOUSE_WHEEL:
				return OnScroll(event->wheel_y) ? R_TRUE : R_FALSE;
			default:
				return R_FALSE;
			}
		}

		int OnClose(Rid window, void*)
		{
			if (g.capture.active)
				EndCapture();
			Rwindow_hide(window);
			g.shown = false;
			return R_FALSE;
		}

		void CreateWindow()
		{
			if (g.window != R_INVALID_ID)
				return;
			g.window = ui::LoadWindow("windows/input_settings.xml");
			if (g.window == R_INVALID_ID)
				return;
			g.status_label = ui::Find(g.window, "is.status");
			g.error_label = ui::Find(g.window, "is.error");
			g.capture_label = ui::Find(g.window, "is.capture");
			g.actions_list = ui::Find(g.window, "is.actions");
			g.axes_list = ui::Find(g.window, "is.axes");
			ui::SetText(ui::Find(g.window, "is.file"), g.file.filename().string());
			ui::SetTooltip(ui::Find(g.window, "is.file"), g.file.string());
			ui::OnClick(ui::Find(g.window, "is.reload"), []()
			{
				Load();
				lynx::LoadConfigFile(g.file.string().c_str());
				g.rebuild = true;
				RefreshStatus();
			});
			ui::OnClick(ui::Find(g.window, "is.add_action"), []()
			{
				Mapping m;
				m.name = UniqueName(g.actions, "NewAction");
				g.actions.push_back(std::move(m));
				Save();
				g.rebuild = true;
			});
			ui::OnClick(ui::Find(g.window, "is.add_axis"), []()
			{
				Mapping m;
				m.name = UniqueName(g.axes, "NewAxis");
				g.axes.push_back(std::move(m));
				Save();
				g.rebuild = true;
			});
			Rwindow_set_event_hook(g.window, OnWindowEvent, nullptr);
			Rwindow_set_close_callback(g.window, OnClose, nullptr);
			g.rebuild = true;
		}
	}


	void Init(GLFWwindow*, const fs::path& file)
	{
		g.file = file;
		Load();
		g.rebuild = true;
	}


	void Show()
	{
		CreateWindow();
		if (g.window == R_INVALID_ID)
			return;
		Rwindow_show(g.window);
		g.shown = true;
	}


	bool IsOpen()
	{
		return g.shown;
	}


	void Update()
	{
		if (g.wait_release && !g.capture.active && !AnyKeyOrButtonHeld())
			g.wait_release = false;

		if (g.window != R_INVALID_ID)
		{
			if (g.rebuild)
			{
				g.rebuild = false;
				BuildList(MappingKind::Action);
				BuildList(MappingKind::Axis);
			}
			RefreshStatus();
		}

		if (!g.capture.active)
			return;

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
