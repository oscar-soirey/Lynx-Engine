// =============================================================================
// Block Editor in Raven (.blocks files, Scratch-like)
// (included by raven/EditorUi.inl ; API : ../BlockEditor.h)
// -----------------------------------------------------------------------------
// One docked panel per open file, a tab next to the Viewport :
//
//   +---------------------------------------------------+---------------+
//   | Save Undo Redo | class [Hero] extends [Humanoid]  |  VARIABLES    |
//   |---------+-----------------------------------------|  score  [0]   |
//   | palette |                                         |  + Variable   |
//   | Events  |   when the game starts                  |  + Block      |
//   | Control |   forever                               |---------------|
//   | Motion  |     walk (axis MoveRight)               |  GENERATED    |
//   | ...     |                                         |  JAVASCRIPT   |
//   +---------+-----------------------------------------+---------------+
//
// The blocks are the Raven BlockEditor widget (raven_tools.h). The file keeps
// the class, the variables and the scripts ; each save writes the JavaScript
// class next to it (Hero.blocks -> Hero.blocks.js) and reloads the scripts :
// the engine knows nothing about blocks, the class is an ordinary JS class.
//
// Like Scratch :
// - a script starts with a hat (when the game starts, every frame, when I
//   receive...) ; several scripts run side by side ;
// - wait, repeat, forever, wait until... let a frame pass : the scripts are
//   async methods, a small scheduler in the class wakes them each Update ;
// - "My Blocks" : define a block (with inputs), then use it like the others ;
// - variables : of the actor (editable in Details, saved with the level) or
//   global (shared by every actor).
// =============================================================================

#include "../BlockEditor.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace editor_ui::blocks
{
    namespace bfs = std::filesystem;

    // =========================================================================
    // Kinds of blocks
    // =========================================================================
    //
    // code : the JavaScript, {0} {1}... the slots (converted by their type :
    // %n a number, %s a text, %b a condition, %r any value), {raw0} the typed
    // text as is. An empty code : written by Generator (loops, hats...).

    struct KindDef
    {
        const char* name;
        const char* label;
        RBlockShape shape;
        const char* category;
        const char* code;
        const char* help;
    };

    constexpr RBlockShape kHat = R_BLOCK_HAT;
    constexpr RBlockShape kStep = R_BLOCK_STATEMENT;
    constexpr RBlockShape kCap = R_BLOCK_CAP;
    constexpr RBlockShape kWrap = R_BLOCK_C;
    constexpr RBlockShape kValue = R_BLOCK_REPORTER;
    constexpr RBlockShape kCond = R_BLOCK_BOOLEAN;

    static const KindDef kKinds[] = {
        // ---- Events -----------------------------------------------------------------
        { "ev.start", "when the game starts", kHat, "Events", "", "Runs once, when the actor begins to play (BeginPlay)." },
        { "ev.tick", "every frame", kHat, "Events", "", "Runs at each frame (Update). Waits inside : the next run starts once this one is finished." },
        { "ev.end", "when the game ends", kHat, "Events", "", "Runs when the actor leaves the game (EndPlay) : no waits here." },
        { "ev.key", "when action %s{Jump} is pressed", kHat, "Events", "", "An action of Project Settings > Input." },
        { "ev.overlap", "when something touches me", kHat, "Events", "", "Another collider starts to overlap mine (OnBeginOverlap). \"other actor\" is it." },
        { "ev.overlap_end", "when something stops touching me", kHat, "Events", "", "OnEndOverlap. \"other actor\" is it." },
        { "ev.hit", "when I bump into something", kHat, "Events", "", "My blocking collider hit an actor or a wall (OnHit). \"other actor\" is it (nothing for a wall)." },
        { "ev.landed", "when I land", kHat, "Events", "", "Humanoid : back on the ground (OnLanded)." },
        { "ev.jumped", "when I jump", kHat, "Events", "", "Humanoid : a jump started (OnJumped)." },
        { "ev.receive", "when I receive %s{message1}", kHat, "Events", "", "A broadcast of any actor of the level." },
        { "ev.broadcast", "broadcast %s{message1}", kStep, "Events", "this.__broadcast({0});", "Every actor made of blocks runs its \"when I receive\" scripts." },
        { "ev.broadcast_wait", "broadcast %s{message1} and wait", kStep, "Events", "await this.__broadcast({0});", "Same, then waits until those scripts are finished." },

        // ---- Control ----------------------------------------------------------------
        { "ctl.wait", "wait %n{1} seconds", kStep, "Control", "await this.__wait({0});", "Game time (follows the time dilation)." },
        { "ctl.repeat", "repeat %n{10}", kWrap, "Control", "", "One turn per frame, like Scratch." },
        { "ctl.forever", "forever", kWrap, "Control", "", "One turn per frame, until the actor is destroyed or the script stopped." },
        { "ctl.if", "if %b then", kWrap, "Control", "", "" },
        { "ctl.ifelse", "if %b then|else", kWrap, "Control", "", "" },
        { "ctl.wait_until", "wait until %b", kStep, "Control", "", "Checked once per frame." },
        { "ctl.repeat_until", "repeat until %b", kWrap, "Control", "", "" },
        { "ctl.while", "while %b", kWrap, "Control", "", "" },
        { "ctl.for", "for %s{i} from %n{1} to %n{10}", kWrap, "Control", "", "Counts with a variable (one turn per frame)." },
        { "ctl.stop", "stop this script", kCap, "Control", "return;", "" },
        { "ctl.stop_all", "stop all my scripts", kCap, "Control", "this.__stopAll(); return;", "Every script of this actor stops." },
        { "ctl.js", "run JavaScript %s{print(this.id)}", kStep, "Control", "{raw0};", "Anything of the Lynx API (this : the actor)." },
        { "ctl.js_value", "JavaScript %s{Math.PI}", kValue, "Control", "({raw0})", "The value of a JavaScript expression (this : the actor)." },

        // ---- Motion -----------------------------------------------------------------
        { "mo.change_x", "change x by %n{1}", kStep, "Motion", "this.position.x += {0};", "" },
        { "mo.set_x", "set x to %n{0}", kStep, "Motion", "this.position.x = {0};", "" },
        { "mo.change_y", "change y by %n{1}", kStep, "Motion", "this.position.y += {0};", "" },
        { "mo.set_y", "set y to %n{0}", kStep, "Motion", "this.position.y = {0};", "" },
        { "mo.goto", "go to x %n{0} y %n{0}", kStep, "Motion", "this.position.x = {0}; this.position.y = {1};", "" },
        { "mo.goto_actor", "go to %r", kStep, "Motion", "this.__goto({0});", "Onto an actor (\"other actor\", \"first actor with tag\"...)." },
        { "mo.glide", "glide %n{1} secs to x %n{0} y %n{0}", kStep, "Motion", "await this.__glide({1}, {2}, {0});", "" },
        { "mo.toward", "move toward %r speed %n{3}", kStep, "Motion", "this.__moveToward({0}, {1});", "One step of this frame (put it in a forever / every frame)." },
        { "mo.set_vel", "set velocity x %n{0} y %n{0}", kStep, "Motion", "this.__setVel({0}, {1});", "Units per second ; stops against the walls when the actor has a collider." },
        { "mo.set_vel_x", "set velocity x to %n{0}", kStep, "Motion", "this.__setVel({0}, null);", "" },
        { "mo.set_vel_y", "set velocity y to %n{0}", kStep, "Motion", "this.__setVel(null, {0});", "" },
        { "mo.change_vel", "change velocity by x %n{0} y %n{0}", kStep, "Motion", "this.__addVel({0}, {1});", "" },
        { "mo.x", "x position", kValue, "Motion", "this.position.x", "" },
        { "mo.y", "y position", kValue, "Motion", "this.position.y", "" },
        { "mo.vx", "velocity x", kValue, "Motion", "this.__vel().x", "" },
        { "mo.vy", "velocity y", kValue, "Motion", "this.__vel().y", "" },

        // ---- Character (Humanoid) ----------------------------------------------------
        { "ch.walk", "walk %n{1}", kStep, "Character", "this.Move?.({0});", "Humanoid : -1 left, 1 right (each frame : in a forever / every frame)." },
        { "ch.jump", "jump", kStep, "Character", "this.Jump?.();", "Humanoid." },
        { "ch.stop_jump", "stop jumping", kStep, "Character", "this.StopJumping?.();", "Humanoid : a short jump when the button is released." },
        { "ch.launch", "launch x %n{0} y %n{10}", kStep, "Character", "this.Launch?.({0}, {1});", "Humanoid : a push (jump pad, knock back)." },
        { "ch.stop", "stop moving", kStep, "Character", "this.StopMovement?.();", "Humanoid." },
        { "ch.grounded", "on the ground?", kCond, "Character", "(!!this.IsGrounded?.())", "Humanoid." },
        { "ch.falling", "falling?", kCond, "Character", "(!!this.IsFalling?.())", "Humanoid." },
        { "ch.facing_right", "facing right?", kCond, "Character", "(!!this.IsFacingRight?.())", "Humanoid." },

        // ---- Looks ------------------------------------------------------------------
        { "lk.show", "show", kStep, "Looks", "this.__look(true);", "" },
        { "lk.hide", "hide", kStep, "Looks", "this.__look(false);", "" },
        { "lk.image", "set image to %s{sprite.png}", kStep, "Looks", "this.__image({0});", "A StaticSprite (added when the actor has none)." },
        { "lk.anim", "play animation %s{run.png} frames %n{8} every %n{0.1} s", kStep, "Looks", "this.__anim({0}, {1}, {2});", "A horizontal strip of frames (AnimationSprite)." },
        { "lk.graph", "use anim graph %s{anim/Hero.animgraph}", kStep, "Looks", "this.__graph({0});", "An Anim Graph of the project." },
        { "lk.param", "set anim %s{speed} to %r{0}", kStep, "Looks", "this.__animSet({0}, {1});", "A parameter of the Anim Graph." },
        { "lk.trigger", "trigger anim %s{hurt}", kStep, "Looks", "this.__sprite()?.trigger?.({0});", "A trigger of the Anim Graph." },
        { "lk.face", "face %s{right}", kStep, "Looks", "this.__face({0});", "left or right (flips the sprite)." },
        { "lk.size", "set size to %n{100} %", kStep, "Looks", "this.__size({0});", "" },
        { "lk.state", "animation state", kValue, "Looks", "(this.__sprite()?.currentState ?? \"\")", "" },

        // ---- Sound ------------------------------------------------------------------
        { "snd.play", "play sound %s{jump.wav}", kStep, "Sound", "this.__sound({0});", "" },
        { "snd.stop", "stop sound", kStep, "Sound", "this.getComponent(\"SoundSource\")?.stop();", "" },
        { "snd.volume", "set volume to %n{100} %", kStep, "Sound", "this.__soundSource().volume = {0} / 100;", "" },

        // ---- Sensing ----------------------------------------------------------------
        { "se.pressed", "action %s{Jump} pressed?", kCond, "Sensing", "Input.pressed({0})", "Went down this frame." },
        { "se.held", "action %s{Jump} held?", kCond, "Sensing", "Input.held({0})", "" },
        { "se.released", "action %s{Jump} released?", kCond, "Sensing", "Input.released({0})", "" },
        { "se.axis", "axis %s{MoveRight}", kValue, "Sensing", "Input.axis({0})", "An axis of Project Settings > Input (-1 to 1)." },
        { "se.touching", "touching tag %s{enemy}?", kCond, "Sensing", "this.__touching({0})", "My collider overlaps an actor with this tag (empty : any actor)." },
        { "se.has_tag", "%r has tag %s{player}?", kCond, "Sensing", "this.__hasTag({0}, {1})", "" },
        { "se.distance", "distance to %r", kValue, "Sensing", "this.__dist({0})", "" },
        { "se.of", "%s{x} of %r", kValue, "Sensing", "this.__of({0}, {1})", "x, y, z, id, class, or a property (hp...) of an actor." },
        { "se.timer", "timer", kValue, "Sensing", "(this.__time - this.__timer0)", "Seconds since the start (or reset timer)." },
        { "se.reset_timer", "reset timer", kStep, "Sensing", "this.__timer0 = this.__time;", "" },

        // ---- Operators --------------------------------------------------------------
        { "op.add", "%n + %n", kValue, "Operators", "({0} + {1})", "" },
        { "op.sub", "%n - %n", kValue, "Operators", "({0} - {1})", "" },
        { "op.mul", "%n * %n", kValue, "Operators", "({0} * {1})", "" },
        { "op.div", "%n / %n", kValue, "Operators", "({0} / {1})", "" },
        { "op.rand", "pick random %n{1} to %n{10}", kValue, "Operators", "this.__rand({0}, {1})", "Whole numbers when both ends are whole." },
        { "op.gt", "%r > %r{50}", kCond, "Operators", "this.__lt({1}, {0})", "" },
        { "op.lt", "%r < %r{50}", kCond, "Operators", "this.__lt({0}, {1})", "" },
        { "op.eq", "%r = %r{50}", kCond, "Operators", "this.__eq({0}, {1})", "Numbers, or texts (upper / lower case alike)." },
        { "op.and", "%b and %b", kCond, "Operators", "({0} && {1})", "" },
        { "op.or", "%b or %b", kCond, "Operators", "({0} || {1})", "" },
        { "op.not", "not %b", kCond, "Operators", "(!{0})", "" },
        { "op.join", "join %s{apple } %s{banana}", kValue, "Operators", "({0} + {1})", "" },
        { "op.letter", "letter %n{1} of %s{apple}", kValue, "Operators", "{1}.charAt({0} - 1)", "" },
        { "op.length", "length of %s{apple}", kValue, "Operators", "{0}.length", "" },
        { "op.contains", "%s{apple} contains %s{a}?", kCond, "Operators", "{0}.toLowerCase().includes({1}.toLowerCase())", "" },
        { "op.mod", "%n mod %n", kValue, "Operators", "this.__mod({0}, {1})", "" },
        { "op.round", "round %n", kValue, "Operators", "Math.round({0})", "" },
        { "op.math", "%s{abs} of %n", kValue, "Operators", "this.__math({0}, {1})", "abs, floor, ceiling, sqrt, sin, cos, tan (degrees), asin, acos, atan, ln, log, e ^, 10 ^." },

        // ---- Variables (and one reporter per variable : var.<name>) -------------------
        { "var.set", "set %s{score} to %r{0}", kStep, "Variables", "", "" },
        { "var.change", "change %s{score} by %n{1}", kStep, "Variables", "", "" },
        { "var.get", "variable %s{score}", kValue, "Variables", "", "A variable by its name." },

        // ---- Actors -----------------------------------------------------------------
        { "ac.me", "myself", kValue, "Actors", "this", "" },
        { "ac.other", "other actor", kValue, "Actors", "this.__other", "The actor of the last touch / bump." },
        { "ac.find", "actor %s{id}", kValue, "Actors", "Level.find({0})", "By its id (Details)." },
        { "ac.with_tag", "first actor with tag %s{player}", kValue, "Actors", "this.__first(Level.findWithTag({0}))", "" },
        { "ac.count", "number of %s{Enemy}", kValue, "Actors", "Level.count({0})", "Actors of a class." },
        { "ac.spawn", "spawn %s{Enemy} at x %n{0} y %n{0}", kStep, "Actors", "Level.spawn({0}, { x: {1}, y: {2}, z: 0 });", "A class of Place Actors (C++, JavaScript or blocks)." },
        { "ac.spawn_here", "spawn %s{Bullet} here", kStep, "Actors", "this.__spawnHere({0});", "" },
        { "ac.destroy", "destroy myself", kCap, "Actors", "this.destroy(); return;", "" },
        { "ac.destroy_other", "destroy %r", kStep, "Actors", "this.__destroy({0});", "" },
        { "ac.lifetime", "destroy myself in %n{2} seconds", kStep, "Actors", "this.lifetime = {0};", "" },
        { "ac.tag", "add tag %s{enemy}", kStep, "Actors", "this.addTag({0});", "" },
        { "ac.untag", "remove tag %s{enemy}", kStep, "Actors", "this.removeTag({0});", "" },
        { "ac.set_of", "set %s{hp} of %r to %r{0}", kStep, "Actors", "this.__setOf({0}, {1}, {2});", "x, y, z, or a property of an actor." },
        { "ac.call", "call %s{TakeDamage} of %r with %r{1}", kStep, "Actors", "await this.__call({1}, {0}, {2});", "A function of a class (C++, JavaScript, My Blocks)." },
        { "ac.collider", "add collider %n{1} by %n{1}", kStep, "Actors", "this.__collider({0}, {1}, false);", "Blocks the other colliders and the walls." },
        { "ac.trigger", "add trigger zone %n{1} by %n{1}", kStep, "Actors", "this.__collider({0}, {1}, true);", "Overlaps without blocking (when something touches me)." },

        // ---- World ------------------------------------------------------------------
        { "wd.print", "print %r{Hello!}", kStep, "World", "print({0});", "In the Console." },
        { "wd.shake", "shake the camera %n{1}", kStep, "World", "Engine.cameraShake({0});", "" },
        { "wd.slowmo", "set time dilation to %n{1}", kStep, "World", "Engine.setTimeDilation({0});", "1 : normal, 0.5 : slow motion." },
        { "wd.slowmo_for", "set time dilation to %n{0.3} for %n{0.5} s", kStep, "World", "Engine.setTimeDilation({0}, {1});", "" },
        { "wd.widget", "show widget %s{ui/HUD.widget}", kStep, "World", "UI.create({0});", "A widget of the Widget Editor." },

        // ---- My Blocks (and one block per definition : call.<name>.<inputs>) ----------
        { "fn.define", "define %s{my block} inputs %s{}", kHat, "My Blocks", "", "A block of your own. Inputs : names separated by commas (\"amount, target\")." },
        { "fn.arg", "input %s{amount}", kValue, "My Blocks", "", "An input of the block being defined." },
    };

    struct CategoryColor { const char* name; int rgb; };
    static const CategoryColor kCategoryColors[] = {
        { "Events", 0xFFBF00 }, { "Control", 0xFFAB19 }, { "Motion", 0x4C97FF }, { "Character", 0x3D6FD9 },
        { "Looks", 0x9966FF }, { "Sound", 0xCF63CF }, { "Sensing", 0x5CB1D6 }, { "Operators", 0x59C059 },
        { "Variables", 0xFF8C1A }, { "Actors", 0x0FBD8C }, { "World", 0x2E9E8F }, { "My Blocks", 0xFF6680 },
    };

    const char* const kParents[] = {
        "Actor", "Humanoid", "ColliderActor", "SpriteActor", "SoundActor", "Light2DActor", "PointLightActor", "ParticleActor",
    };

    static const KindDef* FindKind(const std::string& name)
    {
        for (const KindDef& k : kKinds)
            if (name == k.name)
                return &k;
        return nullptr;
    }

    // Slot types of a label : "%n + %n" -> "nn".
    static std::string SlotTypes(const char* label)
    {
        std::string types;
        for (const char* c = label; *c; ++c)
            if (c[0] == '%' && (c[1] == 'n' || c[1] == 's' || c[1] == 'b' || c[1] == 'r'))
                types += c[1];
        return types;
    }

    // =========================================================================
    // Names
    // =========================================================================

    static bool IsIdentifier(const std::string& s)
    {
        if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0])))
            return false;
        for (char c : s)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$')
                return false;
        return true;
    }

    static std::string Identifier(const std::string& text, const char* fallback = "block")
    {
        std::string out;
        for (char c : text)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$')
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

    static std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return {};
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static std::vector<std::string> SplitInputs(const std::string& text)
    {
        std::vector<std::string> out;
        std::string part;
        std::stringstream ss(text);
        while (std::getline(ss, part, ','))
        {
            part = Trim(part);
            if (!part.empty())
                out.push_back(Identifier(part, "input"));
        }
        return out;
    }

    // A label may not hold the characters of the label syntax.
    static std::string LabelSafe(const std::string& text)
    {
        std::string out;
        for (char c : text)
            out += (c == '%' || c == '|' || c == '{' || c == '}') ? ' ' : c;
        return out;
    }

    static std::string JsString(const std::string& s)
    {
        std::string out = "\"";
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                }
                else
                    out += static_cast<char>(c);
            }
        }
        return out + "\"";
    }

    // "12", " -0.5 " -> a JavaScript number ; false when it is not a number.
    static bool JsNumber(const std::string& text, std::string& out)
    {
        const std::string t = Trim(text);
        if (t.empty())
            return false;
        char* end = nullptr;
        const double v = std::strtod(t.c_str(), &end);
        if (end != t.c_str() + t.size() || !std::isfinite(v))
            return false;
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.15g", v);
        out = buffer;
        if (out[0] == '-')
            out = "(" + out + ")";
        return true;
    }

    // Default value of a variable : a number, true / false, or a text.
    static std::string JsLiteral(const std::string& text)
    {
        std::string number;
        if (JsNumber(text, number))
            return number;
        const std::string t = Trim(text);
        if (t == "true" || t == "false")
            return t;
        return JsString(text);
    }

    static std::string Value(Rid editor, int block, int slot)
    {
        const char* v = Rblock_editor_get_value(editor, block, slot);
        return v ? v : "";
    }

    // =========================================================================
    // The document
    // =========================================================================

    struct Var
    {
        std::string name;
        std::string value = "0";
        bool global = false;
    };

    struct Doc
    {
        bfs::path path;
        std::string class_name = "MyActor";
        std::string parent = "Actor";
        std::vector<Var> vars;

        std::string saved, snapshot;
        std::vector<std::string> undo, redo;
        bool dirty = false, touched = false;
        bfs::file_time_type write_time{};
        double disk_check = 0.0;
        bool changed_on_disk = false;

        Rid panel = R_INVALID_ID, editor = R_INVALID_ID, class_input = R_INVALID_ID, parent_combo = R_INVALID_ID;
        Rid vars_box = R_INVALID_ID, code = R_INVALID_ID, status = R_INVALID_ID, disk = R_INVALID_ID;
        Rid undo_button = R_INVALID_ID, redo_button = R_INVALID_ID;
        std::string shown_title, shown_code;
        double status_time = -100.0;
        bool building = false;
        bool vars_dirty = true, code_dirty = true, kinds_dirty = false;
        std::set<std::string> kinds;   // kinds added for this file (variables, My Blocks)
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
        std::cout << "[BLOCKS] " << text << "\n";
    }

    static bfs::path GeneratedPath(const bfs::path& blocks_file)
    {
        bfs::path js = blocks_file;
        js += ".js";
        return js;
    }

    static const Var* FindVar(const Doc& doc, const std::string& name)
    {
        for (const Var& v : doc.vars)
            if (v.name == name)
                return &v;
        return nullptr;
    }

    // ---- dynamic kinds : a reporter per variable, a block per definition ----------

    static void AddKind(Doc& doc, const std::string& name, const std::string& label, RBlockShape shape, const char* category)
    {
        if (doc.editor == R_INVALID_ID || doc.kinds.count(name) || FindKind(name))
            return;
        if (Rblock_editor_add_kind(doc.editor, name.c_str(), label.c_str(), shape, category) >= 0)
            doc.kinds.insert(name);
    }

    static std::string CallKindName(const std::string& ident, size_t inputs)
    {
        return "call." + ident + "." + std::to_string(inputs);
    }

    static std::string CallLabel(const std::string& display, size_t inputs)
    {
        std::string label = LabelSafe(display.empty() ? std::string("my block") : display);
        for (size_t i = 0; i < inputs; ++i)
            label += " %r";
        return label;
    }

    static void EnsureVarKinds(Doc& doc)
    {
        for (const Var& v : doc.vars)
            AddKind(doc, "var." + v.name, v.name, R_BLOCK_REPORTER, "Variables");
    }

    // The kinds a saved text uses (before Rblock_editor_load : an unknown kind stops it).
    static void EnsureKindsOfText(Doc& doc, const std::string& text)
    {
        // Definitions : (block "fn.define" ("name" "inputs"))
        std::map<std::string, std::string> displays;   // ident -> display name
        const std::string define = "(block \"fn.define\" (\"";
        for (size_t at = text.find(define); at != std::string::npos; at = text.find(define, at + 1))
        {
            const size_t start = at + define.size();
            const size_t end = text.find('"', start);
            if (end == std::string::npos)
                break;
            const std::string display = text.substr(start, end - start);
            displays[Identifier(display)] = display;
        }

        const std::string marker = "(block \"";
        for (size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at + 1))
        {
            const size_t start = at + marker.size();
            const size_t end = text.find('"', start);
            if (end == std::string::npos)
                break;
            const std::string name = text.substr(start, end - start);
            if (FindKind(name) || doc.kinds.count(name))
                continue;
            if (name.rfind("var.", 0) == 0)
                AddKind(doc, name, name.substr(4), R_BLOCK_REPORTER, "Variables");
            else if (name.rfind("call.", 0) == 0)
            {
                const size_t dot = name.rfind('.');
                const std::string ident = name.substr(5, dot > 5 ? dot - 5 : 0);
                const size_t inputs = dot != std::string::npos ? std::strtoul(name.c_str() + dot + 1, nullptr, 10) : 0;
                auto it = displays.find(ident);
                AddKind(doc, name, CallLabel(it != displays.end() ? it->second : ident, inputs), R_BLOCK_STATEMENT, "My Blocks");
            }
        }
    }

    // A new definition (or new inputs) : its block appears in the palette.
    static void EnsureCallKinds(Doc& doc)
    {
        const int count = Rblock_editor_get_script_count(doc.editor);
        for (int i = 0; i < count; ++i)
        {
            const int top = Rblock_editor_get_script(doc.editor, i);
            const char* kind = Rblock_editor_get_kind(doc.editor, top);
            if (!kind || std::strcmp(kind, "fn.define") != 0)
                continue;
            const std::string display = Value(doc.editor, top, 0);
            const size_t inputs = SplitInputs(Value(doc.editor, top, 1)).size();
            AddKind(doc, CallKindName(Identifier(display), inputs), CallLabel(display, inputs), R_BLOCK_STATEMENT, "My Blocks");
        }
    }

    // =========================================================================
    // Blocks -> JavaScript
    // =========================================================================

    struct Generator
    {
        const Doc& doc;
        Rid ed;
        int counter = 0;
        std::vector<std::string> inputs;     // of the definition being written
        bool uses_touch = false, uses_collider = false;

        Generator(const Doc& d, Rid e) : doc(d), ed(e) {}

        std::string Kind(int block) const
        {
            const char* k = Rblock_editor_get_kind(ed, block);
            return k ? k : "";
        }

        std::string Raw(int block, int slot) const { return Value(ed, block, slot); }

        std::string Types(int block) const
        {
            const std::string kind = Kind(block);
            if (const KindDef* k = FindKind(kind))
                return SlotTypes(k->label);
            if (kind.rfind("call.", 0) == 0)
                return std::string(static_cast<size_t>(Rblock_editor_get_slot_count(ed, block)), 'r');
            return {};
        }

        // A slot, converted by its type.
        std::string Slot(int block, int slot, char type)
        {
            const int input = Rblock_editor_get_input(ed, block, slot);
            if (input > 0)
            {
                const std::string e = Expr(input);
                switch (type)
                {
                case 'n': return "this.__n(" + e + ")";
                case 's': return "String(" + e + ")";
                case 'b': return "(!!" + e + ")";
                default: return e;
                }
            }
            const std::string raw = Raw(block, slot);
            std::string number;
            switch (type)
            {
            case 'n': return JsNumber(raw, number) ? number : "0";
            case 's': return JsString(raw);
            case 'b': return "false";
            default: return JsNumber(raw, number) ? number : JsString(raw);
            }
        }

        std::string Fill(int block, const std::string& code)
        {
            const std::string types = Types(block);
            std::string out;
            for (size_t i = 0; i < code.size(); ++i)
            {
                if (code[i] == '{')
                {
                    const size_t close = code.find('}', i);
                    if (close != std::string::npos)
                    {
                        const std::string key = code.substr(i + 1, close - i - 1);
                        if (key.rfind("raw", 0) == 0 && key.size() > 3 && std::isdigit(static_cast<unsigned char>(key[3])))
                        {
                            out += Raw(block, std::atoi(key.c_str() + 3));
                            i = close;
                            continue;
                        }
                        if (!key.empty() && std::all_of(key.begin(), key.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
                        {
                            const int slot = std::atoi(key.c_str());
                            out += Slot(block, slot, slot < static_cast<int>(types.size()) ? types[slot] : 'r');
                            i = close;
                            continue;
                        }
                    }
                }
                out += code[i];
            }
            return out;
        }

        // A variable by its name : an input of the definition, a global, or the actor's.
        std::string VarRef(const std::string& raw) const
        {
            const std::string name = Trim(raw);
            const std::string ident = Identifier(name, "input");
            if (std::find(inputs.begin(), inputs.end(), ident) != inputs.end())
                return "a_" + ident;
            const Var* v = FindVar(doc, name);
            if (v && v->global)
                return "LynxVars[" + JsString(name) + "]";
            if (IsIdentifier(name))
                return "this." + name;
            return "this[" + JsString(name) + "]";
        }

        std::string Expr(int block)
        {
            const std::string kind = Kind(block);
            if (kind.rfind("var.", 0) == 0 && kind != "var.set" && kind != "var.change" && kind != "var.get")
                return VarRef(kind.substr(4));
            if (kind == "var.get")
                return VarRef(Raw(block, 0));
            if (kind == "fn.arg")
            {
                const std::string ident = Identifier(Trim(Raw(block, 0)), "input");
                return std::find(inputs.begin(), inputs.end(), ident) != inputs.end() ? "a_" + ident : "undefined";
            }
            if (kind == "se.touching")
                uses_touch = true;
            if (const KindDef* k = FindKind(kind))
                if (k->code[0])
                    return Fill(block, k->code);
            return "undefined";
        }

        static std::string Pad(int indent) { return std::string(static_cast<size_t>(indent) * 4, ' '); }

        void Line(std::string& out, int indent, const std::string& text)
        {
            out += Pad(indent) + text + "\n";
        }

        void Chain(int first, int indent, std::string& out)
        {
            for (int b = first; b > 0; b = Rblock_editor_get_next(ed, b))
                Stmt(b, indent, out);
        }

        void Body(int block, int mouth, int indent, std::string& out)
        {
            Chain(Rblock_editor_get_body(ed, block, mouth), indent, out);
        }

        void Stmt(int block, int indent, std::string& out)
        {
            const std::string kind = Kind(block);
            const std::string loop = "__i" + std::to_string(++counter);

            if (kind == "ctl.repeat")
            {
                Line(out, indent, "for (let " + loop + " = Math.round(" + Slot(block, 0, 'n') + "); " + loop + " > 0; " + loop + "--) {");
                Body(block, 0, indent + 1, out);
                Line(out, indent + 1, "await this.__frame();");
                Line(out, indent, "}");
            }
            else if (kind == "ctl.forever")
            {
                Line(out, indent, "for (;;) {");
                Body(block, 0, indent + 1, out);
                Line(out, indent + 1, "await this.__frame();");
                Line(out, indent, "}");
            }
            else if (kind == "ctl.if" || kind == "ctl.ifelse")
            {
                Line(out, indent, "if (" + Slot(block, 0, 'b') + ") {");
                Body(block, 0, indent + 1, out);
                if (kind == "ctl.ifelse")
                {
                    Line(out, indent, "} else {");
                    Body(block, 1, indent + 1, out);
                }
                Line(out, indent, "}");
            }
            else if (kind == "ctl.wait_until")
            {
                Line(out, indent, "while (!(" + Slot(block, 0, 'b') + ")) await this.__frame();");
            }
            else if (kind == "ctl.repeat_until" || kind == "ctl.while")
            {
                const std::string cond = Slot(block, 0, 'b');
                Line(out, indent, kind == "ctl.while" ? "while (" + cond + ") {" : "while (!(" + cond + ")) {");
                Body(block, 0, indent + 1, out);
                Line(out, indent + 1, "await this.__frame();");
                Line(out, indent, "}");
            }
            else if (kind == "ctl.for")
            {
                const std::string var = VarRef(Raw(block, 0));
                const std::string last = loop + "_end";
                Line(out, indent, "for (let " + last + " = " + Slot(block, 2, 'n') + ", " + loop + " = " + Slot(block, 1, 'n') + "; " +
                                      loop + " <= " + last + "; " + loop + "++) {");
                Line(out, indent + 1, var + " = " + loop + ";");
                Body(block, 0, indent + 1, out);
                Line(out, indent + 1, "await this.__frame();");
                Line(out, indent, "}");
            }
            else if (kind == "var.set")
            {
                Line(out, indent, VarRef(Raw(block, 0)) + " = " + Slot(block, 1, 'r') + ";");
            }
            else if (kind == "var.change")
            {
                const std::string var = VarRef(Raw(block, 0));
                Line(out, indent, var + " = this.__n(" + var + ") + " + Slot(block, 1, 'n') + ";");
            }
            else if (kind.rfind("call.", 0) == 0)
            {
                const size_t dot = kind.rfind('.');
                const std::string ident = kind.substr(5, dot > 5 ? dot - 5 : 0);
                std::string args;
                const int slots = Rblock_editor_get_slot_count(ed, block);
                for (int i = 0; i < slots; ++i)
                    args += (i ? ", " : "") + Slot(block, i, 'r');
                Line(out, indent, "await this.__f_" + ident + "(" + args + ");");
            }
            else if (const KindDef* k = FindKind(kind); k && k->code[0] && (k->shape == R_BLOCK_STATEMENT || k->shape == R_BLOCK_CAP))
            {
                if (kind == "ac.collider" || kind == "ac.trigger")
                    uses_collider = true;
                Line(out, indent, Fill(block, k->code));
            }
            else if (!kind.empty())
            {
                Line(out, indent, "// (" + kind + " : not a step)");
            }
        }
    };

    // "when I receive %s{message1}" -> "when I receive coin" (a comment of the code).
    static std::string Describe(Rid editor, int block, const char* label)
    {
        std::string out;
        int slot = 0;
        for (const char* c = label; *c; ++c)
        {
            if (c[0] == '%' && (c[1] == 'n' || c[1] == 's' || c[1] == 'b' || c[1] == 'r'))
            {
                out += Rblock_editor_get_input(editor, block, slot) > 0 ? std::string("(...)") : Value(editor, block, slot);
                ++slot;
                ++c;
                if (c[1] == '{')
                    while (c[1] && c[1] != '}')
                        ++c;
                if (c[1] == '}')
                    ++c;
                continue;
            }
            out += *c == '|' ? ' ' : *c;
        }
        for (char& ch : out)
            if (ch == '\n' || ch == '\r')
                ch = ' ';
        return out;
    }

    struct Script
    {
        std::string hat;        // kind of the hat
        std::string method;     // __h3, __f_jump
        std::string argument;   // the message (ev.receive), the action (ev.key)
        std::string title;      // "when I receive coin"
        std::string code;
    };

    static std::string GenerateJs(const Doc& doc)
    {
        Generator gen(doc, doc.editor);
        std::vector<Script> scripts;
        std::string defines;
        int loose = 0;

        const int count = Rblock_editor_get_script_count(doc.editor);
        for (int i = 0; i < count; ++i)
        {
            const int top = Rblock_editor_get_script(doc.editor, i);
            const std::string kind = gen.Kind(top);
            const KindDef* k = FindKind(kind);
            if (!k || k->shape != R_BLOCK_HAT)
            {
                ++loose;
                continue;
            }
            if (kind == "fn.define")
            {
                const std::string display = gen.Raw(top, 0);
                gen.inputs = SplitInputs(gen.Raw(top, 1));
                std::string params;
                for (size_t p = 0; p < gen.inputs.size(); ++p)
                    params += (p ? ", " : "") + std::string("a_") + gen.inputs[p];
                std::string body;
                gen.Chain(Rblock_editor_get_next(doc.editor, top), 2, body);
                defines += "    // My Block \"" + LabelSafe(display) + "\"\n";
                defines += "    async __f_" + Identifier(display) + "(" + params + ") {\n" + body + "    }\n\n";
                gen.inputs.clear();
                continue;
            }
            Script s;
            s.hat = kind;
            s.title = Describe(doc.editor, top, k->label);
            s.method = "__h" + std::to_string(scripts.size() + 1);
            if (kind == "ev.receive" || kind == "ev.key")
                s.argument = gen.Slot(top, 0, 's');
            gen.Chain(Rblock_editor_get_next(doc.editor, top), 2, s.code);
            scripts.push_back(std::move(s));
        }

        auto hats = [&](const char* kind)
        {
            std::vector<const Script*> list;
            for (const Script& s : scripts)
                if (s.hat == kind)
                    list.push_back(&s);
            return list;
        };
        auto has = [&](const char* kind) { return !hats(kind).empty(); };

        const bool touch_events = has("ev.overlap") || has("ev.overlap_end") || has("ev.hit");
        const bool own_collider = doc.parent == "Humanoid" || doc.parent == "ColliderActor";
        const bool auto_collider = (gen.uses_touch || touch_events) && !gen.uses_collider && !own_collider;

        const std::string file = doc.path.filename().string();
        std::string js;
        js += "// Generated by the Lynx block editor from " + file + " : edit the blocks, not this file\n";
        js += "// (it is written again each time " + file + " is saved).\n";
        js += "class " + doc.class_name + " extends " + doc.parent + " {\n";

        // Variables of the actor : editable in Details, saved with the level.
        bool any_property = false;
        for (const Var& v : doc.vars)
            any_property |= !v.global;
        if (any_property)
        {
            js += "    static properties = {\n";
            for (const Var& v : doc.vars)
                if (!v.global)
                    js += "        " + (IsIdentifier(v.name) ? v.name : JsString(v.name)) + ": " + JsLiteral(v.value) + ",\n";
            js += "    };\n\n";
        }

        // ---- lifecycle -----------------------------------------------------------
        js += "    BeginPlay() {\n        this.__init();\n";
        if (auto_collider)
            js += "        if (!this.getComponent(\"Collider\")) this.addComponent(\"Collider\", { trigger: true });\n";
        for (const Script* s : hats("ev.start"))
            js += "        this.__run(\"" + s->method + "\");\n";
        js += "    }\n\n";

        js += "    Update(dt) {\n        this.__init();\n        this.__tick(dt);\n";
        for (const Script* s : hats("ev.tick"))
            js += "        if (!this.__busy." + s->method + ") this.__run(\"" + s->method + "\");\n";
        for (const Script* s : hats("ev.key"))
            js += "        if (Input.pressed(" + s->argument + ") && !this.__busy." + s->method + ") this.__run(\"" + s->method + "\");\n";
        js += "    }\n\n";

        js += "    EndPlay() {\n        this.__init();\n";
        for (const Script* s : hats("ev.end"))
            js += "        this.__run(\"" + s->method + "\");\n";
        js += "        this.__alive = false;\n        this.__waiters = [];\n    }\n\n";

        auto event = [&](const char* kind, const char* signature, const char* store)
        {
            const auto list = hats(kind);
            if (list.empty())
                return;
            js += std::string("    ") + signature + " {\n        this.__init();\n";
            if (store[0])
                js += std::string("        ") + store + "\n";
            for (const Script* s : list)
                js += "        this.__run(\"" + s->method + "\");\n";
            js += "    }\n\n";
        };
        event("ev.overlap", "OnBeginOverlap(other)", "this.__other = other;");
        event("ev.overlap_end", "OnEndOverlap(other)", "this.__other = other;");
        event("ev.hit", "OnHit(other, normal)", "this.__other = other; this.__normal = normal;");
        event("ev.landed", "OnLanded()", "");
        event("ev.jumped", "OnJumped()", "");

        // Broadcasts reach every actor that has __recv.
        js += "    __recv(message) {\n        if (!this.__ready || !this.__alive) return Promise.resolve();\n        const runs = [];\n";
        for (const Script* s : hats("ev.receive"))
            js += "        if (this.__eq(message, " + s->argument + ")) runs.push(this.__run(\"" + s->method + "\"));\n";
        js += "        return Promise.all(runs);\n    }\n\n";

        // ---- scripts ------------------------------------------------------------
        for (const Script& s : scripts)
        {
            js += "    // " + s.title + "\n";
            js += "    async " + s.method + "() {\n" + s.code + "    }\n\n";
        }
        js += defines;
        if (loose > 0)
            js += "    // " + std::to_string(loose) + " script(s) without a hat on top : not run.\n\n";

        // ---- runtime : scheduler and helpers of the blocks ----------------------------
        js += "    __init() {\n";
        js += "        if (this.__ready) return;\n";
        js += "        this.__ready = true; this.__alive = true; this.__gen = 0;\n";
        js += "        this.__waiters = []; this.__busy = {};\n";
        js += "        this.__time = 0; this.__timer0 = 0; this.__dt = 0;\n";
        js += "        this.__other = null; this.__normal = null;\n";
        js += "        if (!globalThis.LynxVars) globalThis.LynxVars = {};\n";
        for (const Var& v : doc.vars)
            if (v.global)
                js += "        if (!(" + JsString(v.name) + " in LynxVars)) LynxVars[" + JsString(v.name) + "] = " + JsLiteral(v.value) + ";\n";
        js += "    }\n";
        js += R"js(
    __run(name, ...args) {
        if (!this.__alive) return Promise.resolve();
        this.__busy[name] = true;
        const done = () => { this.__busy[name] = false; };
        let run;
        try { run = Promise.resolve(this[name](...args)); }
        catch (e) { done(); console.error(e && e.stack ? e.stack : e); return Promise.resolve(); }
        return run.then(done, e => { done(); console.error(e && e.stack ? e.stack : e); });
    }
    __frame() {
        const gen = this.__gen;
        return new Promise(resolve => this.__waiters.push({ resolve, gen }));
    }
    __tick(dt) {
        this.__dt = dt;
        this.__time += dt;
        const waiters = this.__waiters;
        this.__waiters = [];
        for (const w of waiters) if (w.gen === this.__gen && this.__alive) w.resolve();
    }
    __stopAll() { this.__gen++; this.__waiters = []; this.__busy = {}; }
    async __wait(seconds) {
        const end = this.__time + Math.max(0, this.__n(seconds));
        do { await this.__frame(); } while (this.__time < end);
    }
    async __glide(x, y, seconds) {
        const x0 = this.position.x, y0 = this.position.y, t0 = this.__time, d = Math.max(0, this.__n(seconds));
        for (;;) {
            const k = d > 0 ? Math.min(1, (this.__time - t0) / d) : 1;
            this.position.x = x0 + (x - x0) * k;
            this.position.y = y0 + (y - y0) * k;
            if (k >= 1) break;
            await this.__frame();
        }
    }
    __broadcast(message) {
        const runs = [];
        for (const a of Level.all() || []) {
            try { if (a && typeof a.__recv === "function") runs.push(a.__recv(String(message))); } catch (e) { }
        }
        return Promise.all(runs);
    }
    __n(v) { const n = Number(v); return Number.isFinite(n) ? n : 0; }
    __eq(a, b) {
        const na = Number(a), nb = Number(b);
        if (String(a).trim() !== "" && String(b).trim() !== "" && !isNaN(na) && !isNaN(nb)) return na === nb;
        return String(a).toLowerCase() === String(b).toLowerCase();
    }
    __lt(a, b) {
        const na = Number(a), nb = Number(b);
        if (!isNaN(na) && !isNaN(nb)) return na < nb;
        return String(a).toLowerCase() < String(b).toLowerCase();
    }
    __mod(a, b) { const r = a % b; return r !== 0 && (r < 0) !== (b < 0) ? r + b : r; }
    __rand(a, b) {
        const lo = Math.min(a, b), hi = Math.max(a, b);
        if (Number.isInteger(lo) && Number.isInteger(hi)) return lo + Math.floor(Math.random() * (hi - lo + 1));
        return lo + Math.random() * (hi - lo);
    }
    __math(op, v) {
        const r = Math.PI / 180;
        switch (String(op).trim().toLowerCase()) {
            case "abs": return Math.abs(v);
            case "floor": return Math.floor(v);
            case "ceiling": case "ceil": return Math.ceil(v);
            case "sqrt": return Math.sqrt(v);
            case "sin": return Math.sin(v * r);
            case "cos": return Math.cos(v * r);
            case "tan": return Math.tan(v * r);
            case "asin": return Math.asin(v) / r;
            case "acos": return Math.acos(v) / r;
            case "atan": return Math.atan(v) / r;
            case "ln": return Math.log(v);
            case "log": return Math.log10(v);
            case "e ^": case "e^": case "exp": return Math.exp(v);
            case "10 ^": case "10^": return Math.pow(10, v);
            default: return v;
        }
    }
    __first(r) { return Array.isArray(r) ? (r.length ? r[0] : null) : (r ?? null); }
    __actor(a) { return typeof a === "string" ? Level.find(a) : a; }
    __hasTag(a, tag) { a = this.__actor(a); try { return !!(a && a.hasTag(tag)); } catch (e) { return false; } }
    __dist(a) {
        a = this.__actor(a);
        if (!a) return 0;
        const dx = a.position.x - this.position.x, dy = a.position.y - this.position.y;
        return Math.sqrt(dx * dx + dy * dy);
    }
    __of(name, a) {
        a = this.__actor(a);
        if (!a) return 0;
        switch (String(name).trim()) {
            case "x": return a.position.x;
            case "y": return a.position.y;
            case "z": return a.position.z;
            case "id": return a.id;
            case "class": return a.className;
        }
        if (a[name] !== undefined) return a[name];
        return a.hasProperty && a.hasProperty(name) ? a.getProperty(name) : 0;
    }
    __setOf(name, a, v) {
        a = this.__actor(a);
        if (!a) return;
        switch (String(name).trim()) {
            case "x": a.position.x = this.__n(v); return;
            case "y": a.position.y = this.__n(v); return;
            case "z": a.position.z = this.__n(v); return;
        }
        if (a.hasProperty && a.hasProperty(name)) a.setProperty(name, v); else a[name] = v;
    }
    async __call(a, name, arg) {
        a = this.__actor(a);
        if (!a) return;
        if (typeof a[name] === "function") return await a[name](arg);
        if (typeof a["__f_" + name] === "function") return await a["__f_" + name](arg);
        if (a.call) return a.call(name, arg);
    }
    __goto(a) { a = this.__actor(a); if (a) { this.position.x = a.position.x; this.position.y = a.position.y; } }
    __moveToward(a, speed) {
        a = this.__actor(a);
        if (!a) return;
        const dx = a.position.x - this.position.x, dy = a.position.y - this.position.y;
        const d = Math.sqrt(dx * dx + dy * dy), step = this.__n(speed) * this.__dt;
        if (d <= step || d === 0) { this.position.x = a.position.x; this.position.y = a.position.y; return; }
        this.position.x += dx / d * step;
        this.position.y += dy / d * step;
    }
    __vel() { return this.velocity || { x: 0, y: 0, z: 0 }; }
    __setVel(x, y) { const v = this.__vel(); this.velocity = vec3(x ?? v.x, y ?? v.y, v.z || 0); }
    __addVel(x, y) { const v = this.__vel(); this.velocity = vec3(v.x + x, v.y + y, v.z || 0); }
    __spawnHere(name) { return Level.spawn(name, { x: this.position.x, y: this.position.y, z: this.position.z }); }
    __destroy(a) { a = this.__actor(a); try { if (a) a.destroy(); } catch (e) { } }
    __collider(w, h, trigger) {
        const c = this.getComponent("Collider") || this.addComponent("Collider");
        c.size = { x: w, y: h };
        c.trigger = trigger;
        return c;
    }
    __touching(tag) {
        const c = this.getComponent("Collider");
        if (!c) return false;
        for (const a of c.overlapping() || []) {
            try { if (a && (!tag || a.hasTag(tag))) return true; } catch (e) { }
        }
        return false;
    }
    __sprite() { return this.getComponent("AnimationSprite") || this.getComponent("StaticSprite"); }
    __look(visible) { const s = this.__sprite(); if (s) s.visible = visible; }
    __image(texture) { (this.getComponent("StaticSprite") || this.addComponent("StaticSprite")).texture = texture; }
    __anim(texture, frames, frameTime) {
        const a = this.getComponent("AnimationSprite") || this.addComponent("AnimationSprite");
        if (a.texture !== texture || a.frameCount !== frames) a.setAnimation(texture, frames, frameTime, true);
        else a.frameTime = frameTime;
    }
    __graph(asset) {
        const a = this.getComponent("AnimationSprite");
        if (a) a.loadGraph(asset); else this.addComponent("AnimationSprite", { graph: asset });
    }
    __animSet(name, v) {
        const a = this.__sprite();
        if (!a) return;
        if (typeof v === "boolean") a.setBool(name, v); else a.setFloat(name, this.__n(v));
    }
    __face(side) {
        const s = this.__sprite();
        if (s) s.flipX = String(side).trim().toLowerCase().startsWith("l");
    }
    __size(percent) {
        const k = this.__n(percent) / 100, sc = this.transform.scale;
        sc.x = (sc.x < 0 ? -1 : 1) * k;
        sc.y = k;
    }
    __soundSource() { return this.getComponent("SoundSource") || this.addComponent("SoundSource", { spatial: false }); }
    __sound(file) { const s = this.__soundSource(); if (file) s.sound = file; s.play(); }
}
)js";
        return js;
    }

    // =========================================================================
    // File : header (class, parent, variables) + the scripts of Raven
    // =========================================================================
    //
    //   lynx-blocks 1
    //   class Hero
    //   extends Humanoid
    //   var score 0
    //   global lives 3
    //   blocks
    //   (script 40 40 ...)

    static std::string Serialize(const Doc& doc)
    {
        std::string text = "lynx-blocks 1\n";
        text += "class " + doc.class_name + "\n";
        text += "extends " + doc.parent + "\n";
        for (const Var& v : doc.vars)
        {
            std::string value = v.value;
            std::replace(value.begin(), value.end(), '\n', ' ');
            text += std::string(v.global ? "global " : "var ") + v.name + " " + value + "\n";
        }
        text += "blocks\n";
        if (doc.editor != R_INVALID_ID)
            text += Rblock_editor_save(doc.editor);
        return text;
    }

    // Header -> doc ; returns the scripts.
    static std::string ParseHeader(Doc& doc, const std::string& text)
    {
        doc.vars.clear();
        std::istringstream in(text);
        std::string line;
        size_t consumed = 0;
        while (std::getline(in, line))
        {
            consumed += line.size() + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line == "blocks")
                return consumed <= text.size() ? text.substr(consumed) : std::string();
            const size_t space = line.find(' ');
            const std::string key = line.substr(0, space);
            const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
            if (key == "class")
                doc.class_name = Identifier(Trim(rest), "MyActor");
            else if (key == "extends")
                doc.parent = Identifier(Trim(rest), "Actor");
            else if (key == "var" || key == "global")
            {
                const size_t s = rest.find(' ');
                Var v;
                v.name = Identifier(rest.substr(0, s), "variable");
                v.value = s == std::string::npos ? std::string("0") : rest.substr(s + 1);
                v.global = key == "global";
                if (!FindVar(doc, v.name))
                    doc.vars.push_back(v);
            }
        }
        return {};
    }

    static void Restore(Doc& doc, const std::string& state)
    {
        const std::string scripts = ParseHeader(doc, state);
        EnsureVarKinds(doc);
        EnsureKindsOfText(doc, scripts);
        if (doc.editor != R_INVALID_ID && !Rblock_editor_load(doc.editor, scripts.c_str()))
            Status(doc, "Some blocks could not be read");
        doc.snapshot = state;
        doc.dirty = state != doc.saved;
        doc.vars_dirty = doc.code_dirty = doc.kinds_dirty = true;
    }

    static void LoadDoc(Doc& doc)
    {
        std::ifstream in(doc.path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.rfind("lynx-blocks", 0) != 0)
        {
            doc.class_name = Identifier(doc.path.stem().string(), "MyActor");
            text = Serialize(doc);   // empty or foreign file : a new class
        }
        doc.saved = text;
        doc.undo.clear();
        doc.redo.clear();
        Restore(doc, text);
        doc.saved = doc.snapshot = Serialize(doc);   // as Raven writes it
        doc.dirty = doc.touched = false;
        std::error_code ec;
        doc.write_time = bfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;
    }

    static bool WriteJs(Doc& doc)
    {
        const bfs::path js_path = GeneratedPath(doc.path);
        const std::string js = GenerateJs(doc);
        {
            std::ifstream in(js_path, std::ios::binary);
            const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (old == js)
                return true;
        }
        std::ofstream out(js_path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            Status(doc, "Could not write " + js_path.filename().string());
            return false;
        }
        out << js;
        out.close();
        lynx::ReloadScripts();
        return true;
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
        doc.write_time = bfs::last_write_time(doc.path, ec);
        doc.changed_on_disk = false;
        WriteJs(doc);
        Status(doc, "Saved " + doc.path.filename().string() + "  (class " + doc.class_name + " in " +
                    GeneratedPath(doc.path).filename().string() + ")");
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

    static void Touch(Doc& doc)
    {
        doc.touched = true;
        doc.code_dirty = true;
    }

    // =========================================================================
    // Variables and My Blocks
    // =========================================================================

    static std::string CheckVarName(const Doc& doc, const std::string& name)
    {
        if (!IsIdentifier(name))
            return "Letters, digits and _ only, not starting with a digit.";
        if (FindVar(doc, name))
            return "\"" + name + "\" already exists.";
        if (name.rfind("__", 0) == 0)
            return "Names starting with __ are kept for the blocks.";
        static const char* const kReserved[] = {
            "position", "velocity", "transform", "id", "tags", "scripts", "lifetime", "entity", "className", "valid",
            "class", "this", "new", "return", "var", "let", "const", "function", "if", "else", "for", "while", "true", "false", "null",
        };
        for (const char* r : kReserved)
            if (name == r)
                return "\"" + name + "\" is already used by the actor.";
        return {};
    }

    static void AskNewVariable(Doc& doc)
    {
        Doc* d = &doc;
        eui::AskText(g.window, "New variable", "Name of the variable", "score", [d](const std::string& name)
        {
            if (!Alive(d))
                return;
            Var v;
            v.name = Trim(name);
            d->vars.push_back(v);
            AddKind(*d, "var." + v.name, v.name, R_BLOCK_REPORTER, "Variables");
            d->vars_dirty = true;
            Touch(*d);
            Status(*d, "Variable \"" + v.name + "\" : its block is in Variables");
        },
        [d](const std::string& name) { return Alive(d) ? CheckVarName(*d, Trim(name)) : std::string(); });
    }

    // Under the lowest script.
    static void FreeSpot(Doc& doc, float& x, float& y)
    {
        x = 40.f;
        y = 40.f;
        const int count = Rblock_editor_get_script_count(doc.editor);
        for (int i = 0; i < count; ++i)
        {
            float sx = 0.f, sy = 0.f;
            Rblock_editor_get_position(doc.editor, Rblock_editor_get_script(doc.editor, i), &sx, &sy);
            y = std::max(y, sy + 220.f);
        }
    }

    static void AskNewBlock(Doc& doc)
    {
        Doc* d = &doc;
        eui::AskText(g.window, "Make a block", "Name of the block (inputs : in the define block, \"amount, target\")", "my block",
                     [d](const std::string& name)
        {
            if (!Alive(d))
                return;
            float x, y;
            FreeSpot(*d, x, y);
            const int block = Rblock_editor_add_block(d->editor, "fn.define", x, y);
            if (block > 0)
                Rblock_editor_set_value(d->editor, block, 0, LabelSafe(Trim(name)).c_str());
            EnsureCallKinds(*d);
            Touch(*d);
            Status(*d, "Block \"" + Trim(name) + "\" : put its steps under \"define\", use it from My Blocks");
        },
        [](const std::string& name) { return Trim(name).empty() ? std::string("A name, please.") : std::string(); });
    }

    static void RebuildVars(Doc& doc)
    {
        doc.vars_dirty = false;
        Rwidget_clear_children(doc.vars_box);
        Doc* d = &doc;
        if (doc.vars.empty())
        {
            const char xml[] = "<Label Text='No variable yet.' Class='dim'/>";
            Rid label = Rwidget_create_from_xml(xml, sizeof(xml) - 1);
            if (label != R_INVALID_ID)
                Rwidget_add_child(doc.vars_box, label);
            return;
        }
        for (const Var& v : doc.vars)
        {
            const std::string name = v.name;   // an identifier : safe in XML
            const std::string xml =
                "<HBox Spacing='4'>"
                "<Label Name='name' Text='" + name + "' Slot.Fill='1' Slot.VAlign='Center'/>"
                "<TextInput Name='value' Width='70' Tooltip='Value at the start'/>"
                "<Checkbox Name='global' Text='global' Slot.VAlign='Center' Tooltip='Shared by every actor (else : one per actor, editable in Details)'/>"
                "<Button Name='remove' Text='x' Class='tool' Tooltip='Remove the variable'/>"
                "</HBox>";
            Rid row = Rwidget_create_from_xml(xml.c_str(), xml.size());
            if (row == R_INVALID_ID)
                continue;
            Rwidget_add_child(doc.vars_box, row);
            const Rid value = eui::Find(row, "value");
            Rtext_input_set_text(value, v.value.c_str());
            eui::SetChecked(eui::Find(row, "global"), v.global);
            eui::OnCommit(value, [d, name](const std::string& text)
            {
                for (Var& x : d->vars)
                    if (x.name == name && x.value != text)
                    {
                        x.value = text;
                        Touch(*d);
                    }
            });
            eui::OnToggle(eui::Find(row, "global"), [d, name](bool on)
            {
                if (d->building)
                    return;
                for (Var& x : d->vars)
                    if (x.name == name && x.global != on)
                    {
                        x.global = on;
                        Touch(*d);
                    }
            });
            eui::OnClick(eui::Find(row, "remove"), [d, name]()
            {
                DocLater(d, [name](Doc& x)
                {
                    x.vars.erase(std::remove_if(x.vars.begin(), x.vars.end(), [&](const Var& v) { return v.name == name; }), x.vars.end());
                    x.vars_dirty = true;
                    Touch(x);
                    Status(x, "\"" + name + "\" removed (its blocks now read this." + name + ")");
                });
            });
        }
    }

    // =========================================================================
    // The panel
    // =========================================================================

    static const char kPanelXml[] =
        "<HBox Spacing='0'>"
        "  <VBox Spacing='4' Slot.Fill='1' Slot.Padding='4'>"
        "    <HBox Spacing='6'>"
        "      <Button Name='be.save' Text='Save' Style='Primary' Tooltip='Ctrl+S : the blocks, and the JavaScript class next to them'/>"
        "      <Button Name='be.undo' Text='Undo' Class='tool' Tooltip='Ctrl+Z'/>"
        "      <Button Name='be.redo' Text='Redo' Class='tool' Tooltip='Ctrl+Y'/>"
        "      <Label Text='class' Slot.VAlign='Center'/>"
        "      <TextInput Name='be.class' Width='150' Tooltip='Name of the actor class (Place Actors, Level.spawn)'/>"
        "      <Label Text='extends' Slot.VAlign='Center'/>"
        "      <ComboBox Name='be.parent' Width='150' Tooltip='Humanoid : walks, jumps and falls (Character blocks)'/>"
        "      <Label Name='be.status' Text='' Class='warning' Slot.Fill='1' Slot.VAlign='Center'/>"
        "    </HBox>"
        "    <HBox Name='be.disk' Spacing='6' Visibility='Collapsed'>"
        "      <Label Text='The file changed on disk.' Class='warning' Slot.VAlign='Center'/>"
        "      <Button Name='be.reload' Text='Reload'/>"
        "      <Button Name='be.keep' Text='Keep my version'/>"
        "    </HBox>"
        "    <BlockEditor Name='be.blocks' Slot.Fill='1'/>"
        "  </VBox>"
        "  <VBox Width='320' Spacing='5' Slot.Padding='8'>"
        "    <Label Text='VARIABLES' Class='section'/>"
        "    <ScrollBox Height='170'><VBox Name='be.vars' Spacing='4'/></ScrollBox>"
        "    <HBox Spacing='6'>"
        "      <Button Name='be.addvar' Text='+ Variable' Tooltip='A value of the actor (or global), with its block'/>"
        "      <Button Name='be.addfn' Text='+ Block' Tooltip='My Blocks : a block of your own'/>"
        "    </HBox>"
        "    <Label Text='GENERATED JAVASCRIPT' Class='section' Slot.Padding='0,8,0,0'/>"
        "    <TextEditor Name='be.code' ReadOnly='true' LineNumbers='false' Slot.Fill='1'/>"
        "  </VBox>"
        "</HBox>";

    static void BlockChanged(Rid, int, void* user)
    {
        Doc* d = static_cast<Doc*>(user);
        if (!Alive(d) || d->building)
            return;
        Touch(*d);
        d->kinds_dirty = true;
    }

    static void ScriptClicked(Rid, int, void* user)
    {
        Doc* d = static_cast<Doc*>(user);
        if (Alive(d))
            Status(*d, "The scripts run in Play : place a " + d->class_name + " from Place Actors (save first)");
    }

    static void BlockContext(Rid, int block, float x, float y, void* user)
    {
        Doc* d = static_cast<Doc*>(user);
        if (!Alive(d))
            return;
        eui::Later([d, block, x, y]()
        {
            if (!Alive(d))
                return;
            g_menu = std::make_unique<eui::Menu>(g.window);
            eui::Menu& menu = *g_menu;
            if (block > 0)
            {
                const char* kind = Rblock_editor_get_kind(d->editor, block);
                const KindDef* k = kind ? FindKind(kind) : nullptr;
                menu.Item("Delete", [d, block]()
                {
                    if (!Alive(d))
                        return;
                    Rblock_editor_remove_block(d->editor, block);
                    Touch(*d);
                });
                const std::string help = k && k->help[0] ? k->help : "";
                const std::string title = k ? k->label : (kind ? kind : "");
                menu.Item("Help", [title, help]() { eui::Message(g.window, title, help.empty() ? "No help for this block." : help); });
            }
            else
            {
                menu.Item("New variable...", [d]() { if (Alive(d)) AskNewVariable(*d); });
                menu.Item("Make a block...", [d]() { if (Alive(d)) AskNewBlock(*d); });
            }
            (void)x;
            (void)y;
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

    static void SyncHeaderWidgets(Doc& doc)
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

    static void BuildPanel(Doc& doc)
    {
        doc.panel = Rwidget_create_from_xml(kPanelXml, sizeof(kPanelXml) - 1);
        if (doc.panel == R_INVALID_ID)
            return;
        auto find = [&](const char* name) { return eui::Find(doc.panel, name); };
        doc.editor = find("be.blocks");
        doc.class_input = find("be.class");
        doc.parent_combo = find("be.parent");
        doc.vars_box = find("be.vars");
        doc.code = find("be.code");
        doc.status = find("be.status");
        doc.disk = find("be.disk");
        doc.undo_button = find("be.undo");
        doc.redo_button = find("be.redo");
        Doc* d = &doc;

        // The blocks
        for (const CategoryColor& c : kCategoryColors)
            Rblock_editor_set_category_color(doc.editor, c.name, ((c.rgb >> 16) & 255) / 255.f, ((c.rgb >> 8) & 255) / 255.f, (c.rgb & 255) / 255.f);
        for (const KindDef& k : kKinds)
            Rblock_editor_add_kind(doc.editor, k.name, k.label, k.shape, k.category);
        Rblock_editor_set_changed_callback(doc.editor, BlockChanged, d);
        Rblock_editor_set_clicked_callback(doc.editor, ScriptClicked, d);
        Rblock_editor_set_context_callback(doc.editor, BlockContext, d);

        // The code
        if (const char* language = Rlanguage_for_file("generated.js"))
            Rtext_editor_set_language(doc.code, language);

        // Header
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

        eui::OnClick(find("be.save"), [d]() { Commit(*d); SaveDoc(*d); });
        eui::OnClick(doc.undo_button, [d]() { DocLater(d, [](Doc& x) { Undo(x); }); });
        eui::OnClick(doc.redo_button, [d]() { DocLater(d, [](Doc& x) { Redo(x); }); });
        eui::OnClick(find("be.addvar"), [d]() { AskNewVariable(*d); });
        eui::OnClick(find("be.addfn"), [d]() { AskNewBlock(*d); });
        eui::OnClick(find("be.reload"), [d]() { DocLater(d, [](Doc& x) { LoadDoc(x); SyncHeaderWidgets(x); Status(x, "Reloaded"); }); });
        eui::OnClick(find("be.keep"), [d]() { d->changed_on_disk = false; });

        eui::OnEvent(doc.panel, [d](const REvent& e)
        {
            if (e.type != R_EVENT_KEY_DOWN)
                return false;
            bool used = false;
            Shortcut(*d, e, used);
            return used;
        });

        const std::string title = doc.path.filename().string();
        Rdock_area_add_panel(g.dock, doc.panel, title.c_str(), R_DOCK_TAB, g.viewport_panel, 0.f);
        float uv[4];
        eui::FileIconRegion(lynx::editor::file_icons::Kind::Script, uv);
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
        g_docs.erase(it);
        if (panel != R_INVALID_ID && Rwidget_is_valid(panel))
            Rdock_area_close_panel(g.dock, panel);
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
        eui::Ask(g.window, "Unsaved blocks", doc->path.filename().string() + " was changed. Save it before closing ?",
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
        const auto time = bfs::last_write_time(doc.path, ec);
        if (ec || time == doc.write_time)
            return;
        doc.write_time = time;
        if (doc.dirty || doc.touched)
            doc.changed_on_disk = true;
        else
        {
            LoadDoc(doc);
            SyncHeaderWidgets(doc);
            Status(doc, "Reloaded (changed on disk)");
        }
    }

    static void UpdateDoc(Doc& doc)
    {
        if (doc.panel == R_INVALID_ID || !Rwidget_is_valid(doc.panel))
            return;

        if (doc.kinds_dirty)
        {
            doc.kinds_dirty = false;
            EnsureCallKinds(doc);
        }
        if (doc.vars_dirty)
        {
            doc.building = true;
            RebuildVars(doc);
            SyncHeaderWidgets(doc);
            doc.building = false;
        }

        // One undo step once nothing is held / typed.
        if (doc.touched && !ein::Mouse(0) && !ein::WantTextInput())
            Commit(doc);

        // The generated code, a moment after the last change.
        if (doc.code_dirty && !ein::Mouse(0))
        {
            doc.code_dirty = false;
            const std::string js = GenerateJs(doc);
            if (js != doc.shown_code)
            {
                doc.shown_code = js;
                Rtext_editor_set_text(doc.code, js.c_str());
            }
        }

        CheckDisk(doc);
        eui::SetVisible(doc.disk, doc.changed_on_disk);
        eui::SetEnabled(doc.undo_button, !doc.undo.empty() || doc.touched);
        eui::SetEnabled(doc.redo_button, !doc.redo.empty());

        const std::string title = doc.path.filename().string() + (doc.dirty || doc.touched ? " *" : "");
        if (title != doc.shown_title)
        {
            doc.shown_title = title;
            Rdock_area_set_panel_title(g.dock, doc.panel, title.c_str());
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
// lynx::editor::block_editor (BlockEditor.h)
// =============================================================================

namespace lynx::editor::block_editor
{
    namespace eb = editor_ui::blocks;

    bool CanOpen(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".blocks";
    }

    std::filesystem::path GeneratedScript(const std::filesystem::path& blocks_file) { return eb::GeneratedPath(blocks_file); }

    void Open(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
        for (auto& doc : eb::g_docs)
            if (doc->path == absolute || std::filesystem::equivalent(doc->path, absolute, ec))
            {
                Rdock_area_open_panel(editor_ui::g.dock, doc->panel);
                Rdock_area_show_panel(editor_ui::g.dock, doc->panel);
                return;
            }
        if (editor_ui::g.dock == R_INVALID_ID)
            return;
        auto doc = std::make_unique<eb::Doc>();
        doc->path = absolute;
        eb::Doc& d = *doc;
        eb::g_docs.push_back(std::move(doc));
        eb::BuildPanel(d);          // the kinds first : the file uses them
        eb::LoadDoc(d);
        eb::SyncHeaderWidgets(d);
        // A new file (or a .js deleted by hand) : the class exists at once.
        if (!std::filesystem::exists(eb::GeneratedPath(d.path), ec))
            eb::WriteJs(d);
        eb::UpdateDoc(d);
    }

    bool HasFocus()
    {
        const Rid focused = Rwindow_get_focused_widget(editor_ui::g.window);
        for (auto& doc : eb::g_docs)
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
        // The generated class follows its .blocks file.
        if (CanOpen(from) && CanOpen(to))
        {
            const std::filesystem::path old_js = eb::GeneratedPath(from), new_js = eb::GeneratedPath(to);
            if (std::filesystem::exists(old_js, ec) && !std::filesystem::exists(new_js, ec))
                std::filesystem::rename(old_js, new_js, ec);
        }
        const std::filesystem::path destination = std::filesystem::weakly_canonical(std::filesystem::absolute(to, ec), ec);
        for (auto& doc : eb::g_docs)
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
        std::error_code ec;
        // A deleted .blocks takes its class with it.
        if (CanOpen(target))
        {
            const std::filesystem::path js = eb::GeneratedPath(target);
            if (std::filesystem::exists(js, ec) && std::filesystem::remove(js, ec))
                lynx::ReloadScripts();
        }
        std::vector<eb::Doc*> gone;
        for (auto& doc : eb::g_docs)
        {
            std::filesystem::path relative;
            if (RelativeInside(doc->path, target, relative))
                gone.push_back(doc.get());
        }
        for (eb::Doc* doc : gone)
            eb::CloseDoc(doc);
    }

    bool HasUnsavedChanges()
    {
        for (auto& doc : eb::g_docs)
        {
            eb::Commit(*doc);
            if (doc->dirty)
                return true;
        }
        return false;
    }

    void SaveAll()
    {
        for (auto& doc : eb::g_docs)
        {
            eb::Commit(*doc);
            if (doc->dirty)
                eb::SaveDoc(*doc);
        }
    }

    std::string NewFileTemplate(const std::string& class_name)
    {
        const std::string name = eb::Identifier(class_name, "MyActor");
        return
            "lynx-blocks 1\n"
            "class " + name + "\n"
            "extends Actor\n"
            "blocks\n"
            "(script 40 40\n"
            "  (block \"ev.start\" ())\n"
            "  (block \"wd.print\" (\"Hello from " + name + "!\")))\n"
            "(script 40 220\n"
            "  (block \"ev.tick\" ()))\n";
    }

    void RenameClassInCopy(const std::filesystem::path& copy, const std::string& new_class_name)
    {
        std::ifstream in(copy, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const size_t blocks = text.find("\nblocks\n");
        const size_t at = text.find("\nclass ");
        if (at == std::string::npos || (blocks != std::string::npos && at > blocks))
            return;
        const size_t end = text.find('\n', at + 1);
        text.replace(at + 1, (end == std::string::npos ? text.size() : end) - at - 1, "class " + eb::Identifier(new_class_name, "MyActor"));
        std::ofstream out(copy, std::ios::binary | std::ios::trunc);
        out << text;
    }
}
