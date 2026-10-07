#include "TextSelection.h"

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace lynx::editor::text_selection
{
	namespace
	{
		struct Run
		{
			std::string text;
			ImFont* font = nullptr;
			float size = 0.f;
			ImVec2 pos;
			std::vector<float> x;     // x of each character boundary (byte offsets in `bytes`)
			std::vector<int> bytes;

			void Measure()
			{
				if (!x.empty())
					return;
				const char* s = text.c_str();
				int i = 0;
				const int n = static_cast<int>(text.size());
				while (true)
				{
					bytes.push_back(i);
					x.push_back(pos.x + font->CalcTextSizeA(size, FLT_MAX, 0.f, s, s + i).x);
					if (i >= n)
						break;
					unsigned int c = 0;
					const int len = ImTextCharFromUtf8(&c, s + i, s + n);
					i += std::max(1, len);
				}
			}
			float Bottom() const { return pos.y + size; }
		};

		struct Point
		{
			int run = -1;
			int ch = 0;    // index in Run::bytes
			bool operator<(const Point& o) const { return run != o.run ? run < o.run : ch < o.ch; }
			bool operator==(const Point& o) const { return run == o.run && ch == o.ch; }
		};

		struct State
		{
			bool active = false;
			ImGuiID id = 0;
			std::vector<Run> runs;
			Point anchor, focus;
			bool selecting = false;
			std::string copy;
		};

		State g;

		bool HasSelection()
		{
			return g.anchor.run >= 0 && g.focus.run >= 0 && !(g.anchor == g.focus);
		}

		// The character boundary closest to a point of the screen.
		Point HitTest(const ImVec2& p)
		{
			Point best;
			if (g.runs.empty())
				return best;
			if (p.y < g.runs.front().pos.y)
				return { 0, 0 };

			// The line : the runs whose height contains p.y (else the closest line above).
			int line_first = -1;
			for (int i = 0; i < static_cast<int>(g.runs.size()); ++i)
			{
				const Run& r = g.runs[static_cast<size_t>(i)];
				if (p.y >= r.pos.y && p.y < r.Bottom())
				{
					line_first = i;
					break;
				}
			}
			if (line_first < 0)
			{
				// Between lines or below : the last run that starts above p.y.
				int last = 0;
				for (int i = 0; i < static_cast<int>(g.runs.size()); ++i)
					if (g.runs[static_cast<size_t>(i)].pos.y <= p.y)
						last = i;
				Run& r = g.runs[static_cast<size_t>(last)];
				r.Measure();
				return { last, static_cast<int>(r.x.size()) - 1 };
			}

			const float line_y = g.runs[static_cast<size_t>(line_first)].pos.y;
			float best_d = FLT_MAX;
			for (int i = line_first; i < static_cast<int>(g.runs.size()); ++i)
			{
				Run& r = g.runs[static_cast<size_t>(i)];
				if (std::fabs(r.pos.y - line_y) > r.size * 0.5f)
				{
					if (r.pos.y > line_y)
						break;
					continue;
				}
				r.Measure();
				for (size_t c = 0; c < r.x.size(); ++c)
				{
					const float d = std::fabs(r.x[c] - p.x);
					if (d < best_d)
					{
						best_d = d;
						best = { i, static_cast<int>(c) };
					}
				}
			}
			return best;
		}

		std::string Selected()
		{
			if (!HasSelection())
				return {};
			Point a = g.anchor, b = g.focus;
			if (b < a)
				std::swap(a, b);
			std::string out;
			for (int i = a.run; i <= b.run && i < static_cast<int>(g.runs.size()); ++i)
			{
				Run& r = g.runs[static_cast<size_t>(i)];
				r.Measure();
				if (i > a.run)
				{
					const Run& prev = g.runs[static_cast<size_t>(i - 1)];
					if (r.pos.y > prev.pos.y + prev.size * 0.5f)
						out += '\n';
					else if (r.pos.x > prev.x.back() + r.size * 0.15f)
						out += ' ';
				}
				const int c0 = i == a.run ? r.bytes[static_cast<size_t>(a.ch)] : 0;
				const int c1 = i == b.run ? r.bytes[static_cast<size_t>(b.ch)] : static_cast<int>(r.text.size());
				if (c1 > c0)
					out.append(r.text, static_cast<size_t>(c0), static_cast<size_t>(c1 - c0));
			}
			return out;
		}

		void SelectWord(const Point& p)
		{
			// The run is a word (Markdown) or a line : the characters around p up to a space.
			Run& r = g.runs[static_cast<size_t>(p.run)];
			r.Measure();
			auto is_space = [&](int ch)
			{
				const int b = r.bytes[static_cast<size_t>(ch)];
				return b >= static_cast<int>(r.text.size()) || r.text[static_cast<size_t>(b)] == ' ';
			};
			int s = p.ch, e = p.ch;
			while (s > 0 && !is_space(s - 1))
				--s;
			while (e < static_cast<int>(r.bytes.size()) - 1 && !is_space(e))
				++e;
			g.anchor = { p.run, s };
			g.focus = { p.run, e };
		}
	}


	void Begin(const char* id)
	{
		const ImGuiID new_id = ImGui::GetID(id);
		if (new_id != g.id)
		{
			g.anchor = g.focus = Point{};
			g.selecting = false;
		}
		g.id = new_id;
		g.active = true;
		g.runs.clear();
	}

	void Record(ImFont* font, float size, const ImVec2& pos, const char* begin, const char* end)
	{
		if (!g.active || !begin)
			return;
		if (!end)
			end = begin + std::strlen(begin);
		if (end <= begin)
			return;
		Run r;
		r.text.assign(begin, end);
		r.font = font ? font : ImGui::GetFont();
		r.size = size > 0.f ? size : ImGui::GetFontSize();
		r.pos = pos;
		g.runs.push_back(std::move(r));
	}

	void RecordLastItem(const char* begin, const char* end)
	{
		Record(ImGui::GetFont(), ImGui::GetFontSize(), ImGui::GetItemRectMin(), begin, end);
	}

	void TextWrapped(const char* begin, const char* end, ImU32 color)
	{
		if (!end)
			end = begin + std::strlen(begin);
		ImFont* font = ImGui::GetFont();
		const float size = ImGui::GetFontSize();
		const float wrap = std::max(1.f, ImGui::GetContentRegionAvail().x);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 col = color ? color : ImGui::GetColorU32(ImGuiCol_Text);
		float y = origin.y;
		float width = 0.f;
		const char* s = begin;
		while (s < end)
		{
			const char* line_end = static_cast<const char*>(std::memchr(s, '\n', static_cast<size_t>(end - s)));
			if (!line_end)
				line_end = end;
			const char* p = s;
			do
			{
				const char* cut = font->CalcWordWrapPosition(size, p, line_end, wrap);
				if (cut == p && p < line_end)
					++cut;   // a single too long character
				dl->AddText(font, size, ImVec2(origin.x, y), col, p, cut);
				Record(font, size, ImVec2(origin.x, y), p, cut);
				width = std::max(width, font->CalcTextSizeA(size, FLT_MAX, 0.f, p, cut).x);
				y += size;
				p = cut;
				while (p < line_end && *p == ' ')
					++p;
			} while (p < line_end);
			if (line_end == s)
				y += size;   // empty line
			s = line_end < end ? line_end + 1 : end;
		}
		ImGui::Dummy(ImVec2(width, std::max(size, y - origin.y)));
	}

	void End()
	{
		if (!g.active)
			return;
		g.active = false;

		const ImGuiIO& io = ImGui::GetIO();
		const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (g.anchor.run >= static_cast<int>(g.runs.size()) || g.focus.run >= static_cast<int>(g.runs.size()))
			g.anchor = g.focus = Point{};

		// Mouse : click / drag (not on a button, a link...), double-click : a word.
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
		{
			const Point p = HitTest(io.MousePos);
			if (p.run >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				SelectWord(p);
				g.selecting = false;
			}
			else
			{
				g.anchor = g.focus = p;
				g.selecting = p.run >= 0;
			}
			ImGui::SetWindowFocus();
		}
		if (g.selecting)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				g.focus = HitTest(io.MousePos);
				// Outside the view : it scrolls.
				ImGuiWindow* w = ImGui::GetCurrentWindow();
				const float speed = ImGui::GetFontSize() * 0.5f;
				if (io.MousePos.y < w->InnerRect.Min.y)
					ImGui::SetScrollY(ImGui::GetScrollY() - speed);
				else if (io.MousePos.y > w->InnerRect.Max.y)
					ImGui::SetScrollY(ImGui::GetScrollY() + speed);
			}
			else
				g.selecting = false;
		}

		// Highlight
		if (HasSelection())
		{
			Point a = g.anchor, b = g.focus;
			if (b < a)
				std::swap(a, b);
			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg);
			c.w = std::min(c.w, 0.45f);
			const ImU32 color = ImGui::ColorConvertFloat4ToU32(c);
			for (int i = a.run; i <= b.run; ++i)
			{
				Run& r = g.runs[static_cast<size_t>(i)];
				r.Measure();
				const int c0 = i == a.run ? a.ch : 0;
				const int c1 = i == b.run ? b.ch : static_cast<int>(r.x.size()) - 1;
				float x0 = r.x[static_cast<size_t>(c0)], x1 = r.x[static_cast<size_t>(c1)];
				// The space up to the next word of the same line.
				if (i < b.run)
				{
					const Run& next = g.runs[static_cast<size_t>(i + 1)];
					if (std::fabs(next.pos.y - r.pos.y) < r.size * 0.5f && next.pos.x > x1)
						x1 = next.pos.x;
				}
				if (x1 > x0)
					dl->AddRectFilled(ImVec2(x0, r.pos.y), ImVec2(x1, r.Bottom()), color);
			}
		}

		// Copy / select all
		const bool shortcuts = focused && !io.WantTextInput;
		if (shortcuts && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && !g.runs.empty())
		{
			g.anchor = { 0, 0 };
			g.runs.back().Measure();
			g.focus = { static_cast<int>(g.runs.size()) - 1, static_cast<int>(g.runs.back().x.size()) - 1 };
		}
		if (shortcuts && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && HasSelection())
			ImGui::SetClipboardText(Selected().c_str());

		if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
			ImGui::OpenPopup("##TextSelectionMenu");
		if (ImGui::BeginPopup("##TextSelectionMenu"))
		{
			if (ImGui::MenuItem("Copy", "Ctrl+C", false, HasSelection()))
				ImGui::SetClipboardText(Selected().c_str());
			if (ImGui::MenuItem("Select all", "Ctrl+A") && !g.runs.empty())
			{
				g.anchor = { 0, 0 };
				g.runs.back().Measure();
				g.focus = { static_cast<int>(g.runs.size()) - 1, static_cast<int>(g.runs.back().x.size()) - 1 };
			}
			ImGui::EndPopup();
		}
	}

	const char* GetSelectedText()
	{
		g.copy = Selected();
		return g.copy.c_str();
	}
}
