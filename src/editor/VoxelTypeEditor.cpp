#include "VoxelTypeEditor.h"

#include "../core/Voxels.h"
#include "../core/Utils.h"
#include "raven/RavenUi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace lynx::editor::voxel_types
{
	namespace
	{
		constexpr float kSwatch = 36.f;
		constexpr const char* kFile = "assets/voxels.json";
		const char* const kSides[] = { "LEFT", "RIGHT", "TOP", "BOTTOM", "INSIDE" };

		Host g_host;
		bool g_dirty = false;          // changed, not written yet
		double g_dirty_time = 0.0;     // last change (saved a moment after)

		// Palette
		Rid g_palette = R_INVALID_ID;
		Rid g_hint = R_INVALID_ID;
		std::string g_palette_key;     // what the swatches show (rebuilt when it changes)
		std::vector<Rid> g_swatches;   // swatches of the palette (repainted when a texture arrives)
		bool g_texture_pending = false;
		std::unique_ptr<ui::Menu> g_menu;

		// Editor
		Rid g_editor = R_INVALID_ID;
		int g_editor_type = -1;        // type shown by the fields
		std::string g_editor_key;      // its values when they were shown (a reload, an undo... : shown again)
		bool g_filling = false;        // the fields are being set : no change callback

		struct Widgets
		{
			Rid empty, body, id, name, color, others, collision, sides, sides_hint, side[5];
			Rid flags, new_flag, emissive, emissive_color, texture, tile_row, tile, indestructible, on_destroyed;
			Rid preset, behavior, density_row, density, density_value, dispersion_row, dispersion, dispersion_value;
			Rid drag_row, drag, drag_value, buoyancy_row, buoyancy, buoyancy_value, friction_row, friction, friction_value;
			Rid bounciness_row, bounciness, bounciness_value, damage, damage_value, contact, duplicate, remove, message;
		} w{};

		// What a voxel of this type looks like : its texture (assets/<texture>)
		// multiplied by its color, as the renderer draws it (a textured type is
		// usually white : the texture alone), else the plain color.
		void PaintType(RPainter* p, float x, float y, float w, float h, const voxels::VoxelType& t)
		{
			if (!t.texture.empty())
			{
				std::error_code ec;
				const Rid texture = ui::FileTexture(std::filesystem::current_path(ec) / "assets" / t.texture);
				if (texture != R_INVALID_ID)
				{
					int tw = 0, th = 0;
					Rtexture_get_size(texture, &tw, &th);
					if (tw > 0 && th > 0)
					{
						Rdraw_image(p, texture, x, y, w, h, t.color[0], t.color[1], t.color[2], 1.f);
						return;
					}
					g_texture_pending = true;   // still loading : painted again by Update
				}
			}
			Rdraw_rect(p, x, y, w, h, 0.f, t.color[0], t.color[1], t.color[2], 1.f);
		}

		int Selected()
		{
			return g_host.get_selected ? g_host.get_selected() : 0;
		}

		void Select(int type)
		{
			if (g_host.set_selected)
				g_host.set_selected(type);
		}

		void Message(const std::string& text)
		{
			if (w.message != R_INVALID_ID)
				ui::SetText(w.message, text);
		}

		void Changed(uint8_t type, const voxels::VoxelType& value)
		{
			voxels::SetType(type, value);
			voxels::ApplyToScene(g_host.scene);
			g_dirty = true;
			g_dirty_time = Rget_time();
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
				Rcolor_hsv_to_rgb(hue, 0.55f, 0.85f, &t.color[0], &t.color[1], &t.color[2]);
				t.color[3] = 1.f;
			}
			return t;
		}

		int Add(const voxels::VoxelType* from)
		{
			const uint8_t id = voxels::AddType(NewType(from));
			if (id == 0)
			{
				Message("255 types at most");
				return 0;
			}
			voxels::ApplyToScene(g_host.scene);
			g_dirty = true;
			g_dirty_time = Rget_time();
			return id;
		}

		void RemoveLast()
		{
			const int count = voxels::GetTypeCount();
			voxels::RemoveLastType();
			voxels::ApplyToScene(g_host.scene);
			if (Selected() >= count)
				Select(count - 1);
			Save();
		}

		void AskRemoveLast()
		{
			const int count = voxels::GetTypeCount();
			const voxels::VoxelType* t = voxels::GetType(static_cast<uint8_t>(count));
			if (!t)
				return;
			ui::Ask(g_host.window, "Delete voxel type",
			        "Delete \"" + t->name + "\" ?\n\nVoxels of this type already painted keep its number : "
			        "they would take the next type added.",
			        { "Delete", "Cancel" }, [](int button)
			{
				if (button == 0)
					RemoveLast();
			});
		}

		// ---------------------------------------------------------------------
		// Palette
		// ---------------------------------------------------------------------

		std::string PaletteKey()
		{
			std::string key = std::to_string(Selected()) + ";";
			char part[96];
			for (int type = 1; type <= voxels::GetTypeCount(); ++type)
				if (const voxels::VoxelType* t = voxels::GetType(static_cast<uint8_t>(type)))
				{
					std::snprintf(part, sizeof(part), "%d:%.3f,%.3f,%.3f,%d,%u,", type, t->color[0], t->color[1], t->color[2],
					              t->emissive ? 1 : 0, (t->flags & voxels::GetCollisionMask()) ? 1u : 0u);
					key += part;
					key += t->texture + ";";
				}
			return key;
		}

		void OpenTypeMenu(int type)
		{
			const voxels::VoxelType* t = voxels::GetType(static_cast<uint8_t>(type));
			if (!t)
				return;
			const int count = voxels::GetTypeCount();
			if (!g_menu)
				g_menu = std::make_unique<ui::Menu>(g_host.window);
			g_menu->Clear();
			g_menu->Title(std::to_string(type) + " : " + t->name);
			g_menu->Separator();
			g_menu->Item("Edit (Color Picking)", [type]()
			{
				Select(type);
				if (g_host.show_editor)
					g_host.show_editor();
			});
			g_menu->Item("Duplicate", [type]()
			{
				if (const voxels::VoxelType* src = voxels::GetType(static_cast<uint8_t>(type)))
				{
					const voxels::VoxelType copy = *src;
					if (const int id = Add(&copy))
					{
						Select(id);
						if (g_host.show_editor)
							g_host.show_editor();
					}
				}
			});
			const Rid remove = g_menu->Item("Delete", []() { AskRemoveLast(); }, type == count);
			if (type != count)
				Rwidget_set_tooltip(remove, "Only the last type can be deleted : the levels store the number of the types.");
			g_menu->Open();
		}

		// One swatch : the color, an outline when it is the brush type, small marks.
		Rid MakeSwatch(int type)
		{
			const Rid swatch = Rwidget_create(R_WIDGET_CUSTOM);
			Rwidget_set_size(swatch, kSwatch, kSwatch);
			Rwidget_set_cursor(swatch, R_CURSOR_HAND);
			ui::OnPaint(swatch, [swatch, type](RPainter* p, float width, float height)
			{
				float text[4];
				Rwidget_get_color(swatch, R_COLOR_TEXT, &text[0], &text[1], &text[2], &text[3]);
				const bool selected = Selected() == type;
				const float inset = 3.f;
				if (type == 0)
				{
					Rdraw_rect(p, inset, inset, width - inset * 2.f, height - inset * 2.f, 0.f, 0.f, 0.f, 0.f, 1.f);
					// eraser : a diagonal
					Rdraw_line(p, inset + 4.f, height - inset - 4.f, width - inset - 4.f, inset + 4.f, 2.f, 0.9f, 0.3f, 0.3f, 1.f);
				}
				else if (const voxels::VoxelType* t = voxels::GetType(static_cast<uint8_t>(type)))
				{
					PaintType(p, inset, inset, width - inset * 2.f, height - inset * 2.f, *t);
					Rdraw_rect_outline(p, inset, inset, width - inset * 2.f, height - inset * 2.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.6f);
					if (t->emissive)
						Rdraw_circle(p, width - inset - 6.f, inset + 6.f, 3.5f, 1.f, 0.96f, 0.63f, 1.f);
					if ((t->flags & voxels::GetCollisionMask()) == 0)
						Rdraw_line(p, inset + 3.f, height - inset - 3.f, inset + 12.f, height - inset - 12.f, 2.f, 0.f, 0.f, 0.f, 0.65f);
				}
				if (selected)
					Rdraw_rect_outline(p, 0.f, 0.f, width, height, 0.f, 2.f, text[0], text[1], text[2], 1.f);
			});
			ui::OnEvent(swatch, [type](const REvent& event)
			{
				if (event.type == R_EVENT_MOUSE_DOWN && event.button == R_MOUSE_LEFT)
				{
					Select(type);
					if (event.clicks == 2 && type > 0 && g_host.show_editor)
						g_host.show_editor();
					return true;
				}
				if (event.type == R_EVENT_MOUSE_UP && event.button == R_MOUSE_RIGHT && type > 0)
				{
					ui::Later([type]() { OpenTypeMenu(type); });
					return true;
				}
				return event.type == R_EVENT_MOUSE_DOWN;
			});
			if (type == 0)
				Rwidget_set_tooltip(swatch, "0 : empty (eraser)");
			else if (const voxels::VoxelType* t = voxels::GetType(static_cast<uint8_t>(type)))
				Rwidget_set_tooltip(swatch, (std::to_string(type) + " : " + t->name + "\n(double-click : edit, right click : menu)").c_str());
			return swatch;
		}

		void RebuildPalette()
		{
			Rwidget_clear_children(g_palette);
			g_swatches.clear();
			for (int type = 0; type <= voxels::GetTypeCount(); ++type)
				if (type == 0 || voxels::GetType(static_cast<uint8_t>(type)))
					g_swatches.push_back(Rwidget_add_child(g_palette, MakeSwatch(type)));

			const Rid add = Rbutton_create("+");
			Rwidget_set_size(add, kSwatch, kSwatch);
			Rwidget_set_tooltip(add, "New voxel type");
			ui::OnClick(add, []()
			{
				if (const int id = Add(nullptr))
				{
					Select(id);
					if (g_host.show_editor)
						g_host.show_editor();
				}
			});
			Rwidget_add_child(g_palette, add);

			ui::SetText(g_hint, voxels::GetTypeCount() == 0 ? std::string("No voxel type yet : \"+\" adds one.")
			                                                 : std::string());
			ui::SetVisible(g_hint, voxels::GetTypeCount() == 0);
		}

		// ---------------------------------------------------------------------
		// Editor
		// ---------------------------------------------------------------------

		// The type being edited, a copy to change and give back.
		bool Current(uint8_t& id, voxels::VoxelType& t)
		{
			const int selected = Selected();
			const voxels::VoxelType* current = selected > 0 ? voxels::GetType(static_cast<uint8_t>(selected)) : nullptr;
			if (!current)
				return false;
			id = static_cast<uint8_t>(selected);
			t = *current;
			return true;
		}

		// A change made by a field : `edit` changes the copy.
		void Edit(const std::function<void(voxels::VoxelType&)>& edit, bool physics = false)
		{
			if (g_filling)
				return;
			uint8_t id = 0;
			voxels::VoxelType t;
			if (!Current(id, t))
				return;
			edit(t);
			if (physics)
				t.physics.preset.clear();   // custom values now
			Changed(id, t);
		}

		std::string EditorKey(const voxels::VoxelType& t)
		{
			char text[256];
			std::snprintf(text, sizeof(text), "%s|%.4f,%.4f,%.4f|%u|%d|%.3f,%.3f,%.3f|%s|%.3f|%d|%s|%s|%d|%.3f|%d|%.3f|%.3f|%.3f|%.3f|%.3f|%d|%d",
			              t.name.c_str(), t.color[0], t.color[1], t.color[2], t.flags, t.emissive ? 1 : 0,
			              t.emissive_color[0], t.emissive_color[1], t.emissive_color[2], t.texture.c_str(), t.texture_tile,
			              t.indestructible ? 1 : 0, t.on_destroyed.c_str(), t.physics.preset.c_str(),
			              static_cast<int>(t.physics.behavior), t.physics.density, t.physics.dispersion, t.physics.friction,
			              t.physics.bounciness, t.physics.drag, t.physics.buoyancy, t.physics.damage,
			              t.physics.contact_events ? 1 : 0, static_cast<int>(voxels::GetFlagNames().size()));
			return text;
		}

		std::string Number(float value, int decimals = 2)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.*f", decimals, value);
			return text;
		}

		void RebuildOthers(int selected)
		{
			Rwidget_clear_children(w.others);
			for (int other = 1; other <= voxels::GetTypeCount(); ++other)
			{
				const voxels::VoxelType* o = voxels::GetType(static_cast<uint8_t>(other));
				if (!o || other == selected)
					continue;
				const Rid swatch = Rwidget_create(R_WIDGET_CUSTOM);
				Rwidget_set_size(swatch, 20.f, 20.f);
				Rwidget_set_cursor(swatch, R_CURSOR_HAND);
				Rwidget_set_tooltip(swatch, ("Take the color of " + o->name).c_str());
				ui::OnPaint(swatch, [other](RPainter* p, float width, float height)
				{
					if (const voxels::VoxelType* t = voxels::GetType(static_cast<uint8_t>(other)))
					{
						PaintType(p, 0.f, 0.f, width, height, *t);
						Rdraw_rect_outline(p, 0.f, 0.f, width, height, 0.f, 1.f, 0.f, 0.f, 0.f, 0.6f);
					}
				});
				ui::OnEvent(swatch, [other](const REvent& event)
				{
					if (event.type != R_EVENT_MOUSE_DOWN || event.button != R_MOUSE_LEFT)
						return false;
					if (const voxels::VoxelType* src = voxels::GetType(static_cast<uint8_t>(other)))
					{
						const float rgb[3] = { src->color[0], src->color[1], src->color[2] };
						Edit([rgb](voxels::VoxelType& t) { std::copy(rgb, rgb + 3, t.color); });
						g_editor_key.clear();   // the picker shows the new color
					}
					return true;
				});
				Rwidget_add_child(w.others, swatch);
			}
		}

		void RebuildFlags(const voxels::VoxelType& t)
		{
			Rwidget_clear_children(w.flags);
			const std::vector<std::string> names = voxels::GetFlagNames();
			if (names.empty())
			{
				Rwidget_add_child(w.flags, Rlabel_create("No flag yet."));
				return;
			}
			for (const std::string& name : names)
			{
				const uint32_t mask = voxels::GetFlagMask(name.c_str());
				const Rid box = Rcheckbox_create(name.c_str());
				Rcheckbox_set_checked(box, (t.flags & mask) != 0);
				ui::OnToggle(box, [mask](bool on) { Edit([mask, on](voxels::VoxelType& v) { v.flags = on ? (v.flags | mask) : (v.flags & ~mask); }); });
				Rwidget_add_child(w.flags, box);
			}
		}

		// The fields show the type (when the selection or the type changed elsewhere).
		void FillEditor()
		{
			const int selected = Selected();
			const voxels::VoxelType* current = selected > 0 ? voxels::GetType(static_cast<uint8_t>(selected)) : nullptr;
			ui::SetVisible(w.empty, current == nullptr);
			ui::SetVisible(w.body, current != nullptr);
			if (!current)
				return;

			const voxels::VoxelType& t = *current;
			g_filling = true;

			ui::SetText(w.id, "Type " + std::to_string(selected));
			if (!Rwidget_is_focused(w.name))
				ui::SetText(w.name, t.name);
			Rcolor_picker_set_color(w.color, t.color[0], t.color[1], t.color[2], 1.f);
			RebuildOthers(selected);

			const uint32_t all = voxels::GetCollisionMask();
			const uint32_t solid = voxels::GetSolidMask();
			const uint32_t collision = t.flags & all;
			const int mode = collision == solid ? 0 : collision == 0 ? 1 : 2;
			if (Rcombo_box_get_selected(w.collision) != mode)
				Rcombo_box_set_selected(w.collision, mode);
			ui::SetVisible(w.sides, mode == 2);
			ui::SetVisible(w.sides_hint, mode == 2);
			for (int i = 0; i < 5; ++i)
				ui::SetChecked(w.side[i], (t.flags & voxels::GetFlagMask(kSides[i])) != 0);

			RebuildFlags(t);

			ui::SetChecked(w.emissive, t.emissive);
			ui::SetVisible(w.emissive_color, t.emissive);
			Rcolor_picker_set_color(w.emissive_color, t.emissive_color[0], t.emissive_color[1], t.emissive_color[2], 1.f);
			if (!Rwidget_is_focused(w.texture))
				ui::SetText(w.texture, t.texture);
			ui::SetVisible(w.tile_row, !t.texture.empty());
			ui::SetValue(w.tile, t.texture_tile);
			ui::SetChecked(w.indestructible, t.indestructible);
			if (!Rwidget_is_focused(w.on_destroyed))
				ui::SetText(w.on_destroyed, t.on_destroyed);

			// Physics
			const voxels::VoxelPhysicsProps& ph = t.physics;
			const auto& presets = voxels::GetPhysicsPresets();
			int preset_index = ph.preset.empty() ? (ph.IsDefault() ? 0 : -1) : -1;
			for (int i = 0; i < static_cast<int>(presets.size()); ++i)
				if (presets[i].first == ph.preset)
					preset_index = i + 1;
			Rcombo_box_clear_items(w.preset);
			Rcombo_box_add_item(w.preset, "None (static)");
			for (const auto& [name, props] : presets)
				Rcombo_box_add_item(w.preset, name.c_str());
			if (preset_index < 0)
			{
				Rcombo_box_add_item(w.preset, "Custom");
				preset_index = static_cast<int>(presets.size()) + 1;
			}
			Rcombo_box_set_selected(w.preset, preset_index);

			const int behavior = static_cast<int>(ph.behavior);
			if (Rcombo_box_get_selected(w.behavior) != behavior)
				Rcombo_box_set_selected(w.behavior, behavior);
			const bool moving = ph.behavior != voxels::VoxelBehavior::Static;
			const bool fluid = ph.behavior == voxels::VoxelBehavior::Liquid || ph.behavior == voxels::VoxelBehavior::Gas;
			ui::SetVisible(w.density_row, moving);
			ui::SetVisible(w.dispersion_row, moving && ph.behavior != voxels::VoxelBehavior::Powder);
			ui::SetVisible(w.drag_row, fluid);
			ui::SetVisible(w.buoyancy_row, fluid);
			ui::SetVisible(w.friction_row, !fluid);
			ui::SetVisible(w.bounciness_row, !fluid);
			auto slider = [](Rid s, Rid label, float v, int decimals)
			{
				ui::SetValue(s, v);
				ui::SetText(label, Number(v, decimals));
			};
			slider(w.density, w.density_value, ph.density, 2);
			slider(w.dispersion, w.dispersion_value, static_cast<float>(ph.dispersion), 0);
			slider(w.drag, w.drag_value, ph.drag, 2);
			slider(w.buoyancy, w.buoyancy_value, ph.buoyancy, 2);
			slider(w.friction, w.friction_value, ph.friction, 2);
			slider(w.bounciness, w.bounciness_value, ph.bounciness, 2);
			slider(w.damage, w.damage_value, ph.damage, 1);
			ui::SetChecked(w.contact, ph.contact_events);

			ui::SetEnabled(w.remove, selected == voxels::GetTypeCount());

			g_filling = false;
			g_editor_type = selected;
			g_editor_key = EditorKey(t);
		}

		// A slider of the physics : the value, then its label.
		void BindSlider(Rid slider, Rid label, int decimals, std::function<void(voxels::VoxelType&, float)> set)
		{
			ui::OnValue(slider, [label, decimals, set](double value)
			{
				ui::SetText(label, Number(static_cast<float>(value), decimals));
				const float v = static_cast<float>(value);
				Edit([set, v](voxels::VoxelType& t) { set(t, v); }, true);
			});
		}
	}


	void SetHost(const Host& host)
	{
		g_host = host;
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
			Message(std::string("Could not write ") + kFile);
			std::cerr << "[VOXELS] Could not write " << kFile << "\n";
			return false;
		}
		out << voxels::SaveToString();
		g_dirty = false;
		Message("Saved in assets/voxels.json");
		return true;
	}

	void BindPalette(Rid palette, Rid hint)
	{
		g_palette = palette;
		g_hint = hint;
		g_palette_key.clear();
	}

	void BindEditor(Rid panel)
	{
		g_editor = panel;
		auto find = [panel](const char* name) { return ui::Find(panel, name); };
		w.empty = find("vt.empty");
		w.body = find("vt.editor");
		w.id = find("vt.id");
		w.name = find("vt.name");
		w.color = find("vt.color");
		w.others = find("vt.others");
		w.collision = find("vt.collision");
		w.sides = find("vt.sides");
		w.sides_hint = find("vt.sides.hint");
		for (int i = 0; i < 5; ++i)
			w.side[i] = find((std::string("vt.side.") + kSides[i]).c_str());
		w.flags = find("vt.flags");
		w.new_flag = find("vt.new_flag");
		w.emissive = find("vt.emissive");
		w.emissive_color = find("vt.emissive_color");
		w.texture = find("vt.texture");
		w.tile_row = find("vt.tile.row");
		w.tile = find("vt.tile");
		w.indestructible = find("vt.indestructible");
		w.on_destroyed = find("vt.on_destroyed");
		w.preset = find("vt.preset");
		w.behavior = find("vt.behavior");
		w.density_row = find("vt.density.row");
		w.density = find("vt.density");
		w.density_value = find("vt.density.value");
		w.dispersion_row = find("vt.dispersion.row");
		w.dispersion = find("vt.dispersion");
		w.dispersion_value = find("vt.dispersion.value");
		w.drag_row = find("vt.drag.row");
		w.drag = find("vt.drag");
		w.drag_value = find("vt.drag.value");
		w.buoyancy_row = find("vt.buoyancy.row");
		w.buoyancy = find("vt.buoyancy");
		w.buoyancy_value = find("vt.buoyancy.value");
		w.friction_row = find("vt.friction.row");
		w.friction = find("vt.friction");
		w.friction_value = find("vt.friction.value");
		w.bounciness_row = find("vt.bounciness.row");
		w.bounciness = find("vt.bounciness");
		w.bounciness_value = find("vt.bounciness.value");
		w.damage = find("vt.damage");
		w.damage_value = find("vt.damage.value");
		w.contact = find("vt.contact");
		w.duplicate = find("vt.duplicate");
		w.remove = find("vt.delete");
		w.message = find("vt.message");

		ui::OnText(w.name, [](const std::string& text) { Edit([text](voxels::VoxelType& t) { t.name = text; }); });
		ui::OnColor(w.color, [](float r, float g, float b, float)
		{
			Edit([r, g, b](voxels::VoxelType& t) { t.color[0] = r; t.color[1] = g; t.color[2] = b; });
		});
		ui::OnSelect(w.collision, [](int mode)
		{
			Edit([mode](voxels::VoxelType& t)
			{
				const uint32_t all = voxels::GetCollisionMask();
				const uint32_t solid = voxels::GetSolidMask();
				const uint32_t collision = t.flags & all;
				t.flags &= ~all;
				if (mode == 0)
					t.flags |= solid;
				else if (mode == 2)
					t.flags |= collision == 0 || collision == solid ? voxels::GetFlagMask("TOP") : collision;
			});
			g_editor_key.clear();
		});
		for (int i = 0; i < 5; ++i)
		{
			const uint32_t mask = voxels::GetFlagMask(kSides[i]);
			ui::OnToggle(w.side[i], [mask](bool on) { Edit([mask, on](voxels::VoxelType& t) { t.flags = on ? (t.flags | mask) : (t.flags & ~mask); }); });
		}
		auto add_flag = []()
		{
			std::string name = Rtext_input_get_text(w.new_flag);
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
			if (name.empty())
				return;
			if (const uint32_t mask = voxels::DeclareFlag(name.c_str()))
			{
				Edit([mask](voxels::VoxelType& t) { t.flags |= mask; });
				Rtext_input_set_text(w.new_flag, "");
				g_editor_key.clear();
			}
			else
				Message("Too many flags");
		};
		ui::OnClick(find("vt.add_flag"), add_flag);
		ui::OnCommit(w.new_flag, [add_flag](const std::string&) { add_flag(); });

		ui::OnToggle(w.emissive, [](bool on)
		{
			Edit([on](voxels::VoxelType& t) { t.emissive = on; });
			ui::SetVisible(w.emissive_color, on);
		});
		ui::OnColor(w.emissive_color, [](float r, float g, float b, float)
		{
			Edit([r, g, b](voxels::VoxelType& t) { t.emissive_color[0] = r; t.emissive_color[1] = g; t.emissive_color[2] = b; });
		});
		ui::OnText(w.texture, [](const std::string& text)
		{
			Edit([text](voxels::VoxelType& t) { t.texture = text; });
			ui::SetVisible(w.tile_row, !text.empty());
		});
		// A .png dragged from the Content Browser : its path inside assets/.
		ui::OnDrop(w.texture, [](const RDropEvent& event)
		{
			if (!event.drag_type || std::strcmp(event.drag_type, ui::kDragAsset) != 0 || !event.text)
				return false;
			if (event.phase == R_DROP_DROPPED)
			{
				std::string first = event.text;
				first = first.substr(0, first.find('\n'));
				std::string generic = std::filesystem::path(first).generic_string();
				const size_t at = generic.rfind("assets/");
				if (at != std::string::npos)
					generic = generic.substr(at + 7);
				ui::SetText(w.texture, generic);
				Edit([generic](voxels::VoxelType& t) { t.texture = generic; });
				ui::SetVisible(w.tile_row, true);
			}
			return true;
		});
		ui::OnClick(find("vt.texture.clear"), []()
		{
			ui::SetText(w.texture, "");
			Edit([](voxels::VoxelType& t) { t.texture.clear(); });
			ui::SetVisible(w.tile_row, false);
		});
		ui::OnValue(w.tile, [](double v) { Edit([v](voxels::VoxelType& t) { t.texture_tile = std::max(static_cast<float>(v), 0.01f); }); });
		ui::OnToggle(w.indestructible, [](bool on) { Edit([on](voxels::VoxelType& t) { t.indestructible = on; }); });
		ui::OnText(w.on_destroyed, [](const std::string& text) { Edit([text](voxels::VoxelType& t) { t.on_destroyed = text; }); });

		ui::OnSelect(w.preset, [](int index)
		{
			if (g_filling)
				return;
			const auto& presets = voxels::GetPhysicsPresets();
			if (index == 0)
				Edit([](voxels::VoxelType& t) { t.physics = voxels::VoxelPhysicsProps{}; });
			else if (index - 1 < static_cast<int>(presets.size()))
			{
				const voxels::VoxelPhysicsProps props = presets[index - 1].second;
				Edit([props](voxels::VoxelType& t)
				{
					t.physics = props;
					// Collision that goes with it : fluids do not block.
					const uint32_t all = voxels::GetCollisionMask();
					t.flags &= ~all;
					if (props.behavior != voxels::VoxelBehavior::Liquid && props.behavior != voxels::VoxelBehavior::Gas)
						t.flags |= voxels::GetSolidMask();
				});
			}
			g_editor_key.clear();
		});
		ui::OnSelect(w.behavior, [](int behavior)
		{
			Edit([behavior](voxels::VoxelType& t) { t.physics.behavior = static_cast<voxels::VoxelBehavior>(behavior); }, true);
			g_editor_key.clear();
		});
		BindSlider(w.density, w.density_value, 2, [](voxels::VoxelType& t, float v) { t.physics.density = v; });
		BindSlider(w.dispersion, w.dispersion_value, 0, [](voxels::VoxelType& t, float v) { t.physics.dispersion = static_cast<int>(std::lround(v)); });
		BindSlider(w.drag, w.drag_value, 2, [](voxels::VoxelType& t, float v) { t.physics.drag = v; });
		BindSlider(w.buoyancy, w.buoyancy_value, 2, [](voxels::VoxelType& t, float v) { t.physics.buoyancy = v; });
		BindSlider(w.friction, w.friction_value, 2, [](voxels::VoxelType& t, float v) { t.physics.friction = v; });
		BindSlider(w.bounciness, w.bounciness_value, 2, [](voxels::VoxelType& t, float v) { t.physics.bounciness = v; });
		BindSlider(w.damage, w.damage_value, 1, [](voxels::VoxelType& t, float v) { t.physics.damage = v; });
		ui::OnToggle(w.contact, [](bool on) { Edit([on](voxels::VoxelType& t) { t.physics.contact_events = on; }, true); });

		ui::OnClick(w.duplicate, []()
		{
			uint8_t id = 0;
			voxels::VoxelType t;
			if (Current(id, t))
				if (const int nid = Add(voxels::GetType(id)))
					Select(nid);
		});
		ui::OnClick(w.remove, []() { AskRemoveLast(); });
		ui::OnClick(find("vt.reload"), []()
		{
			if (voxels::LoadFile("voxels.json"))
			{
				voxels::ApplyToScene(g_host.scene);
				g_dirty = false;
				Message("Reloaded");
			}
			else
				Message(voxels::GetLoadError());
			g_editor_key.clear();
		});

		g_editor_type = -1;
	}

	void Update()
	{
		if (g_palette != R_INVALID_ID)
		{
			const std::string key = PaletteKey();
			if (key != g_palette_key)
			{
				// Only the selection moved : the swatches paint it themselves.
				const bool same_types = key.substr(key.find(';')) == (g_palette_key.empty() ? std::string("\x01")
				                                                                           : g_palette_key.substr(g_palette_key.find(';')));
				if (!same_types)
					RebuildPalette();
				else
					for (Rid swatch : g_swatches)
						Rwidget_invalidate(swatch);   // the selection outline
				g_palette_key = key;
			}
			// Textures of the types : painted once loaded (FileTexture is asynchronous).
			static double last_texture_check = 0.0;
			if (g_texture_pending && Rget_time() - last_texture_check > 0.25)
			{
				last_texture_check = Rget_time();
				g_texture_pending = false;
				for (Rid swatch : g_swatches)
					if (Rwidget_is_valid(swatch))
						Rwidget_invalidate(swatch);
			}
		}

		if (g_editor != R_INVALID_ID)
		{
			const int selected = Selected();
			const voxels::VoxelType* t = selected > 0 ? voxels::GetType(static_cast<uint8_t>(selected)) : nullptr;
			const std::string key = t ? EditorKey(*t) : std::string();
			// Another type, or the type changed by something else than the fields.
			if (selected != g_editor_type || (key != g_editor_key && !g_filling && !input::Mouse(0) && !input::WantTextInput()))
				FillEditor();
		}

		// Saved once the edit is finished (not on every move of the picker).
		if (g_dirty && !input::Mouse(0) && Rget_time() - g_dirty_time > 0.6)
			Save();
	}
}
