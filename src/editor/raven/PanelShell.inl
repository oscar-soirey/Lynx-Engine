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
        Rid project = R_INVALID_ID, project_menu = R_INVALID_ID, ide = R_INVALID_ID, ide_icon = R_INVALID_ID;
        Rid save = R_INVALID_ID, save_dirty = R_INVALID_ID, undo = R_INVALID_ID;
        Rid translate = R_INVALID_ID, rotate = R_INVALID_ID, scale = R_INVALID_ID;
        Rid play = R_INVALID_ID, simulate = R_INVALID_ID, simulating = R_INVALID_ID;
        Rid compile = R_INVALID_ID, game_menu = R_INVALID_ID, status = R_INVALID_ID, status_text = R_INVALID_ID;
        Rid view = R_INVALID_ID, view_text = R_INVALID_ID, windows = R_INVALID_ID, options = R_INVALID_ID;
        Rid header = R_INVALID_ID, version = R_INVALID_ID, lynxie = R_INVALID_ID;
        Rid st_level = R_INVALID_ID, st_mode = R_INVALID_ID, st_hint = R_INVALID_ID, st_fps = R_INVALID_ID;
    };

    static Toolbar t;
    static std::unique_ptr<eui::Menu> g_menu;          // the menu of the toolbar being shown
    static double g_fps = 0.0;
    static int g_ide_icon_of = -1;                      // ide::Ide shown on tb.ide
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

    static void ProjectMenu(Rid under)
    {
        eui::Menu& menu = NewMenu();

        std::error_code relative_error;
        const std::filesystem::path module_relative =
            std::filesystem::relative(currentProject.module_path, currentProject.root, relative_error);

        menu.Title(currentProject.name);
        menu.Title(currentProject.root.string());
        menu.Title("Game DLL : " + (currentProject.script_only
                                         ? std::string("none (JavaScript only)")
                                         : (relative_error ? currentProject.module_path : module_relative).generic_string()));
        menu.Separator();
        menu.Item("Open another project...", []()
        {
            switch_project_requested = true;
            show_close_confirmation = true;
        }, !isPlaying);
        menu.Item("Ship Game...", []() { lynx::editor::ship_game::Open(); },
                  !isPlaying && !lynx::editor::ship_game::IsBusy());
        menu.Item("Project settings...", []()
        {
            ShowPanel(g.options);
            lynx::editor::options::OpenPage(lynx::editor::options::Page::Project);
        });

        menu.Separator();
        menu.Title("Open the game folder with");
        const bool searching = !ide::DetectionDone();
        const ide::Ide last = ide::LastUsed();
        for (int i = 0; i < static_cast<int>(ide::Ide::Count); ++i)
        {
            const auto which = static_cast<ide::Ide>(i);
            const bool missing = !searching && !ide::IsInstalled(which);
            const std::string text = std::string(ide::Name(which)) + (missing ? "  (not found)" : "") +
                                     (which == last ? "   (last used)" : "");
            menu.Item(text, [which]() { OpenIn(which); }, !missing);
        }
        menu.Item("Show in Explorer", []()
        {
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", GameFolder().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        });
        if (searching)
            menu.Title("Searching the code editors...");

        OpenUnder(menu, under);
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

    static void WindowsMenu(Rid under)
    {
        eui::Menu& menu = NewMenu();
        menu.Title("Editor windows");
        menu.Separator();
        for (const PanelEntry& e : PanelEntries())
        {
            if (*e.panel == g.build || *e.panel == g.options || *e.panel == R_INVALID_ID)
                continue;
            const Rid panel = *e.panel;
            const bool open = IsPanelOpen(panel) != 0;
            menu.Check(e.label, open, [panel, open]()
            {
                if (open)
                    HidePanel(panel);
                else
                    ShowPanel(panel);
            }, panel != g.viewport_panel);
        }
        // Their Raven windows come with the next versions of the editor.
        menu.Separator();
        menu.Item("Input Settings", []() { lynx::editor::input_settings::Show(); });
        menu.Item("Git", []() { git_window::Show(); });
        menu.Item("Profiler", []() { lynx::editor::profiler_window::Show(); });
        menu.Separator();
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
            Rwidget_focus(g.viewport);
    }

    // ---- shortcuts of the level editor (ImGui used to poll them) ----------------------

    static bool OtherEditorHasFocus()
    {
        return lynx::editor::input_settings::BlocksEditorShortcuts() ||
               lynx::editor::widget_editor::HasFocus() ||
               lynx::editor::graph_editors::HasFocus() ||
               lynx::editor::particle_editor::HasFocus() ||
               lynx::editor::plugins::HasFocus();
    }

    static void Shortcuts()
    {
        if (isPlaying || Rwindow_has_popup(g.window) || ein::WantTextInput())
            return;

        // Command palette : Ctrl+P (everything), Ctrl+Shift+P (commands).
        if (ein::Ctrl() && ein::KeyPressed(GLFW_KEY_P) && !lynx::editor::command_palette::IsOpen() &&
            !lynx::editor::input_settings::BlocksEditorShortcuts())
            OpenCommandPalette(ein::Shift() ? ">" : "");

        // Undo.
        if (ein::Ctrl() && ein::KeyPressed(GLFW_KEY_Z) && !OtherEditorHasFocus() && !brushPainting)
            UndoLastEditorAction();

        // Delete : the selected actors (the Content Browser deletes its files).
        content_browser_focused = HasFocus(g.content_browser);
        if (ein::KeyPressed(GLFW_KEY_DELETE) && editing_actor && !content_browser_focused && !OtherEditorHasFocus())
        {
            PruneActorSelection();
            std::vector<lynx::Actor*> to_delete = selected_actors;
            if (to_delete.empty())
                to_delete.push_back(editing_actor);
            DeleteActors(to_delete);
        }
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

    static void BindPluginsPage()
    {
        Rid list = eui::Find(g.window, "plugins.list");
        Rwidget_clear_children(list);
        auto& plugins = lynx::plugins::GetPlugins();
        if (plugins.empty())
        {
            Rid none = Rlabel_create("No plugin : <editor>/plugins and <project>/plugins hold the plugins (plugin.json).");
            Rwidget_add_class(none, "dim");
            Rlabel_set_wrap(none, R_TRUE);
            Rwidget_add_child(list, none);
            return;
        }
        for (const lynx::plugins::PluginInfo& info : plugins)
        {
            const std::string name = info.name;
            Rid box = Rwidget_create(R_WIDGET_VBOX);
            Rid check = Rcheckbox_create((info.name + (info.version.empty() ? "" : "  " + info.version) +
                                          (info.engine_plugin ? "   (engine)" : "   (project)")).c_str());
            Rcheckbox_set_checked(check, info.enabled ? R_TRUE : R_FALSE);
            Rwidget_add_child(box, check);
            if (!info.description.empty())
            {
                Rid description = Rlabel_create(info.description.c_str());
                Rwidget_add_class(description, "dim");
                Rlabel_set_wrap(description, R_TRUE);
                Rwidget_add_child(box, description);
            }
            std::string state = info.loaded ? "Loaded." : info.enabled ? "Not loaded." : "Disabled.";
            if (!info.error.empty())
                state += " " + info.error;
            if (!info.editor_dll.empty())
                state += "  Its editor module needs the new plugin API of the editor (coming).";
            Rid status = Rlabel_create(state.c_str());
            Rwidget_add_class(status, info.error.empty() ? "dim" : "error");
            Rlabel_set_wrap(status, R_TRUE);
            Rwidget_add_child(box, status);
            Rwidget_add_child(list, box);
            eui::OnToggle(check, [name, status](bool on)
            {
                lynx::plugins::SetEnabled(name, on);
                lynx::plugins::SaveSettings();
                eui::SetText(status, "Saved in plugins.json : restart the editor to load / unload it.");
            });
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

        t.project = find("tb.project");
        t.project_menu = find("tb.project.menu");
        t.ide = find("tb.ide");
        t.ide_icon = find("tb.ide.icon");
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
        t.windows = find("tb.windows");
        t.options = find("tb.options");
        t.st_level = find("status.level");
        t.st_mode = find("status.mode");
        t.st_hint = find("status.hint");
        t.st_fps = find("status.fps");

        // The name of the project on its button.
        if (Rid label = Rwidget_find(t.project, "label"); label != R_INVALID_ID)
            eui::SetText(label, currentProject.name.empty() ? std::string("Project") : currentProject.name);

        eui::OnClick(t.project, []() { ProjectMenu(t.project); });
        eui::OnClick(t.project_menu, []() { ProjectMenu(t.project); });
        eui::OnClick(t.ide, []() { OpenIn(ide::LastUsed()); });
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
        eui::OnClick(t.windows, []() { WindowsMenu(t.windows); });
        eui::OnClick(t.options, []()
        {
            if (IsPanelShown(g.options))
                HidePanel(g.options);
            else
                ShowPanel(g.options);
        });

        if (currentProject.script_only)
        {
            if (Rid label = Rwidget_find(t.compile, "label"); label != R_INVALID_ID)
                eui::SetText(label, "Reload Game");
            eui::SetTooltip(t.compile, "Reload the game : stops it, saves pending level edits,\n"
                                       "destroys every actor and rebuilds the level (scripts reloaded).");
        }

        lynx::editor::options::BindPanel(g.options);
        BindProjectPage();
        BindPluginsPage();
    }

    static void UpdateToolbar()
    {
        const bool building = lynx::editor::game_build::IsRunning();

        // Code editor of the IDE button.
        const ide::Ide last = ide::LastUsed();
        if (static_cast<int>(last) != g_ide_icon_of && ide::DetectionDone())
        {
            g_ide_icon_of = static_cast<int>(last);
            const Rid texture = IdeTexture(last);
            if (texture != R_INVALID_ID)
            {
                Rimage_set_texture(t.ide_icon, texture);
                Rimage_set_region(t.ide_icon, 0.f, 0.f, 1.f, 1.f);
            }
            else
            {
                float uv[4];
                eui::IconRegion(lynx::editor::icons::Icon::FileCode, uv);
                Rimage_set_texture(t.ide_icon, eui::IconTexture());
                Rimage_set_region(t.ide_icon, uv[0], uv[1], uv[2], uv[3]);
            }
            eui::SetTooltip(t.ide, std::string("Open the game folder in ") + ide::Name(last) +
                                   "\n(Project menu : another code editor)");
        }

        eui::SetVisible(t.save_dirty, editor_dirty && !isPlaying);
        eui::SetTooltip(t.save, editor_dirty ? "Save the level (Ctrl+S)\nThere are unsaved changes." : "Save the level (Ctrl+S)");
        for (Rid w : { t.save, t.undo, t.translate, t.rotate, t.scale })
            eui::SetEnabled(w, !isPlaying);
        eui::SetClass(t.translate, "active", gizmoModeIndex == 0);
        eui::SetClass(t.rotate, "active", gizmoModeIndex == 1);
        eui::SetClass(t.scale, "active", gizmoModeIndex == 2);

        if (Rid label = Rwidget_find(t.play, "label"); label != R_INVALID_ID)
            eui::SetText(label, isPlaying ? "Stop" : "Play");
        eui::SetClass(t.play, "play", !isPlaying);
        eui::SetClass(t.play, "stop", isPlaying);
        eui::SetTooltip(t.play, isPlaying
            ? (isSimulating ? "Stop the simulation, back to the editor (F3 / Alt+S)"
                            : "Stop the game, back to the editor (F3)")
            : "Play the game in the editor (F3)");
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
        eui::SetClass(t.options, "active", IsPanelShown(g.options) != 0);
    }

    static void UpdateStatusBar()
    {
        eui::SetText(t.st_level, current_level_file + (editor_dirty ? " *" : ""));

        std::string mode;
        if (isPlaying)
            mode = isSimulating ? "Simulating  (Alt+S : stop)" : "Playing  (F3 : stop, Shift : mouse to the editor)";
        else if (actor_drag_active)
            mode = "Placing " + actor_drag_type;
        else if (brushPaintEnabled)
            mode = "Paint  (Tab : select)";
        else
            mode = "Select  (Tab : paint)";
        eui::SetText(t.st_mode, mode);
        eui::SetClass(t.st_mode, "mode-on", isPlaying);

        eui::SetText(t.st_hint, cameraMode == EditorCameraMode::Mode3D
            ? "3D : right mouse to look, WASD to fly, Q / E down / up"
            : "2D : WASD to move, wheel for the height, Ctrl+P : everything");
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
