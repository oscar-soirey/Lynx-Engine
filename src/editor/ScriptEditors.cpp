#include "ScriptEditors.h"

#include "EditorIcons.h"
#include "JsonEditor.h"
#include "JsLanguage.h"
#include "EditorFonts.h"
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
			// .json : visual editor ("Editor") or the text ("Raw")
			bool is_json = false;
			bool visual = false;
			json_editor::Json json;
			json_editor::State json_state;
			bool json_stale = true;          // the text changed : parse it again
			bool json_ok = false;
			bool json_comments = false;
			std::string json_error;
			int json_error_line = 0;
			std::string json_source;         // text the document was read from / written to
			std::vector<std::string> visual_undo;
			std::vector<std::string> visual_redo;
			// .js : language service
			std::unique_ptr<js_language::Document> lang;
			unsigned lang_version = ~0u;     // text version the analysis was made from
			js_language::Completion completion;
			bool completion_open = false;
			int completion_selected = 0;
			bool completion_scroll = false;
			bool accept_completion = false;
			js_language::Signature signature;
			unsigned signature_version = ~0u;
			TextEditor::Coordinates signature_cursor;
			ImVec2 hover_mouse;
			double hover_time = 0.0;
			bool link_valid = false;
			TextEditor::Coordinates link_start, link_end;
			int problem_index = -1;          // status bar : last problem visited
			// closing brackets inserted by AutoCloseBrackets (on the cursor line) :
			// typing one steps over it
			std::vector<char> auto_closers;
			int auto_closers_line = -1;
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
			e.json_stale = true;
			e.visual_undo.clear();
			e.visual_redo.clear();
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
			// Soft selection, a barely visible current line
			palette[static_cast<int>(TextEditor::PaletteIndex::Selection)] = light ? IM_COL32(70, 130, 230, 80) : IM_COL32(90, 150, 240, 95);
			palette[static_cast<int>(TextEditor::PaletteIndex::CurrentLineFill)] = light ? IM_COL32(0, 0, 0, 16) : IM_COL32(255, 255, 255, 14);
			palette[static_cast<int>(TextEditor::PaletteIndex::CurrentLineFillInactive)] = light ? IM_COL32(0, 0, 0, 8) : IM_COL32(255, 255, 255, 7);
			palette[static_cast<int>(TextEditor::PaletteIndex::Cursor)] = light ? IM_COL32(30, 30, 40, 255) : IM_COL32(230, 230, 240, 255);
			e.text.SetPalette(palette);
		}

		// JavaScript : syntax + class structure, a moment after the last key.
		void UpdateLanguage(Editor& e, bool check_syntax);

		void CheckScript(Editor& e)
		{
			if (e.lang)
			{
				e.check_pending = false;
				UpdateLanguage(e, true);
				return;
			}
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

		// ---------------------------------------------------------------------
		// JSON : "Editor" view
		// ---------------------------------------------------------------------

		void SyncJson(Editor& e)
		{
			if (!e.json_stale)
				return;
			e.json_stale = false;
			e.json_source = e.text.GetText();
			e.json_ok = json_editor::Parse(e.json_source, e.json, e.json_error, &e.json_comments);
			e.json_error_line = 0;
			if (!e.json_ok)
			{
				static const std::regex position(R"(line (\d+))");
				std::smatch match;
				if (std::regex_search(e.json_error, match, position))
					e.json_error_line = std::stoi(match.str(1));
			}
		}

		void UpdateDirty(Editor& e)
		{
			e.dirty = e.json_source != e.saved_text && e.json_source != e.saved_text + "\n";
		}

		// The visual editor changed the document : new text (one undo step).
		void CommitJson(Editor& e)
		{
			e.visual_undo.push_back(e.json_source);
			e.visual_redo.clear();
			e.json_source = json_editor::Dump(e.json, e.json_source);
			e.text.SetText(e.json_source);
			UpdateDirty(e);
		}

		void VisualUndo(Editor& e, bool redo)
		{
			std::vector<std::string>& from = redo ? e.visual_redo : e.visual_undo;
			std::vector<std::string>& to = redo ? e.visual_undo : e.visual_redo;
			if (from.empty())
				return;
			to.push_back(e.json_source);
			e.json_source = from.back();
			from.pop_back();
			e.text.SetText(e.json_source);
			std::string error;
			e.json_ok = json_editor::Parse(e.json_source, e.json, error, &e.json_comments);
			UpdateDirty(e);
		}

		void SetVisual(Editor& e, bool visual)
		{
			if (e.visual == visual)
				return;
			e.visual = visual;
			e.json_stale = true;   // the raw text may have been edited
		}

		/** "Editor | Raw" in the right corner of the toolbar. */
		void ModeSwitch(Editor& e)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float w1 = ImGui::CalcTextSize("Editor").x + style.FramePadding.x * 2.f;
			const float w2 = ImGui::CalcTextSize("Raw").x + style.FramePadding.x * 2.f;
			const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			ImGui::SameLine();
			ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - w1 - w2));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, style.ItemSpacing.y));
			for (int i = 0; i < 2; ++i)
			{
				const bool visual = i == 0;
				const bool selected = e.visual == visual;
				if (i > 0)
					ImGui::SameLine();
				if (selected)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				}
				if (ImGui::Button(visual ? "Editor" : "Raw"))
					SetVisual(e, visual);
				if (selected)
					ImGui::PopStyleColor(2);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(visual ? "Visual editor : fields, values, colors" : "The text of the file");
			}
			ImGui::PopStyleVar();
		}

		void DrawJsonView(Editor& e, float height)
		{
			SyncJson(e);
			ImGui::BeginChild("##json_view", ImVec2(0.f, height), ImGuiChildFlags_Borders);
			if (!e.json_ok)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.22f, 0.20f, 1.f));
				ImGui::TextWrapped("This file is not valid JSON : %s", e.json_error.c_str());
				ImGui::PopStyleColor();
				ImGui::Spacing();
				if (ImGui::Button(e.json_error_line > 0 ? "Fix it in the raw text (go to the line)" : "Fix it in the raw text"))
				{
					SetVisual(e, false);
					if (e.json_error_line > 0)
						GoToLine(e, e.json_error_line);
				}
			}
			else if (json_editor::Draw(e.json, e.json_state))
				CommitJson(e);
			ImGui::EndChild();
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
			const char typed = after[at];

			// ")" typed just before a ")" that was inserted for us : step over it
			const auto cursor_now = e.text.GetCursorPosition();
			if (cursor_now.mLine != e.auto_closers_line)
				e.auto_closers.clear();
			// (the first difference is ambiguous when ")" is typed before ")" : look at the cursor)
			const std::string cursor_line = e.text.GetLineText(cursor_now.mLine);
			const int cursor_index = e.text.GetLineIndexOf(cursor_now);
			const char typed_char = cursor_index > 0 && cursor_index <= static_cast<int>(cursor_line.size()) ? cursor_line[cursor_index - 1] : 0;
			if (!e.auto_closers.empty() && typed_char == e.auto_closers.back() && cursor_index < static_cast<int>(cursor_line.size()) &&
			    cursor_line[cursor_index] == typed_char)
			{
				e.text.ReplaceRange(cursor_now, TextEditor::Coordinates(cursor_now.mLine, cursor_now.mColumn + 1), "");
				e.auto_closers.pop_back();
				return;
			}

			const char* closing = nullptr;
			switch (typed)
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
				e.auto_closers.push_back(closing[0]);
				e.auto_closers_line = cursor.mLine;
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
			ImGui::BeginDisabled(e.visual ? e.visual_undo.empty() : !e.text.CanUndo());
			if (icons::Button("Undo", icons::Icon::Undo))
			{
				if (e.visual)
					VisualUndo(e, false);
				else
					e.text.Undo();
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Undo (Ctrl+Z) - Redo : Ctrl+Y");
			if (!e.visual)
			{
				ImGui::SameLine();
				if (icons::Button("Find", icons::Icon::Search, icons::kThemeTint, e.show_find))
				{
					e.show_find = !e.show_find;
					e.focus_find = e.show_find;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Find (Ctrl+F), next F3, previous Shift+F3");
			}

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

			if (e.is_json)
				ModeSwitch(e);
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
			if (e.visual)
			{
				if (e.json_ok)
					ImGui::TextDisabled("%d values   JSON", json_editor::CountValues(e.json));
				else
					ImGui::TextDisabled("JSON");
				if (e.json_ok && e.json_comments)
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.80f, 0.45f, 0.05f, 1.f), "The comments (//) are removed when a value is changed here.");
				}
				return;
			}
			const auto cursor = e.text.GetCursorPosition();
			ImGui::TextDisabled("Ln %d, Col %d   %d lines   %s", cursor.mLine + 1, cursor.mColumn + 1,
			                    e.text.GetTotalLines(), e.text.GetLanguageDefinition().mName.c_str());
			if (e.lang)
			{
				// Problems : errors / warnings, a click (or F8) goes to the next one
				const auto& problems = e.text.GetDiagnostics();
				int errors = 0, warnings = 0;
				for (const auto& d : problems)
				{
					errors += d.mSeverity == TextEditor::DiagnosticSeverity::Error;
					warnings += d.mSeverity == TextEditor::DiagnosticSeverity::Warning;
				}
				auto next_problem = [&]()
				{
					std::vector<const TextEditor::Diagnostic*> list;
					for (const auto& d : problems)
						if (d.mSeverity != TextEditor::DiagnosticSeverity::Hint)
							list.push_back(&d);
					if (list.empty())
						return;
					// the first one after the cursor
					const auto at = e.text.GetCursorPosition();
					const TextEditor::Diagnostic* target = list.front();
					for (const auto* d : list)
						if (at < d->mStart)
						{
							target = d;
							break;
						}
					e.text.SetCursorPosition(target->mStart);
					e.text.SetSelection(target->mStart, target->mEnd);
				};
				ImGui::SameLine(0.f, ImGui::GetFontSize());
				ImGui::TextColored(errors ? ImVec4(0.85f, 0.22f, 0.20f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
				                   "%d error%s", errors, errors == 1 ? "" : "s");
				const bool click1 = ImGui::IsItemClicked();
				ImGui::SameLine();
				ImGui::TextColored(warnings ? ImVec4(0.80f, 0.55f, 0.05f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
				                   "%d warning%s", warnings, warnings == 1 ? "" : "s");
				const bool click2 = ImGui::IsItemClicked();
				if ((click1 || click2) || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				                           ImGui::IsKeyPressed(ImGuiKey_F8, false)))
					next_problem();
				if (ImGui::IsItemHovered() || (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())))
					ImGui::SetTooltip("Click (or F8) : next problem\nCtrl+Space : completion   F12 / Ctrl+click : go to definition");
				if (!e.check_error.empty())
				{
					ImGui::SameLine(0.f, ImGui::GetFontSize());
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.22f, 0.20f, 1.f));
					const std::string label = "Line " + std::to_string(e.check_line) + " : " + e.check_error;
					ImGui::TextUnformatted(label.c_str());
					ImGui::PopStyleColor();
					if (ImGui::IsItemClicked() && e.check_line > 0)
						GoToLine(e, e.check_line);
				}
				else if (errors == 0 && warnings == 0)
				{
					ImGui::SameLine(0.f, ImGui::GetFontSize());
					ImGui::TextColored(ImVec4(0.25f, 0.55f, 0.28f, 1.f), "No problem");
				}
				return;
			}
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

		// ---------------------------------------------------------------------
		// JavaScript language service (diagnostics, completion, hover...)
		// ---------------------------------------------------------------------

		fs::path g_project_root;
		double g_environment_time = -1000.0;

		bool IsJs(const Editor& e)
		{
			return Lower(e.path.extension().string()) == ".js";
		}

		TextEditor::Coordinates ToCoordinates(const Editor& e, int line, int index)
		{
			line = std::clamp(line, 0, std::max(0, e.text.GetTotalLines() - 1));
			return TextEditor::Coordinates(line, e.text.GetColumnOf(line, index));
		}

		int ToIndex(const Editor& e, const TextEditor::Coordinates& c)
		{
			return e.text.GetLineIndexOf(c);
		}

		// Engine globals / members and the project's classes : every few seconds.
		void RefreshEnvironment(bool force)
		{
			const double now = ImGui::GetTime();
			if (!force && now - g_environment_time < 5.0)
				return;
			g_environment_time = now;
			js_language::SetProjectRoot(g_project_root);
			js_language::SetSyntaxChecker([](const std::string& code, const std::string& name, std::string& error)
			{
				return lynx::CheckScriptSyntax(code, name.c_str(), error);
			});
			js_language::RefreshEnvironment([](const std::string& code, std::string& json)
			{
				std::string error;
				return lynx::EvaluateScript(code.c_str(), json, error, "<language service>");
			});
		}

		void UpdateLanguage(Editor& e, bool check_syntax)
		{
			if (!e.lang)
				return;
			e.lang->Update(e.text.GetTextLines(), e.path, check_syntax);
			e.lang_version = e.text.GetTextVersion();
			std::vector<TextEditor::Diagnostic> diagnostics;
			for (const js_language::Diagnostic& d : e.lang->Diagnostics())
			{
				TextEditor::Diagnostic t;
				t.mStart = ToCoordinates(e, d.line, d.start);
				t.mEnd = ToCoordinates(e, d.end_line, d.end);
				t.mSeverity = static_cast<TextEditor::DiagnosticSeverity>(static_cast<int>(d.severity));
				t.mMessage = d.message;
				diagnostics.push_back(t);
			}
			e.text.SetDiagnostics(diagnostics);
			e.text.SetErrorMarkers({});
			// status bar : the first error
			e.check_error.clear();
			e.check_line = 0;
			for (const js_language::Diagnostic& d : e.lang->Diagnostics())
				if (d.severity == js_language::Severity::Error)
				{
					e.check_error = d.message;
					e.check_line = d.line + 1;
					break;
				}
		}

		ImU32 KindColor(js_language::Kind kind)
		{
			using K = js_language::Kind;
			switch (kind)
			{
			case K::Function: case K::Method: return IM_COL32(176, 104, 214, 255);
			case K::Variable: case K::Parameter: return IM_COL32(74, 140, 220, 255);
			case K::Constant: return IM_COL32(40, 150, 150, 255);
			case K::Class: return IM_COL32(222, 140, 50, 255);
			case K::Property: return IM_COL32(60, 160, 190, 255);
			case K::Namespace: return IM_COL32(200, 170, 40, 255);
			case K::Global: return IM_COL32(80, 160, 90, 255);
			case K::Keyword: return IM_COL32(140, 140, 150, 255);
			default: return IM_COL32(120, 140, 120, 255);
			}
		}

		const char* KindGlyph(js_language::Kind kind)
		{
			using K = js_language::Kind;
			switch (kind)
			{
			case K::Function: return "f";
			case K::Method: return "m";
			case K::Variable: return "v";
			case K::Parameter: return "p";
			case K::Constant: return "c";
			case K::Class: return "C";
			case K::Property: return "o";
			case K::Namespace: return "N";
			case K::Global: return "g";
			case K::Keyword: return "k";
			default: return "s";
			}
		}

		void OpenCompletion(Editor& e, bool explicit_request)
		{
			if (!e.lang)
				return;
			if (e.lang_version != e.text.GetTextVersion())
				UpdateLanguage(e, false);
			const auto cursor = e.text.GetCursorPosition();
			e.completion = e.lang->Complete(cursor.mLine, ToIndex(e, cursor), explicit_request);
			e.completion_open = !e.completion.items.empty();
			e.completion_selected = 0;
			e.completion_scroll = true;
		}

		void AcceptCompletion(Editor& e)
		{
			if (!e.completion_open || e.completion.items.empty())
				return;
			const js_language::CompletionItem& item =
				e.completion.items[std::clamp(e.completion_selected, 0, static_cast<int>(e.completion.items.size()) - 1)];
			const auto start = ToCoordinates(e, e.completion.line, e.completion.start);
			// the word after the cursor is replaced too (completing in the middle of a name)
			const auto end = ToCoordinates(e, e.completion.line, e.completion.end);
			e.text.ReplaceRange(start, end, item.label);
			e.completion_open = false;
			UpdateLanguage(e, false);
		}

		// Keys of the completion list : taken before the text editor sees them.
		void CompletionKeys(Editor& e)
		{
			if (!e.completion_open)
				return;
			e.text.SuppressKeys({ ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Tab,
			                      ImGuiKey_Escape, ImGuiKey_PageUp, ImGuiKey_PageDown });
			const int count = static_cast<int>(e.completion.items.size());
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			{
				e.completion_selected = (e.completion_selected + 1) % std::max(1, count);
				e.completion_scroll = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			{
				e.completion_selected = (e.completion_selected - 1 + count) % std::max(1, count);
				e.completion_scroll = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
			{
				e.completion_selected = std::min(count - 1, e.completion_selected + 8);
				e.completion_scroll = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
			{
				e.completion_selected = std::max(0, e.completion_selected - 8);
				e.completion_scroll = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				e.completion_open = false;
			else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
			         ImGui::IsKeyPressed(ImGuiKey_Tab, false))
				e.accept_completion = true;   // after the text editor (it may have typed a letter this frame)
		}

		// A label with the typed letters in bold.
		void DrawMatchedLabel(ImDrawList* dl, ImVec2 pos, const std::string& label, const std::string& typed, ImU32 color, ImU32 strong)
		{
			ImFont* regular = ImGui::GetFont();
			ImFont* bold = fonts::CodeBold() ? fonts::CodeBold() : regular;
			const float size = ImGui::GetFontSize();
			size_t k = 0;
			const std::string low_typed = Lower(typed);
			for (size_t i = 0; i < label.size(); ++i)
			{
				const char c[2] = { label[i], 0 };
				const bool match = k < low_typed.size() &&
				                   std::tolower(static_cast<unsigned char>(label[i])) == static_cast<unsigned char>(low_typed[k]);
				if (match)
					++k;
				dl->AddText(match ? bold : regular, size, pos, match ? strong : color, c);
				pos.x += regular->CalcTextSizeA(size, FLT_MAX, 0.f, c).x;
			}
		}

		void DrawCompletion(Editor& e)
		{
			if (!e.completion_open || e.completion.items.empty())
				return;
			const ImGuiStyle& style = ImGui::GetStyle();
			const float line_h = e.text.GetLineHeight();
			const ImVec2 anchor = e.text.CoordinatesToScreenPos(ToCoordinates(e, e.completion.line, e.completion.start));
			const int count = static_cast<int>(e.completion.items.size());
			e.completion_selected = std::clamp(e.completion_selected, 0, count - 1);

			ImGui::PushFont(fonts::Code(), ImGui::GetCurrentContext()->FontSizeBase * fonts::CodeScale());
			const float row_h = std::floor(ImGui::GetFontSize() * 1.45f);
			const int visible_rows = std::min(count, 10);
			const float list_w = std::floor(ImGui::GetFontSize() * 26.f);
			const float list_h = row_h * visible_rows + style.WindowPadding.y * 2.f;

			// below the line, or above when there is no room
			ImVec2 pos(anchor.x - ImGui::GetFontSize() * 1.6f, anchor.y + line_h + 2.f);
			const ImVec2 display = ImGui::GetMainViewport()->Size;
			if (pos.y + list_h > ImGui::GetMainViewport()->Pos.y + display.y)
				pos.y = anchor.y - list_h - 2.f;
			ImGui::SetNextWindowPos(pos);
			ImGui::SetNextWindowSize(ImVec2(list_w, list_h));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, 4.f));
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
			                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
			                               ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;
			const std::string id = "##completion" + e.path.generic_string();
			if (ImGui::Begin(id.c_str(), nullptr, flags))
			{
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const std::string typed = e.completion.in_string || e.completion.items.empty() ? std::string() : [&]()
				{
					const auto cursor = e.text.GetCursorPosition();
					const std::string line = e.text.GetLineText(e.completion.line);
					const int end = std::clamp(ToIndex(e, cursor), e.completion.start, static_cast<int>(line.size()));
					return line.substr(e.completion.start, end - e.completion.start);
				}();
				const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
				const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
				const ImU32 strong = ImGui::GetColorU32(ImGuiCol_PlotLinesHovered);
				for (int i = 0; i < count; ++i)
				{
					const js_language::CompletionItem& item = e.completion.items[i];
					ImGui::PushID(i);
					const ImVec2 row_min = ImGui::GetCursorScreenPos();
					const bool selected = i == e.completion_selected;
					if (ImGui::InvisibleButton("##item", ImVec2(ImGui::GetContentRegionAvail().x, row_h)))
					{
						e.completion_selected = i;
						e.accept_completion = true;
					}
					if (ImGui::IsItemHovered())
						dl->AddRectFilled(row_min, ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.5f), 4.f);
					if (selected)
					{
						dl->AddRectFilled(row_min, ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Header), 4.f);
						if (e.completion_scroll)
						{
							ImGui::SetScrollHereY(0.5f);
							e.completion_scroll = false;
						}
					}
					// kind badge
					const float badge = std::floor(row_h * 0.72f);
					const ImVec2 b0(row_min.x + 3.f, row_min.y + (row_h - badge) * 0.5f);
					const ImVec2 b1(b0.x + badge, b0.y + badge);
					dl->AddRectFilled(b0, b1, KindColor(item.kind), 4.f);
					const char* glyph = KindGlyph(item.kind);
					const ImVec2 gs = ImGui::CalcTextSize(glyph);
					dl->AddText(ImVec2(std::floor(b0.x + (badge - gs.x) * 0.5f), std::floor(b0.y + (badge - gs.y) * 0.5f)),
					            IM_COL32(255, 255, 255, 255), glyph);
					// label (typed letters in bold), detail on the right
					const float text_y = std::floor(row_min.y + (row_h - ImGui::GetFontSize()) * 0.5f);
					DrawMatchedLabel(dl, ImVec2(b1.x + 7.f, text_y), item.label, typed, text, strong);
					if (!item.detail.empty() && item.detail != item.label)
					{
						std::string detail = item.detail;
						const float max_w = list_w * 0.45f;
						while (!detail.empty() && ImGui::CalcTextSize(detail.c_str()).x > max_w)
							detail.pop_back();
						if (detail.size() < item.detail.size() && detail.size() > 2)
							detail = detail.substr(0, detail.size() - 2) + "..";
						const float w = ImGui::CalcTextSize(detail.c_str()).x;
						const float label_end = b1.x + 7.f + ImGui::CalcTextSize(item.label.c_str()).x + 12.f;
						const float x = std::max(label_end, ImGui::GetItemRectMax().x - w - 6.f);
						if (x + w <= ImGui::GetItemRectMax().x + 1.f)
							dl->AddText(ImVec2(x, text_y), dim, detail.c_str());
					}
					ImGui::PopID();
				}
				const ImVec2 list_min = ImGui::GetWindowPos();
				const ImVec2 list_max(list_min.x + ImGui::GetWindowWidth(), list_min.y + ImGui::GetWindowHeight());
				ImGui::End();

				// Documentation of the selected item, next to the list
				const js_language::CompletionItem& sel = e.completion.items[e.completion_selected];
				const bool useful_detail = !sel.detail.empty() && sel.detail != sel.label && sel.detail != sel.label + "()";
				if (!sel.doc.empty() || useful_detail)
				{
					const float doc_w = std::floor(ImGui::GetFontSize() * 22.f);
					ImVec2 doc_pos(list_max.x + 4.f, list_min.y);
					if (doc_pos.x + doc_w > ImGui::GetMainViewport()->Pos.x + display.x)
						doc_pos.x = list_min.x - doc_w - 4.f;
					ImGui::SetNextWindowPos(doc_pos);
					ImGui::SetNextWindowSizeConstraints(ImVec2(doc_w, 0.f), ImVec2(doc_w, list_h * 1.5f));
					const std::string doc_id = id + "_doc";
					if (ImGui::Begin(doc_id.c_str(), nullptr, flags | ImGuiWindowFlags_AlwaysAutoResize))
					{
						ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
						if (useful_detail || !sel.detail.empty())
						{
							ImGui::PushTextWrapPos(doc_w - 10.f);
							ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindColor(sel.kind)), "%s", sel.detail.c_str());
							ImGui::PopTextWrapPos();
						}
						if (!sel.doc.empty())
						{
							if (!sel.detail.empty())
								ImGui::Separator();
							ImGui::PushTextWrapPos(doc_w - 10.f);
							ImGui::TextUnformatted(sel.doc.c_str());
							ImGui::PopTextWrapPos();
						}
					}
					ImGui::End();
				}
			}
			else
				ImGui::End();
			ImGui::PopStyleVar(2);
			ImGui::PopFont();
		}

		// Parameters of the call around the cursor, above the line.
		void DrawSignature(Editor& e)
		{
			if (!e.lang || !e.text.IsFocused())
				return;
			const auto cursor = e.text.GetCursorPosition();
			if (e.signature_version != e.text.GetTextVersion() || e.signature_cursor != cursor)
			{
				if (e.lang_version != e.text.GetTextVersion())
					UpdateLanguage(e, false);
				e.signature = e.lang->SignatureAt(cursor.mLine, ToIndex(e, cursor));
				e.signature_version = e.text.GetTextVersion();
				e.signature_cursor = cursor;
			}
			if (!e.signature.valid)
				return;
			ImGui::PushFont(fonts::Code(), ImGui::GetCurrentContext()->FontSizeBase * fonts::CodeScale());
			const ImVec2 at = e.text.CoordinatesToScreenPos(cursor);
			ImGui::SetNextWindowPos(ImVec2(at.x - ImGui::GetFontSize(), at.y - 4.f), ImGuiCond_Always, ImVec2(0.f, 1.f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
			                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
			                               ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoInputs |
			                               ImGuiWindowFlags_AlwaysAutoResize;
			const std::string id = "##signature" + e.path.generic_string();
			if (ImGui::Begin(id.c_str(), nullptr, flags))
			{
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
				const js_language::Signature& s = e.signature;
				ImDrawList* dl = ImGui::GetWindowDrawList();
				ImVec2 pos = ImGui::GetCursorScreenPos();
				const float size = ImGui::GetFontSize();
				ImFont* regular = ImGui::GetFont();
				ImFont* bold = fonts::CodeBold() ? fonts::CodeBold() : regular;
				const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
				const ImU32 active = IM_COL32(176, 104, 214, 255);
				std::pair<int, int> range(-1, -1);
				if (s.active >= 0 && s.active < static_cast<int>(s.params.size()))
					range = s.params[s.active];
				float width = 0.f;
				for (int i = 0; i < static_cast<int>(s.label.size()); ++i)
				{
					const char c[2] = { s.label[i], 0 };
					const bool in_active = i >= range.first && i < range.second;
					dl->AddText(in_active ? bold : regular, size, ImVec2(pos.x + width, pos.y), in_active ? active : text, c);
					if (in_active)
						dl->AddLine(ImVec2(pos.x + width, pos.y + size + 1.f),
						            ImVec2(pos.x + width + regular->CalcTextSizeA(size, FLT_MAX, 0.f, c).x, pos.y + size + 1.f), active);
					width += regular->CalcTextSizeA(size, FLT_MAX, 0.f, c).x;
				}
				ImGui::Dummy(ImVec2(width, size + 3.f));
				if (!s.doc.empty())
				{
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
					ImGui::TextDisabled("%s", s.doc.c_str());
					ImGui::PopTextWrapPos();
				}
			}
			ImGui::End();
			ImGui::PopStyleVar();
			ImGui::PopFont();
		}

		// Mouse resting on the code : type / documentation / problems there.
		void DrawHover(Editor& e)
		{
			TextEditor::Coordinates at;
			const bool over = e.text.GetMouseCoordinates(at) && !e.completion_open && !ImGui::IsMouseDown(ImGuiMouseButton_Left);
			const ImVec2 mouse = ImGui::GetMousePos();
			if (!over || mouse.x != e.hover_mouse.x || mouse.y != e.hover_mouse.y)
			{
				e.hover_mouse = mouse;
				e.hover_time = ImGui::GetTime();
				if (!over)
					return;
			}
			if (ImGui::GetTime() - e.hover_time < 0.45)
				return;

			// problems under the mouse
			std::vector<const TextEditor::Diagnostic*> problems;
			for (const TextEditor::Diagnostic& d : e.text.GetDiagnostics())
			{
				TextEditor::Coordinates end = d.mEnd == d.mStart ? TextEditor::Coordinates(d.mEnd.mLine, d.mEnd.mColumn + 1) : d.mEnd;
				if (!(at < d.mStart) && at < end)
					problems.push_back(&d);
			}
			js_language::Hover hover;
			if (e.lang)
			{
				if (e.lang_version != e.text.GetTextVersion())
					UpdateLanguage(e, false);
				hover = e.lang->HoverAt(at.mLine, ToIndex(e, at));
			}
			if (!hover.valid && problems.empty())
				return;

			ImGui::PushFont(fonts::Code(), ImGui::GetCurrentContext()->FontSizeBase * fonts::CodeScale() * 2.f);   // big : easy to read
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
			if (ImGui::BeginTooltip())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.f);
				for (const TextEditor::Diagnostic* d : problems)
				{
					const ImVec4 color = d->mSeverity == TextEditor::DiagnosticSeverity::Error ? ImVec4(0.91f, 0.25f, 0.22f, 1.f) :
					                     d->mSeverity == TextEditor::DiagnosticSeverity::Warning ? ImVec4(0.87f, 0.63f, 0.09f, 1.f) :
					                     d->mSeverity == TextEditor::DiagnosticSeverity::Info ? ImVec4(0.27f, 0.55f, 0.90f, 1.f) :
					                     ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
					const char* label = d->mSeverity == TextEditor::DiagnosticSeverity::Error ? "Error" :
					                    d->mSeverity == TextEditor::DiagnosticSeverity::Warning ? "Warning" :
					                    d->mSeverity == TextEditor::DiagnosticSeverity::Info ? "Info" : "Hint";
					ImGui::TextColored(color, "%s", label);
					ImGui::SameLine();
					ImGui::TextUnformatted(d->mMessage.c_str());
				}
				if (hover.valid)
				{
					if (!problems.empty())
						ImGui::Separator();
					ImFont* bold = fonts::CodeBold();
					if (bold)
						ImGui::PushFont(bold, 0.f);
					ImGui::TextColored(ImVec4(0.45f, 0.30f, 0.70f, 1.f), "%s", hover.signature.c_str());
					if (bold)
						ImGui::PopFont();
					if (!hover.doc.empty())
						ImGui::TextUnformatted(hover.doc.c_str());
				}
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::PopStyleVar();
			ImGui::PopFont();
		}

		void GoToDefinition(Editor& e, const TextEditor::Coordinates& at);

		// After the text editor : typed letters open / update the list, Ctrl + click...
		void AfterTextEditor(Editor& e, unsigned typed, bool focused)
		{
			if (!e.lang)
				return;
			ImGuiIO& io = ImGui::GetIO();
			const bool changed = e.text.GetTextVersion() != e.lang_version;

			if (e.accept_completion)
			{
				e.accept_completion = false;
				AcceptCompletion(e);
			}
			else if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))
				OpenCompletion(e, true);
			else if (changed && typed != 0)
			{
				const auto cursor = e.text.GetCursorPosition();
				const char c = typed < 128 ? static_cast<char>(typed) : 'a';
				if (e.completion_open)
				{
					// still in the word : filter again ; otherwise close (or a new list after ".")
					const bool word = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || (e.completion.in_string && c != '"' && c != '\'');
					if (word)
						OpenCompletion(e, false);
					else
						e.completion_open = false;
				}
				if (!e.completion_open)
				{
					UpdateLanguage(e, false);
					if (e.lang->ShouldTrigger(cursor.mLine, ToIndex(e, cursor), c))
						OpenCompletion(e, false);
				}
			}
			else if (changed && e.completion_open)
				OpenCompletion(e, false);   // Backspace...

			// The cursor left the word : close.
			if (e.completion_open)
			{
				const auto cursor = e.text.GetCursorPosition();
				const int index = ToIndex(e, cursor);
				if (cursor.mLine != e.completion.line || index < e.completion.start || !focused)
					e.completion_open = false;
			}

			// Ctrl + hover : underline what can be followed ; Ctrl + click / F12 : go there
			TextEditor::Coordinates at;
			e.link_valid = false;
			if (io.KeyCtrl && e.text.GetMouseCoordinates(at))
			{
				const js_language::Location loc = e.lang->DefinitionAt(at.mLine, ToIndex(e, at));
				int start = 0, end = 0;
				if (loc.valid && e.lang->IdentifierAt(at.mLine, ToIndex(e, at), start, end))
				{
					e.link_valid = true;
					e.link_start = ToCoordinates(e, at.mLine, start);
					e.link_end = ToCoordinates(e, at.mLine, end);
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						GoToDefinition(e, at);
				}
			}
			if (focused && ImGui::IsKeyPressed(ImGuiKey_F12, false))
				GoToDefinition(e, e.text.GetCursorPosition());
		}

		void GoToDefinition(Editor& e, const TextEditor::Coordinates& at)
		{
			if (!e.lang)
				return;
			if (e.lang_version != e.text.GetTextVersion())
				UpdateLanguage(e, false);
			const js_language::Location loc = e.lang->DefinitionAt(at.mLine, ToIndex(e, at));
			if (!loc.valid)
				return;
			Editor* target = &e;
			std::error_code ec;
			if (!loc.file.empty() && !fs::equivalent(loc.file, e.path, ec))
			{
				Open(loc.file, loc.line + 1);
				target = nullptr;
				for (auto& other : g_editors)
					if (fs::equivalent(other->path, loc.file, ec))
						target = other.get();
				if (!target)
					return;
				if (target->lang && target->lang_version != target->text.GetTextVersion())
					UpdateLanguage(*target, false);
			}
			const auto start = ToCoordinates(*target, loc.line, loc.start);
			const auto end = ToCoordinates(*target, loc.line, loc.end);
			target->text.SetCursorPosition(start);
			target->text.SetSelection(start, end);
			target->focus_next_frame = true;
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

			// (the text editor is not drawn in the JSON "Editor" view : its
			// "changed" flag only means something in the raw view)
			if (!e.visual && !e.dirty && e.text.IsTextChanged())
				e.dirty = e.text.GetText() != e.saved_text && e.text.GetText() != e.saved_text + "\n";
			if (!e.visual && e.text.IsTextChanged())
			{
				e.last_edit_time = ImGui::GetTime();
				e.check_pending = true;
				e.json_stale = true;
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
				if (focused && e.visual)
				{
					// Undo / redo of the visual editor (not while a field is typed in :
					// the field has its own).
					if (io.KeyCtrl && !ImGui::IsAnyItemActive())
					{
						if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
							VisualUndo(e, false);
						else if (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (ImGui::IsKeyPressed(ImGuiKey_Z, false) && io.KeyShift))
							VisualUndo(e, true);
					}
					if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false))
						e.zoom = 1.f;
				}
				else if (focused)
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
				if (e.show_find && !e.visual)
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
				const float previous_scale = io.FontGlobalScale;
				io.FontGlobalScale = previous_scale * e.zoom;
				const float height = std::max(80.f, ImGui::GetContentRegionAvail().y - status_height);
				if (e.visual)
				{
					DrawJsonView(e, height);
					io.FontGlobalScale = previous_scale;
				}
				else
				{
					// letter typed this frame (the text editor takes the queue)
					unsigned typed = 0;
					if (!io.InputQueueCharacters.empty())
						typed = io.InputQueueCharacters.back();
					if (e.lang && e.text.IsFocused())
						CompletionKeys(e);
					if (e.link_valid)
						e.text.SetLinkUnderline(e.link_start, e.link_end);
					const std::string before = e.text.GetText();
					e.text.Render("##text", ImVec2(0.f, height), true);
					io.FontGlobalScale = previous_scale;
					AutoCloseBrackets(e, before);
					if (e.lang)
					{
						AfterTextEditor(e, typed, e.text.IsFocused());
						DrawCompletion(e);
						if (!e.completion_open)
							DrawSignature(e);
						DrawHover(e);
					}
				}

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
		// JetBrains Mono : keywords in bold, comments in italic
		editor->text.SetFonts(fonts::Code(), fonts::CodeBold(), fonts::CodeItalic(), fonts::CodeBoldItalic(), fonts::CodeScale());
		if (Lower(absolute.extension().string()) == ".js")
		{
			editor->lang = std::make_unique<js_language::Document>();
			editor->text.SetBuiltinTooltips(false);   // the language service shows its own
			RefreshEnvironment(true);
		}
		// .json : opens in the visual editor ("Raw" in the corner for the text).
		editor->is_json = Lower(absolute.extension().string()) == ".json";
		editor->visual = editor->is_json;
		ApplyPalette(*editor);
		Load(*editor);
		if (line > 0)
			GoToLine(*editor, line);
		g_editors.push_back(std::move(editor));
	}


	void SetProjectRoot(const fs::path& root)
	{
		g_project_root = root;
		js_language::SetProjectRoot(root);
	}


	void DrawAll(ImGuiID dock_id)
	{
		if (std::any_of(g_editors.begin(), g_editors.end(), [](const std::unique_ptr<Editor>& e) { return e->lang != nullptr; }))
			RefreshEnvironment(false);
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
			const bool js = Lower(editor->path.extension().string()) == ".js";
			if (js && !editor->lang)
			{
				editor->lang = std::make_unique<js_language::Document>();
				editor->text.SetBuiltinTooltips(false);
			}
			else if (!js && editor->lang)
			{
				editor->lang.reset();
				editor->completion_open = false;
				editor->text.SetDiagnostics({});
				editor->text.SetBuiltinTooltips(true);
			}
			editor->check_pending = true;
			editor->is_json = Lower(editor->path.extension().string()) == ".json";
			if (!editor->is_json)
				editor->visual = false;
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
