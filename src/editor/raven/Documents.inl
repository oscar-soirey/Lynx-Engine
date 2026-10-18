// =============================================================================
// Document tabs (as in Unreal) : the level, then one tab per open file
// (included by raven/EditorUi.inl ; API for the code before : ../Documents.h)
// -----------------------------------------------------------------------------
// editor.xml : <TabView Name="documents" Documents="true"> holds the page of
// the level (toolbar + the dock area of the level editor). An editor of a file
// opens a tab : its page has a dock area of its own, where its parts are
// panels (graph in the middle, lists on the left, details on the right...)
// that can be moved, resized or put behind each other.
//
//   Rid dock;
//   doc.panel = documents::OpenSplit(root, "Hero.vasm", Kind::Script,
//                                    { "My Blueprint", "Graph", "Details" },
//                                    [page]() { ... ask to save, then documents::Close(page) ... }, &dock);
//
// The x of a tab (or a middle click) calls the on_close of its editor, which
// asks before closing (unsaved changes) and closes the tab with Close.
// =============================================================================

#include "../Documents.h"

namespace editor_ui::documents
{
    struct Entry
    {
        Rid page = R_INVALID_ID;
        Rid dock = R_INVALID_ID;
        std::function<void()> on_close;
    };

    static std::vector<Entry> g_entries;
    static std::string g_level_title;

    static bool Tabs()
    {
        return g.documents != R_INVALID_ID && Rwidget_is_valid(g.documents);
    }

    static int OnTabClose(Rid, int, Rid content, void*)
    {
        for (const Entry& e : g_entries)
            if (e.page == content && e.on_close)
            {
                const std::function<void()> fn = e.on_close;
                eui::Later(fn);     // the editor asks, then closes the tab
                return R_FALSE;
            }
        g_entries.erase(std::remove_if(g_entries.begin(), g_entries.end(), [content](const Entry& e) { return e.page == content; }),
                        g_entries.end());
        return R_TRUE;
    }

    static void SetIcon(Rid page, lynx::editor::file_icons::Kind kind)
    {
        float uv[4];
        eui::FileIconRegion(kind, uv);
        if (Tabs())
            Rtab_view_set_tab_icon(g.documents, page, eui::FileIconTexture(), uv[0], uv[1], uv[2], uv[3], R_FALSE);
        else
            Rdock_area_set_panel_icon(g.dock, page, eui::FileIconTexture(), uv[0], uv[1], uv[2], uv[3], R_FALSE);
    }

    static void Init()
    {
        g.documents = eui::Find(g.window, "documents");
        g.level_page = eui::Find(g.window, "doc.level");
        if (!Tabs() || g.level_page == R_INVALID_ID)
        {
            g.documents = R_INVALID_ID;   // an older editor.xml : the files open in the dock of the level
            return;
        }
        Rtab_view_set_documents(g.documents, R_TRUE);
        Rtab_view_set_tab_closable(g.documents, g.level_page, R_FALSE);
        SetIcon(g.level_page, lynx::editor::file_icons::Kind::Level);
        Rtab_view_set_close_callback(g.documents, OnTabClose, nullptr);
    }

    // A tab with a dock area of its own ; `dock` : that area.
    static Rid Open(const std::string& title, lynx::editor::file_icons::Kind icon, std::function<void()> on_close, Rid* dock)
    {
        static const char kPage[] = "<VBox><DockArea Name='doc.dock' Slot.Fill='1'/></VBox>";
        const Rid page = Rwidget_create_from_xml(kPage, sizeof(kPage) - 1);
        if (page == R_INVALID_ID)
            return R_INVALID_ID;
        const Rid area = eui::Find(page, "doc.dock");
        if (Tabs())
            Rtab_view_add_tab(g.documents, title.c_str(), page);
        else
            Rdock_area_add_panel(g.dock, page, title.c_str(), R_DOCK_TAB, g.viewport_panel, 0.f);
        SetIcon(page, icon);
        g_entries.push_back({ page, area, std::move(on_close) });
        if (Tabs())
            Rtab_view_show_tab(g.documents, page);
        else
            Rdock_area_show_panel(g.dock, page);
        if (dock)
            *dock = area;
        return page;
    }

    static void AddPanel(Rid dock, Rid panel, const char* title, RDockSide side, Rid relative, float size, lynx::editor::icons::Icon icon)
    {
        Rdock_area_add_panel(dock, panel, title, side, relative, size);
        Rdock_area_set_panel_closable(dock, panel, R_FALSE);
        eui::SetPanelIcon(dock, panel, icon);
    }

    // The editors build one row (left | middle | right) ; each part becomes a panel of the tab.
    struct Parts
    {
        const char* left;
        const char* middle;
        const char* right;
    };

    static Rid OpenSplit(Rid root, const std::string& title, lynx::editor::file_icons::Kind icon, Parts parts,
                         std::function<void()> on_close, Rid* dock_out = nullptr)
    {
        Rid dock = R_INVALID_ID;
        const Rid page = Open(title, icon, std::move(on_close), &dock);
        if (page == R_INVALID_ID)
            return R_INVALID_ID;
        std::vector<Rid> children;
        for (int i = 0; i < Rwidget_get_child_count(root); ++i)
            children.push_back(Rwidget_get_child(root, i));
        for (Rid child : children)
            Rwidget_remove_child(root, child);
        Rwidget_destroy(root);

        using lynx::editor::icons::Icon;
        const Rid middle = children.size() > 1 ? children[1] : (children.empty() ? R_INVALID_ID : children[0]);
        if (middle != R_INVALID_ID)
            AddPanel(dock, middle, parts.middle, R_DOCK_TAB, R_INVALID_ID, 0.f, Icon::Windows);
        if (children.size() > 1 && parts.left)
            AddPanel(dock, children[0], parts.left, R_DOCK_LEFT, middle, 0.22f, Icon::Outliner);
        if (children.size() > 2 && parts.right)
            AddPanel(dock, children[2], parts.right, R_DOCK_RIGHT, middle, 0.27f, Icon::Details);
        if (dock_out)
            *dock_out = dock;
        return page;
    }

    static void SetTitle(Rid page, const std::string& title)
    {
        if (page == R_INVALID_ID || !Rwidget_is_valid(page))
            return;
        if (Tabs())
            Rtab_view_set_tab_title(g.documents, page, title.c_str());
        else
            Rdock_area_set_panel_title(g.dock, page, title.c_str());
    }

    static void Show(Rid page)
    {
        if (page == R_INVALID_ID || !Rwidget_is_valid(page))
            return;
        if (Tabs())
            Rtab_view_show_tab(g.documents, page);
        else
        {
            Rdock_area_open_panel(g.dock, page);
            Rdock_area_show_panel(g.dock, page);
        }
    }

    static void Close(Rid page)
    {
        g_entries.erase(std::remove_if(g_entries.begin(), g_entries.end(), [page](const Entry& e) { return e.page == page; }),
                        g_entries.end());
        if (page == R_INVALID_ID || !Rwidget_is_valid(page))
            return;
        if (Tabs())
            Rwidget_destroy(page);
        else
            Rdock_area_close_panel(g.dock, page);
    }

    static void ShowLevel()
    {
        if (Tabs())
            Rtab_view_show_tab(g.documents, g.level_page);
    }

    static bool LevelActive()
    {
        if (!Tabs())
            return true;
        return Rtab_view_get_active(g.documents) == Rtab_view_find_tab(g.documents, g.level_page);
    }

    // The title of the level tab : the level, * when it has changes.
    static void Update()
    {
        if (!Tabs())
            return;
        const std::string title = "Level  " + std::filesystem::path(current_level_file).stem().string() + (editor_dirty ? " *" : "");
        if (title != g_level_title)
        {
            g_level_title = title;
            Rtab_view_set_tab_title(g.documents, g.level_page, title.c_str());
        }
    }
}

namespace lynx::editor::documents
{
    bool LevelTabActive() { return editor_ui::documents::LevelActive(); }
}
