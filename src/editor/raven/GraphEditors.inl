// =============================================================================
// Graph editors in Raven : Behavior Tree (.bt) and Anim Graph (.animgraph)
// (included by raven/EditorUi.inl ; replaces the ImGui GraphEditors.cpp)
// -----------------------------------------------------------------------------
// One docked panel per open file, a tab next to the Viewport :
//
//   +-------------+------------------------------------+-------------+
//   | Blackboard  |  AnimGraph > Locomotion > Idle      |  Details    |
//   | Parameters  |                                     |  (node,     |
//   | Animations  |   the NodeGraph of Raven            |  state,     |
//   | Blend spaces|                                     |  transition,|
//   | Machines    |                                     |  key...)    |
//   +-------------+------------------------------------+-------------+
//
// - Right click on the canvas : add a node ; on a node : its menu.
// - Drag a pin onto another : link (Behavior Tree : parent -> child ;
//   state machine : a transition from a state to another).
// - Double click : open (State Machine, State). Alt+Up or the path : go up.
// - Del : delete. Ctrl+D : duplicate. Ctrl+Z / Ctrl+Y. Ctrl+S.
// - While the game runs : the running nodes / the current state breathe,
//   with the live Blackboard / parameters (choose the actor at the top).
// =============================================================================

#include "../../gameplay/AnimGraph.h"
#include "../../gameplay/BehaviorTree.h"
#include "../../gameplay/SpriteComponents.h"

#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>

namespace editor_ui::graphs
{
    namespace sfs = std::filesystem;

    enum class Kind { Anim, Tree };

    // What a node of the canvas stands for.
    enum class ItemKind { None, BtNode, Output, Pose, Entry, Any, State, Transition, Motion };
    struct Item
    {
        ItemKind kind = ItemKind::None;
        int id = 0;     // BT node, pose node, state, transition (model ids)
    };

    // What the Details panel shows.
    enum class SelKind { None, BtNode, BtAux, BtKey, Output, Pose, Entry, Any, State, Transition, Motion,
                         Param, Anim, Blend, Machine };
    struct Selection
    {
        SelKind kind = SelKind::None;
        int id = 0;
        int aux = 0;            // decorator / service of a BT node
        std::string name;       // key, parameter, animation, blend space
        bool operator==(const Selection& o) const { return kind == o.kind && id == o.id && aux == o.aux && name == o.name; }
    };

    struct Doc
    {
        Kind kind = Kind::Anim;
        sfs::path path;
        lynx::AnimGraphAsset anim;
        lynx::BehaviorTreeAsset bt;

        std::string saved, snapshot;
        std::vector<std::string> undo, redo;
        bool dirty = false, touched = false;
        sfs::file_time_type write_time{};
        double disk_check = 0.0;
        bool changed_on_disk = false;

        Rid panel = R_INVALID_ID, graph = R_INVALID_ID, left = R_INVALID_ID, details = R_INVALID_ID;
        Rid crumbs = R_INVALID_ID, status = R_INVALID_ID, live = R_INVALID_ID, pick = R_INVALID_ID;
        std::string shown_title;

        // Anim Graph : the level shown (0, 0 : the root graph ; machine : a state machine ; + state : its pose).
        int machine = 0, state = 0;

        std::map<int, Item> items;               // node of the canvas -> what it is
        std::map<int, std::pair<float, float>> transition_pos;   // transition id -> where its node is
        Selection sel;
        bool building = false;
        bool rebuild = true, details_dirty = true, left_dirty = true, frame = true;
        bool check_transitions = false;

        std::map<Rid, Selection> left_rows;      // rows of the lists on the left
        std::map<Rid, std::string> live_rows;     // key / parameter rows : their name (live values)

        int debug_index = 0;
        double debug_time = 0.0;
        size_t debug_count = 0;
        std::set<int> active;                     // nodes of the canvas shown running

        double status_time = -100.0;
    };

    static std::vector<std::unique_ptr<Doc>> g_docs;
    static std::unique_ptr<eui::Menu> g_menu;

    // ---- small helpers ----------------------------------------------------------------

    static std::string GAssetPathOf(const sfs::path& file)
    {
        std::error_code ec;
        const sfs::path assets = sfs::weakly_canonical(sfs::current_path(ec) / "assets", ec);
        const sfs::path relative = sfs::relative(sfs::weakly_canonical(file, ec), assets, ec);
        if (ec || relative.empty() || *relative.begin() == "..")
            return {};
        return relative.generic_string();
    }

    static std::string GExtension(const sfs::path& path)
    {
        std::string e = path.extension().string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }

    static float ToF(const std::string& text, float fallback)
    {
        if (text.empty())
            return fallback;
        char* end = nullptr;
        const float v = std::strtof(text.c_str(), &end);
        return end != text.c_str() ? v : fallback;
    }

    static std::string Num(double v)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", v);
        return buffer;
    }

    static bool ToBool(const std::string& text) { return text == "true" || text == "1" || text == "yes"; }

    static std::vector<std::string> SplitOptions(const char* options)
    {
        std::vector<std::string> out;
        std::string part;
        for (const char* c = options ? options : ""; ; ++c)
        {
            if (*c == '|' || *c == '\0')
            {
                if (!part.empty())
                    out.push_back(part);
                part.clear();
                if (*c == '\0')
                    break;
            }
            else
                part += *c;
        }
        return out;
    }

    static void Status(Doc& doc, const std::string& text)
    {
        eui::SetText(doc.status, text);
        doc.status_time = Rget_time();
        std::cout << "[GRAPH EDITOR] " << text << "\n";
    }

    static void Touch(Doc& doc) { doc.touched = true; }

    static Doc* DocOfGraph(Rid graph)
    {
        for (auto& d : g_docs)
            if (d->graph == graph)
                return d.get();
        return nullptr;
    }

    static bool Alive(const Doc* doc)
    {
        return std::any_of(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
    }

    // Later, if the document is still open (widgets are rebuilt outside of their own callbacks).
    static void DocLater(Doc* doc, std::function<void(Doc&)> fn)
    {
        eui::Later([doc, fn]() { if (Alive(doc)) fn(*doc); });
    }

    // ---- load / save / undo -------------------------------------------------------------

    static std::string Serialize(const Doc& doc)
    {
        return doc.kind == Kind::Anim ? doc.anim.SaveToString() : doc.bt.SaveToString();
    }

    static bool Deserialize(Doc& doc, const std::string& text, std::string* error)
    {
        return doc.kind == Kind::Anim ? doc.anim.LoadFromString(text, error) : doc.bt.LoadFromString(text, error);
    }

    static void ArrangeTree(Doc& doc);
    static bool TreeOverlapping(const lynx::BehaviorTreeAsset& t);
    static void ArrangeAnimIfNeeded(lynx::AnimGraphAsset& g);

    static void LoadDoc(Doc& doc)
    {
        std::ifstream in(doc.path, std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        std::string error;
        if (!Deserialize(doc, text, &error))
        {
            std::cerr << "[GRAPH EDITOR] Could not read " << doc.path.string() << " : " << error << "\n";
            if (doc.kind == Kind::Anim)
                doc.anim = lynx::AnimGraphAsset{};
            else
                doc.bt = lynx::BehaviorTreeAsset::MakeDefault();
        }
        if (doc.kind == Kind::Tree && !doc.bt.Root())
        {
            lynx::BTNodeDesc root = lynx::BehaviorTreeAsset::MakeDefault().nodes.front();
            root.id = doc.bt.NewId();
            doc.bt.nodes.insert(doc.bt.nodes.begin(), root);
        }
        // A hand-written file (no positions) is laid out ; that alone is not an edit.
        if (doc.kind == Kind::Tree && TreeOverlapping(doc.bt))
            ArrangeTree(doc);
        if (doc.kind == Kind::Anim)
            ArrangeAnimIfNeeded(doc.anim);
        doc.snapshot = doc.saved = Serialize(doc);
        doc.undo.clear();
        doc.redo.clear();
        doc.dirty = doc.touched = false;
        doc.sel = {};
        doc.rebuild = doc.details_dirty = doc.left_dirty = doc.frame = true;
        std::error_code ec;
        doc.write_time = sfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;
    }

    static bool SaveDoc(Doc& doc)
    {
        const std::string text = Serialize(doc);
        std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            Status(doc, "Could not write " + doc.path.string());
            return false;
        }
        out << text;
        out.close();
        doc.saved = doc.snapshot = text;
        doc.dirty = doc.touched = false;
        std::error_code ec;
        doc.write_time = sfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;
        Status(doc, "Saved " + doc.path.filename().string());
        return true;
    }

    static void Commit(Doc& doc)
    {
        doc.touched = false;
        const std::string now = Serialize(doc);
        if (now == doc.snapshot)
            return;
        doc.undo.push_back(doc.snapshot);
        if (doc.undo.size() > 200)
            doc.undo.erase(doc.undo.begin());
        doc.redo.clear();
        doc.snapshot = now;
        doc.dirty = now != doc.saved;
    }

    static void Restore(Doc& doc, const std::string& state)
    {
        Deserialize(doc, state, nullptr);
        doc.snapshot = state;
        doc.dirty = state != doc.saved;
        // The level shown may not exist any more.
        if (doc.kind == Kind::Anim)
        {
            lynx::AnimGraphStateMachine* m = doc.machine ? doc.anim.FindStateMachine(doc.machine) : nullptr;
            if (!m)
                doc.machine = doc.state = 0;
            else if (doc.state && std::none_of(m->states.begin(), m->states.end(), [&](const auto& s) { return s.id == doc.state; }))
                doc.state = 0;
        }
        doc.rebuild = doc.details_dirty = doc.left_dirty = true;
    }

    static void Undo(Doc& doc)
    {
        Commit(doc);
        if (doc.undo.empty())
            return;
        doc.redo.push_back(doc.snapshot);
        const std::string state = doc.undo.back();
        doc.undo.pop_back();
        Restore(doc, state);
    }

    static void Redo(Doc& doc)
    {
        if (doc.redo.empty())
            return;
        doc.undo.push_back(doc.snapshot);
        const std::string state = doc.redo.back();
        doc.redo.pop_back();
        Restore(doc, state);
    }

    // ---- canvas helpers --------------------------------------------------------------------

    static int NodeOfPin(Doc& doc, int pin) { return Rnode_graph_get_pin_node(doc.graph, pin); }

    static Item ItemOf(Doc& doc, int node)
    {
        const auto it = doc.items.find(node);
        return it != doc.items.end() ? it->second : Item{};
    }

    static int NodeOf(Doc& doc, ItemKind kind, int id)
    {
        for (const auto& [node, item] : doc.items)
            if (item.kind == kind && item.id == id)
                return node;
        return 0;
    }

    static void Color(Rid graph, int node, float r, float g, float b) { Rnode_graph_set_node_color(graph, node, r, g, b); }

    // Where the pointer is on the canvas (new nodes go there).
    static void PointerOnCanvas(Doc& doc, float& cx, float& cy)
    {
        float wx = 0.f, wy = 0.f, x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        Rwindow_get_mouse_position(g.window, &wx, &wy);
        Rwidget_get_rect(doc.graph, &x, &y, &w, &h);
        Rnode_graph_to_canvas(doc.graph, wx - x, wy - y, &cx, &cy);
    }

    static eui::Menu& NewGraphMenu()
    {
        g_menu = std::make_unique<eui::Menu>(g.window);
        return *g_menu;
    }

    // ---- Details : rows of editors -------------------------------------------------------

    static void ClearBox(Rid box) { Rwidget_clear_children(box); }

    // Title of a part of the panel ("DECORATORS"...), as the <Section> of the editor.
    static Rid AddSection(Rid box, const std::string& title)
    {
        std::string upper = title;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        Rid l = Rwidget_add_child(box, Rlabel_create(upper.c_str()));
        Rwidget_add_class(l, "section");
        Rslot_set_padding(l, 0.f, 8.f, 0.f, 0.f);
        Rid line = Rwidget_add_child(box, Rwidget_create(R_WIDGET_SEPARATOR));
        Rslot_set_padding(line, 0.f, 0.f, 0.f, 2.f);
        return l;
    }

    static Rid AddNote(Rid box, const std::string& text, const char* style_class = "dim")
    {
        Rid l = Rwidget_add_child(box, Rlabel_create(text.c_str()));
        Rlabel_set_wrap(l, R_TRUE);
        if (style_class)
            Rwidget_add_class(l, style_class);
        return l;
    }

    static Rid AddRow(Rid box, const std::string& label, Rid editor, const std::string& tip = {})
    {
        Rid row = Rwidget_add_child(box, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(row, 6.f);
        Rid l = Rwidget_add_child(row, Rlabel_create(label.c_str()));
        Rwidget_set_size(l, 110.f, -1.f);
        Rslot_set_valign(l, R_VALIGN_CENTER);
        if (!tip.empty())
            eui::SetTooltip(l, tip);
        Rwidget_add_child(row, editor);
        Rslot_set_fill(editor, 1.f);
        return row;
    }

    static Rid AddText(Rid box, const std::string& label, const std::string& value,
                       std::function<void(const std::string&)> commit, const std::string& tip = {})
    {
        Rid input = Rwidget_create(R_WIDGET_TEXT_INPUT);
        Rtext_input_set_text(input, value.c_str());
        eui::OnCommit(input, std::move(commit));
        AddRow(box, label, input, tip);
        return input;
    }

    static Rid AddNumber(Rid box, const std::string& label, double value, double lo, double hi, double step, int decimals,
                         std::function<void(double)> changed, const std::string& tip = {})
    {
        Rid spin = Rwidget_create(R_WIDGET_SPIN_BOX);
        Rspin_box_set_range(spin, lo, hi);
        Rspin_box_set_step(spin, step);
        Rspin_box_set_decimals(spin, decimals);
        Rspin_box_set_value(spin, value);
        eui::OnValue(spin, std::move(changed));
        AddRow(box, label, spin, tip);
        return spin;
    }

    static Rid AddCheck(Rid box, const std::string& text, bool value, std::function<void(bool)> changed, const std::string& tip = {})
    {
        Rid check = Rwidget_add_child(box, Rcheckbox_create(text.c_str()));
        Rcheckbox_set_checked(check, value ? R_TRUE : R_FALSE);
        eui::OnToggle(check, std::move(changed));
        if (!tip.empty())
            eui::SetTooltip(check, tip);
        return check;
    }

    static Rid AddCombo(Rid box, const std::string& label, const std::vector<std::string>& options, const std::string& current,
                        std::function<void(const std::string&)> chosen, const std::string& tip = {})
    {
        Rid combo = Rwidget_create(R_WIDGET_COMBO_BOX);
        int selected = -1;
        for (size_t i = 0; i < options.size(); ++i)
        {
            Rcombo_box_add_item(combo, options[i].empty() ? "(none)" : options[i].c_str());
            if (options[i] == current)
                selected = static_cast<int>(i);
        }
        if (selected < 0 && !current.empty())
        {
            // A value the list does not have (a key that was removed) : kept, shown.
            Rcombo_box_add_item(combo, (current + " (missing)").c_str());
            selected = static_cast<int>(options.size());
        }
        Rcombo_box_set_selected(combo, std::max(0, selected));
        auto values = std::make_shared<std::vector<std::string>>(options);
        if (selected == static_cast<int>(options.size()))
            values->push_back(current);
        eui::OnSelect(combo, [values, chosen](int index)
        {
            if (index >= 0 && index < static_cast<int>(values->size()))
                chosen((*values)[static_cast<size_t>(index)]);
        });
        AddRow(box, label, combo, tip);
        return combo;
    }

    static Rid AddButton(Rid box, const std::string& text, std::function<void()> fn, bool primary = false)
    {
        Rid b = Rwidget_add_child(box, Rbutton_create(text.c_str()));
        if (primary)
            Rbutton_set_style(b, R_BUTTON_PRIMARY);
        eui::OnClick(b, std::move(fn));
        return b;
    }

    static Rid AddButtons(Rid box)
    {
        Rid row = Rwidget_add_child(box, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(row, 6.f);
        return row;
    }

    // An asset path (texture...) : text, "..." picks a file of assets/.
    static void AddAsset(Rid box, const std::string& label, const std::string& value, std::function<void(const std::string&)> set)
    {
        Rid row = Rwidget_add_child(box, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(row, 6.f);
        Rid l = Rwidget_add_child(row, Rlabel_create(label.c_str()));
        Rwidget_set_size(l, 110.f, -1.f);
        Rslot_set_valign(l, R_VALIGN_CENTER);
        Rid input = Rwidget_add_child(row, Rwidget_create(R_WIDGET_TEXT_INPUT));
        Rslot_set_fill(input, 1.f);
        Rtext_input_set_text(input, value.c_str());
        auto shared = std::make_shared<std::function<void(const std::string&)>>(std::move(set));
        eui::OnCommit(input, [shared](const std::string& text) { (*shared)(text); });
        Rid pick = Rwidget_add_child(row, Rbutton_create("..."));
        eui::OnClick(pick, [shared, input]()
        {
            const char* now = Rtext_input_get_text(input);
            asset_picker::Open(now ? now : "", [shared, input](const std::string& path)
            {
                if (Rwidget_is_valid(input))
                    Rtext_input_set_text(input, path.c_str());
                (*shared)(path);
            });
        });
    }

    // =============================================================================
    // Behavior Tree
    // =============================================================================

    namespace tree
    {
        using namespace lynx;

        static const char* Category(const std::string& type)
        {
            const BTNodeTypeDef* def = FindBTNodeType(type);
            return def ? def->category : "task";
        }

        static bool IsRoot(const BTNodeDesc& n) { return n.type == "Root"; }

        static std::string NodeTitle(const BTNodeDesc& n)
        {
            if (n.name.empty() || n.name == n.type)
                return n.type;
            return n.name + "  (" + n.type + ")";
        }

        static std::string AuxText(const BTAux& a, const char* category)
        {
            std::string text = a.type;
            const BTNodeTypeDef* def = FindBTNodeType(a.type, category);
            if (def)
                for (const BTParamDef& p : def->params)
                {
                    const std::string v = a.Get(p.name, p.default_value);
                    if (!v.empty() && std::string(p.name) != "abort")
                        text += "  " + v;
                }
            const std::string abort = a.Get("abort", "none");
            if (abort != "none" && !abort.empty())
                text += "  [" + abort + "]";
            return text;
        }

        static std::string ParamsText(const BTNodeDesc& n)
        {
            std::string text;
            const BTNodeTypeDef* def = FindBTNodeType(n.type);
            if (!def)
                return text;
            for (const BTParamDef& p : def->params)
            {
                const std::string v = n.Get(p.name, p.default_value);
                if (v.empty())
                    continue;
                if (!text.empty())
                    text += "\n";
                text += std::string(p.name) + " : " + v;
            }
            return text;
        }

        static bool IsAncestor(const BehaviorTreeAsset& t, int ancestor, int id)
        {
            for (int guard = 0; id != 0 && guard < 4096; ++guard)
            {
                if (id == ancestor)
                    return true;
                const BTNodeDesc* n = t.Find(id);
                id = n ? n->parent : 0;
            }
            return false;
        }

        static void SelectAux(Doc& doc, int node, int aux);

        // Decorators (blue) above, services (green) below, then the parameters.
        static Rid NodeContent(Doc& doc, const BTNodeDesc& n)
        {
            if (n.decorators.empty() && n.services.empty() && ParamsText(n).empty())
                return R_INVALID_ID;
            Rid box = Rwidget_create(R_WIDGET_VBOX);
            Rpanel_set_spacing(box, 2.f);
            Rwidget_set_min_size(box, 170.f, 0.f);
            Doc* d = &doc;
            const int node_id = n.id;
            auto aux_row = [&](const BTAux& a, const char* category, const char* prefix)
            {
                Rid b = Rwidget_add_child(box, Rbutton_create((std::string(prefix) + AuxText(a, category)).c_str()));
                Rwidget_add_class(b, "tool");
                Rwidget_set_attribute(b, "Focusable", "false");
                eui::SetTooltip(b, std::string(category) + " " + a.type + " : click to edit it");
                const int aux = a.id;
                eui::OnClick(b, [d, node_id, aux]() { DocLater(d, [node_id, aux](Doc& doc) { SelectAux(doc, node_id, aux); }); });
            };
            for (const BTAux& a : n.decorators)
                aux_row(a, "decorator", "[D] ");
            const std::string params = ParamsText(n);
            if (!params.empty())
                AddNote(box, params);
            for (const BTAux& a : n.services)
                aux_row(a, "service", "[S] ");
            return box;
        }

        static void ColorOf(Doc& doc, int node, const BTNodeDesc& n)
        {
            const std::string cat = Category(n.type);
            if (IsRoot(n))
                Color(doc.graph, node, 0.36f, 0.36f, 0.42f);
            else if (cat == "composite")
                Color(doc.graph, node, n.type == "Selector" ? 0.78f : 0.62f, n.type == "Selector" ? 0.48f : 0.62f, n.type == "Selector" ? 0.18f : 0.70f);
            else
                Color(doc.graph, node, 0.55f, 0.36f, 0.78f);
        }

        // Execution order of the children of each node : the badge of the node (1, 2...).
        static void UpdateBadges(Doc& doc)
        {
            for (const auto& [node, item] : doc.items)
            {
                const BTNodeDesc* n = doc.bt.Find(item.id);
                std::string badge;
                if (n && n->parent != 0)
                {
                    const auto children = doc.bt.ChildrenOf(n->parent);
                    for (size_t i = 0; i < children.size(); ++i)
                        if (children[i]->id == n->id)
                            badge = std::to_string(i + 1);
                }
                Rnode_graph_set_node_badge(doc.graph, node, badge.c_str());
            }
        }

        static void RefreshNode(Doc& doc, int id)
        {
            const BTNodeDesc* n = doc.bt.Find(id);
            const int node = NodeOf(doc, ItemKind::BtNode, id);
            if (!n || !node)
                return;
            Rnode_graph_set_node_title(doc.graph, node, NodeTitle(*n).c_str());
            Rnode_graph_set_node_content(doc.graph, node, NodeContent(doc, *n));
            ColorOf(doc, node, *n);
        }

        static int AddCanvasNode(Doc& doc, const BTNodeDesc& n)
        {
            const int node = Rnode_graph_add_node(doc.graph, NodeTitle(n).c_str(), n.x, n.y);
            doc.items[node] = { ItemKind::BtNode, n.id };
            if (!IsRoot(n))
                Rnode_graph_add_input(doc.graph, node, "", "bt");
            const std::string cat = Category(n.type);
            if (IsRoot(n) || cat == "composite")
                Rnode_graph_add_output(doc.graph, node, "", "bt");
            Rnode_graph_set_node_content(doc.graph, node, NodeContent(doc, n));
            Rnode_graph_set_node_deletable(doc.graph, node, IsRoot(n) ? R_FALSE : R_TRUE);
            ColorOf(doc, node, n);
            return node;
        }

        static void Build(Doc& doc)
        {
            for (const BTNodeDesc& n : doc.bt.nodes)
                AddCanvasNode(doc, n);
            for (const BTNodeDesc& n : doc.bt.nodes)
            {
                if (n.parent == 0)
                    continue;
                const int child = NodeOf(doc, ItemKind::BtNode, n.id);
                const int parent = NodeOf(doc, ItemKind::BtNode, n.parent);
                const int out = Rnode_graph_get_output(doc.graph, parent, 0);
                const int in = Rnode_graph_get_input(doc.graph, child, 0);
                if (out && in)
                    Rnode_graph_connect(doc.graph, out, in);
            }
            UpdateBadges(doc);
        }

        static void SelectAux(Doc& doc, int node, int aux)
        {
            doc.sel = { SelKind::BtAux, node, aux, {} };
            doc.details_dirty = true;
            const int canvas = NodeOf(doc, ItemKind::BtNode, node);
            if (canvas)
                Rnode_graph_select_node(doc.graph, canvas, R_FALSE);
        }

        static BTNodeDesc* AddNode(Doc& doc, const std::string& type, float x, float y, int parent = 0)
        {
            BTNodeDesc n;
            n.id = doc.bt.NewId();
            n.type = type;
            n.x = x;
            n.y = y;
            n.parent = parent;
            if (const BTNodeTypeDef* def = FindBTNodeType(type))
                for (const BTParamDef& p : def->params)
                    if (p.default_value && p.default_value[0])
                        n.Set(p.name, p.default_value);
            doc.bt.nodes.push_back(n);
            const int node = AddCanvasNode(doc, doc.bt.nodes.back());
            if (parent)
            {
                const int out = Rnode_graph_get_output(doc.graph, NodeOf(doc, ItemKind::BtNode, parent), 0);
                const int in = Rnode_graph_get_input(doc.graph, node, 0);
                if (out && in)
                    Rnode_graph_connect(doc.graph, out, in);
            }
            UpdateBadges(doc);
            Rnode_graph_select_node(doc.graph, node, R_FALSE);
            doc.sel = { SelKind::BtNode, n.id, 0, {} };
            doc.details_dirty = true;
            Touch(doc);
            return doc.bt.Find(n.id);
        }

        static void AddAux(Doc& doc, int node_id, const std::string& type, bool service)
        {
            BTNodeDesc* n = doc.bt.Find(node_id);
            if (!n)
                return;
            BTAux a;
            a.id = doc.bt.NewId();
            a.type = type;
            if (const BTNodeTypeDef* def = FindBTNodeType(type, service ? "service" : "decorator"))
                for (const BTParamDef& p : def->params)
                    if (p.default_value && p.default_value[0])
                        a.Set(p.name, p.default_value);
            (service ? n->services : n->decorators).push_back(a);
            RefreshNode(doc, node_id);
            doc.sel = { SelKind::BtAux, node_id, a.id, {} };
            doc.details_dirty = true;
            Touch(doc);
        }

        static void RemoveAux(Doc& doc, int node_id, int aux)
        {
            BTNodeDesc* n = doc.bt.Find(node_id);
            if (!n)
                return;
            for (auto* list : { &n->decorators, &n->services })
                list->erase(std::remove_if(list->begin(), list->end(), [aux](const BTAux& a) { return a.id == aux; }), list->end());
            RefreshNode(doc, node_id);
            doc.sel = { SelKind::BtNode, node_id, 0, {} };
            doc.details_dirty = true;
            Touch(doc);
        }

        static void DeleteNode(Doc& doc, int id, bool remove_canvas)
        {
            BTNodeDesc* n = doc.bt.Find(id);
            if (!n || IsRoot(*n))
                return;
            for (BTNodeDesc& c : doc.bt.nodes)
                if (c.parent == id)
                    c.parent = 0;
            const int node = NodeOf(doc, ItemKind::BtNode, id);
            doc.bt.nodes.erase(std::remove_if(doc.bt.nodes.begin(), doc.bt.nodes.end(), [id](const BTNodeDesc& x) { return x.id == id; }),
                               doc.bt.nodes.end());
            if (node)
            {
                doc.items.erase(node);
                if (remove_canvas)
                    Rnode_graph_remove_node(doc.graph, node);
            }
            if (doc.sel.id == id)
                doc.sel = {};
            doc.details_dirty = true;
            UpdateBadges(doc);
            Touch(doc);
        }

        // The selection (and the links between the copies), a little lower right.
        static void Duplicate(Doc& doc)
        {
            std::vector<int> ids;
            for (int i = 0; i < Rnode_graph_get_selection_count(doc.graph); ++i)
            {
                const Item item = ItemOf(doc, Rnode_graph_get_selected_node(doc.graph, i));
                const BTNodeDesc* n = item.kind == ItemKind::BtNode ? doc.bt.Find(item.id) : nullptr;
                if (n && !IsRoot(*n))
                    ids.push_back(n->id);
            }
            if (ids.empty())
                return;
            std::map<int, int> copy_of;
            int next = doc.bt.NewId();
            for (int id : ids)
                copy_of[id] = next++;
            std::vector<BTNodeDesc> copies;
            for (int id : ids)
            {
                BTNodeDesc c = *doc.bt.Find(id);
                c.id = copy_of[id];
                c.x += 40.f;
                c.y += 40.f;
                c.parent = copy_of.count(c.parent) ? copy_of[c.parent] : 0;
                for (auto* list : { &c.decorators, &c.services })
                    for (BTAux& a : *list)
                        a.id = next++;
                copies.push_back(c);
            }
            Rnode_graph_clear_selection(doc.graph);
            for (const BTNodeDesc& c : copies)
                doc.bt.nodes.push_back(c);
            doc.rebuild = true;
            Touch(doc);
            DocLater(&doc, [copies](Doc& d)
            {
                for (const BTNodeDesc& c : copies)
                    if (int node = NodeOf(d, ItemKind::BtNode, c.id))
                        Rnode_graph_select_node(d.graph, node, R_TRUE);
            });
        }

        // Laid out from the root : depth to the right, children top to bottom.
        static float Place(BehaviorTreeAsset& t, int id, int depth, float& y)
        {
            BTNodeDesc* n = t.Find(id);
            if (!n)
                return y;
            auto children = t.ChildrenOf(id);
            n->x = static_cast<float>(depth) * 280.f;
            if (children.empty())
            {
                n->y = y;
                y += 120.f;
                return n->y;
            }
            float first = 0.f, last = 0.f;
            for (size_t i = 0; i < children.size(); ++i)
            {
                const float cy = Place(t, children[i]->id, depth + 1, y);
                if (i == 0) first = cy;
                last = cy;
            }
            n = t.Find(id);
            n->y = (first + last) * 0.5f;
            return n->y;
        }
    }

    static bool TreeOverlapping(const lynx::BehaviorTreeAsset& t)
    {
        for (size_t i = 0; i < t.nodes.size(); ++i)
            for (size_t j = i + 1; j < t.nodes.size(); ++j)
                if (std::abs(t.nodes[i].x - t.nodes[j].x) < 1.f && std::abs(t.nodes[i].y - t.nodes[j].y) < 1.f)
                    return true;
        return false;
    }

    static void ArrangeTree(Doc& doc)
    {
        lynx::BTNodeDesc* root = doc.bt.Root();
        if (!root)
            return;
        float y = 0.f;
        tree::Place(doc.bt, root->id, 0, y);
        // The nodes linked to nothing : under the tree.
        float x = 0.f;
        for (lynx::BTNodeDesc& n : doc.bt.nodes)
            if (n.parent == 0 && !tree::IsRoot(n))
            {
                n.x = x;
                n.y = y + 60.f;
                x += 240.f;
            }
    }

    // =============================================================================
    // Anim Graph
    // =============================================================================

    namespace anim
    {
        using namespace lynx;

        static AnimGraphStateMachine* Machine(Doc& doc) { return doc.machine ? doc.anim.FindStateMachine(doc.machine) : nullptr; }

        static AnimGraphState* State(Doc& doc, int id)
        {
            AnimGraphStateMachine* m = Machine(doc);
            if (!m)
                return nullptr;
            for (AnimGraphState& s : m->states)
                if (s.id == id)
                    return &s;
            return nullptr;
        }

        static AnimGraphTransition* Transition(Doc& doc, int id)
        {
            AnimGraphStateMachine* m = Machine(doc);
            if (!m)
                return nullptr;
            for (AnimGraphTransition& t : m->transitions)
                if (t.id == id)
                    return &t;
            return nullptr;
        }

        static std::string MotionKind(const AnimGraphAsset& g, const std::string& name)
        {
            if (g.IsBlendSpace(name))
                return "Blend Space";
            if (g.IsAnimation(name))
                return "Animation";
            return "Pose";
        }

        static std::string PoseTitle(const AnimGraphPoseNode& n)
        {
            const char* kind = n.type == "statemachine" ? "State Machine" : n.type == "blendspace" ? "Blend Space" : "Animation";
            return std::string(kind) + " : " + (n.source.empty() ? "(none)" : n.source);
        }

        static void PoseColor(Doc& doc, int node, const std::string& type)
        {
            if (type == "statemachine")
                Color(doc.graph, node, 0.86f, 0.52f, 0.18f);
            else if (type == "blendspace")
                Color(doc.graph, node, 0.26f, 0.66f, 0.42f);
            else
                Color(doc.graph, node, 0.28f, 0.52f, 0.86f);
        }

        static std::string ConditionText(const AnimGraphCondition& c)
        {
            if (c.kind == "compare")
                return c.param + " " + c.op + " " + Num(c.value);
            if (c.kind == "isTrue")
                return c.param;
            if (c.kind == "isFalse")
                return "not " + c.param;
            if (c.kind == "trigger")
                return c.param + " (trigger)";
            if (c.kind == "finished")
                return "animation finished";
            if (c.kind == "script")
                return c.function + "()";
            return c.kind;
        }

        static std::string TransitionText(const AnimGraphTransition& t)
        {
            std::string text;
            for (const AnimGraphCondition& c : t.conditions)
                text += (text.empty() ? "" : "\nand ") + ConditionText(c);
            if (t.wait_finished)
                text += (text.empty() ? "" : "\n") + std::string("after the animation");
            return text.empty() ? "always" : text;
        }

        static Rid TransitionContent(const AnimGraphTransition& t)
        {
            Rid box = Rwidget_create(R_WIDGET_VBOX);
            AddNote(box, TransitionText(t));
            return box;
        }

        static Rid StateContent(const AnimGraphAsset& g, const AnimGraphState& s)
        {
            Rid box = Rwidget_create(R_WIDGET_VBOX);
            AddNote(box, s.motion.empty() ? std::string("no pose : double click") : MotionKind(g, s.motion) + " : " + s.motion);
            return box;
        }

        // ---- the three levels ----

        static void BuildRoot(Doc& doc)
        {
            const int output = Rnode_graph_add_node(doc.graph, "Output Pose", doc.anim.output_x, doc.anim.output_y);
            doc.items[output] = { ItemKind::Output, 0 };
            Rnode_graph_add_input(doc.graph, output, "Result", "pose");
            Rnode_graph_set_node_deletable(doc.graph, output, R_FALSE);
            Color(doc.graph, output, 0.36f, 0.36f, 0.42f);
            for (const AnimGraphPoseNode& n : doc.anim.pose_nodes)
            {
                const int node = Rnode_graph_add_node(doc.graph, PoseTitle(n).c_str(), n.x, n.y);
                doc.items[node] = { ItemKind::Pose, n.id };
                Rnode_graph_add_output(doc.graph, node, "Pose", "pose");
                PoseColor(doc, node, n.type);
                if (n.type == "statemachine")
                {
                    Rid box = Rwidget_create(R_WIDGET_VBOX);
                    AddNote(box, "double click to open");
                    Rnode_graph_set_node_content(doc.graph, node, box);
                }
                if (doc.anim.output_source == n.id)
                    Rnode_graph_connect(doc.graph, Rnode_graph_get_output(doc.graph, node, 0), Rnode_graph_get_input(doc.graph, output, 0));
            }
        }

        static void TransitionPosition(Doc& doc, const AnimGraphStateMachine& m, const AnimGraphTransition& t, float& x, float& y)
        {
            const auto it = doc.transition_pos.find(t.id);
            if (it != doc.transition_pos.end())
            {
                x = it->second.first;
                y = it->second.second;
                return;
            }
            float fx = m.any_x, fy = m.any_y, tx = 0.f, ty = 0.f;
            if (const AnimGraphState* from = m.FindState(t.from)) { fx = from->x; fy = from->y; }
            if (const AnimGraphState* to = m.FindState(t.to)) { tx = to->x; ty = to->y; }
            x = (fx + tx) * 0.5f + 40.f;
            y = (fy + ty) * 0.5f + 10.f;
            // The way back sits a little lower : the two do not cover each other.
            for (const AnimGraphTransition& o : m.transitions)
                if (o.id < t.id && o.from == t.to && o.to == t.from)
                    y += 70.f;
        }

        static void BuildMachine(Doc& doc, AnimGraphStateMachine& m)
        {
            const int entry = Rnode_graph_add_node(doc.graph, "Entry", m.entry_x, m.entry_y);
            doc.items[entry] = { ItemKind::Entry, 0 };
            Rnode_graph_add_output(doc.graph, entry, "", "flow");
            Rnode_graph_set_node_deletable(doc.graph, entry, R_FALSE);
            Color(doc.graph, entry, 0.24f, 0.56f, 0.30f);

            const int any = Rnode_graph_add_node(doc.graph, "Any State", m.any_x, m.any_y);
            doc.items[any] = { ItemKind::Any, 0 };
            Rnode_graph_add_output(doc.graph, any, "", "flow");
            Rnode_graph_set_node_deletable(doc.graph, any, R_FALSE);
            Color(doc.graph, any, 0.60f, 0.45f, 0.20f);

            for (const AnimGraphState& s : m.states)
            {
                const int node = Rnode_graph_add_node(doc.graph, s.name.c_str(), s.x, s.y);
                doc.items[node] = { ItemKind::State, s.id };
                const int in = Rnode_graph_add_input(doc.graph, node, "", "flow");
                Rnode_graph_set_pin_multiple(doc.graph, in, R_TRUE);
                Rnode_graph_add_output(doc.graph, node, "", "flow");
                Rnode_graph_set_node_content(doc.graph, node, StateContent(doc.anim, s));
                Color(doc.graph, node, 0.30f, 0.36f, 0.50f);
                if (s.name == m.entry_state)
                    Rnode_graph_connect(doc.graph, Rnode_graph_get_output(doc.graph, entry, 0), in);
            }
            for (const AnimGraphTransition& t : m.transitions)
            {
                float x = 0.f, y = 0.f;
                TransitionPosition(doc, m, t, x, y);
                const int node = Rnode_graph_add_node(doc.graph, (t.priority ? "Transition  #" + std::to_string(t.priority) : std::string("Transition")).c_str(), x, y);
                doc.items[node] = { ItemKind::Transition, t.id };
                const int in = Rnode_graph_add_input(doc.graph, node, "", "flow");
                const int out = Rnode_graph_add_output(doc.graph, node, "", "flow");
                Rnode_graph_set_node_content(doc.graph, node, TransitionContent(t));
                Color(doc.graph, node, 0.42f, 0.42f, 0.46f);
                const int from = t.from.empty() ? any : [&]() { const AnimGraphState* s = m.FindState(t.from); return s ? NodeOf(doc, ItemKind::State, s->id) : 0; }();
                const AnimGraphState* to_state = m.FindState(t.to);
                const int to = to_state ? NodeOf(doc, ItemKind::State, to_state->id) : 0;
                if (from)
                    Rnode_graph_connect(doc.graph, Rnode_graph_get_output(doc.graph, from, 0), in);
                if (to)
                    Rnode_graph_connect(doc.graph, out, Rnode_graph_get_input(doc.graph, to, 0));
            }
        }

        static void BuildState(Doc& doc, AnimGraphState& s)
        {
            const int output = Rnode_graph_add_node(doc.graph, "Output Pose", s.output_x, s.output_y);
            doc.items[output] = { ItemKind::Output, 0 };
            Rnode_graph_add_input(doc.graph, output, "Result", "pose");
            Rnode_graph_set_node_deletable(doc.graph, output, R_FALSE);
            Color(doc.graph, output, 0.36f, 0.36f, 0.42f);
            if (!s.motion.empty())
            {
                const std::string kind = MotionKind(doc.anim, s.motion);
                const int node = Rnode_graph_add_node(doc.graph, (kind + " : " + s.motion).c_str(), s.pose_x, s.pose_y);
                doc.items[node] = { ItemKind::Motion, s.id };
                Rnode_graph_add_output(doc.graph, node, "Pose", "pose");
                PoseColor(doc, node, kind == "Blend Space" ? "blendspace" : "animation");
                Rnode_graph_connect(doc.graph, Rnode_graph_get_output(doc.graph, node, 0), Rnode_graph_get_input(doc.graph, output, 0));
            }
        }

        static void Build(Doc& doc)
        {
            AnimGraphStateMachine* m = Machine(doc);
            if (!m)
            {
                doc.machine = doc.state = 0;
                BuildRoot(doc);
                return;
            }
            if (doc.state)
                if (AnimGraphState* s = State(doc, doc.state))
                {
                    BuildState(doc, *s);
                    return;
                }
            doc.state = 0;
            BuildMachine(doc, *m);
        }

        static void Open(Doc& doc, int machine, int state)
        {
            Commit(doc);
            doc.machine = machine;
            doc.state = state;
            doc.sel = {};
            doc.rebuild = doc.frame = doc.details_dirty = true;
        }

        static void GoUp(Doc& doc)
        {
            if (doc.state)
                Open(doc, doc.machine, 0);
            else if (doc.machine)
                Open(doc, 0, 0);
        }

        static std::string UniqueName(const std::vector<std::string>& used, const std::string& base)
        {
            std::string name = base;
            for (int i = 2; std::find(used.begin(), used.end(), name) != used.end(); ++i)
                name = base + std::to_string(i);
            return name;
        }

        static std::vector<std::string> MotionNames(const AnimGraphAsset& g)
        {
            std::vector<std::string> out;
            for (const auto& a : g.animations) out.push_back(a.name);
            for (const auto& b : g.blend_spaces) out.push_back(b.name);
            return out;
        }

        static std::vector<std::string> AllNames(const AnimGraphAsset& g)
        {
            std::vector<std::string> out = MotionNames(g);
            for (const auto& m : g.state_machines) out.push_back(m.name);
            return out;
        }

        static std::vector<std::string> ParamNames(const AnimGraphAsset& g, const char* type = nullptr)
        {
            std::vector<std::string> out;
            for (const auto& p : g.parameters)
                if (!type || p.type == type)
                    out.push_back(p.name);
            return out;
        }

        static void AddPoseNode(Doc& doc, const std::string& type, const std::string& source, float x, float y)
        {
            AnimGraphPoseNode n;
            n.id = doc.anim.NewId();
            n.type = type;
            n.source = source;
            n.x = x;
            n.y = y;
            doc.anim.pose_nodes.push_back(n);
            if (doc.anim.output_source == 0)
                doc.anim.output_source = n.id;
            doc.rebuild = true;
            doc.sel = { SelKind::Pose, n.id, 0, {} };
            doc.details_dirty = true;
            Touch(doc);
        }

        static AnimGraphStateMachine& NewMachine(Doc& doc)
        {
            AnimGraphStateMachine m;
            m.id = doc.anim.NewId();
            m.name = UniqueName(AllNames(doc.anim), "StateMachine");
            m.entry_x = -260.f;
            m.any_x = -260.f;
            m.any_y = 220.f;
            doc.anim.state_machines.push_back(m);
            doc.left_dirty = true;
            Touch(doc);
            return doc.anim.state_machines.back();
        }

        static void AddState(Doc& doc, float x, float y)
        {
            AnimGraphStateMachine* m = Machine(doc);
            if (!m)
                return;
            std::vector<std::string> used;
            for (const auto& s : m->states) used.push_back(s.name);
            AnimGraphState s;
            s.id = doc.anim.NewId();
            s.name = UniqueName(used, "State");
            s.x = x;
            s.y = y;
            s.output_x = 320.f;
            m->states.push_back(s);
            if (m->entry_state.empty())
                m->entry_state = s.name;
            doc.rebuild = true;
            doc.sel = { SelKind::State, s.id, 0, {} };
            doc.details_dirty = true;
            Touch(doc);
        }

        static void AddTransition(Doc& doc, const std::string& from, const std::string& to)
        {
            AnimGraphStateMachine* m = Machine(doc);
            if (!m || to.empty() || from == to)
                return;
            AnimGraphTransition t;
            t.id = doc.anim.NewId();
            t.from = from;
            t.to = to;
            m->transitions.push_back(t);
            doc.rebuild = true;
            doc.sel = { SelKind::Transition, t.id, 0, {} };
            doc.details_dirty = true;
            Touch(doc);
        }

        static void DeleteState(Doc& doc, int id)
        {
            AnimGraphStateMachine* m = Machine(doc);
            AnimGraphState* s = State(doc, id);
            if (!m || !s)
                return;
            const std::string name = s->name;
            m->states.erase(std::remove_if(m->states.begin(), m->states.end(), [id](const AnimGraphState& x) { return x.id == id; }), m->states.end());
            m->transitions.erase(std::remove_if(m->transitions.begin(), m->transitions.end(),
                [&](const AnimGraphTransition& t) { return t.to == name || (!t.from.empty() && t.from == name); }), m->transitions.end());
            if (m->entry_state == name)
                m->entry_state = m->states.empty() ? std::string() : m->states.front().name;
            for (AnimGraphState& o : m->states)
                if (o.next == name)
                    o.next.clear();
            doc.sel = {};
            doc.rebuild = doc.details_dirty = true;
            Touch(doc);
        }

        static void DeleteTransition(Doc& doc, int id)
        {
            AnimGraphStateMachine* m = Machine(doc);
            if (!m)
                return;
            m->transitions.erase(std::remove_if(m->transitions.begin(), m->transitions.end(), [id](const AnimGraphTransition& t) { return t.id == id; }),
                                 m->transitions.end());
            doc.transition_pos.erase(id);
            if (doc.sel.kind == SelKind::Transition && doc.sel.id == id)
                doc.sel = {};
            doc.rebuild = doc.details_dirty = true;
            Touch(doc);
        }

        static void DeletePose(Doc& doc, int id)
        {
            auto& v = doc.anim.pose_nodes;
            v.erase(std::remove_if(v.begin(), v.end(), [id](const AnimGraphPoseNode& n) { return n.id == id; }), v.end());
            if (doc.anim.output_source == id)
                doc.anim.output_source = 0;
            doc.sel = {};
            doc.details_dirty = true;
            Touch(doc);
        }

        static void Duplicate(Doc& doc)
        {
            AnimGraphStateMachine* m = Machine(doc);
            if (!m || doc.state)
                return;
            std::vector<std::string> used;
            for (const auto& s : m->states) used.push_back(s.name);
            std::vector<AnimGraphState> copies;
            for (int i = 0; i < Rnode_graph_get_selection_count(doc.graph); ++i)
            {
                const Item item = ItemOf(doc, Rnode_graph_get_selected_node(doc.graph, i));
                if (item.kind != ItemKind::State)
                    continue;
                if (const AnimGraphState* s = State(doc, item.id))
                {
                    AnimGraphState c = *s;
                    c.id = doc.anim.NewId() + static_cast<int>(copies.size());
                    c.name = UniqueName(used, s->name);
                    used.push_back(c.name);
                    c.x += 40.f;
                    c.y += 40.f;
                    copies.push_back(c);
                }
            }
            for (const auto& c : copies)
                m->states.push_back(c);
            if (!copies.empty())
            {
                doc.rebuild = true;
                Touch(doc);
            }
        }
    }

    // A hand-written file : nodes on top of each other get a simple layout.
    static void ArrangeAnimIfNeeded(lynx::AnimGraphAsset& g)
    {
        for (auto& m : g.state_machines)
        {
            bool overlap = false;
            for (size_t i = 0; i < m.states.size() && !overlap; ++i)
                for (size_t j = i + 1; j < m.states.size(); ++j)
                    if (std::abs(m.states[i].x - m.states[j].x) < 1.f && std::abs(m.states[i].y - m.states[j].y) < 1.f)
                        overlap = true;
            if (!overlap)
                continue;
            for (size_t i = 0; i < m.states.size(); ++i)
            {
                m.states[i].x = 260.f * static_cast<float>(i % 4);
                m.states[i].y = 180.f * static_cast<float>(i / 4);
            }
            m.entry_x = -260.f;
            m.entry_y = 0.f;
            m.any_x = -260.f;
            m.any_y = 220.f;
        }
        for (size_t i = 0; i < g.pose_nodes.size(); ++i)
            if (g.pose_nodes[i].x == 0.f && g.pose_nodes[i].y == 0.f && i > 0)
                g.pose_nodes[i].y = 140.f * static_cast<float>(i);
    }

    // =============================================================================
    // Canvas callbacks
    // =============================================================================

    static int OnConnect(Rid graph, int output_pin, int input_pin, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc || doc->building)
            return R_TRUE;
        const Item from = ItemOf(*doc, NodeOfPin(*doc, output_pin));
        const Item to = ItemOf(*doc, NodeOfPin(*doc, input_pin));
        if (doc->kind == Kind::Tree)
        {
            // A child cannot be one of the parents of its parent.
            if (from.kind != ItemKind::BtNode || to.kind != ItemKind::BtNode)
                return R_FALSE;
            if (tree::IsAncestor(doc->bt, to.id, from.id))
            {
                Status(*doc, "That link would make a loop.");
                return R_FALSE;
            }
            return R_TRUE;
        }

        // Anim Graph
        if (doc->machine && !doc->state)
        {
            lynx::AnimGraphStateMachine* m = anim::Machine(*doc);
            if (!m)
                return R_FALSE;
            if (from.kind == ItemKind::Entry)
                return to.kind == ItemKind::State ? R_TRUE : R_FALSE;
            if ((from.kind == ItemKind::State || from.kind == ItemKind::Any) && to.kind == ItemKind::State)
            {
                // A state to another : a transition node is made between them.
                const lynx::AnimGraphState* a = from.kind == ItemKind::State ? anim::State(*doc, from.id) : nullptr;
                const lynx::AnimGraphState* b = anim::State(*doc, to.id);
                const std::string from_name = a ? a->name : std::string();
                const std::string to_name = b ? b->name : std::string();
                DocLater(doc, [from_name, to_name](Doc& d) { anim::AddTransition(d, from_name, to_name); });
                return R_FALSE;
            }
            // Wires of a transition moved to another state.
            if (from.kind == ItemKind::Transition && to.kind == ItemKind::State)
                return R_TRUE;
            if ((from.kind == ItemKind::State || from.kind == ItemKind::Any) && to.kind == ItemKind::Transition)
                return R_TRUE;
            return R_FALSE;
        }
        // Root graph and pose of a state : a pose into Output Pose.
        return to.kind == ItemKind::Output ? R_TRUE : R_FALSE;
    }

    static void OnLinked(Rid graph, int link, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc || doc->building)
            return;
        const int out_pin = Rnode_graph_get_link_output(graph, link);
        const int in_pin = Rnode_graph_get_link_input(graph, link);
        const int from_node = NodeOfPin(*doc, out_pin);
        const Item from = ItemOf(*doc, from_node);
        const Item to = ItemOf(*doc, NodeOfPin(*doc, in_pin));
        if (doc->kind == Kind::Tree)
        {
            lynx::BTNodeDesc* child = doc->bt.Find(to.id);
            lynx::BTNodeDesc* parent = doc->bt.Find(from.id);
            if (!child || !parent)
                return;
            child->parent = parent->id;
            // The root has one child : the new one replaces the old.
            if (tree::IsRoot(*parent))
            {
                for (int i = Rnode_graph_get_link_count(graph) - 1; i >= 0; --i)
                {
                    const int other = Rnode_graph_get_link(graph, i);
                    if (other == link || Rnode_graph_get_link_output(graph, other) != out_pin)
                        continue;
                    const Item old = ItemOf(*doc, NodeOfPin(*doc, Rnode_graph_get_link_input(graph, other)));
                    if (lynx::BTNodeDesc* n = doc->bt.Find(old.id))
                        n->parent = 0;
                    Rnode_graph_disconnect(graph, other);
                }
            }
            tree::UpdateBadges(*doc);
            doc->details_dirty = true;
            Touch(*doc);
            return;
        }
        if (doc->machine && !doc->state)
        {
            lynx::AnimGraphStateMachine* m = anim::Machine(*doc);
            if (!m)
                return;
            if (from.kind == ItemKind::Entry)
            {
                if (const lynx::AnimGraphState* s = anim::State(*doc, to.id))
                    m->entry_state = s->name;
                // One entry : the other wires of Entry go.
                for (int i = Rnode_graph_get_link_count(graph) - 1; i >= 0; --i)
                {
                    const int other = Rnode_graph_get_link(graph, i);
                    if (other != link && Rnode_graph_get_link_output(graph, other) == out_pin)
                        Rnode_graph_disconnect(graph, other);
                }
            }
            else if (from.kind == ItemKind::Transition)
            {
                if (lynx::AnimGraphTransition* t = anim::Transition(*doc, from.id))
                    if (const lynx::AnimGraphState* s = anim::State(*doc, to.id))
                        t->to = s->name;
            }
            else if (to.kind == ItemKind::Transition)
            {
                if (lynx::AnimGraphTransition* t = anim::Transition(*doc, to.id))
                {
                    const lynx::AnimGraphState* s = from.kind == ItemKind::State ? anim::State(*doc, from.id) : nullptr;
                    t->from = s ? s->name : std::string();
                }
            }
            doc->details_dirty = true;
            Touch(*doc);
            return;
        }
        if (to.kind == ItemKind::Output)
        {
            if (doc->state)
            {
                // (the pose of a state is its motion node : nothing else can be linked)
            }
            else if (from.kind == ItemKind::Pose)
                doc->anim.output_source = from.id;
            doc->details_dirty = true;
            Touch(*doc);
        }
    }

    static void OnUnlinked(Rid graph, int link, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc || doc->building)
            return;
        const Item from = ItemOf(*doc, NodeOfPin(*doc, Rnode_graph_get_link_output(graph, link)));
        const Item to = ItemOf(*doc, NodeOfPin(*doc, Rnode_graph_get_link_input(graph, link)));
        if (doc->kind == Kind::Tree)
        {
            if (lynx::BTNodeDesc* child = doc->bt.Find(to.id))
                if (child->parent == from.id)
                    child->parent = 0;
            DocLater(doc, [](Doc& d) { tree::UpdateBadges(d); });
            doc->details_dirty = true;
            Touch(*doc);
            return;
        }
        if (doc->machine && !doc->state)
        {
            lynx::AnimGraphStateMachine* m = anim::Machine(*doc);
            if (!m)
                return;
            if (from.kind == ItemKind::Entry)
                m->entry_state.clear();
            // A transition left without one of its wires goes (checked once the gesture is over).
            if (from.kind == ItemKind::Transition || to.kind == ItemKind::Transition)
                doc->check_transitions = true;
            Touch(*doc);
            return;
        }
        if (to.kind == ItemKind::Output)
        {
            if (doc->state)
            {
                if (lynx::AnimGraphState* s = anim::State(*doc, doc->state))
                    if (from.kind == ItemKind::Motion)
                    {
                        s->motion.clear();
                        doc->rebuild = true;
                    }
            }
            else if (from.kind == ItemKind::Pose && doc->anim.output_source == from.id)
                doc->anim.output_source = 0;
            doc->details_dirty = true;
            Touch(*doc);
        }
    }

    static void OnMoved(Rid graph, int node, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc || doc->building)
            return;
        float x = 0.f, y = 0.f;
        Rnode_graph_get_node_position(graph, node, &x, &y);
        const Item item = ItemOf(*doc, node);
        if (doc->kind == Kind::Tree)
        {
            if (lynx::BTNodeDesc* n = doc->bt.Find(item.id))
            {
                n->x = x;
                n->y = y;
            }
            DocLater(doc, [](Doc& d) { tree::UpdateBadges(d); });
            Touch(*doc);
            return;
        }
        lynx::AnimGraphStateMachine* m = anim::Machine(*doc);
        switch (item.kind)
        {
        case ItemKind::Output:
            if (doc->state)
            {
                if (lynx::AnimGraphState* s = anim::State(*doc, doc->state)) { s->output_x = x; s->output_y = y; }
            }
            else
            {
                doc->anim.output_x = x;
                doc->anim.output_y = y;
            }
            break;
        case ItemKind::Pose:
            if (lynx::AnimGraphPoseNode* n = doc->anim.FindPoseNode(item.id)) { n->x = x; n->y = y; }
            break;
        case ItemKind::Motion:
            if (lynx::AnimGraphState* s = anim::State(*doc, item.id)) { s->pose_x = x; s->pose_y = y; }
            break;
        case ItemKind::Entry:
            if (m) { m->entry_x = x; m->entry_y = y; }
            break;
        case ItemKind::Any:
            if (m) { m->any_x = x; m->any_y = y; }
            break;
        case ItemKind::State:
            if (lynx::AnimGraphState* s = anim::State(*doc, item.id)) { s->x = x; s->y = y; }
            break;
        case ItemKind::Transition:
            doc->transition_pos[item.id] = { x, y };
            return;   // (not in the file)
        default:
            break;
        }
        Touch(*doc);
    }

    static Selection SelectionOf(Doc& doc, const Item& item)
    {
        switch (item.kind)
        {
        case ItemKind::BtNode: return { SelKind::BtNode, item.id, 0, {} };
        case ItemKind::Output: return { SelKind::Output, 0, 0, {} };
        case ItemKind::Pose: return { SelKind::Pose, item.id, 0, {} };
        case ItemKind::Entry: return { SelKind::Entry, 0, 0, {} };
        case ItemKind::Any: return { SelKind::Any, 0, 0, {} };
        case ItemKind::State: return { SelKind::State, item.id, 0, {} };
        case ItemKind::Transition: return { SelKind::Transition, item.id, 0, {} };
        case ItemKind::Motion: return { SelKind::Motion, item.id, 0, {} };
        default: break;
        }
        (void)doc;
        return {};
    }

    static void OnSelection(Rid graph, int node, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc || doc->building)
            return;
        Selection sel;
        if (Rnode_graph_get_selection_count(graph) == 1)
            sel = SelectionOf(*doc, ItemOf(*doc, Rnode_graph_get_selected_node(graph, 0)));
        else if (node && Rnode_graph_is_node_selected(graph, node))
            sel = SelectionOf(*doc, ItemOf(*doc, node));
        // (a click on a decorator row keeps it)
        if (doc->sel.kind == SelKind::BtAux && sel.kind == SelKind::BtNode && sel.id == doc->sel.id)
            return;
        if (!(sel == doc->sel))
        {
            doc->sel = sel;
            doc->details_dirty = true;
        }
    }

    static void OnRemoved(Rid graph, int node, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc || doc->building)
            return;
        const Item item = ItemOf(*doc, node);
        if (doc->kind == Kind::Tree)
        {
            tree::DeleteNode(*doc, item.id, false);
            return;
        }
        switch (item.kind)
        {
        case ItemKind::Pose: anim::DeletePose(*doc, item.id); break;
        case ItemKind::State: { const int id = item.id; DocLater(doc, [id](Doc& d) { anim::DeleteState(d, id); }); break; }
        case ItemKind::Transition: { const int id = item.id; DocLater(doc, [id](Doc& d) { anim::DeleteTransition(d, id); }); break; }
        case ItemKind::Motion:
            if (lynx::AnimGraphState* s = anim::State(*doc, item.id))
                s->motion.clear();
            doc->details_dirty = true;
            Touch(*doc);
            break;
        default:
            break;
        }
        doc->items.erase(node);
    }

    static void OnActivated(Rid graph, int node, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc || doc->kind != Kind::Anim)
            return;
        const Item item = ItemOf(*doc, node);
        if (item.kind == ItemKind::Pose)
        {
            const lynx::AnimGraphPoseNode* n = doc->anim.FindPoseNode(item.id);
            if (n && n->type == "statemachine")
                if (lynx::AnimGraphStateMachine* m = doc->anim.FindStateMachine(n->source))
                {
                    const int id = m->id;
                    DocLater(doc, [id](Doc& d) { anim::Open(d, id, 0); });
                }
        }
        else if (item.kind == ItemKind::State)
        {
            const int machine = doc->machine, state = item.id;
            DocLater(doc, [machine, state](Doc& d) { anim::Open(d, machine, state); });
        }
    }

    // ---- context menus ------------------------------------------------------------------

    static void TreeMenu(Doc& doc, int node)
    {
        using namespace lynx;
        Doc* d = &doc;
        eui::Menu& menu = NewGraphMenu();
        float cx = 0.f, cy = 0.f;
        PointerOnCanvas(doc, cx, cy);
        const Item item = ItemOf(doc, node);
        BTNodeDesc* n = item.kind == ItemKind::BtNode ? doc.bt.Find(item.id) : nullptr;
        if (n)
        {
            const int id = n->id;
            menu.Title(tree::NodeTitle(*n));
            if (std::string(tree::Category(n->type)) != "root")
            {
                eui::Menu& deco = menu.Sub("Add decorator");
                eui::Menu& serv = menu.Sub("Add service");
                for (const BTNodeTypeDef& t : GetBTNodeTypes())
                {
                    const std::string type = t.type;
                    if (std::string(t.category) == "decorator")
                        deco.Item(type, [d, id, type]() { DocLater(d, [id, type](Doc& x) { tree::AddAux(x, id, type, false); }); });
                    else if (std::string(t.category) == "service")
                        serv.Item(type, [d, id, type]() { DocLater(d, [id, type](Doc& x) { tree::AddAux(x, id, type, true); }); });
                }
            }
            if (std::string(tree::Category(n->type)) == "composite" || tree::IsRoot(*n))
            {
                eui::Menu& child = menu.Sub("Add child");
                const float x = n->x + 280.f;
                const float y = n->y + 120.f * static_cast<float>(doc.bt.ChildrenOf(id).size());
                for (const BTNodeTypeDef& t : GetBTNodeTypes())
                {
                    const std::string cat = t.category, type = t.type;
                    if (cat == "composite" || cat == "task")
                        child.Item(type, [d, id, type, x, y]() { DocLater(d, [id, type, x, y](Doc& z) { tree::AddNode(z, type, x, y, id); }); });
                }
            }
            menu.Separator();
            if (!tree::IsRoot(*n))
            {
                menu.Item("Duplicate", [d]() { DocLater(d, [](Doc& x) { tree::Duplicate(x); }); }, true, "Ctrl+D");
                if (n->parent != 0)
                    menu.Item("Unlink from the parent", [d, id]()
                    {
                        DocLater(d, [id](Doc& x)
                        {
                            if (BTNodeDesc* c = x.bt.Find(id)) c->parent = 0;
                            x.rebuild = true;
                            Touch(x);
                        });
                    });
                menu.Item("Delete", [d, id]() { DocLater(d, [id](Doc& x) { tree::DeleteNode(x, id, true); }); }, true, "Del");
            }
        }
        else
        {
            eui::Menu& comp = menu.Sub("Composite");
            eui::Menu& task = menu.Sub("Task");
            for (const BTNodeTypeDef& t : GetBTNodeTypes())
            {
                const std::string cat = t.category, type = t.type;
                if (cat != "composite" && cat != "task")
                    continue;
                (cat == "composite" ? comp : task).Item(type, [d, type, cx, cy]()
                {
                    DocLater(d, [type, cx, cy](Doc& x) { tree::AddNode(x, type, cx, cy); });
                });
            }
            menu.Separator();
            menu.Item("Arrange the tree", [d]() { DocLater(d, [](Doc& x) { ArrangeTree(x); x.rebuild = x.frame = true; Touch(x); }); });
            menu.Item("Frame all", [d]() { Rnode_graph_frame_all(d->graph); }, true, "F");
        }
        menu.Open();
    }

    static void AnimMenu(Doc& doc, int node)
    {
        using namespace lynx;
        Doc* d = &doc;
        eui::Menu& menu = NewGraphMenu();
        float cx = 0.f, cy = 0.f;
        PointerOnCanvas(doc, cx, cy);
        const Item item = ItemOf(doc, node);

        if (item.kind != ItemKind::None)
        {
            const int id = item.id;
            switch (item.kind)
            {
            case ItemKind::Pose:
            {
                const AnimGraphPoseNode* n = doc.anim.FindPoseNode(id);
                if (n && n->type == "statemachine")
                    if (const AnimGraphStateMachine* m = doc.anim.FindStateMachine(n->source))
                    {
                        const int mid = m->id;
                        menu.Item("Open", [d, mid]() { DocLater(d, [mid](Doc& x) { anim::Open(x, mid, 0); }); });
                    }
                menu.Item("Use as the output", [d, id]() { DocLater(d, [id](Doc& x) { x.anim.output_source = id; x.rebuild = true; Touch(x); }); });
                menu.Item("Delete", [d, node]() { DocLater(d, [node](Doc& x) { Rnode_graph_select_node(x.graph, node, R_FALSE); OnRemoved(x.graph, node, nullptr); Rnode_graph_remove_node(x.graph, node); }); });
                break;
            }
            case ItemKind::State:
            {
                const int machine = doc.machine;
                menu.Item("Open its pose", [d, machine, id]() { DocLater(d, [machine, id](Doc& x) { anim::Open(x, machine, id); }); });
                menu.Item("Set as the entry state", [d, id]()
                {
                    DocLater(d, [id](Doc& x)
                    {
                        if (AnimGraphState* s = anim::State(x, id))
                            if (AnimGraphStateMachine* m = anim::Machine(x)) { m->entry_state = s->name; x.rebuild = true; Touch(x); }
                    });
                });
                menu.Item("Duplicate", [d]() { DocLater(d, [](Doc& x) { anim::Duplicate(x); }); }, true, "Ctrl+D");
                menu.Item("Delete", [d, id]() { DocLater(d, [id](Doc& x) { anim::DeleteState(x, id); }); }, true, "Del");
                break;
            }
            case ItemKind::Transition:
                menu.Item("Delete", [d, id]() { DocLater(d, [id](Doc& x) { anim::DeleteTransition(x, id); }); }, true, "Del");
                break;
            case ItemKind::Motion:
                menu.Item("Remove the pose", [d, id]()
                {
                    DocLater(d, [id](Doc& x) { if (AnimGraphState* s = anim::State(x, id)) { s->motion.clear(); x.rebuild = x.details_dirty = true; Touch(x); } });
                });
                break;
            default:
                menu.Title("(always there)");
                break;
            }
            menu.Open();
            return;
        }

        if (doc.machine && !doc.state)
        {
            menu.Item("Add state", [d, cx, cy]() { DocLater(d, [cx, cy](Doc& x) { anim::AddState(x, cx, cy); }); });
        }
        else if (doc.state)
        {
            eui::Menu& sub = menu.Sub("Pose of the state");
            for (const std::string& name : anim::MotionNames(doc.anim))
            {
                const int sid = doc.state;
                sub.Item(name, [d, sid, name, cx, cy]()
                {
                    DocLater(d, [sid, name, cx, cy](Doc& x)
                    {
                        if (AnimGraphState* s = anim::State(x, sid)) { s->motion = name; s->pose_x = cx; s->pose_y = cy; x.rebuild = x.details_dirty = true; Touch(x); }
                    });
                });
            }
            if (doc.anim.animations.empty() && doc.anim.blend_spaces.empty())
                sub.Title("No animation yet (left panel : +)");
        }
        else
        {
            eui::Menu& a = menu.Sub("Animation");
            for (const auto& x : doc.anim.animations)
            {
                const std::string name = x.name;
                a.Item(name, [d, name, cx, cy]() { DocLater(d, [name, cx, cy](Doc& z) { anim::AddPoseNode(z, "animation", name, cx, cy); }); });
            }
            if (doc.anim.animations.empty())
                a.Title("None yet (left panel : +)");
            eui::Menu& b = menu.Sub("Blend Space");
            for (const auto& x : doc.anim.blend_spaces)
            {
                const std::string name = x.name;
                b.Item(name, [d, name, cx, cy]() { DocLater(d, [name, cx, cy](Doc& z) { anim::AddPoseNode(z, "blendspace", name, cx, cy); }); });
            }
            if (doc.anim.blend_spaces.empty())
                b.Title("None yet (left panel : +)");
            eui::Menu& m = menu.Sub("State Machine");
            for (const auto& x : doc.anim.state_machines)
            {
                const std::string name = x.name;
                m.Item(name, [d, name, cx, cy]() { DocLater(d, [name, cx, cy](Doc& z) { anim::AddPoseNode(z, "statemachine", name, cx, cy); }); });
            }
            m.Item("New state machine", [d, cx, cy]()
            {
                DocLater(d, [cx, cy](Doc& z) { const std::string name = anim::NewMachine(z).name; anim::AddPoseNode(z, "statemachine", name, cx, cy); });
            });
        }
        menu.Separator();
        if (doc.machine)
            menu.Item("Go up", [d]() { DocLater(d, [](Doc& x) { anim::GoUp(x); }); }, true, "Alt+Up");
        menu.Item("Frame all", [d]() { Rnode_graph_frame_all(d->graph); }, true, "F");
        menu.Open();
    }

    static void OnContext(Rid graph, int node, float, float, void*)
    {
        Doc* doc = DocOfGraph(graph);
        if (!doc)
            return;
        if (doc->kind == Kind::Tree)
            TreeMenu(*doc, node);
        else
            AnimMenu(*doc, node);
    }

    // =============================================================================
    // Building the canvas
    // =============================================================================

    static void RebuildCanvas(Doc& doc)
    {
        doc.rebuild = false;
        doc.building = true;
        float cx = 0.f, cy = 0.f, zoom = 1.f;
        Rnode_graph_get_view(doc.graph, &cx, &cy, &zoom);
        Rnode_graph_clear(doc.graph);
        doc.items.clear();
        doc.active.clear();
        if (doc.kind == Kind::Tree)
            tree::Build(doc);
        else
            anim::Build(doc);
        // The selection follows the rebuild.
        for (const auto& [node, item] : doc.items)
            if (SelectionOf(doc, item) == doc.sel ||
                (doc.sel.kind == SelKind::BtAux && item.kind == ItemKind::BtNode && item.id == doc.sel.id))
                Rnode_graph_select_node(doc.graph, node, R_TRUE);
        doc.building = false;
        if (doc.frame)
        {
            doc.frame = false;
            Rnode_graph_frame_all(doc.graph);
        }
        else
            Rnode_graph_set_view(doc.graph, cx, cy, zoom);
    }

    // The path at the top : AnimGraph > Locomotion > Idle.
    static void RebuildCrumbs(Doc& doc)
    {
        ClearBox(doc.crumbs);
        Doc* d = &doc;
        auto crumb = [&](const std::string& text, std::function<void()> fn, bool current)
        {
            Rid b = Rwidget_add_child(doc.crumbs, Rbutton_create(text.c_str()));
            Rwidget_add_class(b, current ? "active" : "tool");
            Rwidget_set_attribute(b, "Focusable", "false");
            eui::OnClick(b, std::move(fn));
        };
        if (doc.kind == Kind::Tree)
        {
            crumb(doc.path.stem().string(), []() {}, true);
            return;
        }
        crumb("AnimGraph", [d]() { DocLater(d, [](Doc& x) { anim::Open(x, 0, 0); }); }, doc.machine == 0);
        if (lynx::AnimGraphStateMachine* m = anim::Machine(doc))
        {
            Rwidget_add_child(doc.crumbs, Rlabel_create(">"));
            const int mid = m->id;
            crumb(m->name, [d, mid]() { DocLater(d, [mid](Doc& x) { anim::Open(x, mid, 0); }); }, doc.state == 0);
            if (lynx::AnimGraphState* s = anim::State(doc, doc.state))
            {
                Rwidget_add_child(doc.crumbs, Rlabel_create(">"));
                crumb(s->name, []() {}, true);
            }
        }
    }

    // =============================================================================
    // Details
    // =============================================================================

    namespace details
    {
        using namespace lynx;

        static std::vector<std::string> KeyNames(const BehaviorTreeAsset& t)
        {
            std::vector<std::string> out = { "" };
            for (const BTKeyDesc& k : t.keys)
                out.push_back(k.name);
            return out;
        }

        // The editor of one parameter of a BT node / decorator / service.
        static void ParamEditor(Doc& doc, Rid box, const BTParamDef& p, std::function<std::string()> get,
                                std::function<void(const std::string&)> set)
        {
            const std::string type = p.type ? p.type : "string", value = get();
            const std::string help = p.help ? p.help : "";
            const std::string fallback = p.default_value ? p.default_value : "";
            if (type == "float" || type == "int")
            {
                const bool is_int = type == "int";
                AddNumber(box, p.name, ToF(value, ToF(fallback, 0.f)), -1e6, 1e6, is_int ? 1.0 : 0.1, is_int ? 0 : 2,
                          [set, is_int](double v) { set(is_int ? std::to_string(static_cast<int>(v)) : Num(v)); }, help);
            }
            else if (type == "bool")
                AddCheck(box, p.name, ToBool(value.empty() ? fallback : value), [set](bool on) { set(on ? "true" : "false"); }, help);
            else if (type == "key")
                AddCombo(box, p.name, KeyNames(doc.bt), value, set, help.empty() ? std::string("A key of the Blackboard") : help);
            else if (type == "enum")
                AddCombo(box, p.name, SplitOptions(p.options), value.empty() ? fallback : value, set, help);
            else
                AddText(box, p.name, value, set, help.empty() && type == "class" ? std::string("Name of the JavaScript class") : help);
        }

        static void TreeNodeDetails(Doc& doc, BTNodeDesc& n)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            const int id = n.id;
            const BTNodeTypeDef* def = FindBTNodeType(n.type);
            AddSection(box, n.type);
            if (def && def->description)
                AddNote(box, def->description);
            AddText(box, "Name", n.name, [d, id](const std::string& text)
            {
                if (BTNodeDesc* x = d->bt.Find(id)) { x->name = text; tree::RefreshNode(*d, id); Touch(*d); }
            });
            if (def)
                for (const BTParamDef& p : def->params)
                {
                    const std::string name = p.name, fallback = p.default_value ? p.default_value : "";
                    ParamEditor(doc, box, p,
                        [d, id, name, fallback]() { const BTNodeDesc* x = d->bt.Find(id); return x ? x->Get(name, fallback) : fallback; },
                        [d, id, name](const std::string& v) { if (BTNodeDesc* x = d->bt.Find(id)) { x->Set(name, v); tree::RefreshNode(*d, id); Touch(*d); } });
                }
            if (tree::IsRoot(n))
                return;
            auto list = [&](const std::vector<BTAux>& items, const char* title, const char* category)
            {
                if (items.empty())
                    return;
                AddSection(box, title);
                for (const BTAux& a : items)
                {
                    const int aux = a.id;
                    Rid row = AddButtons(box);
                    Rid b = Rwidget_add_child(row, Rbutton_create(tree::AuxText(a, category).c_str()));
                    Rslot_set_fill(b, 1.f);
                    eui::OnClick(b, [d, id, aux]() { DocLater(d, [id, aux](Doc& x) { tree::SelectAux(x, id, aux); }); });
                    Rid r = Rwidget_add_child(row, Rbutton_create("x"));
                    eui::SetTooltip(r, "Remove");
                    eui::OnClick(r, [d, id, aux]() { DocLater(d, [id, aux](Doc& x) { tree::RemoveAux(x, id, aux); }); });
                }
            };
            list(n.decorators, "Decorators", "decorator");
            list(n.services, "Services", "service");
            AddNote(box, "Right click the node : add a decorator or a service.");
        }

        static void AuxDetails(Doc& doc, BTNodeDesc& n, int aux_id)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            const int id = n.id;
            BTAux* aux = nullptr;
            bool service = false;
            for (BTAux& a : n.decorators) if (a.id == aux_id) aux = &a;
            for (BTAux& a : n.services) if (a.id == aux_id) { aux = &a; service = true; }
            if (!aux)
            {
                TreeNodeDetails(doc, n);
                return;
            }
            const char* category = service ? "service" : "decorator";
            const BTNodeTypeDef* def = FindBTNodeType(aux->type, category);
            Rid back = AddButton(box, "< " + tree::NodeTitle(n), [d, id]()
            {
                DocLater(d, [id](Doc& x) { x.sel = { SelKind::BtNode, id, 0, {} }; x.details_dirty = true; });
            });
            Rwidget_add_class(back, "tool");
            AddSection(box, std::string(service ? "Service " : "Decorator ") + aux->type);
            if (def && def->description)
                AddNote(box, def->description);
            if (def)
                for (const BTParamDef& p : def->params)
                {
                    const std::string name = p.name, fallback = p.default_value ? p.default_value : "";
                    auto find = [d, id, aux_id]() -> BTAux*
                    {
                        BTNodeDesc* x = d->bt.Find(id);
                        if (!x) return nullptr;
                        for (auto* list : { &x->decorators, &x->services })
                            for (BTAux& a : *list) if (a.id == aux_id) return &a;
                        return nullptr;
                    };
                    ParamEditor(doc, box, p,
                        [find, name, fallback]() { BTAux* a = find(); return a ? a->Get(name, fallback) : fallback; },
                        [d, id, find, name](const std::string& v) { if (BTAux* a = find()) { a->Set(name, v); tree::RefreshNode(*d, id); Touch(*d); } });
                }
            Rid row = AddButtons(box);
            Rid del = Rwidget_add_child(row, Rbutton_create(service ? "Remove the service" : "Remove the decorator"));
            eui::OnClick(del, [d, id, aux_id]() { DocLater(d, [id, aux_id](Doc& x) { tree::RemoveAux(x, id, aux_id); }); });
        }

        static void KeyDetails(Doc& doc, const std::string& key_name)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            BTKeyDesc* key = nullptr;
            for (BTKeyDesc& k : doc.bt.keys) if (k.name == key_name) key = &k;
            if (!key)
                return;
            auto shared_name = std::make_shared<std::string>(key_name);
            auto find = [d, shared_name]() -> BTKeyDesc* { for (BTKeyDesc& k : d->bt.keys) if (k.name == *shared_name) return &k; return nullptr; };
            AddSection(box, "Blackboard key");
            AddText(box, "Name", key->name, [d, find, shared_name](const std::string& text)
            {
                BTKeyDesc* k = find();
                if (!k || text.empty() || text == k->name)
                    return;
                for (const BTKeyDesc& o : d->bt.keys)
                    if (o.name == text) { Status(*d, "A key is already named " + text); return; }
                const std::string old = k->name;
                k->name = text;
                // The nodes that read the key follow.
                auto fix = [&](const std::string& type, const char* category, std::vector<BTParam>& params)
                {
                    const BTNodeTypeDef* def = category ? FindBTNodeType(type, category) : FindBTNodeType(type);
                    if (!def) return;
                    for (const BTParamDef& p : def->params)
                        if (std::string(p.type) == "key")
                            for (auto& v : params)
                                if (v.name == p.name && v.value == old)
                                    v.value = text;
                };
                for (auto& n : d->bt.nodes)
                {
                    fix(n.type, nullptr, n.params);
                    for (auto& a : n.decorators) fix(a.type, "decorator", a.params);
                    for (auto& a : n.services) fix(a.type, "service", a.params);
                }
                *shared_name = text;
                d->sel.name = text;
                d->rebuild = d->left_dirty = true;
                Touch(*d);
            });
            AddCombo(box, "Type", { "bool", "int", "float", "string", "vector", "actor" }, key->type, [d, find](const std::string& t)
            {
                if (BTKeyDesc* k = find()) { k->type = t; d->left_dirty = true; Touch(*d); }
            });
            AddText(box, "Default", key->default_value, [d, find](const std::string& text)
            {
                if (BTKeyDesc* k = find()) { k->default_value = text; Touch(*d); }
            }, "Value at the start : 3, true, 1 2 0 (vector), a text...");
            Rid row = AddButtons(box);
            Rid del = Rwidget_add_child(row, Rbutton_create("Delete the key"));
            eui::OnClick(del, [d, shared_name]()
            {
                DocLater(d, [shared_name](Doc& x)
                {
                    auto& keys = x.bt.keys;
                    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const BTKeyDesc& k) { return k.name == *shared_name; }), keys.end());
                    x.sel = {};
                    x.details_dirty = x.left_dirty = true;
                    Touch(x);
                });
            });
        }

        // ---- Anim Graph ----

        static void ConditionEditor(Doc& doc, Rid box, int transition, size_t index)
        {
            Doc* d = &doc;
            AnimGraphTransition* t = anim::Transition(doc, transition);
            if (!t || index >= t->conditions.size())
                return;
            auto find = [d, transition, index]() -> AnimGraphCondition*
            {
                AnimGraphTransition* x = anim::Transition(*d, transition);
                return x && index < x->conditions.size() ? &x->conditions[index] : nullptr;
            };
            auto refresh = [d]() { d->rebuild = true; Touch(*d); };
            const AnimGraphCondition& c = t->conditions[index];
            Rid row = AddButtons(box);
            Rid title = Rwidget_add_child(row, Rlabel_create(("Condition " + std::to_string(index + 1)).c_str()));
            Rwidget_add_class(title, "section");
            Rslot_set_fill(title, 1.f);
            Rid del = Rwidget_add_child(row, Rbutton_create("x"));
            eui::SetTooltip(del, "Remove the condition");
            eui::OnClick(del, [d, transition, index]()
            {
                DocLater(d, [transition, index](Doc& x)
                {
                    if (AnimGraphTransition* tt = anim::Transition(x, transition))
                        if (index < tt->conditions.size()) tt->conditions.erase(tt->conditions.begin() + static_cast<std::ptrdiff_t>(index));
                    x.rebuild = x.details_dirty = true;
                    Touch(x);
                });
            });
            AddCombo(box, "Kind", { "compare", "isTrue", "isFalse", "trigger", "finished", "script" }, c.kind, [d, find](const std::string& v)
            {
                if (AnimGraphCondition* x = find()) { x->kind = v; d->rebuild = d->details_dirty = true; Touch(*d); }
            });
            if (c.kind == "compare" || c.kind == "isTrue" || c.kind == "isFalse" || c.kind == "trigger")
            {
                std::vector<std::string> params = anim::ParamNames(doc.anim, c.kind == "trigger" ? "trigger" : nullptr);
                params.insert(params.begin(), "");
                AddCombo(box, "Parameter", params, c.param, [find, refresh](const std::string& v) { if (AnimGraphCondition* x = find()) { x->param = v; refresh(); } });
            }
            if (c.kind == "compare")
            {
                AddCombo(box, "Operator", { ">", ">=", "<", "<=", "==", "!=" }, c.op, [find, refresh](const std::string& v) { if (AnimGraphCondition* x = find()) { x->op = v; refresh(); } });
                AddNumber(box, "Value", c.value, -1e6, 1e6, 0.1, 2, [find, refresh](double v) { if (AnimGraphCondition* x = find()) { x->value = static_cast<float>(v); refresh(); } });
            }
            if (c.kind == "script")
                AddText(box, "Function", c.function, [find, refresh](const std::string& v) { if (AnimGraphCondition* x = find()) { x->function = v; refresh(); } },
                        "A JavaScript function of the actor that returns true / false");
        }

        static void TransitionDetails(Doc& doc, AnimGraphTransition& t)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            const int id = t.id;
            AddSection(box, "Transition");
            AddNote(box, (t.from.empty() ? std::string("Any State") : t.from) + "  ->  " + t.to, nullptr);
            AddNumber(box, "Priority", t.priority, -100, 100, 1, 0, [d, id](double v)
            {
                if (AnimGraphTransition* x = anim::Transition(*d, id)) { x->priority = static_cast<int>(v); d->rebuild = true; Touch(*d); }
            }, "Checked first when higher (several transitions can be true)");
            AddCheck(box, "Wait for the end of the animation", t.wait_finished, [d, id](bool on)
            {
                if (AnimGraphTransition* x = anim::Transition(*d, id)) { x->wait_finished = on; d->rebuild = true; Touch(*d); }
            });
            AddNote(box, t.conditions.empty() ? "No condition : always taken." : "Taken when every condition is true.");
            for (size_t i = 0; i < t.conditions.size(); ++i)
                ConditionEditor(doc, box, id, i);
            Rid row = AddButtons(box);
            Rid add = Rwidget_add_child(row, Rbutton_create("Add condition"));
            eui::OnClick(add, [d, id]()
            {
                DocLater(d, [id](Doc& x)
                {
                    if (AnimGraphTransition* tt = anim::Transition(x, id))
                    {
                        AnimGraphCondition c;
                        const auto floats = anim::ParamNames(x.anim, "float");
                        if (!floats.empty()) c.param = floats.front();
                        tt->conditions.push_back(c);
                    }
                    x.rebuild = x.details_dirty = true;
                    Touch(x);
                });
            });
            Rid del = Rwidget_add_child(row, Rbutton_create("Delete"));
            eui::OnClick(del, [d, id]() { DocLater(d, [id](Doc& x) { anim::DeleteTransition(x, id); }); });
        }

        static void StateDetails(Doc& doc, AnimGraphState& s)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            const int id = s.id;
            AnimGraphStateMachine* m = anim::Machine(doc);
            AddSection(box, "State");
            AddText(box, "Name", s.name, [d, id](const std::string& text)
            {
                AnimGraphStateMachine* mm = anim::Machine(*d);
                AnimGraphState* x = anim::State(*d, id);
                if (!mm || !x || text.empty() || text == x->name)
                    return;
                if (mm->FindState(text)) { Status(*d, "A state is already named " + text); return; }
                mm->RenameState(x->name, text);
                d->rebuild = true;
                Touch(*d);
            });
            std::vector<std::string> motions = anim::MotionNames(doc.anim);
            motions.insert(motions.begin(), "");
            AddCombo(box, "Pose", motions, s.motion, [d, id](const std::string& v)
            {
                if (AnimGraphState* x = anim::State(*d, id)) { x->motion = v; d->rebuild = true; Touch(*d); }
            }, "The animation or blend space it plays (double click the state : its pose graph)");
            AddCheck(box, "Interruptible", s.interruptible, [d, id](bool on) { if (AnimGraphState* x = anim::State(*d, id)) { x->interruptible = on; Touch(*d); } },
                     "Off : the transitions wait for the end of its animation");
            AddCheck(box, "Restart on enter", s.restart_on_enter, [d, id](bool on) { if (AnimGraphState* x = anim::State(*d, id)) { x->restart_on_enter = on; Touch(*d); } });
            std::vector<std::string> states = { "" };
            if (m)
                for (const auto& o : m->states) if (o.id != id) states.push_back(o.name);
            AddCombo(box, "Then", states, s.next, [d, id](const std::string& v) { if (AnimGraphState* x = anim::State(*d, id)) { x->next = v; Touch(*d); } },
                     "Played at the end of the animation when no transition is taken");
            AddText(box, "On enter", s.on_enter, [d, id](const std::string& v) { if (AnimGraphState* x = anim::State(*d, id)) { x->on_enter = v; Touch(*d); } },
                    "JavaScript function of the actor called when the state starts");
            AddText(box, "On exit", s.on_exit, [d, id](const std::string& v) { if (AnimGraphState* x = anim::State(*d, id)) { x->on_exit = v; Touch(*d); } });
            if (m && m->entry_state != s.name)
                AddButton(box, "Set as the entry state", [d, id]()
                {
                    DocLater(d, [id](Doc& x)
                    {
                        AnimGraphState* st = anim::State(x, id);
                        if (AnimGraphStateMachine* mm = anim::Machine(x); st && mm) { mm->entry_state = st->name; x.rebuild = x.details_dirty = true; Touch(x); }
                    });
                });
            Rid row = AddButtons(box);
            const int machine = doc.machine;
            Rid open = Rwidget_add_child(row, Rbutton_create("Open its pose"));
            eui::OnClick(open, [d, machine, id]() { DocLater(d, [machine, id](Doc& x) { anim::Open(x, machine, id); }); });
            Rid del = Rwidget_add_child(row, Rbutton_create("Delete"));
            eui::OnClick(del, [d, id]() { DocLater(d, [id](Doc& x) { anim::DeleteState(x, id); }); });
        }

        static void AnimationDetails(Doc& doc, const std::string& name)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            auto shared = std::make_shared<std::string>(name);
            auto find = [d, shared]() -> AnimGraphAnimation* { for (auto& a : d->anim.animations) if (a.name == *shared) return &a; return nullptr; };
            AnimGraphAnimation* a = find();
            if (!a)
                return;
            AddSection(box, "Animation");
            AddText(box, "Name", a->name, [d, find, shared](const std::string& text)
            {
                if (!find() || text.empty() || text == *shared) return;
                if (d->anim.IsMotion(text) || d->anim.FindStateMachine(text)) { Status(*d, "The name " + text + " is used"); return; }
                d->anim.RenameMotion(*shared, text);
                *shared = text;
                d->sel.name = text;
                d->rebuild = d->left_dirty = true;
                Touch(*d);
            });
            AddAsset(box, "Texture", a->texture, [d, find](const std::string& v) { if (auto* x = find()) { x->texture = v; Touch(*d); } });
            AddNumber(box, "Frames", a->frames, 1, 512, 1, 0, [d, find](double v) { if (auto* x = find()) { x->frames = static_cast<int>(v); Touch(*d); } },
                      "Frames of the sheet, side by side");
            AddNumber(box, "Frame time", a->frame_time, 0.005, 10, 0.01, 3, [d, find](double v) { if (auto* x = find()) { x->frame_time = static_cast<float>(v); Touch(*d); } },
                      "Seconds per frame");
            AddCheck(box, "Loop", a->loop, [d, find](bool on) { if (auto* x = find()) { x->loop = on; Touch(*d); } });
            AddSection(box, "Notifies");
            for (size_t i = 0; i < a->notifies.size(); ++i)
            {
                auto notify = [find, i]() -> AnimGraphNotify* { auto* x = find(); return x && i < x->notifies.size() ? &x->notifies[i] : nullptr; };
                AddNumber(box, "Frame", a->notifies[i].frame, 1, 512, 1, 0, [d, notify](double v) { if (auto* n = notify()) { n->frame = static_cast<int>(v); Touch(*d); } });
                AddText(box, "Event", a->notifies[i].event, [d, notify](const std::string& v) { if (auto* n = notify()) { n->event = v; Touch(*d); } },
                        "JavaScript function of the actor called at that frame");
                AddButton(box, "Remove the notify", [d, find, i]()
                {
                    DocLater(d, [find, i](Doc& x) { if (auto* aa = find(); aa && i < aa->notifies.size()) aa->notifies.erase(aa->notifies.begin() + static_cast<std::ptrdiff_t>(i)); x.details_dirty = true; Touch(x); });
                });
            }
            Rid row = AddButtons(box);
            Rid add = Rwidget_add_child(row, Rbutton_create("Add notify"));
            eui::OnClick(add, [d, find]() { DocLater(d, [find](Doc& x) { if (auto* aa = find()) aa->notifies.push_back({}); x.details_dirty = true; Touch(x); }); });
            Rid del = Rwidget_add_child(row, Rbutton_create("Delete"));
            eui::OnClick(del, [d, shared]()
            {
                DocLater(d, [shared](Doc& x)
                {
                    auto& v = x.anim.animations;
                    v.erase(std::remove_if(v.begin(), v.end(), [&](const AnimGraphAnimation& a2) { return a2.name == *shared; }), v.end());
                    x.sel = {};
                    x.rebuild = x.details_dirty = x.left_dirty = true;
                    Touch(x);
                });
            });
        }

        static void BlendDetails(Doc& doc, const std::string& name)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            auto shared = std::make_shared<std::string>(name);
            auto find = [d, shared]() -> AnimGraphBlendSpace* { for (auto& b : d->anim.blend_spaces) if (b.name == *shared) return &b; return nullptr; };
            AnimGraphBlendSpace* b = find();
            if (!b)
                return;
            AddSection(box, "Blend Space");
            AddText(box, "Name", b->name, [d, find, shared](const std::string& text)
            {
                if (!find() || text.empty() || text == *shared) return;
                if (d->anim.IsMotion(text) || d->anim.FindStateMachine(text)) { Status(*d, "The name " + text + " is used"); return; }
                d->anim.RenameMotion(*shared, text);
                *shared = text;
                d->sel.name = text;
                d->rebuild = d->left_dirty = true;
                Touch(*d);
            });
            std::vector<std::string> floats = anim::ParamNames(doc.anim, "float");
            floats.insert(floats.begin(), "");
            AddCombo(box, "Variable", floats, b->variable, [d, find](const std::string& v) { if (auto* x = find()) { x->variable = v; Touch(*d); } },
                     "The float parameter that chooses the animation");
            AddSection(box, "Samples");
            std::vector<std::string> anims;
            for (const auto& a : doc.anim.animations) anims.push_back(a.name);
            for (size_t i = 0; i < b->samples.size(); ++i)
            {
                auto sample = [find, i]() -> AnimGraphSample* { auto* x = find(); return x && i < x->samples.size() ? &x->samples[i] : nullptr; };
                AddNumber(box, "Position", b->samples[i].position, -1e6, 1e6, 0.1, 2, [d, sample](double v) { if (auto* s = sample()) { s->position = static_cast<float>(v); Touch(*d); } });
                AddCombo(box, "Animation", anims, b->samples[i].animation, [d, sample](const std::string& v) { if (auto* s = sample()) { s->animation = v; Touch(*d); } });
                AddButton(box, "Remove the sample", [d, find, i]()
                {
                    DocLater(d, [find, i](Doc& x) { if (auto* bb = find(); bb && i < bb->samples.size()) bb->samples.erase(bb->samples.begin() + static_cast<std::ptrdiff_t>(i)); x.details_dirty = true; Touch(x); });
                });
            }
            Rid row = AddButtons(box);
            Rid add = Rwidget_add_child(row, Rbutton_create("Add sample"));
            eui::OnClick(add, [d, find]()
            {
                DocLater(d, [find](Doc& x)
                {
                    if (auto* bb = find())
                    {
                        AnimGraphSample s;
                        s.position = bb->samples.empty() ? 0.f : bb->samples.back().position + 1.f;
                        if (!x.anim.animations.empty()) s.animation = x.anim.animations.front().name;
                        bb->samples.push_back(s);
                    }
                    x.details_dirty = true;
                    Touch(x);
                });
            });
            Rid del = Rwidget_add_child(row, Rbutton_create("Delete"));
            eui::OnClick(del, [d, shared]()
            {
                DocLater(d, [shared](Doc& x)
                {
                    auto& v = x.anim.blend_spaces;
                    v.erase(std::remove_if(v.begin(), v.end(), [&](const AnimGraphBlendSpace& b2) { return b2.name == *shared; }), v.end());
                    x.sel = {};
                    x.rebuild = x.details_dirty = x.left_dirty = true;
                    Touch(x);
                });
            });
        }

        static void ParamDetails(Doc& doc, const std::string& name)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            auto shared = std::make_shared<std::string>(name);
            auto find = [d, shared]() -> AnimGraphParameter* { for (auto& p : d->anim.parameters) if (p.name == *shared) return &p; return nullptr; };
            AnimGraphParameter* p = find();
            if (!p)
                return;
            AddSection(box, "Parameter");
            AddText(box, "Name", p->name, [d, find, shared](const std::string& text)
            {
                AnimGraphParameter* x = find();
                if (!x || text.empty() || text == *shared) return;
                if (d->anim.FindParameter(text)) { Status(*d, "A parameter is already named " + text); return; }
                // Conditions and blend spaces follow.
                for (auto& m : d->anim.state_machines)
                    for (auto& t : m.transitions)
                        for (auto& c : t.conditions)
                            if (c.param == *shared) c.param = text;
                for (auto& b : d->anim.blend_spaces)
                    if (b.variable == *shared) b.variable = text;
                x->name = text;
                *shared = text;
                d->sel.name = text;
                d->rebuild = d->left_dirty = true;
                Touch(*d);
            });
            AddCombo(box, "Type", { "float", "bool", "int", "trigger" }, p->type, [d, find](const std::string& v)
            {
                if (auto* x = find()) { x->type = v; d->left_dirty = true; Touch(*d); }
            });
            AddNumber(box, "Default", p->default_value, -1e6, 1e6, 0.1, 2, [d, find](double v) { if (auto* x = find()) { x->default_value = static_cast<float>(v); Touch(*d); } });
            AddNote(box, "From the game : sprite.SetFloat(\"" + p->name + "\", 3) / SetBool / SetTrigger.");
            AddButton(box, "Delete", [d, shared]()
            {
                DocLater(d, [shared](Doc& x)
                {
                    auto& v = x.anim.parameters;
                    v.erase(std::remove_if(v.begin(), v.end(), [&](const AnimGraphParameter& q) { return q.name == *shared; }), v.end());
                    x.sel = {};
                    x.details_dirty = x.left_dirty = true;
                    Touch(x);
                });
            });
        }

        static void MachineDetails(Doc& doc, int id)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            AnimGraphStateMachine* m = doc.anim.FindStateMachine(id);
            if (!m)
                return;
            AddSection(box, "State Machine");
            AddText(box, "Name", m->name, [d, id](const std::string& text)
            {
                AnimGraphStateMachine* x = d->anim.FindStateMachine(id);
                if (!x || text.empty() || text == x->name) return;
                if (d->anim.IsMotion(text) || d->anim.FindStateMachine(text)) { Status(*d, "The name " + text + " is used"); return; }
                d->anim.RenameStateMachine(x->name, text);
                d->rebuild = d->left_dirty = true;
                Touch(*d);
            });
            std::vector<std::string> states = { "" };
            for (const auto& s : m->states) states.push_back(s.name);
            AddCombo(box, "Entry state", states, m->entry_state, [d, id](const std::string& v)
            {
                if (AnimGraphStateMachine* x = d->anim.FindStateMachine(id)) { x->entry_state = v; d->rebuild = true; Touch(*d); }
            });
            AddNote(box, std::to_string(m->states.size()) + " state(s), " + std::to_string(m->transitions.size()) + " transition(s).");
            Rid row = AddButtons(box);
            Rid open = Rwidget_add_child(row, Rbutton_create("Open"));
            eui::OnClick(open, [d, id]() { DocLater(d, [id](Doc& x) { anim::Open(x, id, 0); }); });
            Rid del = Rwidget_add_child(row, Rbutton_create("Delete"));
            eui::OnClick(del, [d, id]()
            {
                DocLater(d, [id](Doc& x)
                {
                    AnimGraphStateMachine* mm = x.anim.FindStateMachine(id);
                    if (!mm) return;
                    const std::string name = mm->name;
                    auto& v = x.anim.state_machines;
                    v.erase(std::remove_if(v.begin(), v.end(), [id](const AnimGraphStateMachine& q) { return q.id == id; }), v.end());
                    for (auto it = x.anim.pose_nodes.begin(); it != x.anim.pose_nodes.end();)
                    {
                        if (it->type == "statemachine" && it->source == name)
                        {
                            if (x.anim.output_source == it->id) x.anim.output_source = 0;
                            it = x.anim.pose_nodes.erase(it);
                        }
                        else ++it;
                    }
                    if (x.machine == id) x.machine = x.state = 0;
                    x.sel = {};
                    x.rebuild = x.details_dirty = x.left_dirty = x.frame = true;
                    Touch(x);
                });
            });
        }

        static void PoseDetails(Doc& doc, int id)
        {
            Rid box = doc.details;
            Doc* d = &doc;
            AnimGraphPoseNode* n = doc.anim.FindPoseNode(id);
            if (!n)
                return;
            AddSection(box, anim::PoseTitle(*n));
            std::vector<std::string> sources;
            if (n->type == "animation") for (const auto& a : doc.anim.animations) sources.push_back(a.name);
            else if (n->type == "blendspace") for (const auto& b : doc.anim.blend_spaces) sources.push_back(b.name);
            else for (const auto& m : doc.anim.state_machines) sources.push_back(m.name);
            AddCombo(box, "Source", sources, n->source, [d, id](const std::string& v)
            {
                if (AnimGraphPoseNode* x = d->anim.FindPoseNode(id)) { x->source = v; d->rebuild = true; Touch(*d); }
            });
            AddNote(box, doc.anim.output_source == id ? "Plugged into Output Pose : this is what plays."
                                                      : "Not plugged : drag its pin onto Output Pose.");
        }

        static void Rebuild(Doc& doc)
        {
            doc.details_dirty = false;
            ClearBox(doc.details);
            const Selection s = doc.sel;
            if (doc.kind == Kind::Tree)
            {
                if (s.kind == SelKind::BtNode)
                {
                    if (BTNodeDesc* n = doc.bt.Find(s.id)) { TreeNodeDetails(doc, *n); return; }
                }
                else if (s.kind == SelKind::BtAux)
                {
                    if (BTNodeDesc* n = doc.bt.Find(s.id)) { AuxDetails(doc, *n, s.aux); return; }
                }
                else if (s.kind == SelKind::BtKey)
                {
                    KeyDetails(doc, s.name);
                    return;
                }
                AddSection(doc.details, "Behavior Tree");
                AddNote(doc.details, "Select a node to edit it. Right click the canvas : add a node. Drag the pin of a node "
                                     "onto another : it becomes its child (children run top to bottom, the badge is their order).");
                return;
            }
            switch (s.kind)
            {
            case SelKind::State: if (AnimGraphState* st = anim::State(doc, s.id)) StateDetails(doc, *st); return;
            case SelKind::Transition: if (AnimGraphTransition* t = anim::Transition(doc, s.id)) TransitionDetails(doc, *t); return;
            case SelKind::Pose: PoseDetails(doc, s.id); return;
            case SelKind::Param: ParamDetails(doc, s.name); return;
            case SelKind::Anim: AnimationDetails(doc, s.name); return;
            case SelKind::Blend: BlendDetails(doc, s.name); return;
            case SelKind::Machine: MachineDetails(doc, s.id); return;
            case SelKind::Motion:
                if (AnimGraphState* st = anim::State(doc, s.id))
                {
                    if (doc.anim.IsAnimation(st->motion)) { AnimationDetails(doc, st->motion); return; }
                    if (doc.anim.IsBlendSpace(st->motion)) { BlendDetails(doc, st->motion); return; }
                }
                break;
            case SelKind::Entry: AddSection(doc.details, "Entry"); AddNote(doc.details, "Drag its pin onto the state the machine starts in."); return;
            case SelKind::Any: AddSection(doc.details, "Any State"); AddNote(doc.details, "Drag its pin onto a state : a transition taken from any state (Hurt, Die...)."); return;
            case SelKind::Output: AddSection(doc.details, "Output Pose"); AddNote(doc.details, "What the graph plays : plug a pose into it."); return;
            default: break;
            }
            AddSection(doc.details, "Anim Graph");
            AddNote(doc.details, doc.machine == 0
                ? "The pose plugged into Output Pose plays. Right click : add an Animation, a Blend Space or a State Machine (double click it to open it)."
                : doc.state == 0
                    ? "Right click : add a state. Drag a state onto another : a transition (select it for its conditions). Double click a state : its pose."
                    : "The pose of the state : right click to choose its animation or blend space.");
        }
    }

    // =============================================================================
    // Left panel
    // =============================================================================

    static void LeftList(Doc& doc, const std::string& title, std::function<void()> add, const std::string& add_tip,
                         const std::vector<std::pair<std::string, Selection>>& rows, bool live = false)
    {
        Rid head = Rwidget_add_child(doc.left, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(head, 4.f);
        Rid label = Rwidget_add_child(head, Rlabel_create(title.c_str()));
        Rwidget_add_class(label, "section");
        Rslot_set_fill(label, 1.f);
        Rslot_set_valign(label, R_VALIGN_CENTER);
        if (add)
        {
            Rid plus = Rwidget_add_child(head, Rbutton_create("+"));
            Rwidget_add_class(plus, "tool");
            eui::SetTooltip(plus, add_tip);
            eui::OnClick(plus, std::move(add));
        }
        Rid list = Rwidget_add_child(doc.left, Rwidget_create(R_WIDGET_TREE_VIEW));
        Rwidget_set_size(list, -1.f, std::min(240.f, 26.f * static_cast<float>(std::max<size_t>(rows.size(), 1)) + 8.f));
        for (const auto& [text, sel] : rows)
        {
            Rid item = Rtree_view_add_item(list, R_INVALID_ID, text.c_str());
            doc.left_rows[item] = sel;
            if (live)
                doc.live_rows[item] = sel.name;
        }
        Doc* d = &doc;
        eui::OnTreeSelect(list, [d](Rid item)
        {
            const auto it = d->left_rows.find(item);
            if (it == d->left_rows.end())
                return;
            d->sel = it->second;
            d->details_dirty = true;
            Rnode_graph_clear_selection(d->graph);
        });
        eui::OnTreeActivate(list, [d](Rid item)
        {
            const auto it = d->left_rows.find(item);
            if (it != d->left_rows.end() && it->second.kind == SelKind::Machine)
            {
                const int id = it->second.id;
                DocLater(d, [id](Doc& x) { anim::Open(x, id, 0); });
            }
        });
    }

    static void RebuildLeft(Doc& doc)
    {
        doc.left_dirty = false;
        ClearBox(doc.left);
        doc.left_rows.clear();
        doc.live_rows.clear();
        Doc* d = &doc;
        if (doc.kind == Kind::Tree)
        {
            std::vector<std::pair<std::string, Selection>> rows;
            for (const auto& k : doc.bt.keys)
                rows.push_back({ k.name + "  (" + k.type + ")", { SelKind::BtKey, 0, 0, k.name } });
            LeftList(doc, "Blackboard", [d]()
            {
                DocLater(d, [](Doc& x)
                {
                    std::vector<std::string> used;
                    for (const auto& k : x.bt.keys) used.push_back(k.name);
                    lynx::BTKeyDesc k;
                    k.name = anim::UniqueName(used, "key");
                    x.bt.keys.push_back(k);
                    x.sel = { SelKind::BtKey, 0, 0, k.name };
                    x.details_dirty = x.left_dirty = true;
                    Touch(x);
                });
            }, "Add a key", rows, true);
            AddNote(doc.left, "Memory of the AI : the decorators read it, the tasks and services write it.");
            return;
        }

        std::vector<std::pair<std::string, Selection>> params;
        for (const auto& p : doc.anim.parameters)
            params.push_back({ p.name + "  (" + p.type + ")", { SelKind::Param, 0, 0, p.name } });
        LeftList(doc, "Parameters", [d]()
        {
            DocLater(d, [](Doc& x)
            {
                lynx::AnimGraphParameter p;
                p.name = anim::UniqueName(anim::ParamNames(x.anim), "param");
                x.anim.parameters.push_back(p);
                x.sel = { SelKind::Param, 0, 0, p.name };
                x.details_dirty = x.left_dirty = true;
                Touch(x);
            });
        }, "Add a parameter (speed, airborne, hurt...)", params, true);

        std::vector<std::pair<std::string, Selection>> anims;
        for (const auto& a : doc.anim.animations)
            anims.push_back({ a.name, { SelKind::Anim, 0, 0, a.name } });
        LeftList(doc, "Animations", [d]()
        {
            DocLater(d, [](Doc& x)
            {
                lynx::AnimGraphAnimation a;
                a.name = anim::UniqueName(anim::AllNames(x.anim), "Animation");
                x.anim.animations.push_back(a);
                x.sel = { SelKind::Anim, 0, 0, a.name };
                x.details_dirty = x.left_dirty = true;
                Touch(x);
            });
        }, "Add an animation (a sprite sheet)", anims);

        std::vector<std::pair<std::string, Selection>> blends;
        for (const auto& b : doc.anim.blend_spaces)
            blends.push_back({ b.name, { SelKind::Blend, 0, 0, b.name } });
        LeftList(doc, "Blend Spaces", [d]()
        {
            DocLater(d, [](Doc& x)
            {
                lynx::AnimGraphBlendSpace b;
                b.name = anim::UniqueName(anim::AllNames(x.anim), "BlendSpace");
                const auto floats = anim::ParamNames(x.anim, "float");
                if (!floats.empty()) b.variable = floats.front();
                x.anim.blend_spaces.push_back(b);
                x.sel = { SelKind::Blend, 0, 0, b.name };
                x.details_dirty = x.left_dirty = true;
                Touch(x);
            });
        }, "Add a blend space (a float parameter chooses the animation)", blends);

        std::vector<std::pair<std::string, Selection>> machines;
        for (const auto& m : doc.anim.state_machines)
            machines.push_back({ m.name, { SelKind::Machine, m.id, 0, {} } });
        LeftList(doc, "State Machines", [d]()
        {
            DocLater(d, [](Doc& x)
            {
                const int id = anim::NewMachine(x).id;
                x.sel = { SelKind::Machine, id, 0, {} };
                x.details_dirty = true;
            });
        }, "Add a state machine (double click it to open it)", machines);
    }

    // =============================================================================
    // Live debug (while the game runs)
    // =============================================================================

    static void SetActive(Doc& doc, const std::set<int>& nodes)
    {
        for (int n : doc.active)
            if (!nodes.count(n) && doc.items.count(n))
                Rnode_graph_set_node_active(doc.graph, n, R_FALSE);
        for (int n : nodes)
            if (!doc.active.count(n))
                Rnode_graph_set_node_active(doc.graph, n, R_TRUE);
        doc.active = nodes;
    }

    static void UpdateDebug(Doc& doc)
    {
        const double now = Rget_time();
        if (now - doc.debug_time < 0.15)
            return;
        doc.debug_time = now;
        const std::string asset = GAssetPathOf(doc.path);
        std::set<int> active;
        std::string live;
        size_t count = 0;

        if (doc.kind == Kind::Tree)
        {
            std::vector<lynx::BehaviorTreeComponent*> running = asset.empty() ? std::vector<lynx::BehaviorTreeComponent*>{}
                                                                               : lynx::BehaviorTreeComponent::GetRunning(asset);
            count = running.size();
            lynx::BehaviorTreeComponent* c = nullptr;
            if (!running.empty())
            {
                doc.debug_index = std::clamp(doc.debug_index, 0, static_cast<int>(running.size()) - 1);
                c = running[static_cast<size_t>(doc.debug_index)];
            }
            if (c)
            {
                for (int id : c->GetActiveNodeIds())
                    if (int node = NodeOf(doc, ItemKind::BtNode, id))
                        active.insert(node);
                live = "Live";
                const lynx::Blackboard& bb = c->GetBlackboard();
                for (const auto& [row, key] : doc.live_rows)
                {
                    const lynx::BlackboardValue* v = bb.Get(key);
                    Rtree_item_set_text(row, (key + " = " + (v ? v->ToString() : std::string("(not set)"))).c_str());
                }
            }
        }
        else
        {
            std::vector<lynx::AnimationSpriteComponent*> running = asset.empty() ? std::vector<lynx::AnimationSpriteComponent*>{}
                                                                                  : lynx::AnimationSpriteComponent::GetWithGraph(asset);
            count = running.size();
            lynx::AnimationSpriteComponent* c = nullptr;
            if (!running.empty())
            {
                doc.debug_index = std::clamp(doc.debug_index, 0, static_cast<int>(running.size()) - 1);
                c = running[static_cast<size_t>(doc.debug_index)];
            }
            if (c)
            {
                const std::string state = c->GetCurrentState();
                live = "Playing : " + (state.empty() ? std::string("-") : state);
                for (const auto& [node, item] : doc.items)
                {
                    if (item.kind == ItemKind::State)
                        if (const lynx::AnimGraphState* s = anim::State(doc, item.id); s && s->name == state)
                            active.insert(node);
                    if (item.kind == ItemKind::Pose && item.id == doc.anim.output_source)
                        active.insert(node);
                    if (item.kind == ItemKind::Output && (doc.machine == 0 || (doc.state && anim::State(doc, doc.state) &&
                                                                               anim::State(doc, doc.state)->name == state)))
                        active.insert(node);
                    if (item.kind == ItemKind::Motion && anim::State(doc, item.id) && anim::State(doc, item.id)->name == state)
                        active.insert(node);
                }
                for (const auto& [row, name] : doc.live_rows)
                {
                    const lynx::AnimGraphParameter* p = doc.anim.FindParameter(name);
                    std::string value;
                    if (p && p->type == "bool")
                        value = c->GetBool(name) ? "true" : "false";
                    else
                        value = Num(c->GetFloat(name));
                    Rtree_item_set_text(row, (name + " = " + value).c_str());
                }
            }
        }

        // Back to the plain rows when the game stops.
        if (count == 0 && doc.debug_count != 0)
            doc.left_dirty = true;
        if (count != doc.debug_count)
        {
            doc.debug_count = count;
            Rcombo_box_clear_items(doc.pick);
            for (size_t i = 0; i < count; ++i)
                Rcombo_box_add_item(doc.pick, ("Instance " + std::to_string(i + 1)).c_str());
            Rcombo_box_set_selected(doc.pick, doc.debug_index);
        }
        eui::SetVisible(doc.pick, count > 1);
        eui::SetText(doc.live, live);
        eui::SetVisible(doc.live, !live.empty());
        SetActive(doc, active);
    }

    // =============================================================================
    // The panel
    // =============================================================================

    static const char kPanelXml[] =
        "<HBox Spacing='0'>"
        "  <ScrollBox Width='240'><VBox Name='ge.left' Spacing='4' Slot.Padding='6'/></ScrollBox>"
        "  <VBox Spacing='4' Slot.Fill='1' Slot.Padding='4'>"
        "    <HBox Spacing='6'>"
        "      <HBox Name='ge.crumbs' Spacing='2' Slot.VAlign='Center'/>"
        "      <Spacer Slot.Fill='1'/>"
        "      <Label Name='ge.live' Text='' Class='success' Slot.VAlign='Center' Visibility='Collapsed'/>"
        "      <ComboBox Name='ge.pick' Width='140' Visibility='Collapsed' Tooltip='Which running actor is shown'/>"
        "      <Button Name='ge.frame' Text='Frame' Class='tool' Tooltip='Show every node (F)'/>"
        "      <Button Name='ge.undo' Text='Undo' Class='tool' Tooltip='Ctrl+Z'/>"
        "      <Button Name='ge.redo' Text='Redo' Class='tool' Tooltip='Ctrl+Y'/>"
        "      <Button Name='ge.save' Text='Save' Style='Primary' Tooltip='Ctrl+S'/>"
        "    </HBox>"
        "    <NodeGraph Name='ge.graph' Slot.Fill='1'/>"
        "    <Label Name='ge.status' Text='' Class='dim' Wrap='true'/>"
        "  </VBox>"
        "  <ScrollBox Width='330'><VBox Name='ge.details' Spacing='5' Slot.Padding='8'/></ScrollBox>"
        "</HBox>";

    static void BuildPanel(Doc& doc)
    {
        doc.panel = Rwidget_create_from_xml(kPanelXml, sizeof(kPanelXml) - 1);
        if (doc.panel == R_INVALID_ID)
            return;
        auto find = [&](const char* name) { return eui::Find(doc.panel, name); };
        doc.graph = find("ge.graph");
        doc.left = find("ge.left");
        doc.details = find("ge.details");
        doc.crumbs = find("ge.crumbs");
        doc.status = find("ge.status");
        doc.live = find("ge.live");
        doc.pick = find("ge.pick");
        Doc* d = &doc;

        Rnode_graph_set_type_color(doc.graph, "bt", 0.94f, 0.63f, 0.16f);
        Rnode_graph_set_type_color(doc.graph, "flow", 0.55f, 0.75f, 1.0f);
        Rnode_graph_set_type_color(doc.graph, "pose", 0.47f, 0.82f, 0.52f);
        Rnode_graph_set_connect_callback(doc.graph, OnConnect, nullptr);
        Rnode_graph_set_linked_callback(doc.graph, OnLinked, nullptr);
        Rnode_graph_set_unlinked_callback(doc.graph, OnUnlinked, nullptr);
        Rnode_graph_set_moved_callback(doc.graph, OnMoved, nullptr);
        Rnode_graph_set_selection_callback(doc.graph, OnSelection, nullptr);
        Rnode_graph_set_removed_callback(doc.graph, OnRemoved, nullptr);
        Rnode_graph_set_activated_callback(doc.graph, OnActivated, nullptr);
        Rnode_graph_set_context_callback(doc.graph, OnContext, nullptr);

        eui::OnClick(find("ge.save"), [d]() { Commit(*d); SaveDoc(*d); });
        eui::OnClick(find("ge.undo"), [d]() { DocLater(d, [](Doc& x) { Undo(x); }); });
        eui::OnClick(find("ge.redo"), [d]() { DocLater(d, [](Doc& x) { Redo(x); }); });
        eui::OnClick(find("ge.frame"), [d]() { Rnode_graph_frame_all(d->graph); });
        eui::OnSelect(doc.pick, [d](int index) { d->debug_index = std::max(0, index); d->debug_time = 0.0; });

        // Shortcuts of the editor (the level editor waits : HasFocus).
        eui::OnEvent(doc.panel, [d](const REvent& e)
        {
            if (e.type != R_EVENT_KEY_DOWN)
                return false;
            const bool ctrl = (e.modifiers & R_MOD_CTRL) != 0;
            if (ctrl && e.key == R_KEY_S) { Commit(*d); SaveDoc(*d); return true; }
            if (ctrl && e.key == R_KEY_Z) { DocLater(d, [](Doc& x) { Undo(x); }); return true; }
            if (ctrl && e.key == R_KEY_Y) { DocLater(d, [](Doc& x) { Redo(x); }); return true; }
            if (ctrl && e.key == R_KEY_D)
            {
                DocLater(d, [](Doc& x) { if (x.kind == Kind::Tree) tree::Duplicate(x); else anim::Duplicate(x); });
                return true;
            }
            if ((e.modifiers & R_MOD_ALT) && e.key == R_KEY_UP && d->kind == Kind::Anim)
            {
                DocLater(d, [](Doc& x) { anim::GoUp(x); });
                return true;
            }
            return false;
        });

        const std::string title = doc.path.filename().string();
        Rdock_area_add_panel(g.dock, doc.panel, title.c_str(), R_DOCK_TAB, g.viewport_panel, 0.f);
        float uv[4];
        eui::FileIconRegion(doc.kind == Kind::Tree ? lynx::editor::file_icons::Kind::BehaviorTree : lynx::editor::file_icons::Kind::AnimGraph, uv);
        Rdock_area_set_panel_icon(g.dock, doc.panel, eui::FileIconTexture(), uv[0], uv[1], uv[2], uv[3], R_FALSE);
        doc.shown_title = title;
        Rdock_area_show_panel(g.dock, doc.panel);
    }

    static void CloseDoc(Doc* doc)
    {
        const auto it = std::find_if(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
        if (it == g_docs.end())
            return;
        const Rid panel = doc->panel;
        g_docs.erase(it);   // (the callbacks of the graph find no document any more)
        if (panel != R_INVALID_ID && Rwidget_is_valid(panel))
            Rdock_area_close_panel(g.dock, panel);
    }

    // The x of the tab : asks first when there are changes.
    static bool OnPanelClose(Rid panel)
    {
        Doc* doc = nullptr;
        for (auto& d : g_docs)
            if (d->panel == panel)
                doc = d.get();
        if (!doc)
            return true;
        Commit(*doc);
        if (!doc->dirty)
        {
            DocLater(doc, [](Doc& x) { CloseDoc(&x); });
            return false;
        }
        eui::Ask(g.window, "Unsaved changes", doc->path.filename().string() + " was changed. Save it before closing ?",
                 { "Save", "Don't save", "Cancel" }, [doc](int choice)
        {
            if (!Alive(doc) || choice == 2 || choice < 0)
                return;
            if (choice == 0 && !SaveDoc(*doc))
                return;
            CloseDoc(doc);
        });
        return false;
    }

    static void CheckDisk(Doc& doc)
    {
        const double now = Rget_time();
        if (now - doc.disk_check < 1.0)
            return;
        doc.disk_check = now;
        std::error_code ec;
        const auto time = sfs::last_write_time(doc.path, ec);
        if (ec || time == doc.write_time)
            return;
        doc.write_time = time;
        if (doc.dirty || doc.touched)
        {
            doc.changed_on_disk = true;
            Status(doc, "The file was changed outside of the editor : saving will replace it.");
        }
        else
        {
            LoadDoc(doc);
            Status(doc, "Reloaded (changed on disk).");
        }
    }

    // A transition that lost a wire goes (state machine).
    static void CheckTransitions(Doc& doc)
    {
        doc.check_transitions = false;
        std::vector<int> broken;
        for (const auto& [node, item] : doc.items)
        {
            if (item.kind != ItemKind::Transition)
                continue;
            const int in = Rnode_graph_get_input(doc.graph, node, 0);
            const int out = Rnode_graph_get_output(doc.graph, node, 0);
            bool has_out = false;
            for (int i = 0; i < Rnode_graph_get_link_count(doc.graph); ++i)
                if (Rnode_graph_get_link_output(doc.graph, Rnode_graph_get_link(doc.graph, i)) == out)
                    has_out = true;
            if (!Rnode_graph_get_input_link(doc.graph, in) || !has_out)
                broken.push_back(item.id);
        }
        for (int id : broken)
            anim::DeleteTransition(doc, id);
    }

    static void UpdateDoc(Doc& doc)
    {
        if (doc.panel == R_INVALID_ID || !Rwidget_is_valid(doc.panel))
            return;
        if (doc.check_transitions)
            CheckTransitions(doc);
        if (doc.rebuild)
        {
            RebuildCanvas(doc);
            RebuildCrumbs(doc);
        }
        if (doc.left_dirty)
            RebuildLeft(doc);
        if (doc.details_dirty)
            details::Rebuild(doc);
        if (doc.touched)
            Commit(doc);
        CheckDisk(doc);
        UpdateDebug(doc);

        const std::string title = doc.path.filename().string() + (doc.dirty ? " *" : "");
        if (title != doc.shown_title)
        {
            doc.shown_title = title;
            Rdock_area_set_panel_title(g.dock, doc.panel, title.c_str());
        }
        if (Rget_time() - doc.status_time > 6.0 && doc.status_time > 0.0)
        {
            doc.status_time = -100.0;
            eui::SetText(doc.status, "");
        }
    }

    static void Update()
    {
        for (size_t i = 0; i < g_docs.size(); ++i)
            UpdateDoc(*g_docs[i]);
    }
}


// =============================================================================
// lynx::editor::graph_editors (GraphEditors.h)
// =============================================================================

namespace lynx::editor::graph_editors
{
    namespace eg = editor_ui::graphs;

    bool CanOpen(const std::filesystem::path& path)
    {
        const std::string e = eg::GExtension(path);
        return e == ".animgraph" || e == ".bt";
    }

    void Open(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
        for (auto& doc : eg::g_docs)
            if (doc->path == absolute || std::filesystem::equivalent(doc->path, absolute, ec))
            {
                Rdock_area_open_panel(editor_ui::g.dock, doc->panel);
                Rdock_area_show_panel(editor_ui::g.dock, doc->panel);
                return;
            }
        if (editor_ui::g.dock == R_INVALID_ID)
            return;
        auto doc = std::make_unique<eg::Doc>();
        doc->kind = eg::GExtension(absolute) == ".bt" ? eg::Kind::Tree : eg::Kind::Anim;
        doc->path = absolute;
        eg::LoadDoc(*doc);
        eg::g_docs.push_back(std::move(doc));
        eg::BuildPanel(*eg::g_docs.back());
        eg::UpdateDoc(*eg::g_docs.back());
    }

    void DrawAll(unsigned int) { eg::Update(); }

    bool HasFocus()
    {
        const Rid focused = Rwindow_get_focused_widget(editor_ui::g.window);
        for (auto& doc : eg::g_docs)
            if (editor_ui::IsInside(focused, doc->panel))
                return true;
        return false;
    }

    static bool RelativeInside(const std::filesystem::path& path, const std::filesystem::path& folder, std::filesystem::path& relative)
    {
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::weakly_canonical(std::filesystem::absolute(folder, ec), ec);
        if (path == base || std::filesystem::equivalent(path, base, ec))
        {
            relative.clear();
            return true;
        }
        const std::filesystem::path r = path.lexically_relative(base);
        if (r.empty() || *r.begin() == "..")
            return false;
        relative = r;
        return true;
    }

    void PathMoved(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::error_code ec;
        const std::filesystem::path destination = std::filesystem::weakly_canonical(std::filesystem::absolute(to, ec), ec);
        for (auto& doc : eg::g_docs)
        {
            std::filesystem::path relative;
            if (!RelativeInside(doc->path, from, relative))
                continue;
            doc->path = relative.empty() ? destination : destination / relative;
            doc->write_time = std::filesystem::last_write_time(doc->path, ec);
            doc->shown_title.clear();
        }
    }

    void PathDeleted(const std::filesystem::path& target)
    {
        std::vector<eg::Doc*> gone;
        for (auto& doc : eg::g_docs)
        {
            std::filesystem::path relative;
            if (RelativeInside(doc->path, target, relative))
                gone.push_back(doc.get());
        }
        for (eg::Doc* doc : gone)
            eg::CloseDoc(doc);
    }

    bool HasUnsavedChanges()
    {
        return std::any_of(eg::g_docs.begin(), eg::g_docs.end(), [](const auto& d) { return d->dirty || d->touched; });
    }

    void SaveAll()
    {
        for (auto& doc : eg::g_docs)
            if (doc->dirty || doc->touched)
            {
                eg::Commit(*doc);
                eg::SaveDoc(*doc);
            }
    }

    // Locomotion (State Machine) -> Output Pose ; Locomotion : Entry -> Idle.
    std::string NewAnimGraphTemplate()
    {
        lynx::AnimGraphAsset g;
        lynx::AnimGraphParameter speed;
        speed.name = "speed";
        g.parameters.push_back(speed);
        lynx::AnimGraphAnimation idle;
        idle.name = "Idle";
        g.animations.push_back(idle);
        lynx::AnimGraphStateMachine m;
        m.id = 1;
        m.name = "Locomotion";
        m.entry_x = -260.f;
        m.any_x = -260.f;
        m.any_y = 220.f;
        lynx::AnimGraphState state;
        state.id = 2;
        state.name = "Idle";
        state.motion = "Idle";
        state.x = 0.f;
        state.y = 0.f;
        m.states.push_back(state);
        m.entry_state = "Idle";
        g.state_machines.push_back(m);
        lynx::AnimGraphPoseNode node;
        node.id = 3;
        node.type = "statemachine";
        node.source = "Locomotion";
        g.pose_nodes.push_back(node);
        g.output_source = node.id;
        g.output_x = 360.f;
        g.output_y = 0.f;
        return g.SaveToString();
    }

    std::string NewBehaviorTreeTemplate() { return lynx::BehaviorTreeAsset::MakeDefault().SaveToString(); }

    void SetAssetFieldDrawer(std::function<bool(std::string&)>) {}

    void Shutdown()
    {
        eg::g_docs.clear();
        eg::g_menu.reset();
    }
}

namespace editor_ui::graphs
{
    // The dock area asks before a panel closes (the graph editors may have changes).
    static int OnDockClose(Rid, Rid panel, void*)
    {
        if (widgets::OwnsPanel(panel))
            return widgets::OnPanelClose(panel) ? R_TRUE : R_FALSE;
        return OnPanelClose(panel) ? R_TRUE : R_FALSE;
    }

    static void Init()
    {
        Rdock_area_set_close_callback(g.dock, OnDockClose, nullptr);
    }
}
