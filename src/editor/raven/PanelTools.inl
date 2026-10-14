// =============================================================================
// Panels of the tools : Post Process, 2D Lighting (their settings rows are
// made from the parameters of the engine), Voxel World (size of the world of
// the level), Camera Shake (settings, curves, Test).
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::tools
{
    // ---- Post Process / 2D Lighting : the same kind of parameters ---------------------

    struct PostProcessApi
    {
        using Info = lynx::postprocess::ParamInfo;
        using Type = lynx::postprocess::ParamType;
        static const std::vector<Info>& Params() { return lynx::postprocess::GetParams(); }
        static bool Get(const char* n, float v[3]) { return lynx::postprocess::GetValue(n, v); }
        static void Set(const char* n, const float* v) { lynx::postprocess::SetValue(n, v, 3); }
        static bool Modified(const char* n) { return lynx::postprocess::IsModified(n); }
        static void Reset(const char* n) { lynx::postprocess::Reset(n); }
        static constexpr const char* kFormat = "%.3f";
    };

    struct Lighting2DApi
    {
        using Info = lynx::lighting2d::ParamInfo;
        using Type = lynx::lighting2d::ParamType;
        static const std::vector<Info>& Params() { return lynx::lighting2d::GetParams(); }
        static bool Get(const char* n, float v[3]) { return lynx::lighting2d::GetValue(n, v); }
        static void Set(const char* n, const float* v) { lynx::lighting2d::SetValue(n, v, 3); }
        static bool Modified(const char* n) { return lynx::lighting2d::IsModified(n); }
        static void Reset(const char* n) { lynx::lighting2d::Reset(n); }
        static constexpr const char* kFormat = "%.2f";
    };

    // Rows of every parameter, by group ; `refresh` : value of the engine -> row.
    template <class Api>
    static void BuildParams(Rid body, std::vector<std::function<void()>>& refresh)
    {
        std::string group;
        Rid rows = body;
        for (const auto& p : Api::Params())
        {
            if (group != p.group)
            {
                group = p.group;
                const std::string xml = std::string("<DetailsSection Title=\"") + p.group + "\"/>";
                Rid section = Rwidget_create_from_xml(xml.c_str(), xml.size());
                if (section != R_INVALID_ID)
                {
                    Rwidget_add_child(body, section);
                    rows = eui::Find(section, "rows");
                }
            }

            const char* name = p.name;
            const std::string tip = std::string(p.name) + (p.tooltip && p.tooltip[0] ? std::string("\n") + p.tooltip : "");

            // [label] [editor] [Reset]
            const char* row_xml = "<PropertyRow Name='row'><Button Name='reset' Text='Reset' Visibility='Collapsed'/></PropertyRow>";
            Rid row = Rwidget_create_from_xml(row_xml, std::strlen(row_xml));
            if (row == R_INVALID_ID)
                continue;
            Rwidget_add_child(rows, row);
            eui::SetText(Rwidget_find(row, "label"), p.label);
            eui::SetTooltip(Rwidget_find(row, "label"), tip);
            Rid reset = Rwidget_find(row, "reset");
            Rid holder = Rwidget_get_parent(reset);
            eui::OnClick(reset, [name]() { Api::Reset(name); });

            Rid editor = R_INVALID_ID;
            Rid value_label = R_INVALID_ID;
            switch (p.type)
            {
            case Api::Type::Float:
            {
                editor = Rwidget_create(R_WIDGET_SLIDER);
                Rslider_set_range(editor, p.min, p.max);
                Rslider_set_step(editor, (p.max - p.min) / 1000.f);
                value_label = Rlabel_create("");
                Rwidget_set_size(value_label, 52.f, -1.f);
                eui::OnValue(editor, [name, value_label](double v)
                {
                    float values[3] = {};
                    Api::Get(name, values);
                    values[0] = static_cast<float>(v);
                    Api::Set(name, values);
                    eui::SetText(value_label, Format(Api::kFormat, v));
                });
                break;
            }
            case Api::Type::Toggle:
            {
                editor = Rcheckbox_create("");
                eui::OnToggle(editor, [name](bool on)
                {
                    float values[3] = { on ? 1.f : 0.f, 0.f, 0.f };
                    Api::Set(name, values);
                });
                break;
            }
            case Api::Type::Color:
            {
                editor = Rcolor_picker_create();
                Rcolor_picker_set_popup(editor, R_TRUE);
                Rcolor_picker_set_alpha_enabled(editor, R_FALSE);
                eui::OnColor(editor, [name](float r, float g2, float b, float)
                {
                    float values[3] = { r, g2, b };
                    Api::Set(name, values);
                });
                break;
            }
            }
            if (editor == R_INVALID_ID)
                continue;

            // The editor before the Reset button.
            Rwidget_remove_child(holder, reset);
            Rwidget_add_child(holder, editor);
            if (value_label != R_INVALID_ID)
                Rwidget_add_child(holder, value_label);
            Rwidget_add_child(holder, reset);
            eui::SetTooltip(editor, tip);
            if (p.type == Api::Type::Float)
                Rslot_set_fill(editor, 1.f);

            const auto type = p.type;
            refresh.push_back([name, type, editor, value_label, reset]()
            {
                float v[3] = {};
                Api::Get(name, v);
                switch (type)
                {
                case Api::Type::Float:
                    eui::SetValue(editor, v[0]);
                    eui::SetText(value_label, Format(Api::kFormat, v[0]));
                    break;
                case Api::Type::Toggle:
                    eui::SetChecked(editor, v[0] != 0.f);
                    break;
                case Api::Type::Color:
                    if (!Rwindow_has_popup(g.window))
                    {
                        float r, g2, b, a;
                        Rcolor_picker_get_color(editor, &r, &g2, &b, &a);
                        if (r != v[0] || g2 != v[1] || b != v[2])
                            Rcolor_picker_set_color(editor, v[0], v[1], v[2], 1.f);
                    }
                    break;
                }
                eui::SetVisible(reset, Api::Modified(name));
            });
        }
    }

    static std::vector<std::function<void()>> g_pp_refresh;
    static std::vector<std::function<void()>> g_l2_refresh;
    static Rid g_pp_file = R_INVALID_ID;
    static Rid g_l2_file = R_INVALID_ID;
    static Rid g_l2_status = R_INVALID_ID;

    // ---- Voxel World -----------------------------------------------------------------

    static Rid g_vw_level = R_INVALID_ID, g_vw_file = R_INVALID_ID, g_vw_size = R_INVALID_ID;
    static Rid g_vw_width = R_INVALID_ID, g_vw_height = R_INVALID_ID, g_vw_error = R_INVALID_ID;
    static Rid g_vw_apply = R_INVALID_ID, g_vw_playing = R_INVALID_ID;
    static int g_vw_wanted_w = 0, g_vw_wanted_h = 0;
    static int g_vw_synced_w = -1, g_vw_synced_h = -1;

    static void ApplyVoxelWorldSize()
    {
        const int w = g_vw_wanted_w, h = g_vw_wanted_h;
        const uint64_t removed = HRL_CountVoxelsOutside(scene, w, h);
        if (removed == 0)
        {
            ResizeVoxelWorldWithUndo(w, h);
            return;
        }
        char text[256];
        std::snprintf(text, sizeof(text), "%llu voxel(s) are outside %d x %d and will be removed.\n"
                      "Ctrl+Z restores them (until the editor is closed).",
                      static_cast<unsigned long long>(removed), w, h);
        eui::Ask(g.window, "Shrink the voxel world ?", text, { "Resize", "Cancel" }, [w, h](int choice)
        {
            if (choice == 0)
                ResizeVoxelWorldWithUndo(w, h);
        });
    }

    static void UpdateVoxelWorld()
    {
        int width = 0, height = 0;
        HRL_GetVoxelSize(scene, &width, &height);
        // New level / undo : the fields follow the world.
        if (width != g_vw_synced_w || height != g_vw_synced_h)
        {
            g_vw_wanted_w = g_vw_synced_w = width;
            g_vw_wanted_h = g_vw_synced_h = height;
            Rspin_box_set_value(g_vw_width, width);
            Rspin_box_set_value(g_vw_height, height);
        }
        eui::SetText(Rwidget_find(g_vw_level, "value"), current_level_file.empty() ? "(none)" : current_level_file);
        eui::SetText(Rwidget_find(g_vw_file, "value"), world_file_path_string);
        char size[128];
        std::snprintf(size, sizeof(size), "Current size : %d x %d voxels  (chunks of %d)", width, height,
                      HRL_GetVoxelChunkSize(scene));
        eui::SetText(g_vw_size, size);

        const bool encodable = HRL_IsVoxelSizeEncodable(scene, g_vw_wanted_w, g_vw_wanted_h) != 0;
        const bool same = g_vw_wanted_w == width && g_vw_wanted_h == height;
        eui::SetVisible(g_vw_error, !encodable);
        eui::SetEnabled(g_vw_apply, encodable && !same && !isPlaying);
        eui::SetVisible(g_vw_playing, isPlaying);
    }

    // ---- Camera Shake ----------------------------------------------------------------

    static bool g_shake_loop = false;
    static float g_shake_loop_wait = 0.f;
    static Rid g_cs_test = R_INVALID_ID, g_cs_settings = R_INVALID_ID, g_cs_curves = R_INVALID_ID;
    static Rid g_cs_file = R_INVALID_ID, g_cs_save = R_INVALID_ID;
    static Rid g_cs_enabled = R_INVALID_ID, g_cs_x = R_INVALID_ID, g_cs_y = R_INVALID_ID, g_cs_roll = R_INVALID_ID;

    struct ShakeSlider
    {
        const char* name;
        float lynx::CameraShake::* field;
        const char* format;
        Rid slider = R_INVALID_ID;
        Rid value = R_INVALID_ID;
    };

    static ShakeSlider g_shake_sliders[] = {
        { "cs.duration", &lynx::CameraShake::duration, "%.2f s" },
        { "cs.position", &lynx::CameraShake::positionAmplitude, "%.2f vox" },
        { "cs.rotation", &lynx::CameraShake::rotationAmplitude, "%.2f deg" },
        { "cs.frequency", &lynx::CameraShake::frequency, "%.1f Hz" },
        { "cs.falloff", &lynx::CameraShake::falloff, "%.2f" },
    };

    static void PaintShakeCurves(RPainter* p, float width, float height)
    {
        lynx::CameraShake& shake = lynx::GetCameraShake();
        Rdraw_rect(p, 0.f, 0.f, width, height, 4.f, 24 / 255.f, 24 / 255.f, 28 / 255.f, 1.f);
        Rdraw_rect_outline(p, 0.f, 0.f, width, height, 4.f, 1.f, 70 / 255.f, 70 / 255.f, 80 / 255.f, 1.f);
        const float mid = height * 0.5f;
        Rdraw_line(p, 0.f, mid, width, mid, 1.f, 70 / 255.f, 70 / 255.f, 80 / 255.f, 1.f);

        const float range = std::max(0.001f, std::max(shake.positionAmplitude, shake.rotationAmplitude));
        const float half = height * 0.45f;
        const int samples = std::max(32, static_cast<int>(width / 2.f));
        struct Curve { int axis; float r, g2, b; bool on; };
        const Curve curves[] = {
            { 0, 235 / 255.f, 90 / 255.f, 90 / 255.f, shake.shake_x },
            { 1, 110 / 255.f, 210 / 255.f, 110 / 255.f, shake.shake_y },
            { 2, 100 / 255.f, 160 / 255.f, 1.f, shake.shake_roll },
        };
        lynx::CameraShake settings = shake;   // the settings alone (not a scaled shake from the game)
        settings.scale = 1.f;
        settings.length_scale = 1.f;
        for (const Curve& c : curves)
        {
            if (!c.on)
                continue;
            float px = 0.f, py = mid;
            for (int i = 0; i <= samples; ++i)
            {
                const float t = shake.duration * static_cast<float>(i) / static_cast<float>(samples);
                float v[3];
                settings.Sample(std::min(t, shake.duration * 0.9999f), v[0], v[1], v[2]);
                const float x = width * static_cast<float>(i) / static_cast<float>(samples);
                const float y = mid - v[c.axis] / range * half;
                if (i > 0)
                    Rdraw_line(p, px, py, x, y, 1.5f, c.r, c.g2, c.b, 1.f);
                px = x;
                py = y;
            }
        }
        if (shake.active && shake.duration > 0.f)
        {
            const float x = width * std::clamp(shake.elapsed / shake.Duration(), 0.f, 1.f);
            Rdraw_line(p, x, 0.f, x, height, 2.f, 1.f, 220 / 255.f, 120 / 255.f, 1.f);
        }
        Rdraw_text(p, R_INVALID_ID, 0.f, 6.f, 4.f, "X", 235 / 255.f, 90 / 255.f, 90 / 255.f, 1.f);
        Rdraw_text(p, R_INVALID_ID, 0.f, 24.f, 4.f, "Y", 110 / 255.f, 210 / 255.f, 110 / 255.f, 1.f);
        Rdraw_text(p, R_INVALID_ID, 0.f, 42.f, 4.f, "Roll", 100 / 255.f, 160 / 255.f, 1.f, 1.f);
    }

    static bool g_shake_dirty = false;

    static void ShakeChanged()
    {
        g_shake_dirty = true;
        Rwidget_invalidate(g_cs_curves);
    }

    static void InitCameraShake()
    {
        auto find = [](const char* name) { return eui::Find(g.window, name); };
        lynx::CameraShake& shake = lynx::GetCameraShake();
        g_cs_test = find("cs.test");
        g_cs_settings = find("cs.settings");
        g_cs_curves = find("cs.curves");
        g_cs_file = find("cs.file");
        g_cs_save = find("cs.save");
        g_cs_enabled = find("cs.enabled");
        g_cs_x = find("cs.x");
        g_cs_y = find("cs.y");
        g_cs_roll = find("cs.roll");

        eui::OnClick(g_cs_test, []() { if (!isPlaying) lynx::GetCameraShake().Trigger(); });
        eui::OnToggle(find("cs.loop"), [](bool on) { g_shake_loop = on; });
        eui::OnClick(find("cs.stop"), []()
        {
            g_shake_loop = false;
            eui::SetChecked(eui::Find(g.window, "cs.loop"), false);
            lynx::GetCameraShake().Stop();
        });
        eui::OnToggle(g_cs_enabled, [](bool on) { lynx::GetCameraShake().enabled = on; ShakeChanged(); });
        eui::OnToggle(g_cs_x, [](bool on) { lynx::GetCameraShake().shake_x = on; ShakeChanged(); });
        eui::OnToggle(g_cs_y, [](bool on) { lynx::GetCameraShake().shake_y = on; ShakeChanged(); });
        eui::OnToggle(g_cs_roll, [](bool on) { lynx::GetCameraShake().shake_roll = on; ShakeChanged(); });

        for (ShakeSlider& s : g_shake_sliders)
        {
            s.slider = find(s.name);
            s.value = find((std::string(s.name) + ".value").c_str());
            ShakeSlider* ps = &s;
            eui::OnValue(s.slider, [ps](double v)
            {
                lynx::GetCameraShake().*(ps->field) = static_cast<float>(v);
                eui::SetText(ps->value, Format(ps->format, v));
                ShakeChanged();
            });
        }

        auto preset = [&](const char* name, float duration, float pos, float rot, float freq, float falloff)
        {
            eui::OnClick(find(name), [duration, pos, rot, freq, falloff]()
            {
                lynx::CameraShake& s = lynx::GetCameraShake();
                s.duration = duration;
                s.positionAmplitude = pos;
                s.rotationAmplitude = rot;
                s.frequency = freq;
                s.falloff = falloff;
                ShakeChanged();
                if (!isPlaying)
                    s.Trigger();
            });
        };
        preset("cs.hit", 0.15f, 2.3f, 0.6f, 20.f, 1.25f);
        preset("cs.explosion", 0.6f, 6.f, 3.f, 25.f, 2.f);
        preset("cs.earthquake", 2.f, 3.f, 1.f, 8.f, 0.5f);
        preset("cs.rumble", 1.f, 0.8f, 0.2f, 40.f, 0.3f);

        eui::OnPaint(g_cs_curves, PaintShakeCurves);
        eui::OnClick(g_cs_save, []() { g_shake_dirty = !lynx::SaveCameraShakeSettings(); });
        eui::OnClick(find("cs.revert"), []()
        {
            lynx::CameraShake& s = lynx::GetCameraShake();
            lynx::CameraShake defaults;
            s.duration = defaults.duration;
            s.positionAmplitude = defaults.positionAmplitude;
            s.rotationAmplitude = defaults.rotationAmplitude;
            s.frequency = defaults.frequency;
            s.falloff = defaults.falloff;
            s.shake_x = s.shake_y = s.shake_roll = true;
            lynx::LoadCameraShakeSettings();
            g_shake_dirty = false;
            Rwidget_invalidate(g_cs_curves);
        });
        (void)shake;
    }

    static void UpdateCameraShake(float dt)
    {
        lynx::CameraShake& shake = lynx::GetCameraShake();
        eui::SetEnabled(g_cs_test, !isPlaying && shake.enabled);
        eui::SetText(g_cs_test, shake.active ? "Shaking..." : "Test");
        eui::SetTooltip(g_cs_test, isPlaying
            ? "Stop the game to test in the editor\n(in game : Engine.cameraShake() / GetCameraShake().Trigger())"
            : "Shakes the editor camera with these settings");
        eui::SetEnabled(g_cs_settings, shake.enabled);
        eui::SetChecked(g_cs_enabled, shake.enabled);
        eui::SetChecked(g_cs_x, shake.shake_x);
        eui::SetChecked(g_cs_y, shake.shake_y);
        eui::SetChecked(g_cs_roll, shake.shake_roll);
        for (ShakeSlider& s : g_shake_sliders)
        {
            eui::SetValue(s.slider, shake.*(s.field));
            eui::SetText(s.value, Format(s.format, shake.*(s.field)));
        }
        eui::SetEnabled(g_cs_save, g_shake_dirty);
        eui::SetText(g_cs_file, g_shake_dirty ? "assets/camera_shake.json (not saved)" : "assets/camera_shake.json");

        // Loop : a short pause between two shakes.
        if (g_shake_loop && !isPlaying && shake.enabled && !shake.active)
        {
            g_shake_loop_wait += dt;
            if (g_shake_loop_wait >= 0.4f)
            {
                g_shake_loop_wait = 0.f;
                shake.Trigger();
            }
        }
        if (shake.active)
            Rwidget_invalidate(g_cs_curves);   // the play head moves
    }

    // ---- binding ---------------------------------------------------------------------

    static void Init()
    {
        auto find = [](const char* name) { return eui::Find(g.window, name); };

        BuildParams<PostProcessApi>(find("pp.body"), g_pp_refresh);
        g_pp_file = find("pp.file");
        eui::OnClick(find("pp.save"), []() { lynx::postprocess::Save(); });
        eui::OnClick(find("pp.revert"), []() { lynx::postprocess::Load(); });
        eui::OnClick(find("pp.reset"), []() { lynx::postprocess::Reset(); });

        BuildParams<Lighting2DApi>(find("l2.body"), g_l2_refresh);
        g_l2_file = find("l2.file");
        g_l2_status = find("l2.status");
        eui::OnClick(find("l2.save"), []() { lynx::lighting2d::Save(); });
        eui::OnClick(find("l2.revert"), []() { lynx::lighting2d::Load(); });
        eui::OnClick(find("l2.reset"), []() { lynx::lighting2d::Reset(); });

        g_vw_level = find("vw.level");
        g_vw_file = find("vw.file");
        g_vw_size = find("vw.size");
        g_vw_width = find("vw.width");
        g_vw_height = find("vw.height");
        g_vw_error = find("vw.error");
        g_vw_apply = find("vw.apply");
        g_vw_playing = find("vw.playing");
        eui::OnValue(g_vw_width, [](double v) { g_vw_wanted_w = std::clamp(static_cast<int>(v), 16, 1 << 30); });
        eui::OnValue(g_vw_height, [](double v) { g_vw_wanted_h = std::clamp(static_cast<int>(v), 16, 1 << 30); });
        for (int preset : { 1024, 4096, 16384, 65536, 131072, 262144 })
        {
            const std::string name = "vw.p" + std::to_string(preset);
            eui::OnClick(find(name.c_str()), [preset]()
            {
                g_vw_wanted_w = g_vw_wanted_h = preset;
                Rspin_box_set_value(g_vw_width, preset);
                Rspin_box_set_value(g_vw_height, preset);
            });
        }
        eui::OnClick(g_vw_apply, []() { ApplyVoxelWorldSize(); });
        eui::OnClick(find("vw.reset"), []()
        {
            g_vw_synced_w = g_vw_synced_h = -1;   // the fields take the world again
        });

        InitCameraShake();
    }

    static void Update(float dt)
    {
        // Only what is shown is followed.
        if (IsPanelShown(g.post_process))
        {
            for (const auto& refresh : g_pp_refresh)
                refresh();
            eui::SetText(g_pp_file, lynx::postprocess::IsDirty() ? "assets/postprocess.json (not saved)" : "assets/postprocess.json");
        }
        if (IsPanelShown(g.lighting2d))
        {
            for (const auto& refresh : g_l2_refresh)
                refresh();
            eui::SetText(g_l2_file, lynx::lighting2d::IsDirty() ? "assets/lighting2d.json (not saved)" : "assets/lighting2d.json");
            std::string status;
            if (!lynx::lighting2d::GetError().empty())
                status = lynx::lighting2d::GetError();
            else if (lynx::lighting2d::IsEnabled())
                status = std::to_string(lynx::lighting2d::GetDrawnLightCount()) + " light(s) drawn (max " +
                         std::to_string(lynx::lighting2d::kMaxLights) + " per view). Add lights : Place Actors > Light2DActor.";
            else
                status = "Off : tick Enabled to light the level with the 2D lights.";
            eui::SetText(g_l2_status, status);
            eui::SetClass(g_l2_status, "error", !lynx::lighting2d::GetError().empty());
        }
        if (IsPanelShown(g.voxel_world))
            UpdateVoxelWorld();
        UpdateCameraShake(dt);
    }
}
