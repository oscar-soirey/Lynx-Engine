// =============================================================================
// Content Browser panel : the files of assets/.
//   header : Lynxie, Assets / breadcrumbs, back / forward / up, search, size, + New
//   tiles  : click / Ctrl / Shift / rubber band : selection ; double click : open
//            (folder, level, script...) ; drag : into the viewport (sprites,
//            sounds) or onto a path field ; files from Explorer : copied here
//   right click : the operations of ContentBrowserActions.inl (rename, delete...)
//   keys   : Del, F2, Ctrl+D / C / X / V / A, Enter, Backspace (parent folder),
//            mouse back / forward buttons, Ctrl + wheel (tile size)
// Several views of it : the panel of the level, and the Content Drawer
// (ContentDrawer.inl, Left Ctrl + Space). They show the same folder and the
// same selection ; each has its search, breadcrumbs and tiles (View).
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::content_browser
{
    namespace cba = content_browser_actions;
    namespace stdfs = std::filesystem;

    static const char* const kKindNames[] = {
        "Folder", "File", "Text", "Script", "Python", "Cpp", "Data", "Level", "Image", "Sound", "Font",
        "Widget", "AnimGraph", "BehaviorTree", "Voxels", "Config", "Code", "Markup",
    };

    struct Tile
    {
        stdfs::path path;
        std::string name;
        std::string detail;
        bool is_directory = false;
        lynx::editor::file_icons::Kind kind = lynx::editor::file_icons::Kind::File;
    };

    // One view of the Content Browser : its widgets and what they show.
    struct View
    {
        Rid root = R_INVALID_ID;
        Rid tiles = R_INVALID_ID;
        Rid search = R_INVALID_ID;
        Rid crumbs = R_INVALID_ID;
        Rid count = R_INVALID_ID;
        Rid size = R_INVALID_ID;
        Rid back = R_INVALID_ID;
        Rid forward = R_INVALID_ID;
        Rid up = R_INVALID_ID;
        Rid root_button = R_INVALID_ID;

        std::vector<Tile> shown;
        stdfs::path shown_folder;
        std::string shown_search = "\x01";
        stdfs::path crumbs_folder = "\x01";
        std::vector<stdfs::path> shown_selection;
    };

    static std::vector<std::unique_ptr<View>> g_views;
    static AsyncFolderList g_list;
    static bool g_refresh_now = true;
    static std::unique_ptr<eui::Menu> g_menu;

    // ---- tiles -------------------------------------------------------------------

    static void TileItem(Rid, int index, RTileItem* item, void* user)
    {
        const View& v = *static_cast<const View*>(user);
        if (index < 0 || index >= static_cast<int>(v.shown.size()))
            return;
        const Tile& tile = v.shown[static_cast<size_t>(index)];
        item->text = tile.name.c_str();
        // Images : the picture itself (loaded in the background) ; else the icon of the type.
        if (tile.kind == lynx::editor::file_icons::Kind::Image)
        {
            const Rid texture = eui::FileTexture(tile.path);
            if (texture != R_INVALID_ID)
            {
                item->icon = texture;
                item->uv[0] = item->uv[1] = 0.f;
                item->uv[2] = item->uv[3] = 1.f;
                item->icon_radius = 3.f;
                return;
            }
        }
        item->icon = eui::FileIconTexture();
        eui::FileIconRegion(tile.kind, item->uv);
    }

    // The size of the tiles, in every view (and their sliders).
    static void ApplyTileSize()
    {
        const float scale = content_browser_thumb_scale;
        const float width = std::floor(128.f * scale * 0.75f);
        const float image = std::floor(92.f * scale * 0.75f);
        for (auto& v : g_views)
        {
            Rtile_view_set_tile_size(v->tiles, width, image + 30.f, image);
            if (std::fabs(Rslider_get_value(v->size) - scale * 100.f) > 0.5f)
                Rslider_set_value(v->size, scale * 100.f);
        }
    }

    // The selection of ContentBrowserActions shown on the tiles.
    static void ShowSelection(View& v)
    {
        if (cba::state.selection == v.shown_selection)
            return;
        v.shown_selection = cba::state.selection;
        Rtile_view_clear_selection(v.tiles);
        for (size_t i = 0; i < v.shown.size(); ++i)
            if (cba::IsSelected(v.shown[i].path))
                Rtile_view_set_selected(v.tiles, static_cast<int>(i), R_TRUE);
    }

    static void Refill(View& v, const std::vector<stdfs::directory_entry>& entries)
    {
        const char* text = Rtext_input_get_text(v.search);
        const std::string needle = Lower(text ? text : "");
        v.shown.clear();
        v.shown.reserve(entries.size());
        for (const auto& entry : entries)
        {
            Tile tile;
            tile.path = entry.path();
            tile.name = entry.path().filename().string();
            if (!tile.name.empty() && tile.name[0] == '.')
                continue;   // .trash, .git...
            if (!needle.empty() && Lower(tile.name).find(needle) == std::string::npos)
                continue;
            std::error_code error;
            tile.is_directory = entry.is_directory(error);
            tile.kind = lynx::editor::file_icons::ForFile(tile.path, tile.is_directory);
            v.shown.push_back(std::move(tile));
        }
        Rtile_view_set_items(v.tiles, static_cast<int>(v.shown.size()), TileItem, &v);
        v.shown_selection.clear();
        v.shown_selection.push_back("\x01");   // shown again
        ShowSelection(v);

        const size_t n = v.shown.size();
        eui::SetText(v.count, std::to_string(n) + (n == 1 ? " item" : " items"));
        if (entries.empty())
            Rwidget_set_tooltip(v.tiles, "");
    }

    static std::vector<stdfs::path> SelectedPaths(const View& v)
    {
        std::vector<stdfs::path> out;
        const int count = Rtile_view_get_selection_count(v.tiles);
        for (int i = 0; i < count; ++i)
        {
            const int index = Rtile_view_get_selection(v.tiles, i);
            if (index >= 0 && index < static_cast<int>(v.shown.size()))
                out.push_back(v.shown[static_cast<size_t>(index)].path);
        }
        return out;
    }

    // ---- header ------------------------------------------------------------------

    static void Breadcrumbs(View& v)
    {
        if (v.crumbs_folder == content_browser_current_path)
            return;
        v.crumbs_folder = content_browser_current_path;
        Rwidget_clear_children(v.crumbs);

        std::error_code error;
        stdfs::path relative;
        if (IsPathInside(content_browser_current_path, content_browser_root))
            relative = stdfs::relative(content_browser_current_path, content_browser_root, error);
        stdfs::path at;
        for (const auto& part : relative)
        {
            if (part.empty() || part == ".")
                continue;
            at /= part;
            Rid slash = Rlabel_create("/");
            Rwidget_add_class(slash, "dim");
            Rwidget_add_child(v.crumbs, slash);
            Rid button = Rbutton_create(part.string().c_str());
            Rwidget_add_class(button, "tool");
            Rwidget_set_focusable(button, R_FALSE);
            Rwidget_add_child(v.crumbs, button);
            const stdfs::path target = content_browser_root / at;
            eui::OnClick(button, [target]() { content_browser_current_path = target; });
            // Files dropped on a part of the path : moved there.
        }
        eui::SetTooltip(v.root_button, content_browser_root.generic_string());
    }

    // ---- "+ New" ---------------------------------------------------------------------

    static void NewDialog(const stdfs::path& folder)
    {
        Rid box = eui::Load("dialogs/new_asset.xml");
        if (box == R_INVALID_ID)
            return;
        Rid list = eui::Find(box, "new.list");
        Rid search = eui::Find(box, "new.search");
        Rid paste = eui::Find(box, "new.paste");

        auto entries = std::make_shared<std::vector<cba::NewEntry>>(cba::NewEntries(folder));
        auto fill = std::make_shared<std::function<void()>>();
        *fill = [entries, list, search, box]()
        {
            Rwidget_clear_children(list);
            const char* text = Rtext_input_get_text(search);
            const std::string needle = Lower(text ? text : "");
            std::string section;
            for (size_t i = 0; i < entries->size(); ++i)
            {
                const cba::NewEntry& entry = (*entries)[i];
                if (!needle.empty() && Lower(entry.label + " " + entry.description + " " + entry.section).find(needle) == std::string::npos)
                    continue;
                if (entry.section != section)
                {
                    section = entry.section;
                    const std::string xml = "<ListHeading Text=\"" + section + "\"/>";
                    if (Rid heading = Rwidget_create_from_xml(xml.c_str(), xml.size()); heading != R_INVALID_ID)
                        Rwidget_add_child(list, heading);
                }
                auto escape = [](const std::string& s)
                {
                    std::string out;
                    for (char c : s)
                    {
                        if (c == '"') out += "&quot;";
                        else if (c == '&') out += "&amp;";
                        else if (c == '<') out += "&lt;";
                        else out += c;
                    }
                    return out;
                };
                const std::string xml = std::string("<NewAssetRow Kind=\"") + kKindNames[static_cast<int>(entry.icon)] +
                                        "\" Text=\"" + escape(entry.label) + "\" Detail=\"" + escape(entry.description) + "\"/>";
                Rid row = Rwidget_create_from_xml(xml.c_str(), xml.size());
                if (row == R_INVALID_ID)
                    continue;
                Rwidget_add_child(list, row);
                eui::OnClick(row, [entries, i, box]()
                {
                    const auto create = (*entries)[i].create;
                    Rdialog_close(box, 1);
                    if (create)
                        create();   // asks the name (cba::state.open_name_popup)
                });
            }
        };
        eui::OnText(search, [fill](const std::string&) { (*fill)(); });
        eui::SetEnabled(paste, !cba::state.clipboard.empty());
        eui::OnClick(paste, [folder, box]()
        {
            Rdialog_close(box, 1);
            cba::Paste(folder);
            g_refresh_now = true;
        });
        eui::OnClick(eui::Find(box, "new.cancel"), [box]() { Rdialog_close(box, 0); });
        eui::SetText(eui::Find(box, "new.title"), "Add to " + (folder == content_browser_root
            ? std::string("assets/")
            : "assets/" + folder.lexically_relative(content_browser_root).generic_string()));
        (*fill)();
        Rdialog_open(g.window, box, nullptr, nullptr);
        Rwidget_focus(search);
    }

    // ---- menus -----------------------------------------------------------------------

    static void ItemMenu(const View& v, int index)
    {
        if (index < 0 || index >= static_cast<int>(v.shown.size()))
            return;
        const Tile tile = v.shown[static_cast<size_t>(index)];
        const stdfs::path path = tile.path;

        // Right click on an item of the selection keeps the whole selection.
        if (!cba::IsSelected(path))
            cba::SelectOnly(path);
        else
            cba::state.selected = path;
        std::vector<stdfs::path> targets = cba::SelectedPaths();
        if (std::find(targets.begin(), targets.end(), path) == targets.end())
            targets = { path };

        g_menu = std::make_unique<eui::Menu>(g.window);
        eui::Menu& menu = *g_menu;
        const bool is_level = cba::IsLevel(path);
        menu.Item(tile.is_directory ? "Open folder" : is_level ? "Open level" : "Open",
                  [path]() { cba::OpenEntry(path); },
                  !(is_level && cba::AssetsRelative(path) == current_level_file), "Enter");
        if (is_level)
        {
            const bool startup = cba::AssetsRelative(path) == cba::StartupLevel();
            menu.Check("Startup level of the game", startup, [path]() { cba::SetStartupLevel(path); }, !startup);
            menu.Title("Voxel world : " + cba::LinkedVoxelWorld(path).filename().string());
        }
        menu.Separator();
        menu.Item("Rename", [path]() { cba::BeginRename(path); }, true, "F2");
        menu.Item("Duplicate", [targets]() { cba::Duplicate(targets); g_refresh_now = true; }, true, "Ctrl+D");
        menu.Item("Copy", [targets]() { cba::Copy(targets, false); }, true, "Ctrl+C");
        menu.Item("Cut", [targets]() { cba::Copy(targets, true); }, true, "Ctrl+X");
        if (tile.is_directory)
        {
            menu.Item("Paste into", [path]() { cba::Paste(path); g_refresh_now = true; }, !cba::state.clipboard.empty());
            menu.Separator();
            menu.Item("New...", [path]() { NewDialog(path); });
        }
        menu.Separator();
        menu.Item("Copy path (relative to assets)", [path]()
        {
            std::error_code error;
            const std::string relative = stdfs::relative(path, content_browser_root, error).generic_string();
            Rclipboard_set_text(error ? path.generic_string().c_str() : relative.c_str());
        });
        menu.Item("Show in Explorer", [path]() { cba::OpenInExplorer(path, true); });
        menu.Separator();
        menu.Item("Delete", [targets]()
        {
            cba::state.delete_targets = targets;
            cba::state.open_delete_popup = true;
        }, true, "Del");
        menu.Open();
    }

    static void BackgroundMenu()
    {
        g_menu = std::make_unique<eui::Menu>(g.window);
        eui::Menu& menu = *g_menu;
        const stdfs::path folder = content_browser_current_path;
        menu.Title("Add to this folder");
        // The kinds of files, as in "+ New" (a sub menu per section).
        auto entries = std::make_shared<std::vector<cba::NewEntry>>(cba::NewEntries(folder));
        std::string section;
        eui::Menu* sub = nullptr;
        for (size_t i = 0; i < entries->size(); ++i)
        {
            const cba::NewEntry& entry = (*entries)[i];
            if (i == 0)
            {
                menu.Item(entry.label, [entries]() { (*entries)[0].create(); });
                continue;
            }
            if (entry.section != section || !sub)
            {
                section = entry.section;
                sub = &menu.Sub(section);
            }
            sub->Item(entry.label, [entries, i]() { (*entries)[i].create(); });
        }
        menu.Item("More...", [folder]() { NewDialog(folder); });
        menu.Separator();
        menu.Item("Paste", [folder]() { cba::Paste(folder); g_refresh_now = true; }, !cba::state.clipboard.empty(), "Ctrl+V");
        menu.Item("Open in Explorer", [folder]() { cba::OpenInExplorer(folder, false); });
        menu.Open();
    }

    // ---- keys --------------------------------------------------------------------------

    static bool OnKey(View& v, const REvent& event)
    {
        if (event.type == R_EVENT_MOUSE_WHEEL && (event.modifiers & R_MOD_CTRL))
        {
            content_browser_thumb_scale = std::clamp(
                content_browser_thumb_scale * (event.wheel_y > 0.f ? 1.1f : 1.f / 1.1f), 0.5f, 2.5f);
            ApplyTileSize();
            SaveContentBrowserThumbScale();
            return true;
        }
        (void)v;
        if (event.type != R_EVENT_KEY_DOWN || ein::WantTextInput())
            return false;

        const bool ctrl = (event.modifiers & R_MOD_CTRL) != 0;
        std::error_code error;
        const std::vector<stdfs::path> targets = cba::SelectedPaths();
        const bool has_selection = !targets.empty();
        const bool has_primary = !cba::state.selected.empty() && stdfs::exists(cba::state.selected, error);

        if (has_selection && event.key == R_KEY_DELETE)
        {
            cba::state.delete_targets = targets;
            cba::state.open_delete_popup = true;
        }
        else if (has_primary && event.key == R_KEY_F2)
            cba::BeginRename(cba::state.selected);
        else if (has_selection && ctrl && event.key == R_KEY_D)
            cba::Duplicate(targets);
        else if (has_selection && ctrl && event.key == R_KEY_C)
            cba::Copy(targets, false);
        else if (has_selection && ctrl && event.key == R_KEY_X)
            cba::Copy(targets, true);
        else if (ctrl && event.key == R_KEY_V && !cba::state.clipboard.empty())
            cba::Paste(content_browser_current_path);
        else if (has_primary && event.key == R_KEY_ENTER)
            cba::OpenEntry(cba::state.selected);
        else if (event.key == R_KEY_BACKSPACE && content_browser_current_path != content_browser_root)
            content_browser_current_path = content_browser_current_path.parent_path();
        else
            return false;
        g_refresh_now = true;
        return true;
    }

    // Files dragged from Explorer : copied into the folder under the pointer
    // (a folder tile) or the current one.
    static int OnDrop(Rid, const RDropEvent* drop, void* user)
    {
        View& v = *static_cast<View*>(user);
        if (drop->drag_type)
            return R_FALSE;   // a drag of the editor : not a copy
        if (drop->path_count <= 0)
            return R_FALSE;
        const int index = Rtile_view_get_item_at(v.tiles, drop->local_x, drop->local_y);
        const bool on_folder = index >= 0 && index < static_cast<int>(v.shown.size()) && v.shown[static_cast<size_t>(index)].is_directory;
        if (drop->phase != R_DROP_DROPPED)
        {
            Rtile_view_set_drop_highlight(v.tiles, on_folder && drop->phase != R_DROP_LEAVE ? index : -1);
            return R_TRUE;
        }
        Rtile_view_set_drop_highlight(v.tiles, -1);

        const stdfs::path folder = on_folder ? v.shown[static_cast<size_t>(index)].path : content_browser_current_path;
        const stdfs::path keep = content_browser_current_path;
        content_browser_current_path = folder;
        pending_explorer_drop.paths.clear();
        for (int i = 0; i < drop->path_count; ++i)
            if (drop->paths[i])
                pending_explorer_drop.paths.emplace_back(stdfs::path(reinterpret_cast<const char8_t*>(drop->paths[i])));
        CopyExplorerFilesToContentBrowser();
        content_browser_current_path = keep;
        g_refresh_now = true;
        return R_TRUE;
    }

    // ---- dialogs of ContentBrowserActions -------------------------------------------------

    static void Dialogs()
    {
        if (cba::state.open_name_popup)
        {
            cba::state.open_name_popup = false;
            const char* title =
                cba::state.name_action == cba::NameAction::Rename ? "Rename" :
                cba::state.name_action == cba::NameAction::NewFolder ? "New folder" : "New file";
            std::string label = "in " + cba::state.name_folder.generic_string();
            if (cba::state.name_action == cba::NameAction::NewFile && cba::state.plugin_file_kind < 0)
                label = std::string(cba::kFileKinds[static_cast<int>(cba::state.new_file_kind)].label) + "  " + label;
            eui::AskText(g.window, title, label, cba::state.name,
                [](const std::string& name)
                {
                    std::snprintf(cba::state.name, sizeof(cba::state.name), "%s", name.c_str());
                    if (!cba::ApplyName())
                    {
                        // Refused (the file appeared meanwhile...) : why.
                        cba::state.open_error_popup = true;
                        cba::state.error = cba::state.name_error;
                    }
                    cba::state.name_action = cba::NameAction::None;
                    g_refresh_now = true;
                },
                [](const std::string& name) -> std::string
                {
                    std::string why;
                    if (!cba::IsValidName(name, why))
                        return why;
                    std::error_code error;
                    const stdfs::path destination = cba::state.name_folder / name;
                    if (cba::state.name_action == cba::NameAction::Rename && destination == cba::state.rename_target)
                        return "";
                    if (stdfs::exists(destination, error) &&
                        !(cba::state.name_action == cba::NameAction::Rename &&
                          stdfs::equivalent(destination, cba::state.rename_target, error)))
                        return "\"" + name + "\" already exists.";
                    return "";
                });
        }

        if (cba::state.open_delete_popup)
        {
            cba::state.open_delete_popup = false;
            std::string text;
            for (const stdfs::path& target : cba::state.delete_targets)
                text += "  " + target.generic_string() + "\n";
            text += "\nMoved to .lynx/trash/ (can be restored from there).";
            eui::Ask(g.window, "Delete?", text, { "Delete", "Cancel" }, [](int choice)
            {
                if (choice == 0)
                    cba::Delete(cba::state.delete_targets);
                cba::state.delete_targets.clear();
                g_refresh_now = true;
            });
        }

        if (cba::state.open_error_popup)
        {
            cba::state.open_error_popup = false;
            eui::Message(g.window, "Content Browser", cba::state.error);
        }
    }

    // ---- binding -------------------------------------------------------------------------

    // A view of the Content Browser on the widgets of panels/content_browser.xml under `root`.
    static View* AddView(Rid root)
    {
        auto view = std::make_unique<View>();
        View* v = view.get();
        auto find = [root](const char* name) { return eui::Find(root, name); };
        v->root = root;
        v->tiles = find("cb.tiles");
        v->search = find("cb.search");
        v->crumbs = find("cb.breadcrumbs");
        v->count = find("cb.count");
        v->size = find("cb.size");
        v->back = find("cb.back");
        v->forward = find("cb.forward");
        v->up = find("cb.up");
        v->root_button = find("cb.root");
        g_views.push_back(std::move(view));

        Rimage_set_texture(find("cb.lynxie.icon"), eui::Texture("icons/lynxie.png"));
        eui::OnClick(find("cb.lynxie"), []() { ShowLynxie(); });
        eui::OnClick(v->root_button, []() { content_browser_current_path = content_browser_root; });
        eui::OnClick(v->back, []() { ContentBrowserGo(-1); });
        eui::OnClick(v->forward, []() { ContentBrowserGo(+1); });
        eui::OnClick(v->up, []()
        {
            const stdfs::path parent = content_browser_current_path.parent_path();
            if (content_browser_current_path != content_browser_root && IsPathInside(parent, content_browser_root))
                content_browser_current_path = parent;
        });
        eui::OnClick(find("cb.new"), []() { NewDialog(content_browser_current_path); });

        if (!content_browser_thumb_loaded)
            LoadContentBrowserThumbScale();
        Rslider_set_value(v->size, content_browser_thumb_scale * 100.f);
        eui::OnValue(v->size, [](double value)
        {
            const float scale = std::clamp(static_cast<float>(value) / 100.f, 0.5f, 2.5f);
            if (std::fabs(scale - content_browser_thumb_scale) < 0.004f)
                return;
            content_browser_thumb_scale = scale;
            ApplyTileSize();
            SaveContentBrowserThumbScale();
        });
        ApplyTileSize();

        Rtile_view_set_drag_type(v->tiles, eui::kDragAsset);
        eui::OnTileDragData(v->tiles, [v]()
        {
            std::string data;
            for (const stdfs::path& path : SelectedPaths(*v))
            {
                std::error_code error;
                if (stdfs::is_directory(path, error))
                    continue;   // folders do not go into the level
                if (!data.empty())
                    data += '\n';
                data += path.generic_string();
            }
            return data;
        });
        eui::OnTileSelect(v->tiles, [v](int index)
        {
            std::vector<stdfs::path> paths = SelectedPaths(*v);
            const stdfs::path primary = index >= 0 && index < static_cast<int>(v->shown.size())
                ? v->shown[static_cast<size_t>(index)].path
                : (paths.empty() ? stdfs::path() : paths.back());
            if (paths.empty())
                cba::ClearSelection();
            else
                cba::SetSelection(paths, primary);
            v->shown_selection = cba::state.selection;
        });
        eui::OnTileActivate(v->tiles, [v](int index)
        {
            if (index >= 0 && index < static_cast<int>(v->shown.size()))
                cba::OpenEntry(v->shown[static_cast<size_t>(index)].path);
        });
        eui::OnTileContext(v->tiles, [v](int index, float, float)
        {
            if (index >= 0)
                ItemMenu(*v, index);
            else
                BackgroundMenu();
        });
        eui::OnEvent(v->tiles, [v](const REvent& event) { return OnKey(*v, event); });
        Rwidget_set_drop_callback(v->tiles, OnDrop, v);

        // Back / forward buttons of the mouse anywhere over the view.
        eui::OnEvent(root, [](const REvent& event)
        {
            if (event.type != R_EVENT_MOUSE_DOWN)
                return false;
            if (event.button == R_MOUSE_X1) { ContentBrowserGo(-1); return true; }
            if (event.button == R_MOUSE_X2) { ContentBrowserGo(+1); return true; }
            return false;
        });
        eui::OnText(v->search, [](const std::string&) { g_refresh_now = true; });
        g_refresh_now = true;
        return v;
    }

    static void Init()
    {
        AddView(g.content_browser);
    }

    static void UpdateView(View& v, const std::vector<stdfs::directory_entry>& entries)
    {
        Breadcrumbs(v);
        eui::SetEnabled(v.back, !content_browser_back.empty());
        eui::SetEnabled(v.forward, !content_browser_forward.empty());
        eui::SetEnabled(v.up, content_browser_current_path != content_browser_root);

        const char* text = Rtext_input_get_text(v.search);
        const std::string search = text ? text : "";

        bool differ = g_refresh_now || v.shown_folder != content_browser_current_path || search != v.shown_search;
        if (!differ)
        {
            // A background listing : the same files ?
            const std::string needle = Lower(search);
            size_t k = 0;
            for (const auto& entry : entries)
            {
                const std::string name = entry.path().filename().string();
                if (!name.empty() && name[0] == '.')
                    continue;
                if (!needle.empty() && Lower(name).find(needle) == std::string::npos)
                    continue;
                if (k >= v.shown.size() || v.shown[k].path != entry.path())
                {
                    differ = true;
                    break;
                }
                ++k;
            }
            differ = differ || k != v.shown.size();
        }
        if (differ)
        {
            v.shown_folder = content_browser_current_path;
            v.shown_search = search;
            Refill(v, entries);
        }
        ShowSelection(v);
    }

    static void Update()
    {
        ContentBrowserTrackHistory();

        // Listed when the folder changes or after an action, refreshed twice a
        // second in the background (AsyncFolderList) ; every view shows it.
        static stdfs::path listed_folder = "\x01";
        const auto& entries = g_list.Get(content_browser_current_path, glfwGetTime(), 0.5, g_refresh_now);
        if (listed_folder != content_browser_current_path)
        {
            if (!listed_folder.empty() && listed_folder != "\x01")
                cba::ClearSelection();
            listed_folder = content_browser_current_path;
        }
        for (auto& v : g_views)
            UpdateView(*v, entries);
        g_refresh_now = false;
        Dialogs();
    }
}
