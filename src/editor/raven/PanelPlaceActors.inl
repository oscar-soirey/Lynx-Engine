// =============================================================================
// Place Actors panel : the classes of actors (engine first, then the game).
//   click : an actor of this class at the pointer (one undo)
//   drag  : a real actor follows the pointer over the viewport, dropped there
//           (raven/PanelViewport.inl, R_DROP_*)
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::place_actors
{
    static Rid g_list = R_INVALID_ID;
    static Rid g_search = R_INVALID_ID;
    static std::unordered_map<Rid, std::string> g_classes;   // tree item -> class
    static size_t g_factory_size = static_cast<size_t>(-1);
    static std::string g_shown_search = "\x01";

    static void Place(const std::string& type_name)
    {
        lynx::Level* level = editor_level;
        if (!level || isPlaying)
            return;
        lynx::Actor* actor = level->SpawnActor(type_name.c_str());
        if (!actor)
            return;
        AssignDefaultActorName(level, actor, type_name);
        const std::string actor_id = actor->object_id_;
        UpdateActorLocationFromMouse(actor);
        PushEditorUndo([actor_id]()
        {
            if (lynx::Actor* created = FindEditorActor(actor_id))
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
        SetActorSelected(actor);
        MarkEditorDirty();
    }

    static void Rebuild()
    {
        auto* factory = lynx::Engine::Get()->GetFactory().GetInternalFactory();
        Rtree_view_clear_items(g_list);
        g_classes.clear();
        if (!factory)
        {
            Rtree_view_add_item(g_list, R_INVALID_ID, "Factory unavailable");
            return;
        }

        const char* text = Rtext_input_get_text(g_search);
        const std::string search = Lower(text ? text : "");

        std::vector<std::string> types;
        for (const auto& [type_name, constructor] : *factory)
        {
            (void)constructor;
            if (search.empty() || Lower(type_name).find(search) != std::string::npos)
                types.push_back(type_name);
        }
        std::sort(types.begin(), types.end());
        // Classes of the engine (Actor, lights...) first, then the game's.
        std::stable_partition(types.begin(), types.end(),
                              [](const std::string& name) { return lynx::IsEngineActorClass(name); });

        if (types.empty())
        {
            Rtree_view_add_item(g_list, R_INVALID_ID, "No matching actors");
            return;
        }

        Rid section = R_INVALID_ID;
        int section_kind = -1;
        for (const std::string& type_name : types)
        {
            const int kind = lynx::IsEngineActorClass(type_name) ? 0 : 1;
            if (kind != section_kind)
            {
                section_kind = kind;
                section = Rtree_view_add_item(g_list, R_INVALID_ID, kind == 0 ? "Engine" : "Game");
                Rtree_item_set_expanded(section, R_TRUE);
            }
            Rid item = Rtree_view_add_item(g_list, section, type_name.c_str());
            Rid texture = R_INVALID_ID;
            float uv[4];
            if (ActorTypeIcon(type_name, texture, uv))
                Rtree_item_set_icon(item, texture, uv[0], uv[1], uv[2], uv[3]);
            g_classes[item] = type_name;
        }
    }

    static void Init()
    {
        g_list = eui::Find(g.window, "place.list");
        g_search = eui::Find(g.window, "place.search");
        Rtree_view_set_drag_type(g_list, eui::kDragActorClass);

        // A click (not a drag) places the actor ; the selection of this list means nothing.
        eui::OnEvent(g_list, [](const REvent& event)
        {
            if (event.type != R_EVENT_MOUSE_UP || event.button != R_MOUSE_LEFT || ein::MouseDragDistance(0) > 4.f)
                return false;
            auto it = g_classes.find(Rtree_view_get_item_at(g_list, event.local_x, event.local_y));
            if (it == g_classes.end())
                return false;
            const std::string type_name = it->second;
            eui::Later([type_name]()
            {
                Place(type_name);
                Rtree_view_clear_selection(g_list);
            });
            return false;
        });
    }

    static void Update()
    {
        auto* factory = lynx::Engine::Get()->GetFactory().GetInternalFactory();
        const size_t size = factory ? factory->size() : 0;
        const char* text = Rtext_input_get_text(g_search);
        const std::string search = text ? text : "";
        // The classes change with a reload of the game (or a plugin).
        if (size != g_factory_size || search != g_shown_search)
        {
            g_factory_size = size;
            g_shown_search = search;
            Rebuild();
        }
    }
}

namespace editor_ui
{
    static std::string ClassOfDrag(const char* drag_data)
    {
        if (!drag_data)
            return "";
        char* end = nullptr;
        const unsigned long id = std::strtoul(drag_data, &end, 10);
        if (end == drag_data)
            return "";
        auto it = place_actors::g_classes.find(static_cast<Rid>(id));
        return it == place_actors::g_classes.end() ? std::string() : it->second;
    }
}
