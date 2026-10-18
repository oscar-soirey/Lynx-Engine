// =============================================================================
// Shell of the editor window : header, toolbar, status bar, the panels that
// open / close (editor_windows.txt, editor_layout.txt), the dialogs of the
// editor (quit, warnings) and the shortcuts of the level editor.
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::shell
{
    namespace ide = lynx::editor::external_ide;

    static const char* const kLayoutFile = "editor_layout.txt";

    struct Toolbar
    {
        Rid menubar = R_INVALID_ID;
        Rid save = R_INVALID_ID, save_dirty = R_INVALID_ID, undo = R_INVALID_ID;
        Rid translate = R_INVALID_ID, rotate = R_INVALID_ID, scale = R_INVALID_ID;
        Rid play = R_INVALID_ID, simulate = R_INVALID_ID, simulating = R_INVALID_ID;
        Rid compile = R_INVALID_ID, game_menu = R_INVALID_ID, status = R_INVALID_ID, status_text = R_INVALID_ID;
        Rid view = R_INVALID_ID, view_text = R_INVALID_ID;
        Rid header = R_INVALID_ID, version = R_INVALID_ID, lynxie = R_INVALID_ID;
        Rid st_level = R_INVALID_ID, st_mode = R_INVALID_ID, st_hint = R_INVALID_ID, st_fps = R_INVALID_ID;
    };

    static Toolbar t;
    static std::unique_ptr<eui::Menu> g_menu;          // the menu of the toolbar being shown
    static double g_fps = 0.0;
    static std::vector<Rid> g_ide_textures;             // Raven textures of the code editors
    static bool g_was_playing = false;
    static bool g_close_dialog_open = false;

    static void SetFps(double fps)
    {
        g_fps = fps;
    }

    // ---- code editors ----------------------------------------------------------

    static Rid IdeTexture(ide::Ide which)
    {
        const int i = static_cast<int>(which);
        if (i < 0)
            return R_INVALID_ID;
        if (g_ide_textures.size() < static_cast<size_t>(ide::Ide::Count))
            g_ide_textures.assign(static_cast<size_t>(ide::Ide::Count), R_INVALID_ID);
        if (g_ide_textures[static_cast<size_t>(i)] == R_INVALID_ID)
        {
            std::vector<uint8_t> rgba;
            int size = 0;
            if (ide::IconPixels(which, rgba, size))
                g_ide_textures[static_cast<size_t>(i)] = Rtexture_create_from_pixels(rgba.data(), size, size);
        }
        return g_ide_textures[static_cast<size_t>(i)];
    }

    static std::filesystem::path GameFolder()
    {
        return currentProject.root.empty() ? std::filesystem::current_path() : currentProject.root;
    }

    static void OpenIn(ide::Ide which)
    {
        std::string error;
        if (ide::Open(which, GameFolder(), error))
            ide::SetLastUsed(which);
        else
            ShowEditorWarning(error);
    }

    // ---- menus of the toolbar ------------------------------------------------------

    static eui::Menu& NewMenu()
    {
        g_menu = std::make_unique<eui::Menu>(g.window);
        return *g_menu;
    }

    // Opens the menu under a toolbar button.
    static void OpenUnder(eui::Menu& menu, Rid button)
    {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        Rwidget_get_rect(button, &x, &y, &w, &h);
        menu.Open(x, y + h + 2.f);
    }

    static void RequestReload()
    {
        module_changed_notice = false;
        reload_game_requested = true;
    }

    static void GameMenu(Rid under)
    {
        eui::Menu& menu = NewMenu();
        const bool building = lynx::editor::game_build::IsRunning();

        if (!currentProject.script_only)
            menu.Item("Compile", []() { lynx::editor::game_build::Start(); }, !building);
        menu.Item("Reload Game", []() { RequestReload(); });

        if (!currentProject.script_only)
        {
            menu.Check("Build output", g_build_panel_open, []()
            {
                if (g_build_panel_open)
                    HidePanel(g.build);
                else
                    ShowPanel(g.build);
            });
            menu.Separator();
            menu.Title("Options");
            menu.Check("Reload after Compile", appSettings.reloadAfterBuild, []()
            {
                appSettings.reloadAfterBuild = !appSettings.reloadAfterBuild;
                SaveSettings();
            });
            menu.Check("Reload when the DLL is rebuilt", appSettings.autoReloadOnChange, []()
            {
                appSettings.autoReloadOnChange = !appSettings.autoReloadOnChange;
                SaveSettings();
            });
        }

        menu.Separator();
        menu.Item("Ship Game...", []() { lynx::editor::ship_game::Open(); },
                  !isPlaying && !lynx::editor::ship_game::IsBusy());
        OpenUnder(menu, under);
    }

    // Camera speed + master volume : a small dialog with both sliders.
    static void ViewDialog()
    {
        static const char kXml[] =
            "<VBox Spacing='8' Width='360'>"
            "  <Label Text='Editor camera and sound' Class='dialog-title'/>"
            "  <PropertyRow Label='Camera speed'>"
            "    <Slider Name='speed' Min='1' Max='400' Step='1' Slot.Fill='1'/>"
            "    <Label Name='speed.value' Width='44' TextAlign='Right' Slot.VAlign='Center'/>"
            "  </PropertyRow>"
            "  <PropertyRow Label='Master volume'>"
            "    <Slider Name='volume' Min='0' Max='2' Step='0.01' Slot.Fill='1'/>"
            "    <Label Name='volume.value' Width='44' TextAlign='Right' Slot.VAlign='Center'/>"
            "  </PropertyRow>"
            "  <HBox><Spacer Slot.Fill='1'/><Button Name='close' Text='Close' Style='Primary'/></HBox>"
            "</VBox>";
        Rid box = Rwidget_create_from_xml(kXml, sizeof(kXml) - 1);
        if (box == R_INVALID_ID)
            return;
        Rid speed = eui::Find(box, "speed");
        Rid speed_value = eui::Find(box, "speed.value");
        Rid volume = eui::Find(box, "volume");
        Rid volume_value = eui::Find(box, "volume.value");
        Rslider_set_value(speed, cameraSpeed);
        Rslider_set_value(volume, appSettings.masterVolume);
        eui::SetText(speed_value, Format("%.0f", cameraSpeed));
        eui::SetText(volume_value, Format("%.2f", appSettings.masterVolume));
        eui::OnValue(speed, [speed_value](double v)
        {
            cameraSpeed = static_cast<float>(v);
            appSettings.cameraSpeed = cameraSpeed;
            eui::SetText(speed_value, Format("%.0f", v));
            SaveSettings();
        });
        eui::OnValue(volume, [volume_value](double v)
        {
            appSettings.masterVolume = static_cast<float>(v);
            lynx::SetMasterVolume(appSettings.masterVolume);
            eui::SetText(volume_value, Format("%.2f", v));
            SaveSettings();
        });
        eui::OnClick(eui::Find(box, "close"), [box]() { Rdialog_close(box, 1); });
        Rdialog_open(g.window, box, nullptr, nullptr);
    }

    // ---- quit ----------------------------------------------------------------------

    static void QuitDialog()
    {
        if (g_close_dialog_open)
            return;

        const bool scripts_dirty =
            lynx::editor::script_editors::HasUnsavedChanges() ||
            lynx::editor::widget_editor::HasUnsavedChanges() ||
            lynx::editor::graph_editors::HasUnsavedChanges() ||
            lynx::editor::particle_editor::HasUnsavedChanges() ||
            lynx::editor::vasm_editor::HasUnsavedChanges() ||
            lynx::editor::voxel_types::HasUnsavedChanges();

        std::string text;
        if (switch_project_requested)
            text += "The project browser opens after this editor closes.\n\n";

        std::vector<std::string> buttons;
        if (editor_dirty || scripts_dirty)
        {
            if (editor_dirty)
                text += "The level has unsaved changes.\n";
            if (scripts_dirty)
                text += "Some files have unsaved changes.\n";
            text += "Do you want to save before quitting?";
            buttons = { "Save and Quit", "Quit without Saving", "Cancel" };
        }
        else
        {
            text += "There are no unsaved level changes.";
            buttons = { "Quit", "Cancel" };
        }

        const bool dirty = editor_dirty || scripts_dirty;
        g_close_dialog_open = true;
        eui::Ask(g.window, "Save before quitting?", text, buttons, [dirty](int choice)
        {
            g_close_dialog_open = false;
            const int cancel = dirty ? 2 : 1;
            if (choice < 0 || choice == cancel)
            {
                switch_project_requested = false;
                return;
            }
            if (dirty && choice == 0)
            {
                if (editor_dirty)
                    SaveEditor();
                lynx::editor::voxel_types::Save();
                lynx::editor::script_editors::SaveAll();
                lynx::editor::widget_editor::SaveAll();
                lynx::editor::graph_editors::SaveAll();
                lynx::editor::particle_editor::SaveAll();
                lynx::editor::vasm_editor::SaveAll();
            }
            editor_quit = true;
        });
    }

    static int OnCloseRequest(Rid, void*)
    {
        show_close_confirmation = true;
        return R_FALSE;   // the dialog decides (editor_quit)
    }

    // ---- layout -----------------------------------------------------------------------

    // Panels of editor_windows.txt (the layout file knows where they are).
    static void ApplyWindowFlags()
    {
        for (const PanelEntry& e : PanelEntries())
        {
            if (*e.panel == R_INVALID_ID)
                continue;
            if (*e.flag)
            {
                if (!IsPanelOpen(*e.panel))
                    OpenPanel(*e.panel);
            }
            else
                HidePanel(*e.panel);
        }
    }

    static void LoadLayout()
    {
        g_build_panel_open = lynx::editor::game_build::WindowOpen();
        {
            std::ifstream in(kLayoutFile, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!text.empty() && !Rdock_area_load_layout(g.dock, text.c_str()))
                std::cerr << "[UI] " << kLayoutFile << " : layout of an older editor, the default one is used\n";
        }
        ApplyWindowFlags();

        // The tabs in front : Viewport, Outliner, Details, Console.
        for (Rid panel : { g.viewport_panel, g.outliner, g.details, g.console })
            if (panel != R_INVALID_ID && IsPanelOpen(panel))
                FrontPanel(panel);
    }

    static void SaveLayout()
    {
        if (g.dock == R_INVALID_ID)
            return;
        if (const char* layout = Rdock_area_save_layout(g.dock))
        {
            std::ofstream out(kLayoutFile, std::ios::binary | std::ios::trunc);
            out << layout;
        }
    }

    // The flags follow the panels the user closed (x of the tab) or opened.
    static void SyncWindowFlags()
    {
        bool changed = false;
        for (const PanelEntry& e : PanelEntries())
        {
            if (*e.panel == R_INVALID_ID)
                continue;
            const bool open = IsPanelOpen(*e.panel) != 0;
            if (open != *e.flag)
            {
                *e.flag = open;
                changed = true;
            }
        }
        lynx::editor::game_build::WindowOpen() = g_build_panel_open;
        if (changed)
            SaveEditorWindowVisibility();
    }

    // Play : every panel stays where it is ; the keyboard goes to the game.
    static void FollowPlay()
    {
        if (isPlaying == g_was_playing)
            return;
        g_was_playing = isPlaying;
        if (isPlaying)
        {
            if (g.documents != R_INVALID_ID && g.level_page != R_INVALID_ID && Rwidget_is_valid(g.documents))
                Rtab_view_show_tab(g.documents, g.level_page);   // the game is in the level tab
            Rwidget_focus(g.viewport);
        }
    }

    // ---- shortcuts of the level editor (ImGui used to poll them) ----------------------

    static bool OtherEditorHasFocus()
    {
        return !lynx::editor::documents::LevelTabActive() ||   // another tab is in front
               lynx::editor::input_settings::BlocksEditorShortcuts() ||
               lynx::editor::widget_editor::HasFocus() ||
               lynx::editor::graph_editors::HasFocus() ||
               lynx::editor::particle_editor::HasFocus() ||
               lynx::editor::vasm_editor::HasFocus() ||
               lynx::editor::plugins::HasFocus();
    }

    // The selected actors (or the one in Details).
    static void DeleteSelection()
    {
        if (!editing_actor || isPlaying)
            return;
        PruneActorSelection();
        std::vector<lynx::Actor*> to_delete = selected_actors;
        if (to_delete.empty())
            to_delete.push_back(editing_actor);
        DeleteActors(to_delete);
    }

    static void Shortcuts()
    {
        if (isPlaying || Rwindow_has_popup(g.window) || ein::WantTextInput())
            return;

        // Command palette : Ctrl+P (everything), Ctrl+Shift+P (commands).
        namespace km = lynx::editor::keymap;
        if (km::Pressed(km::Action::CommandPalette) && !lynx::editor::command_palette::IsOpen() &&
            !lynx::editor::input_settings::BlocksEditorShortcuts())
            OpenCommandPalette(ein::Shift() ? ">" : "");

        // Undo.
        if (km::Pressed(km::Action::Undo) && !OtherEditorHasFocus() && !brushPainting)
            UndoLastEditorAction();

        // Delete : the selected actors (the Content Browser deletes its files).
        content_browser_focused = HasFocus(g.content_browser);
        if (km::Pressed(km::Action::DeleteSelection) && editing_actor && !content_browser_focused && !OtherEditorHasFocus())
            DeleteSelection();
    }

    // Messages of the editor : one dialog at a time.
    static void Dialogs()
    {
        if (show_close_confirmation)
        {
            show_close_confirmation = false;
            QuitDialog();
        }
        if (editor_warning_pending)
        {
            editor_warning_pending = false;
            eui::Message(g.window, "Warning", editor_warning);
        }
        if (show_copy_error)
        {
            show_copy_error = false;
            eui::Message(g.window, "Content Browser", copy_error_message);
        }
    }

    // ---- Options : Project page (settings.cfg) and Plugins page -----------------------

    static void BindProjectPage()
    {
        auto find = [](const char* name) { return eui::Find(g.window, name); };
        Rid reload = find("project.reload_after_build");
        Rid auto_reload = find("project.auto_reload");
        Rid vsync = find("project.vsync");
        Rid volume = find("project.volume");
        Rid volume_value = find("project.volume.value");
        Rid speed = find("project.camera_speed");
        Rid speed_value = find("project.camera_speed.value");
        Rid mode = find("project.render_mode");
        Rid renderer = find("project.renderer");

        auto show = [=]()
        {
            eui::SetChecked(reload, appSettings.reloadAfterBuild);
            eui::SetChecked(auto_reload, appSettings.autoReloadOnChange);
            eui::SetChecked(vsync, appSettings.vsync);
            eui::SetValue(volume, appSettings.masterVolume);
            eui::SetText(volume_value, Format("%.2f", appSettings.masterVolume));
            eui::SetValue(speed, appSettings.cameraSpeed);
            eui::SetText(speed_value, Format("%.0f", appSettings.cameraSpeed));
            if (Rcombo_box_get_selected(mode) != static_cast<int>(appSettings.voxelRenderMode))
                Rcombo_box_set_selected(mode, static_cast<int>(appSettings.voxelRenderMode));
            // Items="Vulkan|OpenGL" : the order of lynx::host::RenderBackend.
            if (Rcombo_box_get_selected(renderer) != static_cast<int>(appSettings.renderer))
                Rcombo_box_set_selected(renderer, static_cast<int>(appSettings.renderer));
        };

        eui::OnToggle(reload, [](bool on) { appSettings.reloadAfterBuild = on; SaveSettings(); });
        eui::OnToggle(auto_reload, [](bool on) { appSettings.autoReloadOnChange = on; SaveSettings(); });
        eui::OnToggle(vsync, [](bool on)
        {
            appSettings.vsync = on;
            ApplyRuntimeSettings();
            SaveSettings();
        });
        eui::OnValue(volume, [volume_value](double v)
        {
            appSettings.masterVolume = static_cast<float>(v);
            lynx::SetMasterVolume(appSettings.masterVolume);
            eui::SetText(volume_value, Format("%.2f", v));
            SaveSettings();
        });
        eui::OnValue(speed, [speed_value](double v)
        {
            appSettings.cameraSpeed = static_cast<float>(v);
            cameraSpeed = appSettings.cameraSpeed;
            eui::SetText(speed_value, Format("%.0f", v));
            SaveSettings();
        });
        eui::OnSelect(mode, [](int index)
        {
            if (index < 0 || index == static_cast<int>(appSettings.voxelRenderMode))
                return;
            appSettings.voxelRenderMode = static_cast<HRL_EVoxelRenderMode>(index);
            HRL_SetVoxelRenderMode(scene, appSettings.voxelRenderMode);
            SaveSettings();
        });
        eui::OnSelect(renderer, [](int index)
        {
            if (index < 0 || index > 1 || index == static_cast<int>(appSettings.renderer))
                return;
            appSettings.renderer = static_cast<lynx::host::RenderBackend>(index);
            SaveSettings();   // read by LynxRuntime / the shipped game at their start
        });
        eui::OnClick(find("project.reset"), [show]()
        {
            appSettings = AppSettings{};
            cameraSpeed = appSettings.cameraSpeed;
            ApplyRuntimeSettings();
            SaveSettings();
            show();
        });
        show();
        // The camera speed / volume also change from the toolbar.
        eui::Every(0.5, [show]() { if (IsPanelShown(g.options)) show(); });
    }

    // ---- Options > Plugins : a card per plugin, search, filter -------------------------

    static bool g_plugins_changed = false;   // toggled this session : restart banner

    static const char kPluginCard[] =
        "<Border Class='toolgroup' BorderWidth='1' Padding='12,10'>"
        "  <VBox Spacing='6'>"
        "    <HBox Spacing='10'>"
        "      <Custom Name='avatar' Width='40' Height='40' Slot.VAlign='Center'/>"
        "      <VBox Spacing='1' Slot.Fill='1' Slot.VAlign='Center'>"
        "        <HBox Spacing='8'>"
        "          <Label Name='name' Class='dialog-title'/>"
        "          <Label Name='version' Class='dim' Slot.VAlign='Center'/>"
        "        </HBox>"
        "        <Label Name='info' Class='dim'/>"
        "      </VBox>"
        "      <Checkbox Name='enabled' Text='Enabled' Style='Switch' Slot.VAlign='Center'/>"
        "    </HBox>"
        "    <HBox Name='badges' Spacing='10'>"
        "      <Label Name='source' Class='section'/>"
        "      <Label Name='category' Class='dim'/>"
        "      <Label Name='state'/>"
        "      <Label Name='editor' Text='Editor module' Class='dim' Visibility='Collapsed' Tooltip='Its editor module (windows, menus) needs the new plugin API of the editor (coming) : the runtime part works.'/>"
        "    </HBox>"
        "    <Label Name='older' Class='warning' Wrap='true' Visibility='Collapsed'/>"
        "    <Label Name='desc' Wrap='true'/>"
        "    <Label Name='error' Class='error' Wrap='true' Visibility='Collapsed'/>"
        "    <HBox Spacing='6'>"
        "      <Label Name='note' Class='warning' Slot.Fill='1' Slot.VAlign='Center'/>"
        "      <Button Name='folder' Text='Open Folder' Class='tool'/>"
        "    </HBox>"
        "  </VBox>"
        "</Border>";

    static void PaintPluginAvatar(RPainter* p, float w, float h, const std::string& name)
    {
        uint32_t hash = 2166136261u;
        for (unsigned char c : name)
            hash = (hash ^ c) * 16777619u;
        float r = 0.5f, gr = 0.5f, b = 0.5f;
        Rcolor_hsv_to_rgb(static_cast<float>(hash % 360u) / 360.f, 0.55f, 0.78f, &r, &gr, &b);
        Rdraw_rect(p, 0.f, 0.f, w, h, 8.f, r, gr, b, 1.f);
        const std::string letter = name.empty() ? std::string("?")
            : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
        float tw = 0.f, th = 0.f;
        Rfont_measure_text(R_INVALID_ID, 22.f, letter.c_str(), &tw, &th);
        if (th <= 0.f)
            th = 22.f;
        Rdraw_text(p, R_INVALID_ID, 22.f, (w - tw) * 0.5f, (h - th) * 0.5f, letter.c_str(), 1.f, 1.f, 1.f, 1.f);
    }

    static void FillPluginsPage()
    {
        Rid list = eui::Find(g.window, "plugins.list");
        Rid search = eui::Find(g.window, "plugins.search");
        Rid filter = eui::Find(g.window, "plugins.filter");
        Rwidget_clear_children(list);
        eui::SetVisible(eui::Find(g.window, "plugins.restart"), g_plugins_changed);

        auto& plugins = lynx::plugins::GetPlugins();
        int enabled = 0, loaded = 0;
        for (const auto& info : plugins)
        {
            enabled += info.enabled;
            loaded += info.loaded;
        }
        eui::SetText(eui::Find(g.window, "plugins.summary"), plugins.empty()
            ? std::string("No plugin : <editor>/plugins and <project>/plugins hold the plugins (a folder with plugin.json).")
            : std::to_string(plugins.size()) + " plugins  |  " + std::to_string(enabled) + " enabled for this project  |  " +
              std::to_string(loaded) + " loaded");

        const char* text = Rtext_input_get_text(search);
        const std::string needle = Lower(text ? text : "");
        const int mode = std::max(0, Rcombo_box_get_selected(filter));   // All, Enabled, Disabled, Engine, Project

        int shown = 0;
        for (const lynx::plugins::PluginInfo& info : plugins)
        {
            if ((mode == 1 && !info.enabled) || (mode == 2 && info.enabled) ||
                (mode == 3 && !info.engine_plugin) || (mode == 4 && info.engine_plugin))
                continue;
            if (!needle.empty() && Lower(info.name).find(needle) == std::string::npos &&
                Lower(info.description).find(needle) == std::string::npos &&
                Lower(info.author).find(needle) == std::string::npos &&
                Lower(info.category).find(needle) == std::string::npos)
                continue;

            Rid card = Rwidget_create_from_xml(kPluginCard, sizeof(kPluginCard) - 1);
            if (card == R_INVALID_ID)
                continue;
            Rwidget_add_child(list, card);
            ++shown;
            auto find = [card](const char* n) { return Rwidget_find(card, n); };
            const std::string name = info.name;

            eui::OnPaint(find("avatar"), [name](RPainter* p, float w, float h) { PaintPluginAvatar(p, w, h, name); });
            eui::SetText(find("name"), info.name);
            eui::SetText(find("version"), info.version.empty() ? std::string() : "v" + info.version);
            eui::SetText(find("info"), info.author.empty() ? std::string("Unknown author") : "by " + info.author);
            eui::SetText(find("source"), info.engine_plugin ? "ENGINE" : "PROJECT");
            eui::SetTooltip(find("source"), info.engine_plugin ? "Shipped with Lynx (<editor>/plugins)" : "In this project (<project>/plugins)");
            eui::SetText(find("category"), info.category);

            const Rid state = find("state");
            if (!info.error.empty())
            {
                eui::SetText(state, "Error");
                Rwidget_add_class(state, "error");
            }
            else if (info.loaded)
            {
                eui::SetText(state, "Loaded");
                Rwidget_add_class(state, "success");
            }
            else
            {
                eui::SetText(state, info.enabled ? "Enabled, not loaded yet" : "Disabled");
                Rwidget_add_class(state, "dim");
            }
            eui::SetVisible(find("editor"), !info.editor_dll.empty());

            const bool older = !info.engine_min.empty() &&
                               lynx::plugins::CompareVersions(info.engine_min, lynx::GetEngineVersion()) < 0;
            if (older)
            {
                eui::SetText(find("older"), "Made for Lynx " + info.engine_min + " (this editor : " + lynx::GetEngineVersion() + ").");
                eui::SetVisible(find("older"), true);
            }
            eui::SetText(find("desc"), info.description.empty() ? std::string("No description.") : info.description);
            if (!info.description.empty())
                Rlabel_set_markdown(find("desc"), R_TRUE);
            if (!info.error.empty())
            {
                eui::SetText(find("error"), info.error);
                eui::SetVisible(find("error"), true);
            }

            const Rid check = find("enabled");
            const Rid note = find("note");
            eui::SetChecked(check, info.enabled);
            const std::filesystem::path folder = info.folder;
            eui::OnClick(find("folder"), [folder]()
            {
#ifdef _WIN32
                ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
            });

            const std::string engine_min = info.engine_min;
            eui::OnToggle(check, [name, note, check, older, engine_min](bool on)
            {
                if (const auto* plugin = lynx::plugins::FindPlugin(name); plugin && plugin->enabled == on)
                    return;   // set back by the code (Cancel)
                auto apply = [name, note](bool value)
                {
                    lynx::plugins::SetEnabled(name, value);
                    lynx::plugins::SaveSettings();
                    g_plugins_changed = true;
                    eui::SetText(note, "Restart the editor to " + std::string(value ? "load" : "unload") + " it.");
                    eui::SetVisible(eui::Find(g.window, "plugins.restart"), true);
                };
                if (on && older)
                {
                    eui::Ask(g.window, "Older plugin", name + " was made for Lynx " + engine_min +
                             ". It may not work with this version (" + lynx::GetEngineVersion() + "). Enable it anyway ?",
                             { "Enable", "Cancel" }, [apply, check](int choice)
                    {
                        if (choice == 0)
                            apply(true);
                        else
                            eui::SetChecked(check, false);
                    });
                    return;
                }
                apply(on);
            });
        }

        if (!plugins.empty() && shown == 0)
        {
            Rid none = Rwidget_add_child(list, Rlabel_create("No plugin matches the search."));
            Rwidget_add_class(none, "dim");
        }
    }

    static void BindPluginsPage()
    {
        eui::OnText(eui::Find(g.window, "plugins.search"), [](const std::string&) { eui::Later([]() { FillPluginsPage(); }); });
        eui::OnSelect(eui::Find(g.window, "plugins.filter"), [](int) { eui::Later([]() { FillPluginsPage(); }); });
        eui::OnClick(eui::Find(g.window, "plugins.folder"), []()
        {
            const std::filesystem::path folder = GameFolder() / "plugins";
            std::error_code ec;
            std::filesystem::create_directories(folder, ec);
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        });
        FillPluginsPage();
    }

    // ---- menu bar of the header (File, Edit, Window, Game, Tools, Help) --------------
    // Built once ; enabled / checked states, shortcuts (Options > Keyboard) and the
    // texts that change follow the editor at every frame (UpdateMenuBar).

    namespace km = lynx::editor::keymap;

    struct MenuEntry
    {
        Rid item = R_INVALID_ID;
        std::function<bool()> enabled;            // empty : always
        std::function<bool()> checked;            // empty : not checkable
        std::function<std::string()> text;        // empty : fixed text
        int action = -1;                          // km::Action shown as shortcut
        std::string shown_shortcut = "\x01";
        std::string shown_text;
        int shown_checked = -1;
    };

    static std::vector<MenuEntry> g_menu_entries;

    static Rid MenuAdd(Rid menu, const std::string& text, std::function<void()> fn,
                       std::function<bool()> enabled = {}, int action = -1, std::function<bool()> checked = {})
    {
        const Rid item = Rmenu_add_item(menu, text.c_str());
        if (fn)
            eui::OnMenuItem(item, std::move(fn));
        if (checked)
            Rmenu_item_set_checkable(item, R_TRUE);
        MenuEntry e;
        e.item = item;
        e.enabled = std::move(enabled);
        e.checked = std::move(checked);
        e.action = action;
        e.shown_text = text;
        g_menu_entries.push_back(std::move(e));
        return item;
    }

    static void MenuText(Rid item, std::function<std::string()> text)
    {
        for (MenuEntry& e : g_menu_entries)
            if (e.item == item)
                e.text = std::move(text);
    }

    static int KeyOf(km::Action a) { return static_cast<int>(a); }

    static void OpenUrl(const char* url)
    {
#ifdef _WIN32
        ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
        (void)url;
#endif
    }

    static void SaveAll()
    {
        if (isPlaying)
            return;
        if (editor_dirty)
            SaveEditor();
        lynx::editor::voxel_types::Save();
        lynx::editor::script_editors::SaveAll();
        lynx::editor::widget_editor::SaveAll();
        lynx::editor::graph_editors::SaveAll();
        lynx::editor::particle_editor::SaveAll();
        lynx::editor::vasm_editor::SaveAll();
    }

    static void ShowOptions(lynx::editor::options::Page page)
    {
        ShowPanel(g.options);
        lynx::editor::options::OpenPage(page);
    }

    static void BuildMenuBar()
    {
        const Rid bar = t.menubar;
        if (bar == R_INVALID_ID)
            return;
        using Icon = lynx::editor::icons::Icon;
        auto editing = []() { return !isPlaying; };

        // File
        {
            const Rid m = Rmenu_add_menu(bar, "File");
            MenuAdd(m, "Save Level", []() { if (!isPlaying) SaveEditor(); }, editing, KeyOf(km::Action::Save));
            MenuAdd(m, "Save All", []() { SaveAll(); }, editing);
            Rmenu_add_separator(m);
            MenuAdd(m, "Open Project...", []()
            {
                switch_project_requested = true;
                show_close_confirmation = true;
            }, editing);
            MenuAdd(m, "Project Settings...", []() { ShowOptions(lynx::editor::options::Page::Project); });
            MenuAdd(m, "Ship Game...", []() { lynx::editor::ship_game::Open(); },
                    []() { return !isPlaying && !lynx::editor::ship_game::IsBusy(); });
            Rmenu_add_separator(m);
            const Rid open_ide = MenuAdd(m, "Open in the Code Editor", []() { OpenIn(ide::LastUsed()); });
            MenuText(open_ide, []() { return std::string("Open in ") + ide::Name(ide::LastUsed()); });
            const Rid with = Rmenu_add_menu(m, "Open the Game Folder with");
            for (int i = 0; i < static_cast<int>(ide::Ide::Count); ++i)
            {
                const auto which = static_cast<ide::Ide>(i);
                const Rid item = MenuAdd(with, ide::Name(which), [which]() { OpenIn(which); },
                                         [which]() { return !ide::DetectionDone() || ide::IsInstalled(which); });
                MenuText(item, [which]()
                {
                    std::string text = ide::Name(which);
                    if (ide::DetectionDone() && !ide::IsInstalled(which))
                        text += "  (not found)";
                    return text;
                });
            }
            MenuAdd(m, "Show in Explorer", []()
            {
#ifdef _WIN32
                ShellExecuteW(nullptr, L"open", GameFolder().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
            });
            Rmenu_add_separator(m);
            MenuAdd(m, "Quit", []() { show_close_confirmation = true; });
        }

        // Edit
        {
            const Rid m = Rmenu_add_menu(bar, "Edit");
            MenuAdd(m, "Undo", []() { if (!isPlaying && !brushPainting) UndoLastEditorAction(); },
                    []() { return !isPlaying && !brushPainting; }, KeyOf(km::Action::Undo));
            MenuAdd(m, "Delete", []() { DeleteSelection(); },
                    []() { return !isPlaying && editing_actor != nullptr; }, KeyOf(km::Action::DeleteSelection));
            Rmenu_add_separator(m);
            MenuAdd(m, "Command Palette...", []() { OpenCommandPalette(""); }, {}, KeyOf(km::Action::CommandPalette));
            Rmenu_add_separator(m);
            eui::SetMenuIcon(MenuAdd(m, "Options...", []() { ShowOptions(lynx::editor::options::Page::Interface); }), Icon::Settings);
            MenuAdd(m, "Keyboard Shortcuts...", []() { ShowOptions(lynx::editor::options::Page::Keyboard); });
            MenuAdd(m, "Plugins...", []() { ShowOptions(lynx::editor::options::Page::Plugins); });
        }

        // Window
        {
            const Rid m = Rmenu_add_menu(bar, "Window");
            for (const PanelEntry& e : PanelEntries())
            {
                if (*e.panel == g.build || *e.panel == g.options || *e.panel == R_INVALID_ID)
                    continue;
                const Rid panel = *e.panel;
                const Rid item = MenuAdd(m, e.label, [panel]()
                {
                    if (IsPanelOpen(panel))
                        HidePanel(panel);
                    else
                        ShowPanel(panel);
                }, [panel]() { return panel != g.viewport_panel; }, -1, [panel]() { return IsPanelOpen(panel) != 0; });
                eui::SetMenuIcon(item, e.icon);
            }
            Rmenu_add_separator(m);
            eui::SetMenuIcon(MenuAdd(m, "Input Settings", []() { lynx::editor::input_settings::Show(); }), Icon::Input);
            eui::SetMenuIcon(MenuAdd(m, "Git", []() { git_window::Show(); }), Icon::Git);
            eui::SetMenuIcon(MenuAdd(m, "Profiler", []() { lynx::editor::profiler_window::Show(); }), Icon::Profiler);
            Rmenu_add_separator(m);
            MenuAdd(m, "Camera Speed and Volume...", []() { ViewDialog(); });
        }

        // Game
        {
            const Rid m = Rmenu_add_menu(bar, "Game");
            const Rid play = MenuAdd(m, "Play", []() { TogglePlayMode(); }, {}, KeyOf(km::Action::Play));
            MenuText(play, []() { return std::string(isPlaying ? "Stop" : "Play"); });
            MenuAdd(m, "Simulate", []() { TogglePlayMode(true); }, {}, KeyOf(km::Action::Simulate));
            Rmenu_add_separator(m);
            if (!currentProject.script_only)
                MenuAdd(m, "Compile", []() { lynx::editor::game_build::Start(); },
                        []() { return !lynx::editor::game_build::IsRunning(); });
            MenuAdd(m, "Reload Game", []() { RequestReload(); });
            if (!currentProject.script_only)
            {
                MenuAdd(m, "Build Output", []()
                {
                    if (g_build_panel_open)
                        HidePanel(g.build);
                    else
                        ShowPanel(g.build);
                }, {}, -1, []() { return g_build_panel_open; });
                Rmenu_add_separator(m);
                MenuAdd(m, "Reload after Compile", []()
                {
                    appSettings.reloadAfterBuild = !appSettings.reloadAfterBuild;
                    SaveSettings();
                }, {}, -1, []() { return appSettings.reloadAfterBuild; });
                MenuAdd(m, "Reload when the DLL is rebuilt", []()
                {
                    appSettings.autoReloadOnChange = !appSettings.autoReloadOnChange;
                    SaveSettings();
                }, {}, -1, []() { return appSettings.autoReloadOnChange; });
            }
            Rmenu_add_separator(m);
            MenuAdd(m, "Show Collisions", []()
            {
                show_collision_debug = !show_collision_debug;
                ApplyCollisionDebugToGame();
            }, {}, KeyOf(km::Action::CollisionDebug), []() { return show_collision_debug; });
        }

        // Tools
        {
            const Rid m = Rmenu_add_menu(bar, "Tools");
            MenuAdd(m, "Paint Mode", []() { brushPaintEnabled = true; }, editing, -1, []() { return brushPaintEnabled; });
            MenuAdd(m, "Select Mode", []() { brushPaintEnabled = false; }, editing, -1, []() { return !brushPaintEnabled; });
            MenuAdd(m, "Switch Paint / Select", []() { brushPaintEnabled = !brushPaintEnabled; }, editing, KeyOf(km::Action::TogglePaint));
            Rmenu_add_separator(m);
            MenuAdd(m, "Translate", []() { SetGizmoModeIndex(0); }, editing, -1, []() { return gizmoModeIndex == 0; });
            MenuAdd(m, "Rotate", []() { SetGizmoModeIndex(1); }, editing, -1, []() { return gizmoModeIndex == 1; });
            MenuAdd(m, "Scale", []() { SetGizmoModeIndex(2); }, editing, -1, []() { return gizmoModeIndex == 2; });
            MenuAdd(m, "Next Gizmo Mode", []() { CycleGizmoMode(); }, editing, KeyOf(km::Action::CycleGizmo));
            Rmenu_add_separator(m);
            MenuAdd(m, "Ask Lynxie...", []() { ShowLynxie(); });
        }

        // Help
        {
            const Rid m = Rmenu_add_menu(bar, "Help");
            MenuAdd(m, "Keyboard Shortcuts", []() { ShowOptions(lynx::editor::options::Page::Keyboard); });
            MenuAdd(m, "Lynx on GitHub", []() { OpenUrl("https://github.com/oscar-soirey/Lynx-Engine"); });
            MenuAdd(m, "Report a Bug", []() { OpenUrl("https://github.com/oscar-soirey/Lynx-Engine/issues"); });
            Rmenu_add_separator(m);
            MenuAdd(m, "Support Lynx (GitHub Sponsors)", []() { OpenUrl("https://github.com/sponsors/oscar-soirey"); });
            MenuAdd(m, "About Lynx", []()
            {
                eui::Message(g.window, "About Lynx",
                             "Lynx " + lynx::GetEngineVersion() + "\n\n"
                             "Game engine for 2D, pixel art and destructible voxel worlds.\n"
                             "Made by Oscar Soirey. Free and open source (Apache License 2.0).");
            });
        }
    }

    static void UpdateMenuBar()
    {
        for (MenuEntry& e : g_menu_entries)
        {
            if (e.enabled)
                eui::SetEnabled(e.item, e.enabled());
            if (e.checked)
            {
                const int checked = e.checked() ? 1 : 0;
                if (checked != e.shown_checked)
                {
                    e.shown_checked = checked;
                    Rmenu_item_set_checked(e.item, checked ? R_TRUE : R_FALSE);
                }
            }
            if (e.text)
            {
                const std::string text = e.text();
                if (text != e.shown_text)
                {
                    e.shown_text = text;
                    Rmenu_item_set_text(e.item, text.c_str());
                }
            }
            if (e.action >= 0)
            {
                const std::string shortcut = km::Text(static_cast<km::Action>(e.action));
                if (shortcut != e.shown_shortcut)
                {
                    e.shown_shortcut = shortcut;
                    Rwidget_set_attribute(e.item, "Shortcut", shortcut.c_str());
                }
            }
        }
    }

    // ---- binding ---------------------------------------------------------------------

    static void Init()
    {
        auto find = [](const char* name) { return eui::Find(g.window, name); };

        Rwindow_set_close_callback(g.window, OnCloseRequest, nullptr);
        // Tab switches Paint / Select, Space cycles the gizmo : not the focus.
        Rwindow_set_tab_navigation(g.window, R_FALSE);

        t.header = find("header");
        t.version = find("header.version");
        t.lynxie = find("header.lynxie");
        Rimage_set_texture(find("header.icon"), eui::Texture("icons/lynxie.png"));
        Rimage_set_texture(find("header.lynxie.icon"), eui::Texture("icons/lynxie.png"));
        eui::SetText(t.version, "Lynx " + lynx::GetEngineVersion());
        eui::OnClick(t.lynxie, []() { ShowLynxie(); });

        t.menubar = find("menubar");
        t.save = find("tb.save");
        t.save_dirty = find("tb.save.dirty");
        t.undo = find("tb.undo");
        t.translate = find("tb.translate");
        t.rotate = find("tb.rotate");
        t.scale = find("tb.scale");
        t.play = find("tb.play");
        t.simulate = find("tb.simulate");
        t.simulating = find("tb.simulating");
        t.compile = find("tb.compile");
        t.game_menu = find("tb.game.menu");
        t.status = find("tb.status");
        t.status_text = find("tb.status.text");
        t.view = find("tb.view");
        t.view_text = find("tb.view.text");
        t.st_level = find("status.level");
        t.st_mode = find("status.mode");
        t.st_hint = find("status.hint");
        t.st_fps = find("status.fps");

        eui::OnClick(t.save, []() { if (!isPlaying) SaveEditor(); });
        eui::OnClick(t.undo, []() { if (!isPlaying && !brushPainting) UndoLastEditorAction(); });
        eui::OnClick(t.translate, []() { SetGizmoModeIndex(0); });
        eui::OnClick(t.rotate, []() { SetGizmoModeIndex(1); });
        eui::OnClick(t.scale, []() { SetGizmoModeIndex(2); });
        eui::OnClick(t.play, []() { TogglePlayMode(); });
        eui::OnClick(t.simulate, []() { TogglePlayMode(true); });
        eui::OnClick(t.compile, []()
        {
            if (currentProject.script_only)
                RequestReload();
            else if (!lynx::editor::game_build::IsRunning())
                lynx::editor::game_build::Start();
        });
        eui::OnClick(t.game_menu, []() { GameMenu(t.compile); });
        eui::OnClick(t.status, []()
        {
            if (!lynx::editor::game_build::ToolbarStatus().empty())
                ShowPanel(g.build);
            else if (module_changed_notice)
                RequestReload();
        });
        eui::OnClick(t.view, []() { ViewDialog(); });

        if (currentProject.script_only)
        {
            if (Rid label = Rwidget_find(t.compile, "label"); label != R_INVALID_ID)
                eui::SetText(label, "Reload Game");
            eui::SetTooltip(t.compile, "Reload the game : stops it, saves pending level edits,\n"
                                       "destroys every actor and rebuilds the level (scripts reloaded).");
        }

        lynx::editor::options::BindPanel(g.options);
        lynx::editor::keymap::BindPage(g.window);
        BuildMenuBar();
        BindProjectPage();
        BindPluginsPage();
    }

    static void UpdateToolbar()
    {
        const bool building = lynx::editor::game_build::IsRunning();

        eui::SetVisible(t.save_dirty, editor_dirty && !isPlaying);
        const std::string save_tip = "Save the level (" + km::Text(km::Action::Save) + ")";
        eui::SetTooltip(t.save, editor_dirty ? save_tip + "\nThere are unsaved changes." : save_tip);
        eui::SetTooltip(t.undo, "Undo (" + km::Text(km::Action::Undo) + ")");
        for (Rid w : { t.save, t.undo, t.translate, t.rotate, t.scale })
            eui::SetEnabled(w, !isPlaying);
        eui::SetClass(t.translate, "active", gizmoModeIndex == 0);
        eui::SetClass(t.rotate, "active", gizmoModeIndex == 1);
        eui::SetClass(t.scale, "active", gizmoModeIndex == 2);

        if (Rid label = Rwidget_find(t.play, "label"); label != R_INVALID_ID)
            eui::SetText(label, isPlaying ? "Stop" : "Play");
        {
            // The icon follows : Play / Stop (the ImGui toolbar did).
            static int shown = -1;
            if (shown != (isPlaying ? 1 : 0))
            {
                shown = isPlaying ? 1 : 0;
                eui::SetIcon(Rwidget_find(t.play, "icon"),
                             isPlaying ? lynx::editor::icons::Icon::Stop : lynx::editor::icons::Icon::Play);
            }
        }
        eui::SetClass(t.play, "play", !isPlaying);
        eui::SetClass(t.play, "stop", isPlaying);
        const std::string play_key = km::Text(km::Action::Play);
        const std::string simulate_key = km::Text(km::Action::Simulate);
        eui::SetTooltip(t.play, isPlaying
            ? (isSimulating ? "Stop the simulation, back to the editor (" + play_key + " / " + simulate_key + ")"
                            : "Stop the game, back to the editor (" + play_key + ")")
            : "Play the game in the editor (" + play_key + ")");
        eui::SetVisible(t.simulate, !isPlaying);
        eui::SetVisible(t.simulating, isPlaying && isSimulating);

        if (!currentProject.script_only)
        {
            eui::SetEnabled(t.compile, !building);
            eui::SetTooltip(t.compile, appSettings.reloadAfterBuild
                ? "Compile the game (Build.bat of the project)\nthen reload it if the build succeeded."
                : "Compile the game (Build.bat of the project).");
        }

        // Build state / DLL rebuilt outside the editor.
        const std::string build_status = lynx::editor::game_build::ToolbarStatus();
        std::string status;
        const char* tip = "";
        if (!build_status.empty())
        {
            status = build_status;
            tip = "Click : build output";
        }
        else if (hot_reload_pending)
            status = "(reload at Stop)";
        else if (module_changed_notice)
        {
            status = "DLL rebuilt";
            tip = "The game DLL was rebuilt : click to reload it.";
        }
        eui::SetVisible(t.status, !status.empty());
        eui::SetText(t.status_text, status);
        eui::SetTooltip(t.status, tip);
        eui::SetClass(t.status_text, "error", build_status == "Build failed");
        eui::SetClass(t.status_text, "warning", status == "DLL rebuilt");

        char view[96];
        std::snprintf(view, sizeof(view), "Speed %.0f  Vol %d%%", cameraSpeed,
                      static_cast<int>(std::lround(appSettings.masterVolume * 100.f)));
        eui::SetText(t.view_text, view);
        UpdateMenuBar();
    }

    static void UpdateStatusBar()
    {
        eui::SetText(t.st_level, current_level_file + (editor_dirty ? " *" : ""));

        std::string mode;
        const std::string toggle_key = km::Text(km::Action::TogglePaint);
        if (isPlaying)
            mode = isSimulating ? "Simulating  (" + km::Text(km::Action::Simulate) + " : stop)"
                                : "Playing  (" + km::Text(km::Action::Play) + " : stop, Shift : mouse to the editor)";
        else if (actor_drag_active)
            mode = "Placing " + actor_drag_type;
        else if (brushPaintEnabled)
            mode = "Paint  (" + toggle_key + " : select)";
        else
            mode = "Select  (" + toggle_key + " : paint)";
        eui::SetText(t.st_mode, mode);
        eui::SetClass(t.st_mode, "mode-on", isPlaying);

        eui::SetText(t.st_hint, cameraMode == EditorCameraMode::Mode3D
            ? "3D : right mouse to look, WASD to fly, Q / E down / up"
            : "2D : WASD to move, wheel for the height, " + km::Text(km::Action::CommandPalette) + " : everything");
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f fps", g_fps);
        eui::SetText(t.st_fps, fps);
    }

    static void Update(float)
    {
        // The tooltip of the project (folder, size...) : read again from time to time.
        static double tooltip_time = -100.0;
        const double now = glfwGetTime();
        if (now - tooltip_time > 2.0)
        {
            tooltip_time = now;
            static std::string shown_module = "\x01";
            if (shown_module != currentProject.module_path.string())
            {
                shown_module = currentProject.module_path.string();
                lynx::editor::project_info::Project info;
                info.name = currentProject.name;
                info.root = currentProject.root;
                info.assets_dir = currentProject.assets_dir;
                info.build_dir = currentProject.build_dir;
                info.module_path = currentProject.module_path;
                info.engine_version = lynx::GetEngineVersion();
                lynx::editor::project_info::SetProject(info);
            }
            eui::SetTooltip(t.header, lynx::editor::project_info::TooltipText());
        }

        // Build panel opened by the build (errors), or by its menu.
        if (lynx::editor::game_build::WindowOpen() && !g_build_panel_open)
            ShowPanel(g.build);

        FollowPlay();
        SyncWindowFlags();
        Shortcuts();
        Dialogs();
        UpdateToolbar();
        UpdateStatusBar();
    }
}


// =============================================================================
// Command palette (Ctrl+P)
// =============================================================================

static void OpenCommandPalette(const std::string& prefix)
{
    namespace cp = lynx::editor::command_palette;
    using namespace editor_ui;
    std::vector<cp::Item> items;

    auto command = [&](const char* label, const char* shortcut, std::function<void()> run)
    {
        items.push_back({ label, shortcut, cp::Kind::Command, std::move(run) });
    };

    // --- Commands -------------------------------------------------------------
    command("Save level", "Ctrl+S", []() { SaveEditor(); });
    command("Play / Stop", "F3", []() { TogglePlayMode(); });
    command("Simulate", "Alt+S", []() { TogglePlayMode(true); });
    command("Undo", "Ctrl+Z", []() { UndoLastEditorAction(); });
    command("Hot reload game code", "", []() { RequestHotReload(); });
    command("Options", "", []() { ShowPanel(g.options); });
    command("Plugins", "Options > Plugins", []()
    {
        ShowPanel(g.options);
        lynx::editor::options::OpenPage(lynx::editor::options::Page::Plugins);
    });
    command("Ask Lynxie (AI assistant)", "Commands window", []() { ShowLynxie(); });
    command(lynx::lighting2d::IsEnabled() ? "2D lighting : turn off" : "2D lighting : turn on", "2D Lighting", []()
    {
        lynx::lighting2d::SetFloat("enabled", lynx::lighting2d::IsEnabled() ? 0.f : 1.f);
    });
    if (editing_actor && IsActorAlive(editing_actor))
    {
        command("Focus the selected actor", "camera", []()
        {
            if (editing_actor && IsActorAlive(editing_actor))
            {
                camX = editing_actor->transform.location.x;
                camY = editing_actor->transform.location.y;
            }
        });
    }

    // --- Windows --------------------------------------------------------------
    for (const PanelEntry& e : PanelEntries())
    {
        const Rid panel = *e.panel;
        if (panel == R_INVALID_ID || panel == g.viewport_panel)
            continue;
        const bool open = IsPanelOpen(panel) != 0;
        items.push_back({ std::string(open ? "Hide " : "Show ") + e.label, "window", cp::Kind::Window, [panel, open]()
        {
            if (open)
                HidePanel(panel);
            else
                ShowPanel(panel);
        } });
    }

    // --- Actors of the level ----------------------------------------------------
    if (editor_level)
    {
        for (lynx::Actor* actor : editor_level->GetActors())
        {
            if (!actor)
                continue;
            char where[64];
            std::snprintf(where, sizeof(where), "%.0f, %.0f", actor->transform.location.x, actor->transform.location.y);
            items.push_back({ ActorLabel(actor), where, cp::Kind::Actor, [actor]()
            {
                if (!IsActorAlive(actor))
                    return;
                SetActorSelected(actor);
                camX = actor->transform.location.x;
                camY = actor->transform.location.y;
            } });
        }
    }

    // --- Files of assets/ -------------------------------------------------------
    {
        std::error_code error;
        const std::filesystem::path root = content_browser_root;
        int count = 0;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error);
             it != std::filesystem::recursive_directory_iterator() && count < 5000;
             it.increment(error))
        {
            if (error)
                break;
            const std::string name = it->path().filename().string();
            if (!name.empty() && name[0] == '.')
            {
                if (it->is_directory(error))
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(error))
                continue;
            const std::filesystem::path path = it->path();
            const std::string relative = path.lexically_relative(root).generic_string();
            items.push_back({ name, relative, cp::Kind::File, [path]()
            {
                content_browser_actions::OpenEntry(path);
            } });
            ++count;
        }
    }

    cp::Open(std::move(items), prefix);
}
