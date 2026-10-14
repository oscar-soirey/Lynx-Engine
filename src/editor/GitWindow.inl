// =============================================================================
// Git window (included by EditorMain.cpp)
// -----------------------------------------------------------------------------
// Version control of the project folder with the git executable (PATH) :
//   - branch, ahead / behind, Fetch / Pull / Push
//   - changed files : stage / unstage (checkbox), diff, discard
//   - commit (message), amend
//   - history (git log) with the details of a commit
//   - branches (switch, new), remote URL, user name / email, .gitignore
//
// Every git command runs on a worker thread, without console window : the
// editor never freezes (a push can take seconds). The window refreshes the
// status after each command and every few seconds while it is open.
// =============================================================================

#include <mutex>
#include <thread>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace git_window
{
    namespace stdfs = std::filesystem;
    using GitClock = std::chrono::steady_clock;

    struct ProcessResult
    {
        int exit_code = -1;
        std::string output;   // stdout + stderr
        bool started = false;
    };

    struct FileChange
    {
        std::string path;
        std::string original;   // renames : old path
        char index = ' ';       // X of "XY" : staged state
        char worktree = ' ';    // Y : working tree state

        bool Untracked() const { return index == '?'; }
        bool Staged() const { return index != ' ' && index != '?'; }
        bool Unstaged() const { return worktree != ' ' || Untracked(); }
        bool Conflict() const
        {
            return index == 'U' || worktree == 'U' || (index == 'A' && worktree == 'A') ||
                   (index == 'D' && worktree == 'D');
        }
    };

    struct CommitEntry
    {
        std::string hash;
        std::string author;
        std::string when;
        std::string subject;
    };

    // Filled by the worker, read by the UI (copied under the mutex).
    struct RepositoryState
    {
        bool git_found = false;
        bool is_repository = false;
        std::string git_version;
        std::string branch;
        std::string upstream;
        int ahead = 0;
        int behind = 0;
        bool no_commits = false;
        std::vector<FileChange> changes;
        std::vector<std::string> branches;
        std::vector<CommitEntry> history;
        std::string remote_url;
        std::string user_name;
        std::string user_email;
    };

    struct State
    {
        std::mutex mutex;
        std::thread worker;
        bool worker_running = false;    // UI thread
        bool busy = false;              // UI thread : a user command runs (buttons disabled)
        bool done = false;              // guarded by mutex
        std::string busy_label;
        std::function<void()> queued;   // UI thread : started when the worker is free

        RepositoryState repo;           // guarded by mutex
        std::string log;                // guarded by mutex : output of the commands
        bool log_scroll = false;

        std::string details_title;      // guarded by mutex : diff / commit details
        std::string details;
        std::vector<size_t> details_lines;   // start of each line
        bool details_is_diff = true;

        bool refreshed_once = false;
        GitClock::time_point last_refresh{};

        // UI only.
        std::vector<char> message = std::vector<char>(8192, '\0');
        bool amend = false;
        char new_branch[128] = {};
        char remote_url[512] = {};
        char user_name[128] = {};
        char user_email[256] = {};
        bool settings_loaded = false;
        std::string selected_file;
        std::string discard_target;
        bool discard_untracked = false;
        bool open_discard_popup = false;
        bool open_new_branch_popup = false;
    };

    static State state;

    static constexpr int kHistoryCount = 100;
    static constexpr size_t kMaxDetails = 400 * 1024;
    static constexpr size_t kMaxLog = 200 * 1024;


    // -------------------------------------------------------------------------
    // Process
    // -------------------------------------------------------------------------

#ifdef _WIN32
    static std::wstring Wide(const std::string& text)
    {
        if (text.empty())
            return {};

        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
        return out;
    }

    // Windows command line rules (CommandLineToArgvW).
    static std::wstring QuoteArgument(const std::wstring& argument)
    {
        if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
            return argument;

        std::wstring out = L"\"";

        for (size_t i = 0; ; ++i)
        {
            size_t backslashes = 0;

            while (i < argument.size() && argument[i] == L'\\')
            {
                ++i;
                ++backslashes;
            }

            if (i == argument.size())
            {
                out.append(backslashes * 2, L'\\');
                break;
            }

            if (argument[i] == L'"')
            {
                out.append(backslashes * 2 + 1, L'\\');
                out += L'"';
            }
            else
            {
                out.append(backslashes, L'\\');
                out += argument[i];
            }
        }

        return out + L"\"";
    }
#endif

    // Runs `git <arguments>` in the project folder, waits, returns the output.
    static ProcessResult RunGit(const std::vector<std::string>& arguments)
    {
        ProcessResult result;

        // Paths with accents as UTF-8, never a pager, never a terminal prompt
        // (there is no terminal : it would wait forever).
        std::vector<std::string> full = { "git", "-c", "core.quotepath=false", "--no-pager" };
        full.insert(full.end(), arguments.begin(), arguments.end());

#ifdef _WIN32
        std::wstring command_line;

        for (const std::string& argument : full)
        {
            if (!command_line.empty())
                command_line += L' ';
            command_line += QuoteArgument(Wide(argument));
        }

        SECURITY_ATTRIBUTES inherit{};
        inherit.nLength = sizeof(inherit);
        inherit.bInheritHandle = TRUE;

        HANDLE read_pipe = nullptr;
        HANDLE write_pipe = nullptr;

        if (!CreatePipe(&read_pipe, &write_pipe, &inherit, 0))
        {
            result.output = "CreatePipe failed";
            return result;
        }

        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &inherit, OPEN_EXISTING, 0, nullptr);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = write_pipe;
        startup.hStdError = write_pipe;

        SetEnvironmentVariableW(L"GIT_TERMINAL_PROMPT", L"0");

        PROCESS_INFORMATION info{};
        const std::wstring directory = stdfs::current_path().wstring();

        const BOOL created = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &info);

        CloseHandle(write_pipe);

        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);

        if (!created)
        {
            CloseHandle(read_pipe);
            result.output = "Could not start git (is Git installed and in the PATH ?)";
            return result;
        }

        result.started = true;
        CloseHandle(info.hThread);

        char buffer[4096];
        DWORD read = 0;

        while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
            result.output.append(buffer, read);

        CloseHandle(read_pipe);
        WaitForSingleObject(info.hProcess, INFINITE);

        DWORD exit_code = 1;
        GetExitCodeProcess(info.hProcess, &exit_code);
        CloseHandle(info.hProcess);
        result.exit_code = static_cast<int>(exit_code);
#else
        std::string command = "GIT_TERMINAL_PROMPT=0";

        for (const std::string& argument : full)
        {
            std::string quoted = "'";
            for (const char c : argument)
                quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
            command += " " + quoted + "'";
        }

        command += " </dev/null 2>&1";

        FILE* pipe = popen(command.c_str(), "r");

        if (!pipe)
        {
            result.output = "Could not start git";
            return result;
        }

        char buffer[4096];
        size_t read = 0;

        while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0)
            result.output.append(buffer, read);

        const int status = pclose(pipe);
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        result.started = result.exit_code != 127;
#endif

        return result;
    }


    // -------------------------------------------------------------------------
    // Parsing
    // -------------------------------------------------------------------------

    static std::vector<std::string> SplitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        size_t start = 0;

        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();

            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            lines.push_back(std::move(line));
            start = end + 1;
        }

        return lines;
    }

    static std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    // "## main...origin/main [ahead 1, behind 2]"
    static void ParseBranchLine(const std::string& line, RepositoryState& repo)
    {
        std::string text = line.substr(3);

        const std::string no_commits = "No commits yet on ";
        if (text.rfind(no_commits, 0) == 0)
        {
            repo.no_commits = true;
            repo.branch = text.substr(no_commits.size());
            return;
        }

        const size_t bracket = text.find(" [");
        std::string counts;

        if (bracket != std::string::npos)
        {
            counts = text.substr(bracket + 2);
            text = text.substr(0, bracket);
        }

        const size_t dots = text.find("...");
        repo.branch = dots == std::string::npos ? text : text.substr(0, dots);
        repo.upstream = dots == std::string::npos ? std::string() : text.substr(dots + 3);

        const auto number_after = [&](const char* word) -> int
        {
            const size_t at = counts.find(word);
            return at == std::string::npos ? 0 : std::atoi(counts.c_str() + at + std::strlen(word));
        };

        repo.ahead = number_after("ahead ");
        repo.behind = number_after("behind ");
    }

    // git status --porcelain=v1 -b -z
    static void ParseStatus(const std::string& output, RepositoryState& repo)
    {
        std::vector<std::string> parts;
        size_t start = 0;

        while (start < output.size())
        {
            size_t end = output.find('\0', start);
            if (end == std::string::npos)
                end = output.size();
            parts.push_back(output.substr(start, end - start));
            start = end + 1;
        }

        for (size_t i = 0; i < parts.size(); ++i)
        {
            const std::string& part = parts[i];

            if (part.rfind("## ", 0) == 0)
            {
                ParseBranchLine(part, repo);
                continue;
            }

            if (part.size() < 4)
                continue;

            FileChange change;
            change.index = part[0];
            change.worktree = part[1];
            change.path = part.substr(3);

            // Rename / copy : the next part is the old path.
            if ((change.index == 'R' || change.index == 'C') && i + 1 < parts.size())
                change.original = parts[++i];

            repo.changes.push_back(std::move(change));
        }
    }

    static RepositoryState ReadRepository()
    {
        RepositoryState repo;

        const ProcessResult version = RunGit({ "--version" });
        repo.git_found = version.started && version.exit_code == 0;
        repo.git_version = Trim(version.output);

        if (!repo.git_found)
            return repo;

        const ProcessResult inside = RunGit({ "rev-parse", "--is-inside-work-tree" });
        repo.is_repository = inside.exit_code == 0 && Trim(inside.output) == "true";

        if (!repo.is_repository)
            return repo;

        ParseStatus(RunGit({ "status", "--porcelain=v1", "-b", "-z", "--untracked-files=all" }).output, repo);

        for (const std::string& line : SplitLines(RunGit({ "branch", "--format=%(refname:short)" }).output))
            if (!Trim(line).empty())
                repo.branches.push_back(Trim(line));

        if (!repo.no_commits)
        {
            const ProcessResult log = RunGit({ "log", "-n", std::to_string(kHistoryCount),
                                               "--pretty=format:%h%x1f%an%x1f%ar%x1f%s" });

            for (const std::string& line : SplitLines(log.output))
            {
                std::vector<std::string> fields;
                size_t from = 0;

                for (int f = 0; f < 3; ++f)
                {
                    const size_t sep = line.find('\x1f', from);
                    if (sep == std::string::npos)
                        break;
                    fields.push_back(line.substr(from, sep - from));
                    from = sep + 1;
                }

                if (fields.size() == 3)
                    repo.history.push_back({ fields[0], fields[1], fields[2], line.substr(from) });
            }
        }

        repo.remote_url = Trim(RunGit({ "remote", "get-url", "origin" }).output);
        if (repo.remote_url.find("error") != std::string::npos || repo.remote_url.find("fatal") != std::string::npos)
            repo.remote_url.clear();

        repo.user_name = Trim(RunGit({ "config", "user.name" }).output);
        repo.user_email = Trim(RunGit({ "config", "user.email" }).output);
        return repo;
    }


    // -------------------------------------------------------------------------
    // Worker
    // -------------------------------------------------------------------------

    static void AppendLog(const std::string& text)
    {
        state.log += text;

        if (!text.empty() && text.back() != '\n')
            state.log += '\n';

        if (state.log.size() > kMaxLog)
            state.log.erase(0, state.log.size() - kMaxLog);

        state.log_scroll = true;
    }

    static std::string CommandText(const std::vector<std::string>& arguments)
    {
        std::string text = "> git";

        for (const std::string& argument : arguments)
            text += " " + (argument.find(' ') != std::string::npos ? "\"" + argument + "\"" : argument);

        return text;
    }

    static void JoinWorker()
    {
        if (state.worker.joinable())
            state.worker.join();
    }

    // Runs the commands one after the other (stops at the first failure),
    // then reads the repository again. `on_success` runs on the worker.
    // A command asked while the worker runs (refresh...) waits its turn.
    static bool StartOrQueue(std::function<void()> start)
    {
        if (state.worker_running)
        {
            state.queued = std::move(start);
            return false;
        }

        start();
        return true;
    }

    static void StartJob(const std::string& label,
                         std::vector<std::vector<std::string>> commands,
                         std::function<void()> on_success,
                         bool user)
    {
        JoinWorker();
        state.worker_running = true;
        state.busy = user;
        state.busy_label = label;
        state.done = false;

        state.worker = std::thread([commands = std::move(commands), on_success = std::move(on_success)]()
        {
            bool ok = true;

            for (const auto& arguments : commands)
            {
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    AppendLog(CommandText(arguments));
                }

                const ProcessResult result = RunGit(arguments);

                std::lock_guard<std::mutex> lock(state.mutex);

                if (!result.output.empty())
                    AppendLog(result.output);

                if (result.exit_code != 0)
                {
                    AppendLog("(failed, exit code " + std::to_string(result.exit_code) + ")");
                    ok = false;
                    break;
                }
            }

            if (ok && on_success)
                on_success();

            RepositoryState repo = ReadRepository();

            std::lock_guard<std::mutex> lock(state.mutex);
            state.repo = std::move(repo);
            state.done = true;
        });
    }

    static void RunJob(const std::string& label,
                       std::vector<std::vector<std::string>> commands,
                       std::function<void()> on_success = {})
    {
        if (state.busy)
            return;

        state.busy = true;   // no second click while queued
        state.busy_label = label;

        StartOrQueue([label, commands = std::move(commands), on_success = std::move(on_success)]() mutable
        {
            StartJob(label, std::move(commands), std::move(on_success), true);
        });
    }

    // Background status read : does not disable the buttons.
    static void Refresh()
    {
        state.last_refresh = GitClock::now();
        state.refreshed_once = true;

        if (state.worker_running)
            return;

        StartJob("Refreshing", {}, {}, false);
    }

    static std::vector<size_t> LineStarts(const std::string& text)
    {
        std::vector<size_t> starts;
        if (text.empty())
            return starts;

        starts.push_back(0);
        for (size_t i = 0; i < text.size(); ++i)
            if (text[i] == '\n' && i + 1 < text.size())
                starts.push_back(i + 1);
        return starts;
    }

    // Diff / commit details in the right pane (worker).
    static void ShowDetails(const std::string& title, std::vector<std::string> arguments, bool is_diff,
                            const std::string& fallback_file = {})
    {
        if (state.busy)
            return;

        StartOrQueue([title, arguments = std::move(arguments), is_diff, fallback_file]() mutable
        {
        JoinWorker();
        state.worker_running = true;
        state.busy = false;
        state.done = false;

        state.worker = std::thread([title, arguments = std::move(arguments), is_diff, fallback_file]()
        {
            std::string text = arguments.empty() ? std::string() : RunGit(arguments).output;

            // Untracked file : no diff, show its beginning.
            if (Trim(text).empty() && !fallback_file.empty())
            {
                std::ifstream file(fallback_file, std::ios::binary);
                std::string content{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

                if (content.find('\0') != std::string::npos)
                {
                    text = "(new binary file)";
                }
                else
                {
                    text = "(new file)\n";
                    for (const std::string& line : SplitLines(content))
                        text += "+" + line + "\n";
                }
            }

            if (text.size() > kMaxDetails)
                text = text.substr(0, kMaxDetails) + "\n... (truncated)";

            if (text.empty())
                text = "(no difference)";

            std::vector<size_t> lines = LineStarts(text);

            std::lock_guard<std::mutex> lock(state.mutex);
            state.details_title = title;
            state.details = std::move(text);
            state.details_lines = std::move(lines);
            state.details_is_diff = is_diff;
            state.done = true;
        });
        });
    }

    static const char* kDefaultGitignore =
        "# Lynx project\n"
        "build/\n"
        ".lynx/\n"
        "\n"
        "# Editor layout and settings (per user)\n"
        "imgui.ini\n"
        "editor_windows.txt\n"
        "editor_camera.txt\n"
        "\n"
        "# Python\n"
        "__pycache__/\n"
        "*.pyc\n"
        "\n"
        "# IDE\n"
        ".vs/\n"
        ".idea/\n"
        "cmake-build-*/\n"
        "*.user\n";

    static bool WriteDefaultGitignore()
    {
        std::error_code error;

        if (stdfs::exists(".gitignore", error))
            return false;

        std::ofstream file(".gitignore", std::ios::binary);
        file << kDefaultGitignore;
        return static_cast<bool>(file);
    }


    // -------------------------------------------------------------------------
    // UI
    // -------------------------------------------------------------------------

    struct GitColor { float r, g, b; };

    static GitColor StatusColor(const FileChange& change)
    {
        if (change.Conflict())          return { 1.00f, 0.35f, 0.35f };
        if (change.Untracked())         return { 0.55f, 0.85f, 0.55f };

        const char c = change.Staged() ? change.index : change.worktree;

        if (c == 'D')                   return { 0.95f, 0.50f, 0.45f };
        if (c == 'A')                   return { 0.55f, 0.85f, 0.55f };
        if (c == 'R' || c == 'C')       return { 0.60f, 0.75f, 1.00f };
        return { 0.95f, 0.80f, 0.40f };
    }

    static const char* StatusWord(const FileChange& change)
    {
        if (change.Conflict())  return "conflict";
        if (change.Untracked()) return "new";

        switch (change.Staged() ? change.index : change.worktree)
        {
        case 'M': return "modified";
        case 'A': return "added";
        case 'D': return "deleted";
        case 'R': return "renamed";
        case 'C': return "copied";
        case 'T': return "type";
        default:  return "changed";
        }
    }



    // -------------------------------------------------------------------------
    // Interface (Raven : windows/git.xml)
    // -------------------------------------------------------------------------

    namespace view
    {
        static Rid window = R_INVALID_ID;
        static bool shown = false;
        static Rid loading, not_found, not_repo, main, path_label;
        static Rid branch, fetch, pull, push, sync_label, busy_label, tabs;
        static Rid files, files_count, stage_all, unstage_all, unsaved, message, commit, amend, commit_all;
        static Rid details_title, details, history, history_title, history_details;
        static Rid remote, user_name, user_email, version_label, gitignore_label, gitignore_button, log;
        static std::unordered_map<Rid, std::string> file_items;     // item -> path
        static std::unordered_map<Rid, std::string> commit_items;   // item -> hash
        static std::vector<std::string> branch_names;
        static std::string files_signature = "\x01";
        static std::string history_signature = "\x01";
        static std::string branches_signature = "\x01";
        static std::string details_shown = "\x01";
        static size_t log_shown = 0;
        static std::unique_ptr<lynx::editor::ui::Menu> menu;
        static std::function<void()> save_level;
    }

    namespace gui = lynx::editor::ui;

    static void FillDetails(Rid tree)
    {
        Rtree_view_clear_items(tree);
        const std::string& text = state.details;
        for (size_t start : state.details_lines)
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            if (end > start && text[end - 1] == '\r')
                --end;
            const std::string line = text.substr(start, end - start);
            const Rid item = Rtree_view_add_item(tree, R_INVALID_ID, line.empty() ? " " : line.c_str());
            if (state.details_is_diff && !line.empty())
            {
                if (line[0] == '+' && line.rfind("+++", 0) != 0)
                    Rtree_item_set_text_color(item, 0.50f, 0.85f, 0.50f, 1.f);
                else if (line[0] == '-' && line.rfind("---", 0) != 0)
                    Rtree_item_set_text_color(item, 0.95f, 0.50f, 0.50f, 1.f);
                else if (line[0] == '@')
                    Rtree_item_set_text_color(item, 0.55f, 0.75f, 1.00f, 1.f);
                else if (line.rfind("diff ", 0) == 0 || line.rfind("index ", 0) == 0)
                    Rtree_item_set_text_color(item, 0.60f, 0.60f, 0.60f, 1.f);
            }
        }
    }

    static void ToggleStage(const RepositoryState& repo, const FileChange& change)
    {
        const bool is_staged = change.Staged() && !change.Unstaged();
        const bool partly = change.Staged() && change.Unstaged();
        if (!is_staged || partly)
            RunJob("Staging", { { "add", "-A", "--", change.path } });
        else if (repo.no_commits)
            RunJob("Unstaging", { { "rm", "--cached", "-q", "--", change.path } });
        else
            RunJob("Unstaging", { { "restore", "--staged", "--", change.path } });
    }

    static void SelectFile(const FileChange& change)
    {
        state.selected_file = change.path;
        if (change.Untracked())
            ShowDetails(change.path + "  (new)", {}, true, change.path);
        else if (change.Staged() && !change.Unstaged())
            ShowDetails(change.path + "  (staged)", { "diff", "--cached", "--", change.path }, true);
        else
            ShowDetails(change.path, { "diff", "--", change.path }, true);
    }

    static RepositoryState CopyRepo()
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.repo;
    }

    static const FileChange* FindChange(const RepositoryState& repo, const std::string& path)
    {
        for (const FileChange& c : repo.changes)
            if (c.path == path)
                return &c;
        return nullptr;
    }

    static void WriteMessageFile(const std::string& message)
    {
        std::error_code error;
        stdfs::create_directories(".lynx", error);
        std::ofstream file(".lynx/git_commit_message.txt", std::ios::binary | std::ios::trunc);
        file << message;
    }

    static std::string MessageText()
    {
        const char* text = Rtext_editor_get_text(view::message);
        return text ? text : "";
    }

    static void DiscardDialog(const FileChange& change)
    {
        const std::string target = change.path;
        const bool untracked = change.Untracked() || change.index == 'A';
        gui::Ask(view::window, "Discard changes?", target + "\n\n" +
                 (untracked ? "This new file will be DELETED (it was never committed)."
                            : "The file goes back to its last committed version."),
                 { "Discard", "Cancel" }, [target, untracked](int choice)
        {
            if (choice != 0)
                return;
            if (untracked)
                RunJob("Discarding", { { "rm", "--cached", "-q", "--ignore-unmatch", "--", target },
                                       { "clean", "-f", "-q", "--", target } });
            else
                RunJob("Discarding", { { "restore", "--staged", "--worktree", "--source=HEAD", "--", target } });
        });
    }

    static void NewBranchDialog()
    {
        gui::AskText(view::window, "New branch", "Created from the current branch, then checked out.", "",
            [](const std::string& typed)
            {
                const std::string name = Trim(typed);
                RunJob("Creating the branch", { { "switch", "-c", name } });
            },
            [](const std::string& typed) -> std::string
            {
                const std::string name = Trim(typed);
                if (name.empty())
                    return "Type a name.";
                if (name.find_first_of(" ~^:?*[\\") != std::string::npos)
                    return "No space nor ~ ^ : ? * [ \\ in a branch name.";
                return "";
            });
    }

    static int OnGitWindowClose(Rid window, void*)
    {
        Rwindow_hide(window);
        view::shown = false;
        return R_FALSE;
    }

    static void CreateView()
    {
        using namespace view;
        if (window != R_INVALID_ID)
            return;
        window = gui::LoadWindow("windows/git.xml");
        if (window == R_INVALID_ID)
            return;
        auto find = [](const char* n) { return gui::Find(view::window, n); };
        loading = find("git.loading");
        not_found = find("git.not_found");
        not_repo = find("git.not_repo");
        main = find("git.main");
        path_label = find("git.path");
        branch = find("git.branch");
        fetch = find("git.fetch");
        pull = find("git.pull");
        push = find("git.push");
        sync_label = find("git.sync");
        busy_label = find("git.busy");
        tabs = find("git.tabs");
        files = find("git.files");
        files_count = find("git.files.count");
        stage_all = find("git.stage_all");
        unstage_all = find("git.unstage_all");
        unsaved = find("git.unsaved");
        message = find("git.message");
        commit = find("git.commit");
        amend = find("git.amend");
        commit_all = find("git.commit_all");
        details_title = find("git.details.title");
        details = find("git.details");
        history = find("git.history");
        history_title = find("git.history.title");
        history_details = find("git.history.details");
        remote = find("git.remote");
        user_name = find("git.user_name");
        user_email = find("git.user_email");
        version_label = find("git.version");
        gitignore_label = find("git.gitignore");
        gitignore_button = find("git.gitignore.create");
        log = find("git.log");

        Rtext_editor_set_placeholder(message, "Commit message");
        Rtext_editor_set_word_wrap(message, R_TRUE);
        Rtext_editor_set_line_numbers(message, R_FALSE);

        gui::OnClick(find("git.retry"), []() { Refresh(); });
        gui::OnClick(find("git.init"), []()
        {
            RunJob("Initializing", { { "init", "-b", "main" } }, []() { WriteDefaultGitignore(); });
            state.settings_loaded = false;
        });
        gui::OnSelect(branch, [](int index)
        {
            if (index < 0 || index >= static_cast<int>(view::branch_names.size()))
                return;
            const std::string name = view::branch_names[static_cast<size_t>(index)];
            const RepositoryState repo = CopyRepo();
            if (name != repo.branch)
                RunJob("Switching branch", { { "switch", name } });
        });
        gui::OnClick(find("git.new_branch"), []() { NewBranchDialog(); });
        gui::OnClick(fetch, []() { RunJob("Fetching", { { "fetch", "--prune" } }); });
        gui::OnClick(pull, []() { RunJob("Pulling", { { "pull", "--ff-only" } }); });
        gui::OnClick(push, []()
        {
            const RepositoryState repo = CopyRepo();
            if (repo.no_commits)
                return;
            if (repo.upstream.empty())
                RunJob("Pushing", { { "push", "-u", "origin", repo.branch } });
            else
                RunJob("Pushing", { { "push" } });
        });

        gui::OnClick(stage_all, []() { RunJob("Staging", { { "add", "-A" } }); });
        gui::OnClick(unstage_all, []()
        {
            if (CopyRepo().no_commits)
                RunJob("Unstaging", { { "rm", "-r", "--cached", "-q", "--ignore-unmatch", "." } });
            else
                RunJob("Unstaging", { { "reset", "-q" } });
        });
        gui::OnClick(find("git.save_level"), []() { if (view::save_level) view::save_level(); });
        gui::OnTreeSelect(files, [](Rid item)
        {
            auto it = view::file_items.find(item);
            if (it == view::file_items.end())
                return;
            const RepositoryState repo = CopyRepo();
            if (const FileChange* change = FindChange(repo, it->second))
                SelectFile(*change);
        });
        // Double click : stage / unstage.
        gui::OnTreeActivate(files, [](Rid item)
        {
            auto it = view::file_items.find(item);
            if (it == view::file_items.end() || state.busy)
                return;
            const RepositoryState repo = CopyRepo();
            if (const FileChange* change = FindChange(repo, it->second))
                ToggleStage(repo, *change);
        });
        gui::OnTreeContext(files, [](Rid item, float, float)
        {
            auto it = view::file_items.find(item);
            if (it == view::file_items.end())
                return;
            const RepositoryState repo = CopyRepo();
            const FileChange* found = FindChange(repo, it->second);
            if (!found)
                return;
            const FileChange change = *found;
            const bool no_commits = repo.no_commits;
            view::menu = std::make_unique<gui::Menu>(view::window);
            view::menu->Item("Stage", [change]() { RunJob("Staging", { { "add", "-A", "--", change.path } }); },
                             change.Unstaged() && !state.busy);
            view::menu->Item("Unstage", [change]() { RunJob("Unstaging", { { "restore", "--staged", "--", change.path } }); },
                             change.Staged() && !state.busy && !no_commits);
            view::menu->Item("Staged diff", [change]()
            {
                ShowDetails(change.path + "  (staged)", { "diff", "--cached", "--", change.path }, true);
            }, change.Staged() && !state.busy);
            view::menu->Separator();
            view::menu->Item("Discard changes...", [change]() { DiscardDialog(change); }, !state.busy && !change.Conflict());
            view::menu->Item("Copy path", [change]() { Rclipboard_set_text(change.path.c_str()); });
            view::menu->Open();
        });

        auto do_commit = [](bool stage_everything)
        {
            const std::string text = MessageText();
            if (Trim(text).empty())
                return;
            WriteMessageFile(text);
            if (stage_everything)
                RunJob("Committing", { { "add", "-A" }, { "commit", "-F", ".lynx/git_commit_message.txt" } });
            else
            {
                std::vector<std::string> args = { "commit", "-F", ".lynx/git_commit_message.txt" };
                if (state.amend)
                    args.push_back("--amend");
                RunJob("Committing", { args });
            }
            Rtext_editor_set_text(view::message, "");
            state.amend = false;
            gui::SetChecked(view::amend, false);
        };
        gui::OnClick(commit, [do_commit]() { do_commit(false); });
        gui::OnClick(commit_all, [do_commit]() { do_commit(true); });
        gui::OnToggle(amend, [](bool on) { state.amend = on; });

        gui::OnTreeSelect(history, [](Rid item)
        {
            auto it = view::commit_items.find(item);
            if (it == view::commit_items.end())
                return;
            const RepositoryState repo = CopyRepo();
            for (const CommitEntry& c : repo.history)
                if (c.hash == it->second)
                    ShowDetails(c.hash + "  " + c.subject,
                                { "show", "--stat", "--patch", "--format=%H%nAuthor : %an <%ae>%nDate   : %ad%n%n%B", c.hash }, true);
        });
        gui::OnTreeContext(history, [](Rid item, float, float)
        {
            auto it = view::commit_items.find(item);
            if (it == view::commit_items.end())
                return;
            const std::string hash = it->second;
            view::menu = std::make_unique<gui::Menu>(view::window);
            view::menu->Item("Copy hash", [hash]() { Rclipboard_set_text(hash.c_str()); });
            view::menu->Open();
        });

        gui::OnClick(find("git.remote.set"), []()
        {
            const char* text = Rtext_input_get_text(view::remote);
            const std::string url = Trim(text ? text : "");
            if (url.empty())
                return;
            RunJob("Setting the remote", { { "remote", CopyRepo().remote_url.empty() ? "add" : "set-url", "origin", url } });
            state.settings_loaded = false;
        });
        gui::OnClick(find("git.user.set"), []()
        {
            const char* n = Rtext_input_get_text(view::user_name);
            const char* e = Rtext_input_get_text(view::user_email);
            const std::string name = Trim(n ? n : ""), email = Trim(e ? e : "");
            if (name.empty() || email.empty())
                return;
            RunJob("Setting the author", { { "config", "user.name", name }, { "config", "user.email", email } });
            state.settings_loaded = false;
        });
        gui::OnClick(gitignore_button, []()
        {
            WriteDefaultGitignore();
            Refresh();
        });
        Rwindow_set_close_callback(window, OnGitWindowClose, nullptr);
    }

    static void UpdateView(const RepositoryState& repo, bool level_unsaved)
    {
        using namespace view;
        const bool reading = !state.refreshed_once || (!repo.git_found && state.worker_running);
        gui::SetVisible(loading, reading);
        gui::SetVisible(not_found, !reading && !repo.git_found);
        gui::SetVisible(not_repo, !reading && repo.git_found && !repo.is_repository);
        gui::SetVisible(main, !reading && repo.git_found && repo.is_repository);
        gui::SetText(path_label, stdfs::current_path().generic_string());
        if (reading || !repo.git_found || !repo.is_repository)
            return;

        const bool busy = state.busy;
        const bool has_remote = !repo.remote_url.empty();

        // Branches.
        std::string bs = repo.branch + "|";
        for (const std::string& b : repo.branches)
            bs += b + "|";
        if (bs != branches_signature)
        {
            branches_signature = bs;
            branch_names = repo.branches;
            if (std::find(branch_names.begin(), branch_names.end(), repo.branch) == branch_names.end())
                branch_names.insert(branch_names.begin(), repo.branch.empty() ? "(detached)" : repo.branch);
            Rcombo_box_clear_items(branch);
            int current = 0;
            for (size_t i = 0; i < branch_names.size(); ++i)
            {
                Rcombo_box_add_item(branch, branch_names[i].c_str());
                if (branch_names[i] == repo.branch)
                    current = static_cast<int>(i);
            }
            Rcombo_box_set_selected(branch, current);
        }
        gui::SetEnabled(branch, !busy);
        gui::SetEnabled(fetch, !busy && has_remote);
        gui::SetEnabled(pull, !busy && has_remote);
        gui::SetEnabled(push, !busy && has_remote && !repo.no_commits);
        gui::SetText(pull, repo.behind > 0 ? "Pull (" + std::to_string(repo.behind) + ")" : std::string("Pull"));
        gui::SetText(push, repo.ahead > 0 ? "Push (" + std::to_string(repo.ahead) + ")" : std::string("Push"));
        std::string sync;
        if (!has_remote)
            sync = "no remote (Settings tab)";
        else if (repo.upstream.empty())
            sync = "not pushed yet";
        else if (repo.ahead == 0 && repo.behind == 0)
            sync = "up to date with " + repo.upstream;
        else
            sync = repo.upstream + " : " + std::to_string(repo.ahead) + " ahead, " + std::to_string(repo.behind) + " behind";
        gui::SetText(sync_label, sync);
        gui::SetText(busy_label, busy ? state.busy_label + "..." : std::string());

        // Files.
        int staged = 0;
        std::string fs_sig;
        for (const FileChange& c : repo.changes)
        {
            staged += c.Staged() ? 1 : 0;
            fs_sig += c.index;
            fs_sig += c.worktree;
            fs_sig += c.path + "|";
        }
        if (fs_sig != files_signature)
        {
            files_signature = fs_sig;
            Rtree_view_clear_items(files);
            file_items.clear();
            for (const FileChange& c : repo.changes)
            {
                const bool is_staged = c.Staged() && !c.Unstaged();
                const bool partly = c.Staged() && c.Unstaged();
                char word[16];
                std::snprintf(word, sizeof(word), "%-9s", StatusWord(c));
                const std::string label = std::string(is_staged ? "[x] " : partly ? "[-] " : "[ ] ") + word +
                                          (c.original.empty() ? c.path : c.original + " -> " + c.path);
                const Rid item = Rtree_view_add_item(files, R_INVALID_ID, label.c_str());
                const GitColor color = StatusColor(c);
                Rtree_item_set_text_color(item, color.r, color.g, color.b, 1.f);
                file_items[item] = c.path;
                if (c.path == state.selected_file)
                    Rtree_view_set_selected_item(files, item);
            }

        }
        char count[96];
        std::snprintf(count, sizeof(count), "%d changed file(s), %d staged   (double click : stage / unstage)",
                      static_cast<int>(repo.changes.size()), staged);
        gui::SetText(files_count, repo.changes.empty() ? "Nothing to commit, the folder is clean." : count);
        gui::SetEnabled(stage_all, !busy && !repo.changes.empty());
        gui::SetEnabled(unstage_all, !busy && !repo.changes.empty());
        gui::SetVisible(unsaved, level_unsaved);

        const bool has_message = !Trim(MessageText()).empty();
        gui::SetEnabled(commit, !busy && has_message && (staged > 0 || state.amend));
        gui::SetText(commit, state.amend ? "Amend last commit" : "Commit");
        gui::SetTooltip(commit, !has_message ? "Write a commit message." :
                                (staged == 0 && !state.amend) ? "Stage files first (double click on a file)." : "");
        gui::SetEnabled(amend, !repo.no_commits);
        gui::SetEnabled(commit_all, !busy && !repo.changes.empty() && has_message);

        // History.
        std::string hs;
        for (const CommitEntry& c : repo.history)
            hs += c.hash;
        if (hs != history_signature)
        {
            history_signature = hs;
            Rtree_view_clear_items(history);
            commit_items.clear();
            for (const CommitEntry& c : repo.history)
            {
                const Rid item = Rtree_view_add_item(history, R_INVALID_ID, c.subject.c_str());
                const std::string sub = c.hash + "  -  " + c.author + "  -  " + c.when;
                const Rid detail = Rtree_view_add_item(history, item, sub.c_str());
                Rtree_item_set_text_color(detail, 0.6f, 0.6f, 0.62f, 1.f);
                commit_items[item] = c.hash;
                commit_items[detail] = c.hash;
            }
            if (repo.history.empty())
                Rtree_view_add_item(history, R_INVALID_ID, repo.no_commits ? "No commit yet." : "No history.");
        }

        // Details (diff or commit) : both tabs show them.
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            const std::string sig = state.details_title + "#" + std::to_string(state.details.size()) + "#" +
                                    std::to_string(state.details.empty() ? 0 : static_cast<unsigned char>(state.details[0]));
            if (sig != details_shown)
            {
                details_shown = sig;
                const std::string title = state.details_title.empty() ? "Select a file or a commit" : state.details_title;
                gui::SetText(details_title, title);
                gui::SetText(history_title, title);
                FillDetails(details);
                FillDetails(history_details);
            }
            // Output of the commands.
            if (state.log.size() < log_shown)
            {
                Rtree_view_clear_items(log);
                log_shown = 0;
            }
            if (state.log.size() > log_shown)
            {
                const std::string fresh = state.log.substr(log_shown);
                log_shown = state.log.size();
                Rid last = R_INVALID_ID;
                for (const std::string& line : SplitLines(fresh))
                    last = Rtree_view_add_item(log, R_INVALID_ID, line.empty() ? " " : line.c_str());
                if (last != R_INVALID_ID)
                    Rtree_view_scroll_to_item(log, last);
            }
        }

        // Settings.
        if (!state.settings_loaded)
        {
            Rtext_input_set_text(remote, repo.remote_url.c_str());
            Rtext_input_set_text(user_name, repo.user_name.c_str());
            Rtext_input_set_text(user_email, repo.user_email.c_str());
            state.settings_loaded = true;
        }
        gui::SetText(version_label, repo.git_version);
        std::error_code error;
        const bool has_gitignore = stdfs::exists(".gitignore", error);
        gui::SetText(gitignore_label, has_gitignore ? ".gitignore (present)"
                                                    : ".gitignore (missing : build/ and .lynx/ would be committed)");
        gui::SetEnabled(gitignore_button, !has_gitignore);
    }


    // -------------------------------------------------------------------------
    // Public
    // -------------------------------------------------------------------------

    // Once per frame (even when the window is closed : ends the jobs).
    static void Update()
    {
        bool completed = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.done)
            {
                completed = true;
                state.done = false;
            }
        }

        if (completed)
        {
            JoinWorker();
            state.worker_running = false;
            state.busy = false;

            if (state.queued)
            {
                auto next = std::move(state.queued);
                state.queued = nullptr;
                next();
            }
        }
    }

    static void Show()
    {
        CreateView();
        if (view::window == R_INVALID_ID)
            return;
        Rwindow_show(view::window);
        view::shown = true;
    }

    static bool IsOpen()
    {
        return view::shown;
    }

    // Every frame while the window is open. `level_unsaved` / `save_level` :
    // the commit area offers to save the level first.
    static void Frame(bool level_unsaved, const std::function<void()>& save_level)
    {
        if (!view::shown)
            return;
        view::save_level = save_level;

        // Status : at the first frame, then every 4 s while visible.
        if (!state.worker_running &&
            (!state.refreshed_once || GitClock::now() - state.last_refresh > std::chrono::seconds(4)))
            Refresh();

        UpdateView(CopyRepo(), level_unsaved);
    }

    static void Shutdown()
    {
        JoinWorker();
    }
}
