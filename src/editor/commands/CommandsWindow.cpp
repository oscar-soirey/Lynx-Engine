#include "CommandsWindow.h"

#include "CommandRegistry.h"
#include "CommandServer.h"
#include "ScriptRunner.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::commands_window
{
	namespace
	{
		using commands::Json;
		using Clock = std::chrono::steady_clock;

		// ---------------------------------------------------------------------
		// Text logs
		// ---------------------------------------------------------------------

		struct LogLine
		{
			std::string text;
			int kind = 0;   // 0 normal, 1 command, 2 ok, 3 error
		};

		constexpr size_t kMaxLog = 4000;

		std::deque<LogLine> g_console;
		std::deque<LogLine> g_activity;
		bool g_console_scroll = false;

		void Push(std::deque<LogLine>& log, const std::string& text, int kind)
		{
			// Multi-line text : one entry per line.
			size_t start = 0;

			while (start <= text.size())
			{
				const size_t end = text.find('\n', start);
				log.push_back({ text.substr(start, end == std::string::npos ? std::string::npos : end - start), kind });

				if (end == std::string::npos)
					break;

				start = end + 1;
			}

			while (log.size() > kMaxLog)
				log.pop_front();
		}

		void DrawLog(const char* id, std::deque<LogLine>& log, bool& scroll)
		{
			ImGui::BeginChild(id, ImVec2(0.f, 0.f), true, ImGuiWindowFlags_HorizontalScrollbar);

			const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(log.size()));

			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
				{
					const LogLine& line = log[static_cast<size_t>(i)];

					static const ImVec4 kColors[] = {
						ImVec4(0.85f, 0.85f, 0.85f, 1.f),
						ImVec4(0.55f, 0.75f, 1.00f, 1.f),
						ImVec4(0.55f, 0.85f, 0.55f, 1.f),
						ImVec4(0.95f, 0.45f, 0.45f, 1.f),
					};

					ImGui::PushStyleColor(ImGuiCol_Text, kColors[std::clamp(line.kind, 0, 3)]);
					ImGui::TextUnformatted(line.text.c_str());
					ImGui::PopStyleColor();
				}
			}

			if (scroll || at_bottom)
			{
				ImGui::SetScrollHereY(1.f);
				scroll = false;
			}

			ImGui::EndChild();
		}

		// Results can be huge (screenshots, file contents) : shortened.
		std::string Shorten(const Json& value)
		{
			Json copy = value;

			if (copy.is_object() && copy.contains("data") && copy["data"].is_string() &&
				copy["data"].get<std::string>().size() > 200)
			{
				copy["data"] = "<" + std::to_string(copy["data"].get<std::string>().size()) + " base64 characters>";
			}

			std::string text = copy.dump(2, ' ', false, Json::error_handler_t::replace);

			if (text.size() > 20000)
				text = text.substr(0, 20000) + "\n... (" + std::to_string(text.size()) + " characters)";

			return text;
		}


		// ---------------------------------------------------------------------
		// Console
		// ---------------------------------------------------------------------

		char g_input[4096] = {};
		std::vector<std::string> g_history;
		int g_history_pos = -1;

		// "name {json}" or "name key=value key=value" (values : JSON, or text).
		bool ParseCommandLine(const std::string& line, std::string& name, Json& params, std::string& error)
		{
			size_t i = line.find_first_not_of(" \t");

			if (i == std::string::npos)
				return false;

			const size_t name_end = line.find_first_of(" \t", i);
			name = line.substr(i, name_end == std::string::npos ? std::string::npos : name_end - i);
			params = Json::object();

			if (name_end == std::string::npos)
				return true;

			const std::string rest = line.substr(name_end);
			const size_t first = rest.find_first_not_of(" \t");

			if (first == std::string::npos)
				return true;

			if (rest[first] == '{')
			{
				try
				{
					params = Json::parse(rest.substr(first));
					return true;
				}
				catch (const Json::exception& e)
				{
					error = std::string("invalid JSON : ") + e.what();
					return false;
				}
			}

			// key=value pairs ; a value may be JSON with spaces inside [] {} "".
			size_t p = first;

			while (p < rest.size())
			{
				while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t'))
					++p;

				if (p >= rest.size())
					break;

				const size_t equal = rest.find('=', p);

				if (equal == std::string::npos)
				{
					error = "expected key=value near \"" + rest.substr(p) + "\"";
					return false;
				}

				const std::string key = rest.substr(p, equal - p);
				size_t q = equal + 1;
				int depth = 0;
				bool in_string = false;

				while (q < rest.size())
				{
					const char c = rest[q];

					if (in_string)
					{
						if (c == '\\')
							++q;
						else if (c == '"')
							in_string = false;
					}
					else if (c == '"')
						in_string = true;
					else if (c == '[' || c == '{')
						++depth;
					else if (c == ']' || c == '}')
						--depth;
					else if ((c == ' ' || c == '\t') && depth <= 0)
						break;

					++q;
				}

				const std::string text = rest.substr(equal + 1, q - equal - 1);

				try
				{
					params[key] = Json::parse(text);
				}
				catch (...)
				{
					params[key] = text;   // plain word
				}

				p = q;
			}

			return true;
		}

		void RunConsoleLine(const std::string& line)
		{
			std::string name;
			Json params;
			std::string error;

			Push(g_console, "> " + line, 1);
			g_console_scroll = true;

			if (!ParseCommandLine(line, name, params, error))
			{
				if (!error.empty())
					Push(g_console, error, 3);

				return;
			}

			commands::CommandContext context;
			context.source = "console";

			commands::Execute(name, params, context, [](bool ok, Json value)
			{
				if (ok)
					Push(g_console, Shorten(value), 2);
				else
					Push(g_console, value.is_string() ? value.get<std::string>() : value.dump(), 3);

				g_console_scroll = true;
			});
		}

		int ConsoleCallback(ImGuiInputTextCallbackData* data)
		{
			if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || g_history.empty())
				return 0;

			if (data->EventKey == ImGuiKey_UpArrow)
				g_history_pos = g_history_pos < 0 ? static_cast<int>(g_history.size()) - 1 : std::max(0, g_history_pos - 1);
			else if (data->EventKey == ImGuiKey_DownArrow && g_history_pos >= 0)
				g_history_pos = g_history_pos + 1 >= static_cast<int>(g_history.size()) ? -1 : g_history_pos + 1;

			data->DeleteChars(0, data->BufTextLen);

			if (g_history_pos >= 0)
				data->InsertChars(0, g_history[static_cast<size_t>(g_history_pos)].c_str());

			return 0;
		}

		void DrawConsole()
		{
			ImGui::TextDisabled("command {json}   or   command key=value ...      ex : actor.spawn class=Enemy location=[2,3,0]");

			const float input_height = ImGui::GetFrameHeightWithSpacing();
			ImGui::BeginChild("##ConsoleOut", ImVec2(0.f, -input_height));
			DrawLog("##ConsoleLog", g_console, g_console_scroll);
			ImGui::EndChild();

			ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Clear").x - ImGui::GetStyle().FramePadding.x * 2.f - ImGui::GetStyle().ItemSpacing.x);

			bool reclaim = false;

			if (ImGui::InputText("##ConsoleInput", g_input, sizeof(g_input),
			                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
			                     ConsoleCallback))
			{
				const std::string line = g_input;

				if (!line.empty())
				{
					if (g_history.empty() || g_history.back() != line)
						g_history.push_back(line);

					g_history_pos = -1;
					RunConsoleLine(line);
				}

				g_input[0] = '\0';
				reclaim = true;
			}

			if (reclaim)
				ImGui::SetKeyboardFocusHere(-1);

			ImGui::SameLine();

			if (ImGui::Button("Clear"))
				g_console.clear();
		}


		// ---------------------------------------------------------------------
		// Scripts
		// ---------------------------------------------------------------------

		struct ScriptFile
		{
			fs::path path;
			std::string label;         // relative to commands/
			std::string description;   // first docstring line
		};

		std::vector<ScriptFile> g_scripts;
		Clock::time_point g_last_scan{};
		int g_selected = -1;
		char g_arguments[1024] = {};
		char g_python[512] = {};
		bool g_python_loaded = false;
		bool g_output_scroll = false;
		size_t g_output_lines = 0;

		std::string ReadDescription(const fs::path& file)
		{
			std::ifstream in(file);
			std::string line;

			for (int i = 0; i < 6 && std::getline(in, line); ++i)
			{
				const size_t start = line.find_first_not_of(" \t");

				if (start == std::string::npos || line[start] == '#')
					continue;

				for (const char* quote : { "\"\"\"", "'''" })
				{
					if (line.compare(start, 3, quote) == 0)
					{
						std::string text = line.substr(start + 3);
						const size_t end = text.find(quote);

						if (end != std::string::npos)
							text = text.substr(0, end);

						if (text.empty() && std::getline(in, line))
							text = line;

						const size_t first = text.find_first_not_of(" \t");
						return first == std::string::npos ? std::string() : text.substr(first);
					}
				}

				return {};
			}

			return {};
		}

		void ScanScripts(bool force)
		{
			const Clock::time_point now = Clock::now();

			if (!force && now - g_last_scan < std::chrono::seconds(2))
				return;

			g_last_scan = now;

			const fs::path folder = script_runner::ScriptsFolder();
			const fs::path selected = (g_selected >= 0 && g_selected < static_cast<int>(g_scripts.size()))
				? g_scripts[static_cast<size_t>(g_selected)].path : fs::path();

			std::vector<ScriptFile> scripts;
			std::error_code ec;

			fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);

			for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
			{
				if (it->path().filename() == "__pycache__")
				{
					it.disable_recursion_pending();
					continue;
				}

				if (!it->is_regular_file() || it->path().extension() != ".py")
					continue;

				ScriptFile file;
				file.path = it->path();
				file.label = fs::relative(it->path(), folder, ec).generic_string();
				file.description = ReadDescription(it->path());
				scripts.push_back(std::move(file));
			}

			std::sort(scripts.begin(), scripts.end(),
			          [](const ScriptFile& a, const ScriptFile& b) { return a.label < b.label; });

			g_scripts = std::move(scripts);
			g_selected = -1;

			for (size_t i = 0; i < g_scripts.size(); ++i)
			{
				if (g_scripts[i].path == selected)
					g_selected = static_cast<int>(i);
			}
		}

		void CreateNewScript()
		{
			const fs::path folder = script_runner::ScriptsFolder();
			std::error_code ec;
			fs::create_directories(folder, ec);

			fs::path file = folder / "new_command.py";

			for (int i = 2; fs::exists(file, ec); ++i)
				file = folder / ("new_command_" + std::to_string(i) + ".py");

			std::ofstream out(file);
			out <<
				"\"\"\"New command : describe it here (shown in the editor).\"\"\"\n"
				"\n"
				"import lynx_editor as lynx\n"
				"\n"
				"\n"
				"def main():\n"
				"    info = lynx.info()\n"
				"    print(\"Project :\", info[\"project\"], \"-\", info[\"actor_count\"], \"actors\")\n"
				"\n"
				"    # Every change of this block is ONE Ctrl+Z in the editor.\n"
				"    with lynx.undo_group(\"New command\"):\n"
				"        for actor in lynx.actors():\n"
				"            print(actor.id, actor.cls, actor.location)\n"
				"\n"
				"\n"
				"if __name__ == \"__main__\":\n"
				"    main()\n";

			ScanScripts(true);

			for (size_t i = 0; i < g_scripts.size(); ++i)
			{
				if (g_scripts[i].path == file)
					g_selected = static_cast<int>(i);
			}
		}

		void OpenPath(const fs::path& path)
		{
#ifdef _WIN32
			ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
			(void)path;
#endif
		}

		void DrawScripts()
		{
			if (!g_python_loaded)
			{
				g_python_loaded = true;
				std::snprintf(g_python, sizeof(g_python), "%s", script_runner::PythonExecutable().c_str());
			}

			ScanScripts(false);

			const bool running = script_runner::IsRunning();

			// --- Left : script list ------------------------------------------

			ImGui::BeginChild("##ScriptList", ImVec2(ImGui::GetContentRegionAvail().x * 0.32f, 0.f), true);

			if (ImGui::SmallButton("New"))
				CreateNewScript();

			ImGui::SameLine();

			if (ImGui::SmallButton("Refresh"))
				ScanScripts(true);

			ImGui::SameLine();

			if (ImGui::SmallButton("Folder"))
			{
				std::error_code ec;
				fs::create_directories(script_runner::ScriptsFolder(), ec);
				OpenPath(script_runner::ScriptsFolder());
			}

			ImGui::Separator();

			if (g_scripts.empty())
				ImGui::TextWrapped("No script. Put .py files in the \"commands\" folder of the project, or click New.");

			for (size_t i = 0; i < g_scripts.size(); ++i)
			{
				const ScriptFile& script = g_scripts[i];

				if (ImGui::Selectable(script.label.c_str(), g_selected == static_cast<int>(i),
				                      ImGuiSelectableFlags_AllowDoubleClick))
				{
					g_selected = static_cast<int>(i);

					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !running)
						script_runner::Run(script.path, g_arguments);
				}

				if (!script.description.empty() && ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", script.description.c_str());
			}

			ImGui::EndChild();
			ImGui::SameLine();

			// --- Right : run / output ----------------------------------------

			ImGui::BeginGroup();

			const ScriptFile* selected =
				(g_selected >= 0 && g_selected < static_cast<int>(g_scripts.size()))
					? &g_scripts[static_cast<size_t>(g_selected)] : nullptr;

			if (selected)
			{
				ImGui::TextUnformatted(selected->label.c_str());

				if (!selected->description.empty())
					ImGui::TextDisabled("%s", selected->description.c_str());
			}
			else
			{
				ImGui::TextDisabled("Select a script");
			}

			ImGui::SetNextItemWidth(260.f);
			ImGui::InputTextWithHint("##Args", "arguments", g_arguments, sizeof(g_arguments));
			ImGui::SameLine();

			ImGui::BeginDisabled(!selected || running);

			if (ImGui::Button("Run") && selected)
			{
				script_runner::Run(selected->path, g_arguments);
				g_output_scroll = true;
			}

			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!running);

			if (ImGui::Button("Stop"))
				script_runner::Stop();

			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!selected);

			if (ImGui::Button("Edit") && selected)
				OpenPath(selected->path);

			ImGui::EndDisabled();
			ImGui::SameLine();

			if (ImGui::Button("Clear"))
				script_runner::ClearOutput();

			if (running)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.f), "Running...");
			}

			// Output.
			ImGui::BeginChild("##ScriptOutput", ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing()), true,
			                  ImGuiWindowFlags_HorizontalScrollbar);

			const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;

			script_runner::ForEachLine([](const std::string& line)
			{
				const bool error = line.find("Error") != std::string::npos ||
				                   line.find("ERROR") != std::string::npos ||
				                   line.find("Traceback") != std::string::npos ||
				                   line.rfind("--- Failed", 0) == 0;

				if (error)
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.f));

				ImGui::TextUnformatted(line.c_str());

				if (error)
					ImGui::PopStyleColor();
			});

			const size_t count = script_runner::LineCount();

			if (g_output_scroll || (at_bottom && count != g_output_lines))
			{
				ImGui::SetScrollHereY(1.f);
				g_output_scroll = false;
			}

			g_output_lines = count;

			ImGui::EndChild();

			// Python executable.
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Python");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(260.f);

			if (ImGui::InputText("##Python", g_python, sizeof(g_python)))
			{
				script_runner::PythonExecutable() = g_python;
				script_runner::SaveSettings();
			}

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Python 3 executable : python, py, or a full path (C:/Python312/python.exe).");

			ImGui::EndGroup();
		}


		// ---------------------------------------------------------------------
		// Reference
		// ---------------------------------------------------------------------

		char g_filter[128] = {};

		void DrawReference()
		{
			ImGui::SetNextItemWidth(-1.f);
			ImGui::InputTextWithHint("##RefFilter", "Filter (actor, voxel, asset...)", g_filter, sizeof(g_filter));

			ImGui::BeginChild("##Reference", ImVec2(0.f, 0.f), true);

			const std::string filter = g_filter;

			for (const commands::CommandInfo* command : commands::List())
			{
				if (!filter.empty() && command->name.find(filter) == std::string::npos &&
					command->description.find(filter) == std::string::npos)
					continue;

				if (!ImGui::TreeNode(command->name.c_str()))
				{
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", command->description.c_str());

					continue;
				}

				ImGui::PushTextWrapPos(0.f);
				ImGui::TextUnformatted(command->description.c_str());
				ImGui::PopTextWrapPos();

				for (const commands::ParamInfo& param : command->params)
				{
					ImGui::Bullet();
					ImGui::TextColored(param.required ? ImVec4(1.f, 0.85f, 0.4f, 1.f) : ImVec4(0.7f, 0.8f, 1.f, 1.f),
					                   "%s", param.name.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("(%s%s)", param.type.c_str(), param.required ? ", required" : "");
					ImGui::SameLine();
					ImGui::PushTextWrapPos(0.f);
					ImGui::TextUnformatted(param.description.c_str());
					ImGui::PopTextWrapPos();
				}

				if (ImGui::SmallButton("Try in console"))
				{
					std::snprintf(g_input, sizeof(g_input), "%s ", command->name.c_str());
				}

				ImGui::TreePop();
			}

			ImGui::EndChild();
		}


		// ---------------------------------------------------------------------
		// AI / MCP
		// ---------------------------------------------------------------------

		bool g_activity_scroll = false;

		std::string JsonPath(const fs::path& path)
		{
			return Json(path.generic_string()).dump();
		}

		void DrawAi()
		{
			if (command_server::IsRunning())
			{
				ImGui::Text("Command server : 127.0.0.1:%d   -   %d client(s) connected",
				            command_server::Port(), command_server::ClientCount());
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.f), "Command server not running (see the console).");
			}

			ImGui::TextDisabled("Connection file : %s", command_server::ConnectionFile().generic_string().c_str());

			ImGui::Spacing();
			ImGui::TextWrapped(
				"An AI model acts on the editor through the MCP server python/lynx_mcp.py (every command "
				"becomes a tool). Add it to your MCP client (Claude Desktop, Claude Code...) :");

			const fs::path mcp = script_runner::PythonFolder() / "lynx_mcp.py";
			const std::string python = script_runner::PythonExecutable();

			const std::string desktop =
				"{\n"
				"  \"mcpServers\": {\n"
				"    \"lynx\": {\n"
				"      \"command\": " + Json(python).dump() + ",\n"
				"      \"args\": [" + JsonPath(mcp) + "]\n"
				"    }\n"
				"  }\n"
				"}";

			const std::string code =
				"claude mcp add lynx -- " + python + " \"" + mcp.generic_string() + "\"";

			ImGui::Spacing();
			ImGui::TextUnformatted("Claude Desktop (claude_desktop_config.json) :");
			ImGui::InputTextMultiline("##McpDesktop", const_cast<char*>(desktop.c_str()), desktop.size() + 1,
			                          ImVec2(-1.f, ImGui::GetTextLineHeight() * 8.5f), ImGuiInputTextFlags_ReadOnly);

			if (ImGui::SmallButton("Copy##desktop"))
				ImGui::SetClipboardText(desktop.c_str());

			ImGui::Spacing();
			ImGui::TextUnformatted("Claude Code :");
			ImGui::InputText("##McpCode", const_cast<char*>(code.c_str()), code.size() + 1, ImGuiInputTextFlags_ReadOnly);

			if (ImGui::SmallButton("Copy##code"))
				ImGui::SetClipboardText(code.c_str());

			ImGui::Spacing();
			ImGui::TextWrapped("Without --project, the MCP server connects to the editor started last. "
			                   "Add \"--project\", \"<project folder>\" to the args to pin a project.");

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextUnformatted("Activity");
			ImGui::SameLine();

			if (ImGui::SmallButton("Clear##activity"))
				g_activity.clear();

			DrawLog("##Activity", g_activity, g_activity_scroll);
		}
	}


	void Update()
	{
		static bool installed = false;

		if (!installed)
		{
			installed = true;

			commands::SetLogCallback([](const std::string& line)
			{
				Push(g_activity, line, line.find("error") != std::string::npos ? 3 : 0);
			});
		}

		int exit_code = 0;

		if (script_runner::PollFinished(exit_code))
			g_output_scroll = true;
	}


	void Draw(bool* open)
	{
		if (open && !*open)
			return;

		ImGui::SetNextWindowSize(ImVec2(900.f, 480.f), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Commands", open))
		{
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("##CommandsTabs"))
		{
			if (ImGui::BeginTabItem("Scripts"))
			{
				DrawScripts();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Console"))
			{
				DrawConsole();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Reference"))
			{
				DrawReference();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("AI / MCP"))
			{
				DrawAi();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}
