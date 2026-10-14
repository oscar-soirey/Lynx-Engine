// =============================================================================
// Console panel (stdout / stderr of the editor and of the game) and Build
// panel (output of the build of the game).
// (included by raven/EditorUi.inl)
// =============================================================================

namespace editor_ui::console
{
    namespace oc = lynx::editor::output_console;

    static Rid g_lines = R_INVALID_ID;
    static Rid g_filter = R_INVALID_ID;
    static Rid g_errors = R_INVALID_ID;
    static Rid g_follow = R_INVALID_ID;
    static Rid g_fix = R_INVALID_ID;
    static uint64_t g_last = 0;
    static std::vector<oc::ConsoleLine> g_all;            // every line kept (filter)
    static std::vector<std::string> g_texts;              // for "Fix with Lynxie"
    static int g_last_error = -1;
    static bool g_refill = true;
    static constexpr size_t kMaxLines = 20000;

    static Rid g_build_lines = R_INVALID_ID;
    static Rid g_build_status = R_INVALID_ID;
    static Rid g_build_compile = R_INVALID_ID;
    static Rid g_build_cancel = R_INVALID_ID;
    static uint64_t g_build_version = ~0ull;
    static size_t g_build_shown = 0;

    static void Color(Rid item, int kind)
    {
        if (kind == 2)
            Rtree_item_set_text_color(item, 0.95f, 0.45f, 0.45f, 1.f);
        else if (kind == 1)
            Rtree_item_set_text_color(item, 0.95f, 0.80f, 0.35f, 1.f);
    }

    static int Classify(const std::string& line)
    {
        const std::string lower = Lower(line);
        if (lower.find("error") != std::string::npos)
            return 2;
        if (lower.find("warning") != std::string::npos)
            return 1;
        return 0;
    }

    static bool Passes(const oc::ConsoleLine& line, const std::string& filter, bool errors_only)
    {
        if (errors_only && line.kind == 0)
            return false;
        return filter.empty() || Lower(line.text).find(filter) != std::string::npos;
    }

    static void Append(const std::vector<oc::ConsoleLine>& lines)
    {
        const char* text = Rtext_input_get_text(g_filter);
        const std::string filter = Lower(text ? text : "");
        const bool errors_only = Rcheckbox_is_checked(g_errors) != 0;
        Rid last = R_INVALID_ID;
        for (const auto& line : lines)
        {
            if (!Passes(line, filter, errors_only))
                continue;
            last = Rtree_view_add_item(g_lines, R_INVALID_ID, line.text.c_str());
            Color(last, line.kind);
        }
        if (last != R_INVALID_ID && Rcheckbox_is_checked(g_follow))
            Rtree_view_scroll_to_item(g_lines, last);
    }

    static void Init()
    {
        auto find = [](const char* name) { return eui::Find(g.window, name); };
        g_lines = find("console.lines");
        g_filter = find("console.filter");
        g_errors = find("console.errors");
        g_follow = find("console.follow");
        g_fix = find("console.fix");

        eui::OnText(g_filter, [](const std::string&) { g_refill = true; });
        eui::OnToggle(g_errors, [](bool) { g_refill = true; });
        eui::OnClick(find("console.clear"), []()
        {
            oc::Clear();
            g_all.clear();
            g_texts.clear();
            g_last_error = -1;
            g_refill = true;
        });
        eui::OnClick(find("console.copy"), []()
        {
            std::string all;
            for (const std::string& line : oc::AllText())
                all += line + "\n";
            Rclipboard_set_text(all.c_str());
        });
        eui::OnClick(g_fix, []()
        {
            if (g_last_error >= 0)
                oc::AskFix(g_texts, static_cast<size_t>(g_last_error));
        });

        g_build_lines = find("build.lines");
        g_build_status = find("build.status");
        g_build_compile = find("build.compile");
        g_build_cancel = find("build.cancel");
        eui::OnClick(g_build_compile, []() { lynx::editor::game_build::Start(); });
        eui::OnClick(g_build_cancel, []() { lynx::editor::game_build::Cancel(); });
        eui::OnClick(find("build.copy"), []()
        {
            std::string all;
            for (const std::string& line : lynx::editor::game_build::OutputAll())
                all += line + "\n";
            Rclipboard_set_text(all.c_str());
        });
    }

    static void UpdateBuild()
    {
        const bool running = lynx::editor::game_build::IsRunning();
        eui::SetEnabled(g_build_compile, !running && !currentProject.script_only);
        eui::SetEnabled(g_build_cancel, running);
        const std::string status = lynx::editor::game_build::ToolbarStatus();
        eui::SetText(g_build_status, status);

        uint64_t version = 0;
        const std::vector<std::string> all = lynx::editor::game_build::OutputAll(&version);
        if (version == g_build_version)
            return;
        g_build_version = version;
        if (all.size() < g_build_shown)
        {
            Rtree_view_clear_items(g_build_lines);   // a new build
            g_build_shown = 0;
        }
        Rid last = R_INVALID_ID;
        for (size_t i = g_build_shown; i < all.size(); ++i)
        {
            last = Rtree_view_add_item(g_build_lines, R_INVALID_ID, all[i].c_str());
            Color(last, Classify(all[i]));
        }
        g_build_shown = all.size();
        if (last != R_INVALID_ID)
            Rtree_view_scroll_to_item(g_build_lines, last);
    }

    static void Update()
    {
        std::vector<oc::ConsoleLine> fresh;
        oc::GetSince(g_last, fresh);
        if (!fresh.empty())
            g_last = fresh.back().sequence;
        for (const auto& line : fresh)
        {
            if (line.kind == 2)
                g_last_error = static_cast<int>(g_texts.size());
            g_texts.push_back(line.text);
            g_all.push_back(line);
        }
        // The oldest lines go (as in the Console of the ImGui editor).
        if (g_all.size() > kMaxLines)
        {
            const size_t drop = g_all.size() - kMaxLines;
            g_all.erase(g_all.begin(), g_all.begin() + static_cast<std::ptrdiff_t>(drop));
            g_texts.erase(g_texts.begin(), g_texts.begin() + static_cast<std::ptrdiff_t>(drop));
            g_last_error = g_last_error - static_cast<int>(drop);
            g_refill = true;
        }

        if (g_refill)
        {
            g_refill = false;
            Rtree_view_clear_items(g_lines);
            Append(g_all);
        }
        else if (!fresh.empty())
            Append(fresh);

        eui::SetVisible(g_fix, g_last_error >= 0 && oc::CanFix());
        UpdateBuild();
    }
}
