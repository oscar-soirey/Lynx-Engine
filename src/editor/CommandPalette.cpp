#include "CommandPalette.h"

#include "raven/RavenUi.h"

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
		std::string g_query;
		bool g_open = false;

		// The dialog while it is open.
		Rid g_dialog = R_INVALID_ID;
		Rid g_results = R_INVALID_ID;
		Rid g_count = R_INVALID_ID;
		std::vector<Rid> g_rows;             // tree items, in the order of g_matches
		int g_selected = 0;

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

		void KindColor(Kind kind, float rgb[3])
		{
			switch (kind)
			{
			case Kind::Window: rgb[0] = 0.33f; rgb[1] = 0.55f; rgb[2] = 0.85f; break;
			case Kind::Actor:  rgb[0] = 0.30f; rgb[1] = 0.65f; rgb[2] = 0.36f; break;
			case Kind::File:   rgb[0] = 0.80f; rgb[1] = 0.58f; rgb[2] = 0.15f; break;
			default:           rgb[0] = 0.85f; rgb[1] = 0.42f; rgb[2] = 0.12f; break;
			}
		}

		char Lower(char c)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		void Filter()
		{
			g_matches.clear();
			g_selected = 0;

			// Prefix : one kind only.
			const char* q = g_query.c_str();
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

		void Close()
		{
			if (g_dialog != R_INVALID_ID && Rwidget_is_valid(g_dialog))
				Rdialog_close(g_dialog, 0);
		}

		void Run(int index)
		{
			if (index < 0 || index >= static_cast<int>(g_items.size()))
				return;
			Item item = g_items[index];   // a copy : running may reopen the palette
			Close();

			g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), item.label), g_recent.end());
			g_recent.insert(g_recent.begin(), item.label);
			if (g_recent.size() > 12)
				g_recent.resize(12);

			// After the dialog is gone (the item may open another one).
			if (item.run)
				ui::Later(item.run);
		}

		void Select(int row)
		{
			const int count = static_cast<int>(g_rows.size());
			if (count == 0)
				return;
			g_selected = (row % count + count) % count;
			Rtree_view_set_selected_item(g_results, g_rows[g_selected]);
		}

		void Fill()
		{
			Filter();
			Rtree_view_clear_items(g_results);
			g_rows.clear();
			const int count = static_cast<int>(g_matches.size());
			for (int i = 0; i < count && i < 400; ++i)
			{
				const Item& item = g_items[g_matches[i]];
				std::string text = std::string(KindLabel(item.kind));
				text.resize(9, ' ');
				text += item.label;
				if (!item.detail.empty())
					text += "    " + item.detail;
				const Rid row = Rtree_view_add_item(g_results, R_INVALID_ID, text.c_str());
				float rgb[3];
				KindColor(item.kind, rgb);
				if (item.kind != Kind::Command)
					Rtree_item_set_text_color(row, rgb[0], rgb[1], rgb[2], 1.f);
				g_rows.push_back(row);
			}
			ui::SetText(g_count, count == 0 ? std::string("No result.")
			                                : std::to_string(count) + " result(s)  -  > commands   @ actors   # files");
			Select(0);
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
		const Rid window = input::Window();
		if (window == R_INVALID_ID)
			return;
		if (g_open)
			Close();

		g_items = std::move(items);
		g_query = prefix;

		g_dialog = ui::Load("dialogs/command_palette.xml");
		if (g_dialog == R_INVALID_ID)
			return;
		const Rid query = ui::Find(g_dialog, "palette.query");
		g_results = ui::Find(g_dialog, "palette.results");
		g_count = ui::Find(g_dialog, "palette.count");

		Rtext_input_set_text(query, g_query.c_str());
		ui::OnText(query, [](const std::string& text)
		{
			g_query = text;
			Fill();
		});
		// Arrows and Enter in the field move in the results.
		ui::OnEvent(query, [](const REvent& event)
		{
			if (event.type != R_EVENT_KEY_DOWN)
				return false;
			switch (event.key)
			{
			case R_KEY_DOWN: Select(g_selected + 1); return true;
			case R_KEY_UP: Select(g_selected - 1); return true;
			case R_KEY_PAGE_DOWN: Select(std::min(g_selected + 10, static_cast<int>(g_rows.size()) - 1)); return true;
			case R_KEY_PAGE_UP: Select(std::max(g_selected - 10, 0)); return true;
			case R_KEY_ENTER:
			case R_KEY_KP_ENTER:
				if (g_selected >= 0 && g_selected < static_cast<int>(g_matches.size()))
					Run(g_matches[g_selected]);
				return true;
			default:
				return false;
			}
		});
		ui::OnTreeSelect(g_results, [](Rid item)
		{
			for (int i = 0; i < static_cast<int>(g_rows.size()); ++i)
				if (g_rows[i] == item)
					g_selected = i;
		});
		// One click runs it.
		ui::OnEvent(g_results, [](const REvent& event)
		{
			if (event.type == R_EVENT_MOUSE_UP && event.button == R_MOUSE_LEFT)
			{
				const Rid item = Rtree_view_get_item_at(g_results, event.local_x, event.local_y);
				for (int i = 0; i < static_cast<int>(g_rows.size()); ++i)
					if (g_rows[i] == item)
					{
						ui::Later([i]() { if (g_open && i < static_cast<int>(g_matches.size())) Run(g_matches[i]); });
						return false;
					}
			}
			return false;
		});

		Fill();
		g_open = true;
		Rdialog_open(window, g_dialog, [](Rid, int, void*)
		{
			g_open = false;
			g_dialog = g_results = g_count = R_INVALID_ID;
			g_rows.clear();
		}, nullptr);
		Rwidget_focus(query);
		Rtext_input_select_all(query);
	}

	bool IsOpen()
	{
		return g_open;
	}
}
