// =============================================================================
// Paint panel : mode (Paint / Select, Tab), brush (radius, shape, density),
// the voxel types (swatches : VoxelTypeEditor.cpp). The Color Picking panel
// edits the type of the brush (VoxelTypeEditor.cpp too).
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::paint
{
    struct ShapeButton
    {
        BrushShape shape;
        const char* name;
    };

    static const ShapeButton kShapes[] = {
        { BrushShape::Single, "Single" },
        { BrushShape::Circle, "Circle" },
        { BrushShape::Square, "Square" },
        { BrushShape::Diamond, "Diamond" },
        { BrushShape::Horizontal, "Horizontal" },
        { BrushShape::Vertical, "Vertical" },
        { BrushShape::Cross, "Cross" },
        { BrushShape::Line, "Line" },
        { BrushShape::Rectangle, "Rectangle" },
        { BrushShape::RectangleOutline, "Rectangle Outline" },
        { BrushShape::CircleOutline, "Circle Outline" },
    };

    static Rid g_mode_paint = R_INVALID_ID;
    static Rid g_mode_select = R_INVALID_ID;
    static Rid g_brush = R_INVALID_ID;
    static Rid g_radius = R_INVALID_ID;
    static Rid g_radius_value = R_INVALID_ID;
    static Rid g_shape_name = R_INVALID_ID;
    static Rid g_empty = R_INVALID_ID;
    static Rid g_density = R_INVALID_ID;
    static Rid g_density_value = R_INVALID_ID;
    static std::vector<Rid> g_shape_buttons;
    static Rid g_paint_icon = R_INVALID_ID;

    struct Rgba { float r, g, b, a; };

    static constexpr Rgba Color(int r, int g2, int b, int a = 255)
    {
        return { r / 255.f, g2 / 255.f, b / 255.f, a / 255.f };
    }

    // The pixels of a shape in a 9 x 9 grid (as the ImGui icons).
    static bool ShapeCell(BrushShape shape, int x, int y)
    {
        switch (shape)
        {
        case BrushShape::Single: return x == 0 && y == 0;
        case BrushShape::Circle: return x * x + y * y <= 10;
        case BrushShape::Square: return true;
        case BrushShape::Diamond: return std::abs(x) + std::abs(y) <= 4;
        case BrushShape::Horizontal: return y == 0;
        case BrushShape::Vertical: return x == 0;
        case BrushShape::Cross: return x == 0 || y == 0;
        case BrushShape::Line: return y == 0;
        case BrushShape::Rectangle: return std::abs(x) <= 3 && std::abs(y) <= 3;
        case BrushShape::RectangleOutline:
            return (std::abs(x) == 3 || std::abs(y) == 3) && std::abs(x) <= 3 && std::abs(y) <= 3;
        case BrushShape::CircleOutline:
        {
            const int d = x * x + y * y;
            return d >= 8 && d <= 18;
        }
        }
        return false;
    }

    static void PaintShape(RPainter* p, float w, float h, BrushShape shape)
    {
        const bool selected = brushShape == shape;
        const Rgba bg = selected ? Color(240, 160, 40, 70) : Color(60, 58, 66, 255);
        Rdraw_rect(p, 0.f, 0.f, w, h, 4.f, bg.r, bg.g, bg.b, bg.a);
        const Rgba edge = selected ? Color(240, 160, 40) : Color(90, 90, 90);
        Rdraw_rect_outline(p, 0.f, 0.f, w, h, 4.f, selected ? 2.f : 1.f, edge.r, edge.g, edge.b, edge.a);
        const float size = std::min(w, h) * 0.62f;
        const float cell = size / 9.f;
        for (int y = -4; y <= 4; ++y)
            for (int x = -4; x <= 4; ++x)
                if (ShapeCell(shape, x, y))
                    Rdraw_rect(p, w * 0.5f + (x - 0.5f) * cell, h * 0.5f + (y - 0.5f) * cell, cell, cell, 1.f,
                               225 / 255.f, 225 / 255.f, 225 / 255.f, 1.f);
    }

    // Pixel icons of the two modes : a brush (bristles of the brush type) and an arrow.
    static void PaintModeIcon(RPainter* p, float w, float h, int kind)
    {
        const float u = std::floor(std::min(w, h) / 11.f);
        const float ox = std::floor((w - u * 11.f) * 0.5f), oy = std::floor((h - u * 10.f) * 0.5f);
        auto px = [&](int x, int y, Rgba c) { Rdraw_rect(p, ox + x * u, oy + y * u, u, u, 0.f, c.r, c.g, c.b, c.a); };
        const Rgba ink = Color(28, 24, 22);
        if (kind == 0)
        {
            Rgba tip = Color(230, 90, 70);
            if (const lynx::voxels::VoxelType* t = lynx::voxels::GetType(static_cast<uint8_t>(brushVoxelType)))
                tip = { t->color[0], t->color[1], t->color[2], 1.f };
            for (int i = 0; i < 5; ++i) { px(9 - i, i, Color(170, 110, 60)); px(10 - i, i, ink); }
            for (int i = 0; i < 2; ++i) { px(4 - i, 5 + i, Color(160, 160, 170)); px(5 - i, 5 + i, Color(110, 110, 120)); }
            px(1, 7, tip); px(2, 7, tip); px(1, 8, tip); px(0, 9, tip); px(2, 8, ink); px(0, 8, ink);
        }
        else
        {
            static const char* arrow[] = {
                "X.......", "XX......", "XWX.....", "XWWX....", "XWWWX...",
                "XWWWWX..", "XWWXXX..", "XX.XWX..", "X...XWX.", ".....XX.",
            };
            for (int y = 0; y < 10; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    if (arrow[y][x] == 'X') px(x + 1, y, ink);
                    else if (arrow[y][x] == 'W') px(x + 1, y, Color(250, 248, 240));
                }
        }
    }

    static void Init()
    {
        auto find = [](const char* name) { return eui::Find(g.window, name); };
        g_mode_paint = find("paint.mode.paint");
        g_mode_select = find("paint.mode.select");
        g_brush = find("paint.brush");
        g_radius = find("paint.radius");
        g_radius_value = find("paint.radius.value");
        g_shape_name = find("paint.shape.name");
        g_empty = find("paint.empty");
        g_density = find("paint.density");
        g_density_value = find("paint.density.value");
        g_paint_icon = find("paint.mode.paint.icon");

        eui::OnPaint(g_paint_icon, [](RPainter* p, float w, float h) { PaintModeIcon(p, w, h, 0); });
        eui::OnPaint(find("paint.mode.select.icon"), [](RPainter* p, float w, float h) { PaintModeIcon(p, w, h, 1); });
        eui::OnClick(g_mode_paint, []() { brushPaintEnabled = true; });
        eui::OnClick(g_mode_select, []() { brushPaintEnabled = false; });

        eui::OnValue(g_radius, [](double v) { brushRadius = std::clamp(static_cast<int>(std::lround(v)), 1, 256); });
        eui::OnValue(g_density, [](double v) { brushDensity = std::clamp(static_cast<float>(v), 0.f, 1.f); });
        eui::OnToggle(g_empty, [](bool on) { brushPaintEmptyVoxels = on; });

        Rid shapes = find("paint.shapes");
        for (const ShapeButton& button : kShapes)
        {
            Rid w = Rwidget_create(R_WIDGET_CUSTOM);
            Rwidget_set_size(w, 40.f, 40.f);
            Rwidget_set_tooltip(w, button.name);
            Rwidget_set_cursor(w, R_CURSOR_HAND);
            const BrushShape shape = button.shape;
            eui::OnPaint(w, [shape](RPainter* p, float width, float height) { PaintShape(p, width, height, shape); });
            eui::OnEvent(w, [shape](const REvent& event)
            {
                if (event.type == R_EVENT_MOUSE_DOWN && event.button == R_MOUSE_LEFT)
                {
                    brushShape = shape;
                    for (Rid b : g_shape_buttons)
                        Rwidget_invalidate(b);
                    return true;
                }
                return false;
            });
            Rwidget_add_child(shapes, w);
            g_shape_buttons.push_back(w);
        }

        // Voxel types : swatches here, the editor of the type in Color Picking.
        lynx::editor::voxel_types::Host host;
        host.window = g.window;
        host.scene = scene;
        host.get_selected = []() { return brushVoxelType; };
        host.set_selected = [](int type) { brushVoxelType = type; Rwidget_invalidate(g_paint_icon); };
        host.show_editor = []() { ShowPanel(g.color_picking); };
        lynx::editor::voxel_types::SetHost(host);
        lynx::editor::voxel_types::BindPalette(find("paint.palette"), find("paint.palette.hint"));
        lynx::editor::voxel_types::BindEditor(g.color_picking);
    }

    static void Update()
    {
        eui::SetClass(g_mode_paint, "active", brushPaintEnabled);
        eui::SetClass(g_mode_select, "active", !brushPaintEnabled);
        eui::SetVisible(g_brush, brushPaintEnabled);

        eui::SetValue(g_radius, brushRadius);
        eui::SetText(g_radius_value, std::to_string(brushRadius));
        eui::SetValue(g_density, brushDensity);
        eui::SetText(g_density_value, Format("%.2f", brushDensity));
        eui::SetChecked(g_empty, brushPaintEmptyVoxels);

        static BrushShape shown_shape = static_cast<BrushShape>(-1);
        if (shown_shape != brushShape)
        {
            shown_shape = brushShape;
            for (Rid b : g_shape_buttons)
                Rwidget_invalidate(b);
            for (const ShapeButton& b : kShapes)
                if (b.shape == brushShape)
                    eui::SetText(g_shape_name, brushShape == BrushShape::Single ? "Single voxel" : b.name);
        }

        static int shown_type = -1;
        if (shown_type != brushVoxelType)
        {
            shown_type = brushVoxelType;
            Rwidget_invalidate(g_paint_icon);
        }

        lynx::editor::voxel_types::Update();
    }
}
