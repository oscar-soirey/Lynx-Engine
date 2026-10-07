#include "WidgetEditor.h"

#include "PropertyWidgets.h"

#include "../widgets/Widget.h"
#include "../widgets/Panels.h"
#include "../widgets/CommonWidgets.h"
#include "../widgets/UserWidget.h"
#include "../core/RessourceManager.h"

#include <hrl/hrl.h>
#include <hrl/hrl_gl.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace lynx::editor::widget_editor
{
	namespace
	{
		// =====================================================================
		// Look
		// =====================================================================

		const ImU32 kCanvasBackground = IM_COL32(24, 24, 26, 255);
		const ImU32 kGrid = IM_COL32(255, 255, 255, 12);
		const ImU32 kDesignBackground = IM_COL32(40, 40, 44, 255);
		const ImU32 kDesignBorder = IM_COL32(120, 120, 130, 255);
		const ImU32 kOutline = IM_COL32(150, 150, 160, 70);
		const ImU32 kHoverOutline = IM_COL32(240, 185, 74, 140);
		const ImU32 kSelection = IM_COL32(240, 160, 40, 255);
		const ImU32 kHandle = IM_COL32(255, 255, 255, 255);
		const ImU32 kAnchor = IM_COL32(90, 200, 255, 255);

		struct Resolution
		{
			const char* label;
			float w;
			float h;
		};

		const Resolution kResolutions[] = {
			{ "1920 x 1080  (Full HD)", 1920.f, 1080.f },
			{ "1280 x 720  (HD)", 1280.f, 720.f },
			{ "2560 x 1440  (QHD)", 2560.f, 1440.f },
			{ "3840 x 2160  (4K)", 3840.f, 2160.f },
			{ "1280 x 800  (16:10)", 1280.f, 800.f },
			{ "1024 x 768  (4:3)", 1024.f, 768.f },
			{ "2560 x 1080  (21:9)", 2560.f, 1080.f },
			{ "1080 x 1920  (portrait)", 1080.f, 1920.f },
		};

		const char* const kPanelClasses[] = { "CanvasPanel", "VerticalBox", "HorizontalBox", "Overlay", "Border" };
		const char* const kCommonClasses[] = { "TextBlock", "Button", "Image", "Slider", "CheckBox", "ProgressBar", "Spacer" };

		constexpr const char* kPayloadClass = "LYNX_WIDGET_CLASS";
		constexpr const char* kPayloadAsset = "LYNX_WIDGET_ASSET";
		constexpr const char* kPayloadNode = "LYNX_WIDGET_NODE";


		// =====================================================================
		// Open documents
		// =====================================================================

		enum class DragMode { None, Move, Resize, Pan };

		struct Doc
		{
			fs::path path;
			std::unique_ptr<UserWidget> tree;

			std::string saved;          // content of the file (last load / save)
			std::string snapshot;       // current state (last commit)
			std::vector<std::string> undo;
			std::vector<std::string> redo;
			bool dirty = false;
			bool touched = false;       // edited this frame : commit when idle

			bool open = true;
			bool focus_next = false;
			bool ask_close = false;

			fs::file_time_type write_time{};
			bool changed_on_disk = false;
			double disk_check_time = 0.0;

			Widget* selected = nullptr;
			Widget* hovered = nullptr;
			bool focus_name = false;
			char name_buffer[128] = {};
			Widget* name_buffer_owner = nullptr;
			char class_buffer[128] = {};

			// Designer view
			float zoom = 0.5f;          // screen pixels per renderer pixel
			ImVec2 pan{ 0.f, 0.f };
			bool fit_pending = true;
			bool snap = true;
			float grid = 10.f;
			bool outlines = true;

			DragMode drag = DragMode::None;
			int handle = -1;            // 0..7 : TL T TR R BR B BL L
			WidgetRect drag_rect{};
			ImVec2 drag_mouse{};

			float left_width = 250.f;
			float right_width = 330.f;
			float palette_ratio = 0.42f;
			char palette_filter[64] = {};

			std::string message;        // last error (bad drop...)
			double message_time = 0.0;

			// Changes of the tree asked while it is drawn (drops, menus) : done
			// at the end of the frame, never in the middle of a traversal.
			std::vector<std::function<void()>> pending;
		};

		void Later(Doc& doc, std::function<void()> action)
		{
			doc.pending.push_back(std::move(action));
		}

		std::vector<std::unique_ptr<Doc>> g_docs;
		std::function<bool(std::string&)> g_asset_drawer;

		std::vector<std::string> g_user_widget_assets;   // relative to assets/
		double g_user_widget_scan_time = -100.0;


		// =====================================================================
		// Small helpers
		// =====================================================================

		fs::path AssetsRoot()
		{
			std::error_code ec;
			return fs::weakly_canonical(fs::current_path(ec) / "assets", ec);
		}

		// "ui/Menu.widget" for a file in assets/, "" otherwise.
		std::string AssetPathOf(const fs::path& file)
		{
			std::error_code ec;
			const fs::path root = AssetsRoot();
			const fs::path absolute = fs::weakly_canonical(file, ec);
			const fs::path relative = fs::relative(absolute, root, ec);
			if (ec || relative.empty() || *relative.begin() == "..")
				return {};
			return relative.generic_string();
		}

		std::string PrettyName(const std::string& name)
		{
			std::string out;
			bool upper = true;
			for (char c : name)
			{
				if (c == '_')
				{
					out += ' ';
					upper = true;
					continue;
				}
				out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
				upper = false;
			}
			return out;
		}

		std::string ClassLabel(const Widget& widget)
		{
			return widget.GetTypeName();
		}

		void ShowMessage(Doc& doc, const std::string& text)
		{
			doc.message = text;
			doc.message_time = ImGui::GetTime();
			std::cerr << "[WIDGET EDITOR] " << text << "\n";
		}

		bool InputString(const char* id, std::string& value, ImGuiInputTextFlags flags = 0, bool multiline = false)
		{
			char buffer[2048];
			std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
			bool edited = false;
			if (multiline)
			{
				const float height = ImGui::GetTextLineHeight() * 3.f + ImGui::GetStyle().FramePadding.y * 2.f;
				edited = ImGui::InputTextMultiline(id, buffer, sizeof(buffer), ImVec2(-FLT_MIN, height), flags);
			}
			else
			{
				edited = ImGui::InputText(id, buffer, sizeof(buffer), flags);
			}
			if (edited)
				value = buffer;
			return edited;
		}

		ImTextureID TextureFor(const std::string& path)
		{
			if (path.empty())
				return ImTextureID{};
			const uint32_t texture = RessourceTex(path.c_str());
			if (texture == HRL_INVALID_ID)
				return ImTextureID{};
			const unsigned int gl = HRL_GL_GetTextureGL_ID(texture);
			return gl ? static_cast<ImTextureID>(static_cast<intptr_t>(gl)) : ImTextureID{};
		}

		ImU32 ToColor(const vec4& c, float opacity)
		{
			return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * opacity));
		}

		void ScanUserWidgets()
		{
			if (ImGui::GetTime() - g_user_widget_scan_time < 2.0)
				return;
			g_user_widget_scan_time = ImGui::GetTime();
			g_user_widget_assets.clear();

			std::error_code ec;
			const fs::path root = AssetsRoot();
			for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
			     !ec && it != end; it.increment(ec))
			{
				std::error_code e;
				if (it->is_regular_file(e) && it->path().extension() == ".widget")
					g_user_widget_assets.push_back(fs::relative(it->path(), root, e).generic_string());
			}
			std::sort(g_user_widget_assets.begin(), g_user_widget_assets.end());
		}


		// =====================================================================
		// Tree helpers
		// =====================================================================

		PanelWidget* AsPanel(Widget* widget)
		{
			return dynamic_cast<PanelWidget*>(widget);
		}

		std::vector<Widget*> ChildrenOf(Widget& widget)
		{
			std::vector<Widget*> out;
			if (auto* panel = AsPanel(&widget))
			{
				for (int i = 0; i < panel->GetChildCount(); ++i)
					out.push_back(panel->GetChildAt(i));
			}
			return out;
		}

		// Drawing order : canvas children by z_order (stable).
		std::vector<Widget*> DrawOrder(Widget& widget)
		{
			std::vector<Widget*> children = ChildrenOf(widget);
			if (dynamic_cast<CanvasPanel*>(&widget))
			{
				std::stable_sort(children.begin(), children.end(), [](Widget* a, Widget* b)
				{
					return a->GetSlotAs<CanvasPanelSlot>()->z_order < b->GetSlotAs<CanvasPanelSlot>()->z_order;
				});
			}
			return children;
		}

		bool IsHiddenInDesigner(const Widget& w)
		{
			const EVisibility v = w.GetVisibility();
			return v == EVisibility::Collapsed || v == EVisibility::Hidden;
		}

		// `ancestor` contains `widget` (or is it).
		bool Contains(Widget* ancestor, Widget* widget)
		{
			for (Widget* w = widget; w; w = w->GetParent())
			{
				if (w == ancestor)
					return true;
			}
			return false;
		}

		// Index path from the root (survives undo / reload).
		std::vector<int> PathOf(Widget* widget)
		{
			std::vector<int> path;
			for (Widget* w = widget; w && w->GetParent(); w = w->GetParent())
				path.insert(path.begin(), w->GetParent()->GetChildIndex(w));
			return path;
		}

		Widget* FromPath(Doc& doc, const std::vector<int>& path)
		{
			Widget* w = doc.tree->GetRoot();
			for (int index : path)
			{
				auto* panel = AsPanel(w);
				if (!panel || index < 0 || index >= panel->GetChildCount())
					return w;
				w = panel->GetChildAt(index);
			}
			return w;
		}

		void CollectNames(Widget& widget, std::set<std::string>& names, const Widget* skip)
		{
			if (&widget != skip)
				names.insert(widget.GetName());
			for (Widget* child : ChildrenOf(widget))
				CollectNames(*child, names, skip);
		}

		std::string UniqueName(Doc& doc, std::string base, const Widget* skip)
		{
			std::set<std::string> names;
			if (doc.tree->GetRoot())
				CollectNames(*doc.tree->GetRoot(), names, skip);

			if (base.empty())
				base = "Widget";
			if (!names.count(base))
				return base;

			// "Button_3" -> base "Button"
			const size_t underscore = base.find_last_of('_');
			if (underscore != std::string::npos && underscore + 1 < base.size() &&
			    std::all_of(base.begin() + static_cast<long>(underscore) + 1, base.end(),
			                [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
				base = base.substr(0, underscore);

			for (int i = 1;; ++i)
			{
				const std::string candidate = base + "_" + std::to_string(i);
				if (!names.count(candidate))
					return candidate;
			}
		}

		// Every widget of `subtree` gets a name not used elsewhere.
		void MakeNamesUnique(Doc& doc, Widget& subtree)
		{
			std::vector<Widget*> list;
			std::function<void(Widget&)> collect = [&](Widget& w)
			{
				list.push_back(&w);
				for (Widget* child : ChildrenOf(w))
					collect(*child);
			};
			collect(subtree);

			for (Widget* w : list)
			{
				std::set<std::string> names;
				CollectNames(*doc.tree->GetRoot(), names, w);
				const std::string base = w->GetName().empty() ? ClassLabel(*w) : w->GetName();
				if (w->GetName().empty() || names.count(w->GetName()))
					w->SetName(UniqueName(doc, base, w));
			}
		}

		// Deepest visible widget under `p` (slate units). Nested UserWidgets
		// are one block.
		Widget* HitTest(Widget& widget, ImVec2 p)
		{
			if (IsHiddenInDesigner(widget))
				return nullptr;

			const std::vector<Widget*> children = DrawOrder(widget);
			for (auto it = children.rbegin(); it != children.rend(); ++it)
			{
				if (Widget* hit = HitTest(**it, p))
					return hit;
			}

			return widget.GetGeometry().Contains(p.x, p.y) ? &widget : nullptr;
		}

		// Panel that receives a drop at `p` : the deepest one that has room.
		PanelWidget* DropTargetAt(Doc& doc, ImVec2 p)
		{
			Widget* hit = doc.tree->GetRoot() ? HitTest(*doc.tree->GetRoot(), p) : nullptr;
			for (Widget* w = hit; w; w = w->GetParent())
			{
				if (auto* panel = AsPanel(w); panel && panel->CanAddChild())
					return panel;
			}
			return nullptr;
		}


		// =====================================================================
		// Canvas slot <-> rectangle
		// =====================================================================

		// Offsets giving `rect` (slate units) for the anchors of `slot`.
		void SetCanvasRect(CanvasPanelSlot& slot, const WidgetRect& panel, const WidgetRect& rect)
		{
			auto axis = [](float panel_start, float panel_size, float amin, float amax, float align,
			               float start, float size, float& offset_start, float& offset_end)
			{
				if (amin == amax)
				{
					offset_end = size;
					offset_start = start - (panel_start + amin * panel_size) + align * size;
				}
				else
				{
					offset_start = start - (panel_start + amin * panel_size);
					offset_end = (panel_start + amax * panel_size) - (start + size);
				}
			};

			axis(panel.x, panel.w, slot.anchor_min.x, slot.anchor_max.x, slot.alignment.x, rect.x, rect.w,
			     slot.offsets.x, slot.offsets.z);
			axis(panel.y, panel.h, slot.anchor_min.y, slot.anchor_max.y, slot.alignment.y, rect.y, rect.h,
			     slot.offsets.y, slot.offsets.w);
		}

		float Snap(const Doc& doc, float v)
		{
			if (!doc.snap || doc.grid <= 0.f)
				return v;
			return std::round(v / doc.grid) * doc.grid;
		}


		// =====================================================================
		// Load / save / undo
		// =====================================================================

		void Load(Doc& doc)
		{
			doc.tree = std::make_unique<UserWidget>();
			std::error_code ec;

			if (!doc.tree->LoadFromFile(doc.path.string()))
				ShowMessage(doc, "Could not read " + doc.path.filename().string() + " (empty widget)");

			doc.saved = doc.tree->SaveToString();
			doc.snapshot = doc.saved;
			doc.undo.clear();
			doc.redo.clear();
			doc.dirty = false;
			doc.selected = nullptr;
			doc.hovered = nullptr;
			doc.write_time = fs::last_write_time(doc.path, ec);
			doc.changed_on_disk = false;
			std::snprintf(doc.class_buffer, sizeof(doc.class_buffer), "%s", doc.tree->GetClassName().c_str());
		}

		bool Save(Doc& doc)
		{
			const std::string text = doc.tree->SaveToString();
			if (!doc.tree->SaveToFile(doc.path.string()))
			{
				ShowMessage(doc, "Could not write " + doc.path.string());
				return false;
			}
			doc.saved = text;
			doc.snapshot = text;
			doc.dirty = false;

			std::error_code ec;
			doc.write_time = fs::last_write_time(doc.path, ec);
			doc.changed_on_disk = false;
			std::cout << "[WIDGET EDITOR] Saved " << doc.path.string() << "\n";
			return true;
		}

		// The edits of this frame become one undo step.
		void Commit(Doc& doc)
		{
			doc.touched = false;
			const std::string now = doc.tree->SaveToString();
			if (now == doc.snapshot)
				return;

			doc.undo.push_back(doc.snapshot);
			if (doc.undo.size() > 200)
				doc.undo.erase(doc.undo.begin());
			doc.redo.clear();
			doc.snapshot = now;
			doc.dirty = now != doc.saved;
		}

		void Restore(Doc& doc, const std::string& state)
		{
			const std::vector<int> selection = doc.selected ? PathOf(doc.selected) : std::vector<int>{};
			const bool had_selection = doc.selected != nullptr;

			doc.tree->LoadFromString(state);
			doc.snapshot = state;
			doc.dirty = state != doc.saved;
			doc.hovered = nullptr;
			doc.name_buffer_owner = nullptr;
			doc.selected = had_selection && doc.tree->GetRoot() ? FromPath(doc, selection) : nullptr;
			std::snprintf(doc.class_buffer, sizeof(doc.class_buffer), "%s", doc.tree->GetClassName().c_str());
		}

		void Undo(Doc& doc)
		{
			Commit(doc);
			if (doc.undo.empty())
				return;
			doc.redo.push_back(doc.snapshot);
			const std::string state = doc.undo.back();
			doc.undo.pop_back();
			Restore(doc, state);
		}

		void Redo(Doc& doc)
		{
			if (doc.redo.empty())
				return;
			doc.undo.push_back(doc.snapshot);
			const std::string state = doc.redo.back();
			doc.redo.pop_back();
			Restore(doc, state);
		}


		// =====================================================================
		// Edits
		// =====================================================================

		std::unique_ptr<Widget> MakeWidget(Doc& doc, const std::string& class_name, const std::string& asset)
		{
			if (!asset.empty())
			{
				if (asset == AssetPathOf(doc.path))
				{
					ShowMessage(doc, "A widget can not contain itself");
					return nullptr;
				}
				auto user = std::make_unique<UserWidget>();
				user->asset = asset;
				user->LoadFromAsset(asset);
				user->SetName(fs::path(asset).stem().string());
				return user;
			}

			std::unique_ptr<Widget> widget = ui::NewWidget(class_name);
			if (!widget)
			{
				ShowMessage(doc, "Unknown widget class " + class_name);
				return nullptr;
			}
			widget->SetName(class_name);
			return widget;
		}

		// Adds `widget` to `parent` (nullptr : becomes the root). In a canvas,
		// at `canvas_point` (slate units, nullptr : top-left of the panel).
		Widget* AddWidget(Doc& doc, std::unique_ptr<Widget> widget, PanelWidget* parent, int index,
		                  const ImVec2* canvas_point)
		{
			if (!widget)
				return nullptr;

			Widget* raw = widget.get();

			if (!parent)
			{
				if (doc.tree->GetRoot())
				{
					ShowMessage(doc, "There is already a root : drop into a panel");
					return nullptr;
				}
				doc.tree->SetRoot(std::move(widget));
			}
			else
			{
				if (!parent->CanAddChild())
				{
					ShowMessage(doc, ClassLabel(*parent) + " can only have one child");
					return nullptr;
				}
				if (!parent->InsertChildAt(index < 0 ? parent->GetChildCount() : index, std::move(widget)))
					return nullptr;

				// Canvas : desired size, at the drop point.
				if (auto* slot = raw->GetSlotAs<CanvasPanelSlot>())
				{
					const vec2 desired = raw->GetDesiredSize();
					slot->offsets.z = std::max(desired.x, 40.f);
					slot->offsets.w = std::max(desired.y, 20.f);
					if (canvas_point)
					{
						const WidgetRect& panel = parent->GetGeometry();
						slot->offsets.x = Snap(doc, canvas_point->x - panel.x);
						slot->offsets.y = Snap(doc, canvas_point->y - panel.y);
					}
				}
			}

			MakeNamesUnique(doc, *raw);
			doc.selected = raw;
			doc.touched = true;
			return raw;
		}

		void DeleteWidget(Doc& doc, Widget* widget)
		{
			if (!widget)
				return;
			if (doc.selected && Contains(widget, doc.selected))
				doc.selected = widget->GetParent();
			if (doc.hovered && Contains(widget, doc.hovered))
				doc.hovered = nullptr;
			doc.name_buffer_owner = nullptr;

			if (widget == doc.tree->GetRoot())
				doc.tree->TakeRoot();
			else
				widget->RemoveFromParent();
			doc.touched = true;
		}

		// Moves `widget` into `parent` at `index` (slot kept when the panel
		// has the same kind of slot). Returns the widget at its new place.
		Widget* MoveWidget(Doc& doc, Widget* widget, PanelWidget* parent, int index)
		{
			if (!widget || !parent || Contains(widget, parent))
				return widget;

			if (widget->GetParent() == parent)
			{
				const int from = parent->GetChildIndex(widget);
				if (from < index)
					--index;
				if (from == index)
					return widget;
			}
			else if (!parent->CanAddChild())
			{
				ShowMessage(doc, ClassLabel(*parent) + " can only have one child");
				return widget;
			}

			const std::string copy = ui::CopyWidget(*widget);
			const bool was_root = widget == doc.tree->GetRoot();
			if (was_root)
				return widget;    // the root can not move into its own children

			widget->RemoveFromParent();
			Widget* moved = ui::PasteWidget(*parent, copy, index);
			doc.selected = moved;
			doc.hovered = nullptr;
			doc.name_buffer_owner = nullptr;
			doc.touched = true;
			return moved;
		}

		void Duplicate(Doc& doc, Widget* widget)
		{
			if (!widget || !widget->GetParent())
				return;
			PanelWidget* parent = widget->GetParent();
			if (!parent->CanAddChild())
			{
				ShowMessage(doc, ClassLabel(*parent) + " can only have one child");
				return;
			}

			Widget* copy = ui::PasteWidget(*parent, ui::CopyWidget(*widget), parent->GetChildIndex(widget) + 1);
			if (!copy)
				return;
			if (auto* slot = copy->GetSlotAs<CanvasPanelSlot>())
			{
				slot->offsets.x += 20.f;
				slot->offsets.y += 20.f;
			}
			MakeNamesUnique(doc, *copy);
			doc.selected = copy;
			doc.touched = true;
		}

		void CopySelection(Doc& doc)
		{
			if (doc.selected)
				ImGui::SetClipboardText(ui::CopyWidget(*doc.selected).c_str());
		}

		void Paste(Doc& doc)
		{
			const char* text = ImGui::GetClipboardText();
			if (!text || !std::strstr(text, "LynxWidgetClipboard"))
				return;

			// Into the selected panel, else next to the selected widget.
			PanelWidget* parent = AsPanel(doc.selected);
			int index = -1;
			if (!parent || !parent->CanAddChild())
			{
				parent = doc.selected ? doc.selected->GetParent() : AsPanel(doc.tree->GetRoot());
				index = (doc.selected && parent) ? parent->GetChildIndex(doc.selected) + 1 : -1;
			}
			if (!parent)
			{
				ShowMessage(doc, "Select a panel to paste into");
				return;
			}

			if (Widget* pasted = ui::PasteWidget(*parent, text, index))
			{
				MakeNamesUnique(doc, *pasted);
				doc.selected = pasted;
				doc.touched = true;
			}
			else
			{
				ShowMessage(doc, "Could not paste here (" + ClassLabel(*parent) + " is full ?)");
			}
		}

		// `widget` goes inside a new panel of `class_name`, at its place (a
		// canvas child : the panel takes its slot, the widget fills the panel).
		void WrapWith(Doc& doc, Widget* widget, const char* class_name)
		{
			if (!widget)
				return;

			std::unique_ptr<Widget> wrapper = ui::NewWidget(class_name);
			auto* panel = AsPanel(wrapper.get());
			if (!panel)
				return;
			wrapper->SetName(class_name);

			const std::string copy = ui::CopyWidget(*widget);

			if (widget == doc.tree->GetRoot())
			{
				doc.tree->TakeRoot();
				doc.tree->SetRoot(std::move(wrapper));
			}
			else
			{
				PanelWidget* parent = widget->GetParent();
				const int index = parent->GetChildIndex(widget);

				// Place in a canvas : kept for the wrapper.
				std::unique_ptr<CanvasPanelSlot> old_slot;
				if (auto* s = widget->GetSlotAs<CanvasPanelSlot>())
				{
					old_slot = std::make_unique<CanvasPanelSlot>();
					old_slot->anchor_min = s->anchor_min;
					old_slot->anchor_max = s->anchor_max;
					old_slot->offsets = s->offsets;
					old_slot->alignment = s->alignment;
					old_slot->auto_size = s->auto_size;
					old_slot->z_order = s->z_order;
				}

				widget->RemoveFromParent();
				parent->InsertChildAt(index, std::move(wrapper));

				if (auto* s = panel->GetSlotAs<CanvasPanelSlot>(); s && old_slot)
				{
					s->anchor_min = old_slot->anchor_min;
					s->anchor_max = old_slot->anchor_max;
					s->offsets = old_slot->offsets;
					s->alignment = old_slot->alignment;
					s->auto_size = old_slot->auto_size;
					s->z_order = old_slot->z_order;
				}
			}

			// The widget fills its new panel.
			if (Widget* moved = ui::PasteWidget(*panel, copy))
			{
				if (auto* inner = moved->GetSlotAs<CanvasPanelSlot>())
				{
					inner->anchor_min = vec2(0.f, 0.f);
					inner->anchor_max = vec2(1.f, 1.f);
					inner->alignment = vec2(0.f, 0.f);
					inner->offsets = vec4(0.f, 0.f, 0.f, 0.f);
				}
			}

			MakeNamesUnique(doc, *panel);
			doc.selected = panel;
			doc.hovered = nullptr;
			doc.name_buffer_owner = nullptr;
			doc.touched = true;
		}


		// =====================================================================
		// Designer drawing
		// =====================================================================

		struct View
		{
			ImVec2 origin;    // screen position of slate (0, 0)
			float ppu;        // screen pixels per slate unit

			ImVec2 ToScreen(float x, float y) const { return ImVec2(origin.x + x * ppu, origin.y + y * ppu); }
			ImVec2 ToSlate(ImVec2 p) const { return ImVec2((p.x - origin.x) / ppu, (p.y - origin.y) / ppu); }
		};

		void DrawText(ImDrawList* draw, const View& view, const WidgetRect& r, const std::string& text,
		              float size, ImU32 color, bool centered)
		{
			const float px = size * view.ppu;
			if (px < 2.f || text.empty())
				return;

			ImFont* font = ImGui::GetFont();
			const ImVec2 min = view.ToScreen(r.x, r.y);
			const ImVec2 max = view.ToScreen(r.x + r.w, r.y + r.h);
			ImVec2 pos = min;

			if (centered)
			{
				const ImVec2 text_size = font->CalcTextSizeA(px, FLT_MAX, 0.f, text.c_str());
				pos.x = min.x + (max.x - min.x - text_size.x) * 0.5f;
				pos.y = min.y + (max.y - min.y - text_size.y) * 0.5f;
			}

			// Not clipped by the widget box : a long text overflows it, like in game.
			draw->AddText(font, px, pos, color, text.c_str());
		}

		void DrawBrush(ImDrawList* draw, const ImVec2& a, const ImVec2& b, const std::string& texture, ImU32 color)
		{
			const ImTextureID id = TextureFor(texture);
			if (id)
				draw->AddImage(id, a, b, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f), color);
			else
				draw->AddRectFilled(a, b, color);
		}

		void DrawWidget(ImDrawList* draw, const View& view, Doc& doc, Widget& widget, float opacity)
		{
			if (IsHiddenInDesigner(widget))
				return;

			opacity *= std::clamp(widget.render_opacity, 0.f, 1.f);
			const WidgetRect& r = widget.GetGeometry();
			const ImVec2 a = view.ToScreen(r.x, r.y);
			const ImVec2 b = view.ToScreen(r.x + r.w, r.y + r.h);
			const float s = view.ppu;

			if (auto* border = dynamic_cast<Border*>(&widget))
			{
				DrawBrush(draw, a, b, border->brush_texture, ToColor(border->brush_color, opacity));
			}
			else if (auto* image = dynamic_cast<lynx::Image*>(&widget))
			{
				DrawBrush(draw, a, b, image->texture, ToColor(image->tint, opacity));
			}
			else if (auto* button = dynamic_cast<lynx::Button*>(&widget))
			{
				const bool hovered = doc.hovered == button;
				DrawBrush(draw, a, b, hovered ? button->hovered_texture : button->normal_texture,
				          ToColor(hovered ? button->hovered_color : button->normal_color, opacity));
				DrawText(draw, view, r, button->text, button->font_size, ToColor(button->text_color, opacity), true);
			}
			else if (auto* text = dynamic_cast<TextBlock*>(&widget))
			{
				DrawText(draw, view, r, text->text, text->font_size, ToColor(text->color, opacity), false);
			}
			else if (auto* bar = dynamic_cast<ProgressBar*>(&widget))
			{
				draw->AddRectFilled(a, b, ToColor(bar->background_color, opacity));
				const float p = std::clamp(bar->percent, 0.f, 1.f);
				draw->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * p, b.y), ToColor(bar->fill_color, opacity));
			}
			else if (auto* slider = dynamic_cast<lynx::Slider*>(&widget))
			{
				const bool vertical = slider->orientation == static_cast<int>(EOrientation::Vertical);
				const float range = slider->max_value - slider->min_value;
				const float t = range != 0.f ? std::clamp((slider->value - slider->min_value) / range, 0.f, 1.f) : 0.f;
				const float thickness = 6.f * s;
				if (vertical)
				{
					const float cx = (a.x + b.x) * 0.5f;
					const float y = b.y - (b.y - a.y) * t;
					draw->AddRectFilled(ImVec2(cx - thickness * 0.5f, a.y), ImVec2(cx + thickness * 0.5f, b.y), ToColor(slider->bar_color, opacity));
					draw->AddRectFilled(ImVec2(cx - thickness * 0.5f, y), ImVec2(cx + thickness * 0.5f, b.y), ToColor(slider->fill_color, opacity));
					draw->AddRectFilled(ImVec2(a.x, y - 4.f * s), ImVec2(b.x, y + 4.f * s), ToColor(slider->handle_color, opacity));
				}
				else
				{
					const float cy = (a.y + b.y) * 0.5f;
					const float x = a.x + (b.x - a.x) * t;
					draw->AddRectFilled(ImVec2(a.x, cy - thickness * 0.5f), ImVec2(b.x, cy + thickness * 0.5f), ToColor(slider->bar_color, opacity));
					draw->AddRectFilled(ImVec2(a.x, cy - thickness * 0.5f), ImVec2(x, cy + thickness * 0.5f), ToColor(slider->fill_color, opacity));
					draw->AddRectFilled(ImVec2(x - 4.f * s, a.y), ImVec2(x + 4.f * s, b.y), ToColor(slider->handle_color, opacity));
				}
			}
			else if (auto* check = dynamic_cast<lynx::CheckBox*>(&widget))
			{
				draw->AddRectFilled(a, b, ToColor(check->background_color, opacity));
				if (check->checked)
				{
					const float m = std::min(b.x - a.x, b.y - a.y) * 0.22f;
					draw->AddRectFilled(ImVec2(a.x + m, a.y + m), ImVec2(b.x - m, b.y - m), ToColor(check->checked_color, opacity));
				}
			}

			// Panels / spacers / nested widgets : an outline (option).
			const bool container = AsPanel(&widget) || dynamic_cast<UserWidget*>(&widget) || dynamic_cast<Spacer*>(&widget);
			if (doc.outlines && container && !dynamic_cast<Border*>(&widget))
				draw->AddRect(a, b, kOutline);

			// Nested UserWidget : its own tree (not selectable from here).
			if (auto* user = dynamic_cast<UserWidget*>(&widget))
			{
				if (Widget* root = user->GetRoot())
					DrawWidget(draw, view, doc, *root, opacity);
				return;
			}

			for (Widget* child : DrawOrder(widget))
				DrawWidget(draw, view, doc, *child, opacity);
		}

		// Handle rectangles of the selection (screen) : TL T TR R BR B BL L.
		void HandleRects(const ImVec2& a, const ImVec2& b, ImVec2 out[8])
		{
			const float mx = (a.x + b.x) * 0.5f;
			const float my = (a.y + b.y) * 0.5f;
			out[0] = a;                 out[1] = ImVec2(mx, a.y);  out[2] = ImVec2(b.x, a.y);
			out[3] = ImVec2(b.x, my);   out[4] = b;                out[5] = ImVec2(mx, b.y);
			out[6] = ImVec2(a.x, b.y);  out[7] = ImVec2(a.x, my);
		}

		// Anchors of a canvas child, drawn in its panel.
		void DrawAnchors(ImDrawList* draw, const View& view, const CanvasPanelSlot& slot, const WidgetRect& panel)
		{
			const ImVec2 min = view.ToScreen(panel.x + slot.anchor_min.x * panel.w, panel.y + slot.anchor_min.y * panel.h);
			const ImVec2 max = view.ToScreen(panel.x + slot.anchor_max.x * panel.w, panel.y + slot.anchor_max.y * panel.h);
			const float r = 6.f;

			if (min.x == max.x && min.y == max.y)
			{
				// Point : 4 petals.
				draw->AddTriangleFilled(ImVec2(min.x, min.y), ImVec2(min.x - r, min.y - r * 1.6f), ImVec2(min.x + r, min.y - r * 1.6f), kAnchor);
				draw->AddTriangleFilled(ImVec2(min.x, min.y), ImVec2(min.x - r, min.y + r * 1.6f), ImVec2(min.x + r, min.y + r * 1.6f), kAnchor);
				draw->AddTriangleFilled(ImVec2(min.x, min.y), ImVec2(min.x - r * 1.6f, min.y - r), ImVec2(min.x - r * 1.6f, min.y + r), kAnchor);
				draw->AddTriangleFilled(ImVec2(min.x, min.y), ImVec2(min.x + r * 1.6f, min.y - r), ImVec2(min.x + r * 1.6f, min.y + r), kAnchor);
			}
			else
			{
				draw->AddRect(min, max, kAnchor, 0.f, 0, 1.5f);
				draw->AddCircleFilled(min, r * 0.6f, kAnchor);
				draw->AddCircleFilled(max, r * 0.6f, kAnchor);
				draw->AddCircleFilled(ImVec2(min.x, max.y), r * 0.6f, kAnchor);
				draw->AddCircleFilled(ImVec2(max.x, min.y), r * 0.6f, kAnchor);
			}
		}


		// =====================================================================
		// Panels of the editor window
		// =====================================================================

		bool AcceptWidgetPayloads(Doc& doc, std::string& class_name, std::string& asset, Widget*& node)
		{
			bool accepted = false;
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadClass))
			{
				class_name.assign(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
				class_name = class_name.c_str();
				accepted = true;
			}
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadAsset))
			{
				asset.assign(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
				asset = asset.c_str();
				accepted = true;
			}
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadNode))
			{
				std::memcpy(&node, p->Data, sizeof(Widget*));
				accepted = true;

				// From another Widget Editor : copied, not moved.
				if (!doc.tree->GetRoot() || !Contains(doc.tree->GetRoot(), node))
				{
					bool found = false;
					for (const auto& other : g_docs)
						if (other->tree->GetRoot() && Contains(other->tree->GetRoot(), node))
							found = true;
					if (found)
					{
						const std::string copy = ui::CopyWidget(*node);
						node = nullptr;
						accepted = false;
						if (PanelWidget* root = AsPanel(doc.tree->GetRoot()))
							Later(doc, [&doc, root, copy] {
								if (Widget* pasted = ui::PasteWidget(*root, copy))
								{
									MakeNamesUnique(doc, *pasted);
									doc.selected = pasted;
									doc.touched = true;
								}
							});
					}
					else
					{
						node = nullptr;
						accepted = false;
					}
				}
			}
			return accepted;
		}

		void PaletteItem(Doc& doc, const char* label, const char* class_name, const std::string& asset)
		{
			ImGui::PushID(label);
			ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick);

			if (ImGui::BeginDragDropSource())
			{
				if (asset.empty())
					ImGui::SetDragDropPayload(kPayloadClass, class_name, std::strlen(class_name) + 1);
				else
					ImGui::SetDragDropPayload(kPayloadAsset, asset.c_str(), asset.size() + 1);
				ImGui::Text("+ %s", label);
				ImGui::EndDragDropSource();
			}

			// Double-click : into the selected panel (or next to the selection).
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				PanelWidget* parent = AsPanel(doc.selected);
				int index = -1;
				if (!parent || !parent->CanAddChild())
				{
					parent = doc.selected ? doc.selected->GetParent() : AsPanel(doc.tree->GetRoot());
					index = (parent && doc.selected) ? parent->GetChildIndex(doc.selected) + 1 : -1;
				}
				if (!doc.tree->GetRoot())
					parent = nullptr;
				const std::string cls = class_name ? class_name : "";
				Later(doc, [&doc, cls, asset, parent, index] {
					AddWidget(doc, MakeWidget(doc, cls, asset), parent, index, nullptr);
				});
			}

			if (ImGui::IsItemHovered() && asset.empty())
				ImGui::SetTooltip("Drag onto the Designer or the Hierarchy (double-click : into the selected panel)");
			ImGui::PopID();
		}

		bool MatchesFilter(const Doc& doc, const std::string& text)
		{
			if (!doc.palette_filter[0])
				return true;
			std::string a = text, b = doc.palette_filter;
			std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a.find(b) != std::string::npos;
		}

		void DrawPalette(Doc& doc)
		{
			ImGui::TextUnformatted("Palette");
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", "Search", doc.palette_filter, sizeof(doc.palette_filter));

			if (ImGui::CollapsingHeader("Panel", ImGuiTreeNodeFlags_DefaultOpen))
				for (const char* c : kPanelClasses)
					if (MatchesFilter(doc, c))
						PaletteItem(doc, c, c, {});

			if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen))
				for (const char* c : kCommonClasses)
					if (MatchesFilter(doc, c))
						PaletteItem(doc, c, c, {});

			ScanUserWidgets();
			if (ImGui::CollapsingHeader("User Widgets", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const std::string self = AssetPathOf(doc.path);
				bool any = false;
				for (const std::string& asset : g_user_widget_assets)
				{
					if (asset == self || !MatchesFilter(doc, asset))
						continue;
					PaletteItem(doc, asset.c_str(), "UserWidget", asset);
					any = true;
				}
				if (!any)
					ImGui::TextDisabled("(other .widget assets)");
			}
		}

		void HierarchyContextMenu(Doc& doc, Widget* widget)
		{
			if (ImGui::MenuItem("Rename", "F2"))
			{
				doc.selected = widget;
				doc.focus_name = true;
			}
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, widget->GetParent() != nullptr))
				Later(doc, [&doc, widget] { Duplicate(doc, widget); });
			if (ImGui::MenuItem("Copy", "Ctrl+C"))
			{
				doc.selected = widget;
				CopySelection(doc);
			}
			if (ImGui::MenuItem("Paste", "Ctrl+V"))
			{
				doc.selected = widget;
				Later(doc, [&doc] { Paste(doc); });
			}
			if (ImGui::BeginMenu("Wrap With"))
			{
				for (const char* c : kPanelClasses)
					if (ImGui::MenuItem(c))
						Later(doc, [&doc, widget, c] { WrapWith(doc, widget, c); });
				ImGui::EndMenu();
			}

			if (PanelWidget* parent = widget->GetParent())
			{
				const int index = parent->GetChildIndex(widget);
				if (ImGui::MenuItem("Move Up", nullptr, false, index > 0))
					Later(doc, [&doc, widget, parent, index] { MoveWidget(doc, widget, parent, index - 1); });
				if (ImGui::MenuItem("Move Down", nullptr, false, index + 1 < parent->GetChildCount()))
					Later(doc, [&doc, widget, parent, index] { MoveWidget(doc, widget, parent, index + 2); });
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Delete", "Del"))
				Later(doc, [&doc, widget] { DeleteWidget(doc, widget); });
		}

		// Drop on a node : into it (panel), else before it in its parent.
		void HierarchyDrop(Doc& doc, Widget* target)
		{
			if (!ImGui::BeginDragDropTarget())
				return;

			std::string class_name, asset;
			Widget* node = nullptr;
			if (AcceptWidgetPayloads(doc, class_name, asset, node))
			{
				PanelWidget* parent = AsPanel(target);
				int index = -1;
				if (!parent || (!parent->CanAddChild() && !(node && node->GetParent() == parent)))
				{
					parent = target->GetParent();
					index = parent ? parent->GetChildIndex(target) : -1;
				}

				if (node)
				{
					if (parent && !Contains(node, parent))
						Later(doc, [&doc, node, parent, index] {
							MoveWidget(doc, node, parent, index < 0 ? parent->GetChildCount() : index);
						});
					else
						ShowMessage(doc, "A widget can not go inside itself");
				}
				else if (parent)
				{
					Later(doc, [&doc, class_name, asset, parent, index] {
						AddWidget(doc, MakeWidget(doc, class_name, asset), parent, index, nullptr);
					});
				}
			}
			ImGui::EndDragDropTarget();
		}

		void HierarchyNode(Doc& doc, Widget& widget)
		{
			ImGui::PushID(&widget);

			const std::vector<Widget*> children = ChildrenOf(widget);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
			                           ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
			if (children.empty())
				flags |= ImGuiTreeNodeFlags_Leaf;
			if (doc.selected == &widget)
				flags |= ImGuiTreeNodeFlags_Selected;

			const bool hidden = IsHiddenInDesigner(widget);
			if (hidden)
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			const bool open = ImGui::TreeNodeEx("##node", flags, "%s", widget.GetName().empty() ? "(unnamed)" : widget.GetName().c_str());
			if (hidden)
				ImGui::PopStyleColor();

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
				doc.selected = &widget;
			if (ImGui::IsItemHovered())
				doc.hovered = &widget;

			if (ImGui::BeginDragDropSource())
			{
				Widget* pointer = &widget;
				ImGui::SetDragDropPayload(kPayloadNode, &pointer, sizeof(pointer));
				ImGui::Text("%s", widget.GetName().c_str());
				ImGui::EndDragDropSource();
			}
			HierarchyDrop(doc, &widget);

			if (ImGui::BeginPopupContextItem("##context"))
			{
				doc.selected = &widget;
				HierarchyContextMenu(doc, &widget);
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			ImGui::TextDisabled("%s%s", ClassLabel(widget).c_str(), hidden ? "  (hidden)" : "");

			if (open)
			{
				for (Widget* child : children)
					HierarchyNode(doc, *child);
				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		void DrawHierarchy(Doc& doc)
		{
			ImGui::TextUnformatted("Hierarchy");
			ImGui::Separator();

			if (Widget* root = doc.tree->GetRoot())
			{
				HierarchyNode(doc, *root);
			}
			else
			{
				ImGui::TextDisabled("Empty : drop a panel here");
				ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 60.f));
				if (ImGui::BeginDragDropTarget())
				{
					std::string class_name, asset;
					Widget* node = nullptr;
					if (AcceptWidgetPayloads(doc, class_name, asset, node) && !node)
						Later(doc, [&doc, class_name, asset] {
							AddWidget(doc, MakeWidget(doc, class_name, asset), nullptr, -1, nullptr);
						});
					ImGui::EndDragDropTarget();
				}
			}

			// Click in the empty space : no selection.
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
				doc.selected = nullptr;
		}


		// ---------------------------------------------------------------------
		// Details
		// ---------------------------------------------------------------------

		bool IsAssetField(const std::string& name)
		{
			return name == "asset" || name == "font" || name.find("texture") != std::string::npos;
		}

		bool IsColorField(const std::string& name)
		{
			return name.find("color") != std::string::npos || name == "tint";
		}

		// One reflected field. Returns true when it changed.
		bool PropertyRow(Doc& doc, Object& object, const std::string& name, const property& prop)
		{
			(void)doc;
			(void)object;
			const std::string label = PrettyName(name);
			ImGui::PushID(name.c_str());
			bool changed = false;

			std::visit([&](auto* ptr)
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return;

				if constexpr (std::is_same_v<T, int>)
				{
					property_widgets::RowLabel(label.c_str());
					const auto& names = ui::GetEnumNames(name);
					if (!names.empty())
					{
						const int current = std::clamp(*ptr, 0, static_cast<int>(names.size()) - 1);
						if (ImGui::BeginCombo("##v", names[static_cast<size_t>(current)].c_str()))
						{
							for (int i = 0; i < static_cast<int>(names.size()); ++i)
							{
								if (ImGui::Selectable(names[static_cast<size_t>(i)].c_str(), i == *ptr))
								{
									*ptr = i;
									changed = true;
								}
							}
							ImGui::EndCombo();
						}
					}
					else
					{
						changed = ImGui::DragInt("##v", ptr, 0.2f);
					}
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					property_widgets::RowLabel(label.c_str());
					const bool unit = name == "percent" || name == "render_opacity";
					changed = ImGui::DragFloat("##v", ptr, unit ? 0.005f : 0.5f, unit ? 0.f : 0.f, unit ? 1.f : 0.f, "%.3g");
				}
				else if constexpr (std::is_same_v<T, bool>)
				{
					property_widgets::RowLabel(label.c_str());
					changed = ImGui::Checkbox("##v", ptr);
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					property_widgets::RowLabel(label.c_str());
					if (IsAssetField(name) && g_asset_drawer)
						changed = g_asset_drawer(*ptr);
					else
						changed = InputString("##v", *ptr, 0, name == "text");
				}
				else if constexpr (std::is_same_v<T, vec2>)
				{
					float v[2] = { ptr->x, ptr->y };
					if (property_widgets::VectorRow(label.c_str(), v, 2, 0.5f, nullptr, "%.3g"))
					{
						*ptr = vec2(v[0], v[1]);
						changed = true;
					}
				}
				else if constexpr (std::is_same_v<T, vec3>)
				{
					float v[3] = { ptr->x, ptr->y, ptr->z };
					if (IsColorField(name))
					{
						property_widgets::RowLabel(label.c_str());
						changed = ImGui::ColorEdit3("##v", v, ImGuiColorEditFlags_Float);
					}
					else
					{
						changed = property_widgets::VectorRow(label.c_str(), v, 3, 0.5f, nullptr, "%.3g");
					}
					if (changed)
						*ptr = vec3(v[0], v[1], v[2]);
				}
				else if constexpr (std::is_same_v<T, vec4>)
				{
					float v[4] = { ptr->x, ptr->y, ptr->z, ptr->w };
					if (IsColorField(name))
					{
						property_widgets::RowLabel(label.c_str());
						changed = ImGui::ColorEdit4("##v", v, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
					}
					else
					{
						changed = property_widgets::VectorRow(label.c_str(), v, 4, 0.5f, nullptr, "%.3g");
						if (ImGui::IsItemHovered() && name.find("padding") != std::string::npos)
							ImGui::SetTooltip("Left, Top, Right, Bottom");
					}
					if (changed)
					{
						ptr->x = v[0];
						ptr->y = v[1];
						ptr->z = v[2];
						ptr->w = v[3];
					}
				}
			}, prop.property_member);

			ImGui::PopID();
			return changed;
		}

		// 4 x 4 anchor presets (like Unreal). Shift : alignment too ; Ctrl :
		// the widget moves to the anchors.
		struct AnchorPreset { float min_x, min_y, max_x, max_y; };
		const AnchorPreset kAnchorPresets[16] = {
			{ 0, 0, 0, 0 },     { 0.5f, 0, 0.5f, 0 },       { 1, 0, 1, 0 },       { 0, 0, 1, 0 },
			{ 0, 0.5f, 0, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f }, { 1, 0.5f, 1, 0.5f }, { 0, 0.5f, 1, 0.5f },
			{ 0, 1, 0, 1 },     { 0.5f, 1, 0.5f, 1 },       { 1, 1, 1, 1 },       { 0, 1, 1, 1 },
			{ 0, 0, 0, 1 },     { 0.5f, 0, 0.5f, 1 },       { 1, 0, 1, 1 },       { 0, 0, 1, 1 },
		};

		bool AnchorPresetButton(int index, const AnchorPreset& p, bool current)
		{
			ImGui::PushID(index);
			const float size = ImGui::GetFrameHeight() * 1.7f;
			const bool clicked = ImGui::InvisibleButton("##preset", ImVec2(size, size));
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 a = ImGui::GetItemRectMin();
			const ImVec2 b = ImGui::GetItemRectMax();
			const bool hovered = ImGui::IsItemHovered();

			draw->AddRectFilled(a, b, hovered ? IM_COL32(70, 70, 76, 255) : IM_COL32(45, 45, 50, 255));
			if (current)
				draw->AddRect(a, b, kSelection, 0.f, 0, 2.f);

			// Panel, then the anchored area.
			const float m = size * 0.18f;
			const ImVec2 pa(a.x + m, a.y + m), pb(b.x - m, b.y - m);
			draw->AddRect(pa, pb, IM_COL32(160, 160, 170, 255));
			const ImVec2 amin(pa.x + (pb.x - pa.x) * p.min_x, pa.y + (pb.y - pa.y) * p.min_y);
			const ImVec2 amax(pa.x + (pb.x - pa.x) * p.max_x, pa.y + (pb.y - pa.y) * p.max_y);
			const float t = 2.f;
			draw->AddRectFilled(ImVec2(amin.x - t, amin.y - t), ImVec2(std::max(amax.x, amin.x) + t, std::max(amax.y, amin.y) + t), kAnchor);
			ImGui::PopID();
			return clicked;
		}

		void CanvasSlotDetails(Doc& doc, Widget& widget, CanvasPanelSlot& slot)
		{
			const WidgetRect panel = widget.GetParent()->GetGeometry();

			// Anchors : presets.
			property_widgets::RowLabel("Anchors");
			if (ImGui::Button("Presets...", ImVec2(-FLT_MIN, 0.f)))
				ImGui::OpenPopup("##anchor_presets");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Shift : also the alignment (pivot)\nCtrl : also move the widget to the anchors");

			if (ImGui::BeginPopup("##anchor_presets"))
			{
				ImGui::TextDisabled("Shift : alignment too    Ctrl : position too");
				for (int i = 0; i < 16; ++i)
				{
					const AnchorPreset& p = kAnchorPresets[i];
					const bool current = slot.anchor_min.x == p.min_x && slot.anchor_min.y == p.min_y &&
					                     slot.anchor_max.x == p.max_x && slot.anchor_max.y == p.max_y;
					if (i % 4)
						ImGui::SameLine();
					if (AnchorPresetButton(i, p, current))
					{
						const ImGuiIO& io = ImGui::GetIO();
						const WidgetRect rect = widget.GetGeometry();

						slot.anchor_min = vec2(p.min_x, p.min_y);
						slot.anchor_max = vec2(p.max_x, p.max_y);
						if (io.KeyShift)
						{
							slot.alignment = vec2(p.min_x == p.max_x ? p.min_x : 0.f,
							                      p.min_y == p.max_y ? p.min_y : 0.f);
						}

						if (io.KeyCtrl)
						{
							// At the anchors : position 0, or no margin.
							slot.offsets.x = 0.f;
							slot.offsets.y = 0.f;
							if (p.min_x != p.max_x) slot.offsets.z = 0.f;
							if (p.min_y != p.max_y) slot.offsets.w = 0.f;
						}
						else
						{
							SetCanvasRect(slot, panel, rect);   // the widget does not move
						}
						doc.touched = true;
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndPopup();
			}

			float amin[2] = { slot.anchor_min.x, slot.anchor_min.y };
			float amax[2] = { slot.anchor_max.x, slot.anchor_max.y };
			if (property_widgets::VectorRow("Anchor Min", amin, 2, 0.01f, nullptr, "%.2f"))
			{
				slot.anchor_min = vec2(amin[0], amin[1]);
				doc.touched = true;
			}
			if (property_widgets::VectorRow("Anchor Max", amax, 2, 0.01f, nullptr, "%.2f"))
			{
				slot.anchor_max = vec2(amax[0], amax[1]);
				doc.touched = true;
			}

			// Offsets : position / size, or margins on a stretched axis.
			const bool stretch_x = slot.anchor_min.x != slot.anchor_max.x;
			const bool stretch_y = slot.anchor_min.y != slot.anchor_max.y;
			const char* first = stretch_x || stretch_y ? (stretch_x && stretch_y ? "Left / Top" : (stretch_x ? "Left / Pos Y" : "Pos X / Top")) : "Position";
			const char* second = stretch_x || stretch_y ? (stretch_x && stretch_y ? "Right / Bottom" : (stretch_x ? "Right / Size Y" : "Size X / Bottom")) : "Size";

			float position[2] = { slot.offsets.x, slot.offsets.y };
			float size[2] = { slot.offsets.z, slot.offsets.w };
			if (property_widgets::VectorRow(first, position, 2, 1.f, nullptr, "%.1f"))
			{
				slot.offsets.x = position[0];
				slot.offsets.y = position[1];
				doc.touched = true;
			}
			ImGui::BeginDisabled(slot.auto_size && !stretch_x && !stretch_y);
			if (property_widgets::VectorRow(second, size, 2, 1.f, nullptr, "%.1f"))
			{
				slot.offsets.z = size[0];
				slot.offsets.w = size[1];
				doc.touched = true;
			}
			ImGui::EndDisabled();

			float alignment[2] = { slot.alignment.x, slot.alignment.y };
			if (property_widgets::VectorRow("Alignment", alignment, 2, 0.01f, nullptr, "%.2f"))
			{
				slot.alignment = vec2(alignment[0], alignment[1]);
				doc.touched = true;
			}

			property_widgets::RowLabel("Size To Content");
			if (ImGui::Checkbox("##auto_size", &slot.auto_size))
				doc.touched = true;

			property_widgets::RowLabel("ZOrder");
			if (ImGui::DragInt("##z_order", &slot.z_order, 0.1f))
				doc.touched = true;
		}

		// C++ class for this widget (like the variables of a Widget Blueprint).
		std::string MakeCppClass(Doc& doc)
		{
			std::string class_name = doc.tree->GetClassName();
			if (class_name.empty())
				class_name = doc.path.stem().string();
			class_name.erase(std::remove_if(class_name.begin(), class_name.end(),
			                                [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '_'; }),
			                 class_name.end());
			if (class_name.empty() || std::isdigit(static_cast<unsigned char>(class_name[0])))
				class_name = "W" + class_name;

			std::vector<Widget*> named;
			if (doc.tree->GetRoot())
			{
				std::function<void(Widget&)> collect = [&](Widget& w)
				{
					if (&w != doc.tree->GetRoot() && !w.GetName().empty())
						named.push_back(&w);
					for (Widget* c : ChildrenOf(w))
						collect(*c);
				};
				collect(*doc.tree->GetRoot());
			}

			auto identifier = [](std::string s)
			{
				for (char& c : s)
					if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
						c = '_';
				if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0])))
					s = "_" + s;
				return s;
			};

			std::ostringstream out;
			out << "// " << doc.path.filename().string() << " : Widget Editor > Class = " << class_name << "\n"
			    << "// Register it in LYNX_LINK_MODULE : LYNX_MODULE_REGISTER(" << class_name << ");\n"
			    << "// Create it : lynx::CreateWidget(player, \"" << AssetPathOf(doc.path) << "\")->AddToViewport();\n"
			    << "class " << class_name << " : public lynx::UserWidget\n{\n"
			    << "protected:\n"
			    << "    void NativeConstruct() override\n    {\n";
			for (Widget* w : named)
				out << "        " << identifier(w->GetName()) << " = GetWidget<lynx::" << ClassLabel(*w) << ">(\"" << w->GetName() << "\");\n";
			for (Widget* w : named)
				if (dynamic_cast<lynx::Button*>(w))
					out << "\n        " << identifier(w->GetName()) << "->OnClicked.Subscribe([this] {\n        });\n";
			out << "    }\n\n"
			    << "    void NativeTick(float dt) override\n    {\n    }\n\n"
			    << "private:\n";
			for (Widget* w : named)
				out << "    lynx::" << ClassLabel(*w) << "* " << identifier(w->GetName()) << " = nullptr;\n";
			out << "};\n";
			return out.str();
		}

		// JavaScript class for this widget (Widget Editor > Class = its name).
		std::string MakeJsClass(Doc& doc)
		{
			std::string class_name = doc.tree->GetClassName();
			if (class_name.empty())
				class_name = doc.path.stem().string();
			class_name.erase(std::remove_if(class_name.begin(), class_name.end(),
			                                [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '_'; }),
			                 class_name.end());
			if (class_name.empty() || std::isdigit(static_cast<unsigned char>(class_name[0])))
				class_name = "W" + class_name;

			std::vector<Widget*> named;
			if (doc.tree->GetRoot())
			{
				std::function<void(Widget&)> collect = [&](Widget& w)
				{
					if (&w != doc.tree->GetRoot() && !w.GetName().empty())
						named.push_back(&w);
					for (Widget* c : ChildrenOf(w))
						collect(*c);
				};
				collect(*doc.tree->GetRoot());
			}

			std::ostringstream out;
			out << "// " << doc.path.filename().string() << " : Widget Editor > Class = " << class_name << "\n"
			    << "// Anywhere in assets/ (loaded with the other classes).\n"
			    << "// UI.create(\"" << AssetPathOf(doc.path) << "\", player).addToViewport();\n"
			    << "class " << class_name << " extends UserWidget {\n"
			    << "    Construct() {\n";
			for (Widget* w : named)
			{
				if (dynamic_cast<lynx::Button*>(w))
					out << "        this.find(\"" << w->GetName() << "\").onClicked(() => {\n        });\n";
				else if (dynamic_cast<lynx::Slider*>(w))
					out << "        this.find(\"" << w->GetName() << "\").onValueChanged(value => {\n        });\n";
				else if (dynamic_cast<lynx::CheckBox*>(w))
					out << "        this.find(\"" << w->GetName() << "\").onCheckStateChanged(checked => {\n        });\n";
			}
			out << "    }\n\n"
			    << "    Tick(dt) {\n    }\n\n"
			    << "    Destruct() {\n    }\n"
			    << "}\n";
			return out.str();
		}

		void DrawDetails(Doc& doc)
		{
			ImGui::TextUnformatted("Details");
			ImGui::Separator();

			Widget* widget = doc.selected;

			if (!widget)
			{
				// The asset itself.
				ImGui::TextDisabled("%s", doc.path.filename().string().c_str());
				if (property_widgets::BeginTable("##asset"))
				{
					property_widgets::RowLabel("Class");
					if (ImGui::InputTextWithHint("##class", "UserWidget", doc.class_buffer, sizeof(doc.class_buffer)))
					{
						doc.tree->SetClassName(doc.class_buffer);
						doc.touched = true;
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Class made by CreateWidget / UI.create : a JavaScript class\n(class X extends UserWidget) or a C++ lynx::UserWidget registered\nwith LYNX_MODULE_REGISTER. Empty : lynx::UserWidget.");
					property_widgets::EndTable();
				}

				ImGui::Spacing();
				if (ImGui::Button("Copy JS class", ImVec2(-FLT_MIN, 0.f)))
					ImGui::SetClipboardText(MakeJsClass(doc).c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("class X extends UserWidget with Construct / Tick / Destruct\nand the events of the buttons, sliders and checkboxes.\nPut its name in Class above.");
				if (ImGui::Button("Copy C++ class", ImVec2(-FLT_MIN, 0.f)))
					ImGui::SetClipboardText(MakeCppClass(doc).c_str());
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("A lynx::UserWidget class with a pointer per named widget\n(GetWidget in NativeConstruct, OnClicked for the buttons).");

				ImGui::Spacing();
				ImGui::TextDisabled("Select a widget to edit it.");
				return;
			}

			// Name (unique in the tree).
			if (doc.name_buffer_owner != widget)
			{
				std::snprintf(doc.name_buffer, sizeof(doc.name_buffer), "%s", widget->GetName().c_str());
				doc.name_buffer_owner = widget;
			}
			if (doc.focus_name)
			{
				ImGui::SetKeyboardFocusHere();
				doc.focus_name = false;
			}
			ImGui::SetNextItemWidth(-ImGui::CalcTextSize(ClassLabel(*widget).c_str()).x - ImGui::GetStyle().ItemSpacing.x * 2.f);
			ImGui::InputText("##name", doc.name_buffer, sizeof(doc.name_buffer), ImGuiInputTextFlags_AutoSelectAll);
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				std::string name = doc.name_buffer;
				name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '"' || c == '<' || c == '>' || c == '&'; }), name.end());
				if (!name.empty() && name != widget->GetName())
				{
					widget->SetName(UniqueName(doc, name, widget));
					doc.touched = true;
				}
				doc.name_buffer_owner = nullptr;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%s", ClassLabel(*widget).c_str());

			// Slot
			if (PanelSlot* slot = widget->GetSlot())
			{
				const std::string title = "Slot (" + ClassLabel(*widget->GetParent()) + ")";
				if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen) &&
				    property_widgets::BeginTable("##slot"))
				{
					if (auto* canvas_slot = dynamic_cast<CanvasPanelSlot*>(slot))
					{
						CanvasSlotDetails(doc, *widget, *canvas_slot);
					}
					else
					{
						bool any = false;
						for (const auto& [name, prop] : slot->GetProperties())
						{
							if (name == "object_id_")
								continue;
							any = true;
							if (PropertyRow(doc, *slot, name, prop))
								doc.touched = true;
						}
						if (!any)
						{
							property_widgets::RowLabel("");
							ImGui::TextDisabled("(placed by its %s)", ClassLabel(*widget->GetParent()).c_str());
						}
					}
					property_widgets::EndTable();
				}
			}

			// Widget fields
			if (ImGui::CollapsingHeader(ClassLabel(*widget).c_str(), ImGuiTreeNodeFlags_DefaultOpen) &&
			    property_widgets::BeginTable("##widget"))
			{
				for (const auto& [name, prop] : widget->GetProperties())
				{
					if (name == "object_id_")
						continue;
					if (PropertyRow(doc, *widget, name, prop))
					{
						doc.touched = true;

						// A nested widget shows its new asset at once.
						if (name == "asset")
							if (auto* user = dynamic_cast<UserWidget*>(widget))
							{
								if (user->asset == AssetPathOf(doc.path))
								{
									ShowMessage(doc, "A widget can not contain itself");
									user->asset.clear();
								}
								else
								{
									user->LoadFromAsset(user->asset);
								}
							}
					}
				}
				property_widgets::EndTable();
			}
		}


		// ---------------------------------------------------------------------
		// Designer
		// ---------------------------------------------------------------------

		// Size of the design in slate units, and its DPI scale.
		void DesignSize(const Doc& doc, float& slate_w, float& slate_h, float& dpi)
		{
			const vec2 res = doc.tree->GetDesignSize();
			dpi = std::max(0.01f, std::min(res.x, res.y) / ui::GetDPIReference());
			slate_w = res.x / dpi;
			slate_h = res.y / dpi;
		}

		void FitView(Doc& doc, const ImVec2& area)
		{
			const vec2 res = doc.tree->GetDesignSize();
			doc.zoom = std::max(0.02f, std::min((area.x - 40.f) / res.x, (area.y - 40.f) / res.y));
			doc.pan = ImVec2((area.x - res.x * doc.zoom) * 0.5f, (area.y - res.y * doc.zoom) * 0.5f);
			doc.fit_pending = false;
		}

		void DrawDesigner(Doc& doc)
		{
			ImGuiIO& io = ImGui::GetIO();
			const ImVec2 area_min = ImGui::GetCursorScreenPos();
			const ImVec2 area = ImGui::GetContentRegionAvail();
			if (area.x < 10.f || area.y < 10.f)
				return;

			if (doc.fit_pending)
				FitView(doc, area);

			float slate_w = 0.f, slate_h = 0.f, dpi = 1.f;
			DesignSize(doc, slate_w, slate_h, dpi);

			// Same layout code as the game.
			doc.tree->LayoutForDesign(slate_w, slate_h);

			View view;
			view.origin = ImVec2(area_min.x + doc.pan.x, area_min.y + doc.pan.y);
			view.ppu = doc.zoom * dpi;

			ImGui::InvisibleButton("##designer", area,
			                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
			                       ImGuiButtonFlags_MouseButtonMiddle);
			const bool hovered = ImGui::IsItemHovered();
			const bool active = ImGui::IsItemActive();
			const ImVec2 mouse = io.MousePos;
			const ImVec2 mouse_slate = view.ToSlate(mouse);

			// --- Drop from the Palette / Hierarchy ----------------------------
			if (ImGui::BeginDragDropTarget())
			{
				std::string class_name, asset;
				Widget* node = nullptr;
				if (AcceptWidgetPayloads(doc, class_name, asset, node))
				{
					PanelWidget* target = DropTargetAt(doc, mouse_slate);
					const ImVec2 point = mouse_slate;
					if (node)
					{
						if (target && !Contains(node, target))
						{
							Later(doc, [&doc, node, target, point] {
								Widget* moved = MoveWidget(doc, node, target, target->GetChildCount());
								if (auto* slot = moved ? moved->GetSlotAs<CanvasPanelSlot>() : nullptr)
								{
									slot->anchor_min = slot->anchor_max = vec2(0.f, 0.f);
									slot->alignment = vec2(0.f, 0.f);
									slot->offsets.x = Snap(doc, point.x - target->GetGeometry().x);
									slot->offsets.y = Snap(doc, point.y - target->GetGeometry().y);
								}
							});
						}
					}
					else if (!doc.tree->GetRoot())
					{
						Later(doc, [&doc, class_name, asset] {
							AddWidget(doc, MakeWidget(doc, class_name, asset), nullptr, -1, nullptr);
						});
					}
					else if (target)
					{
						Later(doc, [&doc, class_name, asset, target, point] {
							AddWidget(doc, MakeWidget(doc, class_name, asset), target, -1, &point);
						});
					}
					else
					{
						ShowMessage(doc, "Drop into a panel (a CanvasPanel, a box...)");
					}
				}
				ImGui::EndDragDropTarget();
			}

			// --- Zoom (wheel, around the mouse) / pan ------------------------
			if (hovered && io.MouseWheel != 0.f)
			{
				const float old_zoom = doc.zoom;
				doc.zoom = std::clamp(doc.zoom * std::pow(1.15f, io.MouseWheel), 0.02f, 8.f);
				const ImVec2 rel(mouse.x - area_min.x - doc.pan.x, mouse.y - area_min.y - doc.pan.y);
				doc.pan.x -= rel.x * (doc.zoom / old_zoom - 1.f);
				doc.pan.y -= rel.y * (doc.zoom / old_zoom - 1.f);
				view.origin = ImVec2(area_min.x + doc.pan.x, area_min.y + doc.pan.y);
				view.ppu = doc.zoom * dpi;
			}
			if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)))
			{
				doc.pan.x += io.MouseDelta.x;
				doc.pan.y += io.MouseDelta.y;
				view.origin = ImVec2(area_min.x + doc.pan.x, area_min.y + doc.pan.y);
			}

			// --- Hover --------------------------------------------------------
			if (hovered && doc.drag == DragMode::None && doc.tree->GetRoot())
				doc.hovered = HitTest(*doc.tree->GetRoot(), mouse_slate);

			// --- Selection handles -------------------------------------------
			Widget* sel = doc.selected;
			CanvasPanelSlot* sel_slot = sel ? sel->GetSlotAs<CanvasPanelSlot>() : nullptr;
			ImVec2 sel_a{}, sel_b{};
			ImVec2 handles[8];
			if (sel)
			{
				const WidgetRect& g = sel->GetGeometry();
				sel_a = view.ToScreen(g.x, g.y);
				sel_b = view.ToScreen(g.x + g.w, g.y + g.h);
				HandleRects(sel_a, sel_b, handles);
			}

			auto handle_at = [&](ImVec2 p) -> int
			{
				if (!sel_slot)
					return -1;
				for (int i = 0; i < 8; ++i)
					if (std::fabs(p.x - handles[i].x) <= 6.f && std::fabs(p.y - handles[i].y) <= 6.f)
						return i;
				return -1;
			};

			// --- Click : resize handle, or select (+ move) --------------------
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				const int handle = handle_at(mouse);
				if (handle >= 0)
				{
					doc.drag = DragMode::Resize;
					doc.handle = handle;
				}
				else
				{
					Widget* hit = doc.tree->GetRoot() ? HitTest(*doc.tree->GetRoot(), mouse_slate) : nullptr;
					// Inside the current selection : keep it (to move a big panel).
					if (!(sel && hit && Contains(sel, hit) && sel_slot))
						doc.selected = hit;
					sel = doc.selected;
					sel_slot = sel ? sel->GetSlotAs<CanvasPanelSlot>() : nullptr;
					doc.drag = sel_slot ? DragMode::Move : DragMode::None;
				}
				if (sel)
				{
					doc.drag_rect = sel->GetGeometry();
					doc.drag_mouse = mouse_slate;
				}
			}

			if (doc.drag != DragMode::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
				doc.drag = DragMode::None;

			// --- Drag : move / resize a canvas child --------------------------
			if ((doc.drag == DragMode::Move || doc.drag == DragMode::Resize) && sel && sel_slot &&
			    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f))
			{
				const float dx = mouse_slate.x - doc.drag_mouse.x;
				const float dy = mouse_slate.y - doc.drag_mouse.y;
				WidgetRect r = doc.drag_rect;

				if (doc.drag == DragMode::Move)
				{
					r.x = Snap(doc, doc.drag_rect.x + dx);
					r.y = Snap(doc, doc.drag_rect.y + dy);
				}
				else
				{
					float left = doc.drag_rect.x, top = doc.drag_rect.y;
					float right = left + doc.drag_rect.w, bottom = top + doc.drag_rect.h;
					const int h = doc.handle;
					if (h == 0 || h == 6 || h == 7) left = Snap(doc, left + dx);
					if (h == 2 || h == 3 || h == 4) right = Snap(doc, right + dx);
					if (h == 0 || h == 1 || h == 2) top = Snap(doc, top + dy);
					if (h == 4 || h == 5 || h == 6) bottom = Snap(doc, bottom + dy);
					r.x = std::min(left, right - 1.f);
					r.y = std::min(top, bottom - 1.f);
					r.w = std::max(1.f, right - left);
					r.h = std::max(1.f, bottom - top);
					sel_slot->auto_size = false;
				}

				SetCanvasRect(*sel_slot, sel->GetParent()->GetGeometry(), r);
				doc.touched = true;
				doc.tree->LayoutForDesign(slate_w, slate_h);
			}

			// --- Draw ----------------------------------------------------------
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 area_max(area_min.x + area.x, area_min.y + area.y);
			draw->PushClipRect(area_min, area_max, true);
			draw->AddRectFilled(area_min, area_max, kCanvasBackground);

			// Grid (every 100 slate units).
			const float step = 100.f * view.ppu;
			if (step > 8.f)
			{
				for (float x = std::fmod(view.origin.x - area_min.x, step); x < area.x; x += step)
					draw->AddLine(ImVec2(area_min.x + x, area_min.y), ImVec2(area_min.x + x, area_max.y), kGrid);
				for (float y = std::fmod(view.origin.y - area_min.y, step); y < area.y; y += step)
					draw->AddLine(ImVec2(area_min.x, area_min.y + y), ImVec2(area_max.x, area_min.y + y), kGrid);
			}

			const ImVec2 design_a = view.ToScreen(0.f, 0.f);
			const ImVec2 design_b = view.ToScreen(slate_w, slate_h);
			draw->AddRectFilled(design_a, design_b, kDesignBackground);

			if (Widget* root = doc.tree->GetRoot())
				DrawWidget(draw, view, doc, *root, 1.f);
			else
				draw->AddText(ImVec2(design_a.x + 12.f, design_a.y + 12.f), IM_COL32(160, 160, 170, 255),
				              "Drop a panel here (Palette > CanvasPanel)");

			draw->AddRect(design_a, design_b, kDesignBorder, 0.f, 0, 1.5f);

			if (doc.hovered && doc.hovered != doc.selected && !IsHiddenInDesigner(*doc.hovered))
			{
				const WidgetRect& g = doc.hovered->GetGeometry();
				draw->AddRect(view.ToScreen(g.x, g.y), view.ToScreen(g.x + g.w, g.y + g.h), kHoverOutline, 0.f, 0, 1.f);
			}

			if (sel)
			{
				const WidgetRect& g = sel->GetGeometry();
				sel_a = view.ToScreen(g.x, g.y);
				sel_b = view.ToScreen(g.x + g.w, g.y + g.h);
				draw->AddRect(sel_a, sel_b, kSelection, 0.f, 0, 2.f);

				if (sel_slot)
				{
					DrawAnchors(draw, view, *sel_slot, sel->GetParent()->GetGeometry());
					HandleRects(sel_a, sel_b, handles);
					for (const ImVec2& h : handles)
					{
						draw->AddRectFilled(ImVec2(h.x - 4.f, h.y - 4.f), ImVec2(h.x + 4.f, h.y + 4.f), kHandle);
						draw->AddRect(ImVec2(h.x - 4.f, h.y - 4.f), ImVec2(h.x + 4.f, h.y + 4.f), kSelection);
					}
				}

				// Name + size above the selection.
				char info[160];
				std::snprintf(info, sizeof(info), "%s  %.0f x %.0f", sel->GetName().c_str(), g.w, g.h);
				draw->AddText(ImVec2(sel_a.x, sel_a.y - ImGui::GetTextLineHeight() - 2.f), kSelection, info);
			}

			// Resolution, bottom-left.
			char label[96];
			const vec2 res = doc.tree->GetDesignSize();
			std::snprintf(label, sizeof(label), "%.0f x %.0f   DPI x%.2f   zoom %.0f%%", res.x, res.y, dpi, doc.zoom * 100.f);
			draw->AddText(ImVec2(area_min.x + 8.f, area_max.y - ImGui::GetTextLineHeight() - 6.f), IM_COL32(170, 170, 180, 255), label);


			draw->PopClipRect();
		}


		// ---------------------------------------------------------------------
		// Toolbar, shortcuts, window
		// ---------------------------------------------------------------------

		void DrawToolbar(Doc& doc, const ImVec2& designer_area)
		{
			if (ImGui::Button("Save"))
				Save(doc);
			ImGui::SameLine();
			ImGui::BeginDisabled(doc.undo.empty() && !doc.touched);
			if (ImGui::Button("Undo"))
				Undo(doc);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(doc.redo.empty());
			if (ImGui::Button("Redo"))
				Redo(doc);
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			// Resolution (= design size, saved in the file).
			const vec2 res = doc.tree->GetDesignSize();
			char current[64];
			std::snprintf(current, sizeof(current), "%.0f x %.0f", res.x, res.y);
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
			if (ImGui::BeginCombo("##resolution", current))
			{
				for (const Resolution& r : kResolutions)
				{
					if (ImGui::Selectable(r.label, r.w == res.x && r.h == res.y))
					{
						doc.tree->SetDesignSize(vec2(r.w, r.h));
						doc.fit_pending = true;
						doc.touched = true;
					}
				}
				ImGui::Separator();
				float custom[2] = { res.x, res.y };
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
				if (ImGui::InputFloat2("Custom", custom, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					doc.tree->SetDesignSize(vec2(std::max(64.f, custom[0]), std::max(64.f, custom[1])));
					doc.fit_pending = true;
					doc.touched = true;
				}
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Preview resolution (the layout follows the DPI scale, like in the game)");

			ImGui::SameLine();
			if (ImGui::Button("Fit"))
				FitView(doc, designer_area);
			ImGui::SameLine();
			if (ImGui::Button("1:1"))
			{
				doc.zoom = 1.f;
				doc.pan = ImVec2(20.f, 20.f);
			}

			ImGui::SameLine();
			ImGui::Checkbox("Snap", &doc.snap);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.f);
			ImGui::DragFloat("##grid", &doc.grid, 0.2f, 1.f, 200.f, "%.0f");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Snap step (slate units)");
			ImGui::SameLine();
			ImGui::Checkbox("Outlines", &doc.outlines);

			// Last message (a bad drop...) for a few seconds.
			if (!doc.message.empty() && ImGui::GetTime() - doc.message_time < 5.0)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.f), "%s", doc.message.c_str());
			}
		}

		void Shortcuts(Doc& doc)
		{
			const ImGuiIO& io = ImGui::GetIO();
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
				Save(doc);

			if (io.WantTextInput)
				return;     // typing in a field

			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
				io.KeyShift ? Redo(doc) : Undo(doc);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
				Redo(doc);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
				Duplicate(doc, doc.selected);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
				CopySelection(doc);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
				Paste(doc);
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && doc.selected)
				DeleteWidget(doc, doc.selected);
			if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && doc.selected)
				doc.focus_name = true;
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				doc.selected = nullptr;
			if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl)
				doc.fit_pending = true;

			// Arrows : nudge a canvas child (Shift : the snap step).
			if (auto* slot = doc.selected ? doc.selected->GetSlotAs<CanvasPanelSlot>() : nullptr)
			{
				const float step = io.KeyShift ? std::max(1.f, doc.grid) : 1.f;
				float dx = 0.f, dy = 0.f;
				if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx -= step;
				if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx += step;
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy -= step;
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy += step;
				if (dx != 0.f || dy != 0.f)
				{
					WidgetRect r = doc.selected->GetGeometry();
					r.x += dx;
					r.y += dy;
					SetCanvasRect(*slot, doc.selected->GetParent()->GetGeometry(), r);
					doc.touched = true;
				}
			}
		}

		void CheckDisk(Doc& doc)
		{
			if (ImGui::GetTime() - doc.disk_check_time < 1.0)
				return;
			doc.disk_check_time = ImGui::GetTime();

			std::error_code ec;
			const auto time = fs::last_write_time(doc.path, ec);
			if (ec || time == doc.write_time)
				return;

			doc.write_time = time;
			if (doc.dirty)
				doc.changed_on_disk = true;
			else
				Load(doc);
		}

		// Vertical splitter between two children : returns the new size.
		void Splitter(const char* id, float& size, float min_size, float max_size, bool vertical)
		{
			const float thickness = 5.f;
			ImGui::PushID(id);
			if (vertical)
				ImGui::InvisibleButton("##split", ImVec2(thickness, -1.f));
			else
				ImGui::InvisibleButton("##split", ImVec2(-1.f, thickness));
			if (ImGui::IsItemActive())
				size = std::clamp(size + (vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y), min_size, max_size);
			ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			                                          ImGui::IsItemActive() ? kSelection : IM_COL32(60, 60, 66, 255));
			ImGui::PopID();
		}

		void DrawDoc(Doc& doc, ImGuiID dock_id)
		{
			CheckDisk(doc);

			const std::string title = doc.path.filename().string() + (doc.dirty || doc.touched ? " *" : "") +
			                          "###widget:" + doc.path.generic_string();

			if (dock_id != 0)
				ImGui::SetNextWindowDockID(dock_id, ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(1200.f, 750.f), ImGuiCond_FirstUseEver);
			if (doc.focus_next)
			{
				ImGui::SetNextWindowFocus();
				doc.focus_next = false;
			}

			bool window_open = true;
			const ImGuiWindowFlags flags = (doc.dirty ? ImGuiWindowFlags_UnsavedDocument : 0) |
			                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
			const bool visible = ImGui::Begin(title.c_str(), &window_open, flags);

			if (!window_open)
			{
				if (doc.dirty)
					doc.ask_close = true;
				else
					doc.open = false;
			}

			if (visible)
			{
				// Selection still in the tree (a field may have removed it).
				doc.hovered = nullptr;

				const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
				if (focused)
					Shortcuts(doc);

				if (doc.changed_on_disk)
				{
					ImGui::TextColored(ImVec4(0.80f, 0.45f, 0.05f, 1.f), "The file changed on disk.");
					ImGui::SameLine();
					if (ImGui::SmallButton("Reload"))
						Load(doc);
					ImGui::SameLine();
					if (ImGui::SmallButton("Keep my version"))
						doc.changed_on_disk = false;
				}

				const ImGuiStyle& style = ImGui::GetStyle();
				const float total_w = ImGui::GetContentRegionAvail().x;
				doc.left_width = std::clamp(doc.left_width, 150.f, std::max(150.f, total_w * 0.4f));
				doc.right_width = std::clamp(doc.right_width, 200.f, std::max(200.f, total_w * 0.45f));
				const float center_w = std::max(100.f, total_w - doc.left_width - doc.right_width - 10.f - style.ItemSpacing.x * 4.f);

				DrawToolbar(doc, ImVec2(center_w, ImGui::GetContentRegionAvail().y));
				const float body_h = ImGui::GetContentRegionAvail().y;

				// Left : Palette / Hierarchy
				ImGui::BeginChild("##left", ImVec2(doc.left_width, body_h), ImGuiChildFlags_None);
				{
					const float left_h = ImGui::GetContentRegionAvail().y;
					float palette_h = left_h * doc.palette_ratio;
					ImGui::BeginChild("##palette", ImVec2(0.f, palette_h), ImGuiChildFlags_Borders);
					DrawPalette(doc);
					ImGui::EndChild();
					Splitter("##split_lh", palette_h, 60.f, std::max(60.f, left_h - 80.f), false);
					doc.palette_ratio = palette_h / std::max(1.f, left_h);
					ImGui::BeginChild("##hierarchy", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
					DrawHierarchy(doc);
					ImGui::EndChild();
				}
				ImGui::EndChild();

				ImGui::SameLine(0.f, 0.f);
				Splitter("##split_l", doc.left_width, 150.f, total_w * 0.4f, true);
				ImGui::SameLine(0.f, 0.f);

				// Center : Designer
				ImGui::BeginChild("##designer", ImVec2(center_w, body_h), ImGuiChildFlags_Borders,
				                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
				DrawDesigner(doc);
				ImGui::EndChild();

				ImGui::SameLine(0.f, 0.f);
				float right = doc.right_width;
				{
					// Dragging left makes the right panel wider.
					float before = right;
					Splitter("##split_r", right, -100000.f, 100000.f, true);
					doc.right_width = std::clamp(doc.right_width - (right - before), 200.f, total_w * 0.45f);
				}
				ImGui::SameLine(0.f, 0.f);

				// Right : Details
				ImGui::BeginChild("##details", ImVec2(0.f, body_h), ImGuiChildFlags_Borders);
				DrawDetails(doc);
				ImGui::EndChild();

				// Tree changes asked while drawing.
				std::vector<std::function<void()>> actions;
				actions.swap(doc.pending);
				for (auto& action : actions)
					action();

				// Edits of this frame : one undo step once nothing is held.
				if (doc.touched && !ImGui::IsAnyItemActive() && doc.drag == DragMode::None)
					Commit(doc);
			}

			// Close with unsaved changes.
			if (doc.ask_close)
			{
				ImGui::OpenPopup("Unsaved widget");
				doc.ask_close = false;
			}
			if (ImGui::BeginPopupModal("Unsaved widget", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("Save the changes of %s ?", doc.path.filename().string().c_str());
				if (ImGui::Button("Save"))
				{
					if (Save(doc))
						doc.open = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Don't save"))
				{
					doc.open = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}

			ImGui::End();
		}

		// `path` is `root` or inside it : the part after root.
		bool RelativeInside(const fs::path& path, const fs::path& root, fs::path& relative)
		{
			std::error_code ec;
			const fs::path p = fs::weakly_canonical(fs::absolute(path, ec), ec);
			const fs::path r = fs::weakly_canonical(fs::absolute(root, ec), ec);
			auto pi = p.begin();
			for (auto ri = r.begin(); ri != r.end(); ++ri, ++pi)
			{
				if (ri->empty())
					continue;
				if (pi == p.end() || *pi != *ri)
					return false;
			}
			relative.clear();
			for (; pi != p.end(); ++pi)
				relative /= *pi;
			return true;
		}
	}


	// =========================================================================
	// API
	// =========================================================================

	bool CanOpen(const fs::path& path)
	{
		std::string extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return extension == ".widget";
	}

	void Open(const fs::path& path)
	{
		std::error_code ec;
		const fs::path absolute = fs::weakly_canonical(fs::absolute(path, ec), ec);

		for (auto& doc : g_docs)
		{
			if (doc->path == absolute || fs::equivalent(doc->path, absolute, ec))
			{
				doc->focus_next = true;
				return;
			}
		}

		auto doc = std::make_unique<Doc>();
		doc->path = absolute;
		Load(*doc);
		doc->focus_next = true;
		g_docs.push_back(std::move(doc));
	}

	void DrawAll(ImGuiID dock_id)
	{
		for (auto& doc : g_docs)
			DrawDoc(*doc, dock_id);

		g_docs.erase(std::remove_if(g_docs.begin(), g_docs.end(),
		                            [](const std::unique_ptr<Doc>& d) { return !d->open; }),
		             g_docs.end());
	}

	bool HasFocus()
	{
		if (g_docs.empty())
			return false;
		for (const ImGuiWindow* w = ImGui::GetCurrentContext()->NavWindow; w; w = w->ParentWindow)
		{
			if (std::strstr(w->Name, "###widget:"))
				return true;
		}
		return false;
	}

	void PathMoved(const fs::path& from, const fs::path& to)
	{
		std::error_code ec;
		const fs::path destination = fs::weakly_canonical(fs::absolute(to, ec), ec);
		for (auto& doc : g_docs)
		{
			fs::path relative;
			if (!RelativeInside(doc->path, from, relative))
				continue;
			doc->path = relative.empty() ? destination : destination / relative;
			doc->write_time = fs::last_write_time(doc->path, ec);
			doc->focus_next = true;
		}
	}

	void PathDeleted(const fs::path& target)
	{
		for (auto& doc : g_docs)
		{
			fs::path relative;
			if (RelativeInside(doc->path, target, relative))
				doc->open = false;
		}
	}

	bool HasUnsavedChanges()
	{
		return std::any_of(g_docs.begin(), g_docs.end(), [](const auto& d) { return d->dirty || d->touched; });
	}

	void SaveAll()
	{
		for (auto& doc : g_docs)
			if (doc->dirty || doc->touched)
				Save(*doc);
	}

	std::string NewFileTemplate()
	{
		UserWidget widget;
		auto root = std::make_unique<CanvasPanel>();
		root->SetName("Root");
		widget.SetRoot(std::move(root));
		return widget.SaveToString();
	}

	void SetAssetFieldDrawer(std::function<bool(std::string& value)> drawer)
	{
		g_asset_drawer = std::move(drawer);
	}
}
