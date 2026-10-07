#include "CommandPalette.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace lynx::editor::command_palette
{
	namespace
	{
		std::vector<Item> g_items;
		std::vector<int> g_matches;          // indices in g_items, best first
		std::vector<std::string> g_recent;   // labels of the last items run (most recent first)
		char g_query[256] = "";
		std::string g_last_query = "\x01";
		int g_selected = 0;
		bool g_open = false;
		bool g_focus = false;
		bool g_scroll_to_selected = false;

		const char* KindLabel(Kind kind)
		{
			switch (kind)
			{
			case Kind::Window: return "window";
			case Kind::Actor:  return "actor";
			case Kind::File:   return "file";
			default:           return "command";
			}
		}

		ImVec4 KindColor(Kind kind)
		{
			switch (kind)
			{
			case Kind::Window: return ImVec4(0.55f, 0.75f, 1.f, 1.f);
			case Kind::Actor:  return ImVec4(0.55f, 0.9f, 0.6f, 1.f);
			case Kind::File:   return ImVec4(0.95f, 0.8f, 0.45f, 1.f);
			default:           return ImVec4(0.96f, 0.56f, 0.2f, 1.f);
			}
		}

		char Lower(char c)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		void Filter()
		{
			g_last_query = g_query;
			g_matches.clear();
			g_selected = 0;
			g_scroll_to_selected = true;

			// Prefix : one kind only.
			const char* q = g_query;
			int only = -1;   // -1 : every kind ; 0 commands + windows ; 1 actors ; 2 files
			if (q[0] == '>') { only = 0; ++q; }
			else if (q[0] == '@') { only = 1; ++q; }
			else if (q[0] == '#') { only = 2; ++q; }
			while (*q == ' ')
				++q;
			const std::string query = q;

			std::vector<std::pair<int, int>> scored;   // score, index
			for (int i = 0; i < static_cast<int>(g_items.size()); ++i)
			{
				const Item& item = g_items[i];
				if (only == 0 && item.kind != Kind::Command && item.kind != Kind::Window)
					continue;
				if (only == 1 && item.kind != Kind::Actor)
					continue;
				if (only == 2 && item.kind != Kind::File)
					continue;

				int score = 0;
				if (!query.empty())
				{
					score = FuzzyScore(query, item.label);
					// The detail (path, class) matches too, a little less.
					const int detail = FuzzyScore(query, item.detail);
					if (detail >= 0)
						score = std::max(score, detail / 2);
					if (score < 0)
						continue;
				}
				else if (item.kind == Kind::File || item.kind == Kind::Actor)
				{
					// Empty field : commands, windows and recent items ; actors and
					// files appear when typing (or with their prefix).
					if (only < 0 && std::find(g_recent.begin(), g_recent.end(), item.label) == g_recent.end())
						continue;
				}

				// Recent items first.
				const auto recent = std::find(g_recent.begin(), g_recent.end(), item.label);
				if (recent != g_recent.end())
					score += 1000 - static_cast<int>(recent - g_recent.begin()) * 10;

				// Commands before windows before actors before files (equal scores).
				score = score * 4 + (3 - static_cast<int>(item.kind));
				scored.emplace_back(score, i);
			}

			std::stable_sort(scored.begin(), scored.end(),
			                 [](const auto& a, const auto& b) { return a.first > b.first; });
			for (const auto& [score, index] : scored)
				g_matches.push_back(index);
		}

		void Run(int index)
		{
			if (index < 0 || index >= static_cast<int>(g_items.size()))
				return;
			Item item = g_items[index];   // a copy : running may reopen the palette
			g_open = false;
			ImGui::CloseCurrentPopup();

			g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), item.label), g_recent.end());
			g_recent.insert(g_recent.begin(), item.label);
			if (g_recent.size() > 12)
				g_recent.resize(12);

			if (item.run)
				item.run();
		}
	}

	int FuzzyScore(const std::string& query, const std::string& text)
	{
		if (query.empty())
			return 0;
		if (text.empty())
			return -1;

		int score = 0;
		size_t t = 0;
		int streak = 0;
		int first = -1;
		for (char qc : query)
		{
			if (qc == ' ')
				continue;
			const char q = Lower(qc);
			bool found = false;
			while (t < text.size())
			{
				const char c = Lower(text[t]);
				const bool word_start = t == 0 || !std::isalnum(static_cast<unsigned char>(text[t - 1])) ||
				                        (std::isupper(static_cast<unsigned char>(text[t])) &&
				                         std::islower(static_cast<unsigned char>(text[t - 1])));
				if (c == q)
				{
					if (first < 0)
						first = static_cast<int>(t);
					score += 10 + streak * 6 + (word_start ? 12 : 0);
					++streak;
					++t;
					found = true;
					break;
				}
				streak = 0;
				++t;
			}
			if (!found)
				return -1;
		}
		// Early match and short text : better.
		score -= std::min(first, 20);
		score -= static_cast<int>(std::min<size_t>(text.size(), 60)) / 6;
		return std::max(score, 1);
	}

	void Open(std::vector<Item> items, const std::string& prefix)
	{
		g_items = std::move(items);
		std::strncpy(g_query, prefix.c_str(), sizeof(g_query) - 1);
		g_query[sizeof(g_query) - 1] = '\0';
		g_last_query = "\x01";
		g_open = true;
		g_focus = true;
	}

	bool IsOpen()
	{
		return g_open;
	}

	void Draw()
	{
		if (!g_open)
			return;

		if (!ImGui::IsPopupOpen("##command_palette"))
			ImGui::OpenPopup("##command_palette");

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float font = ImGui::GetFontSize();
		const float width = std::min(viewport->WorkSize.x * 0.9f, font * 38.f);
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
		                               viewport->WorkPos.y + viewport->WorkSize.y * 0.12f),
		                        ImGuiCond_Always, ImVec2(0.5f, 0.f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.f));

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(font * 0.6f, font * 0.6f));
		const bool visible = ImGui::BeginPopup("##command_palette",
		                                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
		                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
		ImGui::PopStyleVar();
		if (!visible)
		{
			g_open = false;   // closed by a click outside
			return;
		}

		if (g_focus)
		{
			ImGui::SetKeyboardFocusHere();
			g_focus = false;
		}
		ImGui::SetNextItemWidth(-1.f);
		ImGui::InputTextWithHint("##query", "Search : commands, windows, @actors, #files", g_query, sizeof(g_query));
		if (g_last_query != g_query)
			Filter();

		const int count = static_cast<int>(g_matches.size());
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && count > 0)
		{
			g_selected = (g_selected + 1) % count;
			g_scroll_to_selected = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && count > 0)
		{
			g_selected = (g_selected + count - 1) % count;
			g_scroll_to_selected = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			g_open = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && count > 0)
		{
			Run(g_matches[std::clamp(g_selected, 0, count - 1)]);
			ImGui::EndPopup();
			return;
		}

		ImGui::TextDisabled(count == 0 ? "No result." : "%d result(s)  -  > commands   @ actors   # files", count);

		const float row = ImGui::GetTextLineHeightWithSpacing();
		const int shown = std::min(count, 12);
		if (shown > 0)
		{
			ImGui::BeginChild("##results", ImVec2(0.f, row * shown + ImGui::GetStyle().WindowPadding.y),
			                  ImGuiChildFlags_None);
			int clicked = -1;
			for (int i = 0; i < count; ++i)
			{
				const Item& item = g_items[g_matches[i]];
				ImGui::PushID(i);
				const bool selected = i == g_selected;
				const ImVec2 start = ImGui::GetCursorPos();
				if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap,
				                      ImVec2(0.f, ImGui::GetTextLineHeight())))
					clicked = i;
				if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x +
				                                  ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0.f)
					g_selected = i;
				if (selected && g_scroll_to_selected)
				{
					ImGui::SetScrollHereY(0.5f);
					g_scroll_to_selected = false;
				}

				ImGui::SetCursorPos(start);
				ImGui::TextColored(KindColor(item.kind), "%-7s", KindLabel(item.kind));
				ImGui::SameLine();
				ImGui::TextUnformatted(item.label.c_str());
				if (!item.detail.empty())
				{
					const float detail_w = ImGui::CalcTextSize(item.detail.c_str()).x;
					const float right = ImGui::GetWindowContentRegionMax().x;
					ImGui::SameLine(std::max(ImGui::GetCursorPosX() + font, right - detail_w));
					ImGui::TextDisabled("%s", item.detail.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
			if (clicked >= 0)
				Run(g_matches[clicked]);
		}

		ImGui::EndPopup();
	}
}
