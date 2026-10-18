// =============================================================================
// Particle Editor in Raven (.vfx), Unreal Cascade-like
// (included by raven/EditorUi.inl ; replaces the ImGui ParticleEditor.cpp)
// -----------------------------------------------------------------------------
// One document tab per open file, its parts are panels of the dock area of
// the tab :
//
//   +--------------------------------------------+----------------------------+
//   |  Save Restart Pause  speed  Loop  duration |  Details                   |
//   |  Preview (its own scene : right drag to   |  (module selected below,   |
//   |  orbit, middle drag to pan, wheel : zoom)  |   curves / gradient)       |
//   +--------------------------------------------+----------------------------+
//   |  Emitters : one column per emitter                                      |
//   |  [x] Flames      [x] Sparks      [ + Emitter ]                          |
//   |   Required        Required                                              |
//   |   Spawn           Spawn                                                 |
//   |   Color Over Life (gradient)                                            |
//   +--------------------------------------------------------------------------+
//
// - Click a module : its settings in Details. Right click a column header :
//   rename, duplicate, delete, move, add module ; right click a module : remove.
//   Double-click the name of an emitter : rename.
// - Curves (Size / Rotation Over Life) : drag the keys, double-click to add one,
//   right click a key to delete it. Gradient (Color Over Life) : the same on the
//   markers under the bar.
// - Ctrl+S save, Ctrl+Z / Ctrl+Y undo / redo, Space : restart the preview.
// - Saving reloads the ParticleActors of the level that use the file.
// =============================================================================

#include "../../core/Particles.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>

namespace editor_ui::particles
{
    namespace sfs = std::filesystem;
    namespace px = lynx::particles;

    constexpr uint32_t kInvalid = 0xFFFFFFFFu;
    constexpr float kDegToRad = 3.14159265358979f / 180.f;

    enum class Module
    {
        System,   // nothing selected : the settings of the system
        Required,
        Spawn,
        Lifetime,
        Location,
        Velocity,
        Rotation,
        Forces,
        ColorOverLife,
        SizeOverLife,
        RotationOverLife,
        Collision,
        Count
    };

    static const char* ModuleName(Module m)
    {
        switch (m)
        {
        case Module::Required:         return "Required";
        case Module::Spawn:            return "Spawn";
        case Module::Lifetime:         return "Lifetime";
        case Module::Location:         return "Initial Location";
        case Module::Velocity:         return "Initial Velocity";
        case Module::Rotation:         return "Initial Rotation";
        case Module::Forces:           return "Forces";
        case Module::ColorOverLife:    return "Color Over Life";
        case Module::SizeOverLife:     return "Size Over Life";
        case Module::RotationOverLife: return "Rotation Over Life";
        case Module::Collision:        return "Collision";
        default:                       return "System";
        }
    }

    struct Rgb { float r, g, b; };

    // Colored bar of the module blocks (like Cascade : one color per kind).
    static Rgb ModuleColor(Module m)
    {
        auto c = [](int r, int g, int b) { return Rgb{ r / 255.f, g / 255.f, b / 255.f }; };
        switch (m)
        {
        case Module::Required:         return c(230, 200, 80);
        case Module::Spawn:            return c(230, 120, 60);
        case Module::Lifetime:         return c(120, 200, 120);
        case Module::Location:         return c(90, 170, 230);
        case Module::Velocity:         return c(110, 140, 240);
        case Module::Rotation:         return c(170, 120, 230);
        case Module::Forces:           return c(200, 90, 200);
        case Module::ColorOverLife:    return c(240, 90, 110);
        case Module::SizeOverLife:     return c(90, 210, 200);
        case Module::RotationOverLife: return c(160, 200, 90);
        case Module::Collision:        return c(200, 160, 110);
        default:                       return c(150, 150, 150);
        }
    }

    // Optional modules : enabled on their block, "Add module" / "Remove module".
    static bool* ModuleFlag(px::EmitterDesc& e, Module m)
    {
        switch (m)
        {
        case Module::Rotation:         return &e.rotation_module;
        case Module::Forces:           return &e.forces_module;
        case Module::ColorOverLife:    return &e.color_module;
        case Module::SizeOverLife:     return &e.size_module;
        case Module::RotationOverLife: return &e.rotation_curve_module;
        case Module::Collision:        return &e.collision;
        default:                       return nullptr;
        }
    }

    // ---------------------------------------------------------------------
    // Preview scene (off-screen HRL scene, shown by a Raven <Viewport>)
    // ---------------------------------------------------------------------

    struct Preview
    {
        uint32_t scene = kInvalid;
        uint32_t camera = kInvalid;
        uint32_t viewport = kInvalid;
        px::Instance instance;
        lynx::editor::gl::TextureLink link;
        int width = 0, height = 0;
        float yaw = -90.f, pitch = 0.f, distance = 14.f;
        float target[3] = { 0.f, 1.5f, 0.f };
        float background[3] = { 0.07f, 0.075f, 0.09f };
        bool grid = true;
        bool paused = false;
        float time_scale = 1.f;
        float applied_bg[3] = { -1.f, -1.f, -1.f };

        void Create()
        {
            if (scene != kInvalid && HRL_IsValidScene(scene))
                return;
            scene = HRL_CreateScene(HRL_FALSE);
            if (scene == kInvalid)
                return;
            camera = HRL_CreateCamera(scene, HRL_PERSPECTIVE);
            HRL_SetCameraPerspectiveFov(camera, 45.f);
            HRL_SetCameraNearPlane(camera, 0.05f);
            HRL_SetCameraFarPlane(camera, 2000.f);
            viewport = HRL_CreateViewport(scene, camera, 0.f, 0.f, 1.f, 1.f);
            HRL_SetSkySphereEnabled(scene, HRL_TRUE);
            applied_bg[0] = -1.f;
            width = height = 0;
        }

        void Destroy()
        {
            instance.Destroy();
            if (scene != kInvalid && HRL_IsValidScene(scene))
                HRL_DeleteScene(scene);   // its camera / viewport too
            scene = camera = viewport = kInvalid;
            width = height = 0;
            link.Release();
        }

        void Frame()
        {
            yaw = -90.f;
            pitch = 0.f;
            distance = 14.f;
            target[0] = 0.f; target[1] = 1.5f; target[2] = 0.f;
        }

        void ApplyCamera()
        {
            if (camera == kInvalid)
                return;
            const float p = pitch * kDegToRad, y = yaw * kDegToRad;
            const float f[3] = { std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p) };
            HRL_SetCameraLocation(camera, target[0] - f[0] * distance, target[1] - f[1] * distance,
                                  target[2] - f[2] * distance);
            HRL_SetCameraRotation(camera, pitch, yaw, 0.f);

            if (std::fabs(background[0] - applied_bg[0]) + std::fabs(background[1] - applied_bg[1]) +
                    std::fabs(background[2] - applied_bg[2]) > 1e-4f)
            {
                const float* b = background;
                HRL_SetSkySphereColors(scene, b[0], b[1], b[2], b[0], b[1], b[2], b[0], b[1], b[2]);
                std::copy(b, b + 3, applied_bg);
            }
        }

        void DrawGrid()
        {
            if (!grid || scene == kInvalid)
                return;
            // The level plane (XY, Z = 0) : the voxel world of the game.
            constexpr int kHalf = 10;
            for (int i = -kHalf; i <= kHalf; ++i)
            {
                const float c = i == 0 ? 0.f : 0.22f;
                const float fi = static_cast<float>(i);
                const float h = static_cast<float>(kHalf);
                HRL_DrawDebugSegment(scene, fi, -h, 0.f, fi, h, 0.f, i == 0 ? 0.3f : c, i == 0 ? 0.85f : c, c);
                HRL_DrawDebugSegment(scene, -h, fi, 0.f, h, fi, 0.f, i == 0 ? 0.85f : c, i == 0 ? 0.3f : c, c);
            }
        }
    };

    // ---------------------------------------------------------------------
    // One open file
    // ---------------------------------------------------------------------

    struct Doc
    {
        sfs::path path;
        px::SystemDesc desc;
        std::string saved, snapshot;            // on the disk / last state pushed to undo
        std::vector<std::string> undo, redo;
        bool dirty = false, touched = false;
        sfs::file_time_type write_time{};
        double disk_check = 0.0;

        int emitter = 0;                        // selected emitter
        Module module = Module::Required;
        int selected_key = -1;                  // curve / gradient key
        bool rebuild_preview = true;
        double last_build = -100.0;
        bool details_dirty = true, emitters_dirty = true, keys_dirty = false;
        bool building = false;

        Rid panel = R_INVALID_ID;               // the page of the tab
        Rid view = R_INVALID_ID, status = R_INVALID_ID, details = R_INVALID_ID, columns = R_INVALID_ID;
        Rid play = R_INVALID_ID, speed = R_INVALID_ID, loop = R_INVALID_ID, duration = R_INVALID_ID, grid = R_INVALID_ID;
        Rid key_box = R_INVALID_ID, key_time = R_INVALID_ID, key_value = R_INVALID_ID;
        std::vector<Rid> column_borders;        // one per emitter
        std::vector<Rid> repaint;               // names / module rows : painted from the state
        std::string shown_title;
        double status_time = -100.0;

        int camera_drag = -1;                   // 1 : orbit (right), 2 : pan (middle)
        int key_drag = -1;                      // key dragged in the curve / gradient

        Preview preview;
    };

    static std::vector<std::unique_ptr<Doc>> g_docs;
    static std::unique_ptr<eui::Menu> g_menu;

    static bool Alive(const Doc* doc)
    {
        return std::any_of(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
    }

    // Later, if the document is still open (widgets are rebuilt outside of their own callbacks).
    static void DocLater(Doc* doc, std::function<void(Doc&)> fn)
    {
        eui::Later([doc, fn]() { if (Alive(doc)) fn(*doc); });
    }

    static px::EmitterDesc* EmAt(Doc& doc, int index)
    {
        if (index < 0 || index >= static_cast<int>(doc.desc.emitters.size()))
            return nullptr;
        return &doc.desc.emitters[static_cast<size_t>(index)];
    }

    static std::string PExtension(const sfs::path& path)
    {
        std::string e = path.extension().string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }

    // assets/<...> path of a file (lynx::fs reads assets-relative paths).
    static std::string AssetPath(const sfs::path& path)
    {
        const sfs::path p = path.lexically_normal();
        std::vector<sfs::path> parts(p.begin(), p.end());
        for (size_t i = parts.size(); i > 0; --i)
            if (parts[i - 1] == "assets")
            {
                sfs::path rel;
                for (size_t j = i; j < parts.size(); ++j)
                    rel /= parts[j];
                return rel.generic_string();
            }
        return p.filename().generic_string();
    }

    static void Status(Doc& doc, const std::string& text)
    {
        eui::SetText(doc.status, text);
        doc.status_time = Rget_time();
        std::cout << "[PARTICLE EDITOR] " << text << "\n";
    }

    static void Touch(Doc& doc)
    {
        doc.touched = true;
        doc.rebuild_preview = true;
    }

    // Structure changed (emitters, modules, keys) : the panels are built again.
    static void Restructure(Doc& doc)
    {
        Touch(doc);
        doc.details_dirty = doc.emitters_dirty = true;
    }

    // The names / module rows are painted from the state : paint them again.
    static void Repaint(Doc& doc)
    {
        for (Rid w : doc.repaint)
            if (Rwidget_is_valid(w))
                Rwidget_invalidate(w);
    }

    // ---- load / save / undo -------------------------------------------------------------

    static void LoadDoc(Doc& doc)
    {
        std::ifstream in(doc.path, std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        std::string error;
        if (!px::FromJson(text.empty() ? px::NewTemplate() : text, doc.desc, error))
        {
            std::cerr << "[PARTICLE EDITOR] " << doc.path.generic_string() << " : " << error << "\n";
            px::FromJson(px::NewTemplate(), doc.desc, error);
        }
        doc.snapshot = doc.saved = px::ToJson(doc.desc);
        doc.undo.clear();
        doc.redo.clear();
        doc.dirty = doc.touched = false;
        doc.emitter = std::clamp(doc.emitter, 0, std::max(0, static_cast<int>(doc.desc.emitters.size()) - 1));
        if (doc.desc.emitters.empty())
            doc.module = Module::System;
        doc.selected_key = -1;
        doc.rebuild_preview = doc.details_dirty = doc.emitters_dirty = true;
        std::error_code ec;
        doc.write_time = sfs::last_write_time(doc.path, ec);
    }

    static bool SaveDoc(Doc& doc)
    {
        const std::string json = px::ToJson(doc.desc);
        std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            Status(doc, "Could not write " + doc.path.generic_string());
            return false;
        }
        out << json;
        out.close();
        doc.saved = doc.snapshot = json;
        doc.dirty = doc.touched = false;
        std::error_code ec;
        doc.write_time = sfs::last_write_time(doc.path, ec);
        px::NotifyAssetChanged(AssetPath(doc.path));   // ParticleActors reload it
        Status(doc, "Saved " + doc.path.filename().string());
        return true;
    }

    static void Commit(Doc& doc)
    {
        doc.touched = false;
        const std::string now = px::ToJson(doc.desc);
        if (now == doc.snapshot)
            return;
        doc.undo.push_back(doc.snapshot);
        if (doc.undo.size() > 200)
            doc.undo.erase(doc.undo.begin());
        doc.redo.clear();
        doc.snapshot = now;
        doc.dirty = now != doc.saved;
    }

    static void Restore(Doc& doc, const std::string& json)
    {
        std::string error;
        px::FromJson(json, doc.desc, error);
        doc.snapshot = json;
        doc.dirty = json != doc.saved;
        doc.emitter = std::clamp(doc.emitter, 0, std::max(0, static_cast<int>(doc.desc.emitters.size()) - 1));
        if (doc.desc.emitters.empty())
            doc.module = Module::System;
        doc.selected_key = -1;
        doc.rebuild_preview = doc.details_dirty = doc.emitters_dirty = true;
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

    // ---- curves -----------------------------------------------------------------------------

    // Value of a float curve at t (keys in any order, linear between them).
    static float Evaluate(const std::vector<px::FloatKey>& keys, float t)
    {
        if (keys.empty())
            return 0.f;
        const px::FloatKey* before = nullptr;
        const px::FloatKey* after = nullptr;
        for (const px::FloatKey& k : keys)
        {
            if (k.time <= t && (!before || k.time > before->time)) before = &k;
            if (k.time >= t && (!after || k.time < after->time)) after = &k;
        }
        if (!before) return after->value;
        if (!after) return before->value;
        if (after->time - before->time < 1e-6f) return before->value;
        const float a = (t - before->time) / (after->time - before->time);
        return before->value + (after->value - before->value) * a;
    }

    static lynx::vec4 Evaluate(const std::vector<px::ColorKey>& keys, float t)
    {
        if (keys.empty())
            return lynx::vec4(1.f, 1.f, 1.f, 1.f);
        const px::ColorKey* before = nullptr;
        const px::ColorKey* after = nullptr;
        for (const px::ColorKey& k : keys)
        {
            if (k.time <= t && (!before || k.time > before->time)) before = &k;
            if (k.time >= t && (!after || k.time < after->time)) after = &k;
        }
        if (!before) return after->color;
        if (!after) return before->color;
        if (after->time - before->time < 1e-6f) return before->color;
        const float a = (t - before->time) / (after->time - before->time);
        const lynx::vec4& c0 = before->color;
        const lynx::vec4& c1 = after->color;
        return lynx::vec4(c0.x + (c1.x - c0.x) * a, c0.y + (c1.y - c0.y) * a, c0.z + (c1.z - c0.z) * a,
                          c0.w + (c1.w - c0.w) * a);
    }

    static float Clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

    static void ThemeColor(RColorRole role, float c[4]) { Rtheme_get_color(role, &c[0], &c[1], &c[2], &c[3]); }

    static void Checkerboard(RPainter* p, float x, float y, float w, float h, float cell)
    {
        Rdraw_rect(p, x, y, w, h, 0.f, 0.78f, 0.78f, 0.78f, 1.f);
        int row = 0;
        for (float yy = y; yy < y + h; yy += cell, ++row)
            for (float xx = x + ((row & 1) ? cell : 0.f); xx < x + w; xx += cell * 2.f)
                Rdraw_rect(p, xx, yy, std::min(cell, x + w - xx), std::min(cell, y + h - yy), 0.f, 0.59f, 0.59f, 0.59f, 1.f);
    }

    // Gradient bar (RGBA over the life of the particles).
    static void PaintGradientStrip(RPainter* p, float x, float y, float w, float h, const std::vector<px::ColorKey>& keys)
    {
        if (w <= 1.f || h <= 1.f)
            return;
        Rdraw_push_clip_rect(p, x, y, w, h);
        Checkerboard(p, x, y, w, h, std::max(3.f, h * 0.5f));
        const int steps = std::clamp(static_cast<int>(w / 3.f), 8, 128);
        for (int i = 0; i < steps; ++i)
        {
            const float t0 = static_cast<float>(i) / steps, t1 = static_cast<float>(i + 1) / steps;
            const lynx::vec4 c = Evaluate(keys, (t0 + t1) * 0.5f);
            const float x0 = std::floor(x + w * t0), x1 = std::ceil(x + w * t1);
            Rdraw_rect(p, x0, y, x1 - x0, h, 0.f, Clamp01(c.x), Clamp01(c.y), Clamp01(c.z), Clamp01(c.w));
        }
        Rdraw_pop_clip_rect(p);
        Rdraw_rect_outline(p, x, y, w, h, 0.f, 1.f, 0.08f, 0.08f, 0.08f, 1.f);
    }

    // The keys being edited (Details of Color / Size / Rotation Over Life).
    static std::vector<px::ColorKey>* ColorKeys(Doc& doc)
    {
        px::EmitterDesc* em = EmAt(doc, doc.emitter);
        return em && doc.module == Module::ColorOverLife ? &em->color : nullptr;
    }

    static std::vector<px::FloatKey>* CurveKeys(Doc& doc)
    {
        px::EmitterDesc* em = EmAt(doc, doc.emitter);
        if (!em)
            return nullptr;
        if (doc.module == Module::SizeOverLife) return &em->size_curve;
        if (doc.module == Module::RotationOverLife) return &em->rotation_curve;
        return nullptr;
    }

    static float CurveDefaultMax(const Doc& doc) { return doc.module == Module::RotationOverLife ? 180.f : 1.f; }

    // ---- Details : rows of editors -------------------------------------------------------

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

    static Rid AddNote(Rid box, const std::string& text)
    {
        Rid l = Rwidget_add_child(box, Rlabel_create(text.c_str()));
        Rlabel_set_wrap(l, R_TRUE);
        Rwidget_add_class(l, "dim");
        return l;
    }

    static Rid AddRow(Rid box, const std::string& label, Rid editor, const std::string& tip = {})
    {
        Rid row = Rwidget_add_child(box, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(row, 6.f);
        Rid l = Rwidget_add_child(row, Rlabel_create(label.c_str()));
        Rwidget_set_size(l, 120.f, -1.f);
        Rslot_set_valign(l, R_VALIGN_CENTER);
        if (!tip.empty())
        {
            eui::SetTooltip(l, tip);
            eui::SetTooltip(editor, tip);
        }
        Rwidget_add_child(row, editor);
        Rslot_set_fill(editor, 1.f);
        return row;
    }

    static Rid MakeSpin(double value, double lo, double hi, double step, int decimals)
    {
        Rid spin = Rwidget_create(R_WIDGET_SPIN_BOX);
        Rspin_box_set_range(spin, lo, hi);
        Rspin_box_set_step(spin, step);
        Rspin_box_set_decimals(spin, decimals);
        Rspin_box_set_value(spin, value);
        return spin;
    }

    // A number of the document. `get(doc)` gives the field (float* / int*) at the
    // time of the change : the editors never keep a pointer into the system,
    // which undo / redo build again.
    template <class Get>
    static Rid NumberField(Doc& doc, Rid box, const std::string& label, double lo, double hi, double step, int decimals,
                           Get get, const std::string& tip = {})
    {
        using T = std::remove_pointer_t<decltype(get(doc))>;
        const T* now = get(doc);
        Rid spin = MakeSpin(now ? static_cast<double>(*now) : 0.0, lo, hi, step, decimals);
        Doc* d = &doc;
        eui::OnValue(spin, [d, get](double v)
        {
            if (!Alive(d) || d->building)
                return;
            if (T* field = get(*d))
            {
                *field = static_cast<T>(v);
                Touch(*d);
            }
        });
        AddRow(box, label, spin, tip);
        return spin;
    }

    template <class Get>
    static Rid CheckField(Doc& doc, Rid box, const std::string& text, Get get, bool restructure = false, const std::string& tip = {})
    {
        const bool* now = get(doc);
        Rid check = Rwidget_add_child(box, Rcheckbox_create(text.c_str()));
        Rcheckbox_set_checked(check, now && *now ? R_TRUE : R_FALSE);
        Doc* d = &doc;
        eui::OnToggle(check, [d, get, restructure](bool on)
        {
            if (!Alive(d) || d->building)
                return;
            if (bool* field = get(*d))
            {
                *field = on;
                if (restructure)
                    DocLater(d, [](Doc& x) { Restructure(x); });
                Touch(*d);
                Repaint(*d);
            }
        });
        if (!tip.empty())
            eui::SetTooltip(check, tip);
        return check;
    }

    // x, y, z of a vec3 (or x, y of a vec2) side by side.
    template <class Get>
    static Rid VectorField(Doc& doc, Rid box, const std::string& label, int count, double step, Get get, const std::string& tip = {})
    {
        Rid row = Rwidget_create(R_WIDGET_HBOX);
        Rpanel_set_spacing(row, 3.f);
        Doc* d = &doc;
        const float* now = get(doc);
        for (int i = 0; i < count; ++i)
        {
            Rid spin = Rwidget_add_child(row, MakeSpin(now ? now[i] : 0.0, -1e6, 1e6, step, 2));
            Rslot_set_fill(spin, 1.f);
            Rwidget_set_min_size(spin, 40.f, -1.f);
            eui::OnValue(spin, [d, get, i](double v)
            {
                if (!Alive(d) || d->building)
                    return;
                if (float* field = get(*d))
                {
                    field[i] = static_cast<float>(v);
                    Touch(*d);
                }
            });
        }
        AddRow(box, label, row, tip);
        return row;
    }

    // min .. max (max follows min).
    template <class Get>
    static Rid RangeField(Doc& doc, Rid box, const std::string& label, double lo, double hi, double step, Get get,
                          const std::string& tip = {})
    {
        Rid row = Rwidget_create(R_WIDGET_HBOX);
        Rpanel_set_spacing(row, 3.f);
        Doc* d = &doc;
        const px::Range* now = get(doc);
        Rid min_spin = Rwidget_add_child(row, MakeSpin(now ? now->min : 0.0, lo, hi, step, 2));
        Rid max_spin = Rwidget_add_child(row, MakeSpin(now ? now->max : 0.0, lo, hi, step, 2));
        Rslot_set_fill(min_spin, 1.f);
        Rslot_set_fill(max_spin, 1.f);
        eui::SetTooltip(min_spin, "min");
        eui::SetTooltip(max_spin, "max");
        eui::OnValue(min_spin, [d, get, max_spin](double v)
        {
            if (!Alive(d) || d->building)
                return;
            if (px::Range* r = get(*d))
            {
                r->min = static_cast<float>(v);
                if (r->max < r->min)
                {
                    r->max = r->min;
                    eui::SetValue(max_spin, r->max);
                }
                Touch(*d);
            }
        });
        eui::OnValue(max_spin, [d, get, min_spin](double v)
        {
            if (!Alive(d) || d->building)
                return;
            if (px::Range* r = get(*d))
            {
                r->max = std::max(static_cast<float>(v), r->min);
                if (r->max != static_cast<float>(v))
                    eui::SetValue(min_spin, r->min);
                Touch(*d);
            }
        });
        AddRow(box, label, row, tip);
        return row;
    }

    template <class E, class Get>
    static Rid EnumField(Doc& doc, Rid box, const std::string& label, const std::vector<std::pair<E, const char*>>& names,
                         Get get, bool restructure = false)
    {
        Rid combo = Rwidget_create(R_WIDGET_COMBO_BOX);
        const E* now = get(doc);
        int selected = 0;
        for (size_t i = 0; i < names.size(); ++i)
        {
            Rcombo_box_add_item(combo, names[i].second);
            if (now && names[i].first == *now)
                selected = static_cast<int>(i);
        }
        Rcombo_box_set_selected(combo, selected);
        Doc* d = &doc;
        eui::OnSelect(combo, [d, get, names, restructure](int index)
        {
            if (!Alive(d) || d->building || index < 0 || index >= static_cast<int>(names.size()))
                return;
            if (E* field = get(*d))
            {
                *field = names[static_cast<size_t>(index)].first;
                Touch(*d);
                if (restructure)
                    DocLater(d, [](Doc& x) { x.details_dirty = true; });
            }
        });
        AddRow(box, label, combo);
        return combo;
    }

    // An asset path (texture) : text, "..." picks a file of assets/.
    static void AssetField(Doc& doc, Rid box, const std::string& label, int emitter)
    {
        Rid row = Rwidget_add_child(box, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(row, 6.f);
        Rid l = Rwidget_add_child(row, Rlabel_create(label.c_str()));
        Rwidget_set_size(l, 120.f, -1.f);
        Rslot_set_valign(l, R_VALIGN_CENTER);
        Rid input = Rwidget_add_child(row, Rwidget_create(R_WIDGET_TEXT_INPUT));
        Rslot_set_fill(input, 1.f);
        const px::EmitterDesc* em = EmAt(doc, emitter);
        Rtext_input_set_text(input, em ? em->texture.c_str() : "");
        Rtext_input_set_placeholder(input, "assets path (.png) ; empty : square");
        Doc* d = &doc;
        auto set = [d, emitter](const std::string& path)
        {
            if (!Alive(d))
                return;
            if (px::EmitterDesc* e = EmAt(*d, emitter); e && e->texture != path)
            {
                e->texture = path;
                Touch(*d);
            }
        };
        eui::OnCommit(input, set);
        Rid pick = Rwidget_add_child(row, Rbutton_create("..."));
        eui::SetTooltip(pick, "Choose an image of the project");
        eui::OnClick(pick, [set, input]()
        {
            const char* now = Rtext_input_get_text(input);
            asset_picker::Open(now ? now : "", [set, input](const std::string& path)
            {
                if (Rwidget_is_valid(input))
                    Rtext_input_set_text(input, path.c_str());
                set(path);
            });
        });
    }

    // ---- gradient editor (Color Over Life) -----------------------------------------------

    constexpr float kGradientBar = 26.f;
    constexpr float kMarker = 7.f;

    static int GradientKeyAt(const std::vector<px::ColorKey>& keys, float w, float x, float y)
    {
        if (y < kGradientBar)
            return -1;
        int best = -1;
        float best_d = kMarker + 1.f;
        for (int i = 0; i < static_cast<int>(keys.size()); ++i)
        {
            const float d = std::fabs(x - w * Clamp01(keys[static_cast<size_t>(i)].time));
            if (d <= best_d)
            {
                best_d = d;
                best = i;
            }
        }
        return best;
    }

    static void PaintGradient(Doc& doc, RPainter* p, float w, float)
    {
        const std::vector<px::ColorKey>* keys = ColorKeys(doc);
        if (!keys)
            return;
        PaintGradientStrip(p, 0.f, 0.f, w, kGradientBar, *keys);
        float accent[4];
        ThemeColor(R_COLOR_ACCENT, accent);
        for (int i = 0; i < static_cast<int>(keys->size()); ++i)
        {
            const px::ColorKey& k = (*keys)[static_cast<size_t>(i)];
            const float x = w * Clamp01(k.time);
            const float tip = kGradientBar + 1.f, base = kGradientBar + kMarker * 1.9f;
            const bool selected = i == doc.selected_key;
            if (selected)
                Rdraw_triangle(p, x, tip - 2.f, x + kMarker + 2.f, base + 2.f, x - kMarker - 2.f, base + 2.f,
                               accent[0], accent[1], accent[2], 1.f);
            else
                Rdraw_triangle(p, x, tip - 1.f, x + kMarker + 1.f, base + 1.f, x - kMarker - 1.f, base + 1.f, 0.08f, 0.08f, 0.08f, 1.f);
            Rdraw_triangle(p, x, tip, x + kMarker, base, x - kMarker, base, Clamp01(k.color.x), Clamp01(k.color.y), Clamp01(k.color.z), 1.f);
        }
    }

    static void RebuildKeys(Doc& doc);

    static bool OnGradientEvent(Doc& doc, Rid widget, const REvent& e)
    {
        std::vector<px::ColorKey>* keys = ColorKeys(doc);
        if (!keys)
            return false;
        float rx = 0.f, ry = 0.f, w = 0.f, h = 0.f;
        Rwidget_get_rect(widget, &rx, &ry, &w, &h);
        w = std::max(1.f, w);
        const float t = Clamp01(e.local_x / w);
        switch (e.type)
        {
        case R_EVENT_MOUSE_DOWN:
        {
            const int hit = GradientKeyAt(*keys, w, e.local_x, e.local_y);
            if (e.button == R_MOUSE_RIGHT)
            {
                if (hit >= 0 && keys->size() > 1)
                {
                    keys->erase(keys->begin() + hit);
                    doc.selected_key = -1;
                    Touch(doc);
                    doc.keys_dirty = true;
                    Repaint(doc);
                    Rwidget_invalidate(widget);
                }
                return true;
            }
            if (e.button != R_MOUSE_LEFT)
                return false;
            if (hit >= 0)
            {
                if (doc.selected_key != hit)
                {
                    doc.selected_key = hit;
                    doc.keys_dirty = true;
                }
                doc.key_drag = hit;
            }
            else if (e.clicks >= 2)
            {
                // A new key with the color of the gradient there.
                keys->push_back({ t, Evaluate(*keys, t) });
                doc.selected_key = static_cast<int>(keys->size()) - 1;
                doc.key_drag = doc.selected_key;
                doc.keys_dirty = true;
                Touch(doc);
                Repaint(doc);
            }
            Rwidget_invalidate(widget);
            return true;
        }
        case R_EVENT_MOUSE_MOVE:
            if (doc.key_drag >= 0 && doc.key_drag < static_cast<int>(keys->size()) && ein::Mouse(0))
            {
                (*keys)[static_cast<size_t>(doc.key_drag)].time = t;
                eui::SetValue(doc.key_time, t);
                Touch(doc);
                Repaint(doc);
                Rwidget_invalidate(widget);
                return true;
            }
            return false;
        case R_EVENT_MOUSE_UP:
            if (doc.key_drag >= 0)
            {
                doc.key_drag = -1;
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    // ---- curve editor (Size / Rotation Over Life) ----------------------------------------

    constexpr float kCurveHeight = 150.f;
    constexpr float kCurvePad = 10.f;

    struct CurveView
    {
        float w = 1.f, h = 1.f, lo = 0.f, hi = 1.f;

        float X(float t) const { return kCurvePad + (w - kCurvePad * 2.f) * t; }
        float Y(float v) const { return h - kCurvePad - (h - kCurvePad * 2.f) * ((v - lo) / (hi - lo)); }
        void From(float x, float y, float& t, float& v) const
        {
            t = Clamp01((x - kCurvePad) / std::max(1.f, w - kCurvePad * 2.f));
            v = lo + (h - kCurvePad - y) / std::max(1.f, h - kCurvePad * 2.f) * (hi - lo);
        }
    };

    // Value range : the keys, with 0 and default_max, padded.
    static CurveView ViewOf(const std::vector<px::FloatKey>& keys, float default_max, float w, float h)
    {
        CurveView view;
        view.w = std::max(1.f, w);
        view.h = std::max(1.f, h);
        float lo = 0.f, hi = default_max;
        for (const px::FloatKey& k : keys)
        {
            lo = std::min(lo, k.value);
            hi = std::max(hi, k.value);
        }
        const float span = std::max(1e-3f, hi - lo);
        view.lo = lo - span * 0.1f;
        view.hi = hi + span * 0.1f;
        return view;
    }

    static int CurveKeyAt(const std::vector<px::FloatKey>& keys, const CurveView& view, float x, float y)
    {
        int best = -1;
        float best_d = 9.f * 9.f;
        for (int i = 0; i < static_cast<int>(keys.size()); ++i)
        {
            const px::FloatKey& k = keys[static_cast<size_t>(i)];
            const float dx = x - view.X(Clamp01(k.time)), dy = y - view.Y(k.value);
            const float d = dx * dx + dy * dy;
            if (d <= best_d)
            {
                best_d = d;
                best = i;
            }
        }
        return best;
    }

    static void PaintCurve(Doc& doc, RPainter* p, float w, float h)
    {
        const std::vector<px::FloatKey>* keys = CurveKeys(doc);
        if (!keys)
            return;
        const CurveView view = ViewOf(*keys, CurveDefaultMax(doc), w, h);
        Rdraw_rect(p, 0.f, 0.f, w, h, 4.f, 0.094f, 0.098f, 0.118f, 1.f);
        for (int i = 0; i <= 4; ++i)
        {
            const float x = view.X(i * 0.25f);
            Rdraw_line(p, x, view.Y(view.hi), x, view.Y(view.lo), 1.f, 0.196f, 0.204f, 0.235f, 1.f);
        }
        if (view.lo < 0.f && view.hi > 0.f)
            Rdraw_line(p, view.X(0.f), view.Y(0.f), view.X(1.f), view.Y(0.f), 1.f, 0.35f, 0.35f, 0.39f, 1.f);
        char label[32];
        std::snprintf(label, sizeof(label), "%.2f", view.hi);
        Rdraw_text(p, R_INVALID_ID, 11.f, 4.f, 2.f, label, 0.51f, 0.51f, 0.55f, 1.f);
        std::snprintf(label, sizeof(label), "%.2f", view.lo);
        Rdraw_text(p, R_INVALID_ID, 11.f, 4.f, h - 15.f, label, 0.51f, 0.51f, 0.55f, 1.f);

        constexpr int kSteps = 64;
        float px0 = view.X(0.f), py0 = view.Y(Evaluate(*keys, 0.f));
        for (int i = 1; i <= kSteps; ++i)
        {
            const float t = static_cast<float>(i) / kSteps;
            const float x = view.X(t), y = view.Y(Evaluate(*keys, t));
            Rdraw_line(p, px0, py0, x, y, 2.f, 0.35f, 0.78f, 0.9f, 1.f);
            px0 = x;
            py0 = y;
        }

        float accent[4];
        ThemeColor(R_COLOR_ACCENT, accent);
        for (int i = 0; i < static_cast<int>(keys->size()); ++i)
        {
            const px::FloatKey& k = (*keys)[static_cast<size_t>(i)];
            const float x = view.X(Clamp01(k.time)), y = view.Y(k.value);
            if (i == doc.selected_key)
                Rdraw_circle(p, x, y, 6.f, accent[0], accent[1], accent[2], 1.f);
            else
                Rdraw_circle(p, x, y, 4.5f, 0.78f, 0.78f, 0.82f, 1.f);
        }
    }

    static bool OnCurveEvent(Doc& doc, Rid widget, const REvent& e)
    {
        std::vector<px::FloatKey>* keys = CurveKeys(doc);
        if (!keys)
            return false;
        float rx = 0.f, ry = 0.f, w = 0.f, h = 0.f;
        Rwidget_get_rect(widget, &rx, &ry, &w, &h);
        const CurveView view = ViewOf(*keys, CurveDefaultMax(doc), w, h);
        switch (e.type)
        {
        case R_EVENT_MOUSE_DOWN:
        {
            const int hit = CurveKeyAt(*keys, view, e.local_x, e.local_y);
            if (e.button == R_MOUSE_RIGHT)
            {
                if (hit >= 0 && keys->size() > 1)
                {
                    keys->erase(keys->begin() + hit);
                    doc.selected_key = -1;
                    doc.keys_dirty = true;
                    Touch(doc);
                    Rwidget_invalidate(widget);
                }
                return true;
            }
            if (e.button != R_MOUSE_LEFT)
                return false;
            if (hit >= 0)
            {
                if (doc.selected_key != hit)
                {
                    doc.selected_key = hit;
                    doc.keys_dirty = true;
                }
                doc.key_drag = hit;
            }
            else if (e.clicks >= 2)
            {
                px::FloatKey k;
                view.From(e.local_x, e.local_y, k.time, k.value);
                keys->push_back(k);
                doc.selected_key = static_cast<int>(keys->size()) - 1;
                doc.key_drag = doc.selected_key;
                doc.keys_dirty = true;
                Touch(doc);
            }
            Rwidget_invalidate(widget);
            return true;
        }
        case R_EVENT_MOUSE_MOVE:
            if (doc.key_drag >= 0 && doc.key_drag < static_cast<int>(keys->size()) && ein::Mouse(0))
            {
                px::FloatKey& k = (*keys)[static_cast<size_t>(doc.key_drag)];
                view.From(e.local_x, e.local_y, k.time, k.value);
                eui::SetValue(doc.key_time, k.time);
                eui::SetValue(doc.key_value, k.value);
                Touch(doc);
                Repaint(doc);
                Rwidget_invalidate(widget);
                return true;
            }
            return false;
        case R_EVENT_MOUSE_UP:
            if (doc.key_drag >= 0)
            {
                doc.key_drag = -1;
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    // The fields of the selected key, under the curve / gradient.
    static void RebuildKeys(Doc& doc)
    {
        doc.keys_dirty = false;
        doc.key_time = doc.key_value = R_INVALID_ID;
        if (doc.key_box == R_INVALID_ID || !Rwidget_is_valid(doc.key_box))
            return;
        Rwidget_clear_children(doc.key_box);
        doc.building = true;
        const int key = doc.selected_key;
        if (std::vector<px::ColorKey>* keys = ColorKeys(doc); keys && key >= 0 && key < static_cast<int>(keys->size()))
        {
            const lynx::vec4 c = (*keys)[static_cast<size_t>(key)].color;
            Rid picker = Rcolor_picker_create();
            Rcolor_picker_set_popup(picker, R_TRUE);
            Rcolor_picker_set_alpha_enabled(picker, R_TRUE);
            Rcolor_picker_set_color(picker, c.x, c.y, c.z, c.w);
            Doc* d = &doc;
            eui::OnColor(picker, [d, key](float r, float g2, float b, float a)
            {
                if (!Alive(d) || d->building)
                    return;
                if (std::vector<px::ColorKey>* k = ColorKeys(*d); k && key < static_cast<int>(k->size()))
                {
                    (*k)[static_cast<size_t>(key)].color = lynx::vec4(r, g2, b, a);
                    Touch(*d);
                    Repaint(*d);
                }
            });
            AddRow(doc.key_box, "Color", picker);
            doc.key_time = NumberField(doc, doc.key_box, "Time", 0.0, 1.0, 0.01, 3, [key](Doc& x)
            {
                std::vector<px::ColorKey>* k = ColorKeys(x);
                return k && key < static_cast<int>(k->size()) ? &(*k)[static_cast<size_t>(key)].time : nullptr;
            });
        }
        else if (std::vector<px::FloatKey>* curve = CurveKeys(doc); curve && key >= 0 && key < static_cast<int>(curve->size()))
        {
            doc.key_time = NumberField(doc, doc.key_box, "Time", 0.0, 1.0, 0.01, 3, [key](Doc& x)
            {
                std::vector<px::FloatKey>* k = CurveKeys(x);
                return k && key < static_cast<int>(k->size()) ? &(*k)[static_cast<size_t>(key)].time : nullptr;
            });
            doc.key_value = NumberField(doc, doc.key_box, "Value", -1e6, 1e6, 0.01, 3, [key](Doc& x)
            {
                std::vector<px::FloatKey>* k = CurveKeys(x);
                return k && key < static_cast<int>(k->size()) ? &(*k)[static_cast<size_t>(key)].value : nullptr;
            });
        }
        doc.building = false;
    }

    // A curve or gradient widget of the Details.
    static void AddKeyEditor(Doc& doc, Rid box, bool gradient)
    {
        Rid w = Rwidget_add_child(box, Rwidget_create(R_WIDGET_CUSTOM));
        Rwidget_set_min_size(w, 140.f, gradient ? kGradientBar + kMarker * 2.2f : kCurveHeight);
        Rwidget_set_size(w, -1.f, gradient ? kGradientBar + kMarker * 2.2f : kCurveHeight);
        Rwidget_set_focusable(w, R_TRUE);
        Doc* d = &doc;
        if (gradient)
        {
            eui::OnPaint(w, [d](RPainter* p, float pw, float ph) { if (Alive(d)) PaintGradient(*d, p, pw, ph); });
            eui::OnEvent(w, [d, w](const REvent& e) { return Alive(d) && OnGradientEvent(*d, w, e); });
        }
        else
        {
            eui::OnPaint(w, [d](RPainter* p, float pw, float ph) { if (Alive(d)) PaintCurve(*d, p, pw, ph); });
            eui::OnEvent(w, [d, w](const REvent& e) { return Alive(d) && OnCurveEvent(*d, w, e); });
        }
        doc.repaint.push_back(w);
        AddNote(box, gradient ? "Double-click : add a key.  Drag a marker.  Right click : delete."
                              : "Double-click : add a key.  Drag a key.  Right click : delete.");
        doc.key_box = Rwidget_add_child(box, Rwidget_create(R_WIDGET_VBOX));
        Rpanel_set_spacing(doc.key_box, 5.f);
        RebuildKeys(doc);
    }

    // ---- Details ------------------------------------------------------------------------------

    static const std::vector<std::pair<px::Shape, const char*>> kShapes = {
        { px::Shape::Point, "Point" }, { px::Shape::Sphere, "Sphere" }, { px::Shape::Box, "Box" },
        { px::Shape::Cylinder, "Cylinder" }, { px::Shape::Cone, "Cone" },
    };
    static const std::vector<std::pair<px::Blend, const char*>> kBlends = {
        { px::Blend::Additive, "Additive (glow)" }, { px::Blend::Alpha, "Alpha (smoke)" },
        { px::Blend::Multiply, "Multiply (dark)" },
    };
    static const std::vector<std::pair<px::Render, const char*>> kRenders = {
        { px::Render::Billboard, "Billboard" }, { px::Render::Stretched, "Stretched (along the speed)" },
    };
    static const std::vector<std::pair<px::Space, const char*>> kSpaces = {
        { px::Space::World, "World (trails behind a moving actor)" },
        { px::Space::Local, "Local (moves with the actor)" },
    };

// The field `member` of the emitter of the Details, found again at each change.
#define PE_FIELD(member) [ei](Doc& x) { px::EmitterDesc* em_ = EmAt(x, ei); return em_ ? &em_->member : nullptr; }
#define PE_VEC(member) [ei](Doc& x) { px::EmitterDesc* em_ = EmAt(x, ei); return em_ ? &em_->member.x : nullptr; }

    static void RebuildDetails(Doc& doc)
    {
        doc.details_dirty = false;
        doc.key_box = doc.key_time = doc.key_value = R_INVALID_ID;
        doc.repaint.erase(std::remove_if(doc.repaint.begin(), doc.repaint.end(), [](Rid w) { return !Rwidget_is_valid(w); }),
                          doc.repaint.end());
        if (doc.details == R_INVALID_ID || !Rwidget_is_valid(doc.details))
            return;
        Rwidget_clear_children(doc.details);
        doc.building = true;
        const Rid box = doc.details;
        Doc* d = &doc;

        if (doc.module == Module::System || doc.desc.emitters.empty())
        {
            AddSection(box, "System");
            NumberField(doc, box, "Duration (s)", 0.05, 600.0, 0.05, 2, [](Doc& x) { return &x.desc.duration; },
                        "One cycle : bursts are timed in it ; a one-shot effect (Particles.spawn) lasts this long.");
            CheckField(doc, box, "Looping", [](Doc& x) { return &x.desc.looping; });
            AddNote(box, std::to_string(doc.desc.emitters.size()) + " emitter(s). Click a module of a column below.");
            doc.building = false;
            return;
        }

        const int ei = doc.emitter;
        px::EmitterDesc& em = doc.desc.emitters[static_cast<size_t>(ei)];
        AddSection(box, em.name + "  >  " + ModuleName(doc.module));

        if (ModuleFlag(em, doc.module))
        {
            const Module m = doc.module;
            CheckField(doc, box, "Module enabled", [ei, m](Doc& x) { px::EmitterDesc* e = EmAt(x, ei); return e ? ModuleFlag(*e, m) : nullptr; },
                       true);
        }

        switch (doc.module)
        {
        case Module::Required:
            AssetField(doc, box, "Texture", ei);
            EnumField(doc, box, "Blend", kBlends, PE_FIELD(blend));
            EnumField(doc, box, "Render", kRenders, PE_FIELD(render), true);
            if (em.render == px::Render::Stretched)
                NumberField(doc, box, "Stretch", 0.0, 10.0, 0.005, 3, PE_FIELD(stretch));
            EnumField(doc, box, "Space", kSpaces, PE_FIELD(space));
            VectorField(doc, box, "Size", 2, 0.01, PE_VEC(size));
            NumberField(doc, box, "Max particles", 1.0, 100000.0, 5.0, 0, PE_FIELD(max_particles));
            break;

        case Module::Spawn:
        {
            NumberField(doc, box, "Rate (/s)", 0.0, 100000.0, 0.5, 2, PE_FIELD(spawn_rate));
            AddSection(box, "Bursts");
            for (int i = 0; i < static_cast<int>(em.bursts.size()); ++i)
            {
                Rid row = Rwidget_add_child(box, Rwidget_create(R_WIDGET_HBOX));
                Rpanel_set_spacing(row, 4.f);
                Rid at = Rwidget_add_child(row, Rlabel_create("at"));
                Rslot_set_valign(at, R_VALIGN_CENTER);
                Rid time = Rwidget_add_child(row, MakeSpin(em.bursts[static_cast<size_t>(i)].time, 0.0, 600.0, 0.01, 2));
                Rslot_set_fill(time, 1.f);
                Rid s = Rwidget_add_child(row, Rlabel_create("s"));
                Rslot_set_valign(s, R_VALIGN_CENTER);
                Rid count = Rwidget_add_child(row, MakeSpin(em.bursts[static_cast<size_t>(i)].count, 0.0, 100000.0, 1.0, 0));
                Rslot_set_fill(count, 1.f);
                Rid particles = Rwidget_add_child(row, Rlabel_create("particles"));
                Rslot_set_valign(particles, R_VALIGN_CENTER);
                Rid remove = Rwidget_add_child(row, Rbutton_create("x"));
                Rwidget_add_class(remove, "tool");
                eui::SetTooltip(remove, "Remove this burst");
                eui::OnValue(time, [d, ei, i](double v)
                {
                    if (!Alive(d) || d->building) return;
                    if (px::EmitterDesc* e = EmAt(*d, ei); e && i < static_cast<int>(e->bursts.size()))
                    {
                        e->bursts[static_cast<size_t>(i)].time = static_cast<float>(v);
                        Touch(*d);
                    }
                });
                eui::OnValue(count, [d, ei, i](double v)
                {
                    if (!Alive(d) || d->building) return;
                    if (px::EmitterDesc* e = EmAt(*d, ei); e && i < static_cast<int>(e->bursts.size()))
                    {
                        e->bursts[static_cast<size_t>(i)].count = static_cast<int>(v);
                        Touch(*d);
                    }
                });
                eui::OnClick(remove, [d, ei, i]()
                {
                    DocLater(d, [ei, i](Doc& x)
                    {
                        if (px::EmitterDesc* e = EmAt(x, ei); e && i < static_cast<int>(e->bursts.size()))
                        {
                            e->bursts.erase(e->bursts.begin() + i);
                            Restructure(x);
                        }
                    });
                });
            }
            Rid add = Rwidget_add_child(box, Rbutton_create("+ Burst"));
            eui::OnClick(add, [d, ei]()
            {
                DocLater(d, [ei](Doc& x)
                {
                    if (px::EmitterDesc* e = EmAt(x, ei))
                    {
                        e->bursts.push_back({ 0.f, 20 });
                        Restructure(x);
                    }
                });
            });
            break;
        }

        case Module::Lifetime:
            RangeField(doc, box, "Lifetime (s)", 0.01, 600.0, 0.01, PE_FIELD(lifetime));
            break;

        case Module::Location:
            EnumField(doc, box, "Shape", kShapes, PE_FIELD(shape), true);
            if (em.shape == px::Shape::Sphere || em.shape == px::Shape::Cylinder || em.shape == px::Shape::Cone)
                NumberField(doc, box, "Radius", 0.0, 1000.0, 0.02, 2, PE_FIELD(shape_radius));
            if (em.shape == px::Shape::Box)
                VectorField(doc, box, "Box size", 3, 0.05, PE_VEC(shape_size));
            if (em.shape == px::Shape::Cone)
                NumberField(doc, box, "Angle (deg)", 0.0, 180.0, 0.5, 1, PE_FIELD(shape_angle));
            VectorField(doc, box, "Offset", 3, 0.05, PE_VEC(offset));
            VectorField(doc, box, "Rotation", 3, 0.5, PE_VEC(shape_rotation));
            break;

        case Module::Velocity:
            VectorField(doc, box, "Min", 3, 0.05, PE_VEC(velocity_min));
            VectorField(doc, box, "Max", 3, 0.05, PE_VEC(velocity_max));
            RangeField(doc, box, "Speed (shape)", -1000.0, 1000.0, 0.05, PE_FIELD(speed),
                       "Outwards from the shape (sphere) / along the cone, added to Min..Max.");
            break;

        case Module::Rotation:
            VectorField(doc, box, "Rotation min", 3, 0.5, PE_VEC(rotation_min));
            VectorField(doc, box, "Rotation max", 3, 0.5, PE_VEC(rotation_max));
            VectorField(doc, box, "Angular vel. min", 3, 0.5, PE_VEC(angular_min));
            VectorField(doc, box, "Angular vel. max", 3, 0.5, PE_VEC(angular_max));
            AddNote(box, "Degrees ; Z turns the sprite in 2D.");
            break;

        case Module::Forces:
            VectorField(doc, box, "Gravity", 3, 0.05, PE_VEC(gravity));
            NumberField(doc, box, "Drag", 0.0, 50.0, 0.01, 2, PE_FIELD(drag));
            VectorField(doc, box, "Force", 3, 0.05, PE_VEC(force));
            AddSection(box, "Noise (turbulence)");
            NumberField(doc, box, "Strength", 0.0, 100.0, 0.02, 2, PE_FIELD(noise_strength));
            NumberField(doc, box, "Frequency", 0.0, 50.0, 0.01, 2, PE_FIELD(noise_frequency));
            NumberField(doc, box, "Scroll speed", 0.0, 50.0, 0.01, 2, PE_FIELD(noise_scroll));
            break;

        case Module::ColorOverLife:
            AddKeyEditor(doc, box, true);
            break;

        case Module::SizeOverLife:
            AddNote(box, "Multiplies the Size of Required, over the life (0 -> 1).");
            AddKeyEditor(doc, box, false);
            break;

        case Module::RotationOverLife:
            AddNote(box, "Rotation in degrees over the life (0 -> 1).");
            AddKeyEditor(doc, box, false);
            break;

        case Module::Collision:
            NumberField(doc, box, "Restitution", 0.0, 1.0, 0.01, 2, PE_FIELD(restitution));
            NumberField(doc, box, "Friction", 0.0, 1.0, 0.01, 2, PE_FIELD(friction));
            AddNote(box, "Collides with the scene of the game (voxels, meshes).");
            break;

        default:
            break;
        }
        doc.building = false;
    }

#undef PE_FIELD
#undef PE_VEC

    // ---- Emitters : one column per emitter (Cascade) ---------------------------------------

    static void Select(Doc& doc, int emitter, Module module)
    {
        if (doc.emitter == emitter && doc.module == module)
            return;
        doc.emitter = emitter;
        doc.module = module;
        doc.selected_key = -1;
        doc.details_dirty = true;
        // The columns show the selection : outline of the column, row of the module.
        for (size_t i = 0; i < doc.column_borders.size(); ++i)
        {
            const bool on = static_cast<int>(i) == doc.emitter && doc.module != Module::System;
            float c[4];
            ThemeColor(R_COLOR_BORDER, c);
            if (on)
                Rborder_set_outline(doc.column_borders[i], 2.f, 0.96f, 0.56f, 0.2f, 1.f);
            else
                Rborder_set_outline(doc.column_borders[i], 1.f, c[0], c[1], c[2], c[3]);
        }
        Repaint(doc);
    }

    static void RenameEmitter(Doc* d, int index)
    {
        if (!Alive(d) || !EmAt(*d, index))
            return;
        eui::AskText(g.window, "Rename emitter", "Name", EmAt(*d, index)->name, [d, index](const std::string& name)
        {
            if (!Alive(d) || name.empty())
                return;
            if (px::EmitterDesc* e = EmAt(*d, index))
            {
                e->name = name;
                Restructure(*d);
            }
        });
    }

    static void EmitterMenu(Doc& doc, int i)
    {
        Doc* d = &doc;
        const int count = static_cast<int>(doc.desc.emitters.size());
        g_menu = std::make_unique<eui::Menu>(g.window);
        eui::Menu& menu = *g_menu;
        menu.Title(doc.desc.emitters[static_cast<size_t>(i)].name);
        menu.Separator();
        menu.Item("Rename", [d, i]() { RenameEmitter(d, i); });
        menu.Item("Duplicate", [d, i]()
        {
            DocLater(d, [i](Doc& x)
            {
                if (!EmAt(x, i)) return;
                px::EmitterDesc copy = *EmAt(x, i);
                copy.name += " copy";
                x.desc.emitters.insert(x.desc.emitters.begin() + i + 1, copy);
                x.emitter = i + 1;
                Restructure(x);
            });
        });
        menu.Item("Move left", [d, i]()
        {
            DocLater(d, [i](Doc& x)
            {
                if (i <= 0 || !EmAt(x, i)) return;
                std::swap(x.desc.emitters[static_cast<size_t>(i)], x.desc.emitters[static_cast<size_t>(i - 1)]);
                x.emitter = i - 1;
                Restructure(x);
            });
        }, i > 0);
        menu.Item("Move right", [d, i]()
        {
            DocLater(d, [i](Doc& x)
            {
                if (!EmAt(x, i + 1)) return;
                std::swap(x.desc.emitters[static_cast<size_t>(i)], x.desc.emitters[static_cast<size_t>(i + 1)]);
                x.emitter = i + 1;
                Restructure(x);
            });
        }, i + 1 < count);

        eui::Menu& add = menu.Sub("Add module");
        bool any = false;
        px::EmitterDesc& em = doc.desc.emitters[static_cast<size_t>(i)];
        for (int m = static_cast<int>(Module::Required); m < static_cast<int>(Module::Count); ++m)
        {
            const Module mod = static_cast<Module>(m);
            bool* flag = ModuleFlag(em, mod);
            if (!flag || *flag)
                continue;
            any = true;
            add.Item(ModuleName(mod), [d, i, mod]()
            {
                DocLater(d, [i, mod](Doc& x)
                {
                    px::EmitterDesc* e = EmAt(x, i);
                    bool* f = e ? ModuleFlag(*e, mod) : nullptr;
                    if (!f) return;
                    *f = true;
                    x.emitter = i;
                    x.module = mod;
                    x.selected_key = -1;
                    Restructure(x);
                });
            });
        }
        if (!any)
            add.Title("Every module is there.");

        menu.Separator();
        menu.Item("Delete", [d, i]()
        {
            DocLater(d, [i](Doc& x)
            {
                if (!EmAt(x, i)) return;
                x.desc.emitters.erase(x.desc.emitters.begin() + i);
                x.emitter = std::clamp(x.emitter, 0, std::max(0, static_cast<int>(x.desc.emitters.size()) - 1));
                if (x.desc.emitters.empty())
                    x.module = Module::System;
                Restructure(x);
            });
        });
        menu.Open();
    }

    static void ModuleMenu(Doc& doc, int i, Module mod)
    {
        Doc* d = &doc;
        g_menu = std::make_unique<eui::Menu>(g.window);
        eui::Menu& menu = *g_menu;
        menu.Title(ModuleName(mod));
        menu.Separator();
        menu.Item("Remove module", [d, i, mod]()
        {
            DocLater(d, [i, mod](Doc& x)
            {
                px::EmitterDesc* e = EmAt(x, i);
                bool* f = e ? ModuleFlag(*e, mod) : nullptr;
                if (!f) return;
                *f = false;
                if (x.emitter == i && x.module == mod)
                    x.module = Module::Required;
                Restructure(x);
            });
        });
        menu.Open();
    }

    constexpr float kColumnWidth = 196.f;
    constexpr float kRowHeight = 24.f;

    static void PaintName(Doc& doc, int i, RPainter* p, float w, float h)
    {
        const px::EmitterDesc* em = EmAt(doc, i);
        if (!em)
            return;
        float c[4];
        ThemeColor(em->enabled ? R_COLOR_TEXT : R_COLOR_TEXT_DISABLED, c);
        Rdraw_push_clip_rect(p, 0.f, 0.f, w, h);
        Rdraw_text(p, R_INVALID_ID, 0.f, 2.f, std::max(0.f, (h - 15.f) * 0.5f), em->name.c_str(), c[0], c[1], c[2], c[3]);
        Rdraw_pop_clip_rect(p);
    }

    static void PaintModule(Doc& doc, int i, Module mod, RPainter* p, float w, float h)
    {
        const px::EmitterDesc* em = EmAt(doc, i);
        if (!em)
            return;
        const bool selected = doc.emitter == i && doc.module == mod;
        float c[4];
        if (selected)
        {
            ThemeColor(R_COLOR_SELECTION, c);
            Rdraw_rect(p, 0.f, 0.f, w, h, 3.f, c[0], c[1], c[2], c[3]);
        }
        const Rgb bar = ModuleColor(mod);
        Rdraw_rect(p, 0.f, 0.f, 4.f, h, 0.f, bar.r, bar.g, bar.b, 1.f);
        ThemeColor(R_COLOR_TEXT, c);
        Rdraw_push_clip_rect(p, 0.f, 0.f, mod == Module::ColorOverLife ? w * 0.66f : w, h);
        Rdraw_text(p, R_INVALID_ID, 0.f, 10.f, std::max(0.f, (h - 15.f) * 0.5f), ModuleName(mod), c[0], c[1], c[2], c[3]);
        Rdraw_pop_clip_rect(p);
        // Little preview : the gradient.
        if (mod == Module::ColorOverLife)
        {
            const float sw = w * 0.30f;
            PaintGradientStrip(p, w - sw - 4.f, h * 0.25f, sw, h * 0.5f, em->color);
        }
    }

    static void RebuildEmitters(Doc& doc)
    {
        doc.emitters_dirty = false;
        doc.column_borders.clear();
        doc.repaint.erase(std::remove_if(doc.repaint.begin(), doc.repaint.end(), [](Rid w) { return !Rwidget_is_valid(w); }),
                          doc.repaint.end());
        if (doc.columns == R_INVALID_ID || !Rwidget_is_valid(doc.columns))
            return;
        Rwidget_clear_children(doc.columns);
        Doc* d = &doc;
        float border[4];
        ThemeColor(R_COLOR_BORDER, border);

        for (int i = 0; i < static_cast<int>(doc.desc.emitters.size()); ++i)
        {
            const px::EmitterDesc& em = doc.desc.emitters[static_cast<size_t>(i)];
            Rid column = Rwidget_add_child(doc.columns, Rwidget_create(R_WIDGET_BORDER));
            Rwidget_set_size(column, kColumnWidth, -1.f);
            Rborder_set_padding(column, 4.f, 4.f, 4.f, 4.f);
            Rborder_set_corner_radius(column, 4.f, 4.f, 4.f, 4.f);
            const bool on = i == doc.emitter && doc.module != Module::System;
            if (on)
                Rborder_set_outline(column, 2.f, 0.96f, 0.56f, 0.2f, 1.f);
            else
                Rborder_set_outline(column, 1.f, border[0], border[1], border[2], border[3]);
            doc.column_borders.push_back(column);

            Rid inner = Rwidget_add_child(column, Rwidget_create(R_WIDGET_VBOX));
            Rpanel_set_spacing(inner, 1.f);

            // Header : enabled, name (double-click : rename ; right click : menu).
            Rid header = Rwidget_add_child(inner, Rwidget_create(R_WIDGET_HBOX));
            Rpanel_set_spacing(header, 4.f);
            Rid enabled = Rwidget_add_child(header, Rcheckbox_create(""));
            Rcheckbox_set_checked(enabled, em.enabled ? R_TRUE : R_FALSE);
            eui::SetTooltip(enabled, "Emitter enabled");
            eui::OnToggle(enabled, [d, i](bool on_)
            {
                if (!Alive(d)) return;
                if (px::EmitterDesc* e = EmAt(*d, i))
                {
                    e->enabled = on_;
                    Touch(*d);
                    Repaint(*d);
                }
            });
            Rid name = Rwidget_add_child(header, Rwidget_create(R_WIDGET_CUSTOM));
            Rslot_set_fill(name, 1.f);
            Rwidget_set_size(name, -1.f, kRowHeight);
            eui::SetTooltip(name, "Double-click : rename. Right click : the menu of the emitter.");
            eui::OnPaint(name, [d, i](RPainter* p, float w, float h) { if (Alive(d)) PaintName(*d, i, p, w, h); });
            eui::OnEvent(name, [d, i](const REvent& e)
            {
                if (!Alive(d) || e.type != R_EVENT_MOUSE_DOWN || !EmAt(*d, i))
                    return false;
                if (e.button == R_MOUSE_RIGHT)
                {
                    Select(*d, i, d->module == Module::System ? Module::Required : d->module);
                    EmitterMenu(*d, i);
                    return true;
                }
                if (e.button != R_MOUSE_LEFT)
                    return false;
                Select(*d, i, d->module == Module::System ? Module::Required : d->module);
                if (e.clicks >= 2)
                    eui::Later([d, i]() { RenameEmitter(d, i); });
                return true;
            });
            doc.repaint.push_back(name);
            Rwidget_add_child(inner, Rwidget_create(R_WIDGET_SEPARATOR));

            // Module blocks
            for (int m = static_cast<int>(Module::Required); m < static_cast<int>(Module::Count); ++m)
            {
                const Module mod = static_cast<Module>(m);
                const bool* flag = ModuleFlag(const_cast<px::EmitterDesc&>(em), mod);
                if (flag && !*flag)
                    continue;   // removed : "Add module" in the menu of the column
                Rid row = Rwidget_add_child(inner, Rwidget_create(R_WIDGET_CUSTOM));
                Rwidget_set_size(row, -1.f, kRowHeight);
                Rwidget_set_cursor(row, R_CURSOR_HAND);
                eui::OnPaint(row, [d, i, mod](RPainter* p, float w, float h) { if (Alive(d)) PaintModule(*d, i, mod, p, w, h); });
                eui::OnEvent(row, [d, i, mod](const REvent& e)
                {
                    if (!Alive(d) || e.type != R_EVENT_MOUSE_DOWN || !EmAt(*d, i))
                        return false;
                    if (e.button == R_MOUSE_LEFT)
                    {
                        Select(*d, i, mod);
                        return true;
                    }
                    if (e.button == R_MOUSE_RIGHT)
                    {
                        Select(*d, i, mod);
                        px::EmitterDesc* em_ = EmAt(*d, i);
                        if (em_ && ModuleFlag(*em_, mod))
                            ModuleMenu(*d, i, mod);
                        return true;
                    }
                    return false;
                });
                doc.repaint.push_back(row);
            }
        }

        // "+ Emitter" column
        Rid add = Rwidget_add_child(doc.columns, Rbutton_create("+ Emitter"));
        Rwidget_set_size(add, kColumnWidth * 0.6f, kRowHeight * 2.f);
        Rslot_set_valign(add, R_VALIGN_TOP);
        eui::OnClick(add, [d]()
        {
            DocLater(d, [](Doc& x)
            {
                px::EmitterDesc em;
                em.name = "Emitter " + std::to_string(x.desc.emitters.size() + 1);
                x.desc.emitters.push_back(em);
                x.emitter = static_cast<int>(x.desc.emitters.size()) - 1;
                x.module = Module::Required;
                x.selected_key = -1;
                Restructure(x);
            });
        });
    }

    // ---- Preview -----------------------------------------------------------------------------

    static bool OnViewEvent(Doc& doc, const REvent& e)
    {
        Preview& pv = doc.preview;
        switch (e.type)
        {
        case R_EVENT_MOUSE_DOWN:
            Rwidget_focus(doc.view);
            if (e.button == R_MOUSE_RIGHT) doc.camera_drag = 1;
            else if (e.button == R_MOUSE_MIDDLE) doc.camera_drag = 2;
            return true;
        case R_EVENT_MOUSE_MOVE:
            if (doc.camera_drag == 1)
            {
                pv.yaw += e.delta_x * 0.3f;
                pv.pitch = std::clamp(pv.pitch - e.delta_y * 0.3f, -89.f, 89.f);
                return true;
            }
            if (doc.camera_drag == 2)
            {
                const float k = pv.distance * 0.0015f;
                const float y = pv.yaw * kDegToRad;
                // right = (-sin yaw, 0, cos yaw) ; up ~ +Y
                pv.target[0] -= -std::sin(y) * e.delta_x * k;
                pv.target[2] -= std::cos(y) * e.delta_x * k;
                pv.target[1] += e.delta_y * k;
                return true;
            }
            return false;
        case R_EVENT_MOUSE_UP:
            if (doc.camera_drag >= 0)
            {
                doc.camera_drag = -1;
                return true;
            }
            return false;
        case R_EVENT_MOUSE_WHEEL:
            if (e.wheel_y == 0.f)
                return false;
            pv.distance = std::clamp(pv.distance * std::pow(0.88f, e.wheel_y), 0.5f, 500.f);
            return true;
        case R_EVENT_KEY_DOWN:
            if (e.key == R_KEY_F && !(e.modifiers & R_MOD_CTRL))
            {
                pv.Frame();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    // The tab of the document is the one shown, and its Preview panel too.
    static bool PreviewShown(Doc& doc)
    {
        if (doc.view == R_INVALID_ID || !Rwidget_is_valid(doc.view))
            return false;
        if (documents::Tabs() && Rtab_view_get_active(g.documents) != Rtab_view_find_tab(g.documents, doc.panel))
            return false;
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        Rwidget_get_rect(doc.view, &x, &y, &w, &h);
        return w > 1.f && h > 1.f;
    }

    static void UpdatePreview(Doc& doc)
    {
        Preview& pv = doc.preview;
        if (!PreviewShown(doc))
            return;
        pv.Create();
        if (pv.scene == kInvalid)
        {
            eui::SetText(doc.status, "Preview unavailable (renderer).");
            return;
        }

        int w = 0, h = 0;
        Rviewport_get_pixel_size(doc.view, &w, &h);
        w = std::max(16, w);
        h = std::max(16, h);
        if (w != pv.width || h != pv.height)
        {
            pv.width = w;
            pv.height = h;
            HRL_ResizeSceneTexture(pv.scene, w, h);
        }

        // Rebuild the system when the asset changed (not on every frame of a drag).
        const double now = Rget_time();
        if (doc.rebuild_preview && (!ein::Mouse(0) || now - doc.last_build > 0.25))
        {
            doc.rebuild_preview = false;
            doc.last_build = now;
            pv.instance.Create(doc.desc, pv.scene);
            pv.instance.SetTimeScale(pv.time_scale);
            pv.instance.SetLooping(doc.desc.looping);
            if (pv.paused)
                pv.instance.Pause();
        }

        pv.ApplyCamera();
        pv.DrawGrid();

        const unsigned int texture = HRL_GL_GetSceneTextureGL_ID(pv.scene);
        int tex_w = 0, tex_h = 0;
        if (lynx::editor::gl::TextureSize(texture, tex_w, tex_h))
            Rviewport_set_texture(doc.view, pv.link.Update(texture, tex_w, tex_h));
    }

    static void TogglePause(Doc& doc)
    {
        Preview& pv = doc.preview;
        pv.paused = !pv.paused;
        if (pv.paused) pv.instance.Pause(); else pv.instance.Play();
        eui::SetText(doc.play, pv.paused ? "Play" : "Pause");
    }

    // ---- The panels -------------------------------------------------------------------------

    static Rid ToolButton(Rid bar, const char* text, const char* tip, std::function<void()> fn)
    {
        Rid b = Rwidget_add_child(bar, Rbutton_create(text));
        Rwidget_add_class(b, "tool");
        if (tip)
            eui::SetTooltip(b, tip);
        eui::OnClick(b, std::move(fn));
        return b;
    }

    static Rid ToolLabel(Rid bar, const char* text)
    {
        Rid l = Rwidget_add_child(bar, Rlabel_create(text));
        Rwidget_add_class(l, "dim");
        Rslot_set_valign(l, R_VALIGN_CENTER);
        return l;
    }

    // Preview : the toolbar, the scene, a status line.
    static Rid BuildPreviewPanel(Doc& doc)
    {
        Doc* d = &doc;
        Rid panel = Rwidget_create(R_WIDGET_VBOX);
        Rpanel_set_spacing(panel, 4.f);

        Rid bar = Rwidget_add_child(panel, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(bar, 6.f);
        Rslot_set_padding(bar, 4.f, 4.f, 4.f, 0.f);

        Rid save = Rwidget_add_child(bar, Rbutton_create("Save"));
        Rbutton_set_style(save, R_BUTTON_PRIMARY);
        eui::SetTooltip(save, "Ctrl+S");
        eui::OnClick(save, [d]() { if (Alive(d)) { Commit(*d); SaveDoc(*d); } });
        ToolButton(bar, "Restart", "Play the system from the start (Space)", [d]() { if (Alive(d)) d->preview.instance.Restart(); });
        doc.play = ToolButton(bar, "Pause", "Pause / play the preview", [d]() { if (Alive(d)) TogglePause(*d); });

        ToolLabel(bar, "Speed");
        doc.speed = Rwidget_add_child(bar, MakeSpin(1.0, 0.0, 3.0, 0.05, 2));
        Rwidget_set_size(doc.speed, 70.f, -1.f);
        eui::SetTooltip(doc.speed, "Speed of the preview");
        eui::OnValue(doc.speed, [d](double v)
        {
            if (!Alive(d)) return;
            d->preview.time_scale = static_cast<float>(v);
            d->preview.instance.SetTimeScale(d->preview.time_scale);
        });

        doc.loop = Rwidget_add_child(bar, Rcheckbox_create("Loop"));
        Rslot_set_valign(doc.loop, R_VALIGN_CENTER);
        Rcheckbox_set_checked(doc.loop, doc.desc.looping ? R_TRUE : R_FALSE);
        eui::OnToggle(doc.loop, [d](bool on)
        {
            if (!Alive(d) || d->desc.looping == on) return;
            d->desc.looping = on;
            Touch(*d);
            d->details_dirty = d->module == Module::System;
        });

        ToolLabel(bar, "Duration");
        doc.duration = Rwidget_add_child(bar, MakeSpin(doc.desc.duration, 0.05, 600.0, 0.05, 2));
        Rwidget_set_size(doc.duration, 76.f, -1.f);
        eui::SetTooltip(doc.duration, "Duration of a cycle in seconds (bursts, one-shot effects).");
        eui::OnValue(doc.duration, [d](double v)
        {
            if (!Alive(d) || d->desc.duration == static_cast<float>(v)) return;
            d->desc.duration = static_cast<float>(v);
            Touch(*d);
        });

        ToolButton(bar, "System", "Settings of the whole system", [d]()
        {
            if (!Alive(d)) return;
            Select(*d, d->emitter, Module::System);
        });

        doc.grid = Rwidget_add_child(bar, Rcheckbox_create("Grid"));
        Rslot_set_valign(doc.grid, R_VALIGN_CENTER);
        Rcheckbox_set_checked(doc.grid, R_TRUE);
        eui::OnToggle(doc.grid, [d](bool on) { if (Alive(d)) d->preview.grid = on; });

        Rid background = Rwidget_add_child(bar, Rcolor_picker_create());
        Rcolor_picker_set_popup(background, R_TRUE);
        Rcolor_picker_set_alpha_enabled(background, R_FALSE);
        {
            const float* b = doc.preview.background;
            Rcolor_picker_set_color(background, b[0], b[1], b[2], 1.f);
        }
        Rslot_set_valign(background, R_VALIGN_CENTER);
        eui::SetTooltip(background, "Background of the preview");
        eui::OnColor(background, [d](float r, float g2, float b, float)
        {
            if (!Alive(d)) return;
            d->preview.background[0] = r;
            d->preview.background[1] = g2;
            d->preview.background[2] = b;
        });

        ToolButton(bar, "Frame", "Put the camera back (F)", [d]() { if (Alive(d)) d->preview.Frame(); });

        Rid spacer = Rwidget_add_child(bar, Rwidget_create(R_WIDGET_SPACER));
        Rslot_set_fill(spacer, 1.f);
        ToolButton(bar, "Undo", "Ctrl+Z", [d]() { DocLater(d, [](Doc& x) { Undo(x); }); });
        ToolButton(bar, "Redo", "Ctrl+Y", [d]() { DocLater(d, [](Doc& x) { Redo(x); }); });

        doc.view = Rwidget_add_child(panel, Rwidget_create(R_WIDGET_VIEWPORT));
        Rslot_set_fill(doc.view, 1.f);
        Rwidget_set_focusable(doc.view, R_TRUE);
        Rviewport_set_realtime(doc.view, R_TRUE);
        eui::OnEvent(doc.view, [d](const REvent& e) { return Alive(d) && OnViewEvent(*d, e); });

        doc.status = Rwidget_add_child(panel, Rlabel_create("Right drag : orbit   Middle drag : pan   Wheel : zoom   F : frame"));
        Rwidget_add_class(doc.status, "dim");
        Rslot_set_padding(doc.status, 6.f, 0.f, 6.f, 4.f);
        return panel;
    }

    static bool OnPanelClose(Rid page);

    static void BuildPanels(Doc& doc)
    {
        const std::string title = doc.path.filename().string();
        auto page = std::make_shared<Rid>(R_INVALID_ID);
        Rid dock = R_INVALID_ID;
        doc.panel = documents::Open(title, lynx::editor::file_icons::ForFile(doc.path, false),
                                    [page]() { if (OnPanelClose(*page)) documents::Close(*page); }, &dock);
        *page = doc.panel;
        doc.shown_title = title;
        if (doc.panel == R_INVALID_ID || dock == R_INVALID_ID)
            return;

        using lynx::editor::icons::Icon;
        const Rid preview = BuildPreviewPanel(doc);

        const Rid details = Rwidget_create(R_WIDGET_SCROLL_BOX);
        doc.details = Rwidget_add_child(details, Rwidget_create(R_WIDGET_VBOX));
        Rpanel_set_spacing(doc.details, 5.f);
        Rslot_set_padding(doc.details, 8.f, 8.f, 8.f, 8.f);

        const Rid emitters = Rwidget_create(R_WIDGET_SCROLL_BOX);
        Rscroll_box_set_orientation(emitters, R_HORIZONTAL);
        Rscroll_box_set_scroll_both(emitters, R_TRUE);
        doc.columns = Rwidget_add_child(emitters, Rwidget_create(R_WIDGET_HBOX));
        Rpanel_set_spacing(doc.columns, 6.f);
        Rslot_set_padding(doc.columns, 6.f, 6.f, 6.f, 6.f);

        documents::AddPanel(dock, preview, "Preview", R_DOCK_TAB, R_INVALID_ID, 0.f, Icon::Eye);
        documents::AddPanel(dock, emitters, "Emitters", R_DOCK_BOTTOM, R_INVALID_ID, 0.38f, Icon::Outliner);
        documents::AddPanel(dock, details, "Details", R_DOCK_RIGHT, preview, 0.34f, Icon::Details);

        // Shortcuts of the editor (the level editor waits : HasFocus).
        Doc* d = &doc;
        eui::OnEvent(doc.panel, [d](const REvent& e)
        {
            if (e.type != R_EVENT_KEY_DOWN || !Alive(d))
                return false;
            const bool ctrl = (e.modifiers & R_MOD_CTRL) != 0;
            if (ctrl && e.key == R_KEY_S) { Commit(*d); SaveDoc(*d); return true; }
            if (ctrl && e.key == R_KEY_Z) { DocLater(d, [](Doc& x) { Undo(x); }); return true; }
            if (ctrl && e.key == R_KEY_Y) { DocLater(d, [](Doc& x) { Redo(x); }); return true; }
            if (!ctrl && e.key == R_KEY_SPACE && !ein::WantTextInput())
            {
                d->preview.instance.Restart();
                return true;
            }
            return false;
        });
    }

    static void CloseDoc(Doc* doc)
    {
        const auto it = std::find_if(g_docs.begin(), g_docs.end(), [doc](const auto& d) { return d.get() == doc; });
        if (it == g_docs.end())
            return;
        const Rid panel = doc->panel;
        if (doc->view != R_INVALID_ID && Rwidget_is_valid(doc->view))
            Rviewport_set_texture(doc->view, R_INVALID_ID);
        doc->preview.Destroy();
        g_docs.erase(it);
        if (panel != R_INVALID_ID && Rwidget_is_valid(panel))
            documents::Close(panel);
    }

    // The x of the tab (or a middle click) : asks first when there are changes.
    static bool OnPanelClose(Rid page)
    {
        Doc* doc = nullptr;
        for (auto& d : g_docs)
            if (d->panel == page)
                doc = d.get();
        if (!doc)
            return true;
        Commit(*doc);
        if (!doc->dirty)
        {
            DocLater(doc, [](Doc& x) { CloseDoc(&x); });
            return false;
        }
        eui::Ask(g.window, "Unsaved particle system", doc->path.filename().string() + " has unsaved changes. Save it before closing ?",
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
            Status(doc, "The file was changed outside of the editor : saving will replace it.");
        else
        {
            LoadDoc(doc);
            Status(doc, "Reloaded (changed on disk).");
        }
    }

    static void UpdateDoc(Doc& doc)
    {
        if (doc.panel == R_INVALID_ID || !Rwidget_is_valid(doc.panel))
            return;
        if (doc.emitters_dirty)
            RebuildEmitters(doc);
        if (doc.details_dirty)
            RebuildDetails(doc);
        else if (doc.keys_dirty)
            RebuildKeys(doc);

        // One undo step once the edit is done (not one per frame of a drag).
        if (doc.touched && !ein::Mouse(0))
            Commit(doc);
        CheckDisk(doc);
        UpdatePreview(doc);

        // The toolbar follows undo / redo.
        doc.building = true;
        eui::SetChecked(doc.loop, doc.desc.looping);
        eui::SetValue(doc.duration, doc.desc.duration);
        doc.building = false;

        const bool dirty = doc.dirty || (doc.touched && px::ToJson(doc.desc) != doc.saved);
        const std::string title = doc.path.filename().string() + (dirty ? " *" : "");
        if (title != doc.shown_title)
        {
            doc.shown_title = title;
            documents::SetTitle(doc.panel, title);
        }
        if (Rget_time() - doc.status_time > 6.0 && doc.status_time > 0.0)
        {
            doc.status_time = -100.0;
            eui::SetText(doc.status, "Right drag : orbit   Middle drag : pan   Wheel : zoom   F : frame");
        }
    }

    static void Update()
    {
        for (size_t i = 0; i < g_docs.size(); ++i)
            UpdateDoc(*g_docs[i]);
    }
}


// =============================================================================
// lynx::editor::particle_editor (ParticleEditor.h)
// =============================================================================

namespace lynx::editor::particle_editor
{
    namespace ep = editor_ui::particles;

    bool CanOpen(const std::filesystem::path& path)
    {
        return ep::PExtension(path) == ".vfx";
    }

    void Open(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
        for (auto& doc : ep::g_docs)
            if (doc->path == absolute || std::filesystem::equivalent(doc->path, absolute, ec))
            {
                editor_ui::documents::Show(doc->panel);
                return;
            }
        auto doc = std::make_unique<ep::Doc>();
        doc->path = absolute;
        ep::LoadDoc(*doc);
        ep::g_docs.push_back(std::move(doc));
        ep::BuildPanels(*ep::g_docs.back());
        ep::UpdateDoc(*ep::g_docs.back());
    }

    void DrawAll(unsigned int) { ep::Update(); }

    bool HasFocus()
    {
        const Rid focused = Rwindow_get_focused_widget(editor_ui::g.window);
        for (auto& doc : ep::g_docs)
            if (editor_ui::IsInside(focused, doc->panel))
                return true;
        return false;
    }

    static bool InsideOf(const std::filesystem::path& path, const std::filesystem::path& folder, std::filesystem::path& relative)
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
        for (auto& doc : ep::g_docs)
        {
            std::filesystem::path relative;
            if (!InsideOf(doc->path, from, relative))
                continue;
            doc->path = relative.empty() ? destination : destination / relative;
            doc->write_time = std::filesystem::last_write_time(doc->path, ec);
            doc->shown_title.clear();
        }
    }

    void PathDeleted(const std::filesystem::path& target)
    {
        std::vector<ep::Doc*> gone;
        for (auto& doc : ep::g_docs)
        {
            std::filesystem::path relative;
            if (InsideOf(doc->path, target, relative))
                gone.push_back(doc.get());
        }
        for (ep::Doc* doc : gone)
            ep::CloseDoc(doc);
    }

    bool HasUnsavedChanges()
    {
        return std::any_of(ep::g_docs.begin(), ep::g_docs.end(), [](const auto& d)
        {
            return d->dirty || (d->touched && lynx::particles::ToJson(d->desc) != d->saved);
        });
    }

    void SaveAll()
    {
        for (auto& doc : ep::g_docs)
            if (doc->dirty || doc->touched)
            {
                ep::Commit(*doc);
                if (doc->dirty)
                    ep::SaveDoc(*doc);
            }
    }

    std::string NewFileTemplate() { return lynx::particles::NewTemplate(); }

    void SetAssetFieldDrawer(std::function<bool(std::string&)>) {}

    // Before the renderer shuts down : the preview scenes go first.
    void Shutdown()
    {
        for (auto& doc : ep::g_docs)
        {
            if (doc->view != R_INVALID_ID && Rwidget_is_valid(doc->view))
                Rviewport_set_texture(doc->view, R_INVALID_ID);
            doc->preview.Destroy();
        }
        ep::g_docs.clear();
        ep::g_menu.reset();
    }
}
