// =============================================================================
// Editor commands (included by EditorMain.cpp : uses its static state)
// -----------------------------------------------------------------------------
// The commands that Python scripts / AI tools run on the editor (see
// commands/CommandRegistry.h and python/README.md). They reuse what the editor
// UI does : undo entries, selection, snapshots, dirty flag...
//
// Coordinates : world units for actors (transform.location), voxel cells
// (integers) for voxels.
// =============================================================================

namespace cmd = lynx::editor::commands;
using CmdJson = cmd::Json;


// -----------------------------------------------------------------------------
// Undo groups (commands) : a batch / a script group is ONE Ctrl+Z
// -----------------------------------------------------------------------------

static int cmdUndoGroupDepth = 0;
static std::vector<std::function<void()>> cmdUndoGroup;

// Undo entry of a command. Not recorded while playing (the level is restored
// at Stop anyway).
static void PushCommandUndo(std::function<void()> undo)
{
    if (isPlaying || !undo)
        return;

    if (cmdUndoGroupDepth > 0)
        cmdUndoGroup.push_back(std::move(undo));
    else
        PushEditorUndo(std::move(undo));
}

static void BeginCommandUndoGroup(const std::string&)
{
    ++cmdUndoGroupDepth;
}

static void EndCommandUndoGroup()
{
    if (cmdUndoGroupDepth <= 0)
        return;

    if (--cmdUndoGroupDepth > 0 || cmdUndoGroup.empty())
        return;

    auto group = std::make_shared<std::vector<std::function<void()>>>(std::move(cmdUndoGroup));
    cmdUndoGroup.clear();

    PushEditorUndo([group]()
    {
        for (auto it = group->rbegin(); it != group->rend(); ++it)
            (*it)();
    });
}

// Something changed in the level (not while playing : nothing to save then).
static void CommandChangedLevel()
{
    if (!isPlaying)
        MarkEditorDirty();
}


// -----------------------------------------------------------------------------
// Level / actors helpers
// -----------------------------------------------------------------------------

static lynx::Level* CmdLevel()
{
    if (!editor_level)
        throw cmd::CommandError("no level loaded (the game is being reloaded ?)");

    return editor_level;
}

static lynx::Actor* CmdActor(const CmdJson& params, const char* key = "id")
{
    const std::string id = cmd::GetString(params, key);
    lynx::Actor* actor = CmdLevel()->GetActorFromID(id.c_str());

    if (!actor)
        throw cmd::CommandError("unknown actor '" + id + "' (see level.list_actors)");

    return actor;
}

static CmdJson CmdVec(const lynx::vec2& v) { return { v.x, v.y }; }
static CmdJson CmdVec(const lynx::vec3& v) { return { v.x, v.y, v.z }; }
static CmdJson CmdVec(const lynx::vec4& v) { return { v.x, v.y, v.z, v.w }; }

static CmdJson CmdTransform(const lynx::transform& t)
{
    return {
        { "location", CmdVec(t.location) },
        { "rotation", CmdVec(t.rotation) },
        { "scale", CmdVec(t.scale) },
    };
}

static const char* CmdPropertyTypeName(size_t type)
{
    static const char* const kNames[] = { "int", "float", "bool", "string", "vec2", "vec3", "vec4", "transform" };
    return type < 8 ? kNames[type] : "unknown";
}

static CmdJson CmdPropertyValue(const lynx::property& property)
{
    EditorPropertyValue value;

    if (!CaptureEditorProperty(property, value))
        return nullptr;

    return std::visit([](const auto& v) -> CmdJson
    {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, lynx::transform>)
            return CmdTransform(v);
        else if constexpr (std::is_same_v<T, lynx::vec2> || std::is_same_v<T, lynx::vec3> || std::is_same_v<T, lynx::vec4>)
            return CmdVec(v);
        else
            return CmdJson(v);
    }, value);
}

static void CmdReadVec3(const CmdJson& value, lynx::vec3& out, const std::string& what)
{
    float v[3] = { out.x, out.y, out.z };

    // Missing z : keep the current one.
    if (value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number())
    {
        out.x = value[0].get<float>();
        out.y = value[1].get<float>();
        return;
    }

    if (!cmd::ReadVector(value, v, 3))
        throw cmd::CommandError(what + " must be [x, y, z] or {\"x\":.., \"y\":.., \"z\":..}");

    out = lynx::vec3(v[0], v[1], v[2]);
}

// JSON -> value of the property's type. `current` : value kept for the parts
// not given (transform).
static EditorPropertyValue CmdToPropertyValue(
    size_t type,
    const CmdJson& value,
    const EditorPropertyValue& current,
    const std::string& name)
{
    const std::string what = "property '" + name + "' (" + CmdPropertyTypeName(type) + ")";

    switch (type)
    {
    case 0:
        if (value.is_number() || value.is_boolean())
            return static_cast<int>(std::lround(value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0) : value.get<double>()));
        break;

    case 1:
        if (value.is_number())
            return value.get<float>();
        break;

    case 2:
        if (value.is_boolean())
            return value.get<bool>();
        if (value.is_number())
            return value.get<double>() != 0.0;
        break;

    case 3:
        if (value.is_string())
            return value.get<std::string>();
        if (value.is_number() || value.is_boolean())
            return value.dump();
        break;

    case 4:
    {
        float v[2];
        if (cmd::ReadVector(value, v, 2))
            return lynx::vec2(v[0], v[1]);
        break;
    }

    case 5:
    {
        float v[3];
        if (cmd::ReadVector(value, v, 3))
            return lynx::vec3(v[0], v[1], v[2]);
        break;
    }

    case 6:
    {
        float v[4];
        if (cmd::ReadVector(value, v, 4))
            return lynx::vec4(v[0], v[1], v[2], v[3]);
        break;
    }

    case 7:
        if (value.is_object())
        {
            lynx::transform t;

            if (const auto* previous = std::get_if<lynx::transform>(&current))
                t = *previous;

            if (value.contains("location")) CmdReadVec3(value["location"], t.location, what + ".location");
            if (value.contains("rotation")) CmdReadVec3(value["rotation"], t.rotation, what + ".rotation");
            if (value.contains("scale"))    CmdReadVec3(value["scale"], t.scale, what + ".scale");

            return t;
        }
        break;

    default:
        break;
    }

    throw cmd::CommandError("bad value for " + what + " : " + value.dump());
}

static CmdJson CmdActorSummary(lynx::Actor* actor)
{
    return {
        { "id", actor->object_id_ },
        { "class", actor->GetTypeName() },
        { "location", CmdVec(actor->transform.location) },
        { "rotation", CmdVec(actor->transform.rotation) },
        { "scale", CmdVec(actor->transform.scale) },
    };
}

static CmdJson CmdActorDetails(lynx::Actor* actor)
{
    CmdJson details = CmdActorSummary(actor);

    CmdJson properties = CmdJson::object();

    for (const auto& [name, property] : actor->GetProperties())
    {
        if (name == "object_id_")
            continue;

        properties[name] = {
            { "type", CmdPropertyTypeName(property.GetType()) },
            { "value", CmdPropertyValue(property) },
        };
    }

    CmdJson functions = CmdJson::array();

    for (const auto& [name, function] : actor->GetFunctions())
        functions.push_back({ { "name", name }, { "arity", function.arity } });

    details["script_class"] = actor->script_class_;
    details["properties"] = std::move(properties);
    details["functions"] = std::move(functions);
    details["selected"] = (actor == editing_actor);

    return details;
}

// Sets one property, with undo. Returns the new value.
static CmdJson CmdSetProperty(lynx::Actor* actor, const std::string& name, const CmdJson& value)
{
    if (name == "object_id_")
        throw cmd::CommandError("use actor.rename to change the id");

    auto& properties = actor->GetProperties();
    auto it = properties.find(name);

    if (it == properties.end())
        throw cmd::CommandError("actor '" + actor->object_id_ + "' has no property '" + name + "' (see actor.get)");

    // ordered_map const access : GetProperties() returns a const reference ;
    // the members are pointers, so the values themselves are writable.
    lynx::property& property = const_cast<lynx::property&>(it->second);

    EditorPropertyValue previous;
    const bool had_previous = CaptureEditorProperty(property, previous);

    const EditorPropertyValue next = CmdToPropertyValue(property.GetType(), value, previous, name);

    if (!ApplyEditorProperty(property, next))
        throw cmd::CommandError("could not set property '" + name + "'");

    if (had_previous)
    {
        const std::string actor_id = actor->object_id_;

        PushCommandUndo([actor_id, name, previous]()
        {
            lynx::Actor* target = FindEditorActor(actor_id);

            if (!target)
                return;

            auto& props = target->GetProperties();
            auto found = props.find(name);

            if (found != props.end())
                ApplyEditorProperty(const_cast<lynx::property&>(found->second), previous);

            MarkEditorDirty();
        });
    }

    CommandChangedLevel();
    return CmdPropertyValue(property);
}

static void CmdSetTransform(lynx::Actor* actor, const lynx::transform& next)
{
    const std::string actor_id = actor->object_id_;
    const lynx::transform previous = actor->transform;

    actor->transform = next;

    PushCommandUndo([actor_id, previous]()
    {
        if (lynx::Actor* target = FindEditorActor(actor_id))
        {
            target->transform = previous;

            if (editing_actor == target)
                SyncGizmoFromTransform(target->transform);

            MarkEditorDirty();
        }
    });

    if (editing_actor == actor)
        SyncGizmoFromTransform(actor->transform);

    CommandChangedLevel();
}

static void CmdDeselectIf(lynx::Actor* actor)
{
    if (editing_actor != actor)
        return;

    editing_actor = nullptr;
    editing_object = HRL_INVALID_ID;
    HRL_SetGizmoVisible(gizmo, HRL_FALSE);
}

static void CmdDestroyActor(lynx::Actor* actor)
{
    const EditorActorSnapshot snapshot = CaptureActorSnapshot(actor);

    PushCommandUndo([snapshot]()
    {
        if (!editor_level)
            return;

        if (!snapshot.object_id.empty() && FindEditorActor(snapshot.object_id))
            return;

        RestoreActorSnapshot(editor_level, snapshot);
        MarkEditorDirty();
    });

    CmdDeselectIf(actor);

    // The Place Actors drag preview is an actor too.
    if (actor == actor_drag_preview)
        CancelActorPlacementDrag();
    else
        CmdLevel()->DestroyActor(actor);

    CommandChangedLevel();
}

// Undo entry : destroys the actor `actor_id` (spawned by a command).
static void CmdPushSpawnUndo(const std::string& actor_id)
{
    PushCommandUndo([actor_id]()
    {
        if (lynx::Actor* created = FindEditorActor(actor_id))
        {
            CmdDeselectIf(created);
            editor_level->DestroyActor(created);
            MarkEditorDirty();
        }
    });
}

static std::vector<std::string> CmdClassNames()
{
    std::vector<std::string> names;

    auto* factory = lynx::GetEngine()->GetFactory().GetInternalFactory();

    if (factory)
    {
        for (const auto& [type_name, constructor] : *factory)
        {
            (void)constructor;
            names.push_back(type_name);
        }
    }

    std::sort(names.begin(), names.end());
    return names;
}

static bool CmdClassExists(const std::string& name)
{
    auto* factory = lynx::GetEngine()->GetFactory().GetInternalFactory();
    return factory && factory->find(name) != factory->end();
}

// Simple wildcard match : '*' any text, '?' one character.
static bool CmdWildcard(const char* pattern, const char* text)
{
    if (*pattern == '\0')
        return *text == '\0';

    if (*pattern == '*')
        return CmdWildcard(pattern + 1, text) || (*text && CmdWildcard(pattern, text + 1));

    if (*text && (*pattern == '?' || std::tolower(static_cast<unsigned char>(*pattern)) ==
                                     std::tolower(static_cast<unsigned char>(*text))))
        return CmdWildcard(pattern + 1, text + 1);

    return false;
}

static CmdJson CmdPropToJson(const lynx::PropVariantType& value)
{
    return std::visit([](const auto& v) -> CmdJson
    {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, lynx::transform>)
            return CmdTransform(v);
        else if constexpr (std::is_same_v<T, lynx::vec2> || std::is_same_v<T, lynx::vec3> || std::is_same_v<T, lynx::vec4>)
            return CmdVec(v);
        else
            return CmdJson(v);
    }, value);
}

static lynx::PropVariantType CmdJsonToProp(const CmdJson& value)
{
    if (value.is_boolean())
        return value.get<bool>();

    if (value.is_number_integer())
        return static_cast<int>(value.get<long long>());

    if (value.is_number())
        return value.get<float>();

    if (value.is_string())
        return value.get<std::string>();

    if (value.is_array())
    {
        float v[4] = { 0.f, 0.f, 0.f, 0.f };

        if (value.size() == 2 && cmd::ReadVector(value, v, 2)) return lynx::vec2(v[0], v[1]);
        if (value.size() == 3 && cmd::ReadVector(value, v, 3)) return lynx::vec3(v[0], v[1], v[2]);
        if (value.size() == 4 && cmd::ReadVector(value, v, 4)) return lynx::vec4(v[0], v[1], v[2], v[3]);
    }

    if (value.is_object() && (value.contains("location") || value.contains("rotation") || value.contains("scale")))
        return std::get<lynx::transform>(CmdToPropertyValue(7, value, lynx::transform(), "argument"));

    throw cmd::CommandError("unsupported argument : " + value.dump());
}


// -----------------------------------------------------------------------------
// Files (assets / project)
// -----------------------------------------------------------------------------

// Resolves a path given by a command, inside assets/ (default) or the project.
// Absolute paths and ".." going out are refused.
static std::filesystem::path CmdResolvePath(const std::string& text, const std::string& root, bool for_write)
{
    if (root != "assets" && root != "project")
        throw cmd::CommandError("'root' must be \"assets\" or \"project\"");

    const std::filesystem::path base =
        root == "assets" ? currentProject.assets_dir : currentProject.root;

    std::string clean = text;
    std::replace(clean.begin(), clean.end(), '\\', '/');

    // "assets/x.png" with root "assets" : tolerated.
    if (root == "assets" && clean.rfind("assets/", 0) == 0)
        clean = clean.substr(7);

    const std::filesystem::path relative = std::filesystem::path(clean).lexically_normal();

    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
        throw cmd::CommandError("paths are relative to " + root + "/ : '" + text + "'");

    const std::string generic = relative.generic_string();

    if (generic == ".." || generic.rfind("../", 0) == 0)
        throw cmd::CommandError("path goes out of " + root + "/ : '" + text + "'");

    if (for_write && root == "project")
    {
        const std::string first = relative.empty() ? std::string() : relative.begin()->string();

        if (first == "build" || first == ".lynx" || first == ".git")
            throw cmd::CommandError("writing in " + first + "/ is not allowed");
    }

    return generic == "." ? base : base / relative;
}

static std::string CmdRelativeTo(const std::filesystem::path& path, const std::filesystem::path& base)
{
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, base, ec);
    return ec ? path.generic_string() : relative.generic_string();
}

// Copies a file / folder that is going to be replaced or deleted into
// <project>/.lynx/trash/<date>/... Returns the copy ("" if nothing to keep).
static std::string CmdBackup(const std::filesystem::path& path)
{
    std::error_code ec;

    if (!std::filesystem::exists(path, ec))
        return {};

    static std::string session_stamp;

    if (session_stamp.empty())
    {
        const std::time_t now = std::time(nullptr);
        char text[32];
        std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", std::localtime(&now));
        session_stamp = text;
    }

    const std::filesystem::path target =
        currentProject.root / ".lynx" / "trash" / session_stamp /
        std::filesystem::path(CmdRelativeTo(path, currentProject.root));

    std::filesystem::create_directories(target.parent_path(), ec);

    // Same file saved twice in one session : keep the oldest one.
    if (std::filesystem::exists(target, ec))
        return CmdRelativeTo(target, currentProject.root);

    std::filesystem::copy(path, target,
                          std::filesystem::copy_options::recursive |
                          std::filesystem::copy_options::overwrite_existing, ec);

    return ec ? std::string() : CmdRelativeTo(target, currentProject.root);
}

static long long CmdFileTime(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);

    if (ec)
        return 0;

    const auto system = std::chrono::file_clock::to_sys(time);
    return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

// After a file changed : reload what depends on it.
static CmdJson CmdAfterFileChanged(const std::filesystem::path& path)
{
    CmdJson reloaded = CmdJson::array();

    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, currentProject.assets_dir, ec);
    const std::string generic = ec ? std::string() : relative.generic_string();

    if (generic == "voxels.json")
    {
        if (lynx::voxels::LoadFile("voxels.json"))
        {
            lynx::voxels::ApplyToScene(scene);
            reloaded.push_back("voxels");
        }
        else
        {
            throw cmd::CommandError("file written, but voxels.json is invalid : " + lynx::voxels::GetLoadError());
        }
    }

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == ".js" && !generic.empty() && generic.rfind("..", 0) != 0)
    {
        lynx::ReloadScripts();
        reloaded.push_back("scripts");
    }

    return reloaded;
}


// -----------------------------------------------------------------------------
// Deferred commands (end of frame / build end)
// -----------------------------------------------------------------------------

struct PendingScreenshot
{
    std::string target;    // "viewport" / "window"
    int max_size = 1024;
    std::string save_to;   // project relative ("" = no file)
    bool include_data = true;
    cmd::Respond respond;
};

static std::vector<PendingScreenshot> pendingScreenshots;
static std::vector<cmd::Respond> pendingCompileResponses;
static int pendingFrameWaits = 0;
static std::vector<std::pair<int, cmd::Respond>> pendingFrameResponses;

static CmdJson CmdBuildResult(bool success)
{
    CmdJson lines = CmdJson::array();

    for (const std::string& line : lynx::editor::game_build::OutputTail(success ? 15 : 80))
        lines.push_back(line);

    return {
        { "success", success },
        { "output", std::move(lines) },
        { "reload", success && appSettings.reloadAfterBuild ? (isPlaying ? "at Stop" : "done") : "no" },
    };
}

// main() : a build started from the editor ended.
static void NotifyCommandBuildFinished(bool success)
{
    auto responses = std::move(pendingCompileResponses);
    pendingCompileResponses.clear();

    for (auto& respond : responses)
        respond(true, CmdBuildResult(success));
}

static void CmdCaptureScreenshot(PendingScreenshot& request)
{
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;

    GLint previous_alignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_alignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    if (request.target == "viewport")
    {
        const unsigned int texture = HRL_GL_GetSceneTextureGL_ID(scene);

        if (texture == 0)
        {
            glPixelStorei(GL_PACK_ALIGNMENT, previous_alignment);
            request.respond(false, "no scene image (is the Viewport window open ?)");
            return;
        }

        GLint previous_texture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);

        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

        if (width > 0 && height > 0)
        {
            pixels.resize(static_cast<size_t>(width) * height * 3);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        }

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    }
    else
    {
        GLFWwindow* window = glfwGetCurrentContext();

        if (window)
            glfwGetFramebufferSize(window, &width, &height);

        if (width > 0 && height > 0)
        {
            pixels.resize(static_cast<size_t>(width) * height * 3);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        }
    }

    glPixelStorei(GL_PACK_ALIGNMENT, previous_alignment);

    if (pixels.empty())
    {
        request.respond(false, "could not read the image");
        return;
    }

    // OpenGL rows are bottom-up.
    const size_t row = static_cast<size_t>(width) * 3;
    std::vector<uint8_t> line(row);

    for (int y = 0; y < height / 2; ++y)
    {
        uint8_t* a = pixels.data() + row * y;
        uint8_t* b = pixels.data() + row * (height - 1 - y);
        std::memcpy(line.data(), a, row);
        std::memcpy(a, b, row);
        std::memcpy(b, line.data(), row);
    }

    cmd::Downscale(pixels, width, height, 3, request.max_size);

    const std::string png = cmd::EncodePng(pixels.data(), width, height, 3);

    CmdJson result = {
        { "width", width },
        { "height", height },
        { "format", "png" },
    };

    if (!request.save_to.empty())
    {
        try
        {
            const std::filesystem::path file = CmdResolvePath(request.save_to, "project", false);
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);

            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(png.data(), static_cast<std::streamsize>(png.size()));

            if (!out)
                throw cmd::CommandError("could not write " + file.string());

            result["file"] = CmdRelativeTo(file, currentProject.root);
        }
        catch (const std::exception& error)
        {
            request.respond(false, error.what());
            return;
        }
    }

    if (request.include_data)
        result["data"] = cmd::Base64Encode(reinterpret_cast<const uint8_t*>(png.data()), png.size());

    request.respond(true, std::move(result));
}

// main() : after the frame is drawn, before glfwSwapBuffers.
static void ProcessFrameEndCommands()
{
    if (!pendingScreenshots.empty())
    {
        auto requests = std::move(pendingScreenshots);
        pendingScreenshots.clear();

        for (auto& request : requests)
            CmdCaptureScreenshot(request);
    }

    if (!pendingFrameResponses.empty())
    {
        std::vector<std::pair<int, cmd::Respond>> still;

        for (auto& [frames, respond] : pendingFrameResponses)
        {
            if (--frames <= 0)
                respond(true, CmdJson::object());
            else
                still.emplace_back(frames, std::move(respond));
        }

        pendingFrameResponses = std::move(still);
    }
}


// -----------------------------------------------------------------------------
// Registration
// -----------------------------------------------------------------------------

using cmd::ParamInfo;

static void RegisterEditorCommands()
{
    cmd::SetUndoGroupHooks(BeginCommandUndoGroup, EndCommandUndoGroup);

    // =========================================================================
    // editor.*
    // =========================================================================

    cmd::Register("editor.info",
        "State of the editor : project, level, play mode, selection, camera, counts.",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            CmdJson voxel_count = lynx::voxels::GetTypeCount();

            return {
                { "project", currentProject.name },
                { "project_root", currentProject.root.generic_string() },
                { "assets_dir", currentProject.assets_dir.generic_string() },
                { "game_module", currentProject.module_path.filename().string() },
                { "level_file", "world.xml" },
                { "level_loaded", editor_level != nullptr },
                { "actor_count", editor_level ? editor_level->GetActors().size() : 0 },
                { "playing", isPlaying },
                { "unsaved_changes", editor_dirty },
                { "selected_actor", editing_actor ? CmdJson(editing_actor->object_id_) : CmdJson(nullptr) },
                { "camera", { camX, camY, camZ } },
                { "voxel_type_count", voxel_count },
                { "building", lynx::editor::game_build::IsRunning() },
                { "undo_steps", editorUndoHistory.size() },
            };
        });

    cmd::Register("editor.save",
        "Saves the level (world.xml) and the voxel world, like Ctrl+S.",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            if (isPlaying)
                throw cmd::CommandError("cannot save while playing (editor.stop first)");

            CmdLevel();
            SaveEditor();
            return { { "saved", true } };
        });

    cmd::Register("editor.undo",
        "Undoes the last editor action (like Ctrl+Z). A batch / an undo group is one action.",
        { { "count", "integer", "How many actions (default 1).", false } },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            if (isPlaying)
                throw cmd::CommandError("cannot undo while playing");

            const int count = std::max(1, cmd::GetInt(params, "count", 1));
            int done = 0;

            for (; done < count && !editorUndoHistory.empty(); ++done)
                UndoLastEditorAction();

            return { { "undone", done }, { "remaining", editorUndoHistory.size() } };
        });

    cmd::Register("editor.play",
        "Starts Play mode (the game runs in the viewport). The level is restored at editor.stop.",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            if (!isPlaying)
                TogglePlayMode();

            lynx::editor::command_server::YieldFrame();
            return { { "playing", isPlaying } };
        });

    cmd::Register("editor.stop",
        "Stops Play mode : the level goes back to its state before Play.",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            if (isPlaying)
                TogglePlayMode();

            // The level is rebuilt at the start of the next frame : the next
            // commands wait for it.
            lynx::editor::command_server::YieldFrame();
            return { { "playing", isPlaying } };
        });

    cmd::RegisterAsync("editor.wait_frames",
        "Answers after N frames (let the game run, then take a screenshot...).",
        { { "count", "integer", "Number of frames (default 1, max 6000).", false } },
        [](const CmdJson& params, const cmd::CommandContext&, cmd::Respond respond)
        {
            const int count = std::clamp(cmd::GetInt(params, "count", 1), 1, 6000);
            pendingFrameResponses.emplace_back(count, std::move(respond));
        });

    cmd::Register("editor.select",
        "Selects an actor in the editor (gizmo, Details panel). No id : clears the selection.",
        { { "id", "string", "Actor id.", false } },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            if (isPlaying)
                throw cmd::CommandError("no selection while playing");

            if (!cmd::Has(params, "id"))
            {
                SetActorSelected(nullptr);
                return { { "selected", nullptr } };
            }

            lynx::Actor* actor = CmdActor(params);
            SetActorSelected(actor);
            return { { "selected", actor->object_id_ } };
        });

    cmd::Register("editor.camera",
        "Moves the editor camera (world units ; z = height, bigger = further). Returns the position.",
        {
            { "x", "number", "Camera x.", false },
            { "y", "number", "Camera y.", false },
            { "z", "number", "Camera height.", false },
            { "focus", "string", "Actor id to center on (instead of x / y).", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            if (cmd::Has(params, "focus"))
            {
                lynx::Actor* actor = CmdActor(params, "focus");
                camX = actor->transform.location.x;
                camY = actor->transform.location.y;
            }

            if (cmd::Has(params, "x")) camX = static_cast<float>(cmd::GetNumber(params, "x"));
            if (cmd::Has(params, "y")) camY = static_cast<float>(cmd::GetNumber(params, "y"));
            if (cmd::Has(params, "z")) camZ = ClampCameraZ(static_cast<float>(cmd::GetNumber(params, "z")));

            HRL_SetCameraLocation(editor_camera, camX, camY, camZ);

            return { { "x", camX }, { "y", camY }, { "z", camZ } };
        });

    cmd::RegisterAsync("editor.screenshot",
        "PNG image of the scene ('viewport', default) or of the whole editor window ('window'), "
        "taken at the end of the current frame. Result : {width, height, data (base64 PNG), file}.",
        {
            { "target", "string", "\"viewport\" (scene only) or \"window\".", false },
            { "max_size", "integer", "Biggest side in pixels (default 1024, 0 = full size).", false },
            { "save_to", "string", "Also save the PNG there (relative to the project, ex : \".lynx/screenshots/a.png\").", false },
            { "include_data", "boolean", "Put the base64 PNG in the result (default true).", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&, cmd::Respond respond)
        {
            PendingScreenshot request;
            request.target = cmd::GetString(params, "target", "viewport");

            if (request.target != "viewport" && request.target != "window")
                throw cmd::CommandError("'target' must be \"viewport\" or \"window\"");

            request.max_size = std::max(0, cmd::GetInt(params, "max_size", 1024));
            request.save_to = cmd::GetString(params, "save_to", "");
            request.include_data = cmd::GetBool(params, "include_data", true);
            request.respond = std::move(respond);

            pendingScreenshots.push_back(std::move(request));
        });

    cmd::Register("editor.log",
        "Prints a message in the editor console (and the Commands window log).",
        { { "message", "string", "Text.", true } },
        [](const CmdJson& params, const cmd::CommandContext& context) -> CmdJson
        {
            const std::string message = cmd::GetString(params, "message");
            std::cout << "[" << context.source << "] " << message << "\n";
            cmd::Log("[" + context.source + "] " + message);
            return CmdJson::object();
        });

    cmd::Register("editor.reload_scripts",
        "Recompiles every JavaScript script / class from the disk (hot reload).",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            lynx::ReloadScripts();
            return { { "reloaded", true } };
        });

    cmd::Register("editor.reload_voxels",
        "Reads assets/voxels.json again and applies the voxel types to the scene.",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            if (!lynx::voxels::LoadFile("voxels.json"))
                throw cmd::CommandError(lynx::voxels::GetLoadError());

            lynx::voxels::ApplyToScene(scene);
            return { { "types", lynx::voxels::GetTypeCount() } };
        });

    cmd::RegisterAsync("editor.compile",
        "Compiles the game (Build.bat / CMake). With wait=true (default) answers when the build ended : "
        "{success, output (last lines : the errors), reload}. A successful build reloads the game DLL.",
        { { "wait", "boolean", "Wait for the end of the build (default true).", false } },
        [](const CmdJson& params, const cmd::CommandContext&, cmd::Respond respond)
        {
            if (!lynx::editor::game_build::IsRunning() && !lynx::editor::game_build::Start())
                throw cmd::CommandError("could not start the build");

            if (cmd::GetBool(params, "wait", true))
                pendingCompileResponses.push_back(std::move(respond));
            else
                respond(true, { { "started", true } });
        });

    cmd::Register("editor.build_status",
        "State of the last game build : {running, status, output (last lines)}.",
        { { "lines", "integer", "Number of output lines (default 40).", false } },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            CmdJson lines = CmdJson::array();

            for (const std::string& line :
                 lynx::editor::game_build::OutputTail(static_cast<size_t>(std::max(1, cmd::GetInt(params, "lines", 40)))))
                lines.push_back(line);

            return {
                { "running", lynx::editor::game_build::IsRunning() },
                { "status", lynx::editor::game_build::ToolbarStatus() },
                { "succeeded", lynx::editor::game_build::LastBuildSucceeded() },
                { "output", std::move(lines) },
            };
        });

    cmd::Register("editor.reload_game",
        "Reloads the game DLL (after a build done outside the editor). Done at the next frame ; at Stop if playing.",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            RequestHotReload();
            lynx::editor::command_server::YieldFrame();
            return { { "requested", true }, { "when", isPlaying ? "at Stop" : "next frame" } };
        });

    cmd::Register("script.eval",
        "Runs JavaScript in the engine (same context as the game scripts : Actor, level, components...). "
        "Returns the value of the last expression (as JSON).",
        { { "code", "string", "JavaScript code.", true } },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string code = cmd::GetString(params, "code");
            std::string result;
            std::string error;

            if (!lynx::EvaluateScript(code.c_str(), result, error, "<command>"))
                throw cmd::CommandError("JavaScript error : " + error);

            try
            {
                return { { "result", CmdJson::parse(result) } };
            }
            catch (...)
            {
                return { { "result", result } };
            }
        });


    // =========================================================================
    // level.* / actor.*
    // =========================================================================

    cmd::Register("level.list_classes",
        "Actor classes that can be spawned (C++ classes of the game and JavaScript classes).",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            return CmdClassNames();
        });

    cmd::Register("level.list_actors",
        "Actors of the level : [{id, class, location, rotation, scale}].",
        {
            { "class", "string", "Only this class (exact name).", false },
            { "id", "string", "Only the ids matching this pattern (* and ? wildcards).", false },
            { "near", "vec2", "Only the actors within 'radius' of this point [x, y].", false },
            { "radius", "number", "Radius for 'near' (default 10).", false },
            { "details", "boolean", "Include the properties of each actor (default false).", false },
            { "limit", "integer", "Maximum count (default 1000).", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            lynx::Level* level = CmdLevel();

            const std::string class_name = cmd::GetString(params, "class", "");
            const std::string pattern = cmd::GetString(params, "id", "");
            const bool details = cmd::GetBool(params, "details", false);
            const int limit = std::max(1, cmd::GetInt(params, "limit", 1000));

            float near_point[2] = { 0.f, 0.f };
            const bool use_near = cmd::Has(params, "near");

            if (use_near)
                cmd::GetVector(params, "near", near_point, 2);

            const float radius = static_cast<float>(cmd::GetNumber(params, "radius", 10.0));

            CmdJson list = CmdJson::array();

            for (lynx::Actor* actor : level->GetActors())
            {
                if (!actor || actor == actor_drag_preview)
                    continue;

                if (!class_name.empty() && actor->GetTypeName() != class_name)
                    continue;

                if (!pattern.empty() && !CmdWildcard(pattern.c_str(), actor->object_id_.c_str()))
                    continue;

                if (use_near)
                {
                    const float dx = actor->transform.location.x - near_point[0];
                    const float dy = actor->transform.location.y - near_point[1];

                    if (dx * dx + dy * dy > radius * radius)
                        continue;
                }

                list.push_back(details ? CmdActorDetails(actor) : CmdActorSummary(actor));

                if (static_cast<int>(list.size()) >= limit)
                    break;
            }

            return list;
        });

    cmd::Register("actor.get",
        "Everything about one actor : transform, properties {name: {type, value}}, functions (HFUNCTION).",
        { { "id", "string", "Actor id.", true } },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            return CmdActorDetails(CmdActor(params));
        });

    cmd::Register("actor.spawn",
        "Creates an actor. Result : {id, ...}. Without 'id' a free one is made from the class (Enemy, Enemy_2...).",
        {
            { "class", "string", "Class name (see level.list_classes).", true },
            { "id", "string", "Wanted id (must be free).", false },
            { "location", "vec3", "World position [x, y, z].", false },
            { "rotation", "vec3", "Rotation [x, y, z] (degrees).", false },
            { "scale", "vec3", "Scale [x, y, z].", false },
            { "voxel", "vec2", "Place at the center of this voxel cell [x, y] (instead of location).", false },
            { "properties", "object", "Property values {name: value} (see actor.get for the names).", false },
            { "select", "boolean", "Select it in the editor (default false).", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            lynx::Level* level = CmdLevel();
            const std::string class_name = cmd::GetString(params, "class");

            if (!CmdClassExists(class_name))
                throw cmd::CommandError("unknown class '" + class_name + "' (see level.list_classes)");

            std::string wanted_id = cmd::GetString(params, "id", "");

            if (!wanted_id.empty() && level->GetActorFromID(wanted_id.c_str()))
                throw cmd::CommandError("id '" + wanted_id + "' is already used");

            lynx::Actor* actor = level->SpawnActor(class_name.c_str());

            if (!actor)
                throw cmd::CommandError("could not spawn '" + class_name + "'");

            if (wanted_id.empty())
                AssignDefaultActorName(level, actor, class_name);
            else
                actor->object_id_ = wanted_id;

            try
            {
                if (cmd::Has(params, "voxel"))
                {
                    float cell[2];
                    cmd::GetVector(params, "voxel", cell, 2);

                    float world_x = 0.f;
                    float world_y = 0.f;

                    if (!HRL_VoxelToWorldCoordinates(scene, std::floor(cell[0]) + 0.5f, std::floor(cell[1]) + 0.5f, &world_x, &world_y))
                        throw cmd::CommandError("invalid voxel cell");

                    actor->transform.location = lynx::vec3(world_x, world_y, 0.f);
                }

                if (cmd::Has(params, "location")) CmdReadVec3(params["location"], actor->transform.location, "location");
                if (cmd::Has(params, "rotation")) CmdReadVec3(params["rotation"], actor->transform.rotation, "rotation");
                if (cmd::Has(params, "scale"))    CmdReadVec3(params["scale"], actor->transform.scale, "scale");

                if (cmd::Has(params, "properties"))
                {
                    if (!params["properties"].is_object())
                        throw cmd::CommandError("'properties' must be an object");

                    // Applied without their own undo entries : the spawn undo
                    // removes the whole actor.
                    const int depth = cmdUndoGroupDepth;
                    cmdUndoGroupDepth = 1;
                    const size_t group_size = cmdUndoGroup.size();

                    try
                    {
                        for (const auto& [name, value] : params["properties"].items())
                            CmdSetProperty(actor, name, value);
                    }
                    catch (...)
                    {
                        cmdUndoGroup.resize(group_size);
                        cmdUndoGroupDepth = depth;
                        throw;
                    }

                    cmdUndoGroup.resize(group_size);
                    cmdUndoGroupDepth = depth;
                }
            }
            catch (...)
            {
                level->DestroyActor(actor);
                throw;
            }

            CmdPushSpawnUndo(actor->object_id_);
            CommandChangedLevel();

            if (cmd::GetBool(params, "select", false) && !isPlaying)
                SetActorSelected(actor);

            return CmdActorSummary(actor);
        });

    cmd::Register("actor.delete",
        "Deletes actors (undo restores them).",
        {
            { "id", "string", "Actor id.", false },
            { "ids", "array", "Several actor ids.", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            std::vector<std::string> ids;

            if (cmd::Has(params, "id"))
                ids.push_back(cmd::GetString(params, "id"));

            if (cmd::Has(params, "ids"))
            {
                if (!params["ids"].is_array())
                    throw cmd::CommandError("'ids' must be an array of strings");

                for (const auto& id : params["ids"])
                    ids.push_back(id.is_string() ? id.get<std::string>() : id.dump());
            }

            if (ids.empty())
                throw cmd::CommandError("give 'id' or 'ids'");

            lynx::Level* level = CmdLevel();

            // All or nothing : check first.
            for (const std::string& id : ids)
            {
                if (!level->GetActorFromID(id.c_str()))
                    throw cmd::CommandError("unknown actor '" + id + "'");
            }

            BeginCommandUndoGroup("delete");

            for (const std::string& id : ids)
            {
                if (lynx::Actor* actor = level->GetActorFromID(id.c_str()))
                    CmdDestroyActor(actor);
            }

            EndCommandUndoGroup();

            return { { "deleted", ids } };
        });

    cmd::Register("actor.set_transform",
        "Moves / rotates / scales an actor. With relative=true the values are ADDED (rotation, location) "
        "or MULTIPLIED (scale). Missing parts are kept.",
        {
            { "id", "string", "Actor id.", true },
            { "location", "vec3", "Position [x, y, z] (or [x, y] : z kept).", false },
            { "rotation", "vec3", "Rotation [x, y, z].", false },
            { "scale", "vec3", "Scale [x, y, z].", false },
            { "relative", "boolean", "Offset instead of absolute values (default false).", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            lynx::Actor* actor = CmdActor(params);
            const bool relative = cmd::GetBool(params, "relative", false);

            lynx::transform next = actor->transform;

            const auto apply = [&](const char* key, lynx::vec3& target, bool multiply)
            {
                if (!cmd::Has(params, key))
                    return;

                if (!relative)
                {
                    CmdReadVec3(params[key], target, key);
                    return;
                }

                lynx::vec3 delta = multiply ? lynx::vec3(1.f) : lynx::vec3(0.f);
                CmdReadVec3(params[key], delta, key);

                if (multiply)
                    target = lynx::vec3(target.x * delta.x, target.y * delta.y, target.z * delta.z);
                else
                    target = lynx::vec3(target.x + delta.x, target.y + delta.y, target.z + delta.z);
            };

            apply("location", next.location, false);
            apply("rotation", next.rotation, false);
            apply("scale", next.scale, true);

            CmdSetTransform(actor, next);
            return CmdActorSummary(actor);
        });

    cmd::Register("actor.set_property",
        "Sets properties of an actor : 'name' + 'value', or 'properties' {name: value}. "
        "Types : int, float, bool, string, vec2/3/4 as [..], transform as {location, rotation, scale}.",
        {
            { "id", "string", "Actor id.", true },
            { "name", "string", "Property name.", false },
            { "value", "any", "New value.", false },
            { "properties", "object", "Several properties {name: value}.", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            lynx::Actor* actor = CmdActor(params);
            CmdJson result = CmdJson::object();

            BeginCommandUndoGroup("set_property");

            try
            {
                if (cmd::Has(params, "name"))
                {
                    if (!params.contains("value"))
                        throw cmd::CommandError("missing parameter 'value'");

                    const std::string name = cmd::GetString(params, "name");
                    result[name] = CmdSetProperty(actor, name, params["value"]);
                }

                if (cmd::Has(params, "properties"))
                {
                    if (!params["properties"].is_object())
                        throw cmd::CommandError("'properties' must be an object");

                    for (const auto& [name, value] : params["properties"].items())
                        result[name] = CmdSetProperty(actor, name, value);
                }
            }
            catch (...)
            {
                EndCommandUndoGroup();
                throw;
            }

            EndCommandUndoGroup();

            if (result.empty())
                throw cmd::CommandError("give 'name' + 'value', or 'properties'");

            return result;
        });

    cmd::Register("actor.rename",
        "Changes the id of an actor (must be free).",
        {
            { "id", "string", "Current id.", true },
            { "new_id", "string", "New id.", true },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            lynx::Actor* actor = CmdActor(params);
            const std::string new_id = cmd::GetString(params, "new_id");

            if (new_id.empty())
                throw cmd::CommandError("'new_id' is empty");

            if (lynx::Actor* other = CmdLevel()->GetActorFromID(new_id.c_str()); other && other != actor)
                throw cmd::CommandError("id '" + new_id + "' is already used");

            const std::string old_id = actor->object_id_;
            actor->object_id_ = new_id;

            PushCommandUndo([old_id, new_id]()
            {
                if (lynx::Actor* target = FindEditorActor(new_id))
                {
                    target->object_id_ = old_id;
                    MarkEditorDirty();
                }
            });

            CommandChangedLevel();
            return { { "id", new_id } };
        });

    cmd::Register("actor.duplicate",
        "Copies an actor (class, properties, transform). Result : the new actor.",
        {
            { "id", "string", "Actor to copy.", true },
            { "new_id", "string", "Id of the copy (default : a free one).", false },
            { "offset", "vec3", "Added to the location of the copy.", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            lynx::Actor* source = CmdActor(params);
            lynx::Level* level = CmdLevel();

            EditorActorSnapshot snapshot = CaptureActorSnapshot(source);
            const std::string new_id = cmd::GetString(params, "new_id", "");

            if (!new_id.empty() && level->GetActorFromID(new_id.c_str()))
                throw cmd::CommandError("id '" + new_id + "' is already used");

            snapshot.object_id.clear();

            if (cmd::Has(params, "offset"))
            {
                lynx::vec3 offset(0.f);
                CmdReadVec3(params["offset"], offset, "offset");
                snapshot.transform.location = lynx::vec3(
                    snapshot.transform.location.x + offset.x,
                    snapshot.transform.location.y + offset.y,
                    snapshot.transform.location.z + offset.z);
            }

            lynx::Actor* copy = RestoreActorSnapshot(level, snapshot);

            if (!copy)
                throw cmd::CommandError("could not copy '" + source->object_id_ + "'");

            if (new_id.empty())
                AssignDefaultActorName(level, copy, copy->GetTypeName());
            else
                copy->object_id_ = new_id;

            CmdPushSpawnUndo(copy->object_id_);
            CommandChangedLevel();

            return CmdActorSummary(copy);
        });

    cmd::Register("actor.call",
        "Calls a function of an actor : a C++ HFUNCTION (see actor.get 'functions'), else a method of its "
        "JavaScript class / scripts (number arguments only). Returns its result.",
        {
            { "id", "string", "Actor id.", true },
            { "function", "string", "Function name.", true },
            { "args", "array", "Arguments (numbers, strings, booleans, [x,y,z]...).", false },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            lynx::Actor* actor = CmdActor(params);
            const std::string function = cmd::GetString(params, "function");

            std::vector<lynx::PropVariantType> args;
            std::vector<double> numbers;
            bool only_numbers = true;

            if (cmd::Has(params, "args"))
            {
                if (!params["args"].is_array())
                    throw cmd::CommandError("'args' must be an array");

                for (const auto& arg : params["args"])
                {
                    args.push_back(CmdJsonToProp(arg));

                    if (arg.is_number())
                        numbers.push_back(arg.get<double>());
                    else
                        only_numbers = false;
                }
            }

            const auto& functions = actor->GetFunctions();

            if (functions.find(function) != functions.end())
            {
                std::optional<lynx::PropVariantType> result;

                if (!actor->CallFunctionByName(function, args, &result))
                    throw cmd::CommandError("call of '" + function + "' failed (argument count / types ?)");

                CommandChangedLevel();
                return { { "result", result ? CmdPropToJson(*result) : CmdJson(nullptr) } };
            }

            if (!only_numbers)
                throw cmd::CommandError("'" + function + "' is not a HFUNCTION ; script functions only take numbers");

            if (!lynx::CallScriptFunction(actor, function.c_str(), numbers))
                throw cmd::CommandError("actor '" + actor->object_id_ + "' has no function '" + function + "'");

            CommandChangedLevel();
            return { { "result", nullptr } };
        });


    // =========================================================================
    // voxel.*
    // =========================================================================

    cmd::Register("voxel.types",
        "Voxel types of assets/voxels.json : [{type, name, color, indestructible}]. Type 0 = empty.",
        {},
        [](const CmdJson&, const cmd::CommandContext&) -> CmdJson
        {
            CmdJson list = CmdJson::array();

            for (int i = 1; i <= lynx::voxels::GetTypeCount(); ++i)
            {
                const lynx::voxels::VoxelType* type = lynx::voxels::GetType(static_cast<uint8_t>(i));

                if (!type)
                    continue;

                char color[8];
                std::snprintf(color, sizeof(color), "%02x%02x%02x",
                              static_cast<int>(type->color[0] * 255.f + 0.5f),
                              static_cast<int>(type->color[1] * 255.f + 0.5f),
                              static_cast<int>(type->color[2] * 255.f + 0.5f));

                list.push_back({
                    { "type", i },
                    { "name", type->name },
                    { "color", color },
                    { "indestructible", type->indestructible },
                    { "on_destroyed", type->on_destroyed },
                });
            }

            return list;
        });

    cmd::Register("voxel.get",
        "Type of the voxel at a cell (0 = empty).",
        {
            { "x", "integer", "Cell x.", true },
            { "y", "integer", "Cell y.", true },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const int x = cmd::GetInt(params, "x");
            const int y = cmd::GetInt(params, "y");
            return { { "type", static_cast<int>(HRL_GetVoxelType(scene, x, y)) } };
        });

    cmd::Register("voxel.get_region",
        "Voxel types of a rectangle of cells (max 65536 cells). Result : {x0, y0, width, height, rows} ; "
        "rows[0] is y0 (bottom), rows[i][j] the cell (x0 + j, y0 + i).",
        {
            { "x0", "integer", "First cell x.", true },
            { "y0", "integer", "First cell y.", true },
            { "x1", "integer", "Last cell x (included).", true },
            { "y1", "integer", "Last cell y (included).", true },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            int x0 = cmd::GetInt(params, "x0");
            int y0 = cmd::GetInt(params, "y0");
            int x1 = cmd::GetInt(params, "x1");
            int y1 = cmd::GetInt(params, "y1");

            if (x1 < x0) std::swap(x0, x1);
            if (y1 < y0) std::swap(y0, y1);

            const long long cells = static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1);

            if (cells > 65536)
                throw cmd::CommandError("region too big (" + std::to_string(cells) + " cells, max 65536)");

            CmdJson rows = CmdJson::array();

            for (int y = y0; y <= y1; ++y)
            {
                CmdJson row = CmdJson::array();

                for (int x = x0; x <= x1; ++x)
                    row.push_back(static_cast<int>(HRL_GetVoxelType(scene, x, y)));

                rows.push_back(std::move(row));
            }

            return { { "x0", x0 }, { "y0", y0 }, { "width", x1 - x0 + 1 }, { "height", y1 - y0 + 1 }, { "rows", std::move(rows) } };
        });

    // Voxel edits : one undo entry per command.
    struct CmdVoxelChange
    {
        int x;
        int y;
        int previous;
    };

    const auto set_voxels = [](const std::vector<std::array<int, 3>>& cells) -> CmdJson
    {
        if (isPlaying)
            throw cmd::CommandError("voxel edits are not allowed while playing");

        const int type_count = lynx::voxels::GetTypeCount();

        for (const auto& cell : cells)
        {
            if (cell[2] < 0 || cell[2] > type_count)
                throw cmd::CommandError("unknown voxel type " + std::to_string(cell[2]) +
                                        " (0 = empty, 1.." + std::to_string(type_count) + ", see voxel.types)");
        }

        ClearBrushPreview();

        auto changes = std::make_shared<std::vector<CmdVoxelChange>>();

        for (const auto& cell : cells)
        {
            const int previous = static_cast<int>(HRL_GetVoxelType(scene, cell[0], cell[1]));

            if (previous == cell[2])
                continue;

            changes->push_back({ cell[0], cell[1], previous });
            HRL_SetVoxelType(scene, cell[0], cell[1], cell[2]);
        }

        if (!changes->empty())
        {
            PushCommandUndo([changes]()
            {
                for (auto it = changes->rbegin(); it != changes->rend(); ++it)
                    HRL_SetVoxelType(scene, it->x, it->y, it->previous);

                MarkEditorDirty();
            });

            CommandChangedLevel();
        }

        return { { "changed", changes->size() } };
    };

    cmd::Register("voxel.set",
        "Sets voxels : one cell (x, y, type) or 'voxels' = [[x, y, type], ...]. Type 0 = empty.",
        {
            { "x", "integer", "Cell x.", false },
            { "y", "integer", "Cell y.", false },
            { "type", "integer", "Voxel type (see voxel.types).", false },
            { "voxels", "array", "Several cells [[x, y, type], ...].", false },
        },
        [set_voxels](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            std::vector<std::array<int, 3>> cells;

            if (cmd::Has(params, "voxels"))
            {
                if (!params["voxels"].is_array())
                    throw cmd::CommandError("'voxels' must be [[x, y, type], ...]");

                for (const auto& v : params["voxels"])
                {
                    if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number())
                        throw cmd::CommandError("each voxel is [x, y, type] : " + v.dump());

                    cells.push_back({ v[0].get<int>(), v[1].get<int>(), v[2].get<int>() });
                }
            }
            else
            {
                cells.push_back({ cmd::GetInt(params, "x"), cmd::GetInt(params, "y"), cmd::GetInt(params, "type") });
            }

            return set_voxels(cells);
        });

    cmd::Register("voxel.fill",
        "Fills a rectangle of cells (corners included) with a type (0 = erase). outline=true : border only.",
        {
            { "x0", "integer", "Corner x.", true },
            { "y0", "integer", "Corner y.", true },
            { "x1", "integer", "Other corner x.", true },
            { "y1", "integer", "Other corner y.", true },
            { "type", "integer", "Voxel type.", true },
            { "outline", "boolean", "Only the border (default false).", false },
        },
        [set_voxels](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            int x0 = cmd::GetInt(params, "x0");
            int y0 = cmd::GetInt(params, "y0");
            int x1 = cmd::GetInt(params, "x1");
            int y1 = cmd::GetInt(params, "y1");
            const int type = cmd::GetInt(params, "type");
            const bool outline = cmd::GetBool(params, "outline", false);

            if (x1 < x0) std::swap(x0, x1);
            if (y1 < y0) std::swap(y0, y1);

            const long long count = static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1);

            if (count > 1000000)
                throw cmd::CommandError("rectangle too big (max 1 000 000 cells)");

            std::vector<std::array<int, 3>> cells;
            cells.reserve(static_cast<size_t>(count));

            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    if (outline && x != x0 && x != x1 && y != y0 && y != y1)
                        continue;

                    cells.push_back({ x, y, type });
                }
            }

            return set_voxels(cells);
        });

    cmd::Register("voxel.world_to_cell",
        "Converts a world position to a voxel cell.",
        {
            { "x", "number", "World x.", true },
            { "y", "number", "World y.", true },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            float cell_x = 0.f;
            float cell_y = 0.f;

            if (!HRL_WorldToVoxelCoordinates(scene,
                    static_cast<float>(cmd::GetNumber(params, "x")),
                    static_cast<float>(cmd::GetNumber(params, "y")),
                    &cell_x, &cell_y))
                throw cmd::CommandError("outside of the voxel world");

            return { { "x", static_cast<int>(std::floor(cell_x)) }, { "y", static_cast<int>(std::floor(cell_y)) } };
        });

    cmd::Register("voxel.cell_to_world",
        "World position of the center of a voxel cell.",
        {
            { "x", "integer", "Cell x.", true },
            { "y", "integer", "Cell y.", true },
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            float world_x = 0.f;
            float world_y = 0.f;

            if (!HRL_VoxelToWorldCoordinates(scene,
                    static_cast<float>(cmd::GetInt(params, "x")) + 0.5f,
                    static_cast<float>(cmd::GetInt(params, "y")) + 0.5f,
                    &world_x, &world_y))
                throw cmd::CommandError("invalid cell");

            return { { "x", world_x }, { "y", world_y } };
        });


    // =========================================================================
    // asset.* : files of assets/ (root "assets", default) or of the project
    // (root "project" : source code, input.json... ; build/ and .lynx/ are
    // read-only). Replaced / deleted files are kept in .lynx/trash/.
    // =========================================================================

    const ParamInfo root_param = { "root", "string", "\"assets\" (default) or \"project\" (project root : src/, input.json...).", false };

    cmd::Register("asset.list",
        "Files and folders : [{path, type: file|dir, size, modified (unix time)}].",
        {
            { "path", "string", "Folder (default : the root).", false },
            { "recursive", "boolean", "Also the sub folders (default false).", false },
            { "pattern", "string", "Only the names matching (* and ?), ex : \"*.png\".", false },
            { "limit", "integer", "Maximum count (default 2000).", false },
            root_param,
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string root = cmd::GetString(params, "root", "assets");
            const std::filesystem::path base = root == "project" ? currentProject.root : currentProject.assets_dir;
            const std::filesystem::path folder = CmdResolvePath(cmd::GetString(params, "path", "."), root, false);

            std::error_code ec;

            if (!std::filesystem::is_directory(folder, ec))
                throw cmd::CommandError("not a folder : " + CmdRelativeTo(folder, base));

            const bool recursive = cmd::GetBool(params, "recursive", false);
            const std::string pattern = cmd::GetString(params, "pattern", "");
            const int limit = std::max(1, cmd::GetInt(params, "limit", 2000));

            CmdJson list = CmdJson::array();
            bool truncated = false;

            const auto add = [&](const std::filesystem::directory_entry& entry)
            {
                const std::string name = entry.path().filename().string();

                if (!pattern.empty() && !CmdWildcard(pattern.c_str(), name.c_str()))
                    return true;

                if (static_cast<int>(list.size()) >= limit)
                {
                    truncated = true;
                    return false;
                }

                std::error_code entry_error;
                const bool is_dir = entry.is_directory(entry_error);

                CmdJson item = {
                    { "path", CmdRelativeTo(entry.path(), base) },
                    { "type", is_dir ? "dir" : "file" },
                };

                if (!is_dir)
                    item["size"] = entry.file_size(entry_error);

                item["modified"] = CmdFileTime(entry.path());
                list.push_back(std::move(item));
                return true;
            };

            if (recursive)
            {
                std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec);

                for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                {
                    // Never list the build folder / editor data in "project".
                    if (root == "project" && it.depth() == 0 && it->is_directory())
                    {
                        const std::string name = it->path().filename().string();

                        if (name == "build" || name == ".lynx" || name == ".git")
                        {
                            it.disable_recursion_pending();
                            continue;
                        }
                    }

                    if (!add(*it))
                        break;
                }
            }
            else
            {
                for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
                {
                    if (!add(entry))
                        break;
                }
            }

            return { { "entries", std::move(list) }, { "truncated", truncated } };
        });

    cmd::Register("asset.read",
        "Reads a file. encoding \"text\" (default, UTF-8) or \"base64\" (images, sounds...). Max 64 MB.",
        {
            { "path", "string", "File path.", true },
            { "encoding", "string", "\"text\" or \"base64\".", false },
            root_param,
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string root = cmd::GetString(params, "root", "assets");
            const std::filesystem::path file = CmdResolvePath(cmd::GetString(params, "path"), root, false);
            const std::string encoding = cmd::GetString(params, "encoding", "text");

            std::error_code ec;

            if (!std::filesystem::is_regular_file(file, ec))
                throw cmd::CommandError("file not found : " + cmd::GetString(params, "path"));

            if (std::filesystem::file_size(file, ec) > 64u * 1024u * 1024u)
                throw cmd::CommandError("file too big (max 64 MB)");

            std::ifstream in(file, std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

            CmdJson result = { { "path", cmd::GetString(params, "path") }, { "size", data.size() } };

            if (encoding == "base64")
                result["content"] = cmd::Base64Encode(data);
            else if (encoding == "text")
                result["content"] = std::move(data);
            else
                throw cmd::CommandError("'encoding' must be \"text\" or \"base64\"");

            result["encoding"] = encoding;
            return result;
        });

    cmd::Register("asset.write",
        "Creates or replaces a file (folders are created). The previous version goes to .lynx/trash/. "
        "voxels.json and .js files are reloaded automatically.",
        {
            { "path", "string", "File path.", true },
            { "content", "string", "Content (text, or base64 with encoding=\"base64\").", true },
            { "encoding", "string", "\"text\" (default) or \"base64\".", false },
            { "append", "boolean", "Add at the end instead of replacing (default false).", false },
            { "overwrite", "boolean", "Allow replacing an existing file (default true).", false },
            root_param,
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string root = cmd::GetString(params, "root", "assets");
            const std::filesystem::path file = CmdResolvePath(cmd::GetString(params, "path"), root, true);
            const std::string encoding = cmd::GetString(params, "encoding", "text");
            const bool append = cmd::GetBool(params, "append", false);

            std::string data = cmd::GetString(params, "content");

            if (encoding == "base64")
            {
                std::string decoded;

                if (!cmd::Base64Decode(data, decoded))
                    throw cmd::CommandError("'content' is not valid base64");

                data = std::move(decoded);
            }
            else if (encoding != "text")
            {
                throw cmd::CommandError("'encoding' must be \"text\" or \"base64\"");
            }

            std::error_code ec;
            const bool existed = std::filesystem::exists(file, ec);

            if (existed && std::filesystem::is_directory(file, ec))
                throw cmd::CommandError("this path is a folder");

            if (existed && !append && !cmd::GetBool(params, "overwrite", true))
                throw cmd::CommandError("the file already exists (overwrite=false)");

            const std::string backup = existed ? CmdBackup(file) : std::string();

            std::filesystem::create_directories(file.parent_path(), ec);

            std::ofstream out(file, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            out.close();

            if (!out)
                throw cmd::CommandError("could not write " + file.string());

            CmdJson result = {
                { "path", CmdRelativeTo(file, root == "project" ? currentProject.root : currentProject.assets_dir) },
                { "size", data.size() },
                { "created", !existed },
            };

            if (!backup.empty())
                result["backup"] = backup;

            result["reloaded"] = CmdAfterFileChanged(file);
            return result;
        });

    cmd::Register("asset.delete",
        "Deletes a file or a folder (moved to .lynx/trash/, can be recovered from there).",
        {
            { "path", "string", "File or folder.", true },
            root_param,
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string root = cmd::GetString(params, "root", "assets");
            const std::filesystem::path path = CmdResolvePath(cmd::GetString(params, "path"), root, true);

            if (path == currentProject.assets_dir || path == currentProject.root)
                throw cmd::CommandError("cannot delete the root folder");

            std::error_code ec;

            if (!std::filesystem::exists(path, ec))
                throw cmd::CommandError("not found : " + cmd::GetString(params, "path"));

            const std::string backup = CmdBackup(path);

            if (backup.empty())
                throw cmd::CommandError("could not keep a copy in .lynx/trash : nothing deleted");

            std::filesystem::remove_all(path, ec);

            if (ec)
                throw cmd::CommandError("could not delete : " + ec.message());

            return { { "deleted", cmd::GetString(params, "path") }, { "backup", backup } };
        });

    cmd::Register("asset.move",
        "Moves / renames a file or folder.",
        {
            { "from", "string", "Current path.", true },
            { "to", "string", "New path.", true },
            { "overwrite", "boolean", "Replace the destination if it exists (default false).", false },
            root_param,
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string root = cmd::GetString(params, "root", "assets");
            const std::filesystem::path from = CmdResolvePath(cmd::GetString(params, "from"), root, true);
            const std::filesystem::path to = CmdResolvePath(cmd::GetString(params, "to"), root, true);

            std::error_code ec;

            if (!std::filesystem::exists(from, ec))
                throw cmd::CommandError("not found : " + cmd::GetString(params, "from"));

            if (std::filesystem::exists(to, ec))
            {
                if (!cmd::GetBool(params, "overwrite", false))
                    throw cmd::CommandError("the destination exists (overwrite=false)");

                CmdBackup(to);
                std::filesystem::remove_all(to, ec);
            }

            std::filesystem::create_directories(to.parent_path(), ec);
            std::filesystem::rename(from, to, ec);

            if (ec)
                throw cmd::CommandError("could not move : " + ec.message());

            return { { "from", cmd::GetString(params, "from") }, { "to", cmd::GetString(params, "to") } };
        });

    cmd::Register("asset.mkdir",
        "Creates a folder (and its parents).",
        {
            { "path", "string", "Folder path.", true },
            root_param,
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string root = cmd::GetString(params, "root", "assets");
            const std::filesystem::path folder = CmdResolvePath(cmd::GetString(params, "path"), root, true);

            std::error_code ec;
            std::filesystem::create_directories(folder, ec);

            if (ec)
                throw cmd::CommandError("could not create : " + ec.message());

            return { { "path", cmd::GetString(params, "path") } };
        });

    cmd::Register("asset.exists",
        "Tells whether a path exists : {exists, type}.",
        {
            { "path", "string", "Path.", true },
            root_param,
        },
        [](const CmdJson& params, const cmd::CommandContext&) -> CmdJson
        {
            const std::string root = cmd::GetString(params, "root", "assets");
            const std::filesystem::path path = CmdResolvePath(cmd::GetString(params, "path"), root, false);

            std::error_code ec;
            const bool exists = std::filesystem::exists(path, ec);

            return {
                { "exists", exists },
                { "type", !exists ? "none" : (std::filesystem::is_directory(path, ec) ? "dir" : "file") },
            };
        });
}
