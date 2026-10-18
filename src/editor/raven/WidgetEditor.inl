// =============================================================================
// Widget Editor in Raven (.widget files, UMG-like)
// (included by raven/EditorUi.inl ; replaces the ImGui WidgetEditor.cpp)
// -----------------------------------------------------------------------------
// One docked panel per open file, a tab next to the Viewport :
//
//   +-------------+-------------------------------------+--------------+
//   | PALETTE     |  Save Undo Redo | 1920x1080 Fit 1:1  |  Details     |
//   | (drag, or   |  Snap [10] Outlines                 |  (name, slot,|
//   |  dbl-click) |                                     |   widget     |
//   |-------------|         Designer                    |   fields)    |
//   | HIERARCHY   |  select, move, resize, anchors      |              |
//   +-------------+-------------------------------------+--------------+
//
// - Palette : drag a widget onto the Designer or the Hierarchy (double-click :
//   into the selected panel). "User Widgets" : the other .widget assets.
// - Hierarchy : drag to move / reparent, right click : rename, duplicate,
//   wrap with a panel, copy / paste, delete.
// - Designer : click to select, drag to move (children of a CanvasPanel),
//   handles to resize, wheel to zoom, right / middle drag to pan, F to fit.
// - Details : name, the slot of the widget (anchor presets for a CanvasPanel),
//   then its reflected fields.
// - Shortcuts (the panel has the keyboard) : Ctrl+S, Ctrl+Z / Ctrl+Y, Del,
//   Ctrl+D, Ctrl+C / Ctrl+V, F2 (name), F (fit), Escape, arrows (nudge,
//   Shift : the snap step).
//
// The layout is the one of the game (same code, lynx::UserWidget) ; the look
// is drawn here (close to the renderer, not identical).
// =============================================================================

#include "../../widgets/Widget.h"
#include "../../widgets/Panels.h"
#include "../../widgets/CommonWidgets.h"
#include "../../widgets/UserWidget.h"

#include <cfloat>
#include <set>
#include <sstream>

// <windows.h> (included by EditorMain.cpp) turns GetClassName into GetClassNameA :
// UserWidget::GetClassName must keep its name here.
#ifdef GetClassName
#undef GetClassName
#endif

namespace editor_ui::widgets
{
    namespace wfs = std::filesystem;

    constexpr const char* kDragWidgetClass = "lynx.widget.class";
    constexpr const char* kDragWidgetNode = "lynx.widget.node";

    struct Rgba { float r, g, b, a; };
    constexpr Rgba C(int r, int g2, int b, int a = 255) { return { r / 255.f, g2 / 255.f, b / 255.f, a / 255.f }; }

    const Rgba kCanvasBackground = C(24, 24, 26);
    const Rgba kGrid = C(255, 255, 255, 12);
    const Rgba kDesignBackground = C(40, 40, 44);
    const Rgba kDesignBorder = C(120, 120, 130);
    const Rgba kOutline = C(150, 150, 160, 70);
    const Rgba kHoverOutline = C(240, 185, 74, 140);
    const Rgba kSelection = C(240, 160, 40);
    const Rgba kHandle = C(255, 255, 255);
    const Rgba kAnchor = C(90, 200, 255);
    const Rgba kLabel = C(170, 170, 180);

    struct Resolution { const char* label; float w; float h; };
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

    struct P { float x = 0.f, y = 0.f; };

    enum class DragMode { None, Move, Resize, Pan };

    struct PaletteEntry { std::string class_name; std::string asset; };

    struct Doc
    {
        wfs::path path;
        std::unique_ptr<lynx::UserWidget> tree;

        std::string saved, snapshot;
        std::vector<std::string> undo, redo;
        bool dirty = false, touched = false;
        wfs::file_time_type write_time{};
        double disk_check = 0.0;
        bool changed_on_disk = false;

        lynx::Widget* selected = nullptr;
        lynx::Widget* hovered = nullptr;

        // Designer view
        float zoom = 0.5f;          // screen pixels per renderer pixel
        P pan;
        bool fit_pending = true;
        bool snap = true;
        float grid = 10.f;
        bool outlines = true;
        float view_w = 0.f, view_h = 0.f;   // size of the designer (last paint)

        DragMode drag = DragMode::None;
        int handle = -1;            // 0..7 : TL T TR R BR B BL L
        lynx::WidgetRect drag_rect{};
        P drag_mouse;               // slate units
        P last_local;               // pan : last pointer position (designer units)
        bool moved = false;

        // Widgets of the panel
        Rid panel = R_INVALID_ID, designer = R_INVALID_ID, palette = R_INVALID_ID, filter = R_INVALID_ID;
        Rid hierarchy = R_INVALID_ID, details = R_INVALID_ID, status = R_INVALID_ID, resolution = R_INVALID_ID;
        Rid snap_check = R_INVALID_ID, grid_spin = R_INVALID_ID, outlines_check = R_INVALID_ID;
        Rid undo_button = R_INVALID_ID, redo_button = R_INVALID_ID, disk = R_INVALID_ID;
        std::string shown_title;
        double status_time = -100.0;

        std::map<Rid, PaletteEntry> palette_items;
        std::map<Rid, lynx::Widget*> tree_items;
        bool palette_dirty = true, hierarchy_dirty = true, details_dirty = true;
        bool building = false;      // the code fills the widgets : no callback
        Rid name_field = R_INVALID_ID;
        std::vector<std::function<void()>> refresh;   // model -> Details rows
    };

    static std::vector<std::unique_ptr<Doc>> g_docs;
    static std::unique_ptr<eui::Menu> g_menu;
    static std::vector<std::string> g_user_widget_assets;   // relative to assets/
    static double g_user_widget_scan_time = -100.0;

    // =========================================================================
    // Small helpers
    // =========================================================================

    static bool Alive(const Doc* doc)
    {
        return std::any_of(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
    }

    // Later, if the document is still open (widgets are rebuilt outside of their own callbacks).
    static void DocLater(Doc* doc, std::function<void(Doc&)> fn)
    {
        eui::Later([doc, fn]() { if (Alive(doc)) fn(*doc); });
    }

    static wfs::path AssetsRoot()
    {
        std::error_code ec;
        return wfs::weakly_canonical(wfs::current_path(ec) / "assets", ec);
    }

    // "ui/Menu.widget" for a file in assets/, "" otherwise.
    static std::string AssetPathOf(const wfs::path& file)
    {
        std::error_code ec;
        const wfs::path relative = wfs::relative(wfs::weakly_canonical(file, ec), AssetsRoot(), ec);
        if (ec || relative.empty() || *relative.begin() == "..")
            return {};
        return relative.generic_string();
    }

    static std::string PrettyName(const std::string& name)
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

    static std::string ClassLabel(const lynx::Widget& widget) { return widget.GetTypeName(); }

    static void Status(Doc& doc, const std::string& text)
    {
        eui::SetText(doc.status, text);
        doc.status_time = Rget_time();
        std::cout << "[WIDGET EDITOR] " << text << "\n";
    }

    static void Touch(Doc& doc)
    {
        doc.touched = true;
        Rwidget_invalidate(doc.designer);
    }

    // The .widget files of assets/ (User Widgets of the Palette). true when the list changed.
    static bool ScanUserWidgets()
    {
        if (Rget_time() - g_user_widget_scan_time < 2.0)
            return false;
        g_user_widget_scan_time = Rget_time();
        const std::vector<std::string> before = g_user_widget_assets;
        g_user_widget_assets.clear();
        std::error_code ec;
        const wfs::path root = AssetsRoot();
        for (wfs::recursive_directory_iterator it(root, wfs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            std::error_code e;
            if (it->is_regular_file(e) && it->path().extension() == ".widget")
                g_user_widget_assets.push_back(wfs::relative(it->path(), root, e).generic_string());
        }
        std::sort(g_user_widget_assets.begin(), g_user_widget_assets.end());
        return g_user_widget_assets != before;
    }

    static Rid TextureOf(const std::string& asset)
    {
        if (asset.empty())
            return R_INVALID_ID;
        const Rid texture = eui::FileTexture(AssetsRoot() / asset);
        if (texture == R_INVALID_ID)
            return R_INVALID_ID;
        int w = 0, h = 0;
        Rtexture_get_size(texture, &w, &h);
        return w > 0 && h > 0 ? texture : R_INVALID_ID;
    }

    // =========================================================================
    // Tree helpers
    // =========================================================================

    static lynx::PanelWidget* AsPanel(lynx::Widget* widget) { return dynamic_cast<lynx::PanelWidget*>(widget); }

    static std::vector<lynx::Widget*> ChildrenOf(lynx::Widget& widget)
    {
        std::vector<lynx::Widget*> out;
        if (auto* panel = AsPanel(&widget))
            for (int i = 0; i < panel->GetChildCount(); ++i)
                out.push_back(panel->GetChildAt(i));
        return out;
    }

    // Drawing order : canvas children by z_order (stable).
    static std::vector<lynx::Widget*> DrawOrder(lynx::Widget& widget)
    {
        std::vector<lynx::Widget*> children = ChildrenOf(widget);
        if (dynamic_cast<lynx::CanvasPanel*>(&widget))
            std::stable_sort(children.begin(), children.end(), [](lynx::Widget* a, lynx::Widget* b)
            {
                return a->GetSlotAs<lynx::CanvasPanelSlot>()->z_order < b->GetSlotAs<lynx::CanvasPanelSlot>()->z_order;
            });
        return children;
    }

    static bool IsHiddenInDesigner(const lynx::Widget& w)
    {
        const lynx::EVisibility v = w.GetVisibility();
        return v == lynx::EVisibility::Collapsed || v == lynx::EVisibility::Hidden;
    }

    static bool Contains(lynx::Widget* ancestor, lynx::Widget* widget)
    {
        for (lynx::Widget* w = widget; w; w = w->GetParent())
            if (w == ancestor)
                return true;
        return false;
    }

    static std::vector<int> PathOf(lynx::Widget* widget)
    {
        std::vector<int> path;
        for (lynx::Widget* w = widget; w && w->GetParent(); w = w->GetParent())
            path.insert(path.begin(), w->GetParent()->GetChildIndex(w));
        return path;
    }

    static lynx::Widget* FromPath(Doc& doc, const std::vector<int>& path)
    {
        lynx::Widget* w = doc.tree->GetRoot();
        for (int index : path)
        {
            auto* panel = AsPanel(w);
            if (!panel || index < 0 || index >= panel->GetChildCount())
                return w;
            w = panel->GetChildAt(index);
        }
        return w;
    }

    static void CollectNames(lynx::Widget& widget, std::set<std::string>& names, const lynx::Widget* skip)
    {
        if (&widget != skip)
            names.insert(widget.GetName());
        for (lynx::Widget* child : ChildrenOf(widget))
            CollectNames(*child, names, skip);
    }

    static std::string UniqueName(Doc& doc, std::string base, const lynx::Widget* skip)
    {
        std::set<std::string> names;
        if (doc.tree->GetRoot())
            CollectNames(*doc.tree->GetRoot(), names, skip);
        if (base.empty())
            base = "Widget";
        if (!names.count(base))
            return base;
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

    static void MakeNamesUnique(Doc& doc, lynx::Widget& subtree)
    {
        std::vector<lynx::Widget*> list;
        std::function<void(lynx::Widget&)> collect = [&](lynx::Widget& w)
        {
            list.push_back(&w);
            for (lynx::Widget* child : ChildrenOf(w))
                collect(*child);
        };
        collect(subtree);
        for (lynx::Widget* w : list)
        {
            std::set<std::string> names;
            CollectNames(*doc.tree->GetRoot(), names, w);
            const std::string base = w->GetName().empty() ? ClassLabel(*w) : w->GetName();
            if (w->GetName().empty() || names.count(w->GetName()))
                w->SetName(UniqueName(doc, base, w));
        }
    }

    // Deepest visible widget under `p` (slate units). Nested UserWidgets are one block.
    static lynx::Widget* HitTest(lynx::Widget& widget, P p)
    {
        if (IsHiddenInDesigner(widget))
            return nullptr;
        if (!dynamic_cast<lynx::UserWidget*>(&widget))
        {
            const std::vector<lynx::Widget*> children = DrawOrder(widget);
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                if (lynx::Widget* hit = HitTest(**it, p))
                    return hit;
        }
        return widget.GetGeometry().Contains(p.x, p.y) ? &widget : nullptr;
    }

    // Panel that receives a drop at `p` : the deepest one that has room.
    static lynx::PanelWidget* DropTargetAt(Doc& doc, P p)
    {
        lynx::Widget* hit = doc.tree->GetRoot() ? HitTest(*doc.tree->GetRoot(), p) : nullptr;
        for (lynx::Widget* w = hit; w; w = w->GetParent())
            if (auto* panel = AsPanel(w); panel && panel->CanAddChild())
                return panel;
        return nullptr;
    }

    // Offsets giving `rect` (slate units) for the anchors of `slot`.
    static void SetCanvasRect(lynx::CanvasPanelSlot& slot, const lynx::WidgetRect& panel, const lynx::WidgetRect& rect)
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
        axis(panel.x, panel.w, slot.anchor_min.x, slot.anchor_max.x, slot.alignment.x, rect.x, rect.w, slot.offsets.x, slot.offsets.z);
        axis(panel.y, panel.h, slot.anchor_min.y, slot.anchor_max.y, slot.alignment.y, rect.y, rect.h, slot.offsets.y, slot.offsets.w);
    }

    static float Snap(const Doc& doc, float v)
    {
        if (!doc.snap || doc.grid <= 0.f)
            return v;
        return std::round(v / doc.grid) * doc.grid;
    }

    // =========================================================================
    // Load / save / undo
    // =========================================================================

    static void Changed(Doc& doc)   // the structure changed : lists rebuilt
    {
        doc.hierarchy_dirty = doc.details_dirty = true;
        Touch(doc);
    }

    static void LoadDoc(Doc& doc)
    {
        doc.tree = std::make_unique<lynx::UserWidget>();
        if (!doc.tree->LoadFromFile(doc.path.string()))
            Status(doc, "Could not read " + doc.path.filename().string() + " (empty widget)");
        doc.saved = doc.snapshot = doc.tree->SaveToString();
        doc.undo.clear();
        doc.redo.clear();
        doc.dirty = doc.touched = false;
        doc.selected = doc.hovered = nullptr;
        std::error_code ec;
        doc.write_time = wfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;
        doc.hierarchy_dirty = doc.details_dirty = true;
        doc.fit_pending = true;
    }

    static bool SaveDoc(Doc& doc)
    {
        const std::string text = doc.tree->SaveToString();
        if (!doc.tree->SaveToFile(doc.path.string()))
        {
            Status(doc, "Could not write " + doc.path.string());
            return false;
        }
        doc.saved = doc.snapshot = text;
        doc.dirty = doc.touched = false;
        std::error_code ec;
        doc.write_time = wfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;
        Status(doc, "Saved " + doc.path.filename().string());
        return true;
    }

    // The edits since the last commit become one undo step.
    static void Commit(Doc& doc)
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

    static void Restore(Doc& doc, const std::string& state)
    {
        const std::vector<int> selection = doc.selected ? PathOf(doc.selected) : std::vector<int>{};
        const bool had_selection = doc.selected != nullptr;
        doc.tree->LoadFromString(state);
        doc.snapshot = state;
        doc.dirty = state != doc.saved;
        doc.hovered = nullptr;
        doc.selected = had_selection && doc.tree->GetRoot() ? FromPath(doc, selection) : nullptr;
        doc.hierarchy_dirty = doc.details_dirty = true;
        Rwidget_invalidate(doc.designer);
    }

    static void Undo(Doc& doc)
    {
        Commit(doc);
        if (doc.undo.empty())
            return;
        doc.redo.push_back(doc.snapshot);
        const std::string state = doc.undo.back();
        doc.undo.pop_back();
        Restore(doc, state);
    }

    static void Redo(Doc& doc)
    {
        if (doc.redo.empty())
            return;
        doc.undo.push_back(doc.snapshot);
        const std::string state = doc.redo.back();
        doc.redo.pop_back();
        Restore(doc, state);
    }

    static void Select(Doc& doc, lynx::Widget* widget)
    {
        if (doc.selected == widget)
            return;
        doc.selected = widget;
        doc.details_dirty = true;
        // The row of the Hierarchy follows (without rebuilding it).
        doc.building = true;
        Rid row = R_INVALID_ID;
        for (const auto& [item, w] : doc.tree_items)
            if (w == widget)
                row = item;
        if (row != R_INVALID_ID)
            Rtree_view_set_selected_item(doc.hierarchy, row);
        else
            Rtree_view_clear_selection(doc.hierarchy);
        doc.building = false;
        Rwidget_invalidate(doc.designer);
    }

    // =========================================================================
    // Edits
    // =========================================================================

    static std::unique_ptr<lynx::Widget> MakeWidget(Doc& doc, const std::string& class_name, const std::string& asset)
    {
        if (!asset.empty())
        {
            if (asset == AssetPathOf(doc.path))
            {
                Status(doc, "A widget can not contain itself");
                return nullptr;
            }
            auto user = std::make_unique<lynx::UserWidget>();
            user->asset = asset;
            user->LoadFromAsset(asset);
            user->SetName(wfs::path(asset).stem().string());
            return user;
        }
        std::unique_ptr<lynx::Widget> widget = lynx::ui::NewWidget(class_name);
        if (!widget)
        {
            Status(doc, "Unknown widget class " + class_name);
            return nullptr;
        }
        widget->SetName(class_name);
        return widget;
    }

    // Adds `widget` to `parent` (nullptr : the root). In a canvas, at `canvas_point`.
    static lynx::Widget* AddWidget(Doc& doc, std::unique_ptr<lynx::Widget> widget, lynx::PanelWidget* parent, int index,
                                   const P* canvas_point)
    {
        if (!widget)
            return nullptr;
        lynx::Widget* raw = widget.get();
        if (!parent)
        {
            if (doc.tree->GetRoot())
            {
                Status(doc, "There is already a root : drop into a panel");
                return nullptr;
            }
            doc.tree->SetRoot(std::move(widget));
        }
        else
        {
            if (!parent->CanAddChild())
            {
                Status(doc, ClassLabel(*parent) + " can only have one child");
                return nullptr;
            }
            if (!parent->InsertChildAt(index < 0 ? parent->GetChildCount() : index, std::move(widget)))
                return nullptr;
            if (auto* slot = raw->GetSlotAs<lynx::CanvasPanelSlot>())
            {
                const lynx::vec2 desired = raw->GetDesiredSize();
                slot->offsets.z = std::max(desired.x, 40.f);
                slot->offsets.w = std::max(desired.y, 20.f);
                if (canvas_point)
                {
                    const lynx::WidgetRect& panel = parent->GetGeometry();
                    slot->offsets.x = Snap(doc, canvas_point->x - panel.x);
                    slot->offsets.y = Snap(doc, canvas_point->y - panel.y);
                }
            }
        }
        MakeNamesUnique(doc, *raw);
        Changed(doc);
        Select(doc, raw);
        return raw;
    }

    static void DeleteWidget(Doc& doc, lynx::Widget* widget)
    {
        if (!widget)
            return;
        lynx::Widget* next = doc.selected && Contains(widget, doc.selected) ? widget->GetParent() : doc.selected;
        if (doc.hovered && Contains(widget, doc.hovered))
            doc.hovered = nullptr;
        doc.selected = nullptr;
        if (widget == doc.tree->GetRoot())
            doc.tree->TakeRoot();
        else
            widget->RemoveFromParent();
        doc.selected = next;
        Changed(doc);
    }

    static lynx::Widget* MoveWidget(Doc& doc, lynx::Widget* widget, lynx::PanelWidget* parent, int index)
    {
        if (!widget || !parent || Contains(widget, parent) || widget == doc.tree->GetRoot())
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
            Status(doc, ClassLabel(*parent) + " can only have one child");
            return widget;
        }
        const std::string copy = lynx::ui::CopyWidget(*widget);
        widget->RemoveFromParent();
        lynx::Widget* moved = lynx::ui::PasteWidget(*parent, copy, index);
        doc.selected = nullptr;
        doc.hovered = nullptr;
        Changed(doc);
        Select(doc, moved);
        return moved;
    }

    static void Duplicate(Doc& doc, lynx::Widget* widget)
    {
        if (!widget || !widget->GetParent())
            return;
        lynx::PanelWidget* parent = widget->GetParent();
        if (!parent->CanAddChild())
        {
            Status(doc, ClassLabel(*parent) + " can only have one child");
            return;
        }
        lynx::Widget* copy = lynx::ui::PasteWidget(*parent, lynx::ui::CopyWidget(*widget), parent->GetChildIndex(widget) + 1);
        if (!copy)
            return;
        if (auto* slot = copy->GetSlotAs<lynx::CanvasPanelSlot>())
        {
            slot->offsets.x += 20.f;
            slot->offsets.y += 20.f;
        }
        MakeNamesUnique(doc, *copy);
        Changed(doc);
        Select(doc, copy);
    }

    static void CopySelection(Doc& doc)
    {
        if (doc.selected)
            Rclipboard_set_text(lynx::ui::CopyWidget(*doc.selected).c_str());
    }

    static void Paste(Doc& doc)
    {
        const char* text = Rclipboard_get_text();
        if (!text || !std::strstr(text, "LynxWidgetClipboard"))
            return;
        lynx::PanelWidget* parent = AsPanel(doc.selected);
        int index = -1;
        if (!parent || !parent->CanAddChild())
        {
            parent = doc.selected ? doc.selected->GetParent() : AsPanel(doc.tree->GetRoot());
            index = (doc.selected && parent) ? parent->GetChildIndex(doc.selected) + 1 : -1;
        }
        if (!parent)
        {
            Status(doc, "Select a panel to paste into");
            return;
        }
        if (lynx::Widget* pasted = lynx::ui::PasteWidget(*parent, text, index))
        {
            MakeNamesUnique(doc, *pasted);
            Changed(doc);
            Select(doc, pasted);
        }
        else
            Status(doc, "Could not paste here (" + ClassLabel(*parent) + " is full ?)");
    }

    // `widget` goes inside a new panel of `class_name`, at its place.
    static void WrapWith(Doc& doc, lynx::Widget* widget, const char* class_name)
    {
        if (!widget)
            return;
        std::unique_ptr<lynx::Widget> wrapper = lynx::ui::NewWidget(class_name);
        auto* panel = AsPanel(wrapper.get());
        if (!panel)
            return;
        wrapper->SetName(class_name);
        const std::string copy = lynx::ui::CopyWidget(*widget);

        if (widget == doc.tree->GetRoot())
        {
            doc.tree->TakeRoot();
            doc.tree->SetRoot(std::move(wrapper));
        }
        else
        {
            lynx::PanelWidget* parent = widget->GetParent();
            const int index = parent->GetChildIndex(widget);
            std::unique_ptr<lynx::CanvasPanelSlot> old_slot;
            if (auto* s = widget->GetSlotAs<lynx::CanvasPanelSlot>())
            {
                old_slot = std::make_unique<lynx::CanvasPanelSlot>();
                old_slot->anchor_min = s->anchor_min;
                old_slot->anchor_max = s->anchor_max;
                old_slot->offsets = s->offsets;
                old_slot->alignment = s->alignment;
                old_slot->auto_size = s->auto_size;
                old_slot->z_order = s->z_order;
            }
            doc.selected = nullptr;
            widget->RemoveFromParent();
            parent->InsertChildAt(index, std::move(wrapper));
            if (auto* s = panel->GetSlotAs<lynx::CanvasPanelSlot>(); s && old_slot)
            {
                s->anchor_min = old_slot->anchor_min;
                s->anchor_max = old_slot->anchor_max;
                s->offsets = old_slot->offsets;
                s->alignment = old_slot->alignment;
                s->auto_size = old_slot->auto_size;
                s->z_order = old_slot->z_order;
            }
        }
        if (lynx::Widget* moved = lynx::ui::PasteWidget(*panel, copy))
            if (auto* inner = moved->GetSlotAs<lynx::CanvasPanelSlot>())
            {
                inner->anchor_min = lynx::vec2(0.f, 0.f);
                inner->anchor_max = lynx::vec2(1.f, 1.f);
                inner->alignment = lynx::vec2(0.f, 0.f);
                inner->offsets = lynx::vec4(0.f, 0.f, 0.f, 0.f);
            }
        MakeNamesUnique(doc, *panel);
        doc.selected = nullptr;
        doc.hovered = nullptr;
        Changed(doc);
        Select(doc, panel);
    }

    // =========================================================================
    // Designer
    // =========================================================================

    struct View
    {
        P origin;     // designer position of slate (0, 0)
        float ppu;    // designer pixels per slate unit
        P ToScreen(float x, float y) const { return { origin.x + x * ppu, origin.y + y * ppu }; }
        P ToSlate(P p) const { return { (p.x - origin.x) / ppu, (p.y - origin.y) / ppu }; }
    };

    // Size of the design in slate units, and its DPI scale.
    static void DesignSize(const Doc& doc, float& slate_w, float& slate_h, float& dpi)
    {
        const lynx::vec2 res = doc.tree->GetDesignSize();
        dpi = std::max(0.01f, std::min(res.x, res.y) / lynx::ui::GetDPIReference());
        slate_w = res.x / dpi;
        slate_h = res.y / dpi;
    }

    static View ViewOf(const Doc& doc)
    {
        float sw = 0.f, sh = 0.f, dpi = 1.f;
        DesignSize(doc, sw, sh, dpi);
        View v;
        v.origin = doc.pan;
        v.ppu = doc.zoom * dpi;
        return v;
    }

    static void FitView(Doc& doc)
    {
        if (doc.view_w < 10.f || doc.view_h < 10.f)
            return;
        const lynx::vec2 res = doc.tree->GetDesignSize();
        doc.zoom = std::max(0.02f, std::min((doc.view_w - 40.f) / res.x, (doc.view_h - 40.f) / res.y));
        doc.pan = { (doc.view_w - res.x * doc.zoom) * 0.5f, (doc.view_h - res.y * doc.zoom) * 0.5f };
        doc.fit_pending = false;
        Rwidget_invalidate(doc.designer);
    }

    static void Layout(Doc& doc)
    {
        float sw = 0.f, sh = 0.f, dpi = 1.f;
        DesignSize(doc, sw, sh, dpi);
        doc.tree->LayoutForDesign(sw, sh);
    }

    static void Fill(RPainter* p, float x0, float y0, float x1, float y1, Rgba c, float opacity = 1.f)
    {
        if (x1 > x0 && y1 > y0)
            Rdraw_rect(p, x0, y0, x1 - x0, y1 - y0, 0.f, c.r, c.g, c.b, c.a * opacity);
    }

    static void Frame(RPainter* p, float x0, float y0, float x1, float y1, Rgba c, float thickness = 1.f)
    {
        Rdraw_rect_outline(p, x0, y0, x1 - x0, y1 - y0, 0.f, thickness, c.r, c.g, c.b, c.a);
    }

    static Rgba ToRgba(const lynx::vec4& c) { return { c.x, c.y, c.z, c.w }; }

    static void Brush(RPainter* p, P a, P b, const std::string& texture, const lynx::vec4& color, float opacity)
    {
        const Rid t = TextureOf(texture);
        if (t != R_INVALID_ID)
            Rdraw_image(p, t, a.x, a.y, b.x - a.x, b.y - a.y, color.x, color.y, color.z, color.w * opacity);
        else
            Fill(p, a.x, a.y, b.x, b.y, ToRgba(color), opacity);
    }

    static void Text(RPainter* p, const View& view, const lynx::WidgetRect& r, const std::string& text,
                     float size, const lynx::vec4& color, float opacity, bool centered)
    {
        const float px = size * view.ppu;
        if (px < 2.f || text.empty())
            return;
        const P a = view.ToScreen(r.x, r.y);
        const P b = view.ToScreen(r.x + r.w, r.y + r.h);
        P pos = a;
        if (centered)
        {
            float tw = 0.f, th = 0.f;
            Rfont_measure_text(R_INVALID_ID, px, text.c_str(), &tw, &th);
            pos.x = a.x + (b.x - a.x - tw) * 0.5f;
            pos.y = a.y + (b.y - a.y - th) * 0.5f;
        }
        Rdraw_text(p, R_INVALID_ID, px, pos.x, pos.y, text.c_str(), color.x, color.y, color.z, color.w * opacity);
    }

    static void DrawWidget(RPainter* p, const View& view, Doc& doc, lynx::Widget& widget, float opacity)
    {
        if (IsHiddenInDesigner(widget))
            return;
        opacity *= std::clamp(widget.render_opacity, 0.f, 1.f);
        const lynx::WidgetRect& r = widget.GetGeometry();
        const P a = view.ToScreen(r.x, r.y);
        const P b = view.ToScreen(r.x + r.w, r.y + r.h);
        const float s = view.ppu;

        if (auto* border = dynamic_cast<lynx::Border*>(&widget))
            Brush(p, a, b, border->brush_texture, border->brush_color, opacity);
        else if (auto* image = dynamic_cast<lynx::Image*>(&widget))
            Brush(p, a, b, image->texture, image->tint, opacity);
        else if (auto* button = dynamic_cast<lynx::Button*>(&widget))
        {
            const bool hovered = doc.hovered == button;
            Brush(p, a, b, hovered ? button->hovered_texture : button->normal_texture,
                  hovered ? button->hovered_color : button->normal_color, opacity);
            Text(p, view, r, button->text, button->font_size, button->text_color, opacity, true);
        }
        else if (auto* text = dynamic_cast<lynx::TextBlock*>(&widget))
            Text(p, view, r, text->text, text->font_size, text->color, opacity, false);
        else if (auto* bar = dynamic_cast<lynx::ProgressBar*>(&widget))
        {
            Fill(p, a.x, a.y, b.x, b.y, ToRgba(bar->background_color), opacity);
            const float t = std::clamp(bar->percent, 0.f, 1.f);
            Fill(p, a.x, a.y, a.x + (b.x - a.x) * t, b.y, ToRgba(bar->fill_color), opacity);
        }
        else if (auto* slider = dynamic_cast<lynx::Slider*>(&widget))
        {
            const bool vertical = slider->orientation == static_cast<int>(lynx::EOrientation::Vertical);
            const float range = slider->max_value - slider->min_value;
            const float t = range != 0.f ? std::clamp((slider->value - slider->min_value) / range, 0.f, 1.f) : 0.f;
            const float thick = 6.f * s;
            if (vertical)
            {
                const float cx = (a.x + b.x) * 0.5f;
                const float y = b.y - (b.y - a.y) * t;
                Fill(p, cx - thick * 0.5f, a.y, cx + thick * 0.5f, b.y, ToRgba(slider->bar_color), opacity);
                Fill(p, cx - thick * 0.5f, y, cx + thick * 0.5f, b.y, ToRgba(slider->fill_color), opacity);
                Fill(p, a.x, y - 4.f * s, b.x, y + 4.f * s, ToRgba(slider->handle_color), opacity);
            }
            else
            {
                const float cy = (a.y + b.y) * 0.5f;
                const float x = a.x + (b.x - a.x) * t;
                Fill(p, a.x, cy - thick * 0.5f, b.x, cy + thick * 0.5f, ToRgba(slider->bar_color), opacity);
                Fill(p, a.x, cy - thick * 0.5f, x, cy + thick * 0.5f, ToRgba(slider->fill_color), opacity);
                Fill(p, x - 4.f * s, a.y, x + 4.f * s, b.y, ToRgba(slider->handle_color), opacity);
            }
        }
        else if (auto* check = dynamic_cast<lynx::CheckBox*>(&widget))
        {
            Fill(p, a.x, a.y, b.x, b.y, ToRgba(check->background_color), opacity);
            if (check->checked)
            {
                const float m = std::min(b.x - a.x, b.y - a.y) * 0.22f;
                Fill(p, a.x + m, a.y + m, b.x - m, b.y - m, ToRgba(check->checked_color), opacity);
            }
        }

        const bool container = AsPanel(&widget) || dynamic_cast<lynx::UserWidget*>(&widget) || dynamic_cast<lynx::Spacer*>(&widget);
        if (doc.outlines && container && !dynamic_cast<lynx::Border*>(&widget))
            Frame(p, a.x, a.y, b.x, b.y, kOutline);

        if (auto* user = dynamic_cast<lynx::UserWidget*>(&widget))
        {
            if (lynx::Widget* root = user->GetRoot())
                DrawWidget(p, view, doc, *root, opacity);
            return;
        }
        for (lynx::Widget* child : DrawOrder(widget))
            DrawWidget(p, view, doc, *child, opacity);
    }

    // Handles of the selection : TL T TR R BR B BL L.
    static void HandlePoints(P a, P b, P out[8])
    {
        const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
        out[0] = a;            out[1] = { mx, a.y };  out[2] = { b.x, a.y };
        out[3] = { b.x, my };  out[4] = b;            out[5] = { mx, b.y };
        out[6] = { a.x, b.y }; out[7] = { a.x, my };
    }

    static void DrawAnchors(RPainter* p, const View& view, const lynx::CanvasPanelSlot& slot, const lynx::WidgetRect& panel)
    {
        const P mn = view.ToScreen(panel.x + slot.anchor_min.x * panel.w, panel.y + slot.anchor_min.y * panel.h);
        const P mx = view.ToScreen(panel.x + slot.anchor_max.x * panel.w, panel.y + slot.anchor_max.y * panel.h);
        const float r = 5.f;
        auto dot = [&](float x, float y) { Rdraw_circle(p, x, y, r * 0.7f, kAnchor.r, kAnchor.g, kAnchor.b, kAnchor.a); };
        if (mn.x == mx.x && mn.y == mx.y)
        {
            // A point : a small flower.
            Rdraw_line(p, mn.x - r * 1.8f, mn.y, mn.x + r * 1.8f, mn.y, 2.f, kAnchor.r, kAnchor.g, kAnchor.b, kAnchor.a);
            Rdraw_line(p, mn.x, mn.y - r * 1.8f, mn.x, mn.y + r * 1.8f, 2.f, kAnchor.r, kAnchor.g, kAnchor.b, kAnchor.a);
            dot(mn.x, mn.y);
        }
        else
        {
            Rdraw_rect_outline(p, mn.x, mn.y, mx.x - mn.x, mx.y - mn.y, 0.f, 1.5f, kAnchor.r, kAnchor.g, kAnchor.b, kAnchor.a);
            dot(mn.x, mn.y);
            dot(mx.x, mx.y);
            dot(mn.x, mx.y);
            dot(mx.x, mn.y);
        }
    }

    static void PaintDesigner(Doc& doc, RPainter* p, float w, float h)
    {
        doc.view_w = w;
        doc.view_h = h;
        if (doc.fit_pending)
            FitView(doc);
        Layout(doc);

        float sw = 0.f, sh = 0.f, dpi = 1.f;
        DesignSize(doc, sw, sh, dpi);
        const View view = ViewOf(doc);

        Rdraw_push_clip_rect(p, 0.f, 0.f, w, h);
        Fill(p, 0.f, 0.f, w, h, kCanvasBackground);

        // Grid, every 100 slate units.
        const float step = 100.f * view.ppu;
        if (step > 8.f)
        {
            for (float x = std::fmod(view.origin.x, step); x < w; x += step)
                Rdraw_line(p, x, 0.f, x, h, 1.f, kGrid.r, kGrid.g, kGrid.b, kGrid.a);
            for (float y = std::fmod(view.origin.y, step); y < h; y += step)
                Rdraw_line(p, 0.f, y, w, y, 1.f, kGrid.r, kGrid.g, kGrid.b, kGrid.a);
        }

        const P da = view.ToScreen(0.f, 0.f);
        const P db = view.ToScreen(sw, sh);
        Fill(p, da.x, da.y, db.x, db.y, kDesignBackground);

        if (lynx::Widget* root = doc.tree->GetRoot())
            DrawWidget(p, view, doc, *root, 1.f);
        else
            Rdraw_text(p, R_INVALID_ID, 15.f, da.x + 12.f, da.y + 12.f, "Drop a panel here (Palette > CanvasPanel)",
                       kLabel.r, kLabel.g, kLabel.b, 1.f);
        Frame(p, da.x, da.y, db.x, db.y, kDesignBorder, 1.5f);

        if (doc.hovered && doc.hovered != doc.selected && !IsHiddenInDesigner(*doc.hovered))
        {
            const lynx::WidgetRect& g2 = doc.hovered->GetGeometry();
            const P a = view.ToScreen(g2.x, g2.y), b = view.ToScreen(g2.x + g2.w, g2.y + g2.h);
            Frame(p, a.x, a.y, b.x, b.y, kHoverOutline);
        }

        if (lynx::Widget* sel = doc.selected)
        {
            const lynx::WidgetRect& g2 = sel->GetGeometry();
            const P a = view.ToScreen(g2.x, g2.y), b = view.ToScreen(g2.x + g2.w, g2.y + g2.h);
            Frame(p, a.x, a.y, b.x, b.y, kSelection, 2.f);
            if (auto* slot = sel->GetSlotAs<lynx::CanvasPanelSlot>())
            {
                DrawAnchors(p, view, *slot, sel->GetParent()->GetGeometry());
                P handles[8];
                HandlePoints(a, b, handles);
                for (const P& hp : handles)
                {
                    Fill(p, hp.x - 4.f, hp.y - 4.f, hp.x + 4.f, hp.y + 4.f, kHandle);
                    Frame(p, hp.x - 4.f, hp.y - 4.f, hp.x + 4.f, hp.y + 4.f, kSelection);
                }
            }
            char info[192];
            std::snprintf(info, sizeof(info), "%s  %.0f x %.0f", sel->GetName().c_str(), g2.w, g2.h);
            Rdraw_text(p, R_INVALID_ID, 13.f, a.x, a.y - 18.f, info, kSelection.r, kSelection.g, kSelection.b, 1.f);
        }

        char label[96];
        const lynx::vec2 res = doc.tree->GetDesignSize();
        std::snprintf(label, sizeof(label), "%.0f x %.0f   DPI x%.2f   zoom %.0f%%", res.x, res.y, dpi, doc.zoom * 100.f);
        Rdraw_text(p, R_INVALID_ID, 13.f, 8.f, h - 22.f, label, kLabel.r, kLabel.g, kLabel.b, 1.f);
        Rdraw_pop_clip_rect(p);
    }

    static int HandleAt(Doc& doc, P local)
    {
        lynx::Widget* sel = doc.selected;
        if (!sel || !sel->GetSlotAs<lynx::CanvasPanelSlot>())
            return -1;
        const View view = ViewOf(doc);
        const lynx::WidgetRect& g2 = sel->GetGeometry();
        P handles[8];
        HandlePoints(view.ToScreen(g2.x, g2.y), view.ToScreen(g2.x + g2.w, g2.y + g2.h), handles);
        for (int i = 0; i < 8; ++i)
            if (std::fabs(local.x - handles[i].x) <= 6.f && std::fabs(local.y - handles[i].y) <= 6.f)
                return i;
        return -1;
    }

    static bool OnDesignerEvent(Doc& doc, const REvent& e)
    {
        const P local{ e.local_x, e.local_y };
        const View view = ViewOf(doc);
        const P slate = view.ToSlate(local);

        switch (e.type)
        {
        case R_EVENT_MOUSE_DOWN:
        {
            Rwidget_focus(doc.designer);
            if (e.button == R_MOUSE_LEFT)
            {
                const int handle = HandleAt(doc, local);
                if (handle >= 0)
                {
                    doc.drag = DragMode::Resize;
                    doc.handle = handle;
                }
                else
                {
                    lynx::Widget* hit = doc.tree->GetRoot() ? HitTest(*doc.tree->GetRoot(), slate) : nullptr;
                    lynx::Widget* sel = doc.selected;
                    // Inside the current selection : kept (to move a big panel).
                    if (!(sel && hit && Contains(sel, hit) && sel->GetSlotAs<lynx::CanvasPanelSlot>()))
                        Select(doc, hit);
                    doc.drag = doc.selected && doc.selected->GetSlotAs<lynx::CanvasPanelSlot>() ? DragMode::Move : DragMode::None;
                }
                if (doc.selected)
                {
                    doc.drag_rect = doc.selected->GetGeometry();
                    doc.drag_mouse = slate;
                }
                doc.moved = false;
            }
            else
            {
                doc.drag = DragMode::Pan;
                doc.last_local = local;
            }
            Rwidget_invalidate(doc.designer);
            return true;
        }
        case R_EVENT_MOUSE_MOVE:
        {
            if (doc.drag == DragMode::Pan)
            {
                doc.pan.x += local.x - doc.last_local.x;
                doc.pan.y += local.y - doc.last_local.y;
                doc.last_local = local;
                Rwidget_invalidate(doc.designer);
                return true;
            }
            lynx::Widget* sel = doc.selected;
            auto* slot = sel ? sel->GetSlotAs<lynx::CanvasPanelSlot>() : nullptr;
            if ((doc.drag == DragMode::Move || doc.drag == DragMode::Resize) && slot)
            {
                const float dx = slate.x - doc.drag_mouse.x;
                const float dy = slate.y - doc.drag_mouse.y;
                if (!doc.moved && std::fabs(dx) * view.ppu < 2.f && std::fabs(dy) * view.ppu < 2.f)
                    return true;   // a click, not a drag yet
                doc.moved = true;
                lynx::WidgetRect r = doc.drag_rect;
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
                    slot->auto_size = false;
                }
                SetCanvasRect(*slot, sel->GetParent()->GetGeometry(), r);
                Layout(doc);
                Touch(doc);
                return true;
            }
            // Hover
            lynx::Widget* hovered = doc.tree->GetRoot() ? HitTest(*doc.tree->GetRoot(), slate) : nullptr;
            if (hovered != doc.hovered)
            {
                doc.hovered = hovered;
                Rwidget_invalidate(doc.designer);
            }
            const int handle = HandleAt(doc, local);
            Rwidget_set_cursor(doc.designer, handle >= 0 ? R_CURSOR_HAND : R_CURSOR_ARROW);
            return false;
        }
        case R_EVENT_MOUSE_UP:
            if (doc.drag != DragMode::None)
            {
                doc.drag = DragMode::None;
                Rwidget_invalidate(doc.designer);
                return true;
            }
            return false;
        case R_EVENT_MOUSE_WHEEL:
        {
            if (e.wheel_y == 0.f)
                return false;
            const float old_zoom = doc.zoom;
            doc.zoom = std::clamp(doc.zoom * std::pow(1.15f, e.wheel_y), 0.02f, 8.f);
            doc.pan.x = local.x - (local.x - doc.pan.x) * (doc.zoom / old_zoom);
            doc.pan.y = local.y - (local.y - doc.pan.y) * (doc.zoom / old_zoom);
            Rwidget_invalidate(doc.designer);
            return true;
        }
        default:
            return false;
        }
    }

    // A palette entry or a hierarchy node dropped on the Designer.
    static bool OnDesignerDrop(Doc& doc, const RDropEvent& drop)
    {
        const std::string type = drop.drag_type ? drop.drag_type : "";
        if (type != kDragWidgetClass && type != kDragWidgetNode)
            return false;
        if (drop.phase != R_DROP_DROPPED)
            return true;
        const unsigned long id = drop.text ? std::strtoul(drop.text, nullptr, 10) : 0;
        const P slate = ViewOf(doc).ToSlate({ drop.local_x, drop.local_y });
        Doc* d = &doc;

        if (type == kDragWidgetNode)
        {
            Doc* source = nullptr;
            lynx::Widget* node = nullptr;
            for (auto& other : g_docs)
                if (auto it = other->tree_items.find(static_cast<Rid>(id)); it != other->tree_items.end())
                {
                    source = other.get();
                    node = it->second;
                }
            if (source != d || !node)
                return true;
            lynx::PanelWidget* target = DropTargetAt(doc, slate);
            if (!target || Contains(node, target))
                return true;
            DocLater(d, [node, target, slate](Doc& x)
            {
                lynx::Widget* moved = MoveWidget(x, node, target, target->GetChildCount());
                if (auto* slot = moved ? moved->GetSlotAs<lynx::CanvasPanelSlot>() : nullptr)
                {
                    slot->anchor_min = slot->anchor_max = lynx::vec2(0.f, 0.f);
                    slot->alignment = lynx::vec2(0.f, 0.f);
                    slot->offsets.x = Snap(x, slate.x - target->GetGeometry().x);
                    slot->offsets.y = Snap(x, slate.y - target->GetGeometry().y);
                }
            });
            return true;
        }

        auto it = doc.palette_items.find(static_cast<Rid>(id));
        if (it == doc.palette_items.end())
            return true;
        const PaletteEntry entry = it->second;
        DocLater(d, [entry, slate](Doc& x)
        {
            if (!x.tree->GetRoot())
            {
                AddWidget(x, MakeWidget(x, entry.class_name, entry.asset), nullptr, -1, nullptr);
                return;
            }
            lynx::PanelWidget* target = DropTargetAt(x, slate);
            if (!target)
            {
                Status(x, "Drop into a panel (a CanvasPanel, a box...)");
                return;
            }
            AddWidget(x, MakeWidget(x, entry.class_name, entry.asset), target, -1, &slate);
        });
        return true;
    }

    // =========================================================================
    // Palette and Hierarchy
    // =========================================================================

    static void RebuildPalette(Doc& doc)
    {
        doc.palette_dirty = false;
        ScanUserWidgets();
        const char* text = Rtext_input_get_text(doc.filter);
        const std::string needle = Lower(text ? text : "");
        Rtree_view_clear_items(doc.palette);
        doc.palette_items.clear();

        auto group = [&](const char* title, const std::vector<std::pair<std::string, PaletteEntry>>& entries)
        {
            Rid parent = R_INVALID_ID;
            for (const auto& [label, entry] : entries)
            {
                if (!needle.empty() && Lower(label).find(needle) == std::string::npos)
                    continue;
                if (parent == R_INVALID_ID)
                {
                    parent = Rtree_view_add_item(doc.palette, R_INVALID_ID, title);
                    Rtree_item_set_expanded(parent, R_TRUE);
                }
                const Rid item = Rtree_view_add_item(doc.palette, parent, label.c_str());
                doc.palette_items[item] = entry;
            }
        };

        std::vector<std::pair<std::string, PaletteEntry>> panels, common, game, users;
        for (const char* c : kPanelClasses)
            panels.push_back({ c, { c, {} } });
        for (const char* c : kCommonClasses)
            common.push_back({ c, { c, {} } });
        // Other classes : the game's widgets (factory), after the built-in ones.
        for (const std::string& c : lynx::ui::GetBuiltinWidgetClasses())
            if (std::none_of(std::begin(kPanelClasses), std::end(kPanelClasses), [&](const char* x) { return c == x; }) &&
                std::none_of(std::begin(kCommonClasses), std::end(kCommonClasses), [&](const char* x) { return c == x; }) &&
                c != "UserWidget")
                game.push_back({ c, { c, {} } });
        const std::string self = AssetPathOf(doc.path);
        for (const std::string& asset : g_user_widget_assets)
            if (asset != self)
                users.push_back({ asset, { "UserWidget", asset } });

        group("Panels", panels);
        group("Common", common);
        group("Other", game);
        group("User Widgets", users);
    }

    static void AddTreeNode(Doc& doc, lynx::Widget& widget, Rid parent)
    {
        std::string text = widget.GetName().empty() ? ClassLabel(widget) : widget.GetName();
        if (widget.GetName() != ClassLabel(widget))
            text += "   " + ClassLabel(widget);
        if (IsHiddenInDesigner(widget))
            text += "  (hidden)";
        const Rid item = Rtree_view_add_item(doc.hierarchy, parent, text.c_str());
        doc.tree_items[item] = &widget;
        if (IsHiddenInDesigner(widget))
            Rtree_item_set_text_color(item, 0.6f, 0.6f, 0.65f, 1.f);
        if (dynamic_cast<lynx::UserWidget*>(&widget) == nullptr)
            for (lynx::Widget* child : ChildrenOf(widget))
                AddTreeNode(doc, *child, item);
        Rtree_item_set_expanded(item, R_TRUE);
    }

    static void RebuildHierarchy(Doc& doc)
    {
        doc.hierarchy_dirty = false;
        doc.building = true;
        Rtree_view_clear_items(doc.hierarchy);
        doc.tree_items.clear();
        if (lynx::Widget* root = doc.tree->GetRoot())
            AddTreeNode(doc, *root, R_INVALID_ID);
        for (const auto& [item, w] : doc.tree_items)
            if (w == doc.selected)
                Rtree_view_set_selected_item(doc.hierarchy, item);
        doc.building = false;
    }

    static lynx::Widget* NodeOfItem(Doc& doc, Rid item)
    {
        auto it = doc.tree_items.find(item);
        return it == doc.tree_items.end() ? nullptr : it->second;
    }

    static void HierarchyMenu(Doc& doc, lynx::Widget* widget)
    {
        g_menu = std::make_unique<eui::Menu>(g.window);
        eui::Menu& menu = *g_menu;
        Doc* d = &doc;
        if (widget)
        {
            menu.Title(widget->GetName() + "  (" + ClassLabel(*widget) + ")");
            menu.Separator();
            menu.Item("Rename", [d]() { DocLater(d, [](Doc& x) { if (Rwidget_is_valid(x.name_field)) Rwidget_focus(x.name_field); }); }, true, "F2");
            menu.Item("Duplicate", [d, widget]() { DocLater(d, [widget](Doc& x) { Duplicate(x, widget); }); },
                      widget->GetParent() != nullptr, "Ctrl+D");
            menu.Item("Copy", [d, widget]() { Rclipboard_set_text(lynx::ui::CopyWidget(*widget).c_str()); (void)d; }, true, "Ctrl+C");
            menu.Item("Paste", [d]() { DocLater(d, [](Doc& x) { Paste(x); }); }, true, "Ctrl+V");
            eui::Menu& wrap = menu.Sub("Wrap With");
            for (const char* c : kPanelClasses)
                wrap.Item(c, [d, widget, c]() { DocLater(d, [widget, c](Doc& x) { WrapWith(x, widget, c); }); });
            menu.Separator();
            menu.Item("Delete", [d, widget]() { DocLater(d, [widget](Doc& x) { DeleteWidget(x, widget); }); }, true, "Del");
        }
        else
            menu.Item("Paste", [d]() { DocLater(d, [](Doc& x) { Paste(x); }); }, true, "Ctrl+V");
        menu.Open();
    }

    // Drop on the Hierarchy : onto a panel = inside it, onto another widget = after it.
    static bool OnHierarchyDrop(Doc& doc, const RDropEvent& drop)
    {
        const std::string type = drop.drag_type ? drop.drag_type : "";
        if (type != kDragWidgetClass && type != kDragWidgetNode)
            return false;
        const Rid over = Rtree_view_get_item_at(doc.hierarchy, drop.local_x, drop.local_y);
        if (drop.phase == R_DROP_LEAVE)
        {
            Rtree_view_set_drop_highlight(doc.hierarchy, R_INVALID_ID, R_FALSE);
            return true;
        }
        if (drop.phase != R_DROP_DROPPED)
        {
            Rtree_view_set_drop_highlight(doc.hierarchy, over, over == R_INVALID_ID);
            return true;
        }
        Rtree_view_set_drop_highlight(doc.hierarchy, R_INVALID_ID, R_FALSE);

        lynx::Widget* target = NodeOfItem(doc, over);
        lynx::PanelWidget* parent = AsPanel(target);
        int index = -1;
        if (target && (!parent || !parent->CanAddChild()))
        {
            parent = target->GetParent();
            index = parent ? parent->GetChildIndex(target) + 1 : -1;
        }
        if (!target)
            parent = AsPanel(doc.tree->GetRoot());

        const unsigned long id = drop.text ? std::strtoul(drop.text, nullptr, 10) : 0;
        Doc* d = &doc;
        if (type == kDragWidgetNode)
        {
            lynx::Widget* node = NodeOfItem(doc, static_cast<Rid>(id));
            if (!node || !parent || node == target)
                return true;
            DocLater(d, [node, parent, index](Doc& x) { MoveWidget(x, node, parent, index < 0 ? parent->GetChildCount() : index); });
            return true;
        }
        auto it = doc.palette_items.find(static_cast<Rid>(id));
        if (it == doc.palette_items.end())
            return true;
        const PaletteEntry entry = it->second;
        DocLater(d, [entry, parent, index](Doc& x)
        {
            AddWidget(x, MakeWidget(x, entry.class_name, entry.asset), x.tree->GetRoot() ? parent : nullptr, index, nullptr);
        });
        return true;
    }

    // =========================================================================
    // Details
    // =========================================================================

    static Rid MakeRow(Rid parent, const std::string& tag, const std::string& label, const std::string& extra = "")
    {
        std::string xml = "<" + tag;
        if (!label.empty())
        {
            std::string escaped;
            for (char c : label)
            {
                if (c == '"') escaped += "&quot;";
                else if (c == '&') escaped += "&amp;";
                else if (c == '<') escaped += "&lt;";
                else escaped += c;
            }
            xml += " Label=\"" + escaped + "\"";
        }
        xml += extra + "/>";
        Rid row = Rwidget_create_from_xml(xml.c_str(), xml.size());
        if (row != R_INVALID_ID)
            Rwidget_add_child(parent, row);
        return row;
    }

    static Rid DetailsSection(Rid body, const std::string& title)
    {
        std::string safe;
        for (char c : title)
            safe += (c == '"' || c == '<' || c == '&') ? ' ' : c;
        Rid section = MakeRow(body, "DetailsSection", "", " Title=\"" + safe + "\"");
        return section == R_INVALID_ID ? body : eui::Find(section, "rows");
    }

    static bool IsAssetField(const std::string& name)
    {
        return name == "asset" || name == "font" || name.find("texture") != std::string::npos;
    }

    static bool IsColorField(const std::string& name)
    {
        return name.find("color") != std::string::npos || name == "tint";
    }

    static void BindVector(Doc& doc, Rid row, int n, float* values, std::function<void()> after = {})
    {
        static const char* const kAxes[] = { "x", "y", "z", "w" };
        Doc* d = &doc;
        for (int i = 0; i < n; ++i)
        {
            Rid spin = Rwidget_find(row, kAxes[i]);
            if (spin == R_INVALID_ID)
                continue;
            eui::OnValue(spin, [d, values, i, after](double v)
            {
                if (d->building || values[i] == static_cast<float>(v))
                    return;
                values[i] = static_cast<float>(v);
                if (after)
                    after();
                Touch(*d);
            });
            doc.refresh.push_back([spin, values, i]() { eui::SetValue(spin, values[i]); });
        }
    }

    // One reflected field of a widget or a slot.
    static void AddProperty(Doc& doc, Rid parent, lynx::Widget* owner, const std::string& name, const lynx::property& prop)
    {
        const std::string label = PrettyName(name);
        const int type = static_cast<int>(prop.GetType());
        Doc* d = &doc;

        if ((type == 5 || type == 6) && IsColorField(name))
        {
            float* p = nullptr;
            if (auto v3 = std::get_if<lynx::vec3*>(&prop.property_member); v3 && *v3) p = &(*v3)->x;
            if (auto v4 = std::get_if<lynx::vec4*>(&prop.property_member); v4 && *v4) p = &(*v4)->x;
            if (!p)
                return;
            const bool alpha = type == 6;
            Rid row = MakeRow(parent, "ColorProperty", label);
            Rid picker = eui::Find(row, "value");
            Rcolor_picker_set_alpha_enabled(picker, alpha ? R_TRUE : R_FALSE);
            Rcolor_picker_set_color(picker, p[0], p[1], p[2], alpha ? p[3] : 1.f);
            eui::OnColor(picker, [d, p, alpha](float r, float g2, float b, float a)
            {
                if (d->building)
                    return;
                p[0] = r; p[1] = g2; p[2] = b;
                if (alpha)
                    p[3] = a;
                Touch(*d);
            });
            return;
        }

        switch (type)
        {
        case 0:   // int (an enum when the engine names its values)
        {
            auto ptr = std::get_if<int*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            int* value = *ptr;
            const auto& names = lynx::ui::GetEnumNames(name);
            if (!names.empty())
            {
                Rid row = MakeRow(parent, "ChoiceProperty", label);
                Rid combo = eui::Find(row, "value");
                for (const std::string& n : names)
                    Rcombo_box_add_item(combo, n.c_str());
                Rcombo_box_set_selected(combo, std::clamp(*value, 0, static_cast<int>(names.size()) - 1));
                eui::OnSelect(combo, [d, value](int index)
                {
                    if (d->building || index < 0 || index == *value)
                        return;
                    *value = index;
                    if (d->selected)
                        d->hierarchy_dirty = true;   // "(hidden)" in the Hierarchy
                    Touch(*d);
                });
                doc.refresh.push_back([combo, value]()
                {
                    if (Rcombo_box_get_selected(combo) != *value)
                        Rcombo_box_set_selected(combo, *value);
                });
                return;
            }
            Rid row = MakeRow(parent, "IntProperty", label);
            Rid spin = eui::Find(row, "value");
            eui::OnValue(spin, [d, value](double v)
            {
                const int n = static_cast<int>(std::lround(v));
                if (d->building || n == *value)
                    return;
                *value = n;
                Touch(*d);
            });
            doc.refresh.push_back([spin, value]() { eui::SetValue(spin, *value); });
            return;
        }
        case 1:   // float
        {
            auto ptr = std::get_if<float*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            float* value = *ptr;
            Rid row = MakeRow(parent, "FloatProperty", label);
            Rid spin = eui::Find(row, "value");
            if (name == "percent" || name == "render_opacity")
            {
                Rspin_box_set_range(spin, 0.0, 1.0);
                Rspin_box_set_step(spin, 0.01);
            }
            else
                Rspin_box_set_step(spin, 0.5);
            eui::OnValue(spin, [d, value](double v)
            {
                if (d->building || static_cast<float>(v) == *value)
                    return;
                *value = static_cast<float>(v);
                Touch(*d);
            });
            doc.refresh.push_back([spin, value]() { eui::SetValue(spin, *value); });
            return;
        }
        case 2:   // bool
        {
            auto ptr = std::get_if<bool*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            bool* value = *ptr;
            Rid row = MakeRow(parent, "BoolProperty", label);
            Rid check = eui::Find(row, "value");
            eui::OnToggle(check, [d, value](bool on)
            {
                if (d->building || on == *value)
                    return;
                *value = on;
                Touch(*d);
            });
            doc.refresh.push_back([check, value]() { eui::SetChecked(check, *value); });
            return;
        }
        case 3:   // string
        {
            auto ptr = std::get_if<std::string*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            std::string* value = *ptr;
            const bool asset = IsAssetField(name);
            Rid row = MakeRow(parent, asset ? "AssetProperty" : "TextProperty", label);
            Rid field = eui::Find(row, "value");
            Rtext_input_set_text(field, value->c_str());
            // A nested widget shows its new asset at once.
            auto after = [d, owner, name]()
            {
                if (name != "asset")
                    return;
                if (auto* user = dynamic_cast<lynx::UserWidget*>(owner))
                {
                    if (user->asset == AssetPathOf(d->path))
                    {
                        Status(*d, "A widget can not contain itself");
                        user->asset.clear();
                    }
                    else
                        user->LoadFromAsset(user->asset);
                }
            };
            auto set = [d, field, value, after](const std::string& text)
            {
                if (text == *value)
                    return;
                *value = text;
                if (Rwidget_is_valid(field))
                    Rtext_input_set_text(field, text.c_str());
                after();
                Touch(*d);
            };
            eui::OnText(field, [d, value, after](const std::string& text)
            {
                if (d->building || text == *value)
                    return;
                *value = text;
                after();
                Touch(*d);
            });
            if (asset)
            {
                asset_picker::AcceptAssetDrops(field, set);
                eui::OnClick(eui::Find(row, "pick"), [value, set]() { asset_picker::Open(*value, set); });
            }
            doc.refresh.push_back([field, value]()
            {
                if (Rwidget_is_focused(field))
                    return;
                const char* shown = Rtext_input_get_text(field);
                if (!shown || *value != shown)
                    Rtext_input_set_text(field, value->c_str());
            });
            return;
        }
        case 4:
        case 5:
        case 6:   // vec2 / vec3 / vec4
        {
            const int n = type - 2;
            float* p = nullptr;
            if (auto v2 = std::get_if<lynx::vec2*>(&prop.property_member); v2 && *v2) p = &(*v2)->x;
            if (auto v3 = std::get_if<lynx::vec3*>(&prop.property_member); v3 && *v3) p = &(*v3)->x;
            if (auto v4 = std::get_if<lynx::vec4*>(&prop.property_member); v4 && *v4) p = &(*v4)->x;
            if (!p)
                return;
            static const char* const kTags[] = { "Vector2Property", "Vector3Property", "Vector4Property" };
            Rid row = MakeRow(parent, kTags[n - 2], label, " Step=\"0.5\"");
            if (name.find("padding") != std::string::npos)
                eui::SetTooltip(row, "Left, Top, Right, Bottom");
            BindVector(doc, row, n, p);
            return;
        }
        default:
            return;
        }
    }

    // 16 anchor presets (like Unreal) : min x, min y, max x, max y.
    struct AnchorPreset { const char* name; float min_x, min_y, max_x, max_y; };
    const AnchorPreset kAnchorPresets[] = {
        { "Top Left", 0, 0, 0, 0 },           { "Top Center", 0.5f, 0, 0.5f, 0 },     { "Top Right", 1, 0, 1, 0 },
        { "Top, stretched", 0, 0, 1, 0 },
        { "Center Left", 0, 0.5f, 0, 0.5f },  { "Center", 0.5f, 0.5f, 0.5f, 0.5f },  { "Center Right", 1, 0.5f, 1, 0.5f },
        { "Center, stretched", 0, 0.5f, 1, 0.5f },
        { "Bottom Left", 0, 1, 0, 1 },        { "Bottom Center", 0.5f, 1, 0.5f, 1 },  { "Bottom Right", 1, 1, 1, 1 },
        { "Bottom, stretched", 0, 1, 1, 1 },
        { "Left, stretched", 0, 0, 0, 1 },    { "Middle, stretched", 0.5f, 0, 0.5f, 1 }, { "Right, stretched", 1, 0, 1, 1 },
        { "Fill", 0, 0, 1, 1 },
    };

    static void CanvasSlotDetails(Doc& doc, Rid rows, lynx::Widget& widget, lynx::CanvasPanelSlot& slot)
    {
        Doc* d = &doc;
        lynx::Widget* w = &widget;
        lynx::CanvasPanelSlot* s = &slot;

        // Presets : the widget does not move ; Shift : alignment too ; Ctrl : moved to the anchors.
        Rid row = MakeRow(rows, "ChoiceProperty", "Anchors");
        Rid combo = eui::Find(row, "value");
        eui::SetTooltip(row, "Shift : also the alignment (pivot)\nCtrl : also move the widget to the anchors");
        int current = -1;
        Rcombo_box_add_item(combo, "Custom");
        for (int i = 0; i < static_cast<int>(std::size(kAnchorPresets)); ++i)
        {
            const AnchorPreset& p = kAnchorPresets[i];
            Rcombo_box_add_item(combo, p.name);
            if (slot.anchor_min.x == p.min_x && slot.anchor_min.y == p.min_y && slot.anchor_max.x == p.max_x && slot.anchor_max.y == p.max_y)
                current = i;
        }
        Rcombo_box_set_selected(combo, current + 1);
        eui::OnSelect(combo, [d, w, s](int index)
        {
            if (d->building || index <= 0)
                return;
            const AnchorPreset& p = kAnchorPresets[index - 1];
            const lynx::WidgetRect rect = w->GetGeometry();
            s->anchor_min = lynx::vec2(p.min_x, p.min_y);
            s->anchor_max = lynx::vec2(p.max_x, p.max_y);
            if (ein::Shift())
                s->alignment = lynx::vec2(p.min_x == p.max_x ? p.min_x : 0.f, p.min_y == p.max_y ? p.min_y : 0.f);
            if (ein::Ctrl())
            {
                s->offsets.x = 0.f;
                s->offsets.y = 0.f;
                if (p.min_x != p.max_x) s->offsets.z = 0.f;
                if (p.min_y != p.max_y) s->offsets.w = 0.f;
            }
            else
                SetCanvasRect(*s, w->GetParent()->GetGeometry(), rect);
            d->details_dirty = true;   // the labels of the offsets change
            Touch(*d);
        });

        BindVector(doc, MakeRow(rows, "Vector2Property", "Anchor Min", " Step=\"0.01\""), 2, &slot.anchor_min.x);
        BindVector(doc, MakeRow(rows, "Vector2Property", "Anchor Max", " Step=\"0.01\""), 2, &slot.anchor_max.x);

        const bool stretch_x = slot.anchor_min.x != slot.anchor_max.x;
        const bool stretch_y = slot.anchor_min.y != slot.anchor_max.y;
        const char* first = stretch_x || stretch_y ? (stretch_x && stretch_y ? "Left / Top" : (stretch_x ? "Left / Pos Y" : "Pos X / Top")) : "Position";
        const char* second = stretch_x || stretch_y ? (stretch_x && stretch_y ? "Right / Bottom" : (stretch_x ? "Right / Size Y" : "Size X / Bottom")) : "Size";
        BindVector(doc, MakeRow(rows, "Vector2Property", first, " Step=\"1\""), 2, &slot.offsets.x);
        Rid size_row = MakeRow(rows, "Vector2Property", second, " Step=\"1\"");
        BindVector(doc, size_row, 2, &slot.offsets.z);
        doc.refresh.push_back([size_row, s, stretch_x, stretch_y]() { eui::SetEnabled(size_row, !(s->auto_size && !stretch_x && !stretch_y)); });
        BindVector(doc, MakeRow(rows, "Vector2Property", "Alignment", " Step=\"0.01\""), 2, &slot.alignment.x);

        Rid auto_row = MakeRow(rows, "BoolProperty", "Size To Content");
        Rid auto_check = eui::Find(auto_row, "value");
        eui::OnToggle(auto_check, [d, s](bool on) { if (!d->building && on != s->auto_size) { s->auto_size = on; Touch(*d); } });
        doc.refresh.push_back([auto_check, s]() { eui::SetChecked(auto_check, s->auto_size); });

        Rid z_row = MakeRow(rows, "IntProperty", "ZOrder");
        Rid z_spin = eui::Find(z_row, "value");
        eui::OnValue(z_spin, [d, s](double v)
        {
            const int n = static_cast<int>(std::lround(v));
            if (!d->building && n != s->z_order) { s->z_order = n; Touch(*d); }
        });
        doc.refresh.push_back([z_spin, s]() { eui::SetValue(z_spin, s->z_order); });
    }

    // C++ / JavaScript class for this widget (like the variables of a Widget Blueprint).
    static std::string ClassNameOf(Doc& doc)
    {
        std::string class_name = doc.tree->GetClassName();
        if (class_name.empty())
            class_name = doc.path.stem().string();
        class_name.erase(std::remove_if(class_name.begin(), class_name.end(),
                                        [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '_'; }),
                         class_name.end());
        if (class_name.empty() || std::isdigit(static_cast<unsigned char>(class_name[0])))
            class_name = "W" + class_name;
        return class_name;
    }

    static std::vector<lynx::Widget*> NamedWidgets(Doc& doc)
    {
        std::vector<lynx::Widget*> named;
        if (doc.tree->GetRoot())
        {
            std::function<void(lynx::Widget&)> collect = [&](lynx::Widget& w)
            {
                if (&w != doc.tree->GetRoot() && !w.GetName().empty())
                    named.push_back(&w);
                for (lynx::Widget* c : ChildrenOf(w))
                    collect(*c);
            };
            collect(*doc.tree->GetRoot());
        }
        return named;
    }

    static std::string MakeCppClass(Doc& doc)
    {
        const std::string class_name = ClassNameOf(doc);
        const std::vector<lynx::Widget*> named = NamedWidgets(doc);
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
        for (lynx::Widget* w : named)
            out << "        " << identifier(w->GetName()) << " = GetWidget<lynx::" << ClassLabel(*w) << ">(\"" << w->GetName() << "\");\n";
        for (lynx::Widget* w : named)
            if (dynamic_cast<lynx::Button*>(w))
                out << "\n        " << identifier(w->GetName()) << "->OnClicked.Subscribe([this] {\n        });\n";
        out << "    }\n\n"
            << "    void NativeTick(float dt) override\n    {\n    }\n\n"
            << "private:\n";
        for (lynx::Widget* w : named)
            out << "    lynx::" << ClassLabel(*w) << "* " << identifier(w->GetName()) << " = nullptr;\n";
        out << "};\n";
        return out.str();
    }

    static std::string MakeJsClass(Doc& doc)
    {
        const std::string class_name = ClassNameOf(doc);
        std::ostringstream out;
        out << "// " << doc.path.filename().string() << " : Widget Editor > Class = " << class_name << "\n"
            << "// Anywhere in assets/ (loaded with the other classes).\n"
            << "// UI.create(\"" << AssetPathOf(doc.path) << "\", player).addToViewport();\n"
            << "class " << class_name << " extends UserWidget {\n"
            << "    Construct() {\n";
        for (lynx::Widget* w : NamedWidgets(doc))
        {
            if (dynamic_cast<lynx::Button*>(w))
                out << "        this.find(\"" << w->GetName() << "\").onClicked(() => {\n        });\n";
            else if (dynamic_cast<lynx::Slider*>(w))
                out << "        this.find(\"" << w->GetName() << "\").onValueChanged(value => {\n        });\n";
            else if (dynamic_cast<lynx::CheckBox*>(w))
                out << "        this.find(\"" << w->GetName() << "\").onCheckStateChanged(checked => {\n        });\n";
        }
        out << "    }\n\n    Tick(dt) {\n    }\n\n    Destruct() {\n    }\n}\n";
        return out.str();
    }

    static void RebuildDetails(Doc& doc)
    {
        doc.details_dirty = false;
        doc.building = true;
        Rwidget_clear_children(doc.details);
        doc.refresh.clear();
        doc.name_field = R_INVALID_ID;
        Doc* d = &doc;
        lynx::Widget* widget = doc.selected;

        if (!widget)
        {
            // The asset itself.
            Rid title = Rwidget_add_child(doc.details, Rlabel_create(doc.path.filename().string().c_str()));
            Rwidget_add_class(title, "dialog-title");
            Rid rows = DetailsSection(doc.details, "Widget Asset");
            Rid row = MakeRow(rows, "TextProperty", "Class");
            Rid field = eui::Find(row, "value");
            Rtext_input_set_text(field, doc.tree->GetClassName().c_str());
            Rtext_input_set_placeholder(field, "UserWidget");
            eui::SetTooltip(row, "Class made by CreateWidget / UI.create : a JavaScript class\n(class X extends UserWidget) or a C++ lynx::UserWidget registered\nwith LYNX_MODULE_REGISTER. Empty : lynx::UserWidget.");
            eui::OnText(field, [d](const std::string& text)
            {
                if (d->building || text == d->tree->GetClassName())
                    return;
                d->tree->SetClassName(text);
                Touch(*d);
            });
            Rid buttons = Rwidget_add_child(rows, Rwidget_create(R_WIDGET_HBOX));
            Rpanel_set_spacing(buttons, 6.f);
            Rid js = Rwidget_add_child(buttons, Rbutton_create("Copy JS class"));
            eui::SetTooltip(js, "class X extends UserWidget with Construct / Tick / Destruct\nand the events of the buttons, sliders and checkboxes.\nPut its name in Class above.");
            eui::OnClick(js, [d]() { Rclipboard_set_text(MakeJsClass(*d).c_str()); Status(*d, "JavaScript class copied"); });
            Rid cpp = Rwidget_add_child(buttons, Rbutton_create("Copy C++ class"));
            eui::SetTooltip(cpp, "A lynx::UserWidget class with a pointer per named widget\n(GetWidget in NativeConstruct, OnClicked for the buttons).");
            eui::OnClick(cpp, [d]() { Rclipboard_set_text(MakeCppClass(*d).c_str()); Status(*d, "C++ class copied"); });
            Rid hint = Rwidget_add_child(doc.details, Rlabel_create("Select a widget to edit it."));
            Rwidget_add_class(hint, "dim");
            doc.building = false;
            return;
        }

        // Name (unique in the tree) + class.
        {
            Rid header = Rwidget_add_child(doc.details, Rwidget_create(R_WIDGET_HBOX));
            Rpanel_set_spacing(header, 6.f);
            Rid name = Rwidget_add_child(header, Rwidget_create(R_WIDGET_TEXT_INPUT));
            Rslot_set_fill(name, 1.f);
            Rtext_input_set_text(name, widget->GetName().c_str());
            eui::SetTooltip(name, "Name of the widget (GetWidget / find), unique in this file (F2)");
            doc.name_field = name;
            Rid type = Rwidget_add_child(header, Rlabel_create(ClassLabel(*widget).c_str()));
            Rwidget_add_class(type, "dim");
            Rslot_set_valign(type, R_VALIGN_CENTER);
            eui::OnCommit(name, [d, widget](const std::string& text)
            {
                std::string value = text;
                value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == '"' || c == '<' || c == '>' || c == '&'; }), value.end());
                if (value.empty() || value == widget->GetName())
                    return;
                widget->SetName(UniqueName(*d, value, widget));
                d->hierarchy_dirty = true;
                Touch(*d);
            });
        }

        // Slot
        if (lynx::PanelSlot* slot = widget->GetSlot())
        {
            Rid rows = DetailsSection(doc.details, "Slot (" + ClassLabel(*widget->GetParent()) + ")");
            if (auto* canvas_slot = dynamic_cast<lynx::CanvasPanelSlot*>(slot))
                CanvasSlotDetails(doc, rows, *widget, *canvas_slot);
            else
            {
                bool any = false;
                for (const auto& [name, prop] : slot->GetProperties())
                {
                    if (name == "object_id_")
                        continue;
                    any = true;
                    AddProperty(doc, rows, widget, name, prop);
                }
                if (!any)
                {
                    Rid none = Rwidget_add_child(rows, Rlabel_create(("(placed by its " + ClassLabel(*widget->GetParent()) + ")").c_str()));
                    Rwidget_add_class(none, "dim");
                }
            }
        }

        // Widget fields
        Rid rows = DetailsSection(doc.details, ClassLabel(*widget));
        for (const auto& [name, prop] : widget->GetProperties())
        {
            if (name == "object_id_")
                continue;
            AddProperty(doc, rows, widget, name, prop);
        }
        doc.building = false;
    }

    // =========================================================================
    // The panel
    // =========================================================================

    static const char kPanelXml[] =
        "<HBox Spacing='0'>"
        "  <VBox Spacing='4' Slot.Padding='6'>"
        "    <Label Text='PALETTE' Class='section'/>"
        "    <TextInput Name='we.filter' Placeholder='Search widgets...'/>"
        "    <TreeView Name='we.palette' Height='250' Tooltip='Drag onto the Designer or the Hierarchy (double-click : into the selected panel)'/>"
        "    <Label Text='HIERARCHY' Class='section' Slot.Padding='0,6,0,0'/>"
        "    <TreeView Name='we.tree' Slot.Fill='1'/>"
        "  </VBox>"
        "  <VBox Spacing='4' Slot.Fill='1' Slot.Padding='4'>"
        "    <HBox Spacing='6'>"
        "      <Button Name='we.save' Text='Save' Style='Primary' Tooltip='Ctrl+S'/>"
        "      <Button Name='we.undo' Text='Undo' Class='tool' Tooltip='Ctrl+Z'/>"
        "      <Button Name='we.redo' Text='Redo' Class='tool' Tooltip='Ctrl+Y'/>"
        "      <ComboBox Name='we.res' Width='200' Tooltip='Preview resolution (the layout follows the DPI scale, like in the game)'/>"
        "      <Button Name='we.fit' Text='Fit' Class='tool' Tooltip='F'/>"
        "      <Button Name='we.one' Text='1:1' Class='tool'/>"
        "      <Checkbox Name='we.snap' Text='Snap' Checked='true' Slot.VAlign='Center'/>"
        "      <SpinBox Name='we.grid' Min='1' Max='200' Step='1' Decimals='0' Width='70' Tooltip='Snap step (slate units)'/>"
        "      <Checkbox Name='we.outlines' Text='Outlines' Checked='true' Slot.VAlign='Center'/>"
        "      <Label Name='we.status' Text='' Class='warning' Slot.Fill='1' Slot.VAlign='Center'/>"
        "    </HBox>"
        "    <HBox Name='we.disk' Spacing='6' Visibility='Collapsed'>"
        "      <Label Text='The file changed on disk.' Class='warning' Slot.VAlign='Center'/>"
        "      <Button Name='we.reload' Text='Reload'/>"
        "      <Button Name='we.keep' Text='Keep my version'/>"
        "    </HBox>"
        "    <Custom Name='we.designer' Slot.Fill='1' Focusable='true'/>"
        "  </VBox>"
        "  <ScrollBox><VBox Name='we.details' Spacing='5' Slot.Padding='8'/></ScrollBox>"
        "</HBox>";

    static void Shortcut(Doc& doc, const REvent& e, bool& used)
    {
        const bool ctrl = (e.modifiers & R_MOD_CTRL) != 0;
        const bool shift = (e.modifiers & R_MOD_SHIFT) != 0;
        Doc* d = &doc;
        used = true;
        if (ctrl && e.key == R_KEY_S) { Commit(doc); SaveDoc(doc); return; }
        if (ctrl && e.key == R_KEY_Z) { DocLater(d, [shift](Doc& x) { shift ? Redo(x) : Undo(x); }); return; }
        if (ctrl && e.key == R_KEY_Y) { DocLater(d, [](Doc& x) { Redo(x); }); return; }
        // The keys below type text in the fields of Details.
        if (ein::WantTextInput()) { used = false; return; }
        if (ctrl && e.key == R_KEY_D) { DocLater(d, [](Doc& x) { Duplicate(x, x.selected); }); return; }
        if (ctrl && e.key == R_KEY_C) { CopySelection(doc); return; }
        if (ctrl && e.key == R_KEY_V) { DocLater(d, [](Doc& x) { Paste(x); }); return; }
        if (e.key == R_KEY_DELETE && doc.selected) { DocLater(d, [](Doc& x) { DeleteWidget(x, x.selected); }); return; }
        if (e.key == R_KEY_F2 && doc.selected) { DocLater(d, [](Doc& x) { if (Rwidget_is_valid(x.name_field)) { Rwidget_focus(x.name_field); Rtext_input_select_all(x.name_field); } }); return; }
        if (e.key == R_KEY_ESCAPE) { Select(doc, nullptr); return; }
        if (!ctrl && ein::ToGlfw(e.key) == GLFW_KEY_F) { doc.fit_pending = true; Rwidget_invalidate(doc.designer); return; }

        // Arrows : nudge a canvas child (Shift : the snap step).
        if (auto* slot = doc.selected ? doc.selected->GetSlotAs<lynx::CanvasPanelSlot>() : nullptr)
        {
            const float step = shift ? std::max(1.f, doc.grid) : 1.f;
            float dx = 0.f, dy = 0.f;
            const int key = ein::ToGlfw(e.key);
            if (key == GLFW_KEY_LEFT) dx -= step;
            else if (key == GLFW_KEY_RIGHT) dx += step;
            else if (key == GLFW_KEY_UP) dy -= step;
            else if (key == GLFW_KEY_DOWN) dy += step;
            if (dx != 0.f || dy != 0.f)
            {
                lynx::WidgetRect r = doc.selected->GetGeometry();
                r.x += dx;
                r.y += dy;
                SetCanvasRect(*slot, doc.selected->GetParent()->GetGeometry(), r);
                Layout(doc);
                Touch(doc);
                return;
            }
        }
        used = false;
    }

    static bool OnPanelClose(Rid panel);

    static void BuildPanel(Doc& doc)
    {
        doc.panel = Rwidget_create_from_xml(kPanelXml, sizeof(kPanelXml) - 1);
        if (doc.panel == R_INVALID_ID)
            return;
        auto find = [&](const char* name) { return eui::Find(doc.panel, name); };
        doc.designer = find("we.designer");
        doc.palette = find("we.palette");
        doc.filter = find("we.filter");
        doc.hierarchy = find("we.tree");
        doc.details = find("we.details");
        doc.status = find("we.status");
        doc.resolution = find("we.res");
        doc.snap_check = find("we.snap");
        doc.grid_spin = find("we.grid");
        doc.outlines_check = find("we.outlines");
        doc.undo_button = find("we.undo");
        doc.redo_button = find("we.redo");
        doc.disk = find("we.disk");
        Doc* d = &doc;

        // Designer
        eui::OnPaint(doc.designer, [d](RPainter* p, float w, float h) { PaintDesigner(*d, p, w, h); });
        eui::OnEvent(doc.designer, [d](const REvent& e) { return OnDesignerEvent(*d, e); });
        eui::OnDrop(doc.designer, [d](const RDropEvent& drop) { return OnDesignerDrop(*d, drop); });

        // Palette
        Rtree_view_set_drag_type(doc.palette, kDragWidgetClass);
        eui::OnText(doc.filter, [d](const std::string&) { d->palette_dirty = true; });
        eui::OnTreeActivate(doc.palette, [d](Rid item)
        {
            auto it = d->palette_items.find(item);
            if (it == d->palette_items.end())
                return;
            const PaletteEntry entry = it->second;
            DocLater(d, [entry](Doc& x)
            {
                // Into the selected panel, else next to the selected widget, else the root panel.
                lynx::PanelWidget* parent = AsPanel(x.selected);
                int index = -1;
                if (!x.tree->GetRoot())
                    parent = nullptr;
                else if (!parent || !parent->CanAddChild())
                {
                    parent = x.selected ? x.selected->GetParent() : AsPanel(x.tree->GetRoot());
                    index = x.selected && parent ? parent->GetChildIndex(x.selected) + 1 : -1;
                    if (!parent)
                    {
                        Status(x, "Select a panel first");
                        return;
                    }
                }
                AddWidget(x, MakeWidget(x, entry.class_name, entry.asset), parent, index, nullptr);
            });
        });

        // Hierarchy
        Rtree_view_set_drag_type(doc.hierarchy, kDragWidgetNode);
        eui::OnTreeSelect(doc.hierarchy, [d](Rid item)
        {
            if (d->building)
                return;
            lynx::Widget* w = NodeOfItem(*d, item);
            if (w != d->selected)
            {
                d->selected = w;
                d->details_dirty = true;
                Rwidget_invalidate(d->designer);
            }
        });
        eui::OnTreeContext(doc.hierarchy, [d](Rid item, float, float)
        {
            lynx::Widget* w = NodeOfItem(*d, item);
            eui::Later([d, w]() { if (Alive(d)) HierarchyMenu(*d, w); });
        });
        eui::OnDrop(doc.hierarchy, [d](const RDropEvent& drop) { return OnHierarchyDrop(*d, drop); });

        // Toolbar
        eui::OnClick(find("we.save"), [d]() { Commit(*d); SaveDoc(*d); });
        eui::OnClick(doc.undo_button, [d]() { DocLater(d, [](Doc& x) { Undo(x); }); });
        eui::OnClick(doc.redo_button, [d]() { DocLater(d, [](Doc& x) { Redo(x); }); });
        eui::OnClick(find("we.fit"), [d]() { FitView(*d); });
        eui::OnClick(find("we.one"), [d]() { d->zoom = 1.f; d->pan = { 20.f, 20.f }; Rwidget_invalidate(d->designer); });
        eui::OnToggle(doc.snap_check, [d](bool on) { d->snap = on; });
        eui::OnToggle(doc.outlines_check, [d](bool on) { d->outlines = on; Rwidget_invalidate(d->designer); });
        Rspin_box_set_value(doc.grid_spin, doc.grid);
        eui::OnValue(doc.grid_spin, [d](double v) { d->grid = static_cast<float>(std::clamp(v, 1.0, 200.0)); });
        for (const Resolution& r : kResolutions)
            Rcombo_box_add_item(doc.resolution, r.label);
        Rcombo_box_add_item(doc.resolution, "(other)");
        eui::OnSelect(doc.resolution, [d](int index)
        {
            if (d->building || index < 0 || index >= static_cast<int>(std::size(kResolutions)))
                return;
            const Resolution& r = kResolutions[index];
            const lynx::vec2 res = d->tree->GetDesignSize();
            if (res.x == r.w && res.y == r.h)
                return;
            d->tree->SetDesignSize(lynx::vec2(r.w, r.h));
            d->fit_pending = true;
            Touch(*d);
        });
        eui::OnClick(find("we.reload"), [d]() { DocLater(d, [](Doc& x) { LoadDoc(x); Status(x, "Reloaded"); }); });
        eui::OnClick(find("we.keep"), [d]() { d->changed_on_disk = false; });

        // A tab of its own (Documents.inl) : its parts are panels of the dock area of the tab.
        {
            const std::string title = doc.path.filename().string();
            auto page = std::make_shared<Rid>(R_INVALID_ID);
            doc.panel = documents::OpenSplit(doc.panel, title, lynx::editor::file_icons::Kind::Widget, { "Palette & Hierarchy", "Designer", "Details" },
                                             [page]() { if (OnPanelClose(*page)) documents::Close(*page); });
            *page = doc.panel;
            doc.shown_title = title;
        }

        // Shortcuts of the editor (the level editor waits : HasFocus).
        eui::OnEvent(doc.panel, [d](const REvent& e)
        {
            if (e.type != R_EVENT_KEY_DOWN)
                return false;
            bool used = false;
            Shortcut(*d, e, used);
            return used;
        });

    }

    static void CloseDoc(Doc* doc)
    {
        const auto it = std::find_if(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
        if (it == g_docs.end())
            return;
        const Rid panel = doc->panel;
        g_docs.erase(it);
        if (panel != R_INVALID_ID && Rwidget_is_valid(panel))
            documents::Close(panel);
    }

    static bool OwnsPanel(Rid panel)
    {
        return std::any_of(g_docs.begin(), g_docs.end(), [panel](const auto& d) { return d->panel == panel; });
    }

    // The x of the tab : asks first when there are changes.
    static bool OnPanelClose(Rid panel)
    {
        Doc* doc = nullptr;
        for (auto& d : g_docs)
            if (d->panel == panel)
                doc = d.get();
        if (!doc)
            return true;
        Commit(*doc);
        if (!doc->dirty)
        {
            DocLater(doc, [](Doc& x) { CloseDoc(&x); });
            return false;
        }
        eui::Ask(g.window, "Unsaved widget", doc->path.filename().string() + " was changed. Save it before closing ?",
                 { "Save", "Don't save", "Cancel" }, [doc](int choice)
        {
            if (!Alive(doc) || choice == 2 || choice < 0)
                return;
            if (choice == 0 && !SaveDoc(*doc))
                return;
            CloseDoc(doc);
        });
        return false;
    }

    static void CheckDisk(Doc& doc)
    {
        const double now = Rget_time();
        if (now - doc.disk_check < 1.0)
            return;
        doc.disk_check = now;
        std::error_code ec;
        const auto time = wfs::last_write_time(doc.path, ec);
        if (ec || time == doc.write_time)
            return;
        doc.write_time = time;
        if (doc.dirty || doc.touched)
            doc.changed_on_disk = true;
        else
        {
            LoadDoc(doc);
            Status(doc, "Reloaded (changed on disk)");
        }
    }

    static void UpdateDoc(Doc& doc)
    {
        if (doc.panel == R_INVALID_ID || !Rwidget_is_valid(doc.panel))
            return;

        // The selection may have gone (an undo, a field...).
        if (doc.selected && doc.tree->GetRoot())
        {
            bool alive = false;
            doc.tree->GetRoot()->ForEachWidget([&](lynx::Widget& w) { if (&w == doc.selected) alive = true; });
            if (!alive)
            {
                doc.selected = nullptr;
                doc.details_dirty = doc.hierarchy_dirty = true;
            }
        }
        else if (doc.selected && !doc.tree->GetRoot())
            doc.selected = nullptr;

        if (doc.palette_dirty)
            RebuildPalette(doc);
        if (doc.hierarchy_dirty)
            RebuildHierarchy(doc);
        if (doc.details_dirty)
            RebuildDetails(doc);

        // Values that moved (designer drag, nudge) -> rows of Details.
        doc.building = true;
        for (auto& fn : doc.refresh)
            fn();
        doc.building = false;

        // One undo step once nothing is held / typed.
        if (doc.touched && doc.drag == DragMode::None && !ein::Mouse(0) && !ein::WantTextInput())
            Commit(doc);
        if (doc.touched)
            Rwidget_invalidate(doc.designer);

        CheckDisk(doc);
        eui::SetVisible(doc.disk, doc.changed_on_disk);
        eui::SetEnabled(doc.undo_button, !doc.undo.empty() || doc.touched);
        eui::SetEnabled(doc.redo_button, !doc.redo.empty());

        // Resolution combo
        const lynx::vec2 res = doc.tree->GetDesignSize();
        int index = static_cast<int>(std::size(kResolutions));
        for (int i = 0; i < static_cast<int>(std::size(kResolutions)); ++i)
            if (kResolutions[i].w == res.x && kResolutions[i].h == res.y)
                index = i;
        if (Rcombo_box_get_selected(doc.resolution) != index)
        {
            doc.building = true;
            Rcombo_box_set_selected(doc.resolution, index);
            doc.building = false;
        }

        const std::string title = doc.path.filename().string() + (doc.dirty || doc.touched ? " *" : "");
        if (title != doc.shown_title)
        {
            doc.shown_title = title;
            documents::SetTitle(doc.panel, title);
        }
        if (doc.status_time > 0.0 && Rget_time() - doc.status_time > 5.0)
        {
            doc.status_time = -100.0;
            eui::SetText(doc.status, "");
        }
    }

    static void Update()
    {
        if (!g_docs.empty() && ScanUserWidgets())
            for (auto& doc : g_docs)
                doc->palette_dirty = true;
        for (size_t i = 0; i < g_docs.size(); ++i)
            UpdateDoc(*g_docs[i]);
    }
}


// =============================================================================
// lynx::editor::widget_editor (WidgetEditor.h)
// =============================================================================

namespace lynx::editor::widget_editor
{
    namespace ew = editor_ui::widgets;

    bool CanOpen(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".widget";
    }

    void Open(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
        for (auto& doc : ew::g_docs)
            if (doc->path == absolute || std::filesystem::equivalent(doc->path, absolute, ec))
            {
                editor_ui::documents::Show(doc->panel);
                return;
            }
        if (editor_ui::g.dock == R_INVALID_ID)
            return;
        auto doc = std::make_unique<ew::Doc>();
        doc->path = absolute;
        ew::LoadDoc(*doc);
        ew::g_docs.push_back(std::move(doc));
        ew::BuildPanel(*ew::g_docs.back());
        ew::UpdateDoc(*ew::g_docs.back());
    }

    void DrawAll(unsigned int) { ew::Update(); }

    bool HasFocus()
    {
        const Rid focused = Rwindow_get_focused_widget(editor_ui::g.window);
        for (auto& doc : ew::g_docs)
            if (editor_ui::IsInside(focused, doc->panel))
                return true;
        return false;
    }

    static bool RelativeInside(const std::filesystem::path& path, const std::filesystem::path& folder, std::filesystem::path& relative)
    {
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::weakly_canonical(std::filesystem::absolute(folder, ec), ec);
        if (path == base || std::filesystem::equivalent(path, base, ec))
        {
            relative.clear();
            return true;
        }
        const std::filesystem::path r = path.lexically_relative(base);
        if (r.empty() || *r.begin() == "..")
            return false;
        relative = r;
        return true;
    }

    void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::error_code ec;
        const std::filesystem::path destination = std::filesystem::weakly_canonical(std::filesystem::absolute(to, ec), ec);
        for (auto& doc : ew::g_docs)
        {
            std::filesystem::path relative;
            if (!RelativeInside(doc->path, from, relative))
                continue;
            doc->path = relative.empty() ? destination : destination / relative;
            doc->write_time = std::filesystem::last_write_time(doc->path, ec);
            doc->shown_title.clear();
        }
    }

    void PathDeleted(const std::filesystem::path& target)
    {
        std::vector<ew::Doc*> gone;
        for (auto& doc : ew::g_docs)
        {
            std::filesystem::path relative;
            if (RelativeInside(doc->path, target, relative))
                gone.push_back(doc.get());
        }
        for (ew::Doc* doc : gone)
            ew::CloseDoc(doc);
    }

    bool HasUnsavedChanges()
    {
        for (auto& doc : ew::g_docs)
        {
            ew::Commit(*doc);
            if (doc->dirty)
                return true;
        }
        return false;
    }

    void SaveAll()
    {
        for (auto& doc : ew::g_docs)
        {
            ew::Commit(*doc);
            if (doc->dirty)
                ew::SaveDoc(*doc);
        }
    }

    std::string NewFileTemplate()
    {
        lynx::UserWidget widget;
        auto root = std::make_unique<lynx::CanvasPanel>();
        root->SetName("Root");
        widget.SetRoot(std::move(root));
        return widget.SaveToString();
    }

    void SetAssetFieldDrawer(std::function<bool(std::string&)>) {}
}
