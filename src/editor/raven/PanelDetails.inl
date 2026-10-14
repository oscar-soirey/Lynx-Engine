// =============================================================================
// Details panel : the properties of the selected actor.
// -----------------------------------------------------------------------------
// The rows are made from panels/details_rows.xml (<FloatProperty Label=".."/>,
// <TransformProperty/>...) when the selection changes ; every frame their
// values follow the actor (the gizmo moves it), except the field being edited.
// A change is applied at once ; the undo entry is made when the edit ends
// (mouse released, text field left), with the state before the edit.
//
// Also here : the asset picker (the "..." of a path property), used by every
// path field of the editor.
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::asset_picker
{
    // Chooses a file of assets/ ; `done` gets its path relative to assets/
    // ("" : None). Not called on Cancel.
    static void Open(const std::string& current, std::function<void(const std::string&)> done)
    {
        Rid box = eui::Load("dialogs/asset_picker.xml");
        if (box == R_INVALID_ID)
            return;

        struct State
        {
            std::filesystem::path folder;
            std::string value;
            std::vector<std::filesystem::directory_entry> entries;
            std::unordered_map<Rid, std::filesystem::path> items;
            std::function<void(const std::string&)> done;
        };
        auto s = std::make_shared<State>();
        s->value = current;
        s->done = std::move(done);
        s->folder = content_browser_root;
        // Starts in the folder of the current value.
        if (!current.empty())
        {
            std::error_code error;
            const std::filesystem::path parent = (content_browser_root / current).parent_path();
            if (std::filesystem::is_directory(parent, error) && IsPathInside(parent, content_browser_root))
                s->folder = parent;
        }

        Rid list = eui::Find(box, "picker.list");
        Rid search = eui::Find(box, "picker.search");
        Rid folder_label = eui::Find(box, "picker.folder");

        float folder_uv[4];
        eui::FileIconRegion(lynx::editor::file_icons::Kind::Folder, folder_uv);

        auto fill = std::make_shared<std::function<void()>>();
        *fill = [s, list, search, folder_label, folder_uv]()
        {
            std::string relative = s->folder.lexically_relative(content_browser_root).generic_string();
            if (relative == ".")
                relative.clear();
            eui::SetText(folder_label, "assets/" + relative);

            s->entries = ListFolderSorted(s->folder);
            Rtree_view_clear_items(list);
            s->items.clear();
            const char* text = Rtext_input_get_text(search);
            const std::string needle = Lower(text ? text : "");
            for (const auto& entry : s->entries)
            {
                const std::string name = entry.path().filename().string();
                if (!needle.empty() && Lower(name).find(needle) == std::string::npos)
                    continue;
                std::error_code error;
                const bool is_dir = entry.is_directory(error);
                Rid item = Rtree_view_add_item(list, R_INVALID_ID, name.c_str());
                float uv[4];
                const auto kind = lynx::editor::file_icons::ForFile(entry.path(), is_dir);
                eui::FileIconRegion(kind, uv);
                Rtree_item_set_icon(item, eui::FileIconTexture(), uv[0], uv[1], uv[2], uv[3]);
                s->items[item] = entry.path();
                if (!is_dir && entry.path().lexically_relative(content_browser_root).generic_string() == s->value)
                    Rtree_view_set_selected_item(list, item);
            }
            (void)folder_uv;
        };

        auto choose = [s, box](const std::filesystem::path& path)
        {
            std::error_code error;
            if (std::filesystem::is_directory(path, error))
                return false;
            std::string relative;
            if (!GetAssetRelativePath(path, relative))
                return false;
            auto done_fn = s->done;
            Rdialog_close(box, 1);
            if (done_fn)
                done_fn(relative);
            return true;
        };

        eui::OnText(search, [fill](const std::string&) { (*fill)(); });
        eui::OnTreeActivate(list, [s, fill, choose](Rid item)
        {
            auto it = s->items.find(item);
            if (it == s->items.end())
                return;
            std::error_code error;
            if (std::filesystem::is_directory(it->second, error))
            {
                s->folder = it->second;
                (*fill)();
            }
            else
                choose(it->second);
        });
        eui::OnClick(eui::Find(box, "picker.up"), [s, fill]()
        {
            const std::filesystem::path parent = s->folder.parent_path();
            if (s->folder != content_browser_root && IsPathInside(parent, content_browser_root))
            {
                s->folder = parent;
                (*fill)();
            }
        });
        eui::OnClick(eui::Find(box, "picker.ok"), [s, list, choose]()
        {
            auto it = s->items.find(Rtree_view_get_selected_item(list));
            if (it != s->items.end())
                choose(it->second);
        });
        eui::OnClick(eui::Find(box, "picker.none"), [s, box]()
        {
            auto done_fn = s->done;
            Rdialog_close(box, 1);
            if (done_fn)
                done_fn("");
        });
        eui::OnClick(eui::Find(box, "picker.cancel"), [box]() { Rdialog_close(box, 0); });

        (*fill)();
        Rdialog_open(g.window, box, nullptr, nullptr);
        Rwidget_focus(search);
    }

    // A text field that takes a file dragged from the Content Browser.
    static void AcceptAssetDrops(Rid field, std::function<void(const std::string&)> set)
    {
        eui::OnDrop(field, [set](const RDropEvent& drop)
        {
            if (!drop.drag_type || std::strcmp(drop.drag_type, eui::kDragAsset) != 0)
                return false;
            if (drop.phase != R_DROP_DROPPED)
                return true;
            const std::vector<std::string> paths = SplitLines(drop.text);
            std::string relative;
            if (!paths.empty() && GetAssetRelativePath(std::filesystem::path(paths.front()), relative))
                set(relative);
            return true;
        });
    }
}


namespace editor_ui::details
{
    static Rid g_body = R_INVALID_ID;
    static Rid g_type = R_INVALID_ID;
    static Rid g_icon = R_INVALID_ID;

    static lynx::Actor* g_actor = nullptr;            // actor the rows are made for
    static size_t g_property_count = 0;
    static std::vector<std::function<void()>> g_refresh;   // value of the actor -> row

    static bool g_undo_pending = false;
    static EditorActorSnapshot g_undo_before;
    static std::string g_undo_actor_id;

    static bool IsColorProperty(const std::string& name, int type)
    {
        return (type == 5 || type == 6) && IsColorPropertyName(name);
    }

    static void Changed()
    {
        g_undo_pending = true;
        MarkEditorDirty();
    }

    // One row from panels/details_rows.xml.
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
        else
            std::cerr << "[UI] Details : could not make " << xml << "\n";
        return row;
    }

    static Rid Section(const char* title)
    {
        Rid section = MakeRow(g_body, "DetailsSection", "", std::string(" Title=\"") + title + "\"");
        return section == R_INVALID_ID ? g_body : eui::Find(section, "rows");
    }

    // x / y / z / w spin boxes of a vector row, bound to `values` (n of them).
    static void BindVector(Rid row, int n, std::function<float*()> values, std::function<void()> after = {})
    {
        static const char* const kAxes[] = { "x", "y", "z", "w" };
        for (int i = 0; i < n; ++i)
        {
            Rid spin = Rwidget_find(row, kAxes[i]);
            if (spin == R_INVALID_ID)
                continue;
            eui::OnValue(spin, [values, after, i](double v)
            {
                if (float* p = values())
                {
                    if (p[i] == static_cast<float>(v))
                        return;
                    p[i] = static_cast<float>(v);
                    if (after)
                        after();
                    Changed();
                }
            });
            g_refresh.push_back([spin, values, i]()
            {
                if (float* p = values())
                    eui::SetValue(spin, p[i]);
            });
        }
    }

    static void AddTransform(Rid parent, lynx::transform* value)
    {
        Rid row = MakeRow(parent, "TransformProperty", "");
        if (row == R_INVALID_ID)
            return;
        auto sync = [value]() { SyncGizmoFromTransform(*value); };
        BindVector(eui::Find(row, "location"), 3, [value]() { return &value->location.x; }, sync);
        BindVector(eui::Find(row, "rotation"), 3, [value]() { return &value->rotation.x; }, sync);
        BindVector(eui::Find(row, "scale"), 3, [value]() { return &value->scale.x; }, sync);
    }

    static void AddProperty(Rid parent, const std::string& name, const lynx::property& prop)
    {
        const int type = static_cast<int>(prop.GetType());

        if (IsColorProperty(name, type))
        {
            Rid row = MakeRow(parent, "ColorProperty", name);
            Rid picker = eui::Find(row, "value");
            const bool alpha = type == 6;
            float* p = nullptr;
            if (auto v3 = std::get_if<lynx::vec3*>(&prop.property_member); v3 && *v3)
                p = &(*v3)->x;
            else if (auto v4 = std::get_if<lynx::vec4*>(&prop.property_member); v4 && *v4)
                p = &(*v4)->x;
            if (!p)
                return;
            Rcolor_picker_set_alpha_enabled(picker, alpha ? R_TRUE : R_FALSE);
            eui::OnColor(picker, [p, alpha](float r, float g2, float b, float a)
            {
                p[0] = r; p[1] = g2; p[2] = b;
                if (alpha)
                    p[3] = a;
                Changed();
            });
            g_refresh.push_back([picker, p, alpha]()
            {
                if (Rwindow_has_popup(g.window))
                    return;   // being picked
                float r, g2, b, a;
                Rcolor_picker_get_color(picker, &r, &g2, &b, &a);
                const float want_a = alpha ? p[3] : 1.f;
                if (r != p[0] || g2 != p[1] || b != p[2] || a != want_a)
                    Rcolor_picker_set_color(picker, p[0], p[1], p[2], want_a);
            });
            return;
        }

        switch (type)
        {
        case 0:   // int
        {
            auto ptr = std::get_if<int*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            int* value = *ptr;
            if (name == "auto_possess_player")
            {
                // Like AutoPossessPlayer in Unreal : Disabled (-1), Player 0..7.
                Rid row = MakeRow(parent, "ChoiceProperty", name);
                Rid combo = eui::Find(row, "value");
                Rcombo_box_add_item(combo, "Disabled");
                for (int i = 0; i < std::max(8, *value + 1); ++i)
                    Rcombo_box_add_item(combo, ("Player " + std::to_string(i)).c_str());
                eui::SetTooltip(row, "Player that possesses this actor when the game starts\n"
                                     "(player 0 = default player, others : Engine::CreatePlayer).");
                eui::OnSelect(combo, [value](int index)
                {
                    if (index - 1 != *value)
                    {
                        *value = index - 1;
                        Changed();
                    }
                });
                g_refresh.push_back([combo, value]()
                {
                    if (Rcombo_box_get_selected(combo) != *value + 1)
                        Rcombo_box_set_selected(combo, *value + 1);
                });
                return;
            }
            Rid row = MakeRow(parent, "IntProperty", name);
            Rid spin = eui::Find(row, "value");
            eui::OnValue(spin, [value](double v)
            {
                const int n = static_cast<int>(std::lround(v));
                if (n != *value)
                {
                    *value = n;
                    Changed();
                }
            });
            g_refresh.push_back([spin, value]() { eui::SetValue(spin, *value); });
            return;
        }
        case 1:   // float
        {
            auto ptr = std::get_if<float*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            float* value = *ptr;
            Rid row = MakeRow(parent, "FloatProperty", name);
            Rid spin = eui::Find(row, "value");
            eui::OnValue(spin, [value](double v)
            {
                if (static_cast<float>(v) != *value)
                {
                    *value = static_cast<float>(v);
                    Changed();
                }
            });
            g_refresh.push_back([spin, value]() { eui::SetValue(spin, *value); });
            return;
        }
        case 2:   // bool
        {
            auto ptr = std::get_if<bool*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            bool* value = *ptr;
            Rid row = MakeRow(parent, "BoolProperty", name);
            Rid check = eui::Find(row, "value");
            eui::OnToggle(check, [value](bool on)
            {
                if (on != *value)
                {
                    *value = on;
                    Changed();
                }
            });
            g_refresh.push_back([check, value]() { eui::SetChecked(check, *value); });
            return;
        }
        case 3:   // string : a path of assets/ (typed, dropped or picked)
        {
            auto ptr = std::get_if<std::string*>(&prop.property_member);
            if (!ptr || !*ptr)
                return;
            std::string* value = *ptr;
            Rid row = MakeRow(parent, "AssetProperty", name);
            Rid field = eui::Find(row, "value");
            auto set = [field, value](const std::string& text)
            {
                if (text == *value)
                    return;
                *value = text;
                Rtext_input_set_text(field, text.c_str());
                Changed();
            };
            eui::OnText(field, [value](const std::string& text)
            {
                if (text != *value)
                {
                    *value = text;
                    Changed();
                }
            });
            asset_picker::AcceptAssetDrops(field, set);
            eui::OnClick(eui::Find(row, "pick"), [value, set]() { asset_picker::Open(*value, set); });
            g_refresh.push_back([field, value]()
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
            Rid row = MakeRow(parent, kTags[n - 2], name);
            BindVector(row, n, [p]() { return p; });
            return;
        }
        case 7:   // a transform that is not "transform" (in the Properties section)
        {
            auto ptr = std::get_if<lynx::transform*>(&prop.property_member);
            if (ptr && *ptr)
                AddTransform(parent, *ptr);
            return;
        }
        default:
        {
            Rid row = MakeRow(parent, "ReadOnlyProperty", name);
            eui::SetText(eui::Find(row, "value"), lynx::PropertyToString(prop));
            return;
        }
        }
    }

    static void Rebuild(lynx::Actor* actor)
    {
        Rwidget_clear_children(g_body);
        g_refresh.clear();
        g_actor = actor;
        g_property_count = actor ? actor->GetProperties().size() : 0;
        g_undo_pending = false;

        if (!actor)
        {
            eui::SetText(g_type, "No actor selected");
            eui::SetVisible(g_icon, false);
            return;
        }

        const std::string type_name = actor->GetTypeName();
        eui::SetText(g_type, type_name);
        Rid texture = R_INVALID_ID;
        float uv[4];
        if (ActorTypeIcon(type_name, texture, uv))
        {
            Rimage_set_texture(g_icon, texture);
            Rimage_set_region(g_icon, uv[0], uv[1], uv[2], uv[3]);
            eui::SetVisible(g_icon, true);
        }
        else
            eui::SetVisible(g_icon, false);

        g_undo_before = CaptureActorSnapshot(actor);
        g_undo_actor_id = actor->object_id_;

        // Object ID (the name of the actor in the level and for the scripts).
        {
            Rid row = MakeRow(g_body, "TextProperty", "Object ID");
            Rid field = eui::Find(row, "value");
            eui::OnText(field, [actor](const std::string& text)
            {
                if (IsActorAlive(actor) && text != actor->object_id_)
                {
                    actor->object_id_ = text;
                    Changed();
                }
            });
            g_refresh.push_back([field, actor]()
            {
                if (Rwidget_is_focused(field) || !IsActorAlive(actor))
                    return;
                const char* shown = Rtext_input_get_text(field);
                if (!shown || actor->object_id_ != shown)
                    Rtext_input_set_text(field, actor->object_id_.c_str());
            });
        }

        const auto& properties = actor->GetProperties();

        // Transforms first, in their own section (Location / Rotation / Scale, like Unreal).
        for (const auto& [name, prop] : properties)
        {
            if (prop.GetType() != 7)
                continue;
            auto ptr = std::get_if<lynx::transform*>(&prop.property_member);
            if (!ptr || !*ptr)
                continue;
            AddTransform(Section(name == "transform" ? "Transform" : name.c_str()), *ptr);
        }

        Rid rows = Section("Properties");
        int count = 0;
        for (const auto& [name, prop] : properties)
        {
            if (name == "object_id_" || name == "outliner_folder" || prop.GetType() == 7)
                continue;
            AddProperty(rows, name, prop);
            ++count;
        }
        if (count == 0)
        {
            Rid none = Rlabel_create("No properties");
            Rwidget_add_class(none, "dim");
            Rwidget_add_child(rows, none);
        }

        for (const auto& refresh : g_refresh)
            refresh();
    }

    // The edit ended : one undo entry with the state before it.
    static void CommitUndo()
    {
        if (!g_undo_pending)
            return;
        // Still editing : the mouse drags a number, or a text field of the panel has the keyboard.
        if (ein::Mouse(0) || (ein::WantTextInput() && HasFocus(g.details)))
            return;

        const std::string actor_id_after = g_actor && IsActorAlive(g_actor) ? g_actor->object_id_ : g_undo_actor_id;
        const EditorActorSnapshot before = g_undo_before;
        PushEditorUndo([before, actor_id_after]()
        {
            lynx::Actor* actor = FindEditorActor(actor_id_after);
            if (!actor)
                return;
            actor->object_id_ = before.object_id;
            actor->transform = before.transform;
            auto& properties = actor->GetProperties();
            for (const auto& [name, value] : before.properties)
            {
                auto it = properties.find(name);
                if (it != properties.end())
                    ApplyEditorProperty(it->second, value);
            }
            SetActorSelected(actor);
            MarkEditorDirty();
        });
        g_undo_pending = false;
        if (g_actor && IsActorAlive(g_actor))
        {
            g_undo_before = CaptureActorSnapshot(g_actor);
            g_undo_actor_id = g_actor->object_id_;
        }
    }

    static void Init()
    {
        g_body = eui::Find(g.window, "details.body");
        g_type = eui::Find(g.window, "details.type");
        g_icon = eui::Find(g.window, "details.icon");
    }

    static void Update()
    {
        lynx::Actor* actor = editing_actor && IsActorAlive(editing_actor) ? editing_actor : nullptr;
        CommitUndo();
        if (actor != g_actor || (actor && actor->GetProperties().size() != g_property_count))
        {
            CommitUndo();
            Rebuild(actor);
            return;
        }
        for (const auto& refresh : g_refresh)
            refresh();
    }
}
