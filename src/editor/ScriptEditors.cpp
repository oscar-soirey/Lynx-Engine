#include "ScriptEditors.h"

#include "EditorIcons.h"
#include "../scripting/Scripting.h"

#include <imgui_textedit/TextEditor.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace lynx::editor::script_editors
{
	namespace
	{
		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		// ---------------------------------------------------------------------
		// Languages
		// ---------------------------------------------------------------------

		void AddCommonTokens(TextEditor::LanguageDefinition& d)
		{
			using P = TextEditor::PaletteIndex;
			d.mTokenRegexStrings.push_back({ "\\\"(\\\\.|[^\\\"])*\\\"", P::String });
			d.mTokenRegexStrings.push_back({ "'(\\\\.|[^'])*'", P::String });
			d.mTokenRegexStrings.push_back({ "`([^`\\\\]|\\\\.)*`", P::String });
			d.mTokenRegexStrings.push_back({ "[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", P::Number });
			d.mTokenRegexStrings.push_back({ "0[xX][0-9a-fA-F]+", P::Number });
			d.mTokenRegexStrings.push_back({ "[a-zA-Z_$][a-zA-Z0-9_$]*", P::Identifier });
			d.mTokenRegexStrings.push_back({ "[\\[\\]{}!%\\^&*()\\-+=~|<>?/;,.:@]", P::Punctuation });
			d.mCaseSensitive = true;
			d.mAutoIndentation = true;
		}

		const TextEditor::LanguageDefinition& JavaScript()
		{
			static TextEditor::LanguageDefinition d;
			static bool ready = false;
			if (!ready)
			{
				for (const char* k : { "as", "async", "await", "break", "case", "catch", "class", "const", "continue",
				                       "debugger", "default", "delete", "do", "else", "export", "extends", "false",
				                       "finally", "for", "from", "function", "get", "if", "import", "in", "instanceof",
				                       "let", "new", "null", "of", "return", "set", "static", "super", "switch", "this",
				                       "throw", "true", "try", "typeof", "undefined", "var", "void", "while", "with", "yield" })
					d.mKeywords.insert(k);
				// Globals of the Lynx scripting API (scripting/README.md).
				for (const char* k : { "print", "console", "Level", "Input", "Engine", "vec3", "parent", "Actor", "Math",
				                       "BeginPlay", "Update", "EndPlay", "properties" })
				{
					TextEditor::Identifier id;
					id.mDeclaration = "Lynx";
					d.mIdentifiers.insert({ k, id });
				}
				AddCommonTokens(d);
				d.mCommentStart = "/*";
				d.mCommentEnd = "*/";
				d.mSingleLineComment = "//";
				d.mName = "JavaScript";
				ready = true;
			}
			return d;
		}

		const TextEditor::LanguageDefinition& Python()
		{
			static TextEditor::LanguageDefinition d;
			static bool ready = false;
			if (!ready)
			{
				for (const char* k : { "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class",
				                       "continue", "def", "del", "elif", "else", "except", "finally", "for", "from",
				                       "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass",
				                       "raise", "return", "try", "while", "with", "yield", "self" })
					d.mKeywords.insert(k);
				for (const char* k : { "print", "len", "range", "list", "dict", "str", "int", "float", "lynx", "enumerate" })
				{
					TextEditor::Identifier id;
					id.mDeclaration = "built-in";
					d.mIdentifiers.insert({ k, id });
				}
				AddCommonTokens(d);
				d.mSingleLineComment = "#";
				d.mCommentStart = "\"\"\"";
				d.mCommentEnd = "\"\"\"";
				d.mName = "Python";
				ready = true;
			}
			return d;
		}

		const TextEditor::LanguageDefinition& PlainData()
		{
			static TextEditor::LanguageDefinition d;
			static bool ready = false;
			if (!ready)
			{
				for (const char* k : { "true", "false", "null" })
					d.mKeywords.insert(k);
				AddCommonTokens(d);
				d.mSingleLineComment = "//";   // voxels.json allows // comments
				d.mCommentStart = "<!--";
				d.mCommentEnd = "-->";
				d.mName = "Text";
				ready = true;
			}
			return d;
		}

		const TextEditor::LanguageDefinition& LanguageFor(const fs::path& path)
		{
			const std::string e = Lower(path.extension().string());
			if (e == ".js")
				return JavaScript();
			if (e == ".py")
				return Python();
			if (e == ".cpp" || e == ".h" || e == ".hpp" || e == ".inl" || e == ".c")
				return TextEditor::LanguageDefinition::CPlusPlus();
			if (e == ".glsl" || e == ".vert" || e == ".frag")
				return TextEditor::LanguageDefinition::GLSL();
			return PlainData();
		}

		// ---------------------------------------------------------------------
		// One open file
		// ---------------------------------------------------------------------

		struct Editor
		{
			fs::path path;
			TextEditor text;
			std::string saved_text;          // content on disk (dirty = different)
			fs::file_time_type write_time{};
			bool has_write_time = false;
			bool open = true;
			bool dirty = false;
			bool changed_on_disk = false;    // while dirty : ask before reloading
			bool focus_next_frame = true;
			bool ask_close = false;
			float zoom = 1.f;
			std::string error;               // save / load error
			// JavaScript check
			std::string check_error;
			int check_line = 0;
			double last_edit_time = 0.0;
			bool check_pending = true;
			// Find / go to line
			bool show_find = false;
			bool focus_find = false;
			char find[128] = {};
			bool show_goto = false;
			char goto_line[16] = {};
		};

		std::vector<std::unique_ptr<Editor>> g_editors;

		bool ReadFile(const fs::path& path, std::string& out)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return false;
			out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			return true;
		}

		void Load(Editor& e)
		{
			std::string source;
			if (!ReadFile(e.path, source))
			{
				e.error = "Could not open " + e.path.string();
				return;
			}
			const auto cursor = e.text.GetCursorPosition();
			e.text.SetText(source);
			if (cursor.mLine < e.text.GetTotalLines())
				e.text.SetCursorPosition(cursor);
			e.saved_text = source;
			e.dirty = false;
			e.changed_on_disk = false;
			e.error.clear();
			e.check_pending = true;
			std::error_code ec;
			e.write_time = fs::last_write_time(e.path, ec);
			e.has_write_time = !ec;
		}

		bool Save(Editor& e)
		{
			std::string text = e.text.GetText();
			// TextEditor ends the text with a new line it added : keep the file as it was.
			if (!e.saved_text.empty() && e.saved_text.back() != '\n' && !text.empty() && text.back() == '\n')
				text.pop_back();

			std::ofstream file(e.path, std::ios::binary | std::ios::trunc);
			file.write(text.data(), static_cast<std::streamsize>(text.size()));
			file.flush();
			if (!file)
			{
				e.error = "Could not save " + e.path.string();
				return false;
			}
			std::error_code ec;
			e.write_time = fs::last_write_time(e.path, ec);
			e.has_write_time = !ec;
			e.saved_text = text;
			e.dirty = false;
			e.changed_on_disk = false;
			e.error.clear();
			e.check_pending = true;
			return true;
		}

		void ApplyPalette(Editor& e)
		{
			// Light palette on the light pixel theme, dark palette otherwise ;
			// background and line numbers follow the theme.
			const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
			const bool light = bg.x * 0.3f + bg.y * 0.59f + bg.z * 0.11f > 0.5f;
			TextEditor::Palette palette = light ? TextEditor::GetLightPalette() : TextEditor::GetDarkPalette();
			palette[static_cast<int>(TextEditor::PaletteIndex::Background)] = ImGui::GetColorU32(ImGuiCol_FrameBg);
			palette[static_cast<int>(TextEditor::PaletteIndex::LineNumber)] = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			if (light)
			{
				palette[static_cast<int>(TextEditor::PaletteIndex::Default)] = IM_COL32(20, 20, 20, 255);
				palette[static_cast<int>(TextEditor::PaletteIndex::Identifier)] = IM_COL32(20, 20, 20, 255);
				palette[static_cast<int>(TextEditor::PaletteIndex::Keyword)] = IM_COL32(150, 40, 120, 255);
				palette[static_cast<int>(TextEditor::PaletteIndex::KnownIdentifier)] = IM_COL32(30, 100, 150, 255);
				palette[static_cast<int>(TextEditor::PaletteIndex::Comment)] = IM_COL32(110, 120, 100, 255);
				palette[static_cast<int>(TextEditor::PaletteIndex::MultiLineComment)] = IM_COL32(110, 120, 100, 255);
			}
			e.text.SetPalette(palette);
		}

		// JavaScript : syntax + class structure, a moment after the last key.
		void CheckScript(Editor& e)
		{
			e.check_pending = false;
			e.check_error.clear();
			e.check_line = 0;
			TextEditor::ErrorMarkers markers;

			if (Lower(e.path.extension().string()) == ".js")
			{
				std::string message;
				if (!lynx::CheckScriptSyntax(e.text.GetText(), e.path.filename().string().c_str(), message))
				{
					e.check_error = message.substr(0, message.find('\n'));
					static const std::regex position(R"(:(\d+):(\d+))");
					std::smatch match;
					if (std::regex_search(message, match, position))
						e.check_line = std::stoi(match.str(1));
					if (e.check_line > 0)
						markers[e.check_line] = e.check_error;
				}
			}
			e.text.SetErrorMarkers(markers);
		}

		// Next / previous occurrence of the find text, from the cursor.
		void FindNext(Editor& e, bool backwards)
		{
			const std::string needle = Lower(e.find);
			if (needle.empty())
				return;
			const std::vector<std::string> lines = e.text.GetTextLines();
			const auto cursor = e.text.GetCursorPosition();
			const int count = static_cast<int>(lines.size());

			for (int step = 0; step <= count; ++step)
			{
				int line = backwards ? (cursor.mLine - step + count) % count : (cursor.mLine + step) % count;
				const std::string text = Lower(lines[static_cast<size_t>(line)]);
				size_t at = std::string::npos;
				if (!backwards)
				{
					const size_t from = step == 0 ? static_cast<size_t>(std::max(0, cursor.mColumn + 1)) : 0;
					at = from <= text.size() ? text.find(needle, from) : std::string::npos;
				}
				else
				{
					const size_t before = step == 0 ? static_cast<size_t>(std::max(0, cursor.mColumn - 1)) : std::string::npos;
					at = text.rfind(needle, before);
				}
				if (at != std::string::npos)
				{
					const TextEditor::Coordinates start(line, static_cast<int>(at));
					const TextEditor::Coordinates end(line, static_cast<int>(at + needle.size()));
					e.text.SetCursorPosition(start);
					e.text.SetSelection(start, end);
					return;
				}
			}
		}

		void GoToLine(Editor& e, int line)
		{
			line = std::clamp(line, 1, std::max(1, e.text.GetTotalLines()));
			e.text.SetCursorPosition(TextEditor::Coordinates(line - 1, 0));
		}

		// ( [ { : the closing one is inserted after the cursor.
		void AutoCloseBrackets(Editor& e, const std::string& before)
		{
			const std::string after = e.text.GetText();
			if (after.size() != before.size() + 1)
				return;
			size_t at = 0;
			while (at < before.size() && before[at] == after[at])
				++at;
			if (at < before.size() && before.compare(at, std::string::npos, after, at + 1, std::string::npos) != 0)
				return;
			const char* closing = nullptr;
			switch (after[at])
			{
			case '{': closing = "}"; break;
			case '[': closing = "]"; break;
			case '(': closing = ")"; break;
			default: break;
			}
			if (closing)
			{
				const auto cursor = e.text.GetCursorPosition();
				e.text.InsertText(closing);
				e.text.SetCursorPosition(cursor);
			}
		}

		void Toolbar(Editor& e)
		{
			if (icons::Button("Save", icons::Icon::Save, icons::kThemeTint, false))
				Save(e);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Save (Ctrl+S)");
			ImGui::SameLine();
			if (icons::Button("Reload", icons::Icon::Reload))
				Load(e);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Reload from disk (unsaved edits are lost)");
			ImGui::SameLine();
			ImGui::BeginDisabled(!e.text.CanUndo());
			if (icons::Button("Undo", icons::Icon::Undo))
				e.text.Undo();
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Undo (Ctrl+Z) - Redo : Ctrl+Y");
			ImGui::SameLine();
			if (icons::Button("Find", icons::Icon::Search, icons::kThemeTint, e.show_find))
			{
				e.show_find = !e.show_find;
				e.focus_find = e.show_find;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Find (Ctrl+F), next F3, previous Shift+F3");

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
			if (ImGui::SmallButton("-"))
				e.zoom = std::max(0.5f, e.zoom - 0.1f);
			ImGui::SameLine();
			ImGui::TextDisabled("%d%%", static_cast<int>(std::round(e.zoom * 100.f)));
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
				e.zoom = std::min(3.f, e.zoom + 0.1f);

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
			ImGui::TextDisabled("%s", e.path.parent_path().filename().generic_string().c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", e.path.string().c_str());
		}

		void FindBar(Editor& e)
		{
			ImGui::AlignTextToFramePadding();
			icons::Draw(icons::Icon::Search, 0.f, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::SameLine();
			if (e.focus_find)
			{
				ImGui::SetKeyboardFocusHere();
				e.focus_find = false;
			}
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
			if (ImGui::InputTextWithHint("##find", "Find", e.find, sizeof(e.find), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				FindNext(e, ImGui::GetIO().KeyShift);
				ImGui::SetKeyboardFocusHere(-1);
			}
			ImGui::SameLine();
			if (ImGui::Button("Next"))
				FindNext(e, false);
			ImGui::SameLine();
			if (ImGui::Button("Previous"))
				FindNext(e, true);
			ImGui::SameLine();
			if (ImGui::SmallButton("x##closefind"))
				e.show_find = false;
		}

		void StatusBar(Editor& e)
		{
			const auto cursor = e.text.GetCursorPosition();
			ImGui::TextDisabled("Ln %d, Col %d   %d lines   %s", cursor.mLine + 1, cursor.mColumn + 1,
			                    e.text.GetTotalLines(), e.text.GetLanguageDefinition().mName.c_str());
			if (!e.check_error.empty())
			{
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.22f, 0.20f, 1.f));
				const std::string label = (e.check_line > 0 ? "Line " + std::to_string(e.check_line) + " : " : std::string()) +
				                          e.check_error;
				ImGui::TextUnformatted(label.c_str());
				ImGui::PopStyleColor();
				if (ImGui::IsItemClicked() && e.check_line > 0)
					GoToLine(e, e.check_line);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Click : go to the line");
			}
			else if (Lower(e.path.extension().string()) == ".js")
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.25f, 0.55f, 0.28f, 1.f), "No error");
			}
		}

		void DrawEditor(Editor& e, ImGuiID dock_id)
		{
			// --- Changes on disk ------------------------------------------------
			std::error_code ec;
			const auto disk_time = fs::last_write_time(e.path, ec);
			if (!ec && e.has_write_time && disk_time != e.write_time)
			{
				if (!e.dirty)
					Load(e);   // nothing to lose
				else
				{
					e.changed_on_disk = true;
					e.write_time = disk_time;
				}
			}

			if (!e.dirty && e.text.IsTextChanged())
				e.dirty = e.text.GetText() != e.saved_text && e.text.GetText() != e.saved_text + "\n";
			if (e.text.IsTextChanged())
			{
				e.last_edit_time = ImGui::GetTime();
				e.check_pending = true;
			}
			if (e.check_pending && ImGui::GetTime() - e.last_edit_time > 0.4)
				CheckScript(e);

			// --- Window ----------------------------------------------------------
			// "Name.js *###<path>" : the title changes, the window stays the same.
			const std::string title = e.path.filename().string() + (e.dirty ? " *" : "") + "###script:" + e.path.generic_string();

			if (dock_id != 0)
				ImGui::SetNextWindowDockID(dock_id, ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(900.f, 650.f), ImGuiCond_FirstUseEver);
			if (e.focus_next_frame)
			{
				ImGui::SetNextWindowFocus();
				e.focus_next_frame = false;
			}

			bool window_open = true;
			ImGuiWindowFlags flags = e.dirty ? ImGuiWindowFlags_UnsavedDocument : 0;
			const bool visible = ImGui::Begin(title.c_str(), &window_open, flags);

			if (!window_open)
			{
				if (e.dirty)
					e.ask_close = true;
				else
					e.open = false;
			}

			if (visible)
			{
				ImGuiIO& io = ImGui::GetIO();
				const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
				const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

				// Shortcuts of the editor window.
				if (focused)
				{
					if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
					{
						e.show_find = true;
						e.focus_find = true;
						const std::string selected = e.text.GetSelectedText();
						if (!selected.empty() && selected.find('\n') == std::string::npos)
							std::snprintf(e.find, sizeof(e.find), "%s", selected.c_str());
					}
					if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false))
					{
						e.show_goto = true;
						ImGui::OpenPopup("Go to line");
					}
					if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
						FindNext(e, io.KeyShift);
					if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false))
						e.zoom = 1.f;
				}
				if (hovered && io.KeyCtrl && io.MouseWheel != 0.f)
				{
					e.zoom = std::clamp(e.zoom + io.MouseWheel * 0.1f, 0.5f, 3.f);
					io.MouseWheel = 0.f;
				}

				Toolbar(e);
				if (e.show_find)
					FindBar(e);

				if (e.changed_on_disk)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.45f, 0.05f, 1.f));
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted("The file changed on disk.");
					ImGui::PopStyleColor();
					ImGui::SameLine();
					if (ImGui::SmallButton("Reload"))
						Load(e);
					ImGui::SameLine();
					if (ImGui::SmallButton("Keep my version"))
						e.changed_on_disk = false;
				}
				if (!e.error.empty())
					ImGui::TextColored(ImVec4(0.85f, 0.22f, 0.20f, 1.f), "%s", e.error.c_str());

				// Go to line
				if (ImGui::BeginPopup("Go to line"))
				{
					if (ImGui::IsWindowAppearing())
						ImGui::SetKeyboardFocusHere();
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
					if (ImGui::InputTextWithHint("##goto", "Line", e.goto_line, sizeof(e.goto_line),
					                             ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue))
					{
						GoToLine(e, std::atoi(e.goto_line));
						e.goto_line[0] = '\0';
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}

				const bool save_requested = focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false);

				// The text editor, zoomed while it renders only.
				const float status_height = ImGui::GetFrameHeightWithSpacing();
				const std::string before = e.text.GetText();
				const float previous_scale = io.FontGlobalScale;
				io.FontGlobalScale = previous_scale * e.zoom;
				e.text.Render("##text", ImVec2(0.f, std::max(80.f, ImGui::GetContentRegionAvail().y - status_height)), true);
				io.FontGlobalScale = previous_scale;
				AutoCloseBrackets(e, before);

				StatusBar(e);

				if (save_requested)
					Save(e);
			}
			ImGui::End();

			// Unsaved file being closed.
			if (e.ask_close)
			{
				const std::string popup = "Save changes?###close:" + e.path.generic_string();
				ImGui::OpenPopup(popup.c_str());
				if (ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				{
					ImGui::Text("%s has unsaved changes.", e.path.filename().string().c_str());
					if (ImGui::Button("Save"))
					{
						if (Save(e))
							e.open = false;
						e.ask_close = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					if (ImGui::Button("Don't save"))
					{
						e.open = false;
						e.ask_close = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel"))
					{
						e.ask_close = false;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}
		}
	}


	bool CanOpen(const fs::path& path)
	{
		const std::string e = Lower(path.extension().string());
		for (const char* ext : { ".js", ".py", ".json", ".xml", ".txt", ".md", ".cfg", ".ini", ".glsl", ".vert", ".frag",
		                         ".cpp", ".h", ".hpp", ".inl", ".c", ".csv" })
			if (e == ext)
				return true;
		return false;
	}


	void Open(const fs::path& path, int line)
	{
		std::error_code ec;
		const fs::path absolute = fs::weakly_canonical(fs::absolute(path, ec), ec);

		for (auto& editor : g_editors)
		{
			if (fs::equivalent(editor->path, absolute, ec) || editor->path == absolute)
			{
				editor->focus_next_frame = true;
				if (line > 0)
					GoToLine(*editor, line);
				return;
			}
		}

		auto editor = std::make_unique<Editor>();
		editor->path = absolute;
		editor->text.SetLanguageDefinition(LanguageFor(absolute));
		editor->text.SetTabSize(4);
		editor->text.SetShowWhitespaces(false);
		ApplyPalette(*editor);
		Load(*editor);
		if (line > 0)
			GoToLine(*editor, line);
		g_editors.push_back(std::move(editor));
	}


	void DrawAll(ImGuiID dock_id)
	{
		for (auto& editor : g_editors)
		{
			ApplyPalette(*editor);   // follows a theme change
			DrawEditor(*editor, dock_id);
		}
		g_editors.erase(std::remove_if(g_editors.begin(), g_editors.end(),
		                               [](const std::unique_ptr<Editor>& e) { return !e->open; }),
		                g_editors.end());
	}


	bool HasKeyboardFocus()
	{
		if (g_editors.empty())
			return false;
		if (!ImGui::GetIO().WantCaptureKeyboard)
			return false;
		// The focused window or one of its parents (the text area is a child
		// window ; a docked editor's parent is the dock host).
		for (const ImGuiWindow* w = ImGui::GetCurrentContext()->NavWindow; w; w = w->ParentWindow)
		{
			if (std::strstr(w->Name, "###script:"))
				return true;
		}
		return false;
	}


	namespace
	{
		// `path` is `root` or inside it : the part after root, otherwise empty.
		bool RelativeInside(const fs::path& path, const fs::path& root, fs::path& relative)
		{
			std::error_code ec;
			const fs::path p = fs::weakly_canonical(fs::absolute(path, ec), ec);
			const fs::path r = fs::weakly_canonical(fs::absolute(root, ec), ec);
			auto pi = p.begin();
			for (auto ri = r.begin(); ri != r.end(); ++ri, ++pi)
			{
				if (ri->empty())
					continue;
				if (pi == p.end() || *pi != *ri)
					return false;
			}
			relative.clear();
			for (; pi != p.end(); ++pi)
				relative /= *pi;
			return true;
		}
	}


	void PathMoved(const fs::path& from, const fs::path& to)
	{
		std::error_code ec;
		const fs::path destination = fs::weakly_canonical(fs::absolute(to, ec), ec);
		for (auto& editor : g_editors)
		{
			fs::path relative;
			if (!RelativeInside(editor->path, from, relative))
				continue;
			editor->path = relative.empty() ? destination : destination / relative;
			editor->text.SetLanguageDefinition(LanguageFor(editor->path));
			editor->write_time = fs::last_write_time(editor->path, ec);
			editor->has_write_time = !ec;
			editor->focus_next_frame = true;   // new title = new window
		}
	}


	void PathDeleted(const fs::path& target)
	{
		for (auto& editor : g_editors)
		{
			fs::path relative;
			if (RelativeInside(editor->path, target, relative))
				editor->open = false;   // removed at the next DrawAll
		}
	}


	bool HasUnsavedChanges()
	{
		return std::any_of(g_editors.begin(), g_editors.end(), [](const auto& e) { return e->dirty; });
	}


	void SaveAll()
	{
		for (auto& editor : g_editors)
			if (editor->dirty)
				Save(*editor);
	}
}
