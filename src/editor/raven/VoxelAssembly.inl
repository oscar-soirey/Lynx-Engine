// =============================================================================
// Voxel Assembly editor in Raven (.vasm files)
// (included by raven/EditorUi.inl ; API : ../VoxelAssemblyEditor.h ;
//  the blocks : ../../vasm/VasmKinds.h ; the runtime : ../../vasm)
// -----------------------------------------------------------------------------
// One docked tab per open file, next to the Viewport (like a Blueprint) :
//
//   +--------------------+---------------------------------------------------+
//   | class [Hero]        | Save Undo Redo   Event Graph            status   |
//   | extends [Humanoid]  |---------+-----------------------------------------|
//   | GRAPHS          [+] | palette |                                         |
//   |  Event Graph        | Events  |   when the game starts                  |
//   | FUNCTIONS       [+] | Control |   forever                               |
//   |  Take Damage        | Motion  |     walk (axis MoveRight)               |
//   | VARIABLES       [+] | ...     |                                         |
//   |  score : Number     |         |                                         |
//   |--------------------|         |                                         |
//   | details of the item |         |                                         |
//   +--------------------+---------+-----------------------------------------+
//
// - Several event graphs ; each function has its own graph, which starts with
//   "when called" ; its inputs are blocks of its palette ; "return" ends it.
// - The canvas : left click selects (Ctrl : adds), left drag on the canvas
//   draws a selection rectangle, right drag pans, right click : the menu.
// - Menus in the blocks : the actions and axes of Input Settings, the
//   variables of the file.
// - Save : the .vasm only ; the engine reads it (vasm::PrepareClasses) : the
//   class is in Place Actors at once. No JavaScript.
// =============================================================================

#include "../VoxelAssemblyEditor.h"
#include "../../vasm/VasmKinds.h"
#include "../../vasm/VoxelAssembly.h"
#include "../../gameplay/Input.h"

#include <cstdio>
#include <set>
#include <sstream>

namespace editor_ui::vasm_ed
{
    namespace vfs = std::filesystem;
    namespace vk = lynx::vasm;

    // =========================================================================
    // Small helpers
    // =========================================================================

    static RBlockShape ToShape(vk::Shape shape)
    {
        switch (shape)
        {
        case vk::Shape::Hat: return R_BLOCK_HAT;
        case vk::Shape::Cap: return R_BLOCK_CAP;
        case vk::Shape::Wrap: return R_BLOCK_C;
        case vk::Shape::Value: return R_BLOCK_REPORTER;
        case vk::Shape::Condition: return R_BLOCK_BOOLEAN;
        default: return R_BLOCK_STATEMENT;
        }
    }

    // Index of the first %m slot of a label, -1 if none.
    static int ChoiceSlot(const char* label)
    {
        int slot = 0;
        for (const char* c = label; *c; ++c)
            if (c[0] == '%' && (c[1] == 'n' || c[1] == 's' || c[1] == 'b' || c[1] == 'r' || c[1] == 'm'))
            {
                if (c[1] == 'm')
                    return slot;
                ++slot;
            }
        return -1;
    }

    static std::string Trim(const std::string& s)
    {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return {};
        const size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static bool IsIdentifier(const std::string& s)
    {
        if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0])))
            return false;
        for (char c : s)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                return false;
        return true;
    }

    static std::string Identifier(const std::string& text, const char* fallback)
    {
        std::string out;
        for (char c : text)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                out += c;
            else if (!out.empty() && out.back() != '_')
                out += '_';
        }
        while (!out.empty() && out.back() == '_')
            out.pop_back();
        if (out.empty())
            out = fallback;
        if (std::isdigit(static_cast<unsigned char>(out[0])))
            out = "_" + out;
        return out;
    }

    // A name shown in a label : no character of the label syntax, one line.
    static std::string LabelSafe(const std::string& text)
    {
        std::string out;
        for (char c : text)
            out += (c == '%' || c == '|' || c == '{' || c == '}' || c == '\n' || c == '\r' || c == '"') ? ' ' : c;
        return Trim(out);
    }

    static std::string Join(const std::vector<std::string>& list)
    {
        std::string out;
        for (const std::string& s : list)
        {
            if (s.empty())
                continue;
            if (!out.empty())
                out += '\n';
            out += s;
        }
        return out;
    }

    static void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
    {
        if (from.empty())
            return;
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
            text.replace(at, from.size(), to);
    }

    // =========================================================================
    // The document
    // =========================================================================

    struct Var
    {
        std::string id, type = "number", name, value = "0";
        bool global = false;
    };

    struct Func
    {
        std::string id, name;
        std::vector<std::string> inputs;
        std::string text;
        float vx = -20.f, vy = -20.f, vz = 1.f;
    };

    struct Graph
    {
        std::string name;
        std::string text;
        float vx = -20.f, vy = -20.f, vz = 1.f;
    };

    enum class Pick { None, Graph, Func, Var };

    const char* const kTypes[] = { "number", "text", "bool", "actor" };
    const char* const kTypeLabels[] = { "Number", "Text", "Boolean", "Actor" };

    const char* const kParents[] = {
        "Actor", "Humanoid", "ColliderActor", "SpriteActor", "SoundActor", "Light2DActor", "PointLightActor", "ParticleActor",
    };

    struct CategoryColor { const char* name; int rgb; };
    const CategoryColor kCategoryColors[] = {
        { "Events", 0xFFBF00 }, { "Control", 0xFFAB19 }, { "Motion", 0x4C97FF }, { "Character", 0x3D6FD9 },
        { "Looks", 0x9966FF }, { "Sound", 0xCF63CF }, { "Sensing", 0x5CB1D6 }, { "Operators", 0x59C059 },
        { "Variables", 0xFF8C1A }, { "Actors", 0x0FBD8C }, { "World", 0x2E9E8F }, { "Functions", 0xFF6680 },
        { "Inputs", 0xE0507A },
    };

    struct Doc
    {
        vfs::path path;
        std::string class_name = "MyActor";
        std::string parent = "Actor";
        std::vector<Var> vars;
        std::vector<Func> funcs;
        std::vector<Graph> graphs;

        bool in_function = false;   // the graph in the canvas
        int current = 0;
        Pick pick = Pick::Graph;    // the item shown in the details
        int pick_index = 0;

        std::string saved, snapshot;
        std::vector<std::string> undo, redo;
        bool dirty = false, touched = false;
        vfs::file_time_type write_time{};
        double disk_check = 0.0;
        bool changed_on_disk = false;

        Rid panel = R_INVALID_ID, editor = R_INVALID_ID, class_input = R_INVALID_ID, parent_combo = R_INVALID_ID;
        Rid graphs_list = R_INVALID_ID, funcs_list = R_INVALID_ID, vars_list = R_INVALID_ID, details = R_INVALID_ID;
        Rid status = R_INVALID_ID, disk = R_INVALID_ID, undo_button = R_INVALID_ID, redo_button = R_INVALID_ID;
        Rid graph_title = R_INVALID_ID;
        std::string shown_title;
        double status_time = -100.0, options_time = -100.0;
        bool building = false;
        bool lists_dirty = true, details_dirty = true;
        std::set<std::string> kinds;          // kinds made for this file
        std::string input_names;              // actions + axes last given to the menus
    };

    static std::vector<std::unique_ptr<Doc>> g_docs;
    static std::unique_ptr<eui::Menu> g_menu;

    static bool Alive(const Doc* doc)
    {
        return std::any_of(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
    }

    static void DocLater(Doc* doc, std::function<void(Doc&)> fn)
    {
        eui::Later([doc, fn]() { if (Alive(doc)) fn(*doc); });
    }

    static void Status(Doc& doc, const std::string& text)
    {
        eui::SetText(doc.status, text);
        doc.status_time = Rget_time();
        std::cout << "[VOXEL ASSEMBLY] " << text << "\n";
    }

    static Var* FindVar(Doc& doc, const std::string& name)
    {
        for (Var& v : doc.vars)
            if (v.name == name)
                return &v;
        return nullptr;
    }

    static Func* FindFunc(Doc& doc, const std::string& id)
    {
        for (Func& f : doc.funcs)
            if (f.id == id)
                return &f;
        return nullptr;
    }

    static std::string NewId(const Doc& doc, char prefix)
    {
        int n = 0;
        auto scan = [&](const std::string& id)
        {
            if (id.size() > 1 && id[0] == prefix)
                n = std::max(n, std::atoi(id.c_str() + 1));
        };
        for (const Var& v : doc.vars)
            scan(v.id);
        for (const Func& f : doc.funcs)
            scan(f.id);
        return std::string(1, prefix) + std::to_string(n + 1);
    }

    static Func* CurrentFunc(Doc& doc)
    {
        return doc.in_function && doc.current >= 0 && doc.current < static_cast<int>(doc.funcs.size()) ? &doc.funcs[doc.current] : nullptr;
    }

    // ---- the text of the canvas goes back into its graph ---------------------------------

    static void Flush(Doc& doc)
    {
        if (doc.editor == R_INVALID_ID)
            return;
        const char* saved = Rblock_editor_save(doc.editor);
        const std::string text = saved ? saved : "";
        float x = 0.f, y = 0.f, z = 1.f;
        Rblock_editor_get_view(doc.editor, &x, &y, &z);
        if (Func* f = CurrentFunc(doc))
        {
            f->text = text;
            f->vx = x; f->vy = y; f->vz = z;
        }
        else if (!doc.in_function && doc.current >= 0 && doc.current < static_cast<int>(doc.graphs.size()))
        {
            Graph& g = doc.graphs[doc.current];
            g.text = text;
            g.vx = x; g.vy = y; g.vz = z;
        }
    }

    // =========================================================================
    // File
    // =========================================================================

    static std::string Serialize(Doc& doc)
    {
        Flush(doc);
        std::string text = "lynx-vasm 1\n";
        text += "class " + doc.class_name + "\n";
        text += "extends " + doc.parent + "\n";
        for (const Var& v : doc.vars)
        {
            std::string value = v.value;
            std::replace(value.begin(), value.end(), '\n', ' ');
            text += "var " + v.id + " " + v.type + " " + (v.global ? "global " : "actor ") + v.name + " " + value + "\n";
        }
        for (const Func& f : doc.funcs)
        {
            text += "function " + f.id + " " + f.name + "\n";
            for (const std::string& in : f.inputs)
                text += "input " + f.id + " " + in + "\n";
        }
        for (const Graph& g : doc.graphs)
            text += "graph event " + g.name + "\n" + g.text + (g.text.empty() || g.text.back() == '\n' ? "" : "\n") + "end\n";
        for (const Func& f : doc.funcs)
            text += "graph function " + f.id + "\n" + f.text + (f.text.empty() || f.text.back() == '\n' ? "" : "\n") + "end\n";
        return text;
    }

    static void Parse(Doc& doc, const std::string& text)
    {
        doc.vars.clear();
        doc.funcs.clear();
        doc.graphs.clear();
        doc.parent = "Actor";
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::istringstream words(line);
            std::string key, rest;
            words >> key;
            std::getline(words, rest);
            rest = Trim(rest);
            if (key == "class")
                doc.class_name = Identifier(rest, "MyActor");
            else if (key == "extends")
                doc.parent = Identifier(rest, "Actor");
            else if (key == "var")
            {
                std::istringstream w(rest);
                Var v;
                std::string scope;
                w >> v.id >> v.type >> scope >> v.name;
                std::getline(w, v.value);
                if (!v.value.empty() && v.value[0] == ' ')
                    v.value.erase(0, 1);
                v.global = scope == "global";
                if (!v.id.empty() && !v.name.empty() && !FindVar(doc, v.name))
                    doc.vars.push_back(v);
            }
            else if (key == "function")
            {
                std::istringstream w(rest);
                Func f;
                w >> f.id;
                std::getline(w, f.name);
                f.name = Trim(f.name);
                if (!f.id.empty())
                    doc.funcs.push_back(f);
            }
            else if (key == "input")
            {
                std::istringstream w(rest);
                std::string fid, name;
                w >> fid;
                std::getline(w, name);
                if (Func* f = FindFunc(doc, fid))
                    f->inputs.push_back(Trim(name));
            }
            else if (key == "graph")
            {
                std::istringstream w(rest);
                std::string type, id;
                w >> type;
                std::getline(w, id);
                id = Trim(id);
                std::string body;
                while (std::getline(in, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line == "end")
                        break;
                    body += line + "\n";
                }
                if (type == "function")
                {
                    if (Func* f = FindFunc(doc, id))
                        f->text = body;
                }
                else
                {
                    Graph g;
                    g.name = id.empty() ? std::string("Event Graph") : id;
                    g.text = body;
                    doc.graphs.push_back(g);
                }
            }
        }
        if (doc.graphs.empty())
            doc.graphs.push_back(Graph{ "Event Graph", "", -20.f, -20.f, 1.f });
        if (doc.in_function ? doc.current >= static_cast<int>(doc.funcs.size()) : doc.current >= static_cast<int>(doc.graphs.size()))
        {
            doc.in_function = false;
            doc.current = 0;
        }
    }

    // =========================================================================
    // Kinds of the palette (VasmKinds.h + those of the file)
    // =========================================================================

    static void AddKind(Doc& doc, const std::string& name, const std::string& label, RBlockShape shape, const char* category)
    {
        if (doc.editor == R_INVALID_ID)
            return;
        if (doc.kinds.count(name))
        {
            Rblock_editor_set_kind_label(doc.editor, name.c_str(), label.c_str());
            Rblock_editor_set_kind_visible(doc.editor, name.c_str(), R_TRUE);
            return;
        }
        if (Rblock_editor_add_kind(doc.editor, name.c_str(), label.c_str(), shape, category) >= 0)
            doc.kinds.insert(name);
    }

    static std::string CallLabel(const Func& f)
    {
        std::string label = LabelSafe(f.name.empty() ? f.id : f.name);
        for (size_t i = 0; i < f.inputs.size(); ++i)
            label += " %r";
        return label;
    }

    // Every kind of the file, shown or hidden for the graph in the canvas.
    static void SyncKinds(Doc& doc)
    {
        if (doc.editor == R_INVALID_ID)
            return;
        std::set<std::string> wanted;
        for (const Var& v : doc.vars)
        {
            AddKind(doc, "var." + v.id, LabelSafe(v.name), R_BLOCK_REPORTER, "Variables");
            wanted.insert("var." + v.id);
        }
        const Func* current = CurrentFunc(doc);
        for (const Func& f : doc.funcs)
        {
            AddKind(doc, "call." + f.id, CallLabel(f), R_BLOCK_STATEMENT, "Functions");
            AddKind(doc, "callv." + f.id, CallLabel(f), R_BLOCK_REPORTER, "Functions");
            wanted.insert("call." + f.id);
            wanted.insert("callv." + f.id);
            for (size_t i = 0; i < f.inputs.size(); ++i)
            {
                const std::string name = "arg." + f.id + "." + std::to_string(i);
                AddKind(doc, name, LabelSafe(f.inputs[i]), R_BLOCK_REPORTER, "Inputs");
                if (current == &f)
                    wanted.insert(name);
            }
        }
        for (const std::string& name : doc.kinds)
            if (!wanted.count(name))
                Rblock_editor_set_kind_visible(doc.editor, name.c_str(), R_FALSE);

        // Events in the event graphs, "when called" and "return" in the functions.
        for (const vk::KindInfo& k : vk::kKinds)
        {
            bool visible = true;
            if (std::strcmp(k.name, "fn.entry") == 0)
                visible = false;   // one per function graph, made by the editor
            else if (std::strcmp(k.name, "fn.return") == 0)
                visible = doc.in_function;
            else if (k.shape == vk::Shape::Hat)
                visible = !doc.in_function;
            Rblock_editor_set_kind_visible(doc.editor, k.name, visible ? R_TRUE : R_FALSE);
        }
    }

    // The menus of the %m slots : actions, axes, variables (and the inputs of the function).
    static void SyncOptions(Doc& doc, bool force)
    {
        if (doc.editor == R_INVALID_ID)
            return;
        const std::string actions = Join(lynx::GetInputActionNames());
        const std::string axes = Join(lynx::GetInputAxisNames());
        std::vector<std::string> names;
        for (const Var& v : doc.vars)
            names.push_back(v.name);
        if (const Func* f = CurrentFunc(doc))
            for (const std::string& in : f->inputs)
                names.push_back(in);
        const std::string variables = Join(names);
        const std::string all = actions + "\x1f" + axes + "\x1f" + variables;
        if (!force && all == doc.input_names)
            return;
        doc.input_names = all;
        for (const vk::KindInfo& k : vk::kKinds)
        {
            const int slot = ChoiceSlot(k.label);
            if (slot < 0)
                continue;
            const char* options = "";
            switch (k.choices)
            {
            case vk::Choices::Actions: options = actions.c_str(); break;
            case vk::Choices::Axes: options = axes.c_str(); break;
            case vk::Choices::Variables: options = variables.c_str(); break;
            case vk::Choices::Fixed: options = k.options; break;
            default: break;
            }
            Rblock_editor_set_slot_options(doc.editor, k.name, slot, options);
        }
    }

    // Blocks of kinds the file no longer has (a deleted variable...) : a hidden kind, so the text loads.
    static void EnsureKindsOfText(Doc& doc, const std::string& text)
    {
        const std::string marker = "(block \"";
        for (size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 1))
        {
            const size_t start = at + marker.size();
            const size_t end = text.find('"', start);
            if (end == std::string::npos)
                break;
            const std::string name = text.substr(start, end - start);
            if (vk::FindKind(name) >= 0 || doc.kinds.count(name))
                continue;
            // its slots : the values written after the name
            size_t slots = 0;
            size_t c = text.find('(', end);
            if (c != std::string::npos)
            {
                bool quoted = false;
                for (++c; c < text.size(); ++c)
                {
                    if (text[c] == '\\' && quoted) { ++c; continue; }
                    if (text[c] == '"') { if (!quoted) ++slots; quoted = !quoted; continue; }
                    if (!quoted && text[c] == ')')
                        break;
                }
            }
            std::string label = "(removed)";
            for (size_t i = 0; i < slots; ++i)
                label += " %r";
            const bool statement = name.rfind("call.", 0) == 0;
            if (Rblock_editor_add_kind(doc.editor, name.c_str(), label.c_str(), statement ? R_BLOCK_STATEMENT : R_BLOCK_REPORTER, "Variables") >= 0)
            {
                doc.kinds.insert(name);
                Rblock_editor_set_kind_visible(doc.editor, name.c_str(), R_FALSE);
            }
        }
    }

    // A function graph starts with its "when called", which cannot be deleted.
    static void EnsureEntry(Doc& doc)
    {
        int entry = 0;
        const int count = Rblock_editor_get_script_count(doc.editor);
        for (int i = 0; i < count && !entry; ++i)
        {
            const int top = Rblock_editor_get_script(doc.editor, i);
            const char* kind = Rblock_editor_get_kind(doc.editor, top);
            if (kind && std::strcmp(kind, "fn.entry") == 0)
                entry = top;
        }
        if (!entry)
            entry = Rblock_editor_add_block(doc.editor, "fn.entry", 40.f, 40.f);
        if (entry > 0)
            Rblock_editor_set_block_deletable(doc.editor, entry, R_FALSE);
    }

    static std::string CurrentTitle(Doc& doc)
    {
        if (const Func* f = CurrentFunc(doc))
        {
            std::string title = "Function  " + f->name + " (";
            for (size_t i = 0; i < f->inputs.size(); ++i)
                title += (i ? ", " : "") + f->inputs[i];
            return title + ")";
        }
        if (doc.current >= 0 && doc.current < static_cast<int>(doc.graphs.size()))
            return doc.graphs[doc.current].name;
        return {};
    }

    // The graph of doc.in_function / doc.current into the canvas.
    static void LoadGraph(Doc& doc)
    {
        if (doc.editor == R_INVALID_ID)
            return;
        doc.building = true;
        SyncKinds(doc);
        SyncOptions(doc, true);
        std::string text;
        float vx = -20.f, vy = -20.f, vz = 1.f;
        if (const Func* f = CurrentFunc(doc))
        {
            text = f->text;
            vx = f->vx; vy = f->vy; vz = f->vz;
        }
        else if (doc.current >= 0 && doc.current < static_cast<int>(doc.graphs.size()))
        {
            const Graph& g = doc.graphs[doc.current];
            text = g.text;
            vx = g.vx; vy = g.vy; vz = g.vz;
        }
        EnsureKindsOfText(doc, text);
        Rblock_editor_load(doc.editor, text.c_str());
        if (doc.in_function)
            EnsureEntry(doc);
        Rblock_editor_set_view(doc.editor, vx, vy, vz);
        eui::SetText(doc.graph_title, CurrentTitle(doc));
        doc.building = false;
        doc.lists_dirty = true;
    }

    static void Switch(Doc& doc, bool in_function, int index)
    {
        Flush(doc);
        doc.in_function = in_function;
        doc.current = index;
        LoadGraph(doc);
    }

    // =========================================================================
    // Undo, save
    // =========================================================================

    static void Touch(Doc& doc)
    {
        doc.touched = true;
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
        Parse(doc, state);
        LoadGraph(doc);
        doc.snapshot = state;
        doc.dirty = state != doc.saved;
        doc.lists_dirty = doc.details_dirty = true;
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

    static void LoadDoc(Doc& doc)
    {
        std::ifstream in(doc.path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.rfind("lynx-vasm", 0) != 0)
            text = "lynx-vasm 1\nclass " + Identifier(doc.path.stem().string(), "MyActor") + "\nextends Actor\n";
        doc.in_function = false;
        doc.current = 0;
        doc.pick = Pick::Graph;
        doc.pick_index = 0;
        Parse(doc, text);
        LoadGraph(doc);
        doc.saved = doc.snapshot = Serialize(doc);   // as Raven writes it
        doc.undo.clear();
        doc.redo.clear();
        doc.dirty = doc.touched = false;
        std::error_code ec;
        doc.write_time = vfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;
        doc.lists_dirty = doc.details_dirty = true;
    }

    static bool SaveDoc(Doc& doc)
    {
        const std::string text = Serialize(doc);
        {
            std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                Status(doc, "Could not write " + doc.path.string());
                return false;
            }
            out << text;
        }
        doc.saved = doc.snapshot = text;
        doc.dirty = doc.touched = false;
        std::error_code ec;
        doc.write_time = vfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;

        // The engine reads it again : the class is in Place Actors at once.
        lynx::vasm::PrepareClasses();
        std::string error;
        if (!lynx::vasm::Check(text, error))
            Status(doc, "Saved, but " + error);
        else
            Status(doc, "Saved " + doc.path.filename().string() + "  (class " + doc.class_name + ")");
        return true;
    }

    // =========================================================================
    // Graphs, functions, variables
    // =========================================================================

    static std::string CheckName(Doc& doc, const std::string& raw, bool variable, const std::string& keep = {})
    {
        const std::string name = Trim(raw);
        if (name.empty())
            return "A name, please.";
        if (variable)
        {
            if (!IsIdentifier(name))
                return "Letters, digits and _ only, not starting with a digit.";
            if (name != keep && FindVar(doc, name))
                return "\"" + name + "\" already exists.";
            static const char* const kReserved[] = { "x", "y", "z", "id", "class", "transform", "scripts", "outliner_folder",
                                                     "auto_possess_player", "object_id_" };
            for (const char* r : kReserved)
                if (name == r)
                    return "\"" + name + "\" is already used by the actor.";
        }
        return {};
    }

    static void Pick_(Doc& doc, Pick pick, int index)
    {
        doc.pick = pick;
        doc.pick_index = index;
        doc.details_dirty = doc.lists_dirty = true;
    }

    static void AddGraph(Doc& doc)
    {
        Doc* d = &doc;
        eui::AskText(g.window, "New event graph", "Name of the graph", "Event Graph " + std::to_string(doc.graphs.size() + 1),
                     [d](const std::string& name)
        {
            if (!Alive(d))
                return;
            Flush(*d);
            d->graphs.push_back(Graph{ Trim(name), "", -20.f, -20.f, 1.f });
            Switch(*d, false, static_cast<int>(d->graphs.size()) - 1);
            Pick_(*d, Pick::Graph, d->current);
            Touch(*d);
        },
        [d](const std::string& name) { return Alive(d) ? CheckName(*d, name, false) : std::string(); });
    }

    static void AddFunction(Doc& doc)
    {
        Doc* d = &doc;
        eui::AskText(g.window, "New function", "Name of the function (its inputs : in the details)", "My Function",
                     [d](const std::string& name)
        {
            if (!Alive(d))
                return;
            Flush(*d);
            Func f;
            f.id = NewId(*d, 'f');
            f.name = LabelSafe(name);
            d->funcs.push_back(f);
            Switch(*d, true, static_cast<int>(d->funcs.size()) - 1);
            Pick_(*d, Pick::Func, d->current);
            Touch(*d);
            Status(*d, "Function \"" + f.name + "\" : its blocks go under \"when called\" ; use it from Functions");
        },
        [d](const std::string& name) { return Alive(d) ? CheckName(*d, name, false) : std::string(); });
    }

    static void AddVariable(Doc& doc)
    {
        Doc* d = &doc;
        eui::AskText(g.window, "New variable", "Name of the variable", "score", [d](const std::string& name)
        {
            if (!Alive(d))
                return;
            Var v;
            v.id = NewId(*d, 'v');
            v.name = Trim(name);
            d->vars.push_back(v);
            SyncKinds(*d);
            SyncOptions(*d, true);
            Pick_(*d, Pick::Var, static_cast<int>(d->vars.size()) - 1);
            Touch(*d);
            Status(*d, "Variable \"" + v.name + "\" : its block is in Variables");
        },
        [d](const std::string& name) { return Alive(d) ? CheckName(*d, name, true) : std::string(); });
    }

    // Every graph (the canvas too) : `from` -> `to` in the texts.
    static void ReplaceInGraphs(Doc& doc, const std::vector<std::pair<std::string, std::string>>& changes, const Func* only = nullptr)
    {
        Flush(doc);
        auto apply = [&](std::string& text)
        {
            for (const auto& [from, to] : changes)
                ReplaceAll(text, from, to);
        };
        if (!only)
            for (Graph& g : doc.graphs)
                apply(g.text);
        for (Func& f : doc.funcs)
            if (!only || only == &f)
                apply(f.text);
        LoadGraph(doc);
    }

    static void RenameVar(Doc& doc, int index, const std::string& name)
    {
        if (index < 0 || index >= static_cast<int>(doc.vars.size()) || doc.vars[index].name == name)
            return;
        const std::string old = doc.vars[index].name;
        doc.vars[index].name = name;
        std::vector<std::pair<std::string, std::string>> changes;
        for (const char* kind : { "var.set", "var.change", "ctl.for" })
            changes.emplace_back(std::string("(block \"") + kind + "\" (\"" + old + "\"", std::string("(block \"") + kind + "\" (\"" + name + "\"");
        ReplaceInGraphs(doc, changes);
        Touch(doc);
        doc.lists_dirty = true;
    }

    static void DeleteVar(Doc& doc, int index)
    {
        if (index < 0 || index >= static_cast<int>(doc.vars.size()))
            return;
        const std::string name = doc.vars[index].name;
        doc.vars.erase(doc.vars.begin() + index);
        SyncKinds(doc);
        SyncOptions(doc, true);
        Pick_(doc, Pick::None, 0);
        Touch(doc);
        Status(doc, "\"" + name + "\" removed (its blocks no longer have a value)");
    }

    static void DeleteFunc(Doc& doc, int index)
    {
        if (index < 0 || index >= static_cast<int>(doc.funcs.size()))
            return;
        Flush(doc);
        const std::string name = doc.funcs[index].name;
        doc.funcs.erase(doc.funcs.begin() + index);
        if (doc.in_function)
        {
            doc.in_function = false;
            doc.current = 0;
        }
        LoadGraph(doc);
        Pick_(doc, Pick::Graph, doc.current);
        Touch(doc);
        Status(doc, "Function \"" + name + "\" removed");
    }

    static void DeleteGraph(Doc& doc, int index)
    {
        if (index < 0 || index >= static_cast<int>(doc.graphs.size()) || doc.graphs.size() < 2)
            return;
        Flush(doc);
        doc.graphs.erase(doc.graphs.begin() + index);
        if (!doc.in_function)
            doc.current = std::min(doc.current, static_cast<int>(doc.graphs.size()) - 1);
        LoadGraph(doc);
        Pick_(doc, Pick::Graph, doc.in_function ? 0 : doc.current);
        Touch(doc);
    }

    // An input removed : the inputs after it move up (arg.f.2 -> arg.f.1).
    static void RemoveInput(Doc& doc, Func& f, size_t index)
    {
        if (index >= f.inputs.size())
            return;
        std::vector<std::pair<std::string, std::string>> changes;
        const std::string base = "(block \"arg." + f.id + ".";
        changes.emplace_back(base + std::to_string(index) + "\"", "(block \"arg." + f.id + ".gone" + std::to_string(index) + "\"");
        for (size_t i = index + 1; i < f.inputs.size(); ++i)
            changes.emplace_back(base + std::to_string(i) + "\"", base + std::to_string(i - 1) + "\"");
        f.inputs.erase(f.inputs.begin() + static_cast<long>(index));
        ReplaceInGraphs(doc, changes, &f);
        Touch(doc);
    }

    // =========================================================================
    // Lists and details
    // =========================================================================

    static void RebuildLists(Doc& doc)
    {
        doc.lists_dirty = false;
        doc.building = true;
        Rlist_view_clear_items(doc.graphs_list);
        for (const Graph& g2 : doc.graphs)
            Rlist_view_add_item(doc.graphs_list, g2.name.c_str());
        Rlist_view_clear_items(doc.funcs_list);
        for (const Func& f : doc.funcs)
        {
            std::string text = f.name + " (";
            for (size_t i = 0; i < f.inputs.size(); ++i)
                text += (i ? ", " : "") + f.inputs[i];
            Rlist_view_add_item(doc.funcs_list, (text + ")").c_str());
        }
        Rlist_view_clear_items(doc.vars_list);
        for (const Var& v : doc.vars)
        {
            int t = 0;
            for (int i = 0; i < 4; ++i)
                if (v.type == kTypes[i])
                    t = i;
            std::string text = v.name + "  :  " + kTypeLabels[t] + (v.global ? "  (global)" : "");
            Rlist_view_add_item(doc.vars_list, text.c_str());
        }
        Rlist_view_set_selected(doc.graphs_list, doc.pick == Pick::Graph ? doc.pick_index : -1);
        Rlist_view_set_selected(doc.funcs_list, doc.pick == Pick::Func ? doc.pick_index : -1);
        Rlist_view_set_selected(doc.vars_list, doc.pick == Pick::Var ? doc.pick_index : -1);
        eui::SetText(doc.graph_title, CurrentTitle(doc));
        doc.building = false;
    }

    static Rid AddXml(Rid parent, const std::string& xml)
    {
        Rid w = Rwidget_create_from_xml(xml.c_str(), xml.size());
        if (w != R_INVALID_ID)
            Rwidget_add_child(parent, w);
        return w;
    }

    static void RebuildDetails(Doc& doc)
    {
        doc.details_dirty = false;
        doc.building = true;
        Rwidget_clear_children(doc.details);
        Doc* d = &doc;

        if (doc.pick == Pick::Graph && doc.pick_index >= 0 && doc.pick_index < static_cast<int>(doc.graphs.size()))
        {
            const int index = doc.pick_index;
            AddXml(doc.details, "<Label Text='EVENT GRAPH' Class='section'/>");
            Rid row = AddXml(doc.details, "<HBox Spacing='6'><Label Text='Name' Width='70' Slot.VAlign='Center'/><TextInput Name='name' Slot.Fill='1'/></HBox>");
            Rid name = eui::Find(row, "name");
            Rtext_input_set_text(name, doc.graphs[index].name.c_str());
            eui::OnCommit(name, [d, index](const std::string& text)
            {
                const std::string n = LabelSafe(text);
                if (n.empty() || index >= static_cast<int>(d->graphs.size()) || d->graphs[index].name == n)
                    return;
                d->graphs[index].name = n;
                d->lists_dirty = true;
                Touch(*d);
            });
            Rid del = AddXml(doc.details, "<Button Text='Delete this graph' Class='tool'/>");
            eui::SetEnabled(del, doc.graphs.size() > 1);
            eui::OnClick(del, [d, index]() { DocLater(d, [index](Doc& x) { DeleteGraph(x, index); }); });
        }
        else if (doc.pick == Pick::Func && doc.pick_index >= 0 && doc.pick_index < static_cast<int>(doc.funcs.size()))
        {
            const int index = doc.pick_index;
            Func& f = doc.funcs[index];
            AddXml(doc.details, "<Label Text='FUNCTION' Class='section'/>");
            Rid row = AddXml(doc.details, "<HBox Spacing='6'><Label Text='Name' Width='70' Slot.VAlign='Center'/><TextInput Name='name' Slot.Fill='1'/></HBox>");
            Rid name = eui::Find(row, "name");
            Rtext_input_set_text(name, f.name.c_str());
            eui::OnCommit(name, [d, index](const std::string& text)
            {
                const std::string n = LabelSafe(text);
                if (n.empty() || index >= static_cast<int>(d->funcs.size()) || d->funcs[index].name == n)
                    return;
                d->funcs[index].name = n;
                SyncKinds(*d);
                d->lists_dirty = true;
                eui::SetText(d->graph_title, CurrentTitle(*d));
                Touch(*d);
            });
            AddXml(doc.details, "<Label Text='Inputs (blocks of its graph)' Class='dim' Slot.Padding='0,6,0,0'/>");
            for (size_t i = 0; i < f.inputs.size(); ++i)
            {
                Rid in_row = AddXml(doc.details, "<HBox Spacing='4'><TextInput Name='input' Slot.Fill='1'/><Button Name='remove' Text='x' Class='tool' Tooltip='Remove the input'/></HBox>");
                Rid input = eui::Find(in_row, "input");
                Rtext_input_set_text(input, f.inputs[i].c_str());
                eui::OnCommit(input, [d, index, i](const std::string& text)
                {
                    const std::string n = Identifier(Trim(text), "input");
                    if (index >= static_cast<int>(d->funcs.size()) || i >= d->funcs[index].inputs.size() || d->funcs[index].inputs[i] == n)
                        return;
                    d->funcs[index].inputs[i] = n;
                    SyncKinds(*d);
                    SyncOptions(*d, true);
                    d->lists_dirty = true;
                    Touch(*d);
                });
                eui::OnClick(eui::Find(in_row, "remove"), [d, index, i]()
                {
                    DocLater(d, [index, i](Doc& x)
                    {
                        if (index < static_cast<int>(x.funcs.size()))
                            RemoveInput(x, x.funcs[index], i);
                        x.details_dirty = x.lists_dirty = true;
                    });
                });
            }
            Rid add = AddXml(doc.details, "<Button Text='+ Input' Class='tool'/>");
            eui::OnClick(add, [d, index]()
            {
                DocLater(d, [index](Doc& x)
                {
                    if (index >= static_cast<int>(x.funcs.size()))
                        return;
                    Func& fn = x.funcs[index];
                    fn.inputs.push_back("input" + std::to_string(fn.inputs.size() + 1));
                    SyncKinds(x);
                    SyncOptions(x, true);
                    x.details_dirty = x.lists_dirty = true;
                    Touch(x);
                });
            });
            Rid del = AddXml(doc.details, "<Button Text='Delete this function' Class='tool' Slot.Padding='0,8,0,0'/>");
            eui::OnClick(del, [d, index]()
            {
                const std::string fname = index < static_cast<int>(d->funcs.size()) ? d->funcs[index].name : std::string();
                eui::Ask(g.window, "Delete the function", "Delete \"" + fname + "\" and its graph ?", { "Delete", "Cancel" },
                         [d, index](int choice) { if (Alive(d) && choice == 0) DeleteFunc(*d, index); });
            });
        }
        else if (doc.pick == Pick::Var && doc.pick_index >= 0 && doc.pick_index < static_cast<int>(doc.vars.size()))
        {
            const int index = doc.pick_index;
            const Var& v = doc.vars[index];
            AddXml(doc.details, "<Label Text='VARIABLE' Class='section'/>");
            Rid row = AddXml(doc.details, "<HBox Spacing='6'><Label Text='Name' Width='70' Slot.VAlign='Center'/><TextInput Name='name' Slot.Fill='1'/></HBox>");
            Rid name = eui::Find(row, "name");
            Rtext_input_set_text(name, v.name.c_str());
            eui::OnCommit(name, [d, index](const std::string& text)
            {
                const std::string n = Trim(text);
                if (index >= static_cast<int>(d->vars.size()) || d->vars[index].name == n)
                    return;
                const std::string problem = CheckName(*d, n, true, d->vars[index].name);
                if (!problem.empty())
                {
                    Status(*d, problem);
                    d->details_dirty = true;
                    return;
                }
                DocLater(d, [index, n](Doc& x) { RenameVar(x, index, n); x.details_dirty = true; });
            });
            Rid type_row = AddXml(doc.details, "<HBox Spacing='6'><Label Text='Type' Width='70' Slot.VAlign='Center'/><ComboBox Name='type' Slot.Fill='1'/></HBox>");
            Rid type = eui::Find(type_row, "type");
            int t = 0;
            for (int i = 0; i < 4; ++i)
            {
                Rcombo_box_add_item(type, kTypeLabels[i]);
                if (v.type == kTypes[i])
                    t = i;
            }
            Rcombo_box_set_selected(type, t);
            eui::OnSelect(type, [d, index](int choice)
            {
                if (d->building || choice < 0 || choice > 3 || index >= static_cast<int>(d->vars.size()))
                    return;
                if (d->vars[index].type == kTypes[choice])
                    return;
                d->vars[index].type = kTypes[choice];
                d->details_dirty = d->lists_dirty = true;
                Touch(*d);
            });
            if (v.type != "actor")
            {
                Rid value_row = AddXml(doc.details, "<HBox Spacing='6'><Label Text='Default' Width='70' Slot.VAlign='Center'/><TextInput Name='value' Slot.Fill='1'/></HBox>");
                Rid value = eui::Find(value_row, "value");
                Rtext_input_set_text(value, v.value.c_str());
                eui::OnCommit(value, [d, index](const std::string& text)
                {
                    if (index < static_cast<int>(d->vars.size()) && d->vars[index].value != text)
                    {
                        d->vars[index].value = text;
                        Touch(*d);
                    }
                });
            }
            Rid global = AddXml(doc.details, "<Checkbox Text='Global (shared by every actor)'/>");
            eui::SetChecked(global, v.global);
            eui::OnToggle(global, [d, index](bool on)
            {
                if (d->building || index >= static_cast<int>(d->vars.size()) || d->vars[index].global == on)
                    return;
                d->vars[index].global = on;
                d->lists_dirty = true;
                Touch(*d);
            });
            AddXml(doc.details, std::string("<Label Class='dim' Wrap='true' Text='") +
                   (v.global ? "One value for the whole game." :
                    v.type == "actor" ? "One per actor, while the game runs." : "One per actor : editable in Details, saved with the level.") + "'/>");
            Rid del = AddXml(doc.details, "<Button Text='Delete this variable' Class='tool' Slot.Padding='0,8,0,0'/>");
            eui::OnClick(del, [d, index]() { DocLater(d, [index](Doc& x) { DeleteVar(x, index); }); });
        }
        else
        {
            AddXml(doc.details, "<Label Class='dim' Wrap='true' Text='Select a graph, a function or a variable.'/>");
        }
        doc.building = false;
    }

    // =========================================================================
    // The panel
    // =========================================================================

    static const char kPanelXml[] =
        "<HBox Spacing='0'>"
        "  <VBox Spacing='4' Slot.Padding='6'>"
        "    <HBox Spacing='6'><Label Text='Class' Width='56' Slot.VAlign='Center'/><TextInput Name='va.class' Slot.Fill='1' Tooltip='Name of the actor class (Place Actors, spawn)'/></HBox>"
        "    <HBox Spacing='6'><Label Text='Extends' Width='56' Slot.VAlign='Center'/><ComboBox Name='va.parent' Slot.Fill='1' Tooltip='Humanoid : walks, jumps and falls (Character blocks)'/></HBox>"
        "    <HBox Spacing='4' Slot.Padding='0,8,0,0'><Label Text='GRAPHS' Class='section' Slot.Fill='1' Slot.VAlign='Center'/><Button Name='va.addgraph' Text='+' Class='tool' Tooltip='New event graph'/></HBox>"
        "    <ListView Name='va.graphs' Height='80'/>"
        "    <HBox Spacing='4'><Label Text='FUNCTIONS' Class='section' Slot.Fill='1' Slot.VAlign='Center'/><Button Name='va.addfn' Text='+' Class='tool' Tooltip='New function (with its own graph)'/></HBox>"
        "    <ListView Name='va.funcs' Height='100'/>"
        "    <HBox Spacing='4'><Label Text='VARIABLES' Class='section' Slot.Fill='1' Slot.VAlign='Center'/><Button Name='va.addvar' Text='+' Class='tool' Tooltip='New variable'/></HBox>"
        "    <ListView Name='va.vars' Slot.Fill='1'/>"
        "  </VBox>"
        "  <VBox Spacing='4' Slot.Fill='1' Slot.Padding='4'>"
        "    <HBox Spacing='6'>"
        "      <Button Name='va.save' Text='Save' Style='Primary' Tooltip='Ctrl+S'/>"
        "      <Button Name='va.undo' Text='Undo' Class='tool' Tooltip='Ctrl+Z'/>"
        "      <Button Name='va.redo' Text='Redo' Class='tool' Tooltip='Ctrl+Y'/>"
        "      <Label Name='va.title' Text='' Class='section' Slot.VAlign='Center' Slot.Padding='10,0,0,0'/>"
        "      <Label Name='va.status' Text='' Class='warning' Slot.Fill='1' Slot.VAlign='Center'/>"
        "    </HBox>"
        "    <HBox Name='va.disk' Spacing='6' Visibility='Collapsed'>"
        "      <Label Text='The file changed on disk.' Class='warning' Slot.VAlign='Center'/>"
        "      <Button Name='va.reload' Text='Reload'/>"
        "      <Button Name='va.keep' Text='Keep my version'/>"
        "    </HBox>"
        "    <BlockEditor Name='va.blocks' Slot.Fill='1'/>"
        "  </VBox>"
        "  <ScrollBox><VBox Name='va.details' Spacing='5' Slot.Padding='8'/></ScrollBox>"
        "</HBox>";

    static void SyncHeader(Doc& doc)
    {
        doc.building = true;
        eui::SetText(doc.class_input, doc.class_name);
        int index = -1;
        const int count = Rcombo_box_get_item_count(doc.parent_combo);
        for (int i = 0; i < count; ++i)
            if (doc.parent == Rcombo_box_get_item_text(doc.parent_combo, i))
                index = i;
        if (index < 0)
        {
            Rcombo_box_add_item(doc.parent_combo, doc.parent.c_str());
            index = count;
        }
        if (Rcombo_box_get_selected(doc.parent_combo) != index)
            Rcombo_box_set_selected(doc.parent_combo, index);
        doc.building = false;
    }

    static void BlockChanged(Rid, int, void* user)
    {
        Doc* d = static_cast<Doc*>(user);
        if (Alive(d) && !d->building)
            Touch(*d);
    }

    static void ScriptClicked(Rid, int, void* user)
    {
        Doc* d = static_cast<Doc*>(user);
        if (Alive(d))
            Status(*d, "The blocks run in Play : place a " + d->class_name + " from Place Actors (save first)");
    }

    static void BlockContext(Rid, int block, float, float, void* user)
    {
        Doc* d = static_cast<Doc*>(user);
        if (!Alive(d))
            return;
        eui::Later([d, block]()
        {
            if (!Alive(d))
                return;
            g_menu = std::make_unique<eui::Menu>(g.window);
            eui::Menu& menu = *g_menu;
            if (block > 0)
            {
                const char* kind = Rblock_editor_get_kind(d->editor, block);
                const int k = kind ? vk::FindKind(kind) : -1;
                const int selected = Rblock_editor_get_selection_count(d->editor);
                menu.Item(selected > 1 ? "Delete the selection" : "Delete", [d]()
                {
                    if (!Alive(d))
                        return;
                    std::vector<int> blocks;
                    for (int i = 0; i < Rblock_editor_get_selection_count(d->editor); ++i)
                        blocks.push_back(Rblock_editor_get_selected(d->editor, i));
                    // the roots only : a block goes with what hangs under it
                    for (int b : blocks)
                    {
                        const char* kb = Rblock_editor_get_kind(d->editor, b);
                        if (!kb || std::strcmp(kb, "fn.entry") == 0)
                            continue;
                        RBlockPlace place;
                        int index = 0;
                        int parent = Rblock_editor_get_parent(d->editor, b, &place, &index);
                        bool inside = false;
                        while (parent > 0 && !inside)
                        {
                            inside = std::find(blocks.begin(), blocks.end(), parent) != blocks.end();
                            parent = Rblock_editor_get_parent(d->editor, parent, &place, &index);
                        }
                        if (!inside)
                            Rblock_editor_remove_block(d->editor, b);
                    }
                    Touch(*d);
                }, !(kind && std::strcmp(kind, "fn.entry") == 0 && selected <= 1));
                const std::string help = k >= 0 ? vk::kKinds[k].help : "";
                const std::string title = k >= 0 ? vk::kKinds[k].label : (kind ? kind : "");
                menu.Item("Help", [title, help]() { eui::Message(g.window, title, help.empty() ? "No help for this block." : help); });
            }
            else
            {
                menu.Item("New variable...", [d]() { if (Alive(d)) AddVariable(*d); });
                menu.Item("New function...", [d]() { if (Alive(d)) AddFunction(*d); });
                menu.Item("New event graph...", [d]() { if (Alive(d)) AddGraph(*d); });
            }
            menu.Open();
        });
    }

    static void Shortcut(Doc& doc, const REvent& e, bool& used)
    {
        const bool ctrl = (e.modifiers & R_MOD_CTRL) != 0;
        const bool shift = (e.modifiers & R_MOD_SHIFT) != 0;
        Doc* d = &doc;
        used = true;
        if (ctrl && e.key == R_KEY_S) { Commit(doc); SaveDoc(doc); return; }
        if (ein::WantTextInput()) { used = false; return; }
        if (ctrl && e.key == R_KEY_Z) { DocLater(d, [shift](Doc& x) { shift ? Redo(x) : Undo(x); }); return; }
        if (ctrl && e.key == R_KEY_Y) { DocLater(d, [](Doc& x) { Redo(x); }); return; }
        used = false;
    }

    static bool OnPanelClose(Rid panel);

    static void BuildPanel(Doc& doc)
    {
        doc.panel = Rwidget_create_from_xml(kPanelXml, sizeof(kPanelXml) - 1);
        if (doc.panel == R_INVALID_ID)
            return;
        auto find = [&](const char* name) { return eui::Find(doc.panel, name); };
        doc.editor = find("va.blocks");
        doc.class_input = find("va.class");
        doc.parent_combo = find("va.parent");
        doc.graphs_list = find("va.graphs");
        doc.funcs_list = find("va.funcs");
        doc.vars_list = find("va.vars");
        doc.details = find("va.details");
        doc.status = find("va.status");
        doc.disk = find("va.disk");
        doc.undo_button = find("va.undo");
        doc.redo_button = find("va.redo");
        doc.graph_title = find("va.title");
        Doc* d = &doc;

        // The blocks
        for (const CategoryColor& c : kCategoryColors)
            Rblock_editor_set_category_color(doc.editor, c.name, ((c.rgb >> 16) & 255) / 255.f, ((c.rgb >> 8) & 255) / 255.f, (c.rgb & 255) / 255.f);
        for (const vk::KindInfo& k : vk::kKinds)
            Rblock_editor_add_kind(doc.editor, k.name, k.label, ToShape(k.shape), k.category);
        Rblock_editor_set_changed_callback(doc.editor, BlockChanged, d);
        Rblock_editor_set_clicked_callback(doc.editor, ScriptClicked, d);
        Rblock_editor_set_context_callback(doc.editor, BlockContext, d);

        // Class
        for (const char* parent : kParents)
            Rcombo_box_add_item(doc.parent_combo, parent);
        eui::OnCommit(doc.class_input, [d](const std::string& text)
        {
            const std::string name = Identifier(Trim(text), "MyActor");
            if (name != d->class_name)
            {
                d->class_name = name;
                Touch(*d);
            }
            eui::SetText(d->class_input, d->class_name);
        });
        eui::OnSelect(doc.parent_combo, [d](int index)
        {
            if (d->building || index < 0)
                return;
            const std::string parent = Rcombo_box_get_item_text(d->parent_combo, index);
            if (parent != d->parent)
            {
                d->parent = parent;
                Touch(*d);
            }
        });

        // Lists
        eui::OnListSelect(doc.graphs_list, [d](int index)
        {
            if (d->building || index < 0)
                return;
            DocLater(d, [index](Doc& x)
            {
                if (x.in_function || x.current != index)
                    Switch(x, false, index);
                Pick_(x, Pick::Graph, index);
            });
        });
        eui::OnListSelect(doc.funcs_list, [d](int index)
        {
            if (d->building || index < 0)
                return;
            DocLater(d, [index](Doc& x)
            {
                if (!x.in_function || x.current != index)
                    Switch(x, true, index);
                Pick_(x, Pick::Func, index);
            });
        });
        eui::OnListSelect(doc.vars_list, [d](int index)
        {
            if (d->building || index < 0)
                return;
            DocLater(d, [index](Doc& x) { Pick_(x, Pick::Var, index); });
        });
        eui::OnClick(find("va.addgraph"), [d]() { AddGraph(*d); });
        eui::OnClick(find("va.addfn"), [d]() { AddFunction(*d); });
        eui::OnClick(find("va.addvar"), [d]() { AddVariable(*d); });

        // Toolbar
        eui::OnClick(find("va.save"), [d]() { Commit(*d); SaveDoc(*d); });
        eui::OnClick(doc.undo_button, [d]() { DocLater(d, [](Doc& x) { Undo(x); }); });
        eui::OnClick(doc.redo_button, [d]() { DocLater(d, [](Doc& x) { Redo(x); }); });
        eui::OnClick(find("va.reload"), [d]() { DocLater(d, [](Doc& x) { LoadDoc(x); SyncHeader(x); Status(x, "Reloaded"); }); });
        eui::OnClick(find("va.keep"), [d]() { d->changed_on_disk = false; });

        // A tab of its own (Documents.inl) : its parts are panels of the dock area of the tab.
        {
            const std::string title = doc.path.filename().string();
            auto page = std::make_shared<Rid>(R_INVALID_ID);
            doc.panel = documents::OpenSplit(doc.panel, title, lynx::editor::file_icons::Kind::VoxelAssembly, { "My Blueprint", "Graph", "Details" },
                                             [page]() { if (OnPanelClose(*page)) documents::Close(*page); });
            *page = doc.panel;
            doc.shown_title = title;
        }

        eui::OnEvent(doc.panel, [d](const REvent& e)
        {
            if (e.type != R_EVENT_KEY_DOWN)
                return false;
            bool used = false;
            Shortcut(*d, e, used);
            return used;
        });

    }

    static void CloseDoc(Doc* doc)
    {
        const auto it = std::find_if(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
        if (it == g_docs.end())
            return;
        const Rid panel = doc->panel;
        g_docs.erase(it);
        if (panel != R_INVALID_ID && Rwidget_is_valid(panel))
            documents::Close(panel);
    }

    static bool OwnsPanel(Rid panel)
    {
        return std::any_of(g_docs.begin(), g_docs.end(), [panel](const auto& d) { return d->panel == panel; });
    }

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
        eui::Ask(g.window, "Unsaved Voxel Assembly", doc->path.filename().string() + " was changed. Save it before closing ?",
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
        const auto time = vfs::last_write_time(doc.path, ec);
        if (ec || time == doc.write_time)
            return;
        doc.write_time = time;
        if (doc.dirty || doc.touched)
            doc.changed_on_disk = true;
        else
        {
            LoadDoc(doc);
            SyncHeader(doc);
            Status(doc, "Reloaded (changed on disk)");
        }
    }

    static void UpdateDoc(Doc& doc)
    {
        if (doc.panel == R_INVALID_ID || !Rwidget_is_valid(doc.panel))
            return;

        if (doc.lists_dirty)
            RebuildLists(doc);
        if (doc.details_dirty)
            RebuildDetails(doc);

        // Input Settings may have changed : the menus of the blocks follow.
        if (Rget_time() - doc.options_time > 2.0)
        {
            doc.options_time = Rget_time();
            SyncOptions(doc, false);
        }

        // One undo step once nothing is held / typed.
        if (doc.touched && !ein::Mouse(0) && !ein::WantTextInput())
            Commit(doc);

        CheckDisk(doc);
        eui::SetVisible(doc.disk, doc.changed_on_disk);
        eui::SetEnabled(doc.undo_button, !doc.undo.empty() || doc.touched);
        eui::SetEnabled(doc.redo_button, !doc.redo.empty());

        const std::string title = doc.path.filename().string() + (doc.dirty || doc.touched ? " *" : "");
        if (title != doc.shown_title)
        {
            doc.shown_title = title;
            documents::SetTitle(doc.panel, title);
        }
        if (doc.status_time > 0.0 && Rget_time() - doc.status_time > 6.0)
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
// lynx::editor::vasm_editor (VoxelAssemblyEditor.h)
// =============================================================================

namespace lynx::editor::vasm_editor
{
    namespace ev = editor_ui::vasm_ed;

    bool CanOpen(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".vasm";
    }

    void Open(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
        for (auto& doc : ev::g_docs)
            if (doc->path == absolute || std::filesystem::equivalent(doc->path, absolute, ec))
            {
                editor_ui::documents::Show(doc->panel);
                return;
            }
        if (editor_ui::g.dock == R_INVALID_ID)
            return;
        auto doc = std::make_unique<ev::Doc>();
        doc->path = absolute;
        ev::Doc& d = *doc;
        ev::g_docs.push_back(std::move(doc));
        ev::BuildPanel(d);     // the kinds first : the file uses them
        ev::LoadDoc(d);
        ev::SyncHeader(d);
        ev::UpdateDoc(d);
    }

    bool HasFocus()
    {
        const Rid focused = Rwindow_get_focused_widget(editor_ui::g.window);
        for (auto& doc : ev::g_docs)
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
        for (auto& doc : ev::g_docs)
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
        std::vector<ev::Doc*> gone;
        for (auto& doc : ev::g_docs)
        {
            std::filesystem::path relative;
            if (RelativeInside(doc->path, target, relative))
                gone.push_back(doc.get());
        }
        for (ev::Doc* doc : gone)
            ev::CloseDoc(doc);
        if (CanOpen(target))
            lynx::vasm::PrepareClasses();   // its class goes
    }

    bool HasUnsavedChanges()
    {
        for (auto& doc : ev::g_docs)
        {
            ev::Commit(*doc);
            if (doc->dirty)
                return true;
        }
        return false;
    }

    void SaveAll()
    {
        for (auto& doc : ev::g_docs)
        {
            ev::Commit(*doc);
            if (doc->dirty)
                ev::SaveDoc(*doc);
        }
    }

    std::string NewFileTemplate(const std::string& class_name)
    {
        const std::string name = ev::Identifier(class_name, "MyActor");
        return
            "lynx-vasm 1\n"
            "class " + name + "\n"
            "extends Actor\n"
            "graph event Event Graph\n"
            "(script 40 40\n"
            "  (block \"ev.start\" ())\n"
            "  (block \"wd.print\" (\"Hello from " + name + "!\")))\n"
            "(script 40 220\n"
            "  (block \"ev.tick\" ()))\n"
            "end\n";
    }

    void RenameClassInCopy(const std::filesystem::path& copy, const std::string& new_class_name)
    {
        std::ifstream in(copy, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const size_t at = text.find("\nclass ");
        if (at == std::string::npos)
            return;
        const size_t end = text.find('\n', at + 1);
        text.replace(at + 1, (end == std::string::npos ? text.size() : end) - at - 1, "class " + ev::Identifier(new_class_name, "MyActor"));
        std::ofstream out(copy, std::ios::binary | std::ios::trunc);
        out << text;
    }
}
