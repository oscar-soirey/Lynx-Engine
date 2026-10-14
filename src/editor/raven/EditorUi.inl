// =============================================================================
// Lynx Editor (Raven) : the interface of the editor window.
// -----------------------------------------------------------------------------
// Included at the end of EditorMain.cpp : it uses the editor state (selection,
// brush, camera, level...) directly, as the ImGui code did.
//
//   interface/editor.xml        the window : header, toolbar, dock, status bar
//   interface/panels/*.xml      one file per panel (docked)
//   interface/dialogs/*.xml     the dialogs
//
// The C++ only connects the widgets to the editor : each panel is in its own
// file (Panel*.inl, below). Every frame, editor::UpdateUi() makes the
// interface follow the state (selection, Play, build...) and shows the scene
// image in the Viewport panel.
// =============================================================================

namespace eui = lynx::editor::ui;
namespace ein = lynx::editor::input;

namespace editor_ui
{
    // ---- the window and its panels ------------------------------------------

    struct Shell
    {
        Rid window = R_INVALID_ID;
        Rid dock = R_INVALID_ID;

        Rid viewport_panel = R_INVALID_ID;
        Rid viewport = R_INVALID_ID;
        Rid outliner = R_INVALID_ID;
        Rid paint = R_INVALID_ID;
        Rid content_browser = R_INVALID_ID;
        Rid details = R_INVALID_ID;
        Rid place_actors = R_INVALID_ID;
        Rid color_picking = R_INVALID_ID;
        Rid console = R_INVALID_ID;
        Rid build = R_INVALID_ID;
        Rid options = R_INVALID_ID;
        Rid post_process = R_INVALID_ID;
        Rid lighting2d = R_INVALID_ID;
        Rid voxel_world = R_INVALID_ID;
        Rid camera_shake = R_INVALID_ID;
    };

    static Shell g;

    // A panel and the flag of editor_windows.txt that says if it is open.
    struct PanelEntry
    {
        Rid* panel;
        bool* flag;
        const char* label;
    };

    static bool g_build_panel_open = false;    // game_build::WindowOpen() follows it

    static std::vector<PanelEntry> PanelEntries()
    {
        return {
            { &g.viewport_panel, &editorWindows.viewport, "Viewport" },
            { &g.outliner, &editorWindows.outliner, "Outliner" },
            { &g.place_actors, &editorWindows.placeActors, "Place Actors" },
            { &g.content_browser, &editorWindows.contentBrowser, "Content Browser" },
            { &g.details, &editorWindows.details, "Details" },
            { &g.color_picking, &editorWindows.colorPicking, "Color Picking" },
            { &g.paint, &editorWindows.config, "Paint" },
            { &g.camera_shake, &editorWindows.cameraShake, "Camera Shake" },
            { &g.post_process, &editorWindows.postProcess, "Post Process" },
            { &g.lighting2d, &editorWindows.lighting2D, "2D Lighting" },
            { &g.voxel_world, &editorWindows.voxelWorld, "Voxel World" },
            { &g.console, &editorWindows.console, "Console" },
            { &g.build, &g_build_panel_open, "Build" },
            { &g.options, &showEditorOptions, "Options" },
        };
    }

    // ---- panels : docked in the editor window, or in a window of their own ----------

    // Windows of their own (Options, Post Process...) : open or not.
    static std::set<Rid> g_open_windows;

    static bool IsSeparate(Rid panel)
    {
        return panel != R_INVALID_ID && Rwidget_get_window(panel) != g.window;
    }

    static bool IsPanelOpen(Rid panel)
    {
        if (panel == R_INVALID_ID)
            return false;
        if (IsSeparate(panel))
            return g_open_windows.count(panel) != 0;
        return g.dock != R_INVALID_ID && Rdock_area_is_panel_open(g.dock, panel);
    }

    // Open and its tab in front (a window of its own : open).
    static bool IsPanelShown(Rid panel)
    {
        if (IsSeparate(panel))
            return IsPanelOpen(panel) && !Rwindow_is_minimized(Rwidget_get_window(panel));
        return panel != R_INVALID_ID && g.dock != R_INVALID_ID && Rdock_area_is_panel_shown(g.dock, panel);
    }

    static void OpenPanel(Rid panel)
    {
        if (panel == R_INVALID_ID)
            return;
        if (IsSeparate(panel))
        {
            const Rid window = Rwidget_get_window(panel);
            if (!g_open_windows.count(panel))
            {
                g_open_windows.insert(panel);
                Rwindow_show(window);
            }
            return;
        }
        if (g.dock != R_INVALID_ID && !Rdock_area_is_panel_open(g.dock, panel))
            Rdock_area_open_panel(g.dock, panel);
    }

    static void FrontPanel(Rid panel)
    {
        if (panel == R_INVALID_ID)
            return;
        if (IsSeparate(panel))
        {
            const Rid window = Rwidget_get_window(panel);
            if (Rwindow_is_minimized(window))
                Rwindow_restore(window);
            Rwindow_show(window);
            return;
        }
        if (g.dock != R_INVALID_ID)
            Rdock_area_show_panel(g.dock, panel);
    }

    static void ClosePanel(Rid panel)
    {
        if (panel == R_INVALID_ID)
            return;
        if (IsSeparate(panel))
        {
            if (g_open_windows.erase(panel))
                Rwindow_hide(Rwidget_get_window(panel));
            return;
        }
        if (g.dock != R_INVALID_ID && Rdock_area_is_panel_open(g.dock, panel))
            Rdock_area_hide_panel(g.dock, panel);
    }

    // Shows a panel (opens it where it was, then brings it to the front).
    static void ShowPanel(Rid panel)
    {
        if (panel == R_INVALID_ID)
            return;
        OpenPanel(panel);
        FrontPanel(panel);
        for (const PanelEntry& e : PanelEntries())
            if (*e.panel == panel)
                *e.flag = true;
    }

    static void HidePanel(Rid panel)
    {
        ClosePanel(panel);
    }

    // A window of its own : the x of its title bar hides it.
    static int OnToolWindowClose(Rid window, void*)
    {
        for (Rid panel : std::vector<Rid>(g_open_windows.begin(), g_open_windows.end()))
            if (Rwidget_get_window(panel) == window)
                ClosePanel(panel);
        return R_FALSE;
    }

    // The window of a panel kept apart (windows/<file>) ; its root widget is `name`.
    static Rid LoadToolWindow(const char* file, const char* name)
    {
        const Rid window = eui::LoadWindow(file);
        if (window == R_INVALID_ID)
            return R_INVALID_ID;
        Rwindow_set_close_callback(window, OnToolWindowClose, nullptr);
        return eui::Find(window, name);
    }

    // `widget` is `panel` or inside it.
    static bool IsInside(Rid widget, Rid panel)
    {
        for (Rid w = widget; w != R_INVALID_ID; w = Rwidget_get_parent(w))
            if (w == panel)
                return true;
        return false;
    }

    // The keyboard focus is in this panel.
    static bool HasFocus(Rid panel)
    {
        return panel != R_INVALID_ID && IsInside(Rwindow_get_focused_widget(g.window), panel);
    }

    // ---- small helpers shared by the panels ------------------------------------

    static std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    static std::string Format(const char* format, double value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), format, value);
        return buffer;
    }

    static std::vector<std::string> SplitLines(const char* text)
    {
        std::vector<std::string> out;
        if (!text)
            return out;
        std::string line;
        for (const char* c = text; ; ++c)
        {
            if (*c == '\n' || *c == '\0')
            {
                if (!line.empty())
                    out.push_back(line);
                line.clear();
                if (*c == '\0')
                    break;
            }
            else if (*c != '\r')
                line += *c;
        }
        return out;
    }

    static void ShowLynxie()
    {
        editorWindows.commands = true;
        SaveEditorWindowVisibility();
        lynx::editor::commands_window::ShowLynxie();
    }

    // Icons of the editor classes (lights, sprites...) : the atlas of the engine
    // (resources/actor_icons.png, embedded in lynx.dll too).
    namespace actor_icons
    {
#include "../../gameplay/resources/actor_icons_png.inl"
    }

    static Rid g_actor_icon_atlas = R_INVALID_ID;
    static std::unordered_map<std::string, Rid> g_actor_preview;   // class -> image named after it (assets/)

    // Icon of a class of actor : its engine icon, an image of assets/ named
    // after the class (Player.png), or nothing.
    static bool ActorTypeIcon(const std::string& type_name, Rid& texture, float uv[4])
    {
        uint32_t hrl_texture = HRL_INVALID_ID;
        if (lynx::GetEngineActorIcon(type_name, hrl_texture, uv))
        {
            if (g_actor_icon_atlas == R_INVALID_ID)
            {
                g_actor_icon_atlas = Rtexture_create(reinterpret_cast<const char*>(actor_icons::kActorIconsPng),
                                                     sizeof(actor_icons::kActorIconsPng));
                if (g_actor_icon_atlas != R_INVALID_ID)
                    Rtexture_set_filter(g_actor_icon_atlas, R_FILTER_NEAREST);
            }
            texture = g_actor_icon_atlas;
            return texture != R_INVALID_ID;
        }

        auto it = g_actor_preview.find(type_name);
        if (it == g_actor_preview.end())
        {
            Rid found = R_INVALID_ID;
            const std::string wanted = Lower(type_name);
            std::error_code error;
            const std::filesystem::path root = std::filesystem::current_path() / "assets";
            for (auto entry = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, error);
                 !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error))
            {
                if (!entry->is_regular_file(error) || !IsPreviewImageExtension(entry->path()))
                    continue;
                if (Lower(entry->path().stem().string()) != wanted)
                    continue;
                found = eui::FileTexture(entry->path());
                if (found != R_INVALID_ID)
                    break;
            }
            it = g_actor_preview.emplace(type_name, found).first;
        }
        texture = it->second;
        uv[0] = uv[1] = 0.f;
        uv[2] = uv[3] = 1.f;
        return texture != R_INVALID_ID;
    }

    static std::string ActorLabel(lynx::Actor* actor)
    {
        std::string label = actor->GetTypeName();
        if (!actor->object_id_.empty())
            label += " [" + actor->object_id_ + "]";
        return label;
    }

    // Same class, same properties, a new id ("_Copy"...) : one undo.
    static lynx::Actor* DuplicateActor(lynx::Level* level, lynx::Actor* actor)
    {
        if (!level || !actor)
            return nullptr;

        const std::string type_name = actor->GetTypeName();
        lynx::Actor* copy = level->SpawnActor(type_name.c_str());
        if (!copy)
            return nullptr;

        copy->transform = actor->transform;
        copy->outliner_folder = actor->outliner_folder;

        const auto& source_properties = actor->GetProperties();
        auto& destination_properties = copy->GetProperties();
        for (const auto& [name, source] : source_properties)
        {
            if (name == "object_id_")
                continue;
            auto it = destination_properties.find(name);
            if (it == destination_properties.end() || it->second.GetType() != source.GetType())
                continue;
            const auto& destination = it->second;
            std::visit([&](auto* from)
            {
                using T = std::remove_pointer_t<std::decay_t<decltype(from)>>;
                if (auto to = std::get_if<T*>(&destination.property_member); to && *to && from)
                    **to = *from;
            }, source.property_member);
        }

        const std::string base_id = actor->object_id_.empty() ? type_name : actor->object_id_;
        std::string id = base_id + "_Copy";
        for (int i = 2; level->GetActorFromID(id.c_str()); ++i)
            id = base_id + "_Copy" + std::to_string(i);
        copy->object_id_ = id;

        PushEditorUndo([id]()
        {
            if (lynx::Actor* created = FindEditorActor(id))
            {
                if (editing_actor == created)
                {
                    editing_actor = nullptr;
                    editing_object = HRL_INVALID_ID;
                    HRL_SetGizmoVisible(gizmo, HRL_FALSE);
                }
                editor_level->DestroyActor(created);
            }
        });

        SetActorSelected(copy);
        MarkEditorDirty();
        return copy;
    }

    // Every selected actor (or `actor`), one undo.
    static void DeleteActors(std::vector<lynx::Actor*> actors)
    {
        if (!editor_level || actors.empty())
            return;

        std::vector<EditorActorSnapshot> snapshots;
        for (lynx::Actor* actor : actors)
            if (IsActorAlive(actor))
                snapshots.push_back(CaptureActorSnapshot(actor));

        PushEditorUndo([snapshots]()
        {
            if (!editor_level)
                return;
            std::vector<lynx::Actor*> restored_all;
            for (const EditorActorSnapshot& snapshot : snapshots)
            {
                if (!snapshot.object_id.empty() && FindEditorActor(snapshot.object_id))
                    continue;
                if (lynx::Actor* restored = RestoreActorSnapshot(editor_level, snapshot))
                    restored_all.push_back(restored);
            }
            if (!restored_all.empty())
                SetActorSelection(restored_all, restored_all.back());
        });

        SetActorSelected(nullptr);
        for (lynx::Actor* actor : actors)
            if (IsActorAlive(actor))
                editor_level->DestroyActor(actor);
        MarkEditorDirty();
    }

    // "Convert to voxels..." : not in Raven yet (phase 2).
    static void SpriteVoxelsItem(eui::Menu& menu, lynx::Actor* actor)
    {
        std::string reason;
        const bool can = lynx::editor::sprite_voxels::CanConvert(actor, &reason);
        menu.Item("Convert to voxels...", [actor]() { lynx::editor::sprite_voxels::Open(actor); }, can);
    }
}

namespace editor_ui
{
    // Class of actor dragged from Place Actors (its tree items) : PanelPlaceActors.inl.
    static std::string ClassOfDrag(const char* drag_data);
}

// The panels (they use editor_ui::g and the helpers above).
#include "PanelShell.inl"
#include "PanelViewport.inl"
#include "PanelOutliner.inl"
#include "PanelDetails.inl"
#include "PanelPlaceActors.inl"
#include "PanelContentBrowser.inl"
#include "PanelPaint.inl"
#include "PanelConsole.inl"
#include "PanelTools.inl"


// =============================================================================
// Entry points (declared before main / in namespace editor)
// =============================================================================

static bool StartInterface()
{
    RInitDesc desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.app_name = "Lynx Editor";

    if (!Rinit_ex(&desc))
    {
#ifdef _WIN32
        MessageBoxW(nullptr, L"The interface could not start : Vulkan is not available on this computer "
                             L"(update the graphics driver).", L"Lynx Editor", MB_OK | MB_ICONERROR);
#endif
        std::cerr << "[UI] Rinit failed (Vulkan)\n";
        return false;
    }

    eui::Init();                         // embedded files, <Icon>, themes
    lynx::editor::options::Init();       // theme and scale of the user
    return true;
}

static void StopInterface()
{
    eui::Shutdown();
    Rshutdown();
}

static void SetProjectWindowIcon(Rid window, const std::vector<uint8_t>& png)
{
    if (png.empty())
        return;
    int width = 0, height = 0;
    if (uint8_t* rgba = Rimage_decode(png.data(), png.size(), &width, &height))
    {
        Rwindow_set_icon(window, rgba, width, height);
        Rpixels_free(rgba);
    }
}

static void SetFpsText(double fps)
{
    editor_ui::shell::SetFps(fps);
}

static void SetGameCursorHidden(bool hidden)
{
    if (editor_ui::g.viewport != R_INVALID_ID)
        Rwidget_set_cursor(editor_ui::g.viewport, hidden ? R_CURSOR_NONE : R_CURSOR_ARROW);
}

namespace editor
{
    static void InitUi(Rid window)
    {
        using namespace editor_ui;
        g.window = window;
        ein::Attach(window);

        g.dock = eui::Find(window, "dock");
        g.viewport_panel = eui::Find(window, "viewport.panel");
        g.viewport = eui::Find(window, "viewport");
        g.outliner = eui::Find(window, "outliner");
        g.paint = eui::Find(window, "paint");
        g.content_browser = eui::Find(window, "content_browser");
        g.details = eui::Find(window, "details");
        g.place_actors = eui::Find(window, "place_actors");
        g.color_picking = eui::Find(window, "color_picking");
        g.console = eui::Find(window, "console");
        g.build = eui::Find(window, "build");
        // Windows of their own.
        g.options = LoadToolWindow("windows/options.xml", "options");
        g.post_process = LoadToolWindow("windows/post_process.xml", "post_process");
        g.lighting2d = LoadToolWindow("windows/lighting2d.xml", "lighting2d");
        g.voxel_world = LoadToolWindow("windows/voxel_world.xml", "voxel_world");
        g.camera_shake = LoadToolWindow("windows/camera_shake.xml", "camera_shake");

        shell::Init();
        viewport_panel::Init();
        outliner::Init();
        details::Init();
        place_actors::Init();
        content_browser::Init();
        paint::Init();
        console::Init();
        tools::Init();

        // Layout of the project, then the panels of editor_windows.txt.
        shell::LoadLayout();
    }

    static void UpdateUi(float dt)
    {
        using namespace editor_ui;
        if (g.window == R_INVALID_ID || !Rwindow_is_valid(g.window))
            return;

        shell::Update(dt);
        viewport_panel::Update(dt);

        // Every panel stays shown and follows the level, Play included.
        outliner::Update();
        details::Update();
        place_actors::Update();
        content_browser::Update();
        paint::Update();
        tools::Update(dt);
        console::Update();

        // Windows of their own (Git, Profiler, Ship Game, Convert to voxels).
        git_window::Frame(editor_dirty, []() { SaveEditor(); });
        lynx::editor::profiler_window::Update();
        lynx::editor::ship_game::SetProject(currentProject.root, currentProject.name);
        lynx::editor::ship_game::Draw([]() { SaveEditor(); });
        lynx::editor::sprite_voxels::Draw(::SpriteVoxelHost());
    }

    static void ShutdownUi()
    {
        editor_ui::shell::SaveLayout();
        editor_ui::viewport_panel::Shutdown();
    }
}
