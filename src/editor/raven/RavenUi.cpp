// Lynx Editor (Raven) : C++ layer over Raven. See RavenUi.h.

#include "RavenUi.h"

#include "EditorUiFiles.h"   // generated : kLynxEmbeddedFiles (cmake/LynxEmbedFiles.cmake)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace lynx::editor::ui
{
    namespace
    {
        // ---------------------------------------------------------------------
        // Callbacks owned by the widgets
        // ---------------------------------------------------------------------

        struct Bound
        {
            virtual ~Bound() = default;
        };

        template <class F>
        struct BoundFn : Bound
        {
            explicit BoundFn(F f) : fn(std::move(f)) {}
            F fn;
        };

        std::unordered_map<Rid, std::vector<std::unique_ptr<Bound>>> g_bound;

        void WidgetDestroyed(Rid widget, void*)
        {
            auto it = g_bound.find(widget);
            if (it == g_bound.end())
                return;
            // Moved out first : a holder being destroyed may destroy widgets too.
            std::vector<std::unique_ptr<Bound>> owned = std::move(it->second);
            g_bound.erase(it);
        }

        // Destroy functions are kept apart : a click handler is a function<void()> too.
        std::unordered_map<Rid, std::vector<std::function<void()>>> g_on_destroy;

        void WidgetDestroyedWithHooks(Rid widget, void* user)
        {
            auto it = g_on_destroy.find(widget);
            if (it != g_on_destroy.end())
            {
                std::vector<std::function<void()>> fns = std::move(it->second);
                g_on_destroy.erase(it);
                for (auto& fn : fns)
                    if (fn)
                        fn();
            }
            WidgetDestroyed(widget, user);
        }

        template <class F>
        F* Keep(Rid widget, F fn)
        {
            auto& list = g_bound[widget];
            if (list.empty() && g_on_destroy.find(widget) == g_on_destroy.end())
                Rwidget_set_destroy_callback(widget, WidgetDestroyedWithHooks, nullptr);
            auto holder = std::make_unique<BoundFn<F>>(std::move(fn));
            F* raw = &holder->fn;
            list.push_back(std::move(holder));
            return raw;
        }

        // Actions and timers live until Shutdown.
        std::unordered_map<std::string, std::unique_ptr<std::function<void()>>> g_actions;
        std::vector<std::unique_ptr<std::function<void()>>> g_timers;
        std::vector<Rid> g_timer_ids;

        std::string g_disk_folder;

        // ---------------------------------------------------------------------
        // Atlases
        // ---------------------------------------------------------------------

        constexpr int kIconColumns = 8;
        constexpr int kIconRows = (static_cast<int>(icons::Icon::Count) + kIconColumns - 1) / kIconColumns;
        constexpr float kIconCell = 64.f;

        constexpr int kFileColumns = 8;
        constexpr int kFileRows = (static_cast<int>(file_icons::Kind::Count) + kFileColumns - 1) / kFileColumns;
        constexpr float kFileCell = 128.f;

        Rid g_icon_texture = R_INVALID_ID;
        Rid g_file_texture = R_INVALID_ID;
        std::unordered_map<std::string, Rid> g_textures;
        std::unordered_map<std::string, Rid> g_file_textures;

        const char* const kIconNames[] = {
            "Play", "Stop", "Pause", "Save", "Undo", "Translate", "Rotate", "Scale", "Compile", "Reload",
            "Settings", "Windows", "Console", "ContentBrowser", "Folder", "File", "FileCode", "FileImage",
            "FileSound", "Outliner", "Details", "PlaceActors", "Paint", "Git", "Commands", "Input", "Camera",
            "ColorPick", "Project", "Search", "Clear", "Copy", "Profiler", "Redo", "Shake", "World", "Plugin",
            "Dialogue", "Eye",
        };
        static_assert(sizeof(kIconNames) / sizeof(kIconNames[0]) == static_cast<size_t>(icons::Icon::Count),
                      "kIconNames must follow icons::Icon");

        const char* const kFileKindNames[] = {
            "Folder", "File", "Text", "Script", "Python", "Cpp", "Data", "Level", "Image", "Sound", "Font",
            "Widget", "AnimGraph", "BehaviorTree", "Voxels", "Config", "Code", "Markup",
        };
        static_assert(sizeof(kFileKindNames) / sizeof(kFileKindNames[0]) == static_cast<size_t>(file_icons::Kind::Count),
                      "kFileKindNames must follow file_icons::Kind");

        bool EqualNoCase(const char* a, const char* b)
        {
            for (; *a && *b; ++a, ++b)
                if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
                    return false;
            return *a == *b;
        }

        // <Icon Glyph="Play" Size="16"/> : an Image of the editor atlas, tinted
        // with the text color (TintText) unless Tint is given.
        Rid MakeIconTag(const char*, void*)
        {
            Rid image = Rwidget_create(R_WIDGET_IMAGE);
            Rimage_set_texture(image, IconTexture());
            Rimage_set_tint_text(image, R_TRUE);
            Rimage_set_stretch(image, R_STRETCH_FILL);
            Rwidget_set_size(image, 16.f, 16.f);
            Rwidget_set_hit_testable(image, R_FALSE);
            float uv[4];
            IconRegion(icons::Icon::File, uv);
            Rimage_set_region(image, uv[0], uv[1], uv[2], uv[3]);
            return image;
        }

        int IconTagAttribute(Rid widget, const char* name, const char* value, void*)
        {
            if (EqualNoCase(name, "Glyph"))
            {
                bool found = false;
                const icons::Icon icon = IconFromName(value, &found);
                if (!found)
                    return R_FALSE;
                float uv[4];
                IconRegion(icon, uv);
                Rimage_set_region(widget, uv[0], uv[1], uv[2], uv[3]);
                return R_TRUE;
            }
            if (EqualNoCase(name, "Size"))
            {
                const float size = static_cast<float>(std::atof(value));
                if (size <= 0.f)
                    return R_FALSE;
                Rwidget_set_size(widget, size, size);
                return R_TRUE;
            }
            if (EqualNoCase(name, "Tint"))
                Rimage_set_tint_text(widget, R_FALSE);   // then Raven reads the color
            return R_FALSE;
        }

        Rid MakeFileIconTag(const char*, void*)
        {
            Rid image = Rwidget_create(R_WIDGET_IMAGE);
            Rimage_set_texture(image, FileIconTexture());
            Rimage_set_stretch(image, R_STRETCH_FILL);
            Rwidget_set_size(image, 32.f, 32.f);
            Rwidget_set_hit_testable(image, R_FALSE);
            float uv[4];
            FileIconRegion(file_icons::Kind::File, uv);
            Rimage_set_region(image, uv[0], uv[1], uv[2], uv[3]);
            return image;
        }

        int FileIconTagAttribute(Rid widget, const char* name, const char* value, void*)
        {
            if (EqualNoCase(name, "Kind"))
            {
                for (int i = 0; i < static_cast<int>(file_icons::Kind::Count); ++i)
                {
                    if (!EqualNoCase(value, kFileKindNames[i]))
                        continue;
                    float uv[4];
                    FileIconRegion(static_cast<file_icons::Kind>(i), uv);
                    Rimage_set_region(widget, uv[0], uv[1], uv[2], uv[3]);
                    return R_TRUE;
                }
                return R_FALSE;
            }
            if (EqualNoCase(name, "Size"))
            {
                const float size = static_cast<float>(std::atof(value));
                if (size <= 0.f)
                    return R_FALSE;
                Rwidget_set_size(widget, size, size);
                return R_TRUE;
            }
            return R_FALSE;
        }

        // ---------------------------------------------------------------------
        // Themes
        // ---------------------------------------------------------------------

        std::vector<ThemeInfo> g_themes;

        void LoadThemes()
        {
            struct Entry { const char* file; const char* label; };
            const Entry files[] = {
                { "themes/pixel.xml", "Pixel" },
                { "themes/pixel_dark.xml", "Pixel Dark" },
                { "themes/lynx.xml", "Lynx" },
                { "themes/glass_dark.xml", "Glass" },
                { "themes/glass_light.xml", "Glass Light" },
            };
            for (const Entry& e : files)
            {
                const std::string path = Path(e.file);
                if (const char* name = Rtheme_load(path.c_str()))
                    g_themes.push_back({ name, e.label });
                else
                    std::cerr << "[UI] Could not load the theme " << path << "\n";
            }
            g_themes.push_back({ "Dark", "Dark" });
            g_themes.push_back({ "Light", "Light" });
        }
    }


    // =========================================================================
    // Start / stop
    // =========================================================================

    void Init()
    {
        // LYNX_UI_DIR : the interface files from disk (live reload) instead of the embedded ones.
        if (const char* dir = std::getenv("LYNX_UI_DIR"); dir && dir[0])
        {
            std::error_code error;
            if (std::filesystem::is_directory(dir, error))
            {
                g_disk_folder = std::filesystem::path(dir).generic_string();
                std::cout << "[UI] Interface files from " << g_disk_folder << " (live reload)\n";
            }
        }

        if (g_disk_folder.empty())
        {
            for (size_t i = 0; i < kLynxEmbeddedFileCount; ++i)
            {
                const LynxEmbeddedFile& file = kLynxEmbeddedFiles[i];
                Rfile_register_memory(file.path, file.data, file.size, R_FALSE);
            }
            Rxml_set_live_reload(R_FALSE);
        }
        else
        {
            // The fonts and the icon atlases are not in interface/ : the
            // embedded ones are seen at their place in the folder.
            for (size_t i = 0; i < kLynxEmbeddedFileCount; ++i)
            {
                const LynxEmbeddedFile& file = kLynxEmbeddedFiles[i];
                const std::string path = file.path;
                if (path.rfind("ui/fonts/", 0) == 0 || path.rfind("ui/icons/", 0) == 0)
                    Rfile_register_memory((g_disk_folder + path.substr(2)).c_str(), file.data, file.size, R_FALSE);
            }
        }

        Rxml_register_tag("Icon", MakeIconTag, IconTagAttribute, nullptr);
        Rxml_register_tag("FileIcon", MakeFileIconTag, FileIconTagAttribute, nullptr);

        // Shared declarations (<Widget Name="Field">...) used by every panel.
        Rxml_register_widgets_from_file(Path("widgets.xml").c_str());
        // Rows of the Details panel (<FloatProperty Label="speed"/>...).
        Rxml_register_widgets_from_file(Path("panels/details_rows.xml").c_str());

        LoadThemes();
    }

    void Shutdown()
    {
        for (Rid timer : g_timer_ids)
            if (Rtimer_is_valid(timer))
                Rtimer_destroy(timer);
        g_timer_ids.clear();
        g_timers.clear();
        g_actions.clear();
        g_textures.clear();
        g_file_textures.clear();
        g_icon_texture = g_file_texture = R_INVALID_ID;
    }

    const std::string& DiskFolder()
    {
        return g_disk_folder;
    }

    std::string Path(const char* file)
    {
        if (!g_disk_folder.empty())
            return g_disk_folder + "/" + file;
        return std::string("ui/") + file;
    }

    Rid Load(const char* file)
    {
        const std::string path = Path(file);
        Rid widget = Rwidget_create_from_xml_file(path.c_str());
        if (widget == R_INVALID_ID)
            std::cerr << "[UI] Could not build " << path << "\n";
        return widget;
    }

    // Every window built by LoadWindow : Find looks in them too (the panels
    // bind the widgets of the tool windows - Camera Shake, Post Process, 2D
    // Lighting, Voxel World, Options... - from the main window).
    static std::vector<Rid>& LoadedWindows()
    {
        static std::vector<Rid> windows;
        return windows;
    }

    Rid LoadWindow(const char* file)
    {
        const std::string path = Path(file);
        Rid window = Rwindow_create_from_xml_file(path.c_str());
        if (window == R_INVALID_ID)
            std::cerr << "[UI] Could not build the window " << path << "\n";
        else
            LoadedWindows().push_back(window);
        return window;
    }

    Rid Find(Rid scope, const char* name)
    {
        Rid widget = Rwidget_find(scope, name);
        if (widget != R_INVALID_ID)
            return widget;

        // Not in this scope : a widget of another window of the editor.
        std::vector<Rid>& windows = LoadedWindows();
        for (size_t i = 0; i < windows.size();)
        {
            if (!Rwindow_is_valid(windows[i]))
            {
                windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (windows[i] != scope)
            {
                widget = Rwidget_find(windows[i], name);
                if (widget != R_INVALID_ID)
                    return widget;
            }
            ++i;
        }

        std::cerr << "[UI] No widget named \"" << name << "\"\n";
        return widget;
    }

    const std::vector<ThemeInfo>& Themes()
    {
        return g_themes;
    }

    void UseTheme(const std::string& name)
    {
        if (!Rtheme_use(name.c_str()))
            std::cerr << "[UI] Unknown theme " << name << "\n";
    }


    // =========================================================================
    // Callbacks
    // =========================================================================

    void OnClick(Rid button, std::function<void()> fn)
    {
        auto* f = Keep(button, std::move(fn));
        Rbutton_set_clicked_callback(button, [](Rid, void* u) { (*static_cast<std::function<void()>*>(u))(); }, f);
    }

    void OnToggle(Rid checkbox, std::function<void(bool)> fn)
    {
        auto* f = Keep(checkbox, std::move(fn));
        Rcheckbox_set_changed_callback(checkbox,
            [](Rid, int checked, void* u) { (*static_cast<std::function<void(bool)>*>(u))(checked != 0); }, f);
    }

    void OnValue(Rid widget, std::function<void(double)> fn)
    {
        auto* f = Keep(widget, std::move(fn));
        if (Rwidget_get_type(widget) == R_WIDGET_SLIDER)
            Rslider_set_changed_callback(widget,
                [](Rid, float v, void* u) { (*static_cast<std::function<void(double)>*>(u))(v); }, f);
        else
            Rspin_box_set_changed_callback(widget,
                [](Rid, double v, void* u) { (*static_cast<std::function<void(double)>*>(u))(v); }, f);
    }

    void OnSelect(Rid combo, std::function<void(int)> fn)
    {
        auto* f = Keep(combo, std::move(fn));
        Rcombo_box_set_changed_callback(combo,
            [](Rid, int index, void* u) { (*static_cast<std::function<void(int)>*>(u))(index); }, f);
    }

    void OnText(Rid input, std::function<void(const std::string&)> fn)
    {
        auto* f = Keep(input, std::move(fn));
        Rtext_input_set_changed_callback(input,
            [](Rid, const char* text, void* u)
            { (*static_cast<std::function<void(const std::string&)>*>(u))(text ? text : ""); }, f);
    }

    void OnCommit(Rid input, std::function<void(const std::string&)> fn)
    {
        auto* f = Keep(input, std::move(fn));
        Rtext_input_set_committed_callback(input,
            [](Rid, const char* text, void* u)
            { (*static_cast<std::function<void(const std::string&)>*>(u))(text ? text : ""); }, f);
    }

    void OnMenuItem(Rid item, std::function<void()> fn)
    {
        auto* f = Keep(item, std::move(fn));
        Rmenu_item_set_clicked_callback(item, [](Rid, void* u) { (*static_cast<std::function<void()>*>(u))(); }, f);
    }

    void OnColor(Rid picker, std::function<void(float, float, float, float)> fn)
    {
        auto* f = Keep(picker, std::move(fn));
        Rcolor_picker_set_changed_callback(picker,
            [](Rid, float r, float g, float b, float a, void* u)
            { (*static_cast<std::function<void(float, float, float, float)>*>(u))(r, g, b, a); }, f);
    }

    void OnExpand(Rid expander, std::function<void(bool)> fn)
    {
        auto* f = Keep(expander, std::move(fn));
        Rexpander_set_changed_callback(expander,
            [](Rid, int expanded, void* u) { (*static_cast<std::function<void(bool)>*>(u))(expanded != 0); }, f);
    }

    void OnTab(Rid tabs, std::function<void(int)> fn)
    {
        auto* f = Keep(tabs, std::move(fn));
        Rtab_view_set_changed_callback(tabs,
            [](Rid, int index, void* u) { (*static_cast<std::function<void(int)>*>(u))(index); }, f);
    }

    void OnEvent(Rid widget, std::function<bool(const REvent&)> fn)
    {
        auto* f = Keep(widget, std::move(fn));
        Rwidget_set_event_callback(widget,
            [](Rid, const REvent* event, void* u) -> int
            { return (*static_cast<std::function<bool(const REvent&)>*>(u))(*event) ? R_TRUE : R_FALSE; }, f);
    }

    void OnDrop(Rid widget, std::function<bool(const RDropEvent&)> fn)
    {
        auto* f = Keep(widget, std::move(fn));
        Rwidget_set_drop_callback(widget,
            [](Rid, const RDropEvent* event, void* u) -> int
            { return (*static_cast<std::function<bool(const RDropEvent&)>*>(u))(*event) ? R_TRUE : R_FALSE; }, f);
    }

    void OnTreeSelect(Rid tree, std::function<void(Rid)> fn)
    {
        auto* f = Keep(tree, std::move(fn));
        Rtree_view_set_selection_changed_callback(tree,
            [](Rid, Rid item, void* u) { (*static_cast<std::function<void(Rid)>*>(u))(item); }, f);
    }

    void OnTreeActivate(Rid tree, std::function<void(Rid)> fn)
    {
        auto* f = Keep(tree, std::move(fn));
        Rtree_view_set_activated_callback(tree,
            [](Rid, Rid item, void* u) { (*static_cast<std::function<void(Rid)>*>(u))(item); }, f);
    }

    void OnTreeContext(Rid tree, std::function<void(Rid, float, float)> fn)
    {
        auto* f = Keep(tree, std::move(fn));
        Rtree_view_set_context_callback(tree,
            [](Rid, Rid item, float x, float y, void* u)
            { (*static_cast<std::function<void(Rid, float, float)>*>(u))(item, x, y); }, f);
    }

    void OnTileItem(Rid tiles, int count, std::function<void(int, RTileItem&)> fn)
    {
        auto* f = Keep(tiles, std::move(fn));
        Rtile_view_set_items(tiles, count,
            [](Rid, int index, RTileItem* item, void* u)
            { (*static_cast<std::function<void(int, RTileItem&)>*>(u))(index, *item); }, f);
    }

    void OnTileSelect(Rid tiles, std::function<void(int)> fn)
    {
        auto* f = Keep(tiles, std::move(fn));
        Rtile_view_set_selection_changed_callback(tiles,
            [](Rid, int index, void* u) { (*static_cast<std::function<void(int)>*>(u))(index); }, f);
    }

    void OnTileActivate(Rid tiles, std::function<void(int)> fn)
    {
        auto* f = Keep(tiles, std::move(fn));
        Rtile_view_set_activated_callback(tiles,
            [](Rid, int index, void* u) { (*static_cast<std::function<void(int)>*>(u))(index); }, f);
    }

    void OnTileContext(Rid tiles, std::function<void(int, float, float)> fn)
    {
        auto* f = Keep(tiles, std::move(fn));
        Rtile_view_set_context_callback(tiles,
            [](Rid, int index, float x, float y, void* u)
            { (*static_cast<std::function<void(int, float, float)>*>(u))(index, x, y); }, f);
    }

    void OnTileDragData(Rid tiles, std::function<std::string()> fn)
    {
        struct Holder { std::function<std::string()> fn; std::string text; };
        auto* f = Keep(tiles, Holder{ std::move(fn), {} });
        Rtile_view_set_drag_data_callback(tiles, [](Rid, void* u) -> const char*
        {
            auto* h = static_cast<Holder*>(u);
            h->text = h->fn ? h->fn() : std::string();
            return h->text.empty() ? nullptr : h->text.c_str();
        }, f);
    }

    void OnListActivate(Rid list, std::function<void(int)> fn)
    {
        auto* f = Keep(list, std::move(fn));
        Rlist_view_set_activated_callback(list,
            [](Rid, int index, void* u) { (*static_cast<std::function<void(int)>*>(u))(index); }, f);
    }

    void OnListSelect(Rid list, std::function<void(int)> fn)
    {
        auto* f = Keep(list, std::move(fn));
        Rlist_view_set_selection_changed_callback(list,
            [](Rid, int index, void* u) { (*static_cast<std::function<void(int)>*>(u))(index); }, f);
    }

    void OnPaint(Rid custom, std::function<void(RPainter*, float, float)> fn)
    {
        auto* f = Keep(custom, std::move(fn));
        Rcustom_set_paint_callback(custom,
            [](Rid, RPainter* painter, float w, float h, void* u)
            { (*static_cast<std::function<void(RPainter*, float, float)>*>(u))(painter, w, h); }, f);
    }

    void Action(const char* name, std::function<void()> fn)
    {
        auto holder = std::make_unique<std::function<void()>>(std::move(fn));
        Raction_bind(name, [](Rid, const char*, void* u) { (*static_cast<std::function<void()>*>(u))(); }, holder.get());
        g_actions[name] = std::move(holder);
    }

    void OnDestroy(Rid widget, std::function<void()> fn)
    {
        if (g_bound.find(widget) == g_bound.end() && g_on_destroy.find(widget) == g_on_destroy.end())
            Rwidget_set_destroy_callback(widget, WidgetDestroyedWithHooks, nullptr);
        g_on_destroy[widget].push_back(std::move(fn));
    }

    void Every(double seconds, std::function<void()> fn)
    {
        auto holder = std::make_unique<std::function<void()>>(std::move(fn));
        const Rid timer = Rtimer_create(seconds, R_TRUE,
            [](Rid, void* u) { (*static_cast<std::function<void()>*>(u))(); }, holder.get());
        g_timers.push_back(std::move(holder));
        g_timer_ids.push_back(timer);
    }

    void Later(std::function<void()> fn)
    {
        auto* holder = new std::function<void()>(std::move(fn));
        Rtask_post([](void* u)
        {
            std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()>*>(u));
            (*f)();
        }, holder);
    }


    // =========================================================================
    // Small helpers
    // =========================================================================

    void SetText(Rid widget, const std::string& text)
    {
        if (widget == R_INVALID_ID)
            return;
        switch (Rwidget_get_type(widget))
        {
        case R_WIDGET_LABEL:
            if (std::strcmp(Rlabel_get_text(widget), text.c_str()) != 0)
                Rlabel_set_text(widget, text.c_str());
            break;
        case R_WIDGET_TEXT_INPUT:
            if (std::strcmp(Rtext_input_get_text(widget), text.c_str()) != 0)
                Rtext_input_set_text(widget, text.c_str());
            break;
        default:
            Rwidget_set_attribute(widget, "Text", text.c_str());
            break;
        }
    }

    void SetVisible(Rid widget, bool visible)
    {
        if (widget == R_INVALID_ID)
            return;
        const RVisibility wanted = visible ? R_VISIBLE : R_COLLAPSED;
        if (Rwidget_get_visibility(widget) != wanted)
            Rwidget_set_visibility(widget, wanted);
    }

    void SetEnabled(Rid widget, bool enabled)
    {
        if (widget != R_INVALID_ID && (Rwidget_is_enabled(widget) != 0) != enabled)
            Rwidget_set_enabled(widget, enabled ? R_TRUE : R_FALSE);
    }

    void SetChecked(Rid checkbox, bool checked)
    {
        if (checkbox != R_INVALID_ID && (Rcheckbox_is_checked(checkbox) != 0) != checked)
            Rcheckbox_set_checked(checkbox, checked ? R_TRUE : R_FALSE);
    }

    void SetClass(Rid widget, const char* style_class, bool on)
    {
        if (widget == R_INVALID_ID)
            return;
        const bool has = Rwidget_has_class(widget, style_class) != 0;
        if (on && !has)
            Rwidget_add_class(widget, style_class);
        else if (!on && has)
            Rwidget_remove_class(widget, style_class);
    }

    void SetTooltip(Rid widget, const std::string& text)
    {
        if (widget == R_INVALID_ID)
            return;
        const char* current = Rwidget_get_tooltip(widget);
        if (!current || text != current)
            Rwidget_set_tooltip(widget, text.c_str());
    }

    void SetValue(Rid spin_box, double value)
    {
        if (spin_box == R_INVALID_ID || Rwidget_is_focused(spin_box))
            return;
        if (Rwidget_get_type(spin_box) == R_WIDGET_SLIDER)
        {
            if (std::fabs(Rslider_get_value(spin_box) - value) > 1e-6)
                Rslider_set_value(spin_box, static_cast<float>(value));
            return;
        }
        if (std::fabs(Rspin_box_get_value(spin_box) - value) > 1e-9)
            Rspin_box_set_value(spin_box, value);
    }


    // =========================================================================
    // Icons
    // =========================================================================

    Rid IconTexture()
    {
        if (g_icon_texture == R_INVALID_ID)
        {
            const std::string path = Path("icons/editor_icons.png");
            g_icon_texture = Rtexture_create_from_file(path.c_str());
            if (g_icon_texture != R_INVALID_ID)
                Rtexture_set_filter(g_icon_texture, R_FILTER_NEAREST);
        }
        return g_icon_texture;
    }

    void IconRegion(icons::Icon icon, float uv[4])
    {
        const int index = std::clamp(static_cast<int>(icon), 0, static_cast<int>(icons::Icon::Count) - 1);
        const float cw = 1.f / kIconColumns, ch = 1.f / kIconRows;
        const float iu = 0.5f / (kIconColumns * kIconCell), iv = 0.5f / (kIconRows * kIconCell);
        uv[0] = (index % kIconColumns) * cw + iu;
        uv[1] = (index / kIconColumns) * ch + iv;
        uv[2] = (index % kIconColumns + 1) * cw - iu;
        uv[3] = (index / kIconColumns + 1) * ch - iv;
    }

    Rid FileIconTexture()
    {
        if (g_file_texture == R_INVALID_ID)
        {
            const std::string path = Path("icons/file_icons.png");
            g_file_texture = Rtexture_create_from_file(path.c_str());
            if (g_file_texture != R_INVALID_ID)
                Rtexture_set_filter(g_file_texture, R_FILTER_NEAREST);
        }
        return g_file_texture;
    }

    void FileIconRegion(file_icons::Kind kind, float uv[4])
    {
        const int index = std::clamp(static_cast<int>(kind), 0, static_cast<int>(file_icons::Kind::Count) - 1);
        const float cw = 1.f / kFileColumns, ch = 1.f / kFileRows;
        const float iu = 0.5f / (kFileColumns * kFileCell), iv = 0.5f / (kFileRows * kFileCell);
        uv[0] = (index % kFileColumns) * cw + iu;
        uv[1] = (index / kFileColumns) * ch + iv;
        uv[2] = (index % kFileColumns + 1) * cw - iu;
        uv[3] = (index / kFileColumns + 1) * ch - iv;
    }

    Rid Texture(const std::string& path)
    {
        auto it = g_textures.find(path);
        if (it != g_textures.end())
            return it->second;
        const std::string full = Path(path.c_str());
        Rid texture = Rtexture_create_from_file(full.c_str());
        if (texture != R_INVALID_ID)
            Rtexture_set_filter(texture, R_FILTER_NEAREST);
        g_textures[path] = texture;
        return texture;
    }

    Rid FileTexture(const std::filesystem::path& path)
    {
        const std::string key = path.generic_string();
        auto it = g_file_textures.find(key);
        if (it != g_file_textures.end())
            return it->second;
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        Rid texture = R_INVALID_ID;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
        {
            texture = Rtexture_create_from_file_async(key.c_str());
            if (texture != R_INVALID_ID)
                Rtexture_set_filter(texture, R_FILTER_NEAREST);
        }
        g_file_textures[key] = texture;
        return texture;
    }

    Rid MakeIcon(icons::Icon icon, float size)
    {
        Rid image = MakeIconTag(nullptr, nullptr);
        float uv[4];
        IconRegion(icon, uv);
        Rimage_set_region(image, uv[0], uv[1], uv[2], uv[3]);
        Rwidget_set_size(image, size, size);
        return image;
    }

    icons::Icon IconFromName(const char* name, bool* found)
    {
        for (int i = 0; i < static_cast<int>(icons::Icon::Count); ++i)
        {
            if (name && EqualNoCase(name, kIconNames[i]))
            {
                if (found)
                    *found = true;
                return static_cast<icons::Icon>(i);
            }
        }
        if (found)
            *found = false;
        return icons::Icon::File;
    }


    // =========================================================================
    // Menus
    // =========================================================================

    Menu::Menu(Rid window)
        : window_(window), menu_(Rwidget_create(R_WIDGET_MENU))
    {
    }

    Menu::Menu(Rid window, Rid menu)
        : window_(window), menu_(menu)
    {
    }

    Menu::~Menu()
    {
        for (Menu* sub : subs_)
        {
            sub->menu_ = R_INVALID_ID;   // destroyed with its parent
            delete sub;
        }
        if (menu_ != R_INVALID_ID && Rwidget_is_valid(menu_) && Rwidget_get_parent(menu_) == R_INVALID_ID)
            Rwidget_destroy(menu_);
    }

    void Menu::Clear()
    {
        for (Menu* sub : subs_)
        {
            sub->menu_ = R_INVALID_ID;
            delete sub;
        }
        subs_.clear();
        if (menu_ != R_INVALID_ID)
            Rwidget_clear_children(menu_);
        items_ = 0;
    }

    Rid Menu::Item(const std::string& text, std::function<void()> fn, bool enabled, const char* shortcut)
    {
        const Rid item = Rmenu_add_item(menu_, text.c_str());
        if (shortcut)
            Rwidget_set_attribute(item, "Shortcut", shortcut);
        if (!enabled)
            Rwidget_set_enabled(item, R_FALSE);
        if (fn)
            OnMenuItem(item, std::move(fn));
        ++items_;
        return item;
    }

    Rid Menu::Check(const std::string& text, bool checked, std::function<void()> fn, bool enabled)
    {
        const Rid item = Item(text, std::move(fn), enabled);
        Rmenu_item_set_checkable(item, R_TRUE);
        Rmenu_item_set_checked(item, checked ? R_TRUE : R_FALSE);
        return item;
    }

    Rid Menu::Title(const std::string& text)
    {
        const Rid item = Rmenu_add_item(menu_, text.c_str());
        Rwidget_set_enabled(item, R_FALSE);
        ++items_;
        return item;
    }

    void Menu::Separator()
    {
        if (items_ > 0)
            Rmenu_add_separator(menu_);
    }

    Menu& Menu::Sub(const std::string& text)
    {
        const Rid sub = Rmenu_add_menu(menu_, text.c_str());
        Menu* menu = new Menu(window_, sub);
        subs_.push_back(menu);
        ++items_;
        return *menu;
    }

    void Menu::Open()
    {
        float x = 0.f, y = 0.f;
        Rwindow_get_mouse_position(window_, &x, &y);
        Open(x, y);
    }

    void Menu::Open(float x, float y)
    {
        if (items_ > 0)
            Rmenu_open(menu_, window_, x, y);
    }


    // =========================================================================
    // Dialogs
    // =========================================================================

    namespace
    {
        Rid DialogFrame(const std::string& title, const std::string& text, Rid& buttons)
        {
            Rid box = Rwidget_create(R_WIDGET_VBOX);
            Rpanel_set_spacing(box, 12.f);
            Rwidget_set_min_size(box, 360.f, 0.f);
            Rwidget_set_max_size(box, 720.f, -1.f);
            Rwidget_add_class(box, "dialog");

            if (!title.empty())
            {
                Rid heading = Rlabel_create(title.c_str());
                Rwidget_add_class(heading, "dialog-title");
                Rwidget_add_child(box, heading);
            }
            if (!text.empty())
            {
                Rid label = Rlabel_create(text.c_str());
                Rlabel_set_wrap(label, R_TRUE);
                Rwidget_add_child(box, label);
            }
            buttons = Rwidget_create(R_WIDGET_HBOX);
            Rpanel_set_spacing(buttons, 8.f);
            return box;
        }

        struct AskState
        {
            std::function<void(int)> done;
            int result = -1;
        };
    }

    void Message(Rid window, const std::string& title, const std::string& text)
    {
        Ask(window, title, text, { "OK" }, {});
    }

    void Ask(Rid window, const std::string& title, const std::string& text,
             const std::vector<std::string>& labels, std::function<void(int)> done)
    {
        Rid buttons = R_INVALID_ID;
        Rid box = DialogFrame(title, text, buttons);

        Rid spacer = Rwidget_create(R_WIDGET_SPACER);
        Rwidget_add_child(buttons, spacer);
        Rslot_set_fill(spacer, 1.f);

        for (size_t i = 0; i < labels.size(); ++i)
        {
            Rid button = Rbutton_create(labels[i].c_str());
            if (i == 0)
                Rbutton_set_style(button, R_BUTTON_PRIMARY);
            const int index = static_cast<int>(i);
            OnClick(button, [button, index]() { Rdialog_close(button, index + 1); });
            Rwidget_add_child(buttons, button);
        }
        Rwidget_add_child(box, buttons);

        auto* state = new AskState{ std::move(done) };
        Rdialog_open(window, box, [](Rid, int result, void* u)
        {
            std::unique_ptr<AskState> s(static_cast<AskState*>(u));
            if (s->done)
                s->done(result - 1);   // 0 (dismissed) -> -1
        }, state);
    }

    void AskText(Rid window, const std::string& title, const std::string& label, const std::string& initial,
                 std::function<void(const std::string&)> done,
                 std::function<std::string(const std::string&)> check)
    {
        Rid buttons = R_INVALID_ID;
        Rid box = DialogFrame(title, label, buttons);

        Rid input = Rwidget_create(R_WIDGET_TEXT_INPUT);
        Rtext_input_set_text(input, initial.c_str());
        Rwidget_add_child(box, input);

        Rid problem = Rlabel_create("");
        Rwidget_add_class(problem, "dim");
        Rwidget_set_visibility(problem, R_COLLAPSED);
        Rwidget_add_child(box, problem);

        Rid spacer = Rwidget_create(R_WIDGET_SPACER);
        Rwidget_add_child(buttons, spacer);
        Rslot_set_fill(spacer, 1.f);
        Rid ok = Rbutton_create("OK");
        Rbutton_set_style(ok, R_BUTTON_PRIMARY);
        Rid cancel = Rbutton_create("Cancel");
        Rwidget_add_child(buttons, ok);
        Rwidget_add_child(buttons, cancel);
        Rwidget_add_child(box, buttons);

        struct State
        {
            std::function<void(const std::string&)> done;
            std::function<std::string(const std::string&)> check;
            std::string value;
        };
        auto state = std::make_shared<State>(State{ std::move(done), std::move(check), initial });

        auto validate = [state, ok, problem](const std::string& text)
        {
            state->value = text;
            const std::string why = state->check ? state->check(text) : (text.empty() ? std::string(" ") : std::string());
            SetEnabled(ok, why.empty());
            const bool shown = !why.empty() && why != " ";
            SetVisible(problem, shown);
            if (shown)
                SetText(problem, why);
        };
        validate(initial);

        OnText(input, validate);
        OnCommit(input, [state, ok](const std::string& text)
        {
            state->value = text;
            if (Rwidget_is_enabled(ok))
                Rdialog_close(ok, 1);
        });
        OnClick(ok, [ok]() { Rdialog_close(ok, 1); });
        OnClick(cancel, [cancel]() { Rdialog_close(cancel, 0); });

        auto* keep = new std::shared_ptr<State>(state);
        Rdialog_open(window, box, [](Rid, int result, void* u)
        {
            std::unique_ptr<std::shared_ptr<State>> s(static_cast<std::shared_ptr<State>*>(u));
            if (result == 1 && (*s)->done)
                (*s)->done((*s)->value);
        }, keep);

        Rwidget_focus(input);
        Rtext_input_select_all(input);
    }
}


// =============================================================================
// Input
// =============================================================================

namespace lynx::editor::input
{
    namespace
    {
        Rid g_window = R_INVALID_ID;
        bool g_pressed_keys[512] = {};
        bool g_clicked[8] = {};
        bool g_released[8] = {};
        float g_drag[8] = {};
        float g_dx = 0.f, g_dy = 0.f;
        float g_wheel = 0.f;
    }

    void Attach(Rid window)
    {
        g_window = window;
    }

    Rid Window()
    {
        return g_window;
    }

    void Feed(const REvent& event)
    {
        switch (event.type)
        {
        case R_EVENT_KEY_DOWN:
            if (!event.repeat)
            {
                const int key = ToGlfw(event.key);
                if (key > 0 && key < 512)
                    g_pressed_keys[key] = true;
            }
            break;
        case R_EVENT_MOUSE_DOWN:
            if (event.button >= 0 && event.button < 8)
            {
                g_clicked[event.button] = true;
                g_drag[event.button] = 0.f;
            }
            break;
        case R_EVENT_MOUSE_UP:
            if (event.button >= 0 && event.button < 8)
                g_released[event.button] = true;
            break;
        case R_EVENT_MOUSE_MOVE:
            g_dx += event.delta_x;
            g_dy += event.delta_y;
            for (int b = 0; b < 8; ++b)
                if (g_window != R_INVALID_ID && Rwindow_is_mouse_button_down(g_window, static_cast<RMouseButton>(b)))
                    g_drag[b] += std::fabs(event.delta_x) + std::fabs(event.delta_y);
            break;
        case R_EVENT_MOUSE_WHEEL:
            g_wheel += event.wheel_y;
            break;
        default:
            break;
        }
    }

    void NewFrame()
    {
        std::memset(g_pressed_keys, 0, sizeof(g_pressed_keys));
        std::memset(g_clicked, 0, sizeof(g_clicked));
        std::memset(g_released, 0, sizeof(g_released));
        g_dx = g_dy = 0.f;
        g_wheel = 0.f;
    }

    bool Key(int glfw_key)
    {
        if (g_window == R_INVALID_ID)
            return false;
        const RKey key = FromGlfw(glfw_key);
        return key != R_KEY_UNKNOWN && Rwindow_is_key_down(g_window, key);
    }

    bool KeyPressed(int glfw_key)
    {
        return glfw_key > 0 && glfw_key < 512 && g_pressed_keys[glfw_key];
    }

    bool Mouse(int button)
    {
        return g_window != R_INVALID_ID && Rwindow_is_mouse_button_down(g_window, static_cast<RMouseButton>(button));
    }

    bool MouseClicked(int button)
    {
        return button >= 0 && button < 8 && g_clicked[button];
    }

    bool MouseReleased(int button)
    {
        return button >= 0 && button < 8 && g_released[button];
    }

    float MouseDragDistance(int button)
    {
        return button >= 0 && button < 8 ? g_drag[button] : 0.f;
    }

    void MousePosition(double& x, double& y)
    {
        float fx = 0.f, fy = 0.f;
        if (g_window != R_INVALID_ID)
            Rwindow_get_mouse_position(g_window, &fx, &fy);
        x = fx;
        y = fy;
    }

    void MouseDelta(float& dx, float& dy)
    {
        dx = g_dx;
        dy = g_dy;
    }

    float Wheel()
    {
        return g_wheel;
    }

    bool Ctrl()
    {
        return g_window != R_INVALID_ID && (Rwindow_get_modifiers(g_window) & R_MOD_CTRL) != 0;
    }

    bool Shift()
    {
        return g_window != R_INVALID_ID && (Rwindow_get_modifiers(g_window) & R_MOD_SHIFT) != 0;
    }

    bool Alt()
    {
        return g_window != R_INVALID_ID && (Rwindow_get_modifiers(g_window) & R_MOD_ALT) != 0;
    }

    bool WantTextInput()
    {
        return g_window != R_INVALID_ID && Rwindow_wants_text_input(g_window);
    }

    // GLFW and Raven share the printable keys and Escape .. End (256 .. 269).
    int ToGlfw(RKey key)
    {
        const int k = static_cast<int>(key);
        if (k > 0 && k < 256)
            return k;
        if (k >= R_KEY_ESCAPE && k <= R_KEY_END)
            return k;
        switch (key)
        {
        case R_KEY_CAPS_LOCK: return 280;
        case R_KEY_PRINT_SCREEN: return 283;
        case R_KEY_PAUSE: return 284;
        case R_KEY_LEFT_SHIFT: return 340;
        case R_KEY_LEFT_CTRL: return 341;
        case R_KEY_LEFT_ALT: return 342;
        case R_KEY_LEFT_SUPER: return 343;
        case R_KEY_RIGHT_SHIFT: return 344;
        case R_KEY_RIGHT_CTRL: return 345;
        case R_KEY_RIGHT_ALT: return 346;
        case R_KEY_RIGHT_SUPER: return 347;
        case R_KEY_MENU: return 348;
        case R_KEY_KP_DECIMAL: return 330;
        case R_KEY_KP_DIVIDE: return 331;
        case R_KEY_KP_MULTIPLY: return 332;
        case R_KEY_KP_SUBTRACT: return 333;
        case R_KEY_KP_ADD: return 334;
        case R_KEY_KP_ENTER: return 335;
        default: break;
        }
        if (k >= R_KEY_F1 && k <= R_KEY_F12)
            return 290 + (k - R_KEY_F1);
        if (k >= R_KEY_KP_0 && k <= R_KEY_KP_9)
            return 320 + (k - R_KEY_KP_0);
        return 0;
    }

    RKey FromGlfw(int glfw_key)
    {
        if (glfw_key > 0 && glfw_key < 256)
            return static_cast<RKey>(glfw_key);
        if (glfw_key >= 256 && glfw_key <= 269)
            return static_cast<RKey>(glfw_key);
        if (glfw_key >= 290 && glfw_key <= 301)
            return static_cast<RKey>(R_KEY_F1 + (glfw_key - 290));
        if (glfw_key >= 320 && glfw_key <= 329)
            return static_cast<RKey>(R_KEY_KP_0 + (glfw_key - 320));
        switch (glfw_key)
        {
        case 280: return R_KEY_CAPS_LOCK;
        case 283: return R_KEY_PRINT_SCREEN;
        case 284: return R_KEY_PAUSE;
        case 330: return R_KEY_KP_DECIMAL;
        case 331: return R_KEY_KP_DIVIDE;
        case 332: return R_KEY_KP_MULTIPLY;
        case 333: return R_KEY_KP_SUBTRACT;
        case 334: return R_KEY_KP_ADD;
        case 335: return R_KEY_KP_ENTER;
        case 340: return R_KEY_LEFT_SHIFT;
        case 341: return R_KEY_LEFT_CTRL;
        case 342: return R_KEY_LEFT_ALT;
        case 343: return R_KEY_LEFT_SUPER;
        case 344: return R_KEY_RIGHT_SHIFT;
        case 345: return R_KEY_RIGHT_CTRL;
        case 346: return R_KEY_RIGHT_ALT;
        case 347: return R_KEY_RIGHT_SUPER;
        case 348: return R_KEY_MENU;
        default: return R_KEY_UNKNOWN;
        }
    }
}
