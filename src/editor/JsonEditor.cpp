#include "JsonEditor.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace lynx::editor::json_editor
{
	namespace
	{
		// ---------------------------------------------------------------------
		// Types
		// ---------------------------------------------------------------------

		enum class Kind { Null, Bool, Int, Float, String, Object, Array, Count };

		Kind KindOf(const Json& v)
		{
			if (v.is_object()) return Kind::Object;
			if (v.is_array()) return Kind::Array;
			if (v.is_boolean()) return Kind::Bool;
			if (v.is_number_integer()) return Kind::Int;   // signed and unsigned
			if (v.is_number()) return Kind::Float;
			if (v.is_string()) return Kind::String;
			return Kind::Null;
		}

		const char* Badge(Kind k)
		{
			switch (k)
			{
			case Kind::Bool: return "on/off";
			case Kind::Int: return "123";
			case Kind::Float: return "1.5";
			case Kind::String: return "abc";
			case Kind::Object: return "{ }";
			case Kind::Array: return "[ ]";
			default: return "null";
			}
		}

		const char* KindName(Kind k)
		{
			switch (k)
			{
			case Kind::Bool: return "Boolean";
			case Kind::Int: return "Integer";
			case Kind::Float: return "Decimal number";
			case Kind::String: return "Text";
			case Kind::Object: return "Object  { key : value }";
			case Kind::Array: return "List  [ a, b, c ]";
			default: return "Null";
			}
		}

		ImVec4 BadgeColor(Kind k)
		{
			switch (k)
			{
			case Kind::Bool: return ImVec4(0.58f, 0.36f, 0.70f, 1.f);
			case Kind::Int: return ImVec4(0.22f, 0.48f, 0.78f, 1.f);
			case Kind::Float: return ImVec4(0.18f, 0.58f, 0.70f, 1.f);
			case Kind::String: return ImVec4(0.30f, 0.60f, 0.30f, 1.f);
			case Kind::Object: return ImVec4(0.85f, 0.55f, 0.18f, 1.f);
			case Kind::Array: return ImVec4(0.80f, 0.40f, 0.35f, 1.f);
			default: return ImVec4(0.50f, 0.50f, 0.50f, 1.f);
			}
		}

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::string ScalarText(const Json& v)
		{
			if (v.is_string()) return v.get<std::string>();
			if (v.is_null()) return "";
			return v.dump();
		}

		/** `v` converted to another type (keeps what makes sense). */
		Json Convert(const Json& v, Kind to)
		{
			const Kind from = KindOf(v);
			if (from == to)
				return v;
			const std::string text = ScalarText(v);
			auto number = [&](double fallback) -> double
			{
				if (v.is_number()) return v.get<double>();
				if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
				char* end = nullptr;
				const double d = std::strtod(text.c_str(), &end);
				return end && end != text.c_str() ? d : fallback;
			};
			switch (to)
			{
			case Kind::Null: return nullptr;
			case Kind::Bool:
				if (v.is_number()) return v.get<double>() != 0.0;
				return Lower(text) == "true" || text == "1" || (v.is_structured() && !v.empty());
			case Kind::Int: return static_cast<std::int64_t>(std::llround(number(0.0)));
			case Kind::Float: return number(0.0);
			case Kind::String: return v.is_structured() ? v.dump() : text;
			case Kind::Object:
			{
				Json o = Json::object();
				if (v.is_array())
					for (size_t i = 0; i < v.size(); ++i)
						o[std::to_string(i)] = v[i];
				else if (!v.is_null())
					o["value"] = v;
				return o;
			}
			case Kind::Array:
			{
				Json a = Json::array();
				if (v.is_object())
					for (auto& item : v.items())
						a.push_back(item.value());
				else if (!v.is_null())
					a.push_back(v);
				return a;
			}
			default: return v;
			}
		}

		/** A new element for a list : same type as the last one (empty), text otherwise. */
		Json NewElementLike(const Json& list)
		{
			if (list.empty())
				return "";
			return list.back();   // a copy of the last one : same fields, same kind of values
		}

		std::string UniqueKey(const Json& object, const std::string& base)
		{
			if (!object.contains(base))
				return base;
			for (int i = 2;; ++i)
			{
				const std::string key = base + std::to_string(i);
				if (!object.contains(key))
					return key;
			}
		}

		/** Rebuilds the object with the members in `order` (indexes). */
		void Reorder(Json& object, const std::vector<size_t>& order)
		{
			std::vector<std::pair<std::string, Json>> items;
			for (auto& item : object.items())
				items.emplace_back(item.key(), item.value());
			Json result = Json::object();
			for (size_t i : order)
				result[items[i].first] = std::move(items[i].second);
			object = std::move(result);
		}

		bool LooksLikeColorKey(const std::string& key)
		{
			const std::string k = Lower(key);
			for (const char* word : { "color", "colour", "tint", "rgb", "albedo", "emissive", "background" })
				if (k.find(word) != std::string::npos)
					return true;
			return false;
		}

		/** [r, g, b] or [r, g, b, a] between 0 and 1, under a "color" key. */
		bool IsColorArray(const std::string& key, const Json& v)
		{
			if (!v.is_array() || (v.size() != 3 && v.size() != 4) || !LooksLikeColorKey(key))
				return false;
			for (const Json& c : v)
				if (!c.is_number() || c.get<double>() < 0.0 || c.get<double>() > 1.0)
					return false;
			return true;
		}

		/** [r, g, b(, a)] between 0 and 255 (integers), under a "color" key. */
		bool IsColorArray255(const std::string& key, const Json& v)
		{
			if (!v.is_array() || (v.size() != 3 && v.size() != 4) || !LooksLikeColorKey(key))
				return false;
			bool above_one = false;
			for (const Json& c : v)
			{
				if (!c.is_number_integer() || c.get<std::int64_t>() < 0 || c.get<std::int64_t>() > 255)
					return false;
				above_one |= c.get<std::int64_t>() > 1;
			}
			return above_one;
		}

		bool IsHexColor(const Json& v)
		{
			if (!v.is_string())
				return false;
			const std::string& s = v.get_ref<const std::string&>();
			if ((s.size() != 7 && s.size() != 9) || s[0] != '#')
				return false;
			return std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
		}

		// ---------------------------------------------------------------------
		// Widgets
		// ---------------------------------------------------------------------

		int ResizeCallback(ImGuiInputTextCallbackData* data)
		{
			if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
			{
				auto* text = static_cast<std::string*>(data->UserData);
				text->resize(static_cast<size_t>(data->BufTextLen));
				data->Buf = text->data();
			}
			return 0;
		}

		bool InputString(const char* id, std::string& text, bool multiline, float height = 0.f)
		{
			const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
			if (multiline)
				return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, ImVec2(-FLT_MIN, height), flags,
				                                 ResizeCallback, &text);
			return ImGui::InputText(id, text.data(), text.capacity() + 1, flags, ResizeCallback, &text);
		}

		/** Colored type badge ; click : change the type. */
		bool TypeBadge(Kind kind, Kind& new_kind)
		{
			const ImVec4 color = BadgeColor(kind);
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.08f, color.y + 0.08f, color.z + 0.08f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
			const bool clicked = ImGui::Button(Badge(kind), ImVec2(ImGui::GetFontSize() * 3.4f, 0.f));
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(4);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s - click to change the type", KindName(kind));
			if (clicked)
				ImGui::OpenPopup("##type");

			bool changed = false;
			if (ImGui::BeginPopup("##type"))
			{
				ImGui::TextDisabled("Type");
				ImGui::Separator();
				for (int i = 0; i < static_cast<int>(Kind::Count); ++i)
				{
					const Kind k = static_cast<Kind>(i);
					ImGui::PushStyleColor(ImGuiCol_Text, BadgeColor(k));
					ImGui::Bullet();
					ImGui::PopStyleColor();
					ImGui::SameLine();
					if (ImGui::Selectable(KindName(k), k == kind) && k != kind)
					{
						new_kind = k;
						changed = true;
					}
				}
				ImGui::EndPopup();
			}
			return changed;
		}

		// ---------------------------------------------------------------------
		// Tree
		// ---------------------------------------------------------------------

		enum class Op { None, Delete, Duplicate, Up, Down, Rename, Replace };

		struct Action
		{
			Op op = Op::None;
			size_t index = 0;
			std::string key;   // Rename
			Json value;        // Replace
		};

		struct Ctx
		{
			State& state;
			std::string filter;      // lower case
			bool edited = false;     // a value changed this frame
		};

		bool Matches(const std::string& key, const Json& v, const std::string& filter)
		{
			if (filter.empty())
				return true;
			if (Lower(key).find(filter) != std::string::npos)
				return true;
			if (v.is_primitive())
				return Lower(ScalarText(v)).find(filter) != std::string::npos;
			if (v.is_object())
			{
				for (auto& item : v.items())
					if (Matches(item.key(), item.value(), filter))
						return true;
			}
			else
			{
				for (const Json& e : v)
					if (Matches("", e, filter))
						return true;
			}
			return false;
		}

		void DrawChildren(Ctx& ctx, Json& v, int depth);

		/** The value widget of a primitive (or a color list) : fills the rest of the line. */
		void DrawLeafValue(Ctx& ctx, const std::string& key, Json& v, float width)
		{
			ImGui::SetNextItemWidth(width);
			if (IsColorArray(key, v) || IsColorArray255(key, v))
			{
				const bool is255 = IsColorArray255(key, v);
				const float scale = is255 ? 255.f : 1.f;
				float c[4] = { 0.f, 0.f, 0.f, 1.f };
				for (size_t i = 0; i < v.size(); ++i)
					c[i] = v[i].get<float>() / scale;
				const bool alpha = v.size() == 4;
				const bool changed = alpha ? ImGui::ColorEdit4("##color", c, ImGuiColorEditFlags_AlphaBar)
				                           : ImGui::ColorEdit3("##color", c);
				if (changed)
				{
					for (size_t i = 0; i < v.size(); ++i)
					{
						if (is255)
							v[i] = static_cast<std::int64_t>(std::lround(c[i] * 255.f));
						else
							v[i] = std::round(static_cast<double>(c[i]) * 1000.0) / 1000.0;
					}
					ctx.edited = true;
				}
				return;
			}

			switch (KindOf(v))
			{
			case Kind::Bool:
			{
				bool b = v.get<bool>();
				if (ImGui::Checkbox(b ? "true##bool" : "false##bool", &b))
				{
					v = b;
					ctx.edited = true;
				}
				break;
			}
			case Kind::Int:
			{
				std::int64_t n = v.get<std::int64_t>();
				if (ImGui::DragScalar("##int", ImGuiDataType_S64, &n, 0.2f))
				{
					v = n;
					ctx.edited = true;
				}
				if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
					ImGui::SetTooltip("Drag, or double-click to type");
				break;
			}
			case Kind::Float:
			{
				double d = v.get<double>();
				const double speed = std::max(0.001, std::abs(d) * 0.01);
				if (ImGui::DragScalar("##float", ImGuiDataType_Double, &d, static_cast<float>(speed), nullptr, nullptr, "%.6g"))
				{
					v = d;
					ctx.edited = true;
				}
				if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
					ImGui::SetTooltip("Drag, or double-click to type");
				break;
			}
			case Kind::String:
			{
				std::string& text = v.get_ref<std::string&>();
				if (IsHexColor(v))
				{
					// Color button before the text.
					unsigned r = 0, g = 0, b = 0, a = 255;
					std::sscanf(text.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
					if (text.size() == 9)
						std::sscanf(text.c_str() + 7, "%02x", &a);
					float c[4] = { r / 255.f, g / 255.f, b / 255.f, a / 255.f };
					const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs |
					                                  (text.size() == 9 ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha);
					if (ImGui::ColorEdit4("##hex", c, flags))
					{
						char buffer[16];
						if (text.size() == 9)
							std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", static_cast<unsigned>(std::lround(c[0] * 255.f)),
							              static_cast<unsigned>(std::lround(c[1] * 255.f)), static_cast<unsigned>(std::lround(c[2] * 255.f)),
							              static_cast<unsigned>(std::lround(c[3] * 255.f)));
						else
							std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", static_cast<unsigned>(std::lround(c[0] * 255.f)),
							              static_cast<unsigned>(std::lround(c[1] * 255.f)), static_cast<unsigned>(std::lround(c[2] * 255.f)));
						text = buffer;
						ctx.edited = true;
					}
					ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
					ImGui::SetNextItemWidth(std::max(40.f, width - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x));
				}
				const bool multiline = text.find('\n') != std::string::npos;
				const int lines = static_cast<int>(std::count(text.begin(), text.end(), '\n')) + 1;
				if (InputString("##text", text, multiline,
				                ImGui::GetTextLineHeight() * static_cast<float>(std::min(lines, 8)) + ImGui::GetStyle().FramePadding.y * 2.f))
					ctx.edited = true;
				break;
			}
			default:
				ImGui::TextDisabled("null");
				break;
			}
		}

		/**
		 * One row : a member of an object (`key` editable) or an element of a
		 * list (`key` = nullptr, shown as [i]). Returns what the parent must do.
		 */
		Action DrawRow(Ctx& ctx, const std::string* key, size_t index, Json& v, const Json& parent, int depth)
		{
			Action action;
			action.index = index;
			const std::string key_text = key ? *key : std::string();
			const Kind kind = KindOf(v);
			const bool color = IsColorArray(key_text, v) || IsColorArray255(key_text, v);
			const bool container = v.is_structured() && !color;
			const float small_button = ImGui::GetFrameHeight();
			const ImGuiStyle& style = ImGui::GetStyle();

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding |
			                           ImGuiTreeNodeFlags_SpanAvailWidth;
			if (container)
			{
				if (depth == 0)
					flags |= ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen;
				else if (v.size() <= 6)
					flags |= ImGuiTreeNodeFlags_DefaultOpen;
				if (!ctx.filter.empty())
					ImGui::SetNextItemOpen(true);
				else if (ctx.state.open_all != 0)
					ImGui::SetNextItemOpen(ctx.state.open_all > 0);
			}
			else
				flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

			const bool open = ImGui::TreeNodeEx("##row", flags) && container;

			// --- Right click : row menu -------------------------------------------
			if (ImGui::BeginPopupContextItem("##row_menu"))
			{
				if (ImGui::MenuItem("Duplicate"))
					action.op = Op::Duplicate;
				if (ImGui::MenuItem("Move up", nullptr, false, index > 0))
					action.op = Op::Up;
				if (ImGui::MenuItem("Move down", nullptr, false, index + 1 < parent.size()))
					action.op = Op::Down;
				ImGui::Separator();
				if (ImGui::MenuItem("Copy as JSON"))
					ImGui::SetClipboardText(v.dump(2).c_str());
				if (ImGui::MenuItem("Paste JSON (replace)"))
				{
					const char* clip = ImGui::GetClipboardText();
					Json pasted = Json::parse(clip ? clip : "", nullptr, false, true);
					if (pasted.is_discarded())
						ctx.state.clipboard_error = "The clipboard does not contain JSON.";
					else
					{
						ctx.state.clipboard_error.clear();
						action.op = Op::Replace;
						action.value = std::move(pasted);
					}
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete"))
					action.op = Op::Delete;
				ImGui::EndPopup();
			}

			// --- Key ---------------------------------------------------------------
			ImGui::SameLine();
			if (key)
			{
				// The new name is applied when the field is left (a key can't
				// exist twice in an object).
				const ImGuiID id = ImGui::GetID("##key");
				const bool active = ImGui::GetActiveID() == id;
				std::string& buffer = ctx.state.key_buffer;
				std::string shown = active ? buffer : *key;
				const bool duplicate = active && shown != *key && parent.contains(shown);
				const bool invalid = duplicate || shown.empty();
				if (invalid)
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.12f, 0.12f, 1.f));
				if (depth == 0 && container)
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.f);
				InputString("##key", shown, false);
				if (depth == 0 && container)
					ImGui::PopStyleColor();
				if (invalid)
					ImGui::PopStyleColor();
				if (ImGui::IsItemActivated() || ImGui::IsItemActive())
					buffer = shown;
				if (ImGui::IsItemDeactivatedAfterEdit() && shown != *key && !shown.empty() && !parent.contains(shown))
				{
					action.op = Op::Rename;
					action.key = shown;
				}
				if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
					ImGui::SetTooltip(duplicate ? "This key already exists" : "Key (applied when you leave the field)");
			}
			else
			{
				ImGui::AlignTextToFramePadding();
				char label[32];
				std::snprintf(label, sizeof(label), "[%zu]", index);
				ImGui::TextDisabled("%s", label);
				ImGui::SameLine(0.f, 0.f);
				ImGui::Dummy(ImVec2(std::max(0.f, ImGui::GetFontSize() * 3.f - ImGui::CalcTextSize(label).x), 0.f));
			}

			// --- Type badge ----------------------------------------------------------
			ImGui::SameLine();
			Kind new_kind = kind;
			if (TypeBadge(kind, new_kind))
			{
				action.op = Op::Replace;
				action.value = Convert(v, new_kind);
			}

			// --- Value / summary, "+" and "X" ------------------------------------------
			ImGui::SameLine();
			const float buttons = (container ? small_button + style.ItemSpacing.x : 0.f) + small_button;
			const float width = std::max(60.f, ImGui::GetContentRegionAvail().x - buttons - style.ItemSpacing.x);
			if (container)
			{
				ImGui::AlignTextToFramePadding();
				if (v.is_object())
					ImGui::TextDisabled("%zu field%s", v.size(), v.size() == 1 ? "" : "s");
				else
					ImGui::TextDisabled("%zu element%s", v.size(), v.size() == 1 ? "" : "s");
				ImGui::SameLine();
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - buttons));
				if (ImGui::Button("+", ImVec2(small_button, 0.f)))
				{
					if (v.is_object())
						v[UniqueKey(v, "NewKey")] = "";
					else
						v.push_back(NewElementLike(v));
					ctx.edited = true;
					ImGui::GetStateStorage()->SetInt(ImGui::GetID("##row"), 1);   // open it
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(v.is_object() ? "Add a field" : "Add an element");
				ImGui::SameLine();
			}
			else
			{
				DrawLeafValue(ctx, key_text, v, width);
				ImGui::SameLine();
				// "X" on the right edge, whatever the width of the value.
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - small_button));
			}
			if (ImGui::Button("X", ImVec2(small_button, 0.f)))
				action.op = Op::Delete;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(key ? "Delete this field" : "Delete this element");

			if (open)
			{
				DrawChildren(ctx, v, depth + 1);
				ImGui::TreePop();
			}
			return action;
		}

		void Apply(Ctx& ctx, Json& v, const Action& a)
		{
			if (a.op == Op::None)
				return;
			ctx.edited = true;
			if (v.is_array())
			{
				switch (a.op)
				{
				case Op::Delete: v.erase(a.index); break;
				case Op::Duplicate: v.insert(v.begin() + static_cast<std::ptrdiff_t>(a.index) + 1, v[a.index]); break;
				case Op::Up: std::swap(v[a.index], v[a.index - 1]); break;
				case Op::Down: std::swap(v[a.index], v[a.index + 1]); break;
				case Op::Replace: v[a.index] = a.value; break;
				default: break;
				}
				return;
			}

			// Object : the members stay in their order.
			std::vector<size_t> order(v.size());
			for (size_t i = 0; i < order.size(); ++i)
				order[i] = i;
			auto key_at = [&](size_t i)
			{
				auto it = v.begin();
				std::advance(it, static_cast<std::ptrdiff_t>(i));
				return it.key();
			};
			switch (a.op)
			{
			case Op::Delete: v.erase(key_at(a.index)); break;
			case Op::Replace: v[key_at(a.index)] = a.value; break;
			case Op::Up: std::swap(order[a.index], order[a.index - 1]); Reorder(v, order); break;
			case Op::Down: std::swap(order[a.index], order[a.index + 1]); Reorder(v, order); break;
			case Op::Duplicate:
			{
				const std::string key = key_at(a.index);
				Json copy = v[key];
				v[UniqueKey(v, key + "_copy")] = std::move(copy);   // added at the end
				order.insert(order.begin() + static_cast<std::ptrdiff_t>(a.index) + 1, order.size());
				Reorder(v, order);
				break;
			}
			case Op::Rename:
			{
				std::vector<std::pair<std::string, Json>> items;
				for (auto& item : v.items())
					items.emplace_back(item.key(), item.value());
				items[a.index].first = a.key;
				Json result = Json::object();
				for (auto& item : items)
					result[item.first] = std::move(item.second);
				v = std::move(result);
				break;
			}
			default: break;
			}
		}

		void DrawChildren(Ctx& ctx, Json& v, int depth)
		{
			Action action;
			if (v.is_object())
			{
				size_t i = 0;
				for (auto it = v.begin(); it != v.end(); ++it, ++i)
				{
					const std::string key = it.key();
					if (!Matches(key, it.value(), ctx.filter))
						continue;
					ImGui::PushID(static_cast<int>(i));
					ImGui::PushID(key.c_str());
					Action a = DrawRow(ctx, &key, i, it.value(), v, depth);
					ImGui::PopID();
					ImGui::PopID();
					if (a.op != Op::None)
						action = std::move(a);
				}
				if (v.empty())
					ImGui::TextDisabled("Empty object : + adds a field.");
			}
			else if (v.is_array())
			{
				for (size_t i = 0; i < v.size(); ++i)
				{
					if (!Matches("", v[i], ctx.filter))
						continue;
					ImGui::PushID(static_cast<int>(i));
					Action a = DrawRow(ctx, nullptr, i, v[i], v, depth);
					ImGui::PopID();
					if (a.op != Op::None)
						action = std::move(a);
				}
				if (v.empty())
					ImGui::TextDisabled("Empty list : + adds an element.");
			}
			Apply(ctx, v, action);
		}

		/**
		 * Like dump(indent), but a list of simple values that fits on a line
		 * stays on one line ([0.1, 0.2, 0.3], ["a", "b"]) : the way people write it.
		 */
		void Write(const Json& v, std::string& out, const std::string& unit, int depth)
		{
			auto scalar = [](const Json& x) { return x.dump(-1, ' ', false, Json::error_handler_t::replace); };
			auto newline = [&](int d)
			{
				out += '\n';
				for (int i = 0; i < d; ++i)
					out += unit;
			};
			if (v.is_object())
			{
				if (v.empty())
				{
					out += "{}";
					return;
				}
				out += '{';
				bool first = true;
				for (auto& item : v.items())
				{
					if (!first)
						out += ',';
					first = false;
					newline(depth + 1);
					out += Json(item.key()).dump(-1, ' ', false, Json::error_handler_t::replace);
					out += ": ";
					Write(item.value(), out, unit, depth + 1);
				}
				newline(depth);
				out += '}';
				return;
			}
			if (v.is_array())
			{
				if (v.empty())
				{
					out += "[]";
					return;
				}
				const bool simple = std::all_of(v.begin(), v.end(), [](const Json& x) { return x.is_primitive(); });
				if (simple)
				{
					std::string line = "[";
					for (size_t i = 0; i < v.size(); ++i)
						line += (i ? ", " : "") + scalar(v[i]);
					line += ']';
					if (line.size() + static_cast<size_t>(depth) * unit.size() <= 100)
					{
						out += line;
						return;
					}
				}
				out += '[';
				for (size_t i = 0; i < v.size(); ++i)
				{
					if (i)
						out += ',';
					newline(depth + 1);
					Write(v[i], out, unit, depth + 1);
				}
				newline(depth);
				out += ']';
				return;
			}
			out += scalar(v);
		}
	}


	bool Parse(const std::string& text, Json& out, std::string& error, bool* has_comments)
	{
		try
		{
			out = Json::parse(text, nullptr, true, true);
		}
		catch (const Json::parse_error& e)
		{
			error = e.what();
			// "[json.exception.parse_error.101] parse error at line 3, column 5: ..." : the useful part.
			const size_t at = error.find("parse error");
			if (at != std::string::npos)
				error = error.substr(at);
			return false;
		}
		error.clear();
		if (has_comments)
			*has_comments = Json::parse(text, nullptr, false, false).is_discarded();
		return true;
	}


	std::string Dump(const Json& doc, const std::string& like)
	{
		// Indentation of the first indented line of the previous text.
		int indent = 4;
		char indent_char = ' ';
		const size_t line = like.find('\n');
		if (line != std::string::npos && line + 1 < like.size())
		{
			const char c = like[line + 1];
			if (c == '\t')
			{
				indent_char = '\t';
				indent = 1;
			}
			else if (c == ' ')
			{
				size_t n = 0;
				while (line + 1 + n < like.size() && like[line + 1 + n] == ' ')
					++n;
				indent = static_cast<int>(std::clamp<size_t>(n, 1, 8));
			}
		}
		std::string text;
		Write(doc, text, std::string(static_cast<size_t>(indent), indent_char), 0);
		if (like.empty() || like.back() == '\n')
			text += '\n';
		return text;
	}


	int CountValues(const Json& doc)
	{
		if (!doc.is_structured())
			return 1;
		int n = 0;
		for (const Json& v : doc)
			n += CountValues(v);
		return n;
	}


	bool Draw(Json& doc, State& state)
	{
		Ctx ctx{ state, Lower(state.filter) };

		// --- Header : filter, expand / collapse, add -------------------------------
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
		ImGui::InputTextWithHint("##filter", "Filter keys and values", state.filter, sizeof(state.filter));
		ctx.filter = Lower(state.filter);
		ImGui::SameLine();
		if (ImGui::SmallButton("Expand all"))
			state.open_all = 1;
		ImGui::SameLine();
		if (ImGui::SmallButton("Collapse all"))
			state.open_all = -1;
		if (!state.clipboard_error.empty())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.85f, 0.22f, 0.20f, 1.f), "%s", state.clipboard_error.c_str());
		}
		ImGui::Separator();

		ImGui::BeginChild("##json_tree", ImVec2(0.f, 0.f), ImGuiChildFlags_None, ImGuiWindowFlags_None);
		if (doc.is_structured())
		{
			DrawChildren(ctx, doc, 0);
			ImGui::Spacing();
			if (ImGui::Button(doc.is_object() ? "+ Add field" : "+ Add element"))
			{
				if (doc.is_object())
					doc[UniqueKey(doc, "NewKey")] = "";
				else
					doc.push_back(NewElementLike(doc));
				ctx.edited = true;
			}
		}
		else
		{
			// A file that is a single value.
			ImGui::TextDisabled("Value");
			ImGui::SameLine();
			Kind new_kind = KindOf(doc);
			if (TypeBadge(KindOf(doc), new_kind))
			{
				doc = Convert(doc, new_kind);
				ctx.edited = true;
			}
			ImGui::SameLine();
			DrawLeafValue(ctx, "", doc, ImGui::GetContentRegionAvail().x);
		}
		ImGui::EndChild();
		state.open_all = 0;

		// An edit counts once the widget is released (one undo per drag / typing).
		if (ctx.edited)
			state.pending = true;
		if (state.pending && !ImGui::IsAnyItemActive())
		{
			state.pending = false;
			return true;
		}
		return false;
	}
}
