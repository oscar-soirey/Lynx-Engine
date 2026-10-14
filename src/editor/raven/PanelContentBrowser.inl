// =============================================================================
// Content Browser panel : the files of assets/.
//   header : Lynxie, Assets / breadcrumbs, back / forward / up, search, size, + New
//   tiles  : click / Ctrl / Shift / rubber band : selection ; double click : open
//            (folder, level, script...) ; drag : into the viewport (sprites,
//            sounds) or onto a path field ; files from Explorer : copied here
//   right click : the operations of ContentBrowserActions.inl (rename, delete...)
//   keys   : Del, F2, Ctrl+D / C / X / V / A, Enter, Backspace (parent folder),
//            mouse back / forward buttons, Ctrl + wheel (tile size)
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

    static Rid g_tiles = R_INVALID_ID;
    static Rid g_search = R_INVALID_ID;
    static Rid g_crumbs = R_INVALID_ID;
    static Rid g_count = R_INVALID_ID;
    static Rid g_size = R_INVALID_ID;
    static Rid g_back = R_INVALID_ID;
    static Rid g_forward = R_INVALID_ID;
    static Rid g_up = R_INVALID_ID;

    static AsyncFolderList g_list;
    static std::vector<Tile> g_shown;
    static stdfs::path g_shown_folder;
    static std::string g_shown_search = "\x01";
    static stdfs::path g_crumbs_folder = "\x01";
    static bool g_refresh_now = true;
    static std::vector<stdfs::path> g_shown_selection;
    static std::unique_ptr<eui::Menu> g_menu;

    // ---- tiles -------------------------------------------------------------------

    static void TileItem(Rid, int index, RTileItem* item, void*)
    {
        if (index < 0 || index >= static_cast<int>(g_shown.size()))
            return;
        const Tile& tile = g_shown[static_cast<size_t>(index)];
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

    static void ApplyTileSize()
    {
        const float scale = content_browser_thumb_scale;
        const float width = std::floor(128.f * scale * 0.75f);
        const float image = std::floor(92.f * scale * 0.75f);
        Rtile_view_set_tile_size(g_tiles, width, image + 30.f, image);
    }

    // The selection of ContentBrowserActions shown on the tiles.
    static void ShowSelection()
    {
        if (cba::state.selection == g_shown_selection)
            return;
        g_shown_selection = cba::state.selection;
        Rtile_view_clear_selection(g_tiles);
        for (size_t i = 0; i < g_shown.size(); ++i)
            if (cba::IsSelected(g_shown[i].path))
                Rtile_view_set_selected(g_tiles, static_cast<int>(i), R_TRUE);
    }

    static void Refill(const std::vector<stdfs::directory_entry>& entries)
    {
        const char* text = Rtext_input_get_text(g_search);
        const std::string needle = Lower(text ? text : "");
        g_shown.clear();
        g_shown.reserve(entries.size());
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
            g_shown.push_back(std::move(tile));
        }
        Rtile_view_set_items(g_tiles, static_cast<int>(g_shown.size()), TileItem, nullptr);
        g_shown_selection.clear();
        g_shown_selection.push_back("\x01");   // shown again
        ShowSelection();

        const size_t n = g_shown.size();
        eui::SetText(g_count, std::to_string(n) + (n == 1 ? " item" : " items"));
        if (entries.empty())
            Rwidget_set_tooltip(g_tiles, "");
    }

    static std::vector<stdfs::path> SelectedPaths()
    {
        std::vector<stdfs::path> out;
        const int count = Rtile_view_get_selection_count(g_tiles);
        for (int i = 0; i < count; ++i)
        {
            const int index = Rtile_view_get_selection(g_tiles, i);
            if (index >= 0 && index < static_cast<int>(g_shown.size()))
                out.push_back(g_shown[static_cast<size_t>(index)].path);
        }
        return out;
    }

    // ---- header ------------------------------------------------------------------

    static void Breadcrumbs()
    {
        if (g_crumbs_folder == content_browser_current_path)
            return;
        g_crumbs_folder = content_browser_current_path;
        Rwidget_clear_children(g_crumbs);

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
            Rwidget_add_child(g_crumbs, slash);
            Rid button = Rbutton_create(part.string().c_str());
            Rwidget_add_class(button, "tool");
            Rwidget_set_focusable(button, R_FALSE);
            Rwidget_add_child(g_crumbs, button);
            const stdfs::path target = content_browser_root / at;
            eui::OnClick(button, [target]() { content_browser_current_path = target; });
            // Files dropped on a part of the path : moved there.
        }
        eui::SetTooltip(eui::Find(g.window, "cb.root"), content_browser_root.generic_string());
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

    static void ItemMenu(int index)
    {
        if (index < 0 || index >= static_cast<int>(g_shown.size()))
            return;
        const Tile tile = g_shown[static_cast<size_t>(index)];
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

    static bool OnKey(const REvent& event)
    {
        if (event.type == R_EVENT_MOUSE_WHEEL && (event.modifiers & R_MOD_CTRL))
        {
            content_browser_thumb_scale = std::clamp(
                content_browser_thumb_scale * (event.wheel_y > 0.f ? 1.1f : 1.f / 1.1f), 0.5f, 2.5f);
            Rslider_set_value(g_size, content_browser_thumb_scale * 100.f);
            ApplyTileSize();
            SaveContentBrowserThumbScale();
            return true;
        }
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
    static int OnDrop(Rid, const RDropEvent* drop, void*)
    {
        if (drop->drag_type)
            return R_FALSE;   // a drag of the editor : not a copy
        if (drop->path_count <= 0)
            return R_FALSE;
        const int index = Rtile_view_get_item_at(g_tiles, drop->local_x, drop->local_y);
        const bool on_folder = index >= 0 && index < static_cast<int>(g_shown.size()) && g_shown[static_cast<size_t>(index)].is_directory;
        if (drop->phase != R_DROP_DROPPED)
        {
            Rtile_view_set_drop_highlight(g_tiles, on_folder && drop->phase != R_DROP_LEAVE ? index : -1);
            return R_TRUE;
        }
        Rtile_view_set_drop_highlight(g_tiles, -1);

        const stdfs::path folder = on_folder ? g_shown[static_cast<size_t>(index)].path : content_browser_current_path;
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

    static void Init()
    {
        g_tiles = eui::Find(g.window, "cb.tiles");
        g_search = eui::Find(g.window, "cb.search");
        g_crumbs = eui::Find(g.window, "cb.breadcrumbs");
        g_count = eui::Find(g.window, "cb.count");
        g_size = eui::Find(g.window, "cb.size");
        g_back = eui::Find(g.window, "cb.back");
        g_forward = eui::Find(g.window, "cb.forward");
        g_up = eui::Find(g.window, "cb.up");

        Rimage_set_texture(eui::Find(g.window, "cb.lynxie.icon"), eui::Texture("icons/lynxie.png"));
        eui::OnClick(eui::Find(g.window, "cb.lynxie"), []() { ShowLynxie(); });
        eui::OnClick(eui::Find(g.window, "cb.root"), []() { content_browser_current_path = content_browser_root; });
        eui::OnClick(g_back, []() { ContentBrowserGo(-1); });
        eui::OnClick(g_forward, []() { ContentBrowserGo(+1); });
        eui::OnClick(g_up, []()
        {
            const stdfs::path parent = content_browser_current_path.parent_path();
            if (content_browser_current_path != content_browser_root && IsPathInside(parent, content_browser_root))
                content_browser_current_path = parent;
        });
        eui::OnClick(eui::Find(g.window, "cb.new"), []() { NewDialog(content_browser_current_path); });

        if (!content_browser_thumb_loaded)
            LoadContentBrowserThumbScale();
        Rslider_set_value(g_size, content_browser_thumb_scale * 100.f);
        eui::OnValue(g_size, [](double v)
        {
            content_browser_thumb_scale = std::clamp(static_cast<float>(v) / 100.f, 0.5f, 2.5f);
            ApplyTileSize();
            SaveContentBrowserThumbScale();
        });
        ApplyTileSize();

        Rtile_view_set_drag_type(g_tiles, eui::kDragAsset);
        eui::OnTileDragData(g_tiles, []()
        {
            std::string data;
            for (const stdfs::path& path : SelectedPaths())
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
        eui::OnTileSelect(g_tiles, [](int index)
        {
            std::vector<stdfs::path> paths = SelectedPaths();
            const stdfs::path primary = index >= 0 && index < static_cast<int>(g_shown.size())
                ? g_shown[static_cast<size_t>(index)].path
                : (paths.empty() ? stdfs::path() : paths.back());
            if (paths.empty())
                cba::ClearSelection();
            else
                cba::SetSelection(paths, primary);
            g_shown_selection = cba::state.selection;
        });
        eui::OnTileActivate(g_tiles, [](int index)
        {
            if (index >= 0 && index < static_cast<int>(g_shown.size()))
                cba::OpenEntry(g_shown[static_cast<size_t>(index)].path);
        });
        eui::OnTileContext(g_tiles, [](int index, float, float)
        {
            if (index >= 0)
                ItemMenu(index);
            else
                BackgroundMenu();
        });
        eui::OnEvent(g_tiles, OnKey);
        Rwidget_set_drop_callback(g_tiles, OnDrop, nullptr);

        // Back / forward buttons of the mouse anywhere over the panel.
        eui::OnEvent(g.content_browser, [](const REvent& event)
        {
            if (event.type != R_EVENT_MOUSE_DOWN)
                return false;
            if (event.button == R_MOUSE_X1) { ContentBrowserGo(-1); return true; }
            if (event.button == R_MOUSE_X2) { ContentBrowserGo(+1); return true; }
            return false;
        });
        eui::OnText(g_search, [](const std::string&) { g_refresh_now = true; });
    }

    static void Update()
    {
        ContentBrowserTrackHistory();
        Breadcrumbs();
        eui::SetEnabled(g_back, !content_browser_back.empty());
        eui::SetEnabled(g_forward, !content_browser_forward.empty());
        eui::SetEnabled(g_up, content_browser_current_path != content_browser_root);

        const char* text = Rtext_input_get_text(g_search);
        const std::string search = text ? text : "";

        // Listed when the folder changes or after an action, refreshed twice a
        // second in the background (AsyncFolderList).
        const auto& entries = g_list.Get(content_browser_current_path, glfwGetTime(), 0.5, g_refresh_now);
        bool differ = g_refresh_now || g_shown_folder != content_browser_current_path || search != g_shown_search;
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
                if (k >= g_shown.size() || g_shown[k].path != entry.path())
                {
                    differ = true;
                    break;
                }
                ++k;
            }
            differ = differ || k != g_shown.size();
        }
        if (differ)
        {
            if (g_shown_folder != content_browser_current_path)
                cba::ClearSelection();
            g_refresh_now = false;
            g_shown_folder = content_browser_current_path;
            g_shown_search = search;
            Refill(entries);
        }

        ShowSelection();
        Dialogs();
    }
}
