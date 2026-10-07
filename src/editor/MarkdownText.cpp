#include "MarkdownText.h"
#include "TextSelection.h"
#include "EditorFonts.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

namespace lynx::editor::markdown
{
	namespace
	{
		// ---------------------------------------------------------------------
		// Inline parsing : **bold**, *italic*, `code`, ~~strike~~, [text](url)
		// ---------------------------------------------------------------------

		enum Flags
		{
			kBold = 1,
			kItalic = 2,
			kCode = 4,
			kStrike = 8,
			kLink = 16,
		};

		struct Span
		{
			std::string text;
			int flags = 0;
			std::string url;
		};

		// Fonts : inside Lynxie's font (EditorFonts.h), **bold** / *italic* use
		// Lynxie's Bold / Italic fonts (none since OpenDyslexic was dropped) and `code` the editor's mono
		// font. Otherwise (or without those files) : the current font, bold
		// drawn twice with a 1px offset, italic in a dimmer color.
		ImFont* FontFor(int flags)
		{
			ImFont* current = ImGui::GetFont();
			if (!fonts::Lynxie() || current != fonts::Lynxie())
				return current;
			if (flags & kCode)
				return fonts::Editor();
			const bool bold = (flags & kBold) != 0;
			const bool italic = (flags & kItalic) != 0;
			if (bold && italic && fonts::LynxieBoldItalic())
				return fonts::LynxieBoldItalic();
			if (bold && fonts::LynxieBold())
				return fonts::LynxieBold();
			if (italic && fonts::LynxieItalic())
				return fonts::LynxieItalic();
			return current;
		}

		bool FakeBold(int flags)
		{
			if (!(flags & kBold))
				return false;
			ImFont* font = FontFor(flags);
			return font != fonts::LynxieBold() && font != fonts::LynxieBoldItalic();
		}

		bool RealItalic(int flags)
		{
			ImFont* font = FontFor(flags);
			return (flags & kItalic) && font && (font == fonts::LynxieItalic() || font == fonts::LynxieBoldItalic());
		}

		float TextWidth(ImFont* font, const char* begin, const char* end = nullptr)
		{
			return font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.f, begin, end).x;
		}

		bool IsWordChar(unsigned char c)
		{
			return std::isalnum(c) || c >= 0x80;
		}

		bool IsSpace(char c)
		{
			return c == ' ' || c == '\t';
		}

		// Closing `marker` after `from` : not right after a space, not inside
		// a `code` span ; "_" must end a word ; "*" alone is not part of "**".
		size_t FindClosing(const std::string& s, size_t from, const char* marker)
		{
			const size_t length = std::strlen(marker);
			for (size_t j = from; j + length <= s.size(); ++j)
			{
				if (s[j] == '\\')
				{
					++j;
					continue;
				}
				if (s[j] == '`' && marker[0] != '`')
				{
					const size_t close = s.find('`', j + 1);
					if (close == std::string::npos)
						return std::string::npos;
					j = close;
					continue;
				}
				if (s.compare(j, length, marker) != 0)
					continue;

				if (length == 1 && j + 1 < s.size() && s[j + 1] == marker[0])
				{
					++j;   // "**" while looking for "*"
					continue;
				}
				if (j == from || IsSpace(s[j - 1]))
					continue;
				if (marker[0] == '_' && j + length < s.size() && IsWordChar(static_cast<unsigned char>(s[j + length])))
					continue;
				return j;
			}
			return std::string::npos;
		}

		void ParseInline(const std::string& s, int flags, const std::string& url, std::vector<Span>& out)
		{
			std::string literal;
			auto flush = [&]()
			{
				if (!literal.empty())
				{
					out.push_back({ literal, flags, url });
					literal.clear();
				}
			};

			size_t i = 0;
			while (i < s.size())
			{
				const char c = s[i];
				const char next = i + 1 < s.size() ? s[i + 1] : '\0';
				const char previous = i > 0 ? s[i - 1] : ' ';

				// \* \_ \` ... : the character itself.
				if (c == '\\' && next && std::ispunct(static_cast<unsigned char>(next)))
				{
					literal += next;
					i += 2;
					continue;
				}

				// `code`
				if (c == '`' && !(flags & kCode))
				{
					const size_t close = s.find('`', i + 1);
					if (close != std::string::npos && close > i + 1)
					{
						flush();
						out.push_back({ s.substr(i + 1, close - i - 1), flags | kCode, url });
						i = close + 1;
						continue;
					}
				}

				// **bold** __bold__ ~~strike~~
				if ((c == '*' || c == '_' || c == '~') && next == c)
				{
					const char marker[3] = { c, c, '\0' };
					const bool can_open = i + 2 < s.size() && !IsSpace(s[i + 2]) &&
					                      (c != '_' || !IsWordChar(static_cast<unsigned char>(previous)));
					const size_t close = can_open ? FindClosing(s, i + 2, marker) : std::string::npos;
					if (close != std::string::npos)
					{
						flush();
						ParseInline(s.substr(i + 2, close - i - 2), flags | (c == '~' ? kStrike : kBold), url, out);
						i = close + 2;
						continue;
					}
				}

				// *italic* _italic_
				if ((c == '*' || c == '_') && next != c)
				{
					const char marker[2] = { c, '\0' };
					const bool can_open = next && !IsSpace(next) &&
					                      (c != '_' || !IsWordChar(static_cast<unsigned char>(previous)));
					const size_t close = can_open ? FindClosing(s, i + 1, marker) : std::string::npos;
					if (close != std::string::npos)
					{
						flush();
						ParseInline(s.substr(i + 1, close - i - 1), flags | kItalic, url, out);
						i = close + 1;
						continue;
					}
				}

				// [text](url)
				if (c == '[')
				{
					const size_t end_text = s.find("](", i + 1);
					const size_t end_url = end_text == std::string::npos ? std::string::npos : s.find(')', end_text + 2);
					if (end_url != std::string::npos && end_text > i + 1)
					{
						flush();
						ParseInline(s.substr(i + 1, end_text - i - 1), flags | kLink,
						            s.substr(end_text + 2, end_url - end_text - 2), out);
						i = end_url + 1;
						continue;
					}
				}

				literal += c;
				++i;
			}
			flush();
		}

		// ---------------------------------------------------------------------
		// Layout : words (no space inside), wrapped to the width
		// ---------------------------------------------------------------------

		struct Piece
		{
			const Span* span = nullptr;
			std::string text;
			float text_width = 0.f;
			float pad_left = 0.f;    // `code` : room for its background
			float pad_right = 0.f;

			float Width() const
			{
				return pad_left + text_width + pad_right + (FakeBold(span->flags) ? 1.f : 0.f);
			}
		};

		struct Word
		{
			std::vector<Piece> pieces;
			bool space_before = false;

			float Width() const
			{
				float width = 0.f;
				for (const Piece& piece : pieces)
					width += piece.Width();
				return width;
			}
		};

		size_t Utf8Length(unsigned char c)
		{
			if (c < 0x80) return 1;
			if ((c >> 5) == 0x6) return 2;
			if ((c >> 4) == 0xE) return 3;
			if ((c >> 3) == 0x1E) return 4;
			return 1;
		}

		std::vector<Word> BuildWords(const std::vector<Span>& spans)
		{
			std::vector<Word> words;
			bool pending_space = false;
			bool in_word = false;

			for (const Span& span : spans)
			{
				for (size_t i = 0; i < span.text.size(); ++i)
				{
					const char c = span.text[i];
					if (IsSpace(c) || c == '\n')
					{
						pending_space = true;
						in_word = false;
						continue;
					}
					if (!in_word)
					{
						words.emplace_back();
						words.back().space_before = pending_space && words.size() > 1;
						pending_space = false;
						in_word = true;
					}
					Word& word = words.back();
					if (word.pieces.empty() || word.pieces.back().span != &span)
						word.pieces.push_back(Piece{ &span, {} });
					word.pieces.back().text += c;
				}
			}

			// Widths, and the padding at both ends of every `code` span.
			const float pad = std::max(2.f, ImGui::GetFontSize() * 0.18f);
			const Piece* previous = nullptr;
			std::vector<Piece*> all;
			for (Word& word : words)
				for (Piece& piece : word.pieces)
					all.push_back(&piece);
			for (size_t k = 0; k < all.size(); ++k)
			{
				Piece& piece = *all[k];
				piece.text_width = TextWidth(FontFor(piece.span->flags), piece.text.c_str());
				if (piece.span->flags & kCode)
				{
					if (!previous || previous->span != piece.span)
						piece.pad_left = pad;
					if (k + 1 == all.size() || all[k + 1]->span != piece.span)
						piece.pad_right = pad;
				}
				previous = &piece;
			}
			return words;
		}

		// A word wider than the line : cut between characters.
		std::vector<Word> SplitWord(const Word& word, float max_width)
		{
			std::vector<Word> parts(1);
			parts[0].space_before = word.space_before;
			float width = 0.f;

			for (const Piece& piece : word.pieces)
			{
				size_t i = 0;
				while (i < piece.text.size())
				{
					const size_t length = std::min(Utf8Length(static_cast<unsigned char>(piece.text[i])), piece.text.size() - i);
					const std::string character = piece.text.substr(i, length);
					const float w = TextWidth(FontFor(piece.span->flags), character.c_str());
					if (width + w > max_width && width > 0.f)
					{
						parts.emplace_back();
						width = 0.f;
					}
					Word& part = parts.back();
					if (part.pieces.empty() || part.pieces.back().span != piece.span)
					{
						Piece copy;
						copy.span = piece.span;
						part.pieces.push_back(copy);
					}
					part.pieces.back().text += character;
					part.pieces.back().text_width += w;
					width += w;
					i += length;
				}
			}
			return parts;
		}

		ImU32 MixColors(ImU32 a, ImU32 b, float t)
		{
			const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
			const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
			return ImGui::ColorConvertFloat4ToU32(ImVec4(
				ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
				ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
		}

		// Background of `code` and of code blocks : the window color, a bit
		// towards the text color (darker beige on the light theme).
		ImU32 CodeBackground()
		{
			return MixColors(ImGui::GetColorU32(ImGuiCol_WindowBg), ImGui::GetColorU32(ImGuiCol_Text), 0.09f);
		}

		struct Colors
		{
			ImU32 text;
			ImU32 italic;
			ImU32 code_background;
			ImU32 link;
		};

		Colors CurrentColors(bool dimmed)
		{
			Colors colors;
			const ImU32 text = ImGui::GetColorU32(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text);
			colors.text = text;
			colors.italic = MixColors(text, ImGui::GetColorU32(ImGuiCol_TextDisabled), dimmed ? 0.f : 0.35f);
			colors.code_background = CodeBackground();
			colors.link = ImGui::GetColorU32(ImGuiCol_TextLink);
			return colors;
		}

		void OpenUrl(const std::string& url)
		{
			ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
			if (platform.Platform_OpenInShellFn)
				platform.Platform_OpenInShellFn(ImGui::GetCurrentContext(), url.c_str());
		}

		void DrawPiece(ImDrawList* draw_list, const Piece& piece, ImVec2 pos, float line_height,
		               const Colors& colors, bool code_background_joins_previous, float space_width)
		{
			const int flags = piece.span->flags;
			const float width = piece.Width();

			if (flags & kCode)
			{
				const float join = code_background_joins_previous ? space_width : 0.f;
				draw_list->AddRectFilled(ImVec2(pos.x - join, pos.y), ImVec2(pos.x + width, pos.y + line_height),
				                         colors.code_background);
			}

			ImU32 color = colors.text;
			if (flags & kLink)
				color = colors.link;
			else if ((flags & kItalic) && !RealItalic(flags))
				color = colors.italic;

			ImFont* font = FontFor(flags);
			const float font_size = ImGui::GetFontSize();
			const ImVec2 text_pos(pos.x + piece.pad_left, pos.y);
			const char* begin = piece.text.c_str();
			const char* end = begin + piece.text.size();
			draw_list->AddText(font, font_size, text_pos, color, begin, end);
			text_selection::Record(font, font_size, text_pos, begin, end);
			if (FakeBold(flags))
				draw_list->AddText(font, font_size, ImVec2(text_pos.x + 1.f, text_pos.y), color, begin, end);

			if (flags & kStrike)
			{
				const float y = std::floor(pos.y + line_height * 0.52f);
				draw_list->AddLine(ImVec2(text_pos.x, y), ImVec2(text_pos.x + piece.text_width, y), color, 1.f);
			}

			if (flags & kLink)
			{
				const float y = std::floor(pos.y + line_height - 1.f);
				draw_list->AddLine(ImVec2(text_pos.x, y), ImVec2(text_pos.x + piece.text_width, y), color, 1.f);

				const ImVec2 min(pos.x, pos.y), max(pos.x + width, pos.y + line_height);
				if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(min, max))
				{
					ImGui::SetTooltip("%s", piece.span->url.c_str());
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						OpenUrl(piece.span->url);
				}
			}
		}

		// Draws `text` (inline Markdown) wrapped between `indent` and the right
		// of the available region, then moves the layout below it.
		// `base_flags` : kBold for headings and table headers (their own
		// **...** markers are then simply bold, not shown).
		void DrawInline(const std::string& text, float indent, bool dimmed, int base_flags = 0)
		{
			std::vector<Span> spans;
			ParseInline(text, base_flags, std::string(), spans);
			std::vector<Word> words = BuildWords(spans);

			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float available = std::max(ImGui::GetFontSize() * 4.f, ImGui::GetContentRegionAvail().x - indent);
			const float line_height = ImGui::GetTextLineHeight();
			const float space_width = ImGui::CalcTextSize(" ").x;
			const Colors colors = CurrentColors(dimmed);
			ImDrawList* draw_list = ImGui::GetWindowDrawList();

			// Words wider than a whole line are cut.
			std::vector<Word> laid_out;
			laid_out.reserve(words.size());
			for (Word& word : words)
			{
				if (word.Width() > available)
				{
					for (Word& part : SplitWord(word, available))
						laid_out.push_back(std::move(part));
				}
				else
					laid_out.push_back(std::move(word));
			}

			float x = 0.f;
			int line = 0;
			float widest = 0.f;
			const Piece* previous_piece = nullptr;

			for (const Word& word : laid_out)
			{
				const float width = word.Width();
				float gap = (word.space_before && x > 0.f) ? space_width : 0.f;
				if (x > 0.f && x + gap + width > available)
				{
					++line;
					x = 0.f;
					gap = 0.f;
					previous_piece = nullptr;
				}
				x += gap;

				for (size_t k = 0; k < word.pieces.size(); ++k)
				{
					const Piece& piece = word.pieces[k];
					const bool joins = k == 0 && gap > 0.f && previous_piece &&
					                   previous_piece->span == piece.span && (piece.span->flags & kCode);
					DrawPiece(draw_list, piece,
					          ImVec2(origin.x + indent + x, origin.y + line * line_height),
					          line_height, colors, joins, space_width);
					x += piece.Width();
					previous_piece = &piece;
				}
				widest = std::max(widest, x);
			}

			ImGui::Dummy(ImVec2(indent + widest, line_height * static_cast<float>(line + 1)));
		}

		// ---------------------------------------------------------------------
		// Blocks
		// ---------------------------------------------------------------------

		std::string Trim(const std::string& s)
		{
			const size_t first = s.find_first_not_of(" \t\r");
			if (first == std::string::npos)
				return std::string();
			const size_t last = s.find_last_not_of(" \t\r");
			return s.substr(first, last - first + 1);
		}

		void DrawCodeBlock(const std::vector<std::string>& lines, const std::string& language, int block_index)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float line_height = ImGui::GetTextLineHeight();
			const float pad = style.FramePadding.x;
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float width = std::max(ImGui::GetFontSize() * 4.f, ImGui::GetContentRegionAvail().x);
			const float height = line_height * static_cast<float>(std::max<size_t>(lines.size(), 1)) + pad * 2.f;
			ImDrawList* draw_list = ImGui::GetWindowDrawList();

			draw_list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), CodeBackground());
			draw_list->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
			                   ImGui::GetColorU32(ImGuiCol_Border));

			draw_list->PushClipRect(origin, ImVec2(origin.x + width - pad, origin.y + height), true);
			const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
			ImFont* code_font = FontFor(kCode);   // the editor's mono font in Lynxie's messages
			for (size_t i = 0; i < lines.size(); ++i)
			{
				draw_list->AddText(code_font, ImGui::GetFontSize(),
				                   ImVec2(origin.x + pad, origin.y + pad + line_height * static_cast<float>(i)),
				                   color, lines[i].c_str());
				text_selection::Record(code_font, ImGui::GetFontSize(),
				                       ImVec2(origin.x + pad, origin.y + pad + line_height * static_cast<float>(i)),
				                       lines[i].c_str());
			}
			draw_list->PopClipRect();

			// Language and Copy, top right (shown when hovered).
			ImGui::Dummy(ImVec2(width, height));
			const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			const ImVec2 after = ImGui::GetCursorScreenPos();

			ImGui::PushID(block_index);
			const char* copy_label = "Copy";
			const ImVec2 copy_size(ImGui::CalcTextSize(copy_label).x + style.FramePadding.x * 2.f, ImGui::GetFrameHeight());
			const ImVec2 copy_pos(origin.x + width - copy_size.x - 2.f, origin.y + 2.f);
			const bool over_button = ImGui::IsMouseHoveringRect(copy_pos, ImVec2(copy_pos.x + copy_size.x, copy_pos.y + copy_size.y));
			if (hovered || over_button)
			{
				ImGui::SetCursorScreenPos(copy_pos);
				if (ImGui::Button(copy_label, copy_size))
				{
					std::string all;
					for (const std::string& line : lines)
						all += line + "\n";
					ImGui::SetClipboardText(all.c_str());
				}
			}
			else if (!language.empty())
			{
				const ImVec2 size = ImGui::CalcTextSize(language.c_str());
				draw_list->AddText(ImVec2(origin.x + width - size.x - pad, origin.y + pad),
				                   ImGui::GetColorU32(ImGuiCol_TextDisabled), language.c_str());
			}
			ImGui::PopID();
			ImGui::SetCursorScreenPos(after);
			ImGui::Dummy(ImVec2(0.f, 0.f));
		}

		std::vector<std::string> SplitCells(const std::string& row)
		{
			std::string line = Trim(row);
			if (!line.empty() && line.front() == '|')
				line.erase(0, 1);
			if (!line.empty() && line.back() == '|')
				line.pop_back();

			std::vector<std::string> cells;
			std::string cell;
			bool in_code = false;
			for (size_t i = 0; i < line.size(); ++i)
			{
				const char c = line[i];
				if (c == '\\' && i + 1 < line.size() && line[i + 1] == '|')
				{
					cell += '|';
					++i;
					continue;
				}
				if (c == '`')
					in_code = !in_code;
				if (c == '|' && !in_code)
				{
					cells.push_back(Trim(cell));
					cell.clear();
					continue;
				}
				cell += c;
			}
			cells.push_back(Trim(cell));
			return cells;
		}

		bool IsTableSeparator(const std::string& row)
		{
			const std::string line = Trim(row);
			if (line.find('-') == std::string::npos)
				return false;
			return line.find_first_not_of("|-: \t") == std::string::npos;
		}

		void DrawTable(const std::vector<std::string>& rows, int block_index)
		{
			std::vector<std::vector<std::string>> cells;
			bool has_header = false;
			for (size_t i = 0; i < rows.size(); ++i)
			{
				if (IsTableSeparator(rows[i]))
				{
					if (i == 1)
						has_header = true;
					continue;
				}
				cells.push_back(SplitCells(rows[i]));
			}
			size_t columns = 0;
			for (const auto& row : cells)
				columns = std::max(columns, row.size());
			if (columns == 0)
				return;
			columns = std::min<size_t>(columns, 32);

			// Natural width of every column (its longest cell) : fixed widths when
			// everything fits, otherwise columns that share the width by weight.
			const ImGuiStyle& style = ImGui::GetStyle();
			std::vector<float> natural(columns, ImGui::GetFontSize());
			for (size_t r = 0; r < cells.size(); ++r)
				for (size_t c = 0; c < columns && c < cells[r].size(); ++c)
					natural[c] = std::max(natural[c], ImGui::CalcTextSize(cells[r][c].c_str()).x + 2.f);
			float total = 0.f;
			for (float w : natural)
				total += w + style.CellPadding.x * 2.f + 1.f;
			const bool fits = total <= ImGui::GetContentRegionAvail().x;

			ImGui::PushID(block_index);
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			                              (fits ? ImGuiTableFlags_SizingFixedFit : ImGuiTableFlags_SizingStretchProp);
			if (ImGui::BeginTable("##markdown_table", static_cast<int>(columns), flags))
			{
				for (size_t c = 0; c < columns; ++c)
				{
					ImGui::TableSetupColumn(nullptr, fits ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch,
					                        fits ? natural[c] : std::max(natural[c], ImGui::GetFontSize() * 3.f));
				}
				for (size_t r = 0; r < cells.size(); ++r)
				{
					const bool header = has_header && r == 0;
					ImGui::TableNextRow(header ? ImGuiTableRowFlags_Headers : 0);
					for (size_t c = 0; c < columns; ++c)
					{
						ImGui::TableSetColumnIndex(static_cast<int>(c));
						const std::string& text = c < cells[r].size() ? cells[r][c] : std::string();
						DrawInline(text, 0.f, false, header ? kBold : 0);
					}
				}
				ImGui::EndTable();
			}
			ImGui::PopID();
		}

		// "- ", "* ", "+ " : bullet. "12. ", "3) " : number (returned in `label`).
		bool ListItem(const std::string& line, size_t& depth, std::string& label, std::string& content)
		{
			size_t spaces = 0;
			while (spaces < line.size() && IsSpace(line[spaces]))
				spaces += line[spaces] == '\t' ? 4 : 1;
			const size_t first = line.find_first_not_of(" \t");
			if (first == std::string::npos)
				return false;
			depth = spaces / 2;

			const char c = line[first];
			if ((c == '-' || c == '*' || c == '+') && first + 1 < line.size() && IsSpace(line[first + 1]))
			{
				label.clear();
				content = Trim(line.substr(first + 2));
				return true;
			}

			size_t i = first;
			while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])) && i - first < 4)
				++i;
			if (i > first && i + 1 < line.size() && (line[i] == '.' || line[i] == ')') && IsSpace(line[i + 1]))
			{
				label = line.substr(first, i - first + 1);
				content = Trim(line.substr(i + 2));
				return true;
			}
			return false;
		}

		void DrawListItem(size_t depth, const std::string& label, const std::string& content)
		{
			const float font_size = ImGui::GetFontSize();
			const float base = font_size * 0.6f + static_cast<float>(depth) * font_size * 1.2f;
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
			float indent;

			if (label.empty())
			{
				// Square bullet (pixel style) ; hollow from the second level.
				const float size = std::max(3.f, std::floor(font_size * 0.22f));
				const ImVec2 min(std::floor(origin.x + base), std::floor(origin.y + (ImGui::GetTextLineHeight() - size) * 0.5f));
				const ImVec2 max(min.x + size, min.y + size);
				if (depth == 0)
					draw_list->AddRectFilled(min, max, color);
				else
					draw_list->AddRect(min, max, color);
				indent = base + size + font_size * 0.5f;
			}
			else
			{
				ImFont* label_font = FontFor(kBold);
				const ImVec2 size(TextWidth(label_font, label.c_str()), 0.f);
				draw_list->AddText(label_font, font_size, ImVec2(origin.x + base, origin.y), color, label.c_str());
				text_selection::Record(label_font, font_size, ImVec2(origin.x + base, origin.y), label.c_str());
				if (FakeBold(kBold))
					draw_list->AddText(label_font, font_size, ImVec2(origin.x + base + 1.f, origin.y), color, label.c_str());
				indent = base + std::max(size.x + 1.f, font_size * 0.9f) + font_size * 0.35f;
			}

			DrawInline(content, indent, false);
		}

		void DrawHeading(int level, const std::string& text)
		{
			const float scale = level <= 1 ? 1.4f : (level == 2 ? 1.22f : 1.08f);
			ImGui::Dummy(ImVec2(0.f, ImGui::GetTextLineHeight() * 0.15f));
			ImGui::PushFont(nullptr, ImGui::GetCurrentContext()->FontSizeBase * scale);
			DrawInline(text, 0.f, false, kBold);
			ImGui::PopFont();
			if (level <= 2)
			{
				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();
				const float right = min.x + std::max(max.x - min.x, ImGui::GetContentRegionAvail().x);
				ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y + 1.f), ImVec2(right, max.y + 1.f),
				                                    ImGui::GetColorU32(ImGuiCol_Separator));
				ImGui::Dummy(ImVec2(0.f, 2.f));
			}
		}

		void DrawQuote(const std::string& text)
		{
			const float bar = std::max(2.f, std::floor(ImGui::GetFontSize() * 0.15f));
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			DrawInline(text, bar + ImGui::GetFontSize() * 0.6f, true);
			const float bottom = ImGui::GetItemRectMax().y;
			ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + bar, bottom),
			                                          ImGui::GetColorU32(ImGuiCol_TextDisabled));
		}
	}


	namespace
	{
		void AppendUtf8(std::string& out, unsigned int c)
		{
			if (c < 0x80)
				out += static_cast<char>(c);
			else if (c < 0x800)
			{
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else if (c < 0x10000)
			{
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
		}

		// Characters the pixel font does not have would be drawn as '?'
		// (models write non-breaking spaces, typographic quotes, bullets,
		// emojis...) : replaced by plain ones, or removed.
		std::string Sanitize(const std::string& text)
		{
			ImFont* font = ImGui::GetFont();
			std::string out;
			out.reserve(text.size());
			bool line_start = true;

			for (size_t i = 0; i < text.size();)
			{
				const unsigned char lead = static_cast<unsigned char>(text[i]);
				unsigned int c = lead;
				size_t length = 1;
				if (lead >= 0xF0) { c = lead & 0x07; length = 4; }
				else if (lead >= 0xE0) { c = lead & 0x0F; length = 3; }
				else if (lead >= 0xC0) { c = lead & 0x1F; length = 2; }
				if (i + length > text.size())
				{
					length = 1;
					c = '?';
				}
				for (size_t k = 1; k < length; ++k)
					c = (c << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
				i += length;

				const char* replacement = nullptr;
				bool drop = false;
				switch (c)
				{
					case '\t': replacement = "    "; break;
					case 0x00A0: case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
					case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009: case 0x200A:
					case 0x202F: case 0x205F: case 0x3000:
						replacement = " "; break;
					case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF:
					case 0xFE0E: case 0xFE0F: case 0x20E3: case 0x00AD:
						drop = true; break;
					case 0x2018: case 0x2019: case 0x201A: case 0x2032: replacement = "'"; break;
					case 0x201C: case 0x201D: case 0x201E: case 0x2033: replacement = "\""; break;
					case 0x00AB: replacement = "\""; break;
					case 0x00BB: replacement = "\""; break;
					case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212:
						replacement = "-"; break;
					case 0x2026: replacement = "..."; break;
					case 0x2022: case 0x25CF: case 0x25AA: case 0x25E6: case 0x2023: case 0x2043:
					case 0x25B8: case 0x25BA: case 0x27A4: case 0x2219:
						replacement = line_start ? "-" : "*"; break;
					case 0x2192: case 0x27F6: case 0x279C: case 0x2794: replacement = "->"; break;
					case 0x2190: replacement = "<-"; break;
					case 0x21D2: replacement = "=>"; break;
					case 0x2264: replacement = "<="; break;
					case 0x2265: replacement = ">="; break;
					case 0x2260: replacement = "!="; break;
					case 0x00D7: replacement = "x"; break;
					case 0x2713: case 0x2714: case 0x2705: replacement = "[ok]"; break;
					case 0x274C: case 0x2716: case 0x2717: replacement = "[x]"; break;
					case 0x26A0: replacement = "/!\\"; break;
					default: break;
				}

				if (!replacement && !drop && c >= 0x80)
				{
					// Not in the font : removed (an emoji, a symbol).
					const bool known = c <= 0xFFFF && font && font->IsGlyphInFont(static_cast<ImWchar>(c));
					drop = !known;
				}

				if (drop)
					continue;
				if (replacement)
					out += replacement;
				else
					AppendUtf8(out, c);

				if (c == '\n')
					line_start = true;
				else if (c != ' ' && c != '\t' && c != 0x00A0)
					line_start = false;
			}
			return out;
		}
	}

	void Render(const std::string& raw_text)
	{
		const std::string text = Sanitize(raw_text);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
		                    ImVec2(ImGui::GetStyle().ItemSpacing.x, std::floor(ImGui::GetStyle().ItemSpacing.y * 0.35f)));
		ImGui::BeginGroup();

		std::vector<std::string> lines;
		{
			size_t start = 0;
			while (start <= text.size())
			{
				size_t end = text.find('\n', start);
				std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(std::move(line));
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
		}
		// No empty line at the end of the message.
		while (!lines.empty() && Trim(lines.back()).empty())
			lines.pop_back();

		int block_index = 0;
		bool previous_blank = true;

		for (size_t i = 0; i < lines.size(); ++i)
		{
			const std::string& line = lines[i];
			const std::string trimmed = Trim(line);

			if (trimmed.empty())
			{
				if (!previous_blank)
					ImGui::Dummy(ImVec2(0.f, ImGui::GetTextLineHeight() * 0.4f));
				previous_blank = true;
				continue;
			}
			previous_blank = false;

			// ``` code block ```
			if (trimmed.rfind("```", 0) == 0)
			{
				const std::string language = Trim(trimmed.substr(3));
				const size_t indent = line.find_first_not_of(" \t");
				std::vector<std::string> code;
				++i;
				for (; i < lines.size(); ++i)
				{
					if (Trim(lines[i]).rfind("```", 0) == 0)
						break;
					// Remove the indentation of the fence (code in a list item).
					const std::string& code_line = lines[i];
					size_t cut = 0;
					while (cut < indent && cut < code_line.size() && IsSpace(code_line[cut]))
						++cut;
					std::string kept = code_line.substr(cut);
					// Tabs : 4 spaces (AddText draws them as nothing).
					std::string expanded;
					for (char c : kept)
						expanded += c == '\t' ? std::string("    ") : std::string(1, c);
					code.push_back(std::move(expanded));
				}
				DrawCodeBlock(code, language, block_index++);
				continue;
			}

			// | table |
			if (trimmed.front() == '|')
			{
				std::vector<std::string> rows;
				for (; i < lines.size() && !Trim(lines[i]).empty() && Trim(lines[i]).front() == '|'; ++i)
					rows.push_back(lines[i]);
				--i;
				DrawTable(rows, block_index++);
				continue;
			}

			// # Heading
			if (trimmed.front() == '#')
			{
				size_t level = 0;
				while (level < trimmed.size() && trimmed[level] == '#')
					++level;
				if (level <= 6 && level < trimmed.size() && IsSpace(trimmed[level]))
				{
					DrawHeading(static_cast<int>(level), Trim(trimmed.substr(level)));
					continue;
				}
			}

			// --- *** ___ : separator
			if (trimmed.size() >= 3 && trimmed.find_first_not_of(trimmed.front()) == std::string::npos &&
			    (trimmed.front() == '-' || trimmed.front() == '*' || trimmed.front() == '_'))
			{
				ImGui::Dummy(ImVec2(0.f, 2.f));
				ImGui::Separator();
				ImGui::Dummy(ImVec2(0.f, 2.f));
				continue;
			}

			// > quote
			if (trimmed.front() == '>')
			{
				DrawQuote(Trim(trimmed.substr(1)));
				continue;
			}

			// - item / 1. item
			size_t depth = 0;
			std::string label, content;
			if (ListItem(line, depth, label, content))
			{
				DrawListItem(depth, label, content);
				continue;
			}

			// Paragraph line (each line of the message stays a line).
			DrawInline(trimmed, 0.f, false);
		}

		ImGui::EndGroup();
		ImGui::PopStyleVar();
	}
}
