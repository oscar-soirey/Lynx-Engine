#include "PropertyWidgets.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cmath>
#include <fstream>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace lynx::editor::property_widgets
{
	namespace
	{
		// X red, Y green, Z blue, W grey (the colors of the gizmo axes).
		const ImVec4 kAxisColors[4] = {
			ImVec4(0.80f, 0.20f, 0.18f, 1.f),
			ImVec4(0.36f, 0.62f, 0.16f, 1.f),
			ImVec4(0.20f, 0.40f, 0.82f, 1.f),
			ImVec4(0.45f, 0.45f, 0.45f, 1.f),
		};
		const char* const kAxisNames[4] = { "X", "Y", "Z", "W" };

		// Clipboard format of a vector : "x, y, z".
		std::string ToText(const float* values, int count)
		{
			std::string text;
			char number[32];
			for (int i = 0; i < count; ++i)
			{
				std::snprintf(number, sizeof(number), "%g", values[i]);
				text += (i ? ", " : "") + std::string(number);
			}
			return text;
		}

		bool FromText(const char* text, float* values, int count)
		{
			if (!text)
				return false;
			float parsed[4] = {};
			const char* cursor = text;
			for (int i = 0; i < count; ++i)
			{
				while (*cursor && !(std::isdigit(static_cast<unsigned char>(*cursor)) || *cursor == '-' ||
				                    *cursor == '+' || *cursor == '.'))
					++cursor;
				char* end = nullptr;
				parsed[i] = std::strtof(cursor, &end);
				if (end == cursor)
					return false;
				cursor = end;
			}
			std::copy(parsed, parsed + count, values);
			return true;
		}

		// "Location v" : the label, a small arrow, and its menu.
		bool LabelMenu(const char* label, float* values, int count, const float* reset)
		{
			bool changed = false;
			ImGui::AlignTextToFramePadding();

			const std::string popup = std::string("##menu_") + label;
			const ImVec2 start = ImGui::GetCursorScreenPos();
			ImGui::TextUnformatted(label);
			const ImVec2 text_max = ImGui::GetItemRectMax();

			if (values)
			{
				// Small down arrow after the label (like Unreal's "Location v").
				ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
				const float h = ImGui::GetTextLineHeight();
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float w = h * 0.45f;
				const float cx = p.x + w * 0.5f;
				const float cy = p.y + ImGui::GetStyle().FramePadding.y + h * 0.55f;
				ImGui::GetWindowDrawList()->AddTriangleFilled(
					ImVec2(cx - w * 0.5f, cy - w * 0.25f), ImVec2(cx + w * 0.5f, cy - w * 0.25f), ImVec2(cx, cy + w * 0.3f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));

				// The label and the arrow both open the menu.
				const ImRect area(start, ImVec2(std::max(text_max.x, p.x + w), start.y + ImGui::GetFrameHeight()));
				if (ImGui::IsMouseHoveringRect(area.Min, area.Max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				    ImGui::IsWindowHovered())
					ImGui::OpenPopup(popup.c_str());

				if (ImGui::BeginPopup(popup.c_str()))
				{
					if (reset && ImGui::MenuItem("Reset to default"))
					{
						std::copy(reset, reset + count, values);
						changed = true;
					}
					if (ImGui::MenuItem("Copy"))
						ImGui::SetClipboardText(ToText(values, count).c_str());
					if (ImGui::MenuItem("Paste", nullptr, false, ImGui::GetClipboardText() != nullptr))
						changed |= FromText(ImGui::GetClipboardText(), values, count);
					ImGui::EndPopup();
				}
			}
			return changed;
		}
	}


	namespace
	{
		// Width of the name column (0..1 of the table), shared by every
		// Details-like table and remembered (editor_details.txt).
		float g_name_ratio = 0.34f;
		bool g_name_ratio_loaded = false;
		const char* const kDetailsFile = "editor_details.txt";

		void LoadNameRatio()
		{
			g_name_ratio_loaded = true;
			std::ifstream in(kDetailsFile);
			float ratio = 0.f;
			if (in >> ratio && ratio >= 0.1f && ratio <= 0.9f)
				g_name_ratio = ratio;
		}

		void SaveNameRatio()
		{
			std::ofstream out(kDetailsFile, std::ios::trunc);
			if (out)
				out << g_name_ratio << "\n";
		}
	}

	bool BeginTable(const char* id)
	{
		if (!g_name_ratio_loaded)
			LoadNameRatio();

		// Resizable : drag the line between the names and the values.
		const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings |
		                              ImGuiTableFlags_PadOuterX | ImGuiTableFlags_Resizable |
		                              ImGuiTableFlags_BordersInnerV;
		ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
		border.w *= 0.45f;
		ImGui::PushStyleColor(ImGuiCol_TableBorderLight, border);
		const bool open = ImGui::BeginTable(id, 2, flags);
		ImGui::PopStyleColor();
		if (!open)
			return false;
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, g_name_ratio);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.f - g_name_ratio);

		// Every table follows the shared ratio, except the one being resized.
		if (ImGuiTable* table = ImGui::GetCurrentTable())
		{
			if (table->ResizedColumn == -1 && table->LastResizedColumn == -1)
			{
				table->Columns[0].StretchWeight = g_name_ratio;
				table->Columns[1].StretchWeight = 1.f - g_name_ratio;
			}
		}
		return true;
	}


	void EndTable()
	{
		if (ImGuiTable* table = ImGui::GetCurrentTable())
		{
			if (table->ResizedColumn != -1 || table->LastResizedColumn != -1)
			{
				const float a = table->Columns[0].StretchWeight;
				const float b = table->Columns[1].StretchWeight;
				if (a > 0.f && b > 0.f)
				{
					const float ratio = std::clamp(a / (a + b), 0.1f, 0.9f);
					if (std::fabs(ratio - g_name_ratio) > 1e-4f)
					{
						g_name_ratio = ratio;
						if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
							SaveNameRatio();
					}
				}
			}
			// Drag finished : saved once.
			static bool was_resizing = false;
			const bool resizing = table->ResizedColumn != -1;
			if (was_resizing && !resizing)
				SaveNameRatio();
			was_resizing = resizing;
		}
		ImGui::EndTable();
	}


	void RowLabel(const char* label)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::TableNextColumn();
		ImGui::SetNextItemWidth(-FLT_MIN);
	}


	bool Vector(const char* id, float* values, int count, float speed, const float* /*reset*/, const char* format)
	{
		count = std::clamp(count, 1, 4);
		bool changed = false;

		const ImGuiStyle& style = ImGui::GetStyle();
		const float spacing = style.ItemInnerSpacing.x;
		const float tag_width = ImGui::CalcTextSize("X").x + style.FramePadding.x * 1.2f;
		const float total = ImGui::GetContentRegionAvail().x;
		const float field_width = std::max(20.f, (total - (tag_width + spacing) * count) / count);
		const float height = ImGui::GetFrameHeight();

		ImGui::PushID(id);
		for (int i = 0; i < count; ++i)
		{
			if (i > 0)
				ImGui::SameLine(0.f, spacing);
			ImGui::PushID(i);

			// Colored tag : drawn like a small frame, draggable.
			const ImVec2 tag_min = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##tag", ImVec2(tag_width, height));
			const bool tag_active = ImGui::IsItemActive();
			if (tag_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f))
			{
				const float delta = ImGui::GetIO().MouseDelta.x;
				if (delta != 0.f)
				{
					values[i] += delta * speed * (ImGui::GetIO().KeyShift ? 10.f : ImGui::GetIO().KeyAlt ? 0.1f : 1.f);
					changed = true;
				}
			}

			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 tag_max(tag_min.x + tag_width, tag_min.y + height);
			ImVec4 color = kAxisColors[i];
			if (tag_active)
				color = ImVec4(std::min(1.f, color.x * 1.25f), std::min(1.f, color.y * 1.25f), std::min(1.f, color.z * 1.25f), 1.f);
			draw->AddRectFilled(tag_min, tag_max, ImGui::ColorConvertFloat4ToU32(color), style.FrameRounding);
			const ImVec2 letter = ImGui::CalcTextSize(kAxisNames[i]);
			draw->AddText(ImVec2(tag_min.x + (tag_width - letter.x) * 0.5f, tag_min.y + (height - letter.y) * 0.5f),
			              IM_COL32(255, 255, 255, 255), kAxisNames[i]);

			// The field, glued to its tag.
			ImGui::SameLine(0.f, 0.f);
			ImGui::SetNextItemWidth(field_width);
			changed |= ImGui::DragFloat("##value", &values[i], speed, 0.f, 0.f, format);

			ImGui::PopID();
		}
		ImGui::PopID();
		return changed;
	}


	bool VectorRow(const char* label, float* values, int count, float speed, const float* reset, const char* format)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::PushID(label);
		bool changed = LabelMenu(label, values, count, reset);
		ImGui::TableNextColumn();
		changed |= Vector("##vector", values, count, speed, reset, format);
		ImGui::PopID();
		return changed;
	}


	bool TransformRows(lynx::transform& value)
	{
		float location[3] = { value.location.x, value.location.y, value.location.z };
		float rotation[3] = { value.rotation.x, value.rotation.y, value.rotation.z };
		float scale[3] = { value.scale.x, value.scale.y, value.scale.z };

		static const float kZero[3] = { 0.f, 0.f, 0.f };
		static const float kOne[3] = { 1.f, 1.f, 1.f };

		bool changed = false;
		changed |= VectorRow("Location", location, 3, 0.05f, kZero, "%.2f");
		changed |= VectorRow("Rotation", rotation, 3, 0.5f, kZero, "%.2f");   // degrees
		changed |= VectorRow("Scale", scale, 3, 0.01f, kOne, "%.2f");

		if (changed)
		{
			value.location = { location[0], location[1], location[2] };
			value.rotation = { rotation[0], rotation[1], rotation[2] };
			value.scale = { scale[0], scale[1], scale[2] };
		}
		return changed;
	}
}
