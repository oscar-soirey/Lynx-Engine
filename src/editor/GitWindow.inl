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

    static ImVec4 StatusColor(const FileChange& change)
    {
        if (change.Conflict())          return ImVec4(1.00f, 0.35f, 0.35f, 1.f);
        if (change.Untracked())         return ImVec4(0.55f, 0.85f, 0.55f, 1.f);

        const char c = change.Staged() ? change.index : change.worktree;

        if (c == 'D')                   return ImVec4(0.95f, 0.50f, 0.45f, 1.f);
        if (c == 'A')                   return ImVec4(0.55f, 0.85f, 0.55f, 1.f);
        if (c == 'R' || c == 'C')       return ImVec4(0.60f, 0.75f, 1.00f, 1.f);
        return ImVec4(0.95f, 0.80f, 0.40f, 1.f);
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

    static void DrawDetails()
    {
        std::lock_guard<std::mutex> lock(state.mutex);

        ImGui::TextUnformatted(state.details_title.empty() ? "Select a file or a commit" : state.details_title.c_str());
        ImGui::BeginChild("##GitDetails", ImVec2(0.f, 0.f), true, ImGuiWindowFlags_HorizontalScrollbar);

        const std::string& text = state.details;
        const std::vector<size_t>& lines = state.details_lines;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()));

        while (clipper.Step())
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
        {
            const size_t start = lines[static_cast<size_t>(line)];
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            if (end > start && text[end - 1] == '\r')
                --end;

            const char* begin = text.c_str() + start;
            const char* finish = text.c_str() + end;
            ImVec4 color(0.85f, 0.85f, 0.85f, 1.f);

            if (state.details_is_diff && end > start)
            {
                if (*begin == '+' && !(end - start >= 3 && std::strncmp(begin, "+++", 3) == 0))
                    color = ImVec4(0.50f, 0.85f, 0.50f, 1.f);
                else if (*begin == '-' && !(end - start >= 3 && std::strncmp(begin, "---", 3) == 0))
                    color = ImVec4(0.95f, 0.50f, 0.50f, 1.f);
                else if (*begin == '@')
                    color = ImVec4(0.55f, 0.75f, 1.00f, 1.f);
                else if (std::strncmp(begin, "diff ", 5) == 0 || std::strncmp(begin, "index ", 6) == 0)
                    color = ImVec4(0.60f, 0.60f, 0.60f, 1.f);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(begin, finish);
            ImGui::PopStyleColor();
        }

        ImGui::EndChild();
    }

    static void DrawChanges(const RepositoryState& repo, bool level_unsaved, const std::function<void()>& save_level)
    {
        // --- Left : files + commit ----------------------------------------
        ImGui::BeginChild("##GitLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.45f, 0.f), false);

        int staged = 0;
        for (const FileChange& change : repo.changes)
            staged += change.Staged() ? 1 : 0;

        ImGui::Text("%d changed file(s), %d staged", static_cast<int>(repo.changes.size()), staged);

        ImGui::BeginDisabled(state.busy || repo.changes.empty());
        if (ImGui::SmallButton("Stage all"))
            RunJob("Staging", { { "add", "-A" } });
        ImGui::SameLine();
        if (ImGui::SmallButton("Unstage all"))
        {
            if (repo.no_commits)
                RunJob("Unstaging", { { "rm", "-r", "--cached", "-q", "--ignore-unmatch", "." } });
            else
                RunJob("Unstaging", { { "reset", "-q" } });
        }
        ImGui::EndDisabled();

        const float commit_height = ImGui::GetTextLineHeightWithSpacing() * 6.5f;
        ImGui::BeginChild("##GitFiles", ImVec2(0.f, -commit_height), true);

        if (repo.changes.empty())
            ImGui::TextDisabled("Nothing to commit, the folder is clean.");

        for (const FileChange& change : repo.changes)
        {
            ImGui::PushID(change.path.c_str());

            bool is_staged = change.Staged() && !change.Unstaged();
            const bool partly = change.Staged() && change.Unstaged();

            ImGui::BeginDisabled(state.busy);
            if (partly)
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.95f, 0.80f, 0.40f, 1.f));

            if (ImGui::Checkbox("##stage", &is_staged))
            {
                if (is_staged || partly)
                    RunJob("Staging", { { "add", "-A", "--", change.path } });
                else if (repo.no_commits)
                    RunJob("Unstaging", { { "rm", "--cached", "-q", "--", change.path } });
                else
                    RunJob("Unstaging", { { "restore", "--staged", "--", change.path } });
            }

            if (partly)
                ImGui::PopStyleColor();
            ImGui::EndDisabled();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(partly ? "Partly staged : click to stage the rest" :
                                  is_staged ? "Staged : click to unstage" : "Click to stage");

            ImGui::SameLine();
            ImGui::TextColored(StatusColor(change), "%-8s", StatusWord(change));
            ImGui::SameLine();

            std::string label = change.path;
            if (!change.original.empty())
                label = change.original + " -> " + change.path;

            if (ImGui::Selectable(label.c_str(), state.selected_file == change.path))
            {
                state.selected_file = change.path;

                if (change.Untracked())
                    ShowDetails(change.path + "  (new)", {}, true, change.path);
                else if (change.Staged() && !change.Unstaged())
                    ShowDetails(change.path + "  (staged)", { "diff", "--cached", "--", change.path }, true);
                else
                    ShowDetails(change.path, { "diff", "--", change.path }, true);
            }

            if (ImGui::BeginPopupContextItem("##FileMenu"))
            {
                if (ImGui::MenuItem("Stage", nullptr, false, change.Unstaged() && !state.busy))
                    RunJob("Staging", { { "add", "-A", "--", change.path } });

                if (ImGui::MenuItem("Unstage", nullptr, false, change.Staged() && !state.busy && !repo.no_commits))
                    RunJob("Unstaging", { { "restore", "--staged", "--", change.path } });

                if (ImGui::MenuItem("Staged diff", nullptr, false, change.Staged() && !state.busy))
                    ShowDetails(change.path + "  (staged)", { "diff", "--cached", "--", change.path }, true);

                ImGui::Separator();

                if (ImGui::MenuItem("Discard changes...", nullptr, false, !state.busy && !change.Conflict()))
                {
                    state.discard_target = change.path;
                    state.discard_untracked = change.Untracked() || change.index == 'A';
                    state.open_discard_popup = true;
                }

                if (ImGui::MenuItem("Copy path"))
                    ImGui::SetClipboardText(change.path.c_str());

                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();

        // Commit.
        if (level_unsaved)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f), "The level has unsaved changes.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Save level"))
                save_level();
        }

        ImGui::InputTextMultiline("##CommitMessage", state.message.data(), state.message.size(),
                                  ImVec2(-1.f, ImGui::GetTextLineHeight() * 3.2f));

        if (state.message[0] == '\0' && !ImGui::IsItemActive())
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 6.f, min.y + 4.f),
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), "Commit message");
        }

        const bool has_message = Trim(state.message.data()) != "";
        const bool can_commit = !state.busy && has_message && (staged > 0 || state.amend);

        ImGui::BeginDisabled(!can_commit);
        if (ImGui::Button(state.amend ? "Amend last commit" : "Commit"))
        {
            // The message goes through a file : no quoting problem.
            std::error_code error;
            stdfs::create_directories(".lynx", error);
            {
                std::ofstream file(".lynx/git_commit_message.txt", std::ios::binary | std::ios::trunc);
                file << state.message.data();
            }

            std::vector<std::string> commit = { "commit", "-F", ".lynx/git_commit_message.txt" };
            if (state.amend)
                commit.push_back("--amend");

            RunJob("Committing", { commit });
            state.message[0] = '\0';
            state.amend = false;
        }
        ImGui::EndDisabled();

        if (!has_message && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Write a commit message.");
        else if (staged == 0 && !state.amend && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Stage files first (checkboxes).");

        ImGui::SameLine();
        ImGui::BeginDisabled(repo.no_commits);
        ImGui::Checkbox("Amend", &state.amend);
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(state.busy || repo.changes.empty() || !has_message);
        if (ImGui::Button("Stage all + Commit"))
        {
            std::error_code error;
            stdfs::create_directories(".lynx", error);
            {
                std::ofstream file(".lynx/git_commit_message.txt", std::ios::binary | std::ios::trunc);
                file << state.message.data();
            }

            RunJob("Committing", { { "add", "-A" }, { "commit", "-F", ".lynx/git_commit_message.txt" } });
            state.message[0] = '\0';
        }
        ImGui::EndDisabled();

        ImGui::EndChild();
        ImGui::SameLine();

        // --- Right : diff ------------------------------------------------------
        ImGui::BeginChild("##GitRight", ImVec2(0.f, 0.f), false);
        DrawDetails();
        ImGui::EndChild();
    }

    static void DrawHistory(const RepositoryState& repo)
    {
        ImGui::BeginChild("##GitHistory", ImVec2(ImGui::GetContentRegionAvail().x * 0.45f, 0.f), true);

        if (repo.history.empty())
            ImGui::TextDisabled(repo.no_commits ? "No commit yet." : "No history.");

        std::string shown;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            shown = state.details_title;
        }

        for (const CommitEntry& commit : repo.history)
        {
            ImGui::PushID(commit.hash.c_str());

            if (ImGui::Selectable("##commit", shown.rfind(commit.hash, 0) == 0, 0,
                                  ImVec2(0.f, ImGui::GetTextLineHeight() * 2.f + 2.f)))
            {
                ShowDetails(commit.hash + "  " + commit.subject,
                            { "show", "--stat", "--patch", "--format=%H%nAuthor : %an <%ae>%nDate   : %ad%n%n%B", commit.hash }, true);
            }

            if (ImGui::BeginPopupContextItem("##CommitMenu"))
            {
                if (ImGui::MenuItem("Copy hash"))
                    ImGui::SetClipboardText(commit.hash.c_str());
                ImGui::EndPopup();
            }

            ImGui::SameLine(6.f);
            ImGui::BeginGroup();
            ImGui::TextUnformatted(commit.subject.c_str());
            ImGui::TextDisabled("%s  ·  %s  ·  %s", commit.hash.c_str(), commit.author.c_str(), commit.when.c_str());
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##GitHistoryDetails", ImVec2(0.f, 0.f), false);
        DrawDetails();
        ImGui::EndChild();
    }

    static void DrawSettings(const RepositoryState& repo)
    {
        if (!state.settings_loaded)
        {
            std::snprintf(state.remote_url, sizeof(state.remote_url), "%s", repo.remote_url.c_str());
            std::snprintf(state.user_name, sizeof(state.user_name), "%s", repo.user_name.c_str());
            std::snprintf(state.user_email, sizeof(state.user_email), "%s", repo.user_email.c_str());
            state.settings_loaded = true;
        }

        ImGui::TextDisabled("%s", repo.git_version.c_str());
        ImGui::Spacing();

        ImGui::TextUnformatted("Remote \"origin\" (GitHub, GitLab... URL)");
        ImGui::SetNextItemWidth(-90.f);
        ImGui::InputTextWithHint("##Remote", "https://github.com/me/my-game.git", state.remote_url, sizeof(state.remote_url));
        ImGui::SameLine();
        ImGui::BeginDisabled(state.busy || Trim(state.remote_url).empty());
        if (ImGui::Button("Set##remote"))
        {
            const std::string url = Trim(state.remote_url);
            RunJob("Setting the remote", { { "remote", repo.remote_url.empty() ? "add" : "set-url", "origin", url } });
            state.settings_loaded = false;
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::TextUnformatted("Author of the commits (this repository)");
        ImGui::SetNextItemWidth(220.f);
        ImGui::InputTextWithHint("##UserName", "Name", state.user_name, sizeof(state.user_name));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-90.f);
        ImGui::InputTextWithHint("##UserEmail", "email", state.user_email, sizeof(state.user_email));
        ImGui::SameLine();
        ImGui::BeginDisabled(state.busy || Trim(state.user_name).empty() || Trim(state.user_email).empty());
        if (ImGui::Button("Set##user"))
        {
            RunJob("Setting the author", { { "config", "user.name", Trim(state.user_name) },
                                          { "config", "user.email", Trim(state.user_email) } });
            state.settings_loaded = false;
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        std::error_code error;
        const bool has_gitignore = stdfs::exists(".gitignore", error);

        ImGui::TextUnformatted(".gitignore");
        ImGui::SameLine();
        ImGui::TextDisabled(has_gitignore ? "(present)" : "(missing : build/ and .lynx/ would be committed)");

        ImGui::BeginDisabled(has_gitignore);
        if (ImGui::Button("Create the Lynx .gitignore"))
        {
            WriteDefaultGitignore();
            Refresh();
        }
        ImGui::EndDisabled();
    }

    static void DrawNotRepository()
    {
        ImGui::TextWrapped("This project is not a Git repository yet.");
        ImGui::TextDisabled("%s", stdfs::current_path().generic_string().c_str());
        ImGui::Spacing();

        ImGui::BeginDisabled(state.busy);
        if (ImGui::Button("Initialize a repository"))
        {
            RunJob("Initializing", { { "init", "-b", "main" } }, []() { WriteDefaultGitignore(); });
            state.settings_loaded = false;
        }
        ImGui::EndDisabled();

        ImGui::TextDisabled("Creates .git/ and a .gitignore (build/, .lynx/, editor layout files).");
    }

    static void DrawPopups()
    {
        if (state.open_discard_popup)
        {
            ImGui::OpenPopup("Discard changes?##Git");
            state.open_discard_popup = false;
        }

        if (ImGui::BeginPopupModal("Discard changes?##Git", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(state.discard_target.c_str());
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.f),
                               state.discard_untracked
                                   ? "This new file will be DELETED (it was never committed)."
                                   : "The file goes back to its last committed version.");

            if (ImGui::Button("Discard", ImVec2(110.f, 0.f)))
            {
                if (state.discard_untracked)
                    RunJob("Discarding", { { "rm", "--cached", "-q", "--ignore-unmatch", "--", state.discard_target },
                                           { "clean", "-f", "-q", "--", state.discard_target } });
                else
                    RunJob("Discarding", { { "restore", "--staged", "--worktree", "--source=HEAD", "--", state.discard_target } });

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(110.f, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        if (state.open_new_branch_popup)
        {
            ImGui::OpenPopup("New branch##Git");
            state.open_new_branch_popup = false;
        }

        if (ImGui::BeginPopupModal("New branch##Git", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("Created from the current branch, then checked out.");
            ImGui::SetNextItemWidth(260.f);

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool enter = ImGui::InputText("##BranchName", state.new_branch, sizeof(state.new_branch),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string name = Trim(state.new_branch);
            const bool valid = !name.empty() && name.find_first_of(" ~^:?*[\\") == std::string::npos;

            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("Create", ImVec2(110.f, 0.f)) || (enter && valid))
            {
                RunJob("Creating the branch", { { "switch", "-c", name } });
                state.new_branch[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(110.f, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
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

    // `level_unsaved` / `save_level` : the commit area offers to save first.
    static void Draw(bool* open, bool level_unsaved, const std::function<void()>& save_level)
    {
        if (open && !*open)
            return;

        ImGui::SetNextWindowSize(ImVec2(980.f, 560.f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Git", open))
        {
            ImGui::End();
            return;
        }

        // Status : at the first frame, then every 4 s while visible.
        if (!state.worker_running &&
            (!state.refreshed_once || GitClock::now() - state.last_refresh > std::chrono::seconds(4)))
            Refresh();

        RepositoryState repo;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            repo = state.repo;
        }

        if (!state.refreshed_once || (!repo.git_found && state.worker_running))
        {
            ImGui::TextDisabled("Reading the repository...");
        }
        else if (!repo.git_found)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.f), "Git was not found.");
            ImGui::TextWrapped("Install Git (https://git-scm.com) and make sure \"git\" works in a terminal, "
                               "then restart the editor.");
            if (ImGui::Button("Retry"))
                Refresh();
        }
        else if (!repo.is_repository)
        {
            DrawNotRepository();
        }
        else
        {
            // --- Toolbar : branch, sync ----------------------------------------
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Branch");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.f);

            ImGui::BeginDisabled(state.busy);
            if (ImGui::BeginCombo("##Branch", repo.branch.empty() ? "(detached)" : repo.branch.c_str()))
            {
                for (const std::string& branch : repo.branches)
                {
                    if (ImGui::Selectable(branch.c_str(), branch == repo.branch) && branch != repo.branch)
                        RunJob("Switching branch", { { "switch", branch } });
                }

                ImGui::Separator();

                if (ImGui::Selectable("New branch..."))
                    state.open_new_branch_popup = true;

                ImGui::EndCombo();
            }

            ImGui::SameLine();

            const bool has_remote = !repo.remote_url.empty();

            if (ImGui::Button("Fetch") && has_remote)
                RunJob("Fetching", { { "fetch", "--prune" } });

            ImGui::SameLine();

            std::string pull_label = "Pull";
            if (repo.behind > 0)
                pull_label += " (" + std::to_string(repo.behind) + ")";

            if (ImGui::Button((pull_label + "##pull").c_str()) && has_remote)
                RunJob("Pulling", { { "pull", "--ff-only" } });

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("git pull --ff-only : refuses to merge if both sides changed (do it in a terminal then).");

            ImGui::SameLine();

            std::string push_label = "Push";
            if (repo.ahead > 0)
                push_label += " (" + std::to_string(repo.ahead) + ")";

            if (ImGui::Button((push_label + "##push").c_str()) && has_remote && !repo.no_commits)
            {
                if (repo.upstream.empty())
                    RunJob("Pushing", { { "push", "-u", "origin", repo.branch } });
                else
                    RunJob("Pushing", { { "push" } });
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            if (!has_remote)
                ImGui::TextDisabled("no remote (Settings tab)");
            else if (repo.upstream.empty())
                ImGui::TextDisabled("not pushed yet");
            else if (repo.ahead == 0 && repo.behind == 0)
                ImGui::TextDisabled("up to date with %s", repo.upstream.c_str());
            else
                ImGui::TextDisabled("%s : %d ahead, %d behind", repo.upstream.c_str(), repo.ahead, repo.behind);

            ImGui::SameLine();

            if (state.busy)
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.f), "  %s...", state.busy_label.c_str());

            // --- Tabs ------------------------------------------------------------
            const float log_height = ImGui::GetTextLineHeightWithSpacing() * 5.f;

            ImGui::BeginChild("##GitMain", ImVec2(0.f, -log_height), false);

            if (ImGui::BeginTabBar("##GitTabs"))
            {
                std::string changes_label = "Changes";
                if (!repo.changes.empty())
                    changes_label += " (" + std::to_string(repo.changes.size()) + ")";

                if (ImGui::BeginTabItem((changes_label + "###GitChanges").c_str()))
                {
                    DrawChanges(repo, level_unsaved, save_level);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("History"))
                {
                    DrawHistory(repo);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Settings"))
                {
                    DrawSettings(repo);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::EndChild();

            // --- Output of the commands ------------------------------------------
            ImGui::BeginChild("##GitLog", ImVec2(0.f, 0.f), true, ImGuiWindowFlags_HorizontalScrollbar);
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                ImGui::TextUnformatted(state.log.c_str(), state.log.c_str() + state.log.size());

                if (state.log_scroll)
                {
                    ImGui::SetScrollHereY(1.f);
                    state.log_scroll = false;
                }
            }
            ImGui::EndChild();
        }

        DrawPopups();
        ImGui::End();
    }

    static void Shutdown()
    {
        JoinWorker();
    }
}
