#include "VoxelTypeEditor.h"

#include "../core/Voxels.h"
#include "../core/Utils.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace lynx::editor::voxel_types
{
	namespace
	{
		constexpr float kSwatch = 40.f;
		constexpr const char* kFile = "assets/voxels.json";

		bool g_dirty = false;          // changed, not written yet
		std::string g_message;         // last save result
		double g_message_time = -100.0;
		char g_new_flag[64] = {};
		bool g_confirm_delete = false;

		const char* const kSides[] = { "LEFT", "RIGHT", "TOP", "BOTTOM", "INSIDE" };

		void Changed(uint8_t type, const voxels::VoxelType& value, uint32_t scene)
		{
			voxels::SetType(type, value);
			voxels::ApplyToScene(scene);
			g_dirty = true;
		}

		void Outline(bool selected)
		{
			if (!selected)
				return;
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			// Text color : visible on the light pixel theme as on a dark one.
			ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - 3.f, min.y - 3.f), ImVec2(max.x + 3.f, max.y + 3.f),
			                                    ImGui::GetColorU32(ImGuiCol_Text), 0.f, 0, 2.f);
		}

		std::string Hex(const float* rgb)
		{
			char text[8];
			auto c = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
			std::snprintf(text, sizeof(text), "%02x%02x%02x", c(rgb[0]), c(rgb[1]), c(rgb[2]));
			return text;
		}

		voxels::VoxelType NewType(const voxels::VoxelType* from)
		{
			voxels::VoxelType t;
			if (from)
			{
				t = *from;
				t.name = from->name + " copy";
			}
			else
			{
				t.name = "Voxel " + std::to_string(voxels::GetTypeCount() + 1);
				t.flags = voxels::GetSolidMask();
				// A color that differs from the last type.
				const float hue = std::fmod(voxels::GetTypeCount() * 0.137f, 1.f);
				ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.85f, t.color[0], t.color[1], t.color[2]);
				t.color[3] = 1.f;
			}
			return t;
		}

		int Add(const voxels::VoxelType* from, uint32_t scene)
		{
			const uint8_t id = voxels::AddType(NewType(from));
			if (id == 0)
			{
				g_message = "255 types at most";
				g_message_time = ImGui::GetTime();
				return 0;
			}
			voxels::ApplyToScene(scene);
			g_dirty = true;
			return id;
		}
	}


	bool HasUnsavedChanges()
	{
		return g_dirty;
	}

	bool Save()
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kFile).parent_path(), ec);
		std::ofstream out(kFile, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			g_message = std::string("Could not write ") + kFile;
			g_message_time = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
			std::cerr << "[VOXELS] " << g_message << "\n";
			return false;
		}
		out << voxels::SaveToString();
		g_dirty = false;
		g_message = "Saved in assets/voxels.json";
		g_message_time = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
		return true;
	}


	// =========================================================================
	// Paint window : swatches
	// =========================================================================

	void DrawPalette(int& selected, uint32_t scene, bool& open_editor)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float avail = ImGui::GetContentRegionAvail().x;
		const int per_row = std::max(1, static_cast<int>((avail + style.ItemSpacing.x) / (kSwatch + style.ItemSpacing.x)));
		int column = 0;
		auto next = [&]()
		{
			if (++column < per_row)
				ImGui::SameLine();
			else
				column = 0;
		};

		// Empty voxel (eraser)
		ImGui::PushID(0);
		if (ImGui::ColorButton("##VoxelEmpty", ImVec4(0.f, 0.f, 0.f, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(kSwatch, kSwatch)))
			selected = 0;
		Outline(selected == 0);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("0 : empty (eraser)");
		ImGui::PopID();
		next();

		const int count = voxels::GetTypeCount();
		int remove = 0;
		for (int type = 1; type <= count; ++type)
		{
			const voxels::VoxelType* t = voxels::GetType(static_cast<uint8_t>(type));
			if (!t)
				continue;

			ImGui::PushID(type);
			const ImVec4 color(t->color[0], t->color[1], t->color[2], 1.f);
			if (ImGui::ColorButton("##VoxelColor", color, ImGuiColorEditFlags_NoTooltip, ImVec2(kSwatch, kSwatch)))
				selected = type;
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				open_editor = true;
			Outline(selected == type);

			// Small marks : emissive (star), no collision (dashed corner)
			{
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();
				if (t->emissive)
					dl->AddCircleFilled(ImVec2(max.x - 6.f, min.y + 6.f), 3.5f, IM_COL32(255, 245, 160, 255));
				if ((t->flags & voxels::GetCollisionMask()) == 0)
					dl->AddLine(ImVec2(min.x + 3.f, max.y - 3.f), ImVec2(min.x + 12.f, max.y - 12.f), IM_COL32(0, 0, 0, 160), 2.f);
			}

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%d : %s\n(double-click : edit, right click : menu)", type, t->name.c_str());

			if (ImGui::BeginPopupContextItem("##type_menu"))
			{
				ImGui::TextDisabled("%d : %s", type, t->name.c_str());
				ImGui::Separator();
				if (ImGui::MenuItem("Edit (Color Picking)"))
				{
					selected = type;
					open_editor = true;
				}
				if (ImGui::MenuItem("Duplicate"))
				{
					const voxels::VoxelType copy = *t;
					if (const int id = Add(&copy, scene))
					{
						selected = id;
						open_editor = true;
					}
				}
				if (ImGui::MenuItem("Delete", nullptr, false, type == count))
					remove = type;
				if (type != count && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Only the last type can be deleted :\nthe levels store the number of the types.");
				ImGui::EndPopup();
			}
			ImGui::PopID();
			next();
		}

		// New type
		ImGui::PushID("##new_type");
		if (ImGui::Button("+", ImVec2(kSwatch, kSwatch)))
		{
			const voxels::VoxelType* from = nullptr;
			if (const int id = Add(from, scene))
			{
				selected = id;
				open_editor = true;
			}
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("New voxel type");
		ImGui::PopID();
		if (column != 0)
			ImGui::NewLine();

		if (remove)
		{
			voxels::RemoveLastType();
			voxels::ApplyToScene(scene);
			if (selected >= remove)
				selected = remove - 1;
			Save();
		}

		if (count == 0)
			ImGui::TextDisabled("No voxel type yet : \"+\" adds one.");
	}


	// =========================================================================
	// Color Picking window : the selected type
	// =========================================================================

	void DrawEditor(int& selected, uint32_t scene)
	{
		const voxels::VoxelType* current = selected > 0 ? voxels::GetType(static_cast<uint8_t>(selected)) : nullptr;
		if (!current)
		{
			ImGui::TextDisabled("No voxel type selected.");
			ImGui::Spacing();
			ImGui::TextWrapped("Pick a type in the Paint window (\"+\" adds one), or press C over a voxel of the viewport.");
			return;
		}

		const uint8_t id = static_cast<uint8_t>(selected);
		voxels::VoxelType t = *current;
		bool changed = false;

		ImGui::Text("Type %d", selected);
		ImGui::SameLine();
		ImGui::ColorButton("##preview", ImVec4(t.color[0], t.color[1], t.color[2], 1.f), ImGuiColorEditFlags_NoTooltip,
		                   ImVec2(ImGui::GetFrameHeight() * 2.f, ImGui::GetFrameHeight()));

		// Name
		{
			char name[128];
			std::snprintf(name, sizeof(name), "%s", t.name.c_str());
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText("##name", name, sizeof(name)))
			{
				t.name = name;
				changed = true;
			}
		}

		// Color picking
		ImGui::SeparatorText("Color");
		{
			ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 16.f));
			if (ImGui::ColorPicker3("##color", t.color,
			                        ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar |
			                        ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB))
				changed = true;

			char hex[16];
			std::snprintf(hex, sizeof(hex), "%s", Hex(t.color).c_str());
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
			if (ImGui::InputText("Hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsNoBlank))
			{
				float rgba[4];
				if (HexToColor(hex, rgba))
				{
					std::copy(rgba, rgba + 3, t.color);
					changed = true;
				}
			}

			// The colors of the other types (click : take it).
			const int count = voxels::GetTypeCount();
			if (count > 1)
			{
				ImGui::TextDisabled("Other types");
				const float size = ImGui::GetFrameHeight();
				const float avail = ImGui::GetContentRegionAvail().x;
				float x = 0.f;
				for (int other = 1; other <= count; ++other)
				{
					const voxels::VoxelType* o = voxels::GetType(static_cast<uint8_t>(other));
					if (!o || other == selected)
						continue;
					if (x > 0.f && x + size > avail)
						x = 0.f;
					else if (x > 0.f)
						ImGui::SameLine();
					ImGui::PushID(other);
					if (ImGui::ColorButton("##other", ImVec4(o->color[0], o->color[1], o->color[2], 1.f), 0, ImVec2(size, size)))
					{
						std::copy(o->color, o->color + 3, t.color);
						changed = true;
					}
					ImGui::PopID();
					x += size + ImGui::GetStyle().ItemSpacing.x;
				}
			}
		}

		// Collision
		ImGui::SeparatorText("Collision");
		{
			const uint32_t all = voxels::GetCollisionMask();
			const uint32_t solid = voxels::GetSolidMask();
			const uint32_t collision = t.flags & all;
			int mode = collision == solid ? 0 : collision == 0 ? 1 : 2;
			const char* modes[] = { "Solid (4 sides)", "None", "Custom" };
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
			if (ImGui::Combo("##collision", &mode, modes, 3))
			{
				t.flags &= ~all;
				if (mode == 0)
					t.flags |= solid;
				else if (mode == 2)
					t.flags |= collision == 0 || collision == solid ? voxels::GetFlagMask("TOP") : collision;
				changed = true;
			}
			if (mode == 2)
			{
				for (int i = 0; i < 5; ++i)
				{
					const uint32_t mask = voxels::GetFlagMask(kSides[i]);
					bool on = (t.flags & mask) != 0;
					if (i > 0)
						ImGui::SameLine();
					if (ImGui::Checkbox(kSides[i], &on))
					{
						t.flags = on ? (t.flags | mask) : (t.flags & ~mask);
						changed = true;
					}
				}
				ImGui::PushTextWrapPos(0.f);
				ImGui::TextDisabled("TOP only : a platform you can jump through from below.");
				ImGui::PopTextWrapPos();
			}
		}

		// Game flags
		ImGui::SeparatorText("Flags");
		{
			const std::vector<std::string> names = voxels::GetFlagNames();
			if (names.empty())
				ImGui::TextDisabled("No flag yet.");
			for (size_t i = 0; i < names.size(); ++i)
			{
				const uint32_t mask = voxels::GetFlagMask(names[i].c_str());
				bool on = (t.flags & mask) != 0;
				if (i > 0)
				{
					// Next to the previous one when it fits.
					const float w = ImGui::CalcTextSize(names[i].c_str()).x + ImGui::GetFrameHeight() * 1.5f;
					const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
					if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < right)
						ImGui::SameLine();
				}
				if (ImGui::Checkbox(names[i].c_str(), &on))
				{
					t.flags = on ? (t.flags | mask) : (t.flags & ~mask);
					changed = true;
				}
			}
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
			const bool enter = ImGui::InputTextWithHint("##new_flag", "NEW_FLAG", g_new_flag, sizeof(g_new_flag),
			                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsUppercase |
			                                            ImGuiInputTextFlags_CharsNoBlank);
			ImGui::SameLine();
			if ((ImGui::Button("Add flag") || enter) && g_new_flag[0])
			{
				if (const uint32_t mask = voxels::DeclareFlag(g_new_flag))
				{
					t.flags |= mask;
					changed = true;
					g_new_flag[0] = 0;
				}
				else
				{
					g_message = "Too many flags";
					g_message_time = ImGui::GetTime();
				}
			}
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("Game code : lynx::voxels::GetFlagMask(\"NAME\").");
			ImGui::PopTextWrapPos();
		}

		// Other properties
		ImGui::SeparatorText("Properties");
		if (ImGui::Checkbox("Emissive", &t.emissive))
			changed = true;
		if (t.emissive)
		{
			ImGui::SameLine();
			if (ImGui::ColorEdit3("##emissive", t.emissive_color, ImGuiColorEditFlags_NoInputs))
				changed = true;
		}
		if (ImGui::Checkbox("Indestructible", &t.indestructible))
			changed = true;
		{
			char event[128];
			std::snprintf(event, sizeof(event), "%s", t.on_destroyed.c_str());
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
			if (ImGui::InputTextWithHint("On destroyed", "event name", event, sizeof(event)))
			{
				t.on_destroyed = event;
				changed = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Event of the game : lynx::voxels::RegisterDestroyedEvent(name, fn).");
		}

		if (changed)
			Changed(id, t, scene);

		// Save once the edit is finished (not on every move of the picker).
		if (g_dirty && !ImGui::IsAnyItemActive())
			Save();

		// Buttons
		ImGui::Spacing();
		ImGui::Separator();
		if (ImGui::Button("Duplicate"))
		{
			if (const int nid = Add(current, scene))
				selected = nid;
		}
		ImGui::SameLine();
		const bool last = selected == voxels::GetTypeCount();
		ImGui::BeginDisabled(!last);
		if (ImGui::Button("Delete"))
			g_confirm_delete = true;
		ImGui::EndDisabled();
		if (!last && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Only the last type can be deleted :\nthe levels store the number of the types.");
		ImGui::SameLine();
		if (ImGui::Button("Reload file"))
		{
			if (voxels::LoadFile("voxels.json"))
			{
				voxels::ApplyToScene(scene);
				g_dirty = false;
				g_message = "Reloaded";
			}
			else
				g_message = voxels::GetLoadError();
			g_message_time = ImGui::GetTime();
		}

		if (g_confirm_delete)
		{
			ImGui::OpenPopup("Delete voxel type");
			g_confirm_delete = false;
		}
		if (ImGui::BeginPopupModal("Delete voxel type", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Delete \"%s\" ?", t.name.c_str());
			ImGui::TextDisabled("Voxels of this type already painted keep its number :\nthey would take the next type added.");
			if (ImGui::Button("Delete"))
			{
				voxels::RemoveLastType();
				voxels::ApplyToScene(scene);
				selected = voxels::GetTypeCount();
				Save();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (!g_message.empty() && ImGui::GetTime() - g_message_time < 3.0)
			ImGui::TextDisabled("%s", g_message.c_str());
	}
}
