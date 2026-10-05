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
        { "Level (.xml)",                        "NewLevel",  ".xml"  },
        { "JSON (.json)",                        "NewData",   ".json" },
    };

    struct State
    {
        stdfs::path selected;

        NameAction name_action = NameAction::None;
        NewFileKind new_file_kind = NewFileKind::Text;
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
            return
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<Level>\n"
                "</Level>\n";

        case NewFileKind::Json:
            return "{\n}\n";

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

            stdfs::rename(state.rename_target, destination, error);

            if (error)
            {
                state.name_error = "Could not rename : " + error.message();
                return false;
            }

            lynx::editor::script_editors::PathMoved(state.rename_target, destination);

            if (content_browser_current_path == state.rename_target)
                content_browser_current_path = destination;

            state.selected = destination;
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

            state.selected = destination;
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

            out << FileTemplate(state.new_file_kind, destination.stem().string());
        }

        state.selected = destination;

        if (IsJs(destination))
        {
            ScriptsChanged({ destination });
            lynx::editor::script_editors::Open(destination);
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

            if (IsJs(destination))
                RenameJsClassInCopy(destination, source.stem().string(), ToIdentifier(destination.stem().string()));

            created.push_back(destination);
        }

        if (!created.empty())
        {
            state.selected = created.back();
            ScriptsChanged(created);
        }
    }

    // Moves the entries to .lynx/trash/<date>/<path relative to the project>.
    static void Delete(const std::vector<stdfs::path>& targets)
    {
        std::vector<stdfs::path> deleted;

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

            if (IsPathInside(content_browser_current_path, target))
                content_browser_current_path = target.parent_path();

            if (state.selected == target)
                state.selected.clear();

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

            if (state.clipboard_cut)
            {
                stdfs::rename(source, destination, error);

                if (!error)
                    lynx::editor::script_editors::PathMoved(source, destination);
            }
            else
            {
                stdfs::copy(source, destination, stdfs::copy_options::recursive, error);

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
            state.selected = done.back();
            ScriptsChanged(done);
        }
    }


    // -------------------------------------------------------------------------
    // UI
    // -------------------------------------------------------------------------

    static void NewFileMenu(const stdfs::path& folder)
    {
        if (ImGui::MenuItem("New folder"))
            BeginNew(NameAction::NewFolder, NewFileKind::Text, folder);

        if (ImGui::BeginMenu("New file"))
        {
            for (int i = 0; i < static_cast<int>(NewFileKind::Count); ++i)
            {
                if (ImGui::MenuItem(kFileKinds[i].label))
                    BeginNew(NameAction::NewFile, static_cast<NewFileKind>(i), folder);
            }

            ImGui::EndMenu();
        }
    }

    // Right click menu of one item (call right after the item).
    static void ItemContextMenu(const stdfs::directory_entry& entry)
    {
        if (!ImGui::BeginPopupContextItem("##ItemContext"))
            return;

        const stdfs::path& path = entry.path();
        const bool is_directory = entry.is_directory();
        state.selected = path;

        if (ImGui::MenuItem(is_directory ? "Open folder" : "Open"))
            OpenEntry(path);

        ImGui::Separator();

        if (ImGui::MenuItem("Rename", "F2"))
            BeginRename(path);

        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            Duplicate({ path });

        if (ImGui::MenuItem("Copy", "Ctrl+C"))
            Copy({ path }, false);

        if (ImGui::MenuItem("Cut", "Ctrl+X"))
            Copy({ path }, true);

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
            state.delete_targets = { path };
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

        NewFileMenu(content_browser_current_path);

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
        const bool has_selection = !state.selected.empty() && stdfs::exists(state.selected, error);

        if (has_selection && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            state.delete_targets = { state.selected };
            state.open_delete_popup = true;
        }
        else if (has_selection && ImGui::IsKeyPressed(ImGuiKey_F2, false))
            BeginRename(state.selected);
        else if (has_selection && ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            Duplicate({ state.selected });
        else if (has_selection && ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            Copy({ state.selected }, false);
        else if (has_selection && ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
            Copy({ state.selected }, true);
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !state.clipboard.empty())
            Paste(content_browser_current_path);
        else if (has_selection && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
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
