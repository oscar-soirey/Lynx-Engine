// =============================================================================
// Outliner panel : the actors of the level in their folders
// (Actor::outliner_folder, Level::outliner_folders), like Unreal.
//   click / Ctrl / Shift : selection (the viewport and Details follow)
//   drag : onto a folder (or the empty space : the root)
//   right click : Duplicate, Move to folder, Delete / folder menu
//   double click : the camera goes to the actor ; F2 : rename the folder
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::outliner
{
    struct Row
    {
        lynx::Actor* actor = nullptr;   // nullptr : a folder
        std::string folder;
    };

    static Rid g_tree = R_INVALID_ID;
    static Rid g_search = R_INVALID_ID;
    static Rid g_count = R_INVALID_ID;
    static std::unordered_map<Rid, Row> g_rows;
    static std::unordered_map<lynx::Actor*, Rid> g_actor_items;
    static std::set<std::string> g_collapsed;          // folders closed by the user
    static uint64_t g_signature = 0;
    static std::vector<lynx::Actor*> g_shown_selection;  // what the tree shows as selected
    static lynx::Actor* g_shown_primary = nullptr;
    static std::unique_ptr<eui::Menu> g_menu;
    static bool g_rebuild = true;

    static uint64_t Hash(uint64_t h, const void* data, size_t size)
    {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    static uint64_t Signature(lynx::Level* level, const std::string& search)
    {
        uint64_t h = 1469598103934665603ull;
        h = Hash(h, search.data(), search.size());
        for (const std::string& f : level->outliner_folders)
            h = Hash(h, f.data(), f.size() + 1);
        for (lynx::Actor* a : level->GetActors())
        {
            if (!a)
                continue;
            h = Hash(h, &a, sizeof(a));
            h = Hash(h, a->object_id_.data(), a->object_id_.size() + 1);
            h = Hash(h, a->outliner_folder.data(), a->outliner_folder.size() + 1);
        }
        return h;
    }

    static void Rebuild(lynx::Level* level)
    {
        // Folders the user closed stay closed.
        for (const auto& [item, row] : g_rows)
            if (!row.actor && Rtree_item_is_valid(item))
            {
                if (Rtree_item_is_expanded(item))
                    g_collapsed.erase(row.folder);
                else
                    g_collapsed.insert(row.folder);
            }

        Rtree_view_clear_items(g_tree);
        g_rows.clear();
        g_actor_items.clear();
        g_shown_selection.clear();
        g_shown_primary = nullptr;

        const char* search_text = Rtext_input_get_text(g_search);
        const std::string search = Lower(search_text ? search_text : "");
        const auto& actors = level->GetActors();
        const std::set<std::string> folders = OutlinerAllFolders(level);

        std::map<std::string, std::vector<std::string>> children;
        for (const std::string& f : folders)
            children[OutlinerParent(f)].push_back(f);
        std::map<std::string, std::vector<lynx::Actor*>> by_folder;
        for (lynx::Actor* a : actors)
            if (a)
                by_folder[a->outliner_folder].push_back(a);

        float folder_uv[4];
        eui::FileIconRegion(lynx::editor::file_icons::Kind::Folder, folder_uv);

        int shown = 0;
        std::function<void(const std::string&, Rid)> add = [&](const std::string& folder, Rid parent)
        {
            for (const std::string& child : children[folder])
            {
                Rid item = Rtree_view_add_item(g_tree, parent, OutlinerName(child).c_str());
                Rtree_item_set_icon(item, eui::FileIconTexture(), folder_uv[0], folder_uv[1], folder_uv[2], folder_uv[3]);
                g_rows[item] = Row{ nullptr, child };
                add(child, item);
                Rtree_item_set_expanded(item, !search.empty() || g_collapsed.count(child) == 0);
            }
            for (lynx::Actor* actor : by_folder[folder])
            {
                const std::string label = ActorLabel(actor);
                if (!search.empty() && Lower(label).find(search) == std::string::npos)
                    continue;
                Rid item = Rtree_view_add_item(g_tree, parent, label.c_str());
                Rid texture = R_INVALID_ID;
                float uv[4];
                if (ActorTypeIcon(actor->GetTypeName(), texture, uv))
                    Rtree_item_set_icon(item, texture, uv[0], uv[1], uv[2], uv[3]);
                g_rows[item] = Row{ actor, actor->outliner_folder };
                g_actor_items[actor] = item;
                ++shown;
            }
        };
        add("", R_INVALID_ID);

        const size_t total = actors.size();
        eui::SetText(g_count, search.empty()
            ? std::to_string(total) + (total == 1 ? " actor" : " actors")
            : std::to_string(shown) + " of " + std::to_string(total) + " actors");
    }

    // The selection of the editor (viewport, palette, undo...) shown in the tree.
    static void ShowSelection()
    {
        PruneActorSelection();
        std::vector<lynx::Actor*> wanted = selected_actors;
        if (wanted.empty() && editing_actor)
            wanted.push_back(editing_actor);
        if (wanted == g_shown_selection && editing_actor == g_shown_primary)
            return;
        g_shown_selection = wanted;
        g_shown_primary = editing_actor;

        Rtree_view_clear_selection(g_tree);
        for (lynx::Actor* actor : wanted)
            if (auto it = g_actor_items.find(actor); it != g_actor_items.end())
                Rtree_item_set_selected(it->second, R_TRUE);
        if (editing_actor)
            if (auto it = g_actor_items.find(editing_actor); it != g_actor_items.end())
                Rtree_view_scroll_to_item(g_tree, it->second);
    }

    // The user changed the selection of the tree.
    static void OnSelection(Rid primary_item)
    {
        std::vector<lynx::Actor*> actors;
        const int count = Rtree_view_get_selection_count(g_tree);
        for (int i = 0; i < count; ++i)
        {
            auto it = g_rows.find(Rtree_view_get_selection(g_tree, i));
            if (it != g_rows.end() && it->second.actor && IsActorAlive(it->second.actor))
                actors.push_back(it->second.actor);
        }
        lynx::Actor* primary = nullptr;
        if (auto it = g_rows.find(primary_item); it != g_rows.end() && it->second.actor)
            primary = it->second.actor;
        if (!primary && !actors.empty())
            primary = actors.back();

        if (actors.empty())
            SetActorSelected(nullptr);
        else if (actors.size() == 1)
            SetActorSelected(actors.front());
        else
            SetActorSelection(actors, primary);
        outliner_anchor = primary;
        g_shown_selection = selected_actors.empty() && editing_actor ? std::vector<lynx::Actor*>{ editing_actor } : selected_actors;
        g_shown_primary = editing_actor;
    }

    static void RenameFolder(const std::string& folder)
    {
        lynx::Level* level = editor_level;
        if (!level)
            return;
        eui::AskText(g.window, "Rename folder", "Name", OutlinerName(folder),
            [folder](const std::string& typed)
            {
                if (!editor_level)
                    return;
                std::string name = typed;
                name.erase(std::remove(name.begin(), name.end(), '/'), name.end());
                name.erase(std::remove(name.begin(), name.end(), '|'), name.end());
                const std::string parent = OutlinerParent(folder);
                const std::string target = parent.empty() ? name : parent + "/" + name;
                if (target != folder && !name.empty())
                {
                    PushOutlinerUndo(editor_level);
                    OutlinerRenameFolder(editor_level, folder, target);
                    MarkEditorDirty();
                }
            },
            [folder](const std::string& typed) -> std::string
            {
                std::string name = typed;
                name.erase(std::remove(name.begin(), name.end(), '/'), name.end());
                name.erase(std::remove(name.begin(), name.end(), '|'), name.end());
                if (name.empty())
                    return "Type a name.";
                const std::string parent = OutlinerParent(folder);
                const std::string target = parent.empty() ? name : parent + "/" + name;
                if (target != folder && editor_level && OutlinerAllFolders(editor_level).count(target) > 0)
                    return "A folder \"" + name + "\" already exists";
                return "";
            });
    }

    static void MoveToMenu(eui::Menu& parent, lynx::Actor* actor)
    {
        eui::Menu& menu = parent.Sub("Move to folder");
        menu.Check("(root)", actor->outliner_folder.empty(), [actor]()
        {
            if (editor_level && IsActorAlive(actor))
                OutlinerMoveActor(editor_level, actor, "");
        });
        for (const std::string& f : OutlinerAllFolders(editor_level))
            menu.Check(f, actor->outliner_folder == f, [actor, f]()
            {
                if (editor_level && IsActorAlive(actor))
                    OutlinerMoveActor(editor_level, actor, f);
            });
        menu.Separator();
        menu.Item("New folder", [actor]()
        {
            if (!editor_level || !IsActorAlive(actor))
                return;
            PushOutlinerUndo(editor_level);
            const std::string folder = OutlinerUniqueChild(editor_level, actor->outliner_folder, "New Folder");
            OutlinerDeclare(editor_level, folder);
            actor->outliner_folder = folder;
            MarkEditorDirty();
            RenameFolder(folder);
        });
    }

    static void OnContext(Rid item)
    {
        lynx::Level* level = editor_level;
        if (!level)
            return;
        g_menu = std::make_unique<eui::Menu>(g.window);
        eui::Menu& menu = *g_menu;

        auto it = g_rows.find(item);
        if (it == g_rows.end())
        {
            menu.Item("New folder", []()
            {
                if (!editor_level)
                    return;
                PushOutlinerUndo(editor_level);
                const std::string folder = OutlinerUniqueChild(editor_level, "", "New Folder");
                OutlinerDeclare(editor_level, folder);
                MarkEditorDirty();
                RenameFolder(folder);
            });
        }
        else if (it->second.actor)
        {
            lynx::Actor* actor = it->second.actor;
            // Right click on an actor of the selection keeps the whole selection.
            if (!IsActorInSelection(actor))
                SetActorSelected(actor);
            menu.Title(ActorLabel(actor));
            menu.Item("Duplicate", [actor]() { if (IsActorAlive(actor)) DuplicateActor(editor_level, actor); });
            MoveToMenu(menu, actor);
            SpriteVoxelsItem(menu, actor);
            menu.Separator();
            menu.Item("Delete", [actor]()
            {
                PruneActorSelection();
                std::vector<lynx::Actor*> targets = selected_actors;
                if (std::find(targets.begin(), targets.end(), actor) == targets.end())
                    targets = { actor };
                DeleteActors(targets);
            }, true, "Del");
        }
        else
        {
            const std::string folder = it->second.folder;
            menu.Title(folder);
            menu.Item("New subfolder", [folder]()
            {
                if (!editor_level)
                    return;
                PushOutlinerUndo(editor_level);
                const std::string child = OutlinerUniqueChild(editor_level, folder, "New Folder");
                OutlinerDeclare(editor_level, child);
                MarkEditorDirty();
                RenameFolder(child);
            });
            menu.Item("Rename", [folder]() { RenameFolder(folder); }, true, "F2");
            menu.Item("Delete folder (the actors go to the parent)", [folder]()
            {
                if (!editor_level)
                    return;
                PushOutlinerUndo(editor_level);
                OutlinerDeleteFolder(editor_level, folder);
                MarkEditorDirty();
            });
        }
        menu.Open();
    }

    // Folder under the pointer ("" : the root) for a drop.
    static std::string DropFolder(const RDropEvent& drop, Rid* highlight)
    {
        Rid item = Rtree_view_get_item_at(g_tree, drop.local_x, drop.local_y);
        *highlight = item;
        auto it = g_rows.find(item);
        if (it == g_rows.end())
            return "";
        // On an actor : its folder.
        if (it->second.actor)
        {
            *highlight = Rtree_item_get_parent(item);
            return it->second.actor->outliner_folder;
        }
        return it->second.folder;
    }

    static int OnDrop(Rid, const RDropEvent* drop, void*)
    {
        lynx::Level* level = editor_level;
        if (!level || !drop->drag_type || std::strcmp(drop->drag_type, eui::kDragOutliner) != 0 || drop->drag_source != g_tree)
            return R_FALSE;

        Rid highlight = R_INVALID_ID;
        const std::string folder = DropFolder(*drop, &highlight);
        if (drop->phase == R_DROP_LEAVE)
        {
            Rtree_view_set_drop_highlight(g_tree, R_INVALID_ID, R_FALSE);
            return R_TRUE;
        }
        if (drop->phase != R_DROP_DROPPED)
        {
            Rtree_view_set_drop_highlight(g_tree, highlight, highlight == R_INVALID_ID);
            return R_TRUE;
        }
        Rtree_view_set_drop_highlight(g_tree, R_INVALID_ID, R_FALSE);

        // The dragged items : actors and folders (one undo).
        std::vector<lynx::Actor*> actors;
        std::vector<std::string> folders;
        const char* text = drop->text ? drop->text : "";
        for (char* end = nullptr; *text; text = end)
        {
            const unsigned long id = std::strtoul(text, &end, 10);
            if (end == text)
                break;
            auto it = g_rows.find(static_cast<Rid>(id));
            if (it != g_rows.end())
            {
                if (it->second.actor)
                    actors.push_back(it->second.actor);
                else
                    folders.push_back(it->second.folder);
            }
        }

        bool undo_pushed = false;
        auto undo_once = [&]()
        {
            if (!undo_pushed)
                PushOutlinerUndo(level);
            undo_pushed = true;
        };
        for (lynx::Actor* actor : actors)
        {
            if (!IsActorAlive(actor) || actor->outliner_folder == folder)
                continue;
            undo_once();
            actor->outliner_folder = folder;
            OutlinerDeclare(level, folder);
        }
        for (const std::string& dragged : folders)
        {
            if (OutlinerInside(folder, dragged) || OutlinerParent(dragged) == folder)
                continue;
            undo_once();
            OutlinerRenameFolder(level, dragged, OutlinerUniqueChild(level, folder, OutlinerName(dragged)));
        }
        if (undo_pushed)
            MarkEditorDirty();
        return R_TRUE;
    }

    static void Init()
    {
        g_tree = eui::Find(g.window, "outliner.tree");
        g_search = eui::Find(g.window, "outliner.search");
        g_count = eui::Find(g.window, "outliner.count");

        Rtree_view_set_multi_select(g_tree, R_TRUE);
        Rtree_view_set_drag_type(g_tree, eui::kDragOutliner);
        Rwidget_set_drop_callback(g_tree, OnDrop, nullptr);

        eui::OnTreeSelect(g_tree, [](Rid item) { OnSelection(item); });
        eui::OnTreeActivate(g_tree, [](Rid item)
        {
            auto it = g_rows.find(item);
            if (it == g_rows.end())
                return;
            if (it->second.actor && IsActorAlive(it->second.actor))
            {
                camX = it->second.actor->transform.location.x;
                camY = it->second.actor->transform.location.y;
            }
            else if (!it->second.actor)
                Rtree_item_set_expanded(item, !Rtree_item_is_expanded(item));
        });
        eui::OnTreeContext(g_tree, [](Rid item, float, float) { OnContext(item); });
        eui::OnText(g_search, [](const std::string&) { g_rebuild = true; });
        eui::OnClick(eui::Find(g.window, "outliner.new_folder"), []()
        {
            if (!editor_level)
                return;
            OutlinerNewFolder(editor_level, "");
        });

        // F2 : rename the folder selected.
        eui::OnEvent(g_tree, [](const REvent& event)
        {
            if (event.type != R_EVENT_KEY_DOWN || event.key != R_KEY_F2)
                return false;
            auto it = g_rows.find(Rtree_view_get_selected_item(g_tree));
            if (it == g_rows.end() || it->second.actor)
                return false;
            RenameFolder(it->second.folder);
            return true;
        });
    }

    static void Update()
    {
        lynx::Level* level = editor_level;
        if (!level)
        {
            if (!g_rows.empty())
            {
                Rtree_view_clear_items(g_tree);
                g_rows.clear();
                g_actor_items.clear();
            }
            g_signature = 0;
            return;
        }

        // A folder created from a menu : its name is asked at once.
        if (outliner_rename_open)
        {
            outliner_rename_open = false;
            RenameFolder(outliner_rename_path);
        }

        const char* search_text = Rtext_input_get_text(g_search);
        const uint64_t signature = Signature(level, search_text ? search_text : "");
        if (g_rebuild || signature != g_signature)
        {
            g_rebuild = false;
            g_signature = signature;
            Rebuild(level);
        }
        ShowSelection();
    }
}
