// =============================================================================
// Content Drawer (as in Unreal) : the Content Browser over everything,
// opened and closed with Left Ctrl + Space, whatever has the keyboard.
// (included by raven/EditorUi.inl after PanelContentBrowser.inl)
// -----------------------------------------------------------------------------
// - It takes the whole height under the menus, from the left by default ; the
//   button of its header (or its context) puts it on the right. Its width is
//   the user's (drag its inner edge). Both are kept in content_drawer.cfg
//   (%APPDATA%/Lynx, ~/.config/lynx).
// - It unfolds : its edge runs out on a spring that passes a little and comes
//   back, the content glides behind it more slowly than the edge (so it is not
//   one rigid block), the rows of its header arrive one after the other and
//   the files fade in last ; it folds back faster, without bounce.
// - Escape or a click outside it closes it ; "Dock in layout" opens the
//   Content Browser panel instead.
// - It is a second view of the Content Browser (same folder and selection).
// =============================================================================

namespace editor_ui::content_drawer
{
    namespace stdfs = std::filesystem;

    enum class Side { Left, Right };

    struct State
    {
        Rid host = R_INVALID_ID;      // the overlay over the tabs (editor.xml)
        Rid frame = R_INVALID_ID;     // the drawer : its width unfolds
        Rid canvas = R_INVALID_ID;    // holds the content at its full width
        Rid content = R_INVALID_ID;
        Rid body = R_INVALID_ID;
        Rid header = R_INVALID_ID;
        Rid handle_left = R_INVALID_ID, handle_right = R_INVALID_ID;
        Rid side_button = R_INVALID_ID;
        content_browser::View* view = nullptr;
        std::vector<Rid> rows;        // arrive one after the other
        Rid files = R_INVALID_ID;     // fade in last

        bool open = false;
        float p = 0.f, velocity = 0.f;   // 0 folded .. 1 unfolded (passes 1 a little)
        double opened_at = 0.0;
        Side side = Side::Left;
        float width = 0.f;               // chosen by the user, 0 : a part of the window
        bool resizing = false;
        bool handle_hot = false;
        bool popup_before = false;
        Rid focus_before = R_INVALID_ID;
        bool settings_loaded = false;
    };

    static State d;

    // ---- settings ------------------------------------------------------------------------

    static stdfs::path SettingsFile()
    {
#ifdef _WIN32
        const char* base = std::getenv("APPDATA");
        stdfs::path folder = base ? stdfs::path(base) / "Lynx" : stdfs::path(".lynx");
#else
        const char* home = std::getenv("HOME");
        stdfs::path folder = home ? stdfs::path(home) / ".config" / "lynx" : stdfs::path(".lynx");
#endif
        return folder / "content_drawer.cfg";
    }

    static void LoadSettings()
    {
        d.settings_loaded = true;
        std::ifstream in(SettingsFile());
        std::string key;
        while (in >> key)
        {
            if (key == "side")
            {
                std::string value;
                in >> value;
                d.side = value == "right" ? Side::Right : Side::Left;
            }
            else if (key == "width")
                in >> d.width;
        }
    }

    static void SaveSettings()
    {
        std::error_code ec;
        stdfs::create_directories(SettingsFile().parent_path(), ec);
        std::ofstream out(SettingsFile(), std::ios::trunc);
        out << "side " << (d.side == Side::Right ? "right" : "left") << "\n";
        out << "width " << d.width << "\n";
    }

    // ---- geometry ------------------------------------------------------------------------------

    static float HostWidth()
    {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        Rwidget_get_rect(d.host, &x, &y, &w, &h);
        return w > 0.f ? w : 1280.f;
    }

    // Width when unfolded : the user's, within the window.
    static float FullWidth()
    {
        const float host = HostWidth();
        const float wanted = d.width > 0.f ? d.width : host * 0.38f;
        return std::clamp(wanted, std::min(360.f, host * 0.9f), std::max(360.f, host * 0.92f));
    }

    static void ApplySide()
    {
        const bool left = d.side == Side::Left;
        Rslot_set_halign(d.frame, left ? R_HALIGN_LEFT : R_HALIGN_RIGHT);
        Rslot_set_valign(d.frame, R_VALIGN_FILL);
        // the content is held by the outer edge of the window (anchor 0 or 1)
        Rslot_set_anchors(d.content, left ? 0.f : 1.f, 0.f, left ? 0.f : 1.f, 1.f);
        Rslot_set_alignment(d.content, left ? 0.f : 1.f, 0.f);
        // rounded on its inner edge only
        if (left)
            Rborder_set_corner_radius(d.frame, 0.f, 8.f, 8.f, 0.f);
        else
            Rborder_set_corner_radius(d.frame, 8.f, 0.f, 0.f, 8.f);
        // the handle on the inner edge
        eui::SetVisible(d.handle_right, left);
        eui::SetVisible(d.handle_left, !left);
        eui::SetText(d.side_button, left ? "Open on the right" : "Open on the left");
    }

    // ---- the animation ---------------------------------------------------------------------------

    static float Smooth(float t)
    {
        t = std::clamp(t, 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    static void Animate(float dt)
    {
        dt = std::clamp(dt, 0.f, 1.f / 30.f);
        const float target = d.open ? 1.f : 0.f;
        // Opening : a spring that passes the end a little and comes back (it unfolds) ;
        // closing : stiffer and damped, no bounce.
        const float stiffness = d.open ? 230.f : 520.f;
        const float damping = d.open ? 21.f : 2.f * std::sqrt(520.f);
        // a few small steps : the spring stays stable at a low frame rate
        for (int i = 0; i < 4; ++i)
        {
            const float h = dt / 4.f;
            const float accel = stiffness * (target - d.p) - damping * d.velocity;
            d.velocity += accel * h;
            d.p += d.velocity * h;
        }
        if (!d.open && d.p < 0.004f)
        {
            d.p = 0.f;
            d.velocity = 0.f;
        }
        if (d.open && std::fabs(d.p - 1.f) < 0.0005f && std::fabs(d.velocity) < 0.002f)
        {
            d.p = 1.f;
            d.velocity = 0.f;
        }

        const bool shown = d.open || d.p > 0.f;
        eui::SetVisible(d.frame, shown);
        if (!shown)
            return;

        const float full = FullWidth();
        const float p = std::max(0.f, d.p);
        Rwidget_set_size(d.frame, std::max(1.f, full * p), -1.f);
        Rwidget_set_size(d.content, full, -1.f);

        // The content glides more slowly than the edge (behind it), and a little
        // past it when the edge passes : not one rigid block.
        const float lag = (1.f - p) * full * 0.32f;
        Rslot_set_position(d.content, d.side == Side::Left ? -lag : lag, 0.f);
        Rslot_set_size(d.content, full, 0.f);

        // Shadow : grows with the opening.
        const float open_part = std::min(1.f, p);
        Rborder_set_shadow(d.frame, 30.f * open_part, d.side == Side::Left ? 6.f * open_part : -6.f * open_part, 0.f);

        // The rows arrive one after the other, the files last.
        const float since = static_cast<float>(Rget_time() - d.opened_at);
        for (size_t i = 0; i < d.rows.size(); ++i)
        {
            const float q = d.open ? Smooth((since - 0.05f - 0.06f * static_cast<float>(i)) / 0.22f) : Smooth(p * 1.6f - 0.4f);
            Rwidget_set_opacity(d.rows[i], q);
            const float shift = (1.f - q) * 22.f;
            Rslot_set_padding(d.rows[i], d.side == Side::Left ? 0.f : shift, (1.f - q) * 6.f, d.side == Side::Left ? shift : 0.f, 0.f);
        }
        if (d.files != R_INVALID_ID)
        {
            const float q = d.open ? Smooth((since - 0.16f) / 0.3f) : Smooth(p * 1.4f - 0.3f);
            Rwidget_set_opacity(d.files, q);
        }
    }

    // ---- open / close ------------------------------------------------------------------------------

    static void Open()
    {
        if (d.frame == R_INVALID_ID || d.open)
            return;
        d.open = true;
        d.opened_at = Rget_time();
        const Rid focused = Rwindow_get_focused_widget(g.window);
        d.focus_before = focused != R_INVALID_ID && !IsInside(focused, d.frame) ? focused : R_INVALID_ID;
        content_browser::g_refresh_now = true;
        // the keyboard goes to its files once it is shown (Del, F2, Ctrl+C...)
        eui::Later([]() { if (d.open && d.view) Rwidget_focus(d.view->tiles); });
    }

    static void Close()
    {
        if (!d.open)
            return;
        d.open = false;
        d.resizing = false;
        const Rid focused = Rwindow_get_focused_widget(g.window);
        if (IsInside(focused, d.frame))
        {
            if (d.focus_before != R_INVALID_ID && Rwidget_is_valid(d.focus_before))
                Rwidget_focus(d.focus_before);
            else if (g.viewport != R_INVALID_ID)
                Rwidget_focus(g.viewport);
        }
    }

    static void Toggle()
    {
        if (d.open)
            Close();
        else
            Open();
    }

    // ---- build -----------------------------------------------------------------------------------------

    static const char kDrawerXml[] =
        "<Border Name='drawer' Padding='0' Visibility='Collapsed'>"
        "  <Canvas Name='drawer.canvas'>"
        "    <HBox Name='drawer.content' Spacing='0'>"
        "      <Custom Name='drawer.handle.l' Width='6' Tooltip='Drag to resize'/>"
        "      <VBox Name='drawer.body' Spacing='6' Slot.Fill='1' Slot.Padding='10,8,10,10'>"
        "        <HBox Name='drawer.header' Spacing='6'>"
        "          <Icon Glyph='ContentBrowser' Size='16' Slot.VAlign='Center'/>"
        "          <Label Text='CONTENT DRAWER' Class='section' Slot.VAlign='Center'/>"
        "          <Label Text='Left Ctrl + Space' Class='dim' Slot.VAlign='Center' Slot.Fill='1'/>"
        "          <Button Name='drawer.dock' Text='Dock in layout' Class='tool' Focusable='false' Tooltip='Show the Content Browser panel of the level instead'/>"
        "          <Button Name='drawer.side' Text='Open on the right' Class='tool' Focusable='false'/>"
        "          <Button Name='drawer.close' Text='x' Class='tool' Focusable='false' Tooltip='Close (Left Ctrl + Space, Escape)'/>"
        "        </HBox>"
        "      </VBox>"
        "      <Custom Name='drawer.handle.r' Width='6' Tooltip='Drag to resize'/>"
        "    </HBox>"
        "  </Canvas>"
        "</Border>";

    static void PaintHandle(RPainter* p, float w, float h)
    {
        float r = 0.f, gg = 0.f, b = 0.f, a = 0.f;
        Rtheme_get_color(d.handle_hot || d.resizing ? R_COLOR_ACCENT : R_COLOR_SEPARATOR, &r, &gg, &b, &a);
        const float line = d.handle_hot || d.resizing ? 2.f : 1.f;
        const float x = d.side == Side::Left ? w - line : 0.f;
        Rdraw_rect(p, x, 0.f, line, h, 0.f, r, gg, b, a);
    }

    static void BindHandle(Rid handle)
    {
        Rwidget_set_cursor(handle, R_CURSOR_RESIZE_H);
        eui::OnPaint(handle, [](RPainter* p, float w, float h) { PaintHandle(p, w, h); });
        eui::OnEvent(handle, [handle](const REvent& e)
        {
            if (e.type == R_EVENT_MOUSE_DOWN && e.button == R_MOUSE_LEFT)
            {
                d.resizing = true;
                return true;
            }
            if (e.type == R_EVENT_MOUSE_MOVE)
            {
                const bool hot = Rwidget_is_hovered(handle) != 0;
                if (hot != d.handle_hot)
                {
                    d.handle_hot = hot;
                    Rwidget_invalidate(handle);
                }
            }
            return false;
        });
    }

    static void Init()
    {
        d.host = eui::Find(g.window, "drawer.host");
        if (d.host == R_INVALID_ID)
            return;   // an older editor.xml
        if (!d.settings_loaded)
            LoadSettings();

        d.frame = Rwidget_create_from_xml(kDrawerXml, sizeof(kDrawerXml) - 1);
        if (d.frame == R_INVALID_ID)
            return;
        Rwidget_add_child(d.host, d.frame);
        Rwidget_set_clipping(d.frame, R_TRUE);
        auto find = [](const char* name) { return eui::Find(d.frame, name); };
        d.canvas = find("drawer.canvas");
        d.content = find("drawer.content");
        d.body = find("drawer.body");
        d.header = find("drawer.header");
        d.handle_left = find("drawer.handle.l");
        d.handle_right = find("drawer.handle.r");
        d.side_button = find("drawer.side");
        Rwidget_set_clipping(d.canvas, R_TRUE);

        // A second view of the Content Browser, under the header.
        const Rid browser = eui::Load("panels/content_browser.xml");
        if (browser != R_INVALID_ID)
        {
            Rwidget_set_name(browser, "drawer.browser");
            Rwidget_add_child(d.body, browser);
            Rslot_set_fill(browser, 1.f);
            Rslot_set_padding(browser, 0.f, 0.f, 0.f, 0.f);
            d.view = content_browser::AddView(browser);
            // the rows of the view arrive one after the other, its files last
            d.rows.push_back(d.header);
            for (int i = 0; i < Rwidget_get_child_count(browser); ++i)
            {
                const Rid child = Rwidget_get_child(browser, i);
                if (d.view && child == d.view->tiles)
                    d.files = child;
                else
                    d.rows.push_back(child);
            }
        }
        else
            d.rows.push_back(d.header);

        BindHandle(d.handle_left);
        BindHandle(d.handle_right);
        eui::OnClick(find("drawer.close"), []() { Close(); });
        eui::OnClick(d.side_button, []()
        {
            d.side = d.side == Side::Left ? Side::Right : Side::Left;
            ApplySide();
            SaveSettings();
        });
        eui::OnClick(find("drawer.dock"), []()
        {
            Close();
            ShowPanel(g.content_browser);
        });
        // Escape in the drawer closes it.
        eui::OnEvent(d.frame, [](const REvent& e)
        {
            if (e.type == R_EVENT_KEY_DOWN && e.key == R_KEY_ESCAPE && d.open && !Rwindow_has_popup(g.window))
            {
                Close();
                return true;
            }
            return false;
        });
        ApplySide();
    }

    static void Update(float dt)
    {
        if (d.frame == R_INVALID_ID)
            return;

        // Left Ctrl + Space : in any case (Play, a text field, another tab...).
        if (ein::KeyPressed(GLFW_KEY_SPACE) && ein::Key(GLFW_KEY_LEFT_CONTROL))
            Toggle();

        // A click outside closes it (not the click that closes a menu of the drawer).
        const bool popup = Rwindow_has_popup(g.window) != 0;
        if (d.open && !d.resizing && !popup && !d.popup_before && (ein::MouseClicked(0) || ein::MouseClicked(1)))
        {
            double mx = 0.0, my = 0.0;
            ein::MousePosition(mx, my);
            float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
            Rwidget_get_rect(d.frame, &x, &y, &w, &h);
            const bool inside = mx >= x && mx <= x + w && my >= y && my <= y + h;
            float hx = 0.f, hy = 0.f, hw = 0.f, hh = 0.f;
            Rwidget_get_rect(d.host, &hx, &hy, &hw, &hh);
            const bool in_host = my >= hy;   // the menus above it stay usable
            if (!inside && in_host)
                Close();
        }
        d.popup_before = popup;

        // Resizing by its inner edge.
        if (d.resizing)
        {
            if (!ein::Mouse(0))
            {
                d.resizing = false;
                SaveSettings();
            }
            else
            {
                double mx = 0.0, my = 0.0;
                ein::MousePosition(mx, my);
                float hx = 0.f, hy = 0.f, hw = 0.f, hh = 0.f;
                Rwidget_get_rect(d.host, &hx, &hy, &hw, &hh);
                const float wanted = d.side == Side::Left ? static_cast<float>(mx) - hx : hx + hw - static_cast<float>(mx);
                d.width = std::clamp(wanted, 320.f, std::max(320.f, hw * 0.92f));
                d.p = 1.f;
                d.velocity = 0.f;
            }
        }

        Animate(dt);
    }
}
