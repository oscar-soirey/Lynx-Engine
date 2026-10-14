// =============================================================================
// Viewport panel : the scene image, the input of the game and of the editor
// tools (brush, gizmo, picking), the drops (classes of Place Actors, files of
// the Content Browser), the 2D / 3D buttons.
// (included by raven/EditorUi.inl)
//
// HRL renders the scene off-screen (OpenGL) ; GlBridge copies the texture into
// an image Vulkan shares, shown by the Raven <Viewport>. The mouse is "for the
// scene" when it is over that widget (sceneViewport.hovered) and the click did
// not start on a panel (sceneViewport.blocked) : exactly the rules of the
// ImGui Viewport window.
// =============================================================================

namespace editor_ui::viewport_panel
{
    static lynx::editor::gl::TextureLink g_link;
    static Rid g_modes = R_INVALID_ID;
    static Rid g_2d = R_INVALID_ID;
    static Rid g_3d = R_INVALID_ID;
    static Rid g_badge = R_INVALID_ID;
    static Rid g_badge_text = R_INVALID_ID;
    static std::unique_ptr<eui::Menu> g_actor_menu;
    static bool g_game_button_down[8] = {};    // the press went to the game : its release too
    static double g_last_x = 0.0, g_last_y = 0.0;
    static bool g_have_last = false;

    // The pointer is over the scene image itself (not over the 2D / 3D buttons,
    // a menu or a dialog).
    static bool PointerOverScene(const REvent& event)
    {
        return sceneViewport.visible && event.target == g.viewport && !Rwindow_has_popup(g.window);
    }

    static bool AnyMouseDown()
    {
        for (int b = 0; b < 3; ++b)
            if (ein::Mouse(b))
                return true;
        return false;
    }

    // Right click on an actor (without moving) : its menu.
    static void ActorMenu(double window_x, double window_y)
    {
        if (isPlaying || !editor_level)
            return;
        double scene_x = 0.0, scene_y = 0.0;
        MouseToScene(window_x, window_y, scene_x, scene_y);
        const HRL_id object = HRL_GL_GetHoveredObject(scene, static_cast<int>(scene_x), static_cast<int>(scene_y), nullptr);
        lynx::Actor* actor = object != HRL_INVALID_ID ? static_cast<lynx::Actor*>(HRL_GetMeshUserHandle(object)) : nullptr;
        if (!actor || !IsActorAlive(actor))
            return;

        SetActorSelected(actor);
        g_actor_menu = std::make_unique<eui::Menu>(g.window);
        eui::Menu& menu = *g_actor_menu;
        menu.Title(ActorLabel(actor));
        menu.Separator();
        menu.Item("Duplicate", [actor]() { if (IsActorAlive(actor)) DuplicateActor(editor_level, actor); }, true, "");
        SpriteVoxelsItem(menu, actor);
        menu.Separator();
        menu.Item("Delete", [actor]() { DeleteActors({ actor }); }, true, "Del");
        menu.Open();
    }

    // Every event of the editor window goes through here first (Raven still
    // gives it to the widgets afterwards) : the input state of the editor
    // (lynx::editor::input), the game, HRL (gizmo).
    static int OnWindowEvent(Rid, const REvent* e, void*)
    {
        const REvent& event = *e;
        ein::Feed(event);

        switch (event.type)
        {
        case R_EVENT_MOUSE_MOVE:
        {
            sceneViewport.hovered = PointerOverScene(event) || (sceneViewport.hovered && AnyMouseDown());
            double scene_x = event.x, scene_y = event.y;
            MouseToScene(event.x, event.y, scene_x, scene_y);
            HRL_MouseMovedCallback(static_cast<float>(scene_x), static_cast<float>(scene_y));
            if (g_have_last)
                lynx::InjectMouseMove(static_cast<float>(event.x - g_last_x), static_cast<float>(event.y - g_last_y));
            g_last_x = event.x;
            g_last_y = event.y;
            g_have_last = true;
            break;
        }

        case R_EVENT_MOUSE_DOWN:
        case R_EVENT_MOUSE_UP:
        {
            const bool press = event.type == R_EVENT_MOUSE_DOWN;
            const int button = static_cast<int>(event.button);   // 0 left, 1 right, 2 middle : as GLFW

            if (press)
            {
                sceneViewport.hovered = PointerOverScene(event);
                // A click that starts on a panel / menu is not for the scene,
                // even if the pointer then moves over the image.
                if (!sceneViewport.hovered)
                    sceneViewport.blocked = true;
                if (sceneViewport.hovered)
                    Rwidget_focus(g.viewport);
            }

            // Input Settings : "press the key to bind".
            if (lynx::editor::input_settings::OnMouseButton(button, press ? GLFW_PRESS : GLFW_RELEASE))
                break;

            const bool blocked = editor::BlockGameInput() || editor::WantsMouse();
            if (button >= 0 && button < 8)
            {
                if (press)
                {
                    g_game_button_down[button] = !blocked;
                    if (!blocked)
                        lynx::InjectMouseButtonDown(button);
                }
                else
                {
                    if (g_game_button_down[button])
                        lynx::InjectMouseButtonUp(button);
                    g_game_button_down[button] = false;
                }
            }

            if (button == 0 && press)
                editor::OnLeftMousePress();
            if (button == 0)
                HRL_MouseButtonCallback(HRL_MOUSE_BUTTON_LEFT, press ? HRL_MOUSE_PRESS : HRL_MOUSE_RELEASE);

            if (!press && button == 1 && SceneAcceptsMouse(true) && ein::MouseDragDistance(1) < 5.f)
                ActorMenu(event.x, event.y);

            if (!press && !AnyMouseDown())
                sceneViewport.blocked = false;
            break;
        }

        case R_EVENT_MOUSE_WHEEL:
            if (lynx::editor::input_settings::OnScroll(event.wheel_y))
                break;
            if (!editor::BlockGameInput() && !editor::WantsMouse())
                lynx::InjectMouseWheel(event.wheel_y);
            editor::OnScroll(event.wheel_y);
            break;

        case R_EVENT_KEY_DOWN:
        case R_EVENT_KEY_UP:
        {
            const int key = ein::ToGlfw(event.key);
            const int action = event.type == R_EVENT_KEY_UP ? GLFW_RELEASE : (event.repeat ? GLFW_REPEAT : GLFW_PRESS);
            if (key <= 0 || lynx::editor::input_settings::OnKey(key, action))
                break;
            // The game does not get what is typed in a text field of the editor.
            if (editor::BlockGameInput() || (ein::WantTextInput() && action != GLFW_RELEASE))
                break;
            if (action == GLFW_PRESS)
                lynx::InjectKeyDown(key);
            else if (action == GLFW_RELEASE)
                lynx::InjectKeyUp(key);
            break;
        }

        default:
            break;
        }
        return R_FALSE;   // the widgets get the event too
    }

    // Classes of Place Actors and files of the Content Browser dropped on the scene.
    static int OnDrop(Rid, const RDropEvent* drop, void*)
    {
        if (isPlaying || !editor_level)
            return R_FALSE;

        const std::string type = drop->drag_type ? drop->drag_type : "";

        if (type == eui::kDragActorClass)
        {
            const std::string class_name = ClassOfDrag(drop->text);
            if (class_name.empty())
                return R_FALSE;
            switch (drop->phase)
            {
            case R_DROP_ENTER:
                // A real actor follows the pointer ; released here : it stays.
                if (!actor_drag_active)
                    BeginActorPlacementDrag(editor_level, class_name);
                break;
            case R_DROP_MOVE:
                if (actor_drag_active)
                    UpdateActorLocationFromMouse(actor_drag_preview);
                break;
            case R_DROP_LEAVE:
                CancelActorPlacementDrag();
                break;
            case R_DROP_DROPPED:
                if (!actor_drag_active)
                    BeginActorPlacementDrag(editor_level, class_name);
                UpdateActorLocationFromMouse(actor_drag_preview);
                CommitActorPlacementDrag();
                break;
            }
            return R_TRUE;
        }

        if (type == eui::kDragAsset)
        {
            if (drop->phase != R_DROP_DROPPED)
                return R_TRUE;
            // Images : a sprite ; sounds : a 2D sound source (at the pointer).
            for (const std::string& path : SplitLines(drop->text))
            {
                const std::filesystem::path asset(path);
                if (!IsPathInside(asset, content_browser_root))
                    continue;
                if (IsSupportedImageExtension(asset))
                    CreateActorFromAsset(asset, "StaticSprite", "texture_path");
                else if (IsSupportedAudioExtension(asset))
                    CreateActorFromAsset(asset, "AudioSource2D", "path");
            }
            return R_TRUE;
        }
        return R_FALSE;
    }

    static void Init()
    {
        g_modes = eui::Find(g.window, "viewport.modes");
        g_2d = eui::Find(g.window, "viewport.2d");
        g_3d = eui::Find(g.window, "viewport.3d");
        g_badge = eui::Find(g.window, "viewport.badge");
        g_badge_text = eui::Find(g.window, "viewport.badge.text");

        Rwindow_set_event_hook(g.window, OnWindowEvent, nullptr);
        Rwidget_set_drop_callback(g.viewport, OnDrop, nullptr);
        Rviewport_set_realtime(g.viewport, R_TRUE);

        eui::OnClick(g_2d, []() { SetEditorCameraMode(EditorCameraMode::Mode2D); });
        eui::OnClick(g_3d, []() { SetEditorCameraMode(EditorCameraMode::Mode3D); });
    }

    static void Update(float)
    {
        // Where the image is (window units) and its size in pixels : the
        // conversions of MouseToScene and the render size of HRL.
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        Rwidget_get_rect(g.viewport, &x, &y, &w, &h);
        sceneViewport.visible = IsPanelShown(g.viewport_panel) && w > 1.f && h > 1.f;
        sceneViewport.x = x;
        sceneViewport.y = y;
        sceneViewport.w = w;
        sceneViewport.h = h;
        if (!sceneViewport.visible)
            sceneViewport.hovered = false;

        int pixels_w = 0, pixels_h = 0;
        Rviewport_get_pixel_size(g.viewport, &pixels_w, &pixels_h);
        if (sceneViewport.visible && pixels_w > 0 && pixels_h > 0 &&
            (!sceneViewport.hasRenderSize || pixels_w != sceneViewport.renderW || pixels_h != sceneViewport.renderH))
        {
            sceneViewport.renderW = pixels_w;
            sceneViewport.renderH = pixels_h;
            sceneViewport.hasRenderSize = true;
            // Renderer + widgets of the game (DPI scale).
            editor_engine->SetRenderSize(pixels_w, pixels_h);
        }

        // The image of this frame (rendered by engine->ProgressOneFrame).
        if (sceneViewport.visible)
        {
            const unsigned int texture = HRL_GL_GetSceneTextureGL_ID(scene);
            int tex_w = 0, tex_h = 0;
            if (lynx::editor::gl::TextureSize(texture, tex_w, tex_h))
                Rviewport_set_texture(g.viewport, g_link.Update(texture, tex_w, tex_h));
        }

        // 2D / 3D : editor camera (hidden while playing, shown while simulating).
        eui::SetVisible(g_modes, !isPlaying || isSimulating);
        eui::SetClass(g_2d, "active", cameraMode == EditorCameraMode::Mode2D);
        eui::SetClass(g_3d, "active", cameraMode == EditorCameraMode::Mode3D);

        std::string badge;
        if (actor_drag_active)
            badge = "Placing " + actor_drag_type + " : release in the scene";
        else if (isPlaying && !isSimulating)
            badge = "";
        else if (brushPaintEnabled && !isPlaying)
            badge = brushVoxelType == 0 ? "Paint : erase" : "Paint (Shift : erase)";
        eui::SetText(g_badge_text, badge);
        eui::SetVisible(g_badge, !badge.empty());
    }

    static void Shutdown()
    {
        if (g.viewport != R_INVALID_ID && Rwidget_is_valid(g.viewport))
            Rviewport_set_texture(g.viewport, R_INVALID_ID);
        g_link.Release();
    }
}
