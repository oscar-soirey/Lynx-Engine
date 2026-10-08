// =============================================================================
// Content Browser : file operations (included by EditorMain.cpp)
// -----------------------------------------------------------------------------
// Right click on an item   : Open, Rename, Duplicate, Copy / Cut, Paste into,
//                            Delete, Copy path, Show in Explorer.
// Right click on the empty : New folder, New file (text, JavaScript class,
//                            JavaScript script, level, JSON), Paste, Explorer.
// Shortcuts (window focused) : Del, F2, Ctrl+D, Ctrl+C, Ctrl+X, Ctrl+V.
//
// Deleted files are MOVED to <project>/.lynx/trash/<date>/... (same place as
// the files replaced by the editor commands) : nothing is lost by mistake.
// Uses the static state of EditorMain.cpp (content_browser_*, JS editor...).
// =============================================================================

namespace content_browser_actions
{
    namespace stdfs = std::filesystem;

    enum class NameAction
    {
        None,
        NewFolder,
        NewFile,
        Rename,
    };

    enum class NewFileKind
    {
        Text,
        JavaScriptClass,
        JavaScriptScript,
        Level,
        Json,
        Widget,
        AnimGraph,
        BehaviorTree,
        ParticleSystem,
        Count,
    };

    struct FileKindInfo
    {
        const char* label;
        const char* default_name;   // without extension
        const char* extension;
    };

    static const FileKindInfo kFileKinds[] = {
        { "Text file (.txt)",                    "NewFile",   ".txt"  },
        { "JavaScript class (actor)",            "NewActor",  ".js"   },
        { "JavaScript script (attached)",        "NewScript", ".js"   },
        { "Level (.level + voxel world)",        "NewLevel",  ".level" },
        { "JSON (.json)",                        "NewData",   ".json" },
        { "Widget (.widget, UI)",                "NewWidget", ".widget" },
        { "Anim Graph (.animgraph, sprites)",    "NewAnimGraph", ".animgraph" },
        { "Behavior Tree (.bt, AI)",             "NewBehaviorTree", ".bt" },
        { "Particle System (.vfx)",              "NewParticles", ".vfx" },
    };

    struct State
    {
        stdfs::path selected;                  // primary item (the last one clicked)
        std::vector<stdfs::path> selection;    // every selected item (selected is one of them)

        NameAction name_action = NameAction::None;
        NewFileKind new_file_kind = NewFileKind::Text;
        int plugin_file_kind = -1;             // index in plugins::GetNewFileKinds(), -1 : none
        stdfs::path name_folder;       // where the new entry goes
        stdfs::path rename_target;
        char name[256] = {};
        bool open_name_popup = false;
        bool focus_name = false;
        std::string name_error;

        std::vector<stdfs::path> delete_targets;
        bool open_delete_popup = false;

        std::vector<stdfs::path> clipboard;
        bool clipboard_cut = false;

        std::string error;
        bool open_error_popup = false;
    };

    static State state;


    // -------------------------------------------------------------------------
    // Selection (several items : Shift / Ctrl click, rubber band)
    // -------------------------------------------------------------------------

    static bool IsSelected(const stdfs::path& path)
    {
        return std::find(state.selection.begin(), state.selection.end(), path) != state.selection.end();
    }

    static void SelectOnly(const stdfs::path& path)
    {
        state.selection.clear();

        if (!path.empty())
            state.selection.push_back(path);

        state.selected = path;
    }

    static void ToggleSelected(const stdfs::path& path)
    {
        auto it = std::find(state.selection.begin(), state.selection.end(), path);

        if (it == state.selection.end())
        {
            state.selection.push_back(path);
            state.selected = path;
            return;
        }

        state.selection.erase(it);

        if (state.selected == path)
            state.selected = state.selection.empty() ? stdfs::path() : state.selection.back();
    }

    static void SetSelection(const std::vector<stdfs::path>& paths, const stdfs::path& primary)
    {
        state.selection.clear();

        for (const stdfs::path& path : paths)
        {
            if (!path.empty() && !IsSelected(path))
                state.selection.push_back(path);
        }

        if (!primary.empty() && IsSelected(primary))
            state.selected = primary;
        else
            state.selected = state.selection.empty() ? stdfs::path() : state.selection.back();
    }

    static void ClearSelection()
    {
        state.selection.clear();
        state.selected.clear();
    }

    static void RemoveFromSelection(const stdfs::path& path)
    {
        state.selection.erase(
            std::remove(state.selection.begin(), state.selection.end(), path),
            state.selection.end());

        if (state.selected == path)
            state.selected = state.selection.empty() ? stdfs::path() : state.selection.back();
    }

    // The selected items that still exist : what Delete / Copy / Cut / Duplicate act on.
    static std::vector<stdfs::path> SelectedPaths()
    {
        std::vector<stdfs::path> result;
        std::error_code error;

        for (const stdfs::path& path : state.selection)
        {
            if (stdfs::exists(path, error))
                result.push_back(path);
        }

        return result;
    }


    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    static std::string LowerExtension(const stdfs::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    static bool IsJs(const stdfs::path& path)
    {
        return LowerExtension(path) == ".js";
    }

    // -------------------------------------------------------------------------
    // Levels : a .level and its voxel world (.hrlv) go together
    // -------------------------------------------------------------------------

    static void ShowError(const std::string& message);

    static bool IsLevel(const stdfs::path& path)
    {
        return LowerExtension(path) == ".level";
    }

    // Path relative to assets/ ("" if outside).
    static std::string AssetsRelative(const stdfs::path& path)
    {
        if (!IsPathInside(path, content_browser_root))
            return {};
        std::error_code error;
        const stdfs::path relative = stdfs::relative(path, content_browser_root, error);
        return error ? std::string() : relative.generic_string();
    }

    // Voxel world linked to a .level on the disk (may not exist yet).
    static stdfs::path LinkedVoxelWorld(const stdfs::path& level)
    {
        std::string link = lynx::Level::ReadVoxelLinkOnDisk(level.string());
        if (link.empty())
            link = level.stem().string() + ".hrlv";
        return (level.parent_path() / link).lexically_normal();
    }

    // The voxel world "belongs" to the level (same name) : it follows it when
    // the level is renamed, duplicated, moved or deleted. A voxel world shared
    // under another name is left alone.
    static stdfs::path CompanionVoxelWorld(const stdfs::path& level)
    {
        if (!IsLevel(level))
            return {};
        const stdfs::path voxels = LinkedVoxelWorld(level);
        std::error_code error;
        if (voxels.stem() != level.stem() || !stdfs::exists(voxels, error))
            return {};
        return voxels;
    }

    // `level` (after a copy / rename) : its voxel world gets its name and the
    // link is written. `old_voxels` : the companion of the source.
    static void FollowLevel(const stdfs::path& level, const stdfs::path& old_voxels, bool move)
    {
        if (old_voxels.empty())
            return;

        std::error_code error;
        const stdfs::path voxels = level.parent_path() / (level.stem().string() + old_voxels.extension().string());

        if (voxels != old_voxels)
        {
            if (stdfs::exists(voxels, error))
            {
                ShowError("The voxel world " + voxels.filename().string() + " already exists : the level keeps "
                          "the link to " + old_voxels.filename().string() + ".");
                return;
            }
            if (move)
                stdfs::rename(old_voxels, voxels, error);
            else
                stdfs::copy_file(old_voxels, voxels, error);
            if (error)
            {
                ShowError("Could not " + std::string(move ? "move " : "copy ") + old_voxels.generic_string() +
                          " :\n" + error.message());
                return;
            }
        }

        lynx::Level::SetLinkedVoxelWorldOnDisk(level.string(), voxels.filename().string());
    }

    // The open level was renamed / moved : the editor follows it.
    static void LevelMoved(const stdfs::path& from, const stdfs::path& to)
    {
        if (AssetsRelative(from) != current_level_file)
            return;
        current_level_file = AssetsRelative(to);
        world_file_path_string = (stdfs::path("assets") / lynx::Level::GetLinkedVoxelWorld(current_level_file)).string();
        RememberLastLevel(current_level_file);
    }

    static void OpenLevel(const stdfs::path& path)
    {
        const std::string relative = AssetsRelative(path);
        if (!relative.empty() && relative != current_level_file)
            editor::RequestOpenLevel(relative);
    }

    // assets/save_file.txt : the level the game starts with.
    static void SetStartupLevel(const stdfs::path& path)
    {
        const std::string relative = AssetsRelative(path);
        std::ofstream out(content_browser_root / "save_file.txt", std::ios::binary | std::ios::trunc);
        if (!out || relative.empty())
        {
            ShowError("Could not write assets/save_file.txt");
            return;
        }
        out << relative;
        std::cout << "[CONTENT BROWSER] Startup level : " << relative << "\n";
    }

    static std::string StartupLevel()
    {
        std::ifstream in(content_browser_root / "save_file.txt", std::ios::binary);
        std::string line;
        if (in)
            std::getline(in, line);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        return line;
    }

    static void ShowError(const std::string& message)
    {
        state.error = message;
        state.open_error_popup = true;
        std::cerr << "[CONTENT BROWSER] " << message << "\n";
    }

    // A file name the user typed : not empty, no folder separator, no
    // character refused by Windows.
    static bool IsValidName(const std::string& name, std::string& why)
    {
        if (name.empty() || name.find_first_not_of(" .") == std::string::npos)
        {
            why = "The name is empty.";
            return false;
        }

        if (name.find_first_of("/\\:*?\"<>|") != std::string::npos)
        {
            why = "A name cannot contain / \\ : * ? \" < > |";
            return false;
        }

        if (name.back() == ' ' || name.back() == '.')
        {
            why = "A name cannot end with a space or a dot.";
            return false;
        }

        return true;
    }

    // "Enemy Boss-2" -> "EnemyBoss_2" (valid JavaScript class name).
    static std::string ToIdentifier(const std::string& text)
    {
        std::string out;

        for (const char c : text)
        {
            const unsigned char u = static_cast<unsigned char>(c);

            if (std::isalnum(u) || c == '_' || c == '$')
                out += c;
            else if (c == '-')
                out += '_';
        }

        if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
            out = "C" + out;

        return out;
    }

    static std::string FileTemplate(NewFileKind kind, const std::string& stem)
    {
        switch (kind)
        {
        case NewFileKind::JavaScriptClass:
        {
            const std::string name = ToIdentifier(stem);
            return
                "// Actor class : shows up in Place Actors, levels and Level.spawn(\"" + name + "\").\n"
                "// It can live anywhere in assets/.\n"
                "class " + name + " extends Actor {\n"
                "    // Editable in Details, saved with the level.\n"
                "    static properties = {\n"
                "        speed: 2.0,\n"
                "    };\n"
                "\n"
                "    BeginPlay() {\n"
                "    }\n"
                "\n"
                "    Update(dt) {\n"
                "    }\n"
                "\n"
                "    EndPlay() {\n"
                "    }\n"
                "}\n";
        }

        case NewFileKind::JavaScriptScript:
            return
                "// Script attached to an actor (scripts property) : `parent` is that actor.\n"
                "let speed = 1.0;\n"
                "\n"
                "function BeginPlay() {\n"
                "}\n"
                "\n"
                "function Update(dt) {\n"
                "}\n"
                "\n"
                "function EndPlay() {\n"
                "}\n";

        case NewFileKind::Level:
            // Linked to <name>.hrlv next to it (created empty when opened).
            return
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<Level voxels=\"" + stem + ".hrlv\">\n"
                "</Level>\n";

        case NewFileKind::Json:
            return "{\n}\n";

        case NewFileKind::Widget:
            return lynx::editor::widget_editor::NewFileTemplate();

        case NewFileKind::AnimGraph:
            return lynx::editor::graph_editors::NewAnimGraphTemplate();

        case NewFileKind::BehaviorTree:
            return lynx::editor::graph_editors::NewBehaviorTreeTemplate();

        case NewFileKind::ParticleSystem:
            return lynx::editor::particle_editor::NewFileTemplate();

        default:
            return {};
        }
    }

    // A JS file declares "class <old_name>" : the copy gets the class name of
    // its new file, otherwise the two files would register the same class.
    static void RenameJsClassInCopy(const stdfs::path& copy, const std::string& old_name, const std::string& new_name)
    {
        std::ifstream in(copy, std::ios::binary);
        std::string code{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        in.close();

        const std::string from = "class " + old_name;
        const size_t at = code.find(from);

        if (at == std::string::npos)
            return;

        const size_t after = at + from.size();

        if (after < code.size() &&
            (std::isalnum(static_cast<unsigned char>(code[after])) || code[after] == '_' || code[after] == '$'))
            return;   // "class EnemyBoss" when renaming Enemy

        code.replace(at, from.size(), "class " + new_name);

        std::ofstream out(copy, std::ios::binary | std::ios::trunc);
        out << code;
    }

    static void ScriptsChanged(const std::vector<stdfs::path>& paths)
    {
        for (const stdfs::path& path : paths)
        {
            std::error_code error;

            if (IsJs(path) || stdfs::is_directory(path, error))
            {
                lynx::ReloadScripts();
                return;
            }
        }
    }

    static stdfs::path TrashFolder()
    {
        static std::string stamp;

        if (stamp.empty())
        {
            const std::time_t now = std::time(nullptr);
            char text[32];
            std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", std::localtime(&now));
            stamp = text;
        }

        return stdfs::path(".lynx") / "trash" / stamp;
    }

    static void OpenInExplorer(const stdfs::path& path, bool select)
    {
        std::error_code error;
        const stdfs::path absolute = stdfs::absolute(path, error);

#ifdef _WIN32
        if (select)
        {
            const std::wstring arguments = L"/select,\"" + absolute.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
        }
        else
        {
            ShellExecuteW(nullptr, L"open", absolute.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
#else
        (void)select;
        (void)absolute;
#endif
    }

    static void OpenEntry(const stdfs::path& path)
    {
        std::error_code error;

        if (stdfs::is_directory(path, error))
            content_browser_current_path = path;
        else if (IsLevel(path))
            OpenLevel(path);
        else if (lynx::editor::plugins::CanOpen(path) && lynx::editor::plugins::OpenFile(path))
            return;
        else if (lynx::editor::widget_editor::CanOpen(path))
            lynx::editor::widget_editor::Open(path);
        else if (lynx::editor::graph_editors::CanOpen(path))
            lynx::editor::graph_editors::Open(path);
        else if (lynx::editor::particle_editor::CanOpen(path))
            lynx::editor::particle_editor::Open(path);
        else if (lynx::editor::script_editors::CanOpen(path))
            lynx::editor::script_editors::Open(path);
        else
            OpenInExplorer(path, false);   // default application
    }


    // -------------------------------------------------------------------------
    // Operations
    // -------------------------------------------------------------------------

    static void BeginNew(NameAction action, NewFileKind kind, const stdfs::path& folder)
    {
        state.name_action = action;
        state.new_file_kind = kind;
        state.plugin_file_kind = -1;
        state.name_folder = folder;
        state.name_error.clear();

        const std::string name = action == NameAction::NewFolder
            ? std::string("NewFolder")
            : std::string(kFileKinds[static_cast<int>(kind)].default_name) + kFileKinds[static_cast<int>(kind)].extension;

        // A free name : NewActor.js, NewActor_1.js...
        const stdfs::path unique = MakeUniqueDestinationPath(folder / name);
        std::snprintf(state.name, sizeof(state.name), "%s", unique.filename().string().c_str());

        state.open_name_popup = true;
        state.focus_name = true;
    }

    static void BeginRename(const stdfs::path& target)
    {
        state.name_action = NameAction::Rename;
        state.rename_target = target;
        state.name_folder = target.parent_path();
        state.name_error.clear();
        std::snprintf(state.name, sizeof(state.name), "%s", target.filename().string().c_str());
        state.open_name_popup = true;
        state.focus_name = true;
    }

    // The name popup was validated. false : keep the popup open (name_error).
    static bool ApplyName()
    {
        const std::string name = state.name;

        if (!IsValidName(name, state.name_error))
            return false;

        const stdfs::path destination = state.name_folder / name;
        std::error_code error;

        if (state.name_action == NameAction::Rename)
        {
            if (destination == state.rename_target)
                return true;

            // Windows : "enemy.js" -> "Enemy.js" is the same file, allowed.
            const bool same_file = stdfs::exists(destination, error) &&
                                   stdfs::equivalent(destination, state.rename_target, error);

            if (stdfs::exists(destination, error) && !same_file)
            {
                state.name_error = "\"" + name + "\" already exists.";
                return false;
            }

            const stdfs::path old_voxels = CompanionVoxelWorld(state.rename_target);

            stdfs::rename(state.rename_target, destination, error);

            if (error)
            {
                state.name_error = "Could not rename : " + error.message();
                return false;
            }

            // Level : its voxel world takes the new name, the editor follows.
            if (IsLevel(destination))
                FollowLevel(destination, old_voxels, true);
            LevelMoved(state.rename_target, destination);

            lynx::editor::script_editors::PathMoved(state.rename_target, destination);
            lynx::editor::widget_editor::PathMoved(state.rename_target, destination);
            lynx::editor::graph_editors::PathMoved(state.rename_target, destination);

            if (content_browser_current_path == state.rename_target)
                content_browser_current_path = destination;

            SelectOnly(destination);
            ScriptsChanged({ destination });
            return true;
        }

        if (stdfs::exists(destination, error))
        {
            state.name_error = "\"" + name + "\" already exists.";
            return false;
        }

        if (state.name_action == NameAction::NewFolder)
        {
            if (!stdfs::create_directories(destination, error) || error)
            {
                state.name_error = "Could not create the folder : " + error.message();
                return false;
            }

            SelectOnly(destination);
            return true;
        }

        // New file.
        {
            std::ofstream out(destination, std::ios::binary);

            if (!out)
            {
                state.name_error = "Could not create " + destination.generic_string();
                return false;
            }

            if (state.plugin_file_kind >= 0 &&
                state.plugin_file_kind < static_cast<int>(lynx::editor::plugins::GetNewFileKinds().size()))
            {
                // File kind of a plugin : its template, {{NAME}} = the name.
                std::string text = lynx::editor::plugins::GetNewFileKinds()[state.plugin_file_kind].content;
                const std::string stem = destination.stem().string();
                for (size_t pos = 0; (pos = text.find("{{NAME}}", pos)) != std::string::npos; pos += stem.size())
                    text.replace(pos, 8, stem);
                out << text;
            }
            else
            {
                out << FileTemplate(state.new_file_kind, destination.stem().string());
            }
        }
        state.plugin_file_kind = -1;

        SelectOnly(destination);

        if (IsJs(destination))
        {
            ScriptsChanged({ destination });
            lynx::editor::script_editors::Open(destination);
        }
        else if (lynx::editor::widget_editor::CanOpen(destination))
        {
            lynx::editor::widget_editor::Open(destination);
        }
        else if (lynx::editor::graph_editors::CanOpen(destination))
        {
            lynx::editor::graph_editors::Open(destination);
        }
        else if (lynx::editor::particle_editor::CanOpen(destination))
        {
            lynx::editor::particle_editor::Open(destination);
        }
        else if (lynx::editor::plugins::CanOpen(destination))
        {
            lynx::editor::plugins::OpenFile(destination);
        }

        return true;
    }

    static void Duplicate(const std::vector<stdfs::path>& sources)
    {
        std::vector<stdfs::path> created;

        for (const stdfs::path& source : sources)
        {
            const stdfs::path destination = MakeUniqueDestinationPath(source);
            std::error_code error;

            stdfs::copy(source, destination, stdfs::copy_options::recursive, error);

            if (error)
            {
                ShowError("Could not duplicate " + source.generic_string() + " :\n" + error.message());
                continue;
            }

            // Level : a copy of its voxel world, linked to the copy.
            if (IsLevel(destination))
                FollowLevel(destination, CompanionVoxelWorld(source), false);

            if (IsJs(destination))
                RenameJsClassInCopy(destination, source.stem().string(), ToIdentifier(destination.stem().string()));

            created.push_back(destination);
        }

        if (!created.empty())
        {
            SetSelection(created, created.back());
            ScriptsChanged(created);
        }
    }

    // Moves the entries to .lynx/trash/<date>/<path relative to the project>.
    static void Delete(const std::vector<stdfs::path>& requested)
    {
        std::vector<stdfs::path> deleted;

        // A level goes with its voxel world ; the open level stays.
        std::vector<stdfs::path> targets;
        for (const stdfs::path& target : requested)
        {
            if (IsLevel(target) && AssetsRelative(target) == current_level_file)
            {
                ShowError(target.filename().string() + " is the open level : open another level before deleting it.");
                continue;
            }
            targets.push_back(target);
            const stdfs::path voxels = CompanionVoxelWorld(target);
            if (!voxels.empty() && std::find(targets.begin(), targets.end(), voxels) == targets.end() &&
                std::find(requested.begin(), requested.end(), voxels) == requested.end())
                targets.push_back(voxels);
        }

        for (const stdfs::path& target : targets)
        {
            std::error_code error;

            if (!IsPathInside(target, content_browser_root) ||
                stdfs::equivalent(target, content_browser_root, error))
            {
                ShowError("Refused : " + target.generic_string() + " is not inside assets/.");
                continue;
            }

            const stdfs::path relative = stdfs::relative(target, stdfs::current_path(), error);
            stdfs::path destination = TrashFolder() / (error ? target.filename() : relative);
            destination = MakeUniqueDestinationPath(destination);

            error.clear();
            stdfs::create_directories(destination.parent_path(), error);
            error.clear();
            stdfs::rename(target, destination, error);

            // Another drive, locked file... : copy then remove.
            if (error)
            {
                error.clear();
                stdfs::copy(target, destination, stdfs::copy_options::recursive, error);

                if (!error)
                    stdfs::remove_all(target, error);
            }

            if (error)
            {
                ShowError("Could not delete " + target.generic_string() + " :\n" + error.message());
                continue;
            }

            std::cout << "[CONTENT BROWSER] Deleted " << target.generic_string()
                      << " (kept in " << destination.generic_string() << ")\n";

            lynx::editor::script_editors::PathDeleted(target);
            lynx::editor::widget_editor::PathDeleted(target);
            lynx::editor::graph_editors::PathDeleted(target);
            lynx::editor::particle_editor::PathDeleted(target);

            if (IsPathInside(content_browser_current_path, target))
                content_browser_current_path = target.parent_path();

            RemoveFromSelection(target);

            deleted.push_back(target);
        }

        // Reload after the move : the paths no longer exist, test the names.
        for (const stdfs::path& path : deleted)
        {
            if (IsJs(path) || !path.has_extension())
            {
                lynx::ReloadScripts();
                break;
            }
        }
    }

    static void Copy(const std::vector<stdfs::path>& paths, bool cut)
    {
        state.clipboard = paths;
        state.clipboard_cut = cut;
    }

    static void Paste(const stdfs::path& folder)
    {
        std::vector<stdfs::path> done;

        for (const stdfs::path& source : state.clipboard)
        {
            std::error_code error;

            if (!stdfs::exists(source, error))
                continue;

            if (stdfs::is_directory(source, error) && IsPathInside(folder, source))
            {
                ShowError("Cannot paste " + source.filename().string() + " inside itself.");
                continue;
            }

            stdfs::path destination = folder / source.filename();

            if (state.clipboard_cut && destination == source)
                continue;

            destination = MakeUniqueDestinationPath(destination);

            const stdfs::path old_voxels = CompanionVoxelWorld(source);

            if (state.clipboard_cut)
            {
                stdfs::rename(source, destination, error);

                if (!error && IsLevel(destination))
                {
                    FollowLevel(destination, old_voxels, true);
                    LevelMoved(source, destination);
                }

                if (!error)
                {
                    lynx::editor::script_editors::PathMoved(source, destination);
                    lynx::editor::widget_editor::PathMoved(source, destination);
                    lynx::editor::graph_editors::PathMoved(source, destination);
                    lynx::editor::particle_editor::PathMoved(source, destination);
                }
            }
            else
            {
                stdfs::copy(source, destination, stdfs::copy_options::recursive, error);

                if (!error && IsLevel(destination))
                    FollowLevel(destination, old_voxels, false);

                if (!error && IsJs(destination) && destination.stem() != source.stem())
                    RenameJsClassInCopy(destination, source.stem().string(), ToIdentifier(destination.stem().string()));
            }

            if (error)
            {
                ShowError("Could not paste " + source.generic_string() + " :\n" + error.message());
                continue;
            }

            done.push_back(destination);
        }

        if (state.clipboard_cut)
            state.clipboard.clear();

        if (!done.empty())
        {
            SetSelection(done, done.back());
            ScriptsChanged(done);
        }
    }


    // -------------------------------------------------------------------------
    // UI
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // "New" panel (Unreal-like) : sections, big icons, a description per item,
    // a search field. Used by the right click on the empty grid (directly)
    // and on a folder (sub menu "New").
    // -------------------------------------------------------------------------

    struct NewEntry
    {
        const char* section;
        std::string label;
        std::string description;
        lynx::editor::file_icons::Kind icon;
        std::function<void()> create;
    };

    static const char* KindSection(NewFileKind kind)
    {
        switch (kind)
        {
        case NewFileKind::JavaScriptClass:
        case NewFileKind::JavaScriptScript: return "Scripting";
        case NewFileKind::Level:            return "World";
        case NewFileKind::Widget:           return "User Interface";
        case NewFileKind::AnimGraph:        return "Animation";
        case NewFileKind::BehaviorTree:     return "Artificial Intelligence";
        case NewFileKind::ParticleSystem:   return "Effects";
        default:                            return "Data";
        }
    }

    static const char* KindDescription(NewFileKind kind)
    {
        switch (kind)
        {
        case NewFileKind::Text:             return "Plain text file : notes, data.";
        case NewFileKind::JavaScriptClass:  return "A new actor class (class X extends Actor).";
        case NewFileKind::JavaScriptScript: return "A behaviour to attach to any actor.";
        case NewFileKind::Level:            return "A level and its voxel world (.hrlv).";
        case NewFileKind::Json:             return "Structured data read by your scripts.";
        case NewFileKind::Widget:           return "A user interface (menu, HUD), Widget Editor.";
        case NewFileKind::AnimGraph:        return "Sprite animation states and transitions.";
        case NewFileKind::BehaviorTree:     return "Decisions of an AI : tasks, decorators.";
        case NewFileKind::ParticleSystem:   return "Particle effect (fire, smoke, sparks), Particle Editor.";
        default:                            return "";
        }
    }

    static lynx::editor::file_icons::Kind IconForExtension(const std::string& extension)
    {
        return lynx::editor::file_icons::ForFile(stdfs::path("new" + extension), false);
    }

    static std::vector<NewEntry> NewEntries(const stdfs::path& folder)
    {
        using lynx::editor::file_icons::Kind;
        std::vector<NewEntry> entries;

        entries.push_back({ "Folder", "Folder", "Organize the assets.", Kind::Folder,
                            [folder]() { BeginNew(NameAction::NewFolder, NewFileKind::Text, folder); } });

        // Order of the sections : World, Scripting, User Interface, Animation, AI, Effects, Data.
        const NewFileKind order[] = {
            NewFileKind::Level, NewFileKind::JavaScriptClass, NewFileKind::JavaScriptScript, NewFileKind::Widget,
            NewFileKind::AnimGraph, NewFileKind::BehaviorTree, NewFileKind::ParticleSystem, NewFileKind::Json,
            NewFileKind::Text,
        };
        for (NewFileKind kind : order)
        {
            const FileKindInfo& info = kFileKinds[static_cast<int>(kind)];
            Kind icon = IconForExtension(info.extension);
            if (kind == NewFileKind::JavaScriptClass || kind == NewFileKind::JavaScriptScript)
                icon = Kind::Script;
            entries.push_back({ KindSection(kind), info.label, KindDescription(kind), icon,
                                [folder, kind]() { BeginNew(NameAction::NewFile, kind, folder); } });
        }

        // File kinds of the plugins (.dialogue, .sequence...) : one section per plugin.
        const auto& plugin_kinds = lynx::editor::plugins::GetNewFileKinds();
        for (int i = 0; i < static_cast<int>(plugin_kinds.size()); ++i)
        {
            const auto& pk = plugin_kinds[i];
            entries.push_back({ pk.plugin.c_str(), pk.label, "Plugin " + pk.plugin + " (" + pk.extension + ")",
                                IconForExtension(pk.extension),
                                [folder, i]()
                                {
                                    const auto& kinds = lynx::editor::plugins::GetNewFileKinds();
                                    BeginNew(NameAction::NewFile, NewFileKind::Text, folder);
                                    state.plugin_file_kind = i;
                                    const stdfs::path unique = MakeUniqueDestinationPath(
                                        folder / (kinds[i].default_name + kinds[i].extension));
                                    std::snprintf(state.name, sizeof(state.name), "%s", unique.filename().string().c_str());
                                } });
        }
        return entries;
    }

    static std::string LowerText(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // Section title : small capitals and a line, like Unreal.
    static void NewSectionHeader(const char* title)
    {
        ImGui::Spacing();
        const std::string upper = [&] { std::string t = title; for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return t; }();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.66f, 1.f), "%s", upper.c_str());
        const float text_w = ImGui::CalcTextSize(upper.c_str()).x;
        const float y = pos.y + ImGui::GetTextLineHeight() * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x + text_w + 8.f, y), ImVec2(pos.x + width, y),
                                            ImGui::GetColorU32(ImGuiCol_Separator));
    }

    // One row : big icon, name, description. true : clicked.
    static bool NewEntryRow(const NewEntry& entry, int index)
    {
        const float icon = 32.f;
        const float pad = 6.f;
        const float line = ImGui::GetTextLineHeight();
        const float height = std::max(icon, line * 2.f + 2.f) + pad * 2.f;
        const float width = std::max(360.f, ImGui::GetContentRegionAvail().x);

        ImGui::PushID(index);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##new", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered)
            dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_HeaderHovered), 4.f);

        // Icon on a small dark tile.
        const ImVec2 tile(pos.x + pad, pos.y + (height - icon) * 0.5f);
        dl->AddRectFilled(ImVec2(tile.x - 2.f, tile.y - 2.f), ImVec2(tile.x + icon + 2.f, tile.y + icon + 2.f),
                          IM_COL32(20, 20, 24, 200), 4.f);
        lynx::editor::file_icons::DrawAt(dl, entry.icon, tile, icon);

        const float text_x = pos.x + pad + icon + 12.f;
        const float text_y = pos.y + (height - line * 2.f - 2.f) * 0.5f;
        dl->AddText(ImVec2(text_x, text_y), ImGui::GetColorU32(ImGuiCol_Text), entry.label.c_str());
        dl->AddText(ImVec2(text_x, text_y + line + 2.f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    entry.description.c_str());
        return clicked;
    }

    // The panel itself (inside a popup or a menu).
    static void NewFilePanel(const stdfs::path& folder)
    {
        static char filter[64] = "";
        if (ImGui::IsWindowAppearing())
        {
            filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(std::max(360.f, ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("##newsearch", "Search a type of asset...", filter, sizeof(filter));

        const std::string needle = LowerText(filter);
        const std::vector<NewEntry> entries = NewEntries(folder);
        std::string section;
        int shown = 0;

        for (int i = 0; i < static_cast<int>(entries.size()); ++i)
        {
            const NewEntry& entry = entries[i];
            if (!needle.empty() &&
                LowerText(entry.label).find(needle) == std::string::npos &&
                LowerText(entry.description).find(needle) == std::string::npos &&
                LowerText(entry.section).find(needle) == std::string::npos)
                continue;

            if (section != entry.section)
            {
                section = entry.section;
                NewSectionHeader(entry.section);
            }

            if (NewEntryRow(entry, i))
            {
                entry.create();
                ImGui::CloseCurrentPopup();
            }
            ++shown;
        }

        if (shown == 0)
            ImGui::TextDisabled("Nothing matches \"%s\".", filter);
    }

    static void NewFileMenu(const stdfs::path& folder)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
        if (ImGui::BeginMenu("New..."))
        {
            NewFilePanel(folder);
            ImGui::EndMenu();
        }
        ImGui::PopStyleVar();
    }

    // Right click menu of one item (call right after the item).
    static void ItemContextMenu(const stdfs::directory_entry& entry)
    {
        if (!ImGui::BeginPopupContextItem("##ItemContext"))
            return;

        const stdfs::path& path = entry.path();
        const bool is_directory = entry.is_directory();

        // Right click on an item of the selection keeps the whole selection ;
        // on another item, selects only that one.
        if (!IsSelected(path))
            SelectOnly(path);
        else
            state.selected = path;

        // Duplicate / Copy / Cut / Delete act on the whole selection.
        std::vector<stdfs::path> targets = SelectedPaths();
        if (std::find(targets.begin(), targets.end(), path) == targets.end())
            targets = { path };

        if (ImGui::MenuItem(is_directory ? "Open folder" : IsLevel(path) ? "Open level" : "Open",
                            nullptr, false, !(IsLevel(path) && AssetsRelative(path) == current_level_file)))
            OpenEntry(path);

        if (IsLevel(path))
        {
            const bool startup = AssetsRelative(path) == StartupLevel();
            if (ImGui::MenuItem("Startup level of the game", nullptr, startup, !startup))
                SetStartupLevel(path);

            const stdfs::path voxels = LinkedVoxelWorld(path);
            ImGui::TextDisabled("Voxel world : %s", voxels.filename().string().c_str());
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Rename", "F2"))
            BeginRename(path);

        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            Duplicate(targets);

        if (ImGui::MenuItem("Copy", "Ctrl+C"))
            Copy(targets, false);

        if (ImGui::MenuItem("Cut", "Ctrl+X"))
            Copy(targets, true);

        if (is_directory && ImGui::MenuItem("Paste into", nullptr, false, !state.clipboard.empty()))
            Paste(path);

        if (is_directory)
        {
            ImGui::Separator();
            NewFileMenu(path);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Copy path (relative to assets)"))
        {
            std::error_code error;
            const std::string relative = stdfs::relative(path, content_browser_root, error).generic_string();
            ImGui::SetClipboardText(error ? path.generic_string().c_str() : relative.c_str());
        }

        if (ImGui::MenuItem("Show in Explorer"))
            OpenInExplorer(path, true);

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del"))
        {
            state.delete_targets = targets;
            state.open_delete_popup = true;
        }

        ImGui::EndPopup();
    }

    // Right click on the empty part of the grid.
    static void BackgroundContextMenu()
    {
        if (!ImGui::BeginPopupContextWindow("##BrowserContext",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            return;

        // Unreal-like : the "Add" panel right in the menu.
        NewSectionHeader("Add to this folder");
        NewFilePanel(content_browser_current_path);

        ImGui::Separator();

        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !state.clipboard.empty()))
            Paste(content_browser_current_path);

        if (ImGui::MenuItem("Open in Explorer"))
            OpenInExplorer(content_browser_current_path, false);

        ImGui::EndPopup();
    }

    static void Shortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
            ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            return;

        const bool ctrl = ImGui::GetIO().KeyCtrl;
        std::error_code error;
        const std::vector<stdfs::path> targets = SelectedPaths();
        const bool has_selection = !targets.empty();
        // Rename / Open work on one item : the last one clicked.
        const bool has_primary = !state.selected.empty() && stdfs::exists(state.selected, error);

        if (has_selection && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            state.delete_targets = targets;
            state.open_delete_popup = true;
        }
        else if (has_primary && ImGui::IsKeyPressed(ImGuiKey_F2, false))
            BeginRename(state.selected);
        else if (has_selection && ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            Duplicate(targets);
        else if (has_selection && ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            Copy(targets, false);
        else if (has_selection && ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
            Copy(targets, true);
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !state.clipboard.empty())
            Paste(content_browser_current_path);
        else if (has_primary && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            OpenEntry(state.selected);
    }

    // Popups (name, delete confirmation, error). Inside the Content Browser
    // window, after the grid.
    static void Popups()
    {
        // --- Name -------------------------------------------------------------
        const char* title =
            state.name_action == NameAction::Rename ? "Rename##ContentBrowser" :
            state.name_action == NameAction::NewFolder ? "New folder##ContentBrowser" :
                                                         "New file##ContentBrowser";

        if (state.open_name_popup)
        {
            ImGui::OpenPopup(title);
            state.open_name_popup = false;
        }

        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (state.name_action == NameAction::NewFile)
                ImGui::TextDisabled("%s", kFileKinds[static_cast<int>(state.new_file_kind)].label);

            ImGui::TextDisabled("in %s", state.name_folder.generic_string().c_str());

            if (state.focus_name)
            {
                ImGui::SetKeyboardFocusHere();
                state.focus_name = false;
            }

            ImGui::SetNextItemWidth(360.f);
            const bool enter = ImGui::InputText("##Name", state.name, sizeof(state.name),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

            if (state.name_action == NameAction::NewFile &&
                state.new_file_kind == NewFileKind::JavaScriptClass)
            {
                ImGui::TextDisabled("Class name : %s",
                                    ToIdentifier(stdfs::path(state.name).stem().string()).c_str());
            }

            if (!state.name_error.empty())
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.f), "%s", state.name_error.c_str());

            if (ImGui::Button("OK", ImVec2(110.f, 0.f)) || enter)
            {
                if (ApplyName())
                {
                    state.name_action = NameAction::None;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(110.f, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                state.name_action = NameAction::None;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // --- Delete confirmation -----------------------------------------------
        if (state.open_delete_popup)
        {
            ImGui::OpenPopup("Delete?##ContentBrowser");
            state.open_delete_popup = false;
        }

        if (ImGui::BeginPopupModal("Delete?##ContentBrowser", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            for (const stdfs::path& target : state.delete_targets)
                ImGui::BulletText("%s", target.generic_string().c_str());

            ImGui::TextDisabled("Moved to .lynx/trash/ (can be restored from there).");

            if (ImGui::Button("Delete", ImVec2(110.f, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                Delete(state.delete_targets);
                state.delete_targets.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(110.f, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                state.delete_targets.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // --- Error -------------------------------------------------------------
        if (state.open_error_popup)
        {
            ImGui::OpenPopup("Error##ContentBrowserActions");
            state.open_error_popup = false;
        }

        if (ImGui::BeginPopupModal("Error##ContentBrowserActions", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(state.error.c_str());

            if (ImGui::Button("OK", ImVec2(110.f, 0.f)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }
}
