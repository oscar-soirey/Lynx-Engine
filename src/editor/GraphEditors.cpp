#include "GraphEditors.h"

#include "../gameplay/AnimGraph.h"
#include "../gameplay/BehaviorTree.h"
#include "../gameplay/Actor.h"
#include "../gameplay/SpriteComponents.h"
#include "../scripting/Scripting.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_node/imnodes.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace stdfs = std::filesystem;

namespace lynx::editor::graph_editors
{
	namespace
	{
		// =====================================================================
		// Look
		// =====================================================================

		const ImU32 kSelection = IM_COL32(240, 160, 40, 255);
		const ImU32 kActive = IM_COL32(60, 180, 95, 255);
		const ImU32 kActiveHovered = IM_COL32(80, 200, 115, 255);
		const ImU32 kEntryColor = IM_COL32(60, 120, 60, 255);
		const ImU32 kAnyColor = IM_COL32(120, 90, 40, 255);
		const ImU32 kStateColor = IM_COL32(70, 80, 105, 255);
		const ImU32 kRootColor = IM_COL32(60, 60, 70, 255);
		const ImU32 kCompositeColor = IM_COL32(85, 85, 95, 255);
		const ImU32 kTaskColor = IM_COL32(105, 60, 135, 255);
		const ImVec4 kDecoratorText(0.16f, 0.38f, 0.80f, 1.f);   // readable on the pixel (light) theme
		const ImVec4 kServiceText(0.08f, 0.50f, 0.25f, 1.f);
		const ImVec4 kWarning(0.80f, 0.38f, 0.00f, 1.f);
		const ImVec4 kLive(0.05f, 0.55f, 0.20f, 1.f);

		ImU32 Lighter(ImU32 c, int amount)
		{
			auto ch = [&](int shift) { return std::min(255, static_cast<int>((c >> shift) & 0xFF) + amount); };
			return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), 255);
		}


		// =====================================================================
		// Documents
		// =====================================================================

		enum class Kind { Anim, Tree };

		struct Doc
		{
			Kind kind = Kind::Anim;
			stdfs::path path;
			AnimGraphAsset anim;
			BehaviorTreeAsset bt;

			std::string saved;          // content of the file (last load / save)
			std::string snapshot;       // current state (last commit)
			std::vector<std::string> undo;
			std::vector<std::string> redo;
			bool dirty = false;
			bool touched = false;       // edited this frame : commit when idle

			bool open = true;
			bool focus_next = false;
			bool ask_close = false;

			stdfs::file_time_type write_time{};
			bool changed_on_disk = false;
			double disk_check_time = 0.0;

			ImNodesEditorContext* nodes = nullptr;
			bool place_all = true;                          // positions of the asset -> ImNodes
			std::vector<std::pair<int, ImVec2>> place_screen;   // new nodes : screen position (next frame)
			std::vector<std::pair<int, ImVec2>> place_now;      // ... applied by the canvas this frame
			bool center_pending = true;
			bool reset_zoom = false;

			// Selection (ImNodes node id / link id, decorator or service id)
			int sel_node = 0;
			int sel_link = 0;
			int sel_aux = 0;
			int select_node_next = 0;
			int select_link_next = 0;

			// Nodes / links ImNodes knows this frame (one added by a menu exists next frame).
			std::unordered_set<int> drawn_nodes;
			std::unordered_set<int> drawn_links;

			ImVec2 popup_pos{};
			int dropped_attr = 0;       // link dropped on empty space : new node linked to it
			int context_node = 0;       // right click on a node

			float left_width = 270.f;
			float right_width = 340.f;

			int debug_index = 0;        // which running instance is shown

			std::string message;
			double message_time = -100.0;

			std::vector<std::function<void()>> pending;     // done after the canvas
		};

		std::vector<std::unique_ptr<Doc>> g_docs;
		std::function<bool(std::string&)> g_asset_drawer;
		ImNodesContext* g_nodes_context = nullptr;
		bool g_aux_clicked = false;     // a decorator / service row of a node was clicked this frame

		void Later(Doc& doc, std::function<void()> action)
		{
			doc.pending.push_back(std::move(action));
		}


		// =====================================================================
		// Small helpers
		// =====================================================================

		stdfs::path AssetsRoot()
		{
			std::error_code ec;
			return stdfs::weakly_canonical(stdfs::current_path(ec) / "assets", ec);
		}

		// "anim/Hero.animgraph" for a file in assets/, "" otherwise.
		std::string AssetPathOf(const stdfs::path& file)
		{
			std::error_code ec;
			const stdfs::path absolute = stdfs::weakly_canonical(file, ec);
			const stdfs::path relative = stdfs::relative(absolute, AssetsRoot(), ec);
			if (ec || relative.empty() || *relative.begin() == "..")
				return {};
			return relative.generic_string();
		}

		void ShowMessage(Doc& doc, const std::string& text)
		{
			doc.message = text;
			doc.message_time = ImGui::GetTime();
			std::cerr << "[GRAPH EDITOR] " << text << "\n";
		}

		float ToF(const std::string& text, float fallback)
		{
			if (text.empty())
				return fallback;
			char* end = nullptr;
			const float v = std::strtof(text.c_str(), &end);
			return end != text.c_str() ? v : fallback;
		}

		std::string Num(float v)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(v));
			return buffer;
		}

		bool ToBool(const std::string& text)
		{
			return text == "true" || text == "1" || text == "yes";
		}

		/** Text field edited in place (every keystroke). */
		bool InputStr(const char* label, std::string& value, ImGuiInputTextFlags flags = 0)
		{
			char buffer[512];
			std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
			if (ImGui::InputText(label, buffer, sizeof(buffer), flags))
			{
				value = buffer;
				return true;
			}
			return false;
		}

		/** Name field : applied when the edit ends (renames update the references). */
		bool InputName(const char* label, const std::string& current, std::string& result)
		{
			char buffer[256];
			std::snprintf(buffer, sizeof(buffer), "%s", current.c_str());
			ImGui::InputText(label, buffer, sizeof(buffer), ImGuiInputTextFlags_AutoSelectAll);
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				std::string name = buffer;
				while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
					name.pop_back();
				while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
					name.erase(name.begin());
				if (!name.empty() && name != current)
				{
					result = name;
					return true;
				}
			}
			return false;
		}

		/** Combo of strings ; `none` : label of the empty value (nullptr : no empty value). */
		bool ComboStr(const char* label, std::string& value, const std::vector<std::string>& options,
		              const char* none = nullptr)
		{
			bool changed = false;
			const bool known = value.empty() || std::find(options.begin(), options.end(), value) != options.end();
			std::string preview = value.empty() ? (none ? none : "") : value;
			if (!known)
				preview += "  (missing)";
			if (!known)
				ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
			const bool open = ImGui::BeginCombo(label, preview.c_str());
			if (!known)
				ImGui::PopStyleColor();
			if (open)
			{
				if (none && ImGui::Selectable(none, value.empty()))
				{
					value.clear();
					changed = true;
				}
				for (const std::string& o : options)
				{
					if (ImGui::Selectable(o.c_str(), o == value))
					{
						value = o;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		bool AssetField(const char* id, std::string& value)
		{
			ImGui::PushID(id);
			bool changed = false;
			if (g_asset_drawer)
				changed = g_asset_drawer(value);
			else
				changed = InputStr("##asset", value);
			ImGui::PopID();
			return changed;
		}

		void HelpMarker(const char* text)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", text);
		}

		void SectionTitle(const char* text)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("%s", text);
			ImGui::Separator();
		}

		// Two columns (label | field) for the Details panels.
		bool BeginFields(const char* id)
		{
			if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
				return false;
			ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.38f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
			return true;
		}

		void FieldLabel(const char* label, const char* help = nullptr)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			if (help && *help && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", help);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
		}

		// Classes JS of a base (scan of the files) : cached for a moment.
		const std::vector<std::string>& ScriptClasses(const std::string& base)
		{
			struct Entry { double time = -100.0; std::vector<std::string> names; };
			static std::map<std::string, Entry> cache;
			Entry& e = cache[base];
			if (ImGui::GetTime() - e.time > 2.0)
			{
				e.names = GetScriptClassesOf(base.c_str());
				e.time = ImGui::GetTime();
			}
			return e.names;
		}

		std::string UniqueName(const std::string& base, const std::function<bool(const std::string&)>& exists)
		{
			if (!exists(base))
				return base;
			for (int i = 2; ; ++i)
			{
				const std::string name = base + std::to_string(i);
				if (!exists(name))
					return name;
			}
		}

		// ImNodes attributes : node * 4 + 1 (input), node * 4 + 2 (output).
		int InAttr(int node) { return node * 4 + 1; }
		int OutAttr(int node) { return node * 4 + 2; }
		int NodeOfAttr(int attr) { return attr / 4; }
		bool IsOutAttr(int attr) { return attr % 4 == 2; }

		std::vector<int> SelectedNodes()
		{
			const int n = ImNodes::NumSelectedNodes();
			std::vector<int> ids(static_cast<size_t>(std::max(0, n)));
			if (n > 0)
				ImNodes::GetSelectedNodes(ids.data());
			return ids;
		}

		std::vector<int> SelectedLinks()
		{
			const int n = ImNodes::NumSelectedLinks();
			std::vector<int> ids(static_cast<size_t>(std::max(0, n)));
			if (n > 0)
				ImNodes::GetSelectedLinks(ids.data());
			return ids;
		}

		void PushTitleColor(ImU32 color)
		{
			ImNodes::PushColorStyle(ImNodesCol_TitleBar, color);
			ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, Lighter(color, 20));
			ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, Lighter(color, 35));
		}

		// Title bars are dark colors : light text.
		const ImVec4 kTitleText(1.f, 1.f, 1.f, 1.f);
		const ImVec4 kTitleDim(1.f, 1.f, 1.f, 0.62f);

		void BeginTitle()
		{
			ImNodes::BeginNodeTitleBar();
			ImGui::PushStyleColor(ImGuiCol_Text, kTitleText);
		}

		void EndTitle()
		{
			ImGui::PopStyleColor();
			ImNodes::EndNodeTitleBar();
		}

		void TitleDim(const char* text)
		{
			ImGui::SameLine();
			ImGui::TextColored(kTitleDim, "%s", text);
		}

		// A small rounded badge with a symbol, at the start of a title (icon of the kind of node).
		void TitleBadge(const char* glyph, ImU32 color)
		{
			const float h = ImGui::GetTextLineHeight();
			const ImVec2 text = ImGui::CalcTextSize(glyph);
			const float w = std::max(h, text.x + h * 0.5f);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), color, h * 0.3f);
			dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(255, 255, 255, 70), h * 0.3f);
			dl->AddText(ImVec2(std::floor(pos.x + (w - text.x) * 0.5f), pos.y), IM_COL32(255, 255, 255, 255), glyph);
			ImGui::Dummy(ImVec2(w, h));
			ImGui::SameLine(0.f, h * 0.35f);
		}

		void PopTitleColor()
		{
			ImNodes::PopColorStyle();
			ImNodes::PopColorStyle();
			ImNodes::PopColorStyle();
		}


		// =====================================================================
		// Load / save / undo
		// =====================================================================

		std::string Serialize(const Doc& doc)
		{
			return doc.kind == Kind::Anim ? doc.anim.SaveToString() : doc.bt.SaveToString();
		}

		bool Deserialize(Doc& doc, const std::string& text, std::string* error)
		{
			if (doc.kind == Kind::Anim)
				return doc.anim.LoadFromString(text, error);
			return doc.bt.LoadFromString(text, error);
		}

		void ClearSelection(Doc& doc)
		{
			doc.sel_node = 0;
			doc.sel_link = 0;
			doc.sel_aux = 0;
		}

		namespace anim { void LayoutIfOverlapping(AnimGraphAsset& g); }
		namespace tree { bool Overlapping(const BehaviorTreeAsset& t); void Arrange(Doc& doc); }

		void Load(Doc& doc)
		{
			std::ifstream in(doc.path, std::ios::binary);
			const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

			std::string error;
			if (!Deserialize(doc, text, &error))
			{
				ShowMessage(doc, "Could not read " + doc.path.filename().string() + " : " + error);
				if (doc.kind == Kind::Anim)
					doc.anim = AnimGraphAsset{};
				else
					doc.bt = BehaviorTreeAsset::MakeDefault();
			}
			if (doc.kind == Kind::Tree && !doc.bt.Root())
			{
				// A tree always has a root.
				BehaviorTreeAsset def = BehaviorTreeAsset::MakeDefault();
				BTNodeDesc root = def.nodes.front();
				root.id = doc.bt.NewId();
				doc.bt.nodes.insert(doc.bt.nodes.begin(), root);
			}

			// Hand-written file (no positions) : laid out, saved with the next edit.
			doc.saved = Serialize(doc);
			if (doc.kind == Kind::Anim)
				anim::LayoutIfOverlapping(doc.anim);
			else if (tree::Overlapping(doc.bt))
			{
				tree::Arrange(doc);
				doc.touched = false;
			}
			doc.snapshot = Serialize(doc);
			doc.undo.clear();
			doc.redo.clear();
			doc.saved = doc.snapshot;    // a layout alone is not an edit
			doc.dirty = false;
			doc.touched = false;
			doc.place_all = true;
			ClearSelection(doc);
			std::error_code ec;
			doc.write_time = stdfs::last_write_time(doc.path, ec);
			doc.changed_on_disk = false;
		}

		bool Save(Doc& doc)
		{
			const std::string text = Serialize(doc);
			std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				ShowMessage(doc, "Could not write " + doc.path.string());
				return false;
			}
			out << text;
			out.close();

			doc.saved = text;
			doc.snapshot = text;
			doc.dirty = false;
			doc.touched = false;
			std::error_code ec;
			doc.write_time = stdfs::last_write_time(doc.path, ec);
			doc.changed_on_disk = false;
			std::cout << "[GRAPH EDITOR] Saved " << doc.path.string() << "\n";
			return true;
		}

		void Commit(Doc& doc)
		{
			doc.touched = false;
			const std::string now = Serialize(doc);
			if (now == doc.snapshot)
				return;
			doc.undo.push_back(doc.snapshot);
			if (doc.undo.size() > 200)
				doc.undo.erase(doc.undo.begin());
			doc.redo.clear();
			doc.snapshot = now;
			doc.dirty = now != doc.saved;
		}

		void Restore(Doc& doc, const std::string& state)
		{
			Deserialize(doc, state, nullptr);
			doc.snapshot = state;
			doc.dirty = state != doc.saved;
			doc.place_all = true;
			doc.sel_aux = 0;
		}

		void Undo(Doc& doc)
		{
			Commit(doc);
			if (doc.undo.empty())
				return;
			doc.redo.push_back(doc.snapshot);
			const std::string state = doc.undo.back();
			doc.undo.pop_back();
			Restore(doc, state);
		}

		void Redo(Doc& doc)
		{
			if (doc.redo.empty())
				return;
			doc.undo.push_back(doc.snapshot);
			const std::string state = doc.redo.back();
			doc.redo.pop_back();
			Restore(doc, state);
		}

		void CheckDisk(Doc& doc)
		{
			if (ImGui::GetTime() - doc.disk_check_time < 1.0)
				return;
			doc.disk_check_time = ImGui::GetTime();

			std::error_code ec;
			const auto time = stdfs::last_write_time(doc.path, ec);
			if (ec || time == doc.write_time)
				return;
			doc.write_time = time;
			if (doc.dirty || doc.touched)
				doc.changed_on_disk = true;
			else
				Load(doc);
		}


		// =====================================================================
		// ANIM GRAPH
		// =====================================================================

		namespace anim
		{
			constexpr int kEntryNode = 1;
			constexpr int kAnyNode = 2;
			constexpr int kEntryLink = 1;

			int StateNode(int state_id) { return 1000 + state_id; }
			int StateOfNode(int node) { return node - 1000; }
			int TransitionLink(int id) { return 1000 + id; }
			int TransitionOfLink(int link) { return link - 1000; }

			AnimGraphState* FindState(AnimGraphAsset& g, int id)
			{
				for (auto& s : g.states)
					if (s.id == id)
						return &s;
				return nullptr;
			}

			AnimGraphTransition* FindTransition(AnimGraphAsset& g, int id)
			{
				for (auto& t : g.transitions)
					if (t.id == id)
						return &t;
				return nullptr;
			}

			std::vector<std::string> Motions(const AnimGraphAsset& g)
			{
				std::vector<std::string> out;
				for (const auto& a : g.animations)
					out.push_back(a.name);
				for (const auto& b : g.blend_spaces)
					out.push_back(b.name);
				return out;
			}

			std::vector<std::string> StateNames(const AnimGraphAsset& g)
			{
				std::vector<std::string> out;
				for (const auto& s : g.states)
					out.push_back(s.name);
				return out;
			}

			std::vector<std::string> ParamNames(const AnimGraphAsset& g, const char* only_type = nullptr)
			{
				std::vector<std::string> out;
				for (const auto& p : g.parameters)
					if (!only_type || p.type == only_type)
						out.push_back(p.name);
				return out;
			}

			std::string ConditionText(const AnimGraphCondition& c)
			{
				if (c.kind == "compare")
					return c.param + " " + c.op + " " + Num(c.value);
				if (c.kind == "isTrue")
					return c.param;
				if (c.kind == "isFalse")
					return "!" + c.param;
				if (c.kind == "trigger")
					return c.param + " (trigger)";
				if (c.kind == "finished")
					return "finished";
				if (c.kind == "script")
					return c.function + "()";
				return c.kind;
			}

			std::string TransitionText(const AnimGraphTransition& t)
			{
				std::string text;
				for (const auto& c : t.conditions)
					text += (text.empty() ? "" : " & ") + ConditionText(c);
				if (t.wait_finished)
					text += text.empty() ? "at the end" : " (at the end)";
				return text.empty() ? "always" : text;
			}

			// ---- Renames (references follow) -----------------------------------

			void RenameParameter(AnimGraphAsset& g, const std::string& from, const std::string& to)
			{
				for (auto& p : g.parameters)
					if (p.name == from)
						p.name = to;
				for (auto& t : g.transitions)
					for (auto& c : t.conditions)
						if (c.param == from)
							c.param = to;
				for (auto& b : g.blend_spaces)
					if (b.variable == from)
						b.variable = to;
			}

			void RenameMotion(AnimGraphAsset& g, const std::string& from, const std::string& to)
			{
				for (auto& a : g.animations)
					if (a.name == from)
						a.name = to;
				for (auto& b : g.blend_spaces)
				{
					if (b.name == from)
						b.name = to;
					for (auto& s : b.samples)
						if (s.animation == from)
							s.animation = to;
				}
				for (auto& s : g.states)
					if (s.motion == from)
						s.motion = to;
			}

			bool NameUsedByMotion(const AnimGraphAsset& g, const std::string& name)
			{
				return g.IsMotion(name);
			}

			// ---- Edits ------------------------------------------------------------

			int AddState(Doc& doc, const std::string& base, const std::string& motion, const ImVec2& screen)
			{
				AnimGraphAsset& g = doc.anim;
				AnimGraphState s;
				s.id = g.NewId();
				s.name = UniqueName(base.empty() ? "State" : base,
				                    [&](const std::string& n) { return g.FindState(n) != nullptr; });
				s.motion = motion;
				g.states.push_back(s);
				if (g.entry_state.empty())
					g.entry_state = s.name;
				doc.place_screen.emplace_back(StateNode(s.id), screen);
				doc.select_node_next = StateNode(s.id);
				doc.touched = true;
				return s.id;
			}

			int AddTransition(Doc& doc, const std::string& from, const std::string& to)
			{
				AnimGraphAsset& g = doc.anim;
				AnimGraphTransition t;
				t.id = g.NewId();
				t.from = from;
				t.to = to;
				g.transitions.push_back(t);
				doc.select_link_next = TransitionLink(t.id);
				doc.touched = true;
				return t.id;
			}

			void DeleteState(Doc& doc, int id)
			{
				AnimGraphAsset& g = doc.anim;
				AnimGraphState* s = FindState(g, id);
				if (!s)
					return;
				const std::string name = s->name;
				g.transitions.erase(std::remove_if(g.transitions.begin(), g.transitions.end(),
				                                   [&](const AnimGraphTransition& t) { return t.from == name || t.to == name; }),
				                    g.transitions.end());
				for (auto& other : g.states)
					if (other.next == name)
						other.next.clear();
				g.states.erase(std::remove_if(g.states.begin(), g.states.end(),
				                              [id](const AnimGraphState& st) { return st.id == id; }),
				               g.states.end());
				if (g.entry_state == name)
					g.entry_state = g.states.empty() ? std::string() : g.states.front().name;
				doc.touched = true;
			}

			void DeleteTransition(Doc& doc, int id)
			{
				auto& v = doc.anim.transitions;
				v.erase(std::remove_if(v.begin(), v.end(), [id](const AnimGraphTransition& t) { return t.id == id; }), v.end());
				doc.touched = true;
			}

			void DuplicateState(Doc& doc, int id)
			{
				AnimGraphState* s = FindState(doc.anim, id);
				if (!s)
					return;
				AnimGraphState copy = *s;
				copy.id = doc.anim.NewId();
				copy.name = UniqueName(s->name, [&](const std::string& n) { return doc.anim.FindState(n) != nullptr; });
				copy.x += 40.f;
				copy.y += 40.f;
				doc.anim.states.push_back(copy);
				doc.place_all = true;
				doc.select_node_next = StateNode(copy.id);
				doc.touched = true;
			}

			/** File written by hand (no positions) : Entry, Any State, then the states in a grid. */
			void LayoutIfOverlapping(AnimGraphAsset& g)
			{
				std::set<std::pair<int, int>> seen;
				bool overlap = false;
				auto add = [&](float x, float y) { overlap |= !seen.insert({ static_cast<int>(x), static_cast<int>(y) }).second; };
				add(g.entry_x, g.entry_y);
				add(g.any_x, g.any_y);
				for (const auto& s : g.states)
					add(s.x, s.y);
				if (!overlap)
					return;

				g.entry_x = 0.f;
				g.entry_y = 0.f;
				g.any_x = 0.f;
				g.any_y = 220.f;
				const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(g.states.size())))));
				for (size_t i = 0; i < g.states.size(); ++i)
				{
					g.states[i].x = 280.f + static_cast<float>(i % static_cast<size_t>(columns)) * 300.f;
					g.states[i].y = static_cast<float>(i / static_cast<size_t>(columns)) * 180.f;
				}
			}

			// ---- Debug (game running) ---------------------------------------------

			AnimationSpriteComponent* DebugTarget(Doc& doc, std::vector<AnimationSpriteComponent*>* all = nullptr)
			{
				const std::string asset = AssetPathOf(doc.path);
				if (asset.empty())
					return nullptr;
				std::vector<AnimationSpriteComponent*> running = AnimationSpriteComponent::GetWithGraph(asset);
				if (all)
					*all = running;
				if (running.empty())
					return nullptr;
				doc.debug_index = std::clamp(doc.debug_index, 0, static_cast<int>(running.size()) - 1);
				return running[static_cast<size_t>(doc.debug_index)];
			}

			std::vector<std::string> Issues(const AnimGraphAsset& g)
			{
				std::vector<std::string> out;
				if (g.states.empty())
					out.push_back("No state : right click on the graph to add one.");
				else if (std::none_of(g.states.begin(), g.states.end(), [&](const AnimGraphState& s) { return s.name == g.entry_state; }))
					out.push_back("No entry state (link Entry to a state).");
				for (const auto& s : g.states)
					if (!g.IsMotion(s.motion))
						out.push_back("State " + s.name + " : no animation / blend space.");
				for (const auto& a : g.animations)
					if (a.texture.empty())
						out.push_back("Animation " + a.name + " : no texture.");
				for (const auto& t : g.transitions)
					for (const auto& c : t.conditions)
						if (c.kind != "finished" && c.kind != "script" && !g.FindParameter(c.param))
							out.push_back("Transition " + (t.from.empty() ? std::string("Any") : t.from) + " -> " + t.to +
							              " : unknown parameter \"" + c.param + "\".");
				return out;
			}

			// ---- Left panel ---------------------------------------------------------

			void DrawParameters(Doc& doc, AnimationSpriteComponent* debug)
			{
				AnimGraphAsset& g = doc.anim;
				static const std::vector<std::string> kTypes = { "float", "bool", "int", "trigger" };

				if (!ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
					return;

				int remove = -1;
				for (size_t i = 0; i < g.parameters.size(); ++i)
				{
					AnimGraphParameter& p = g.parameters[i];
					ImGui::PushID(static_cast<int>(i));

					const float w = ImGui::GetContentRegionAvail().x;
					ImGui::SetNextItemWidth(w * 0.36f);
					std::string renamed;
					if (InputName("##name", p.name, renamed))
					{
						if (g.FindParameter(renamed))
							ShowMessage(doc, "A parameter \"" + renamed + "\" already exists");
						else
						{
							RenameParameter(g, p.name, renamed);
							doc.touched = true;
						}
					}
					ImGui::SameLine();
					ImGui::SetNextItemWidth(w * 0.30f);
					std::string type = p.type;
					if (ComboStr("##type", type, kTypes))
					{
						p.type = type;
						doc.touched = true;
					}
					ImGui::SameLine();
					ImGui::SetNextItemWidth(std::max(30.f, w * 0.34f - ImGui::GetFrameHeight() - 12.f));
					if (debug)
					{
						// Live value of the game.
						const float v = debug->GetFloat(p.name);
						ImGui::AlignTextToFramePadding();
						if (p.type == "bool" || p.type == "trigger")
							ImGui::TextColored(kLive, "%s", v != 0.f ? "true" : "false");
						else if (p.type == "int")
							ImGui::TextColored(kLive, "%d", static_cast<int>(v));
						else
							ImGui::TextColored(kLive, "%.2f", static_cast<double>(v));
					}
					else if (p.type == "bool")
					{
						bool b = p.default_value != 0.f;
						if (ImGui::Checkbox("##def", &b))
						{
							p.default_value = b ? 1.f : 0.f;
							doc.touched = true;
						}
					}
					else if (p.type == "int")
					{
						int v = static_cast<int>(p.default_value);
						if (ImGui::DragInt("##def", &v))
						{
							p.default_value = static_cast<float>(v);
							doc.touched = true;
						}
					}
					else if (p.type == "float")
					{
						if (ImGui::DragFloat("##def", &p.default_value, 0.05f))
							doc.touched = true;
					}
					else
						ImGui::Dummy(ImVec2(1.f, ImGui::GetFrameHeight()));
					if (ImGui::IsItemHovered() && !debug)
						ImGui::SetTooltip("Default value");
					ImGui::SameLine();
					if (ImGui::Button("x"))
						remove = static_cast<int>(i);
					ImGui::PopID();
				}
				if (remove >= 0)
				{
					g.parameters.erase(g.parameters.begin() + remove);
					doc.touched = true;
				}

				if (ImGui::Button("+ Parameter"))
				{
					AnimGraphParameter p;
					p.name = UniqueName("speed", [&](const std::string& n) { return g.FindParameter(n) != nullptr; });
					g.parameters.push_back(p);
					doc.touched = true;
				}
				HelpMarker("Values set by the game : sprite.setFloat(\"speed\", 3), setBool, setTrigger...\n"
				           "Triggers are reset once a transition used them.");
			}

			void DrawAnimations(Doc& doc)
			{
				AnimGraphAsset& g = doc.anim;
				if (!ImGui::CollapsingHeader("Animations", ImGuiTreeNodeFlags_DefaultOpen))
					return;

				int remove = -1;
				for (size_t i = 0; i < g.animations.size(); ++i)
				{
					AnimGraphAnimation& a = g.animations[i];
					ImGui::PushID(static_cast<int>(i) + 10000);
					const bool open = ImGui::TreeNodeEx("##anim", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", a.name.c_str());
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Delete"))
							remove = static_cast<int>(i);
						ImGui::EndPopup();
					}
					if (open)
					{
						if (BeginFields("##fields"))
						{
							FieldLabel("Name");
							std::string renamed;
							if (InputName("##name", a.name, renamed))
							{
								if (NameUsedByMotion(g, renamed))
									ShowMessage(doc, "\"" + renamed + "\" is already an animation or a blend space");
								else
								{
									RenameMotion(g, a.name, renamed);
									doc.touched = true;
								}
							}
							FieldLabel("Texture", "Horizontal strip of frames (assets/).");
							if (AssetField("texture", a.texture))
								doc.touched = true;
							FieldLabel("Frames");
							if (ImGui::InputInt("##frames", &a.frames))
							{
								a.frames = std::max(1, a.frames);
								doc.touched = true;
							}
							FieldLabel("Frame time", "Seconds per frame.");
							if (ImGui::DragFloat("##ft", &a.frame_time, 0.005f, 0.001f, 10.f, "%.3f s"))
								doc.touched = true;
							FieldLabel("Loop");
							if (ImGui::Checkbox("##loop", &a.loop))
								doc.touched = true;
							ImGui::EndTable();
						}

						ImGui::TextDisabled("Notifies (frame -> JS function of the actor)");
						int remove_notify = -1;
						for (size_t n = 0; n < a.notifies.size(); ++n)
						{
							ImGui::PushID(static_cast<int>(n));
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.f);
							if (ImGui::InputInt("##frame", &a.notifies[n].frame, 0))
							{
								a.notifies[n].frame = std::clamp(a.notifies[n].frame, 1, std::max(1, a.frames));
								doc.touched = true;
							}
							ImGui::SameLine();
							ImGui::SetNextItemWidth(std::max(40.f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 8.f));
							if (InputStr("##event", a.notifies[n].event))
								doc.touched = true;
							ImGui::SameLine();
							if (ImGui::Button("x"))
								remove_notify = static_cast<int>(n);
							ImGui::PopID();
						}
						if (remove_notify >= 0)
						{
							a.notifies.erase(a.notifies.begin() + remove_notify);
							doc.touched = true;
						}
						if (ImGui::SmallButton("+ Notify"))
						{
							a.notifies.push_back({ std::max(1, a.frames), "OnStep" });
							doc.touched = true;
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Delete animation"))
							remove = static_cast<int>(i);
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (remove >= 0)
				{
					const std::string name = g.animations[static_cast<size_t>(remove)].name;
					g.animations.erase(g.animations.begin() + remove);
					doc.touched = true;
					(void)name;
				}

				if (ImGui::Button("+ Animation"))
				{
					AnimGraphAnimation a;
					a.name = UniqueName("Idle", [&](const std::string& n) { return g.IsMotion(n); });
					g.animations.push_back(a);
					doc.touched = true;
				}
			}

			void DrawBlendSpaces(Doc& doc)
			{
				AnimGraphAsset& g = doc.anim;
				if (!ImGui::CollapsingHeader("Blend spaces", ImGuiTreeNodeFlags_DefaultOpen))
					return;

				std::vector<std::string> anims;
				for (const auto& a : g.animations)
					anims.push_back(a.name);

				int remove = -1;
				for (size_t i = 0; i < g.blend_spaces.size(); ++i)
				{
					AnimGraphBlendSpace& b = g.blend_spaces[i];
					ImGui::PushID(static_cast<int>(i) + 20000);
					const bool open = ImGui::TreeNodeEx("##bs", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", b.name.c_str());
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Delete"))
							remove = static_cast<int>(i);
						ImGui::EndPopup();
					}
					if (open)
					{
						if (BeginFields("##fields"))
						{
							FieldLabel("Name");
							std::string renamed;
							if (InputName("##name", b.name, renamed))
							{
								if (NameUsedByMotion(g, renamed))
									ShowMessage(doc, "\"" + renamed + "\" is already an animation or a blend space");
								else
								{
									RenameMotion(g, b.name, renamed);
									doc.touched = true;
								}
							}
							FieldLabel("Variable", "Float parameter that picks the animation (closest sample below it).");
							if (ComboStr("##var", b.variable, ParamNames(g, "float"), "(none)"))
								doc.touched = true;
							ImGui::EndTable();
						}

						ImGui::TextDisabled("Samples (position -> animation)");
						int remove_sample = -1;
						for (size_t s = 0; s < b.samples.size(); ++s)
						{
							ImGui::PushID(static_cast<int>(s));
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
							if (ImGui::DragFloat("##pos", &b.samples[s].position, 0.05f))
								doc.touched = true;
							ImGui::SameLine();
							ImGui::SetNextItemWidth(std::max(40.f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 8.f));
							if (ComboStr("##anim", b.samples[s].animation, anims))
								doc.touched = true;
							ImGui::SameLine();
							if (ImGui::Button("x"))
								remove_sample = static_cast<int>(s);
							ImGui::PopID();
						}
						if (remove_sample >= 0)
						{
							b.samples.erase(b.samples.begin() + remove_sample);
							doc.touched = true;
						}
						if (ImGui::SmallButton("+ Sample"))
						{
							const float pos = b.samples.empty() ? 0.f : b.samples.back().position + 1.f;
							b.samples.push_back({ pos, anims.empty() ? std::string() : anims.front() });
							doc.touched = true;
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Delete blend space"))
							remove = static_cast<int>(i);
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (remove >= 0)
				{
					g.blend_spaces.erase(g.blend_spaces.begin() + remove);
					doc.touched = true;
				}

				if (ImGui::Button("+ Blend space"))
				{
					AnimGraphBlendSpace b;
					b.name = UniqueName("Locomotion", [&](const std::string& n) { return g.IsMotion(n); });
					const auto floats = ParamNames(g, "float");
					if (!floats.empty())
						b.variable = floats.front();
					g.blend_spaces.push_back(b);
					doc.touched = true;
				}
			}

			void DrawLeft(Doc& doc, AnimationSpriteComponent* debug)
			{
				DrawParameters(doc, debug);
				ImGui::Spacing();
				DrawAnimations(doc);
				ImGui::Spacing();
				DrawBlendSpaces(doc);
			}

			// ---- Details ----------------------------------------------------------------

			void DrawConditions(Doc& doc, AnimGraphTransition& t)
			{
				AnimGraphAsset& g = doc.anim;
				static const std::vector<std::string> kKinds = { "compare", "isTrue", "isFalse", "trigger", "finished", "script" };
				static const std::vector<std::string> kOps = { ">", ">=", "<", "<=", "==", "!=" };

				SectionTitle("Conditions (all must be true)");
				int remove = -1;
				for (size_t i = 0; i < t.conditions.size(); ++i)
				{
					AnimGraphCondition& c = t.conditions[i];
					ImGui::PushID(static_cast<int>(i));
					ImGui::BeginGroup();
					const float w = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 8.f;

					ImGui::SetNextItemWidth(w * 0.32f);
					if (ComboStr("##kind", c.kind, kKinds))
						doc.touched = true;
					ImGui::SameLine();

					if (c.kind == "compare")
					{
						ImGui::SetNextItemWidth(w * 0.30f);
						std::vector<std::string> numbers;
						for (const auto& p : g.parameters)
							if (p.type == "float" || p.type == "int")
								numbers.push_back(p.name);
						if (ComboStr("##param", c.param, numbers))
							doc.touched = true;
						ImGui::SameLine();
						ImGui::SetNextItemWidth(w * 0.14f);
						if (ComboStr("##op", c.op, kOps))
							doc.touched = true;
						ImGui::SameLine();
						ImGui::SetNextItemWidth(std::max(30.f, w * 0.24f - 12.f));
						if (ImGui::DragFloat("##value", &c.value, 0.05f))
							doc.touched = true;
					}
					else if (c.kind == "isTrue" || c.kind == "isFalse")
					{
						ImGui::SetNextItemWidth(w * 0.68f - 4.f);
						if (ComboStr("##param", c.param, ParamNames(g, "bool")))
							doc.touched = true;
					}
					else if (c.kind == "trigger")
					{
						ImGui::SetNextItemWidth(w * 0.68f - 4.f);
						if (ComboStr("##param", c.param, ParamNames(g, "trigger")))
							doc.touched = true;
					}
					else if (c.kind == "script")
					{
						ImGui::SetNextItemWidth(w * 0.68f - 4.f);
						if (InputStr("##fn", c.function))
							doc.touched = true;
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("JS function of the actor that returns true / false.");
					}
					else
					{
						ImGui::AlignTextToFramePadding();
						ImGui::TextDisabled("the animation ended");
					}
					ImGui::EndGroup();
					ImGui::SameLine();
					if (ImGui::Button("x"))
						remove = static_cast<int>(i);
					ImGui::PopID();
				}
				if (remove >= 0)
				{
					t.conditions.erase(t.conditions.begin() + remove);
					doc.touched = true;
				}
				if (ImGui::Button("+ Condition"))
				{
					AnimGraphCondition c;
					if (!g.parameters.empty())
					{
						c.param = g.parameters.front().name;
						const std::string& type = g.parameters.front().type;
						c.kind = type == "bool" ? "isTrue" : type == "trigger" ? "trigger" : "compare";
					}
					t.conditions.push_back(c);
					doc.touched = true;
				}
				if (t.conditions.empty())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("none : always");
				}
			}

			void DrawDetails(Doc& doc, AnimationSpriteComponent* debug)
			{
				AnimGraphAsset& g = doc.anim;

				if (doc.sel_link == kEntryLink || doc.sel_node == kEntryNode)
				{
					ImGui::TextUnformatted("Entry");
					ImGui::Separator();
					ImGui::TextWrapped("The state the graph starts in.");
					if (BeginFields("##entry"))
					{
						FieldLabel("Entry state");
						if (ComboStr("##entry", g.entry_state, StateNames(g)))
							doc.touched = true;
						ImGui::EndTable();
					}
					return;
				}

				if (doc.sel_node == kAnyNode)
				{
					ImGui::TextUnformatted("Any State");
					ImGui::Separator();
					ImGui::TextWrapped("Its transitions are checked from every state (hurt, death...). "
					                   "Drag its pin onto a state to add one.");
					return;
				}

				if (doc.sel_link > kEntryLink)
				{
					AnimGraphTransition* t = FindTransition(g, TransitionOfLink(doc.sel_link));
					if (!t)
						return;
					ImGui::Text("Transition   %s  ->  %s", t->from.empty() ? "Any State" : t->from.c_str(), t->to.c_str());
					ImGui::Separator();
					if (BeginFields("##transition"))
					{
						FieldLabel("Priority", "Checked first when several transitions are possible (highest first).");
						if (ImGui::InputInt("##priority", &t->priority))
							doc.touched = true;
						FieldLabel("At the end", "Waits for the end of the animation of the state.");
						if (ImGui::Checkbox("##wait", &t->wait_finished))
							doc.touched = true;
						ImGui::EndTable();
					}
					DrawConditions(doc, *t);
					ImGui::Spacing();
					ImGui::Separator();
					if (ImGui::Button("Delete transition"))
					{
						const int id = t->id;
						Later(doc, [&doc, id] { DeleteTransition(doc, id); doc.sel_link = 0; });
					}
					return;
				}

				AnimGraphState* s = doc.sel_node >= 1000 ? FindState(g, StateOfNode(doc.sel_node)) : nullptr;
				if (!s)
				{
					ImGui::TextDisabled("Nothing selected");
					ImGui::Spacing();
					ImGui::TextWrapped(
						"Right click on the graph : add a state.\n"
						"Drag the pin of a state onto another one : transition.\n"
						"Click a transition (a link) to edit its conditions.\n\n"
						"Use : AnimationSprite.graph = \"%s\" (or loadGraph), then setFloat / setBool / setTrigger.\n"
						"Notifies and on enter / on exit call the JS function of the actor with that name.",
						AssetPathOf(doc.path).c_str());
					return;
				}

				const bool is_entry = g.entry_state == s->name;
				ImGui::Text("State   %s%s", s->name.c_str(), is_entry ? "   (entry)" : "");
				if (debug && debug->GetCurrentState() == s->name)
				{
					ImGui::SameLine();
					ImGui::TextColored(kLive, "  playing");
				}
				ImGui::Separator();

				if (BeginFields("##state"))
				{
					FieldLabel("Name");
					std::string renamed;
					if (InputName("##name", s->name, renamed))
					{
						if (g.FindState(renamed))
							ShowMessage(doc, "A state \"" + renamed + "\" already exists");
						else
						{
							g.RenameState(s->name, renamed);
							doc.touched = true;
						}
					}
					FieldLabel("Animation", "Animation or blend space played in this state.");
					if (ComboStr("##motion", s->motion, Motions(g), "(none)"))
						doc.touched = true;
					FieldLabel("Interruptible", "Off : transitions wait for the end of the animation.");
					if (ImGui::Checkbox("##int", &s->interruptible))
						doc.touched = true;
					FieldLabel("Restart on enter");
					if (ImGui::Checkbox("##restart", &s->restart_on_enter))
						doc.touched = true;
					FieldLabel("Next", "Played at the end of the animation when no transition is taken.");
					std::vector<std::string> others;
					for (const auto& o : g.states)
						if (o.id != s->id)
							others.push_back(o.name);
					if (ComboStr("##next", s->next, others, "(none)"))
						doc.touched = true;
					FieldLabel("On enter", "JS function of the actor called when the state starts.");
					if (InputStr("##enter", s->on_enter))
						doc.touched = true;
					FieldLabel("On exit");
					if (InputStr("##exit", s->on_exit))
						doc.touched = true;
					ImGui::EndTable();
				}

				ImGui::Spacing();
				if (!is_entry && ImGui::Button("Set as entry"))
				{
					g.entry_state = s->name;
					doc.touched = true;
				}
				if (!is_entry)
					ImGui::SameLine();
				if (ImGui::Button("Duplicate"))
				{
					const int id = s->id;
					Later(doc, [&doc, id] { DuplicateState(doc, id); });
				}
				ImGui::SameLine();
				if (ImGui::Button("Delete"))
				{
					const int id = s->id;
					Later(doc, [&doc, id] { DeleteState(doc, id); doc.sel_node = 0; });
				}

				SectionTitle("Transitions from this state");
				std::vector<const AnimGraphTransition*> out;
				for (const auto& t : g.transitions)
					if (t.from == s->name)
						out.push_back(&t);
				std::stable_sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->priority > b->priority; });
				if (out.empty())
					ImGui::TextDisabled("none (drag the right pin onto a state)");
				for (const AnimGraphTransition* t : out)
				{
					ImGui::PushID(t->id);
					const std::string label = "-> " + t->to + "   " + TransitionText(*t);
					if (ImGui::Selectable(label.c_str()))
						doc.select_link_next = TransitionLink(t->id);
					ImGui::PopID();
				}
			}

			// ---- Canvas -------------------------------------------------------------------

			void DrawStateNode(Doc& doc, const AnimGraphState& s, bool playing)
			{
				AnimGraphAsset& g = doc.anim;
				const int node = StateNode(s.id);
				const float width = ImGui::GetFontSize() * 10.f;

				PushTitleColor(playing ? kActive : kStateColor);
				ImNodes::BeginNode(node);
				BeginTitle();
				{
					// Badge : animation, blend space, or nothing yet.
					bool blend = false;
					for (const auto& b : g.blend_spaces)
						blend |= b.name == s.motion;
					if (s.motion.empty() || !g.IsMotion(s.motion))
						TitleBadge("?", IM_COL32(200, 110, 40, 255));
					else if (blend)
						TitleBadge("BS", IM_COL32(150, 90, 200, 255));
					else
						TitleBadge("A", IM_COL32(60, 140, 210, 255));
				}
				ImGui::TextUnformatted(s.name.c_str());
				if (g.entry_state == s.name)
					TitleDim("(entry)");
				EndTitle();

				ImNodes::BeginInputAttribute(InAttr(node), ImNodesPinShape_CircleFilled);
				if (s.motion.empty())
					ImGui::TextColored(kWarning, "no animation");
				else if (!g.IsMotion(s.motion))
					ImGui::TextColored(kWarning, "%s (missing)", s.motion.c_str());
				else
					ImGui::TextDisabled("%s", s.motion.c_str());
				ImNodes::EndInputAttribute();

				ImNodes::BeginOutputAttribute(OutAttr(node), ImNodesPinShape_TriangleFilled);
				int shown = 0, count = 0;
				for (const auto& t : g.transitions)
				{
					if (t.from != s.name)
						continue;
					++count;
					if (shown >= 4)
						continue;
					++shown;
					std::string line = "-> " + t.to + "  " + TransitionText(t);
					if (line.size() > 38)
						line = line.substr(0, 36) + "..";
					ImGui::TextUnformatted(line.c_str());
				}
				if (count > shown)
					ImGui::TextDisabled("+%d", count - shown);
				if (count == 0)
				{
					ImGui::Dummy(ImVec2(width - ImGui::CalcTextSize("out").x, 1.f));
					ImGui::SameLine();
					ImGui::TextDisabled("out");
				}
				ImNodes::EndOutputAttribute();
				ImNodes::EndNode();
				PopTitleColor();
			}

			void DrawSpecialNode(int node, const char* title, ImU32 color, const char* text)
			{
				PushTitleColor(color);
				ImNodes::BeginNode(node);
				BeginTitle();
				TitleBadge(node == kEntryNode ? ">" : "*", Lighter(color, 40));
				ImGui::TextUnformatted(title);
				EndTitle();
				ImNodes::BeginOutputAttribute(OutAttr(node), ImNodesPinShape_TriangleFilled);
				ImGui::TextDisabled("%s", text);
				ImNodes::EndOutputAttribute();
				ImNodes::EndNode();
				PopTitleColor();
			}

			void AddStateMenu(Doc& doc, const std::string& link_from)
			{
				AnimGraphAsset& g = doc.anim;
				auto create = [&](const std::string& base, const std::string& motion)
				{
					const ImVec2 pos = doc.popup_pos;
					const int id = AddState(doc, base, motion, pos);
					if (doc.dropped_attr)
					{
						const std::string to = FindState(g, id)->name;
						if (link_from == "<entry>")
							g.entry_state = to;
						else
							AddTransition(doc, link_from, to);
					}
				};

				if (ImGui::MenuItem("Empty state"))
					create("State", "");
				const auto motions = Motions(g);
				if (!motions.empty())
				{
					ImGui::Separator();
					ImGui::TextDisabled("State playing");
					for (const std::string& m : motions)
						if (ImGui::MenuItem(m.c_str()))
							create(m, m);
				}
			}

			void DrawCanvas(Doc& doc, AnimationSpriteComponent* debug)
			{
				AnimGraphAsset& g = doc.anim;
				const std::string playing = debug ? debug->GetCurrentState() : std::string();

				ImNodes::BeginNodeEditor();

				// Positions of the asset (load, undo) and of the new nodes.
				if (doc.place_all)
				{
					ImNodes::SetNodeGridSpacePos(kEntryNode, ImVec2(g.entry_x, g.entry_y));
					ImNodes::SetNodeGridSpacePos(kAnyNode, ImVec2(g.any_x, g.any_y));
					ImNodes::SnapNodeToGrid(kEntryNode);
					ImNodes::SnapNodeToGrid(kAnyNode);
					for (const auto& s : g.states)
					{
						ImNodes::SetNodeGridSpacePos(StateNode(s.id), ImVec2(s.x, s.y));
						ImNodes::SnapNodeToGrid(StateNode(s.id));     // the nodes are always on the grid
					}
				}
				for (const auto& [node, screen] : doc.place_now)
				{
					ImNodes::SetNodeScreenSpacePos(node, screen);
					ImNodes::SnapNodeToGrid(node);
				}

				DrawSpecialNode(kEntryNode, "Entry", kEntryColor, "start");
				DrawSpecialNode(kAnyNode, "Any State", kAnyColor, "from any state");
				// Only these exist in ImNodes this frame (a node added by a menu below is drawn next frame).
				std::unordered_set<int> drawn{ kEntryNode, kAnyNode };
				for (const auto& s : g.states)
				{
					DrawStateNode(doc, s, !playing.empty() && s.name == playing);
					drawn.insert(StateNode(s.id));
				}
				doc.drawn_nodes = drawn;
				doc.drawn_links.clear();

				// Links
				if (AnimGraphState* entry = g.FindState(g.entry_state))
				{
					ImNodes::Link(kEntryLink, OutAttr(kEntryNode), InAttr(StateNode(entry->id)));
					doc.drawn_links.insert(kEntryLink);
				}
				for (const auto& t : g.transitions)
				{
					AnimGraphState* to = g.FindState(t.to);
					AnimGraphState* from = t.from.empty() ? nullptr : g.FindState(t.from);
					if (!to || (!t.from.empty() && !from))
						continue;
					const int start = from ? OutAttr(StateNode(from->id)) : OutAttr(kAnyNode);
					const bool live = debug && t.to == playing && (t.from.empty() || from);
					if (live)
						ImNodes::PushColorStyle(ImNodesCol_Link, kActive);
					ImNodes::Link(TransitionLink(t.id), start, InAttr(StateNode(to->id)));
					doc.drawn_links.insert(TransitionLink(t.id));
					if (live)
						ImNodes::PopColorStyle();
				}

				ImNodes::EndNodeEditor();

				// Context menus
				// After EndNodeEditor : ImNodes::IsEditorHovered() only works inside the editor
				// scope, so the canvas child (and its ImNodes region) is checked here.
				const bool editor_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
				int hovered_node = 0;
				const bool node_hovered = ImNodes::IsNodeHovered(&hovered_node);
				if (editor_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
				    ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 25.f)
				{
					doc.popup_pos = ImGui::GetMousePos();
					doc.dropped_attr = 0;
					if (node_hovered)
					{
						doc.context_node = hovered_node;
						ImGui::OpenPopup("##node_menu");
					}
					else
						ImGui::OpenPopup("##add_state");
				}

				if (ImGui::BeginPopup("##add_state"))
				{
					ImGui::TextDisabled("Add a state");
					ImGui::Separator();
					std::string from;
					if (doc.dropped_attr)
					{
						const int node = NodeOfAttr(doc.dropped_attr);
						if (node == kEntryNode)
							from = "<entry>";
						else if (node != kAnyNode)
							if (AnimGraphState* s = FindState(g, StateOfNode(node)))
								from = s->name;
					}
					AddStateMenu(doc, from);
					ImGui::EndPopup();
				}

				if (ImGui::BeginPopup("##node_menu"))
				{
					const int node = doc.context_node;
					if (node >= 1000)
					{
						AnimGraphState* s = FindState(g, StateOfNode(node));
						if (s)
						{
							ImGui::TextDisabled("%s", s->name.c_str());
							ImGui::Separator();
							if (ImGui::MenuItem("Set as entry", nullptr, false, g.entry_state != s->name))
							{
								g.entry_state = s->name;
								doc.touched = true;
							}
							if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
							{
								const int id = s->id;
								Later(doc, [&doc, id] { DuplicateState(doc, id); });
							}
							if (ImGui::MenuItem("Delete", "Del"))
							{
								const int id = s->id;
								Later(doc, [&doc, id] { DeleteState(doc, id); doc.sel_node = 0; });
							}
						}
					}
					else
					{
						ImGui::TextDisabled(node == kEntryNode ? "Entry" : "Any State");
						ImGui::Separator();
						ImGui::TextDisabled("Drag its pin onto a state.");
					}
					ImGui::EndPopup();
				}

				// ---- Changes made in the canvas ---------------------------------------
				int start = 0, end = 0;
				if (ImNodes::IsLinkCreated(&start, &end))
				{
					if (!IsOutAttr(start))
						std::swap(start, end);
					const int from_node = NodeOfAttr(start);
					const int to_node = NodeOfAttr(end);
					AnimGraphState* to = to_node >= 1000 ? FindState(g, StateOfNode(to_node)) : nullptr;
					if (IsOutAttr(start) && !IsOutAttr(end) && to && from_node != to_node)
					{
						if (from_node == kEntryNode)
						{
							g.entry_state = to->name;
							doc.touched = true;
						}
						else if (from_node == kAnyNode)
							AddTransition(doc, "", to->name);
						else if (AnimGraphState* from = FindState(g, StateOfNode(from_node)))
							AddTransition(doc, from->name, to->name);
					}
				}

				int dropped = 0;
				if (ImNodes::IsLinkDropped(&dropped, false) && IsOutAttr(dropped))
				{
					// Pin dropped on empty space : new state linked to it.
					doc.dropped_attr = dropped;
					doc.popup_pos = ImGui::GetMousePos();
					ImGui::OpenPopup("##add_state");
				}

				int destroyed = 0;
				if (ImNodes::IsLinkDestroyed(&destroyed))
				{
					if (destroyed == kEntryLink)
						g.entry_state.clear();
					else
						DeleteTransition(doc, TransitionOfLink(destroyed));
					doc.touched = true;
				}

				// Positions back into the asset.
				auto sync = [&](int node, float& x, float& y)
				{
					if (!drawn.count(node))
						return;
					const ImVec2 p = ImNodes::GetNodeGridSpacePos(node);
					if (std::fabs(p.x - x) > 0.5f || std::fabs(p.y - y) > 0.5f)
					{
						x = std::round(p.x);
						y = std::round(p.y);
						if (!doc.place_all && doc.place_now.empty())
							doc.touched = true;
					}
				};
				sync(kEntryNode, g.entry_x, g.entry_y);
				sync(kAnyNode, g.any_x, g.any_y);
				for (auto& s : g.states)
					sync(StateNode(s.id), s.x, s.y);
			}

			void DeleteSelection(Doc& doc)
			{
				AnimGraphAsset& g = doc.anim;
				for (int link : SelectedLinks())
				{
					if (link == kEntryLink)
						g.entry_state.clear();
					else
						DeleteTransition(doc, TransitionOfLink(link));
				}
				for (int node : SelectedNodes())
					if (node >= 1000)
						DeleteState(doc, StateOfNode(node));
				ImNodes::ClearLinkSelection();
				ImNodes::ClearNodeSelection();
				ClearSelection(doc);
				doc.touched = true;
			}

			void DuplicateSelection(Doc& doc)
			{
				for (int node : SelectedNodes())
					if (node >= 1000)
						DuplicateState(doc, StateOfNode(node));
			}
		}


		// =====================================================================
		// BEHAVIOR TREE
		// =====================================================================

		namespace tree
		{
			std::vector<std::string> KeyNames(const BehaviorTreeAsset& t)
			{
				std::vector<std::string> out;
				for (const auto& k : t.keys)
					out.push_back(k.name);
				return out;
			}

			const BTKeyDesc* FindKey(const BehaviorTreeAsset& t, const std::string& name)
			{
				for (const auto& k : t.keys)
					if (k.name == name)
						return &k;
				return nullptr;
			}

			const char* Category(const std::string& type)
			{
				const BTNodeTypeDef* def = FindBTNodeType(type);
				return def ? def->category : "task";
			}

			bool IsTask(const BTNodeDesc& n) { return std::string(Category(n.type)) == "task"; }
			bool IsRoot(const BTNodeDesc& n) { return n.type == "Root"; }

			ImU32 NodeColor(const BTNodeDesc& n)
			{
				const std::string c = Category(n.type);
				if (c == "root")
					return kRootColor;
				if (c == "composite")
					return kCompositeColor;
				return kTaskColor;
			}

			BTAux* FindAux(BehaviorTreeAsset& t, int id, BTNodeDesc** owner = nullptr, bool* is_service = nullptr)
			{
				for (auto& n : t.nodes)
				{
					for (auto& d : n.decorators)
						if (d.id == id)
						{
							if (owner) *owner = &n;
							if (is_service) *is_service = false;
							return &d;
						}
					for (auto& s : n.services)
						if (s.id == id)
						{
							if (owner) *owner = &n;
							if (is_service) *is_service = true;
							return &s;
						}
				}
				return nullptr;
			}

			std::string Seconds(const std::string& v)
			{
				return Num(ToF(v, 0.f)) + "s";
			}

			std::string AbortSuffix(const BTAux& a)
			{
				const std::string abort = a.Get("abort", "none");
				return abort == "none" || abort.empty() ? std::string() : "  [abort " + abort + "]";
			}

			std::string AuxText(const BTAux& a, bool service)
			{
				std::string text;
				if (!service)
				{
					if (a.type == "Blackboard")
					{
						const std::string op = a.Get("op", "isSet");
						const std::string key = a.Get("key");
						if (op == "isSet")
							text = key + " is set";
						else if (op == "isNotSet")
							text = key + " is not set";
						else
							text = key + " " + op + " " + a.Get("value");
					}
					else if (a.type == "CallFunction")
						text = a.Get("function") + "()";
					else if (a.type == "Script")
						text = a.Get("class").empty() ? std::string("<class ?>") : a.Get("class");
					else if (a.type == "Cooldown")
						return "cooldown " + Seconds(a.Get("time"));
					else if (a.type == "Loop")
						return ToF(a.Get("count"), 0.f) <= 0.f ? "loop forever" : "loop " + a.Get("count") + "x";
					else if (a.type == "TimeLimit")
						return "time limit " + Seconds(a.Get("time"));
					else if (a.type == "ForceSuccess")
						return "force success";
					else
						text = a.type;

					if (ToBool(a.Get("inverse")))
						text = "not (" + text + ")";
					return "if " + text + AbortSuffix(a);
				}

				const std::string every = "  every " + Seconds(a.Get("interval", "0.5"));
				if (a.type == "CallFunction")
					return a.Get("function") + "()" + every;
				if (a.type == "Script")
					return (a.Get("class").empty() ? std::string("<class ?>") : a.Get("class")) + every;
				if (a.type == "DistanceTo")
					return "distance to " + a.Get("target") + " -> " + a.Get("result") + every;
				if (a.type == "FindNearest")
					return "nearest '" + a.Get("tag") + "' -> " + a.Get("result") + every;
				return a.type + every;
			}

			std::string NodeText(const BTNodeDesc& n)
			{
				const std::string& t = n.type;
				if (t == "Selector")
					return "first that succeeds";
				if (t == "Sequence")
					return "all in order";
				if (t == "Wait")
				{
					const float r = ToF(n.Get("random"), 0.f);
					return Seconds(n.Get("time")) + (r > 0.f ? " (+/-" + Num(r) + ")" : "");
				}
				if (t == "MoveTo")
					return "to " + n.Get("target") + "  at " + n.Get("speed");
				if (t == "SetValue")
					return n.Get("key") + " = " + n.Get("value");
				if (t == "ClearValue")
					return "clear " + n.Get("key");
				if (t == "SetAnimParam")
					return n.Get("kind") == "trigger" ? "trigger " + n.Get("param") : n.Get("param") + " = " + n.Get("value");
				if (t == "Log")
					return "\"" + n.Get("text") + "\"";
				if (t == "CallFunction")
					return n.Get("function") + "()";
				if (t == "Script")
					return n.Get("class").empty() ? std::string("<class ?>") : n.Get("class");
				if (t == "Finish")
					return n.Get("result");
				return {};
			}

			// Execution order (depth first from the root) : number shown in the title.
			std::unordered_map<int, int> ExecutionOrder(const BehaviorTreeAsset& t)
			{
				std::unordered_map<int, int> order;
				const BTNodeDesc* root = t.Root();
				if (!root)
					return order;
				int counter = 0;
				std::function<void(int)> visit = [&](int id)
				{
					for (const BTNodeDesc* c : t.ChildrenOf(id))
					{
						if (order.count(c->id))
							continue;
						order[c->id] = ++counter;
						visit(c->id);
					}
				};
				visit(root->id);
				return order;
			}

			bool IsDescendant(const BehaviorTreeAsset& t, int node, int ancestor)
			{
				for (int guard = 0; guard < 10000 && node != 0; ++guard)
				{
					if (node == ancestor)
						return true;
					const BTNodeDesc* n = t.Find(node);
					node = n ? n->parent : 0;
				}
				return false;
			}

			// ---- Edits --------------------------------------------------------------

			int AddNode(Doc& doc, const std::string& type, const ImVec2& screen, int parent)
			{
				BehaviorTreeAsset& t = doc.bt;
				BTNodeDesc n;
				n.id = t.NewId();
				n.type = type;
				n.parent = parent;
				if (const BTNodeTypeDef* def = FindBTNodeType(type))
					for (const BTParamDef& p : def->params)
						n.params.push_back({ p.name, p.default_value });
				// Under its siblings (the order of the children is their height).
				for (const BTNodeDesc* c : t.ChildrenOf(parent))
					n.y = std::max(n.y, c->y + 1.f);
				t.nodes.push_back(n);
				doc.place_screen.emplace_back(n.id, screen);
				doc.select_node_next = n.id;
				doc.touched = true;
				return n.id;
			}

			void AddAux(Doc& doc, int node_id, const std::string& type, bool service)
			{
				BTNodeDesc* n = doc.bt.Find(node_id);
				if (!n)
					return;
				BTAux a;
				a.id = doc.bt.NewId();
				a.type = type;
				if (const BTNodeTypeDef* def = FindBTNodeType(type, service ? "service" : "decorator"))
					for (const BTParamDef& p : def->params)
						a.params.push_back({ p.name, p.default_value });
				(service ? n->services : n->decorators).push_back(a);
				doc.sel_aux = a.id;
				doc.select_node_next = node_id;
				doc.touched = true;
			}

			void RemoveAux(Doc& doc, int aux_id)
			{
				for (auto& n : doc.bt.nodes)
				{
					auto erase = [&](std::vector<BTAux>& v)
					{
						v.erase(std::remove_if(v.begin(), v.end(), [aux_id](const BTAux& a) { return a.id == aux_id; }), v.end());
					};
					erase(n.decorators);
					erase(n.services);
				}
				if (doc.sel_aux == aux_id)
					doc.sel_aux = 0;
				doc.touched = true;
			}

			void MoveAux(Doc& doc, int aux_id, int delta)
			{
				for (auto& n : doc.bt.nodes)
				{
					for (std::vector<BTAux>* v : { &n.decorators, &n.services })
					{
						for (size_t i = 0; i < v->size(); ++i)
						{
							if ((*v)[i].id != aux_id)
								continue;
							const int j = static_cast<int>(i) + delta;
							if (j >= 0 && j < static_cast<int>(v->size()))
							{
								std::swap((*v)[i], (*v)[static_cast<size_t>(j)]);
								doc.touched = true;
							}
							return;
						}
					}
				}
			}

			void DeleteNode(Doc& doc, int id)
			{
				BehaviorTreeAsset& t = doc.bt;
				const BTNodeDesc* n = t.Find(id);
				if (!n || IsRoot(*n))
					return;
				for (auto& other : t.nodes)
					if (other.parent == id)
						other.parent = 0;     // kept, unattached
				t.nodes.erase(std::remove_if(t.nodes.begin(), t.nodes.end(), [id](const BTNodeDesc& d) { return d.id == id; }),
				              t.nodes.end());
				doc.touched = true;
			}

			int DuplicateNode(Doc& doc, int id)
			{
				BehaviorTreeAsset& t = doc.bt;
				const BTNodeDesc* n = t.Find(id);
				if (!n || IsRoot(*n))
					return 0;
				BTNodeDesc copy = *n;
				copy.id = t.NewId();
				t.nodes.push_back(copy);   // reserve the id before the decorators / services
				BTNodeDesc& c = t.nodes.back();
				for (auto& a : c.decorators)
					a.id = t.NewId();
				for (auto& a : c.services)
					a.id = t.NewId();
				// Same parent, just under.
				c.x += 30.f;
				c.y += 30.f;
				doc.place_all = true;
				doc.select_node_next = c.id;
				doc.touched = true;
				return c.id;
			}

			/** child -> parent (replaces its current parent). false if not possible. */
			bool Connect(Doc& doc, int parent_id, int child_id)
			{
				BehaviorTreeAsset& t = doc.bt;
				BTNodeDesc* parent = t.Find(parent_id);
				BTNodeDesc* child = t.Find(child_id);
				if (!parent || !child || parent_id == child_id)
					return false;
				if (IsTask(*parent))
				{
					ShowMessage(doc, "A task has no children (use a Selector or a Sequence)");
					return false;
				}
				if (IsRoot(*child))
					return false;
				if (IsDescendant(t, parent_id, child_id))
				{
					ShowMessage(doc, "That link would make a loop");
					return false;
				}
				// The root has one child.
				if (IsRoot(*parent))
					for (auto& other : t.nodes)
						if (other.parent == parent_id && other.id != child_id)
							other.parent = 0;
				child->parent = parent_id;
				doc.touched = true;
				return true;
			}

			/** Root on the left, children to the right, top to bottom in their order. */
			void Arrange(Doc& doc)
			{
				BehaviorTreeAsset& t = doc.bt;
				const BTNodeDesc* root = t.Root();
				if (!root)
					return;
				const float font = ImGui::GetCurrentContext() ? ImGui::GetFontSize() : 14.f;
				const float dx = font * 18.f;
				const float dy = font * 7.f;
				float next_y = 0.f;
				std::unordered_set<int> placed;

				std::function<float(int, int)> place = [&](int id, int depth) -> float
				{
					placed.insert(id);
					BTNodeDesc* n = t.Find(id);
					std::vector<int> children;
					for (const BTNodeDesc* c : t.ChildrenOf(id))
						if (!placed.count(c->id))
							children.push_back(c->id);
					float y;
					if (children.empty())
					{
						y = next_y;
						next_y += dy;
					}
					else
					{
						float first = 0.f, last = 0.f;
						for (size_t i = 0; i < children.size(); ++i)
						{
							const float cy = place(children[i], depth + 1);
							if (i == 0) first = cy;
							last = cy;
						}
						y = (first + last) * 0.5f;
					}
					// Taller nodes (decorators / services) push the next ones.
					if (n)
					{
						n->x = depth * dx;
						n->y = y;
						next_y += (n->decorators.size() + n->services.size()) * font * 1.4f;
					}
					return y;
				};
				place(root->id, 0);

				// Unattached nodes : below.
				next_y += dy;
				for (auto& n : t.nodes)
				{
					if (placed.count(n.id))
						continue;
					n.x = 0.f;
					n.y = next_y;
					next_y += dy;
				}
				doc.place_all = true;
				doc.center_pending = true;
				doc.touched = true;
			}

			bool Overlapping(const BehaviorTreeAsset& t)
			{
				std::set<std::pair<int, int>> seen;
				for (const auto& n : t.nodes)
					if (!seen.insert({ static_cast<int>(n.x), static_cast<int>(n.y) }).second)
						return true;
				return false;
			}

			void RenameKey(BehaviorTreeAsset& t, const std::string& from, const std::string& to)
			{
				for (auto& k : t.keys)
					if (k.name == from)
						k.name = to;
				auto fix = [&](const std::string& type, const char* category, std::vector<BTParam>& params)
				{
					const BTNodeTypeDef* def = category ? FindBTNodeType(type, category) : FindBTNodeType(type);
					if (!def)
						return;
					for (const BTParamDef& p : def->params)
						if (std::string(p.type) == "key")
							for (auto& v : params)
								if (v.name == p.name && v.value == from)
									v.value = to;
				};
				for (auto& n : t.nodes)
				{
					fix(n.type, nullptr, n.params);
					for (auto& d : n.decorators)
						fix(d.type, "decorator", d.params);
					for (auto& s : n.services)
						fix(s.type, "service", s.params);
				}
			}

			// ---- Debug ----------------------------------------------------------------

			BehaviorTreeComponent* DebugTarget(Doc& doc)
			{
				const std::string asset = AssetPathOf(doc.path);
				if (asset.empty())
					return nullptr;
				std::vector<BehaviorTreeComponent*> running = BehaviorTreeComponent::GetRunning(asset);
				if (running.empty())
					return nullptr;
				doc.debug_index = std::clamp(doc.debug_index, 0, static_cast<int>(running.size()) - 1);
				return running[static_cast<size_t>(doc.debug_index)];
			}

			std::vector<std::string> Issues(const BehaviorTreeAsset& t)
			{
				std::vector<std::string> out;
				const BTNodeDesc* root = t.Root();
				if (root && t.ChildrenOf(root->id).empty())
					out.push_back("The Root has no child : drag its pin onto a node.");
				int unattached = 0;
				for (const auto& n : t.nodes)
				{
					const std::string label = (n.name.empty() ? n.type : n.name);
					if (!IsRoot(n) && n.parent == 0)
						++unattached;
					if (std::string(Category(n.type)) == "composite" && t.ChildrenOf(n.id).empty())
						out.push_back(label + " : no child.");
					if (n.type == "Script" && n.Get("class").empty())
						out.push_back(label + " : no JavaScript class.");
					if (n.type == "CallFunction" && n.Get("function").empty())
						out.push_back(label + " : no function.");
					auto check_keys = [&](const std::string& type, const char* category, const std::vector<BTParam>& params)
					{
						const BTNodeTypeDef* def = category ? FindBTNodeType(type, category) : FindBTNodeType(type);
						if (!def)
							return;
						for (const BTParamDef& p : def->params)
						{
							if (std::string(p.type) != "key")
								continue;
							for (const auto& v : params)
								if (v.name == p.name && !v.value.empty() && !FindKey(t, v.value))
									out.push_back(label + " : unknown Blackboard key \"" + v.value + "\".");
						}
					};
					check_keys(n.type, nullptr, n.params);
					for (const auto& d : n.decorators)
					{
						check_keys(d.type, "decorator", d.params);
						if (d.type == "Script" && d.Get("class").empty())
							out.push_back(label + " : decorator without JavaScript class.");
					}
					for (const auto& s : n.services)
					{
						check_keys(s.type, "service", s.params);
						if (s.type == "Script" && s.Get("class").empty())
							out.push_back(label + " : service without JavaScript class.");
					}
				}
				if (unattached > 0)
					out.push_back(std::to_string(unattached) + " node(s) not linked (ignored by the game).");
				return out;
			}

			// ---- Left panel : Blackboard ------------------------------------------------

			void DrawBlackboard(Doc& doc, BehaviorTreeComponent* debug)
			{
				BehaviorTreeAsset& t = doc.bt;
				static const std::vector<std::string> kTypes = { "bool", "int", "float", "string", "vector", "actor" };

				ImGui::TextUnformatted("Blackboard");
				HelpMarker("Memory of the AI : values read and written by the nodes\n"
				           "and by JavaScript (this.blackboard / actor.getComponent(\"AI\").blackboard).");
				ImGui::Separator();

				int remove = -1;
				for (size_t i = 0; i < t.keys.size(); ++i)
				{
					BTKeyDesc& k = t.keys[i];
					ImGui::PushID(static_cast<int>(i));
					const float w = ImGui::GetContentRegionAvail().x;
					ImGui::SetNextItemWidth(w * 0.40f);
					std::string renamed;
					if (InputName("##name", k.name, renamed))
					{
						if (FindKey(t, renamed))
							ShowMessage(doc, "A key \"" + renamed + "\" already exists");
						else
						{
							RenameKey(t, k.name, renamed);
							doc.touched = true;
						}
					}
					ImGui::SameLine();
					ImGui::SetNextItemWidth(w * 0.25f);
					if (ComboStr("##type", k.type, kTypes))
						doc.touched = true;
					ImGui::SameLine();
					ImGui::SetNextItemWidth(std::max(30.f, w * 0.35f - ImGui::GetFrameHeight() - 12.f));
					if (debug)
					{
						const Blackboard& bb = debug->GetBlackboard();
						ImGui::AlignTextToFramePadding();
						if (bb.IsSet(k.name))
						{
							std::string value = bb.Get(k.name)->ToString();
							if (Actor* a = bb.GetActor(k.name))
								value = a->object_id_.empty() ? std::string("actor") : a->object_id_;
							ImGui::TextColored(kLive, "%s", value.c_str());
						}
						else
							ImGui::TextDisabled("unset");
					}
					else if (k.type == "actor")
					{
						ImGui::AlignTextToFramePadding();
						ImGui::TextDisabled("(none)");
					}
					else
					{
						if (InputStr("##def", k.default_value))
							doc.touched = true;
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Default value (empty : not set). Vector : \"1 2 0\".");
					}
					ImGui::SameLine();
					if (ImGui::Button("x"))
						remove = static_cast<int>(i);
					ImGui::PopID();
				}
				if (remove >= 0)
				{
					t.keys.erase(t.keys.begin() + remove);
					doc.touched = true;
				}
				if (ImGui::Button("+ Key"))
				{
					BTKeyDesc k;
					k.name = UniqueName("target", [&](const std::string& n) { return FindKey(t, n) != nullptr; });
					k.type = "actor";
					t.keys.push_back(k);
					doc.touched = true;
				}

				// Keys written at run time but not declared.
				if (debug)
				{
					std::vector<std::string> extra;
					for (const std::string& key : debug->GetBlackboard().GetKeys())
						if (!FindKey(t, key))
							extra.push_back(key);
					if (!extra.empty())
					{
						SectionTitle("Other values (game)");
						for (const std::string& key : extra)
						{
							const Blackboard& bb = debug->GetBlackboard();
							ImGui::TextUnformatted(key.c_str());
							ImGui::SameLine();
							if (bb.IsSet(key))
								ImGui::TextColored(kLive, "%s", bb.Get(key)->ToString().c_str());
							else
								ImGui::TextDisabled("unset");
						}
					}
				}

				SectionTitle("Running");
				if (debug)
				{
					std::string path;
					for (const std::string& name : debug->GetActiveNodeNames())
						path += (path.empty() ? "" : " > ") + name;
					ImGui::TextWrapped("%s", path.empty() ? "(idle)" : path.c_str());
				}
				else
					ImGui::TextDisabled("Play the game : the active nodes light up.");
			}

			// ---- Details --------------------------------------------------------------------

			void JsTemplate(const std::string& base, const std::string& name)
			{
				std::string code;
				if (base == "BTTask")
					code = "class " + name + " extends BTTask {\n"
					       "    // this.owner, this.blackboard, this.params\n"
					       "    Execute(dt) {\n"
					       "        return BT.Running;   // or BT.Success / BT.Failure (true / false)\n"
					       "    }\n"
					       "    Tick(dt) {\n"
					       "        return BT.Success;\n"
					       "    }\n"
					       "    Abort() {\n"
					       "    }\n"
					       "}\n";
				else if (base == "BTDecorator")
					code = "class " + name + " extends BTDecorator {\n"
					       "    Check() {\n"
					       "        return this.blackboard.has(\"target\");\n"
					       "    }\n"
					       "}\n";
				else
					code = "class " + name + " extends BTService {\n"
					       "    Activated() {\n"
					       "    }\n"
					       "    Tick(dt) {\n"
					       "    }\n"
					       "    Deactivated() {\n"
					       "    }\n"
					       "}\n";
				ImGui::SetClipboardText(code.c_str());
			}

			/** Fields of a node / decorator / service from the schema of its type. */
			void DrawParams(Doc& doc, const BTNodeTypeDef* def, std::vector<BTParam>& params)
			{
				if (!def)
				{
					ImGui::TextColored(kWarning, "Unknown type");
					return;
				}
				if (def->params.empty())
				{
					ImGui::TextDisabled("No parameter.");
					return;
				}
				auto value_of = [&](const char* name) -> std::string&
				{
					for (auto& p : params)
						if (p.name == name)
							return p.value;
					params.push_back({ name, "" });
					return params.back().value;
				};

				if (!BeginFields("##params"))
					return;
				for (const BTParamDef& p : def->params)
				{
					ImGui::PushID(p.name);
					std::string& v = value_of(p.name);
					std::string label = p.name;
					std::replace(label.begin(), label.end(), '_', ' ');
					if (!label.empty())
						label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
					FieldLabel(label.c_str(), p.help);
					const std::string type = p.type;

					if (type == "float")
					{
						float f = ToF(v, ToF(p.default_value, 0.f));
						if (ImGui::DragFloat("##v", &f, 0.05f))
						{
							v = Num(f);
							doc.touched = true;
						}
					}
					else if (type == "int")
					{
						int i = static_cast<int>(ToF(v, ToF(p.default_value, 0.f)));
						if (ImGui::DragInt("##v", &i, 0.2f))
						{
							v = std::to_string(i);
							doc.touched = true;
						}
					}
					else if (type == "bool")
					{
						bool b = ToBool(v);
						if (ImGui::Checkbox("##v", &b))
						{
							v = b ? "true" : "false";
							doc.touched = true;
						}
					}
					else if (type == "key")
					{
						if (ComboStr("##v", v, KeyNames(doc.bt), "(none)"))
							doc.touched = true;
					}
					else if (type == "enum")
					{
						std::vector<std::string> options;
						std::stringstream ss(p.options ? p.options : "");
						std::string o;
						while (std::getline(ss, o, '|'))
							options.push_back(o);
						if (ComboStr("##v", v, options))
							doc.touched = true;
					}
					else if (type == "class")
					{
						const std::string base = p.options ? p.options : "BTTask";
						const float button = ImGui::CalcTextSize("JS").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.f;
						ImGui::SetNextItemWidth(std::max(40.f, ImGui::GetContentRegionAvail().x - button - 12.f));
						const auto& classes = ScriptClasses(base);
						const bool known = v.empty() || std::find(classes.begin(), classes.end(), v) != classes.end();
						if (!known)
							ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
						if (InputStr("##v", v))
							doc.touched = true;
						if (!known)
							ImGui::PopStyleColor();
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(known ? "class X extends %s (any .js of assets/)" : "No class extends %s with this name yet", base.c_str());
						// Pick an existing class.
						ImGui::SameLine(0.f, 6.f);
						if (ImGui::ArrowButton("##pick", ImGuiDir_Down))
							ImGui::OpenPopup("##classes");
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip(classes.empty() ? "No class extends %s yet" : "Classes that extend %s", base.c_str());
						if (ImGui::BeginPopup("##classes"))
						{
							for (const std::string& c : classes)
								if (ImGui::Selectable(c.c_str(), c == v))
								{
									v = c;
									doc.touched = true;
								}
							ImGui::EndPopup();
						}
						ImGui::SameLine(0.f, 6.f);
						if (ImGui::Button("JS"))
							JsTemplate(base, v.empty() ? std::string("My") + base.substr(2) : v);
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Copy a JavaScript class template (paste it in a .js of assets/).");
					}
					else
					{
						if (InputStr("##v", v))
							doc.touched = true;
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			void AuxMenu(Doc& doc, int node_id, bool service)
			{
				for (const BTNodeTypeDef& def : GetBTNodeTypes())
				{
					if (std::string(def.category) != (service ? "service" : "decorator"))
						continue;
					if (ImGui::MenuItem(def.type))
						AddAux(doc, node_id, def.type, service);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", def.description);
				}
			}

			void DrawAuxList(Doc& doc, BTNodeDesc& n, bool service)
			{
				std::vector<BTAux>& list = service ? n.services : n.decorators;
				SectionTitle(service ? "Services (run while the node is active)" : "Decorators (conditions, modifiers)");
				for (const BTAux& a : list)
				{
					ImGui::PushID(a.id);
					ImGui::PushStyleColor(ImGuiCol_Text, service ? kServiceText : kDecoratorText);
					if (ImGui::Selectable(AuxText(a, service).c_str(), doc.sel_aux == a.id))
						doc.sel_aux = a.id;
					ImGui::PopStyleColor();
					ImGui::PopID();
				}
				const char* add = service ? "+ Service" : "+ Decorator";
				if (ImGui::Button(add))
					ImGui::OpenPopup(service ? "##add_service" : "##add_decorator");
				if (ImGui::BeginPopup(service ? "##add_service" : "##add_decorator"))
				{
					AuxMenu(doc, n.id, service);
					ImGui::EndPopup();
				}
			}

			void DrawDetails(Doc& doc, BehaviorTreeComponent* debug)
			{
				BehaviorTreeAsset& t = doc.bt;

				if (doc.sel_aux)
				{
					BTNodeDesc* owner = nullptr;
					bool service = false;
					if (BTAux* a = FindAux(t, doc.sel_aux, &owner, &service))
					{
						const BTNodeTypeDef* def = FindBTNodeType(a->type, service ? "service" : "decorator");
						ImGui::TextColored(service ? kServiceText : kDecoratorText, "%s   %s", service ? "Service" : "Decorator", a->type.c_str());
						ImGui::TextDisabled("on %s", owner->name.empty() ? owner->type.c_str() : owner->name.c_str());
						ImGui::Separator();
						if (def)
							ImGui::TextWrapped("%s", def->description);
						ImGui::Spacing();
						DrawParams(doc, def, a->params);
						ImGui::Spacing();
						const int id = a->id;
						if (ImGui::Button("Up"))
							Later(doc, [&doc, id] { MoveAux(doc, id, -1); });
						ImGui::SameLine();
						if (ImGui::Button("Down"))
							Later(doc, [&doc, id] { MoveAux(doc, id, +1); });
						ImGui::SameLine();
						if (ImGui::Button("Remove"))
							Later(doc, [&doc, id] { RemoveAux(doc, id); });
						ImGui::SameLine();
						if (ImGui::Button("Back to the node"))
							doc.sel_aux = 0;
						return;
					}
					doc.sel_aux = 0;
				}

				BTNodeDesc* n = doc.sel_node ? t.Find(doc.sel_node) : nullptr;
				if (!n)
				{
					ImGui::TextDisabled("Nothing selected");
					ImGui::Spacing();
					ImGui::TextWrapped(
						"Right click on the graph : add a node. Drag the right pin of a node onto another one : child.\n"
						"Children run from the top to the bottom (their height in the graph).\n"
						"Right click on a node : decorators (conditions) and services.\n\n"
						"Use : actor.addComponent(\"AI\", { behaviorTree: \"%s\" }) in JavaScript, or\n"
						"actor->AddComponent<lynx::BehaviorTreeComponent>().behavior_tree = \"%s\" in C++.",
						AssetPathOf(doc.path).c_str(), AssetPathOf(doc.path).c_str());
					return;
				}

				const BTNodeTypeDef* def = FindBTNodeType(n->type);
				ImGui::Text("%s", n->type.c_str());
				if (debug)
				{
					const auto active = debug->GetActiveNodeIds();
					if (std::find(active.begin(), active.end(), n->id) != active.end())
					{
						ImGui::SameLine();
						ImGui::TextColored(kLive, "  running");
					}
				}
				ImGui::Separator();
				if (def)
					ImGui::TextWrapped("%s", def->description);
				ImGui::Spacing();

				if (!IsRoot(*n))
				{
					if (BeginFields("##name"))
					{
						FieldLabel("Title", "Free name shown on the node (empty : the type).");
						if (InputStr("##title", n->name))
							doc.touched = true;
						ImGui::EndTable();
					}
				}
				DrawParams(doc, def, n->params);

				if (!IsRoot(*n))
				{
					DrawAuxList(doc, *n, false);
					DrawAuxList(doc, *n, true);
				}

				ImGui::Spacing();
				ImGui::Separator();
				if (!IsRoot(*n))
				{
					const int id = n->id;
					if (ImGui::Button("Duplicate"))
						Later(doc, [&doc, id] { DuplicateNode(doc, id); });
					ImGui::SameLine();
					if (n->parent && ImGui::Button("Unlink"))
					{
						n->parent = 0;
						doc.touched = true;
					}
					if (n->parent)
						ImGui::SameLine();
					if (ImGui::Button("Delete"))
						Later(doc, [&doc, id] { DeleteNode(doc, id); doc.sel_node = 0; });
				}
			}

			// ---- Canvas ---------------------------------------------------------------------

			void DrawNode(Doc& doc, BTNodeDesc& n, int order, const std::unordered_set<int>& active)
			{
				const bool running = active.count(n.id) > 0;
				const float width = ImGui::GetFontSize() * 12.f;
				const std::string category = Category(n.type);

				PushTitleColor(running ? kActive : NodeColor(n));
				ImNodes::BeginNode(n.id);

				BeginTitle();
				{
					const char* glyph = "*";
					ImU32 badge = IM_COL32(120, 120, 130, 255);
					const std::string& ty = n.type;
					if (ty == "Root")              { glyph = ">";   badge = IM_COL32(90, 90, 110, 255); }
					else if (ty == "Selector")     { glyph = "?";   badge = IM_COL32(64, 132, 210, 255); }
					else if (ty == "Sequence")     { glyph = "->";  badge = IM_COL32(64, 170, 120, 255); }
					else if (ty == "Wait")         { glyph = "zz";  badge = IM_COL32(120, 110, 190, 255); }
					else if (ty == "MoveTo")       { glyph = ">>";  badge = IM_COL32(60, 160, 190, 255); }
					else if (ty == "SetValue")     { glyph = "=";   badge = IM_COL32(200, 140, 50, 255); }
					else if (ty == "ClearValue")   { glyph = "x";   badge = IM_COL32(190, 80, 80, 255); }
					else if (ty == "SetAnimParam") { glyph = "~";   badge = IM_COL32(220, 110, 150, 255); }
					else if (ty == "Log")          { glyph = "i";   badge = IM_COL32(110, 130, 150, 255); }
					else if (ty == "CallFunction") { glyph = "f()"; badge = IM_COL32(200, 160, 40, 255); }
					else if (ty == "Script")       { glyph = "JS";  badge = IM_COL32(220, 180, 40, 255); }
					else if (ty == "Finish")       { glyph = "!";   badge = IM_COL32(200, 70, 70, 255); }
					TitleBadge(glyph, badge);
				}
				if (order > 0)
				{
					ImGui::TextColored(kTitleDim, "%d", order);
					ImGui::SameLine();
				}
				ImGui::TextUnformatted(n.name.empty() ? n.type.c_str() : n.name.c_str());
				if (!n.name.empty())
					TitleDim(n.type.c_str());
				EndTitle();

				// Decorators (above, like Unreal)
				for (const BTAux& a : n.decorators)
				{
					ImGui::PushID(a.id);
					ImGui::PushStyleColor(ImGuiCol_Text, kDecoratorText);
					std::string text = AuxText(a, false);
					if (text.size() > 34)
						text = text.substr(0, 32) + "..";
					if (ImGui::Selectable(text.c_str(), doc.sel_aux == a.id, 0, ImVec2(width, 0.f)))
					{
						doc.sel_aux = a.id;
						doc.sel_node = n.id;
						doc.select_node_next = n.id;
						g_aux_clicked = true;
					}
					ImGui::PopStyleColor();
					ImGui::PopID();
				}

				// Body : input pin (parent) + summary
				const std::string body = NodeText(n);
				if (!IsRoot(n))
				{
					ImNodes::BeginInputAttribute(InAttr(n.id), ImNodesPinShape_CircleFilled);
					if (body.empty())
						ImGui::Dummy(ImVec2(width, 1.f));
					else
					{
						std::string text = body.size() > 34 ? body.substr(0, 32) + ".." : body;
						const bool missing = (n.type == "Script" && n.Get("class").empty()) ||
						                     (n.type == "CallFunction" && n.Get("function").empty());
						if (missing)
							ImGui::TextColored(kWarning, "%s", text.c_str());
						else
							ImGui::TextUnformatted(text.c_str());
					}
					ImNodes::EndInputAttribute();
				}

				// Services (below)
				for (const BTAux& a : n.services)
				{
					ImGui::PushID(a.id);
					ImGui::PushStyleColor(ImGuiCol_Text, kServiceText);
					std::string text = AuxText(a, true);
					if (text.size() > 34)
						text = text.substr(0, 32) + "..";
					if (ImGui::Selectable(text.c_str(), doc.sel_aux == a.id, 0, ImVec2(width, 0.f)))
					{
						doc.sel_aux = a.id;
						doc.sel_node = n.id;
						doc.select_node_next = n.id;
						g_aux_clicked = true;
					}
					ImGui::PopStyleColor();
					ImGui::PopID();
				}

				// Output pin (children)
				if (category != "task")
				{
					ImNodes::BeginOutputAttribute(OutAttr(n.id), ImNodesPinShape_TriangleFilled);
					const char* label = IsRoot(n) ? "start" : "children";
					ImGui::Dummy(ImVec2(std::max(1.f, width - ImGui::CalcTextSize(label).x), 1.f));
					ImGui::SameLine();
					ImGui::TextDisabled("%s", label);
					ImNodes::EndOutputAttribute();
				}
				else if (IsRoot(n))
				{
					ImNodes::BeginStaticAttribute(n.id * 4 + 3);
					ImGui::Dummy(ImVec2(width, 1.f));
					ImNodes::EndStaticAttribute();
				}

				ImNodes::EndNode();
				PopTitleColor();
			}

			void AddNodeMenu(Doc& doc)
			{
				auto section = [&](const char* category, const char* title)
				{
					ImGui::TextDisabled("%s", title);
					for (const BTNodeTypeDef& def : GetBTNodeTypes())
					{
						if (std::string(def.category) != category)
							continue;
						if (ImGui::MenuItem(def.type))
						{
							int parent = 0;
							if (doc.dropped_attr)
								parent = NodeOfAttr(doc.dropped_attr);
							const int id = AddNode(doc, def.type, doc.popup_pos, 0);
							if (parent && !Connect(doc, parent, id))
								doc.bt.Find(id)->parent = 0;
						}
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("%s", def.description);
					}
				};
				section("composite", "Composites");
				ImGui::Separator();
				section("task", "Tasks");
			}

			void DrawCanvas(Doc& doc, BehaviorTreeComponent* debug)
			{
				BehaviorTreeAsset& t = doc.bt;
				std::unordered_set<int> active;
				if (debug)
					for (int id : debug->GetActiveNodeIds())
						active.insert(id);

				g_aux_clicked = false;
				ImNodes::BeginNodeEditor();

				if (doc.place_all)
					for (const auto& n : t.nodes)
					{
						ImNodes::SetNodeGridSpacePos(n.id, ImVec2(n.x, n.y));
						ImNodes::SnapNodeToGrid(n.id);     // the nodes are always on the grid
					}
				for (const auto& [node, screen] : doc.place_now)
				{
					ImNodes::SetNodeScreenSpacePos(node, screen);
					ImNodes::SnapNodeToGrid(node);
				}

				const auto order = ExecutionOrder(t);
				// Only these exist in ImNodes this frame (a node added by a menu below is drawn next frame).
				std::unordered_set<int> drawn;
				for (auto& n : t.nodes)
				{
					auto it = order.find(n.id);
					DrawNode(doc, n, it != order.end() ? it->second : 0, active);
					drawn.insert(n.id);
				}
				doc.drawn_nodes = drawn;
				doc.drawn_links.clear();

				for (const auto& n : t.nodes)
				{
					if (n.parent == 0)
						continue;
					const BTNodeDesc* parent = t.Find(n.parent);
					if (!parent || IsTask(*parent))
						continue;
					const bool live = active.count(n.id) && active.count(n.parent);
					if (live)
						ImNodes::PushColorStyle(ImNodesCol_Link, kActive);
					ImNodes::Link(n.id, OutAttr(n.parent), InAttr(n.id));
					doc.drawn_links.insert(n.id);
					if (live)
						ImNodes::PopColorStyle();
				}

				ImNodes::EndNodeEditor();

				// After EndNodeEditor : ImNodes::IsEditorHovered() only works inside the editor
				// scope, so the canvas child (and its ImNodes region) is checked here.
				const bool editor_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
				int hovered_node = 0;
				const bool node_hovered = ImNodes::IsNodeHovered(&hovered_node);
				if (editor_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
				    ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 25.f)
				{
					doc.popup_pos = ImGui::GetMousePos();
					doc.dropped_attr = 0;
					if (node_hovered)
					{
						doc.context_node = hovered_node;
						ImGui::OpenPopup("##node_menu");
					}
					else
						ImGui::OpenPopup("##add_node");
				}

				// Click on a node (not on one of its decorators / services) : the node itself.
				if (node_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_aux_clicked)
					doc.sel_aux = 0;

				if (ImGui::BeginPopup("##add_node"))
				{
					AddNodeMenu(doc);
					ImGui::EndPopup();
				}

				if (ImGui::BeginPopup("##node_menu"))
				{
					BTNodeDesc* n = t.Find(doc.context_node);
					if (n)
					{
						ImGui::TextDisabled("%s", n->name.empty() ? n->type.c_str() : n->name.c_str());
						ImGui::Separator();
						if (!IsRoot(*n))
						{
							if (ImGui::BeginMenu("Add decorator"))
							{
								AuxMenu(doc, n->id, false);
								ImGui::EndMenu();
							}
							if (ImGui::BeginMenu("Add service"))
							{
								AuxMenu(doc, n->id, true);
								ImGui::EndMenu();
							}
							ImGui::Separator();
							const int id = n->id;
							if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
								Later(doc, [&doc, id] { DuplicateNode(doc, id); });
							if (ImGui::MenuItem("Unlink from parent", nullptr, false, n->parent != 0))
							{
								n->parent = 0;
								doc.touched = true;
							}
							if (ImGui::MenuItem("Delete", "Del"))
								Later(doc, [&doc, id] { DeleteNode(doc, id); doc.sel_node = 0; });
						}
						else
							ImGui::TextDisabled("Drag its pin onto the first node.");
					}
					ImGui::EndPopup();
				}

				// ---- Changes made in the canvas ---------------------------------------
				int start = 0, end = 0;
				if (ImNodes::IsLinkCreated(&start, &end))
				{
					if (!IsOutAttr(start))
						std::swap(start, end);
					if (IsOutAttr(start) && !IsOutAttr(end))
						Connect(doc, NodeOfAttr(start), NodeOfAttr(end));
				}

				int dropped = 0;
				if (ImNodes::IsLinkDropped(&dropped, false) && IsOutAttr(dropped))
				{
					doc.dropped_attr = dropped;
					doc.popup_pos = ImGui::GetMousePos();
					ImGui::OpenPopup("##add_node");
				}

				int destroyed = 0;
				if (ImNodes::IsLinkDestroyed(&destroyed))
				{
					if (BTNodeDesc* child = t.Find(destroyed))
					{
						child->parent = 0;
						doc.touched = true;
					}
				}

				for (auto& n : t.nodes)
				{
					if (!drawn.count(n.id))
						continue;
					const ImVec2 p = ImNodes::GetNodeGridSpacePos(n.id);
					if (std::fabs(p.x - n.x) > 0.5f || std::fabs(p.y - n.y) > 0.5f)
					{
						n.x = std::round(p.x);
						n.y = std::round(p.y);
						if (!doc.place_all && doc.place_now.empty())
							doc.touched = true;
					}
				}
			}

			void DeleteSelection(Doc& doc)
			{
				for (int link : SelectedLinks())
					if (BTNodeDesc* child = doc.bt.Find(link))
						child->parent = 0;
				for (int node : SelectedNodes())
					DeleteNode(doc, node);
				ImNodes::ClearLinkSelection();
				ImNodes::ClearNodeSelection();
				ClearSelection(doc);
				doc.touched = true;
			}

			void DuplicateSelection(Doc& doc)
			{
				for (int node : SelectedNodes())
					DuplicateNode(doc, node);
			}
		}


		// Node / link drawn by the canvas (ImNodes asserts on unknown ids).
		bool NodeExists(Doc& doc, int node)
		{
			if (doc.kind == Kind::Tree)
				return doc.bt.Find(node) != nullptr;
			return node == anim::kEntryNode || node == anim::kAnyNode ||
			       (node >= 1000 && anim::FindState(doc.anim, anim::StateOfNode(node)));
		}

		bool LinkExists(Doc& doc, int link)
		{
			if (doc.kind == Kind::Tree)
			{
				const BTNodeDesc* child = doc.bt.Find(link);
				const BTNodeDesc* parent = child ? doc.bt.Find(child->parent) : nullptr;
				return parent && !tree::IsTask(*parent);
			}
			if (link == anim::kEntryLink)
				return doc.anim.FindState(doc.anim.entry_state) != nullptr;
			const AnimGraphTransition* t = anim::FindTransition(doc.anim, anim::TransitionOfLink(link));
			return t && doc.anim.FindState(t->to) && (t->from.empty() || doc.anim.FindState(t->from));
		}


		// =====================================================================
		// Window
		// =====================================================================

		void Splitter(const char* id, float& size, float min_size, float max_size)
		{
			const float thickness = 5.f;
			ImGui::PushID(id);
			ImGui::InvisibleButton("##split", ImVec2(thickness, -1.f));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			if (ImGui::IsItemActive())
				size = std::clamp(size + ImGui::GetIO().MouseDelta.x, min_size, max_size);
			ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			                                          ImGui::IsItemActive() ? kSelection : IM_COL32(60, 60, 66, 255));
			ImGui::PopID();
		}

		void Shortcuts(Doc& doc, bool canvas_hovered)
		{
			const ImGuiIO& io = ImGui::GetIO();
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
				Save(doc);
			if (io.WantTextInput)
				return;
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
				io.KeyShift ? Redo(doc) : Undo(doc);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
				Redo(doc);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
				doc.kind == Kind::Anim ? anim::DuplicateSelection(doc) : tree::DuplicateSelection(doc);
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && (canvas_hovered || ImNodes::NumSelectedNodes() + ImNodes::NumSelectedLinks() > 0))
			{
				if (doc.kind == Kind::Tree && doc.sel_aux)
					tree::RemoveAux(doc, doc.sel_aux);
				else
					doc.kind == Kind::Anim ? anim::DeleteSelection(doc) : tree::DeleteSelection(doc);
			}
		}

		void CenterView(Doc& doc, const ImVec2& canvas_size)
		{
			// The start (Entry / Root) near the left, at mid height.
			ImVec2 start(0.f, 0.f);
			if (doc.kind == Kind::Anim)
				start = ImVec2(doc.anim.entry_x, doc.anim.entry_y);
			else if (const BTNodeDesc* root = doc.bt.Root())
				start = ImVec2(root->x, root->y);
			// Panning is in grid units : the screen offsets are divided by the zoom.
			const float zoom = ImNodes::EditorContextGetZoom();
			ImNodes::EditorContextResetPanning(ImVec2(-start.x + 40.f / zoom, -start.y + canvas_size.y * 0.4f / zoom));
		}

		void DrawToolbar(Doc& doc, int debug_count)
		{
			if (ImGui::Button("Save"))
				Save(doc);
			ImGui::SameLine();
			ImGui::BeginDisabled(doc.undo.empty() && !doc.touched);
			if (ImGui::Button("Undo"))
				Undo(doc);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(doc.redo.empty());
			if (ImGui::Button("Redo"))
				Redo(doc);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
			if (doc.kind == Kind::Tree)
			{
				if (ImGui::Button("Arrange"))
					tree::Arrange(doc);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Places the nodes from the Root (left) to the right, keeping their order.");
				ImGui::SameLine();
			}
			if (ImGui::Button("Center"))
				doc.center_pending = true;
			ImGui::SameLine();
			// Zoom (mouse wheel on the graph) : click = 100 %
			{
				char zoom[32];
				std::snprintf(zoom, sizeof(zoom), "%d %%##zoom", static_cast<int>(std::lround(ImNodes::EditorContextGetZoom() * 100.f)));
				if (ImGui::Button(zoom))
					doc.reset_zoom = true;
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Zoom : mouse wheel on the graph. Click : 100 %%.\nRight button drag : move the view.");
			}

			// Instances of the running game.
			if (debug_count > 0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("|");
				ImGui::SameLine();
				ImGui::TextColored(kLive, "Debug");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
				std::vector<std::string> names;
				if (doc.kind == Kind::Anim)
				{
					for (AnimationSpriteComponent* c : AnimationSpriteComponent::GetWithGraph(AssetPathOf(doc.path)))
						names.push_back(c->GetOwner() && !c->GetOwner()->object_id_.empty() ? c->GetOwner()->object_id_ : std::string("actor"));
				}
				else
				{
					for (BehaviorTreeComponent* c : BehaviorTreeComponent::GetRunning(AssetPathOf(doc.path)))
						names.push_back(c->GetOwner() && !c->GetOwner()->object_id_.empty() ? c->GetOwner()->object_id_ : std::string("actor"));
				}
				doc.debug_index = std::clamp(doc.debug_index, 0, std::max(0, static_cast<int>(names.size()) - 1));
				const std::string preview = names.empty() ? std::string() : names[static_cast<size_t>(doc.debug_index)];
				if (ImGui::BeginCombo("##debug", preview.c_str()))
				{
					for (size_t i = 0; i < names.size(); ++i)
					{
						ImGui::PushID(static_cast<int>(i));
						if (ImGui::Selectable(names[i].c_str(), static_cast<int>(i) == doc.debug_index))
							doc.debug_index = static_cast<int>(i);
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
			}

			// Problems of the graph.
			const std::vector<std::string> issues = doc.kind == Kind::Anim ? anim::Issues(doc.anim) : tree::Issues(doc.bt);
			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();
			if (issues.empty())
				ImGui::TextDisabled("OK");
			else
			{
				ImGui::TextColored(kWarning, "%d warning%s", static_cast<int>(issues.size()), issues.size() > 1 ? "s" : "");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					for (const std::string& i : issues)
						ImGui::BulletText("%s", i.c_str());
					ImGui::EndTooltip();
				}
			}

			if (!doc.message.empty() && ImGui::GetTime() - doc.message_time < 5.0)
			{
				ImGui::SameLine();
				ImGui::TextColored(kWarning, "%s", doc.message.c_str());
			}
		}

		void DrawDoc(Doc& doc, ImGuiID dock_id)
		{
			CheckDisk(doc);

			const char* kind = doc.kind == Kind::Anim ? "Anim Graph" : "Behavior Tree";
			const std::string title = doc.path.filename().string() + (doc.dirty || doc.touched ? " *" : "") +
			                          "###graph:" + doc.path.generic_string();

			if (dock_id != 0)
				ImGui::SetNextWindowDockID(dock_id, ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(1200.f, 750.f), ImGuiCond_FirstUseEver);
			if (doc.focus_next)
			{
				ImGui::SetNextWindowFocus();
				doc.focus_next = false;
			}

			bool window_open = true;
			const ImGuiWindowFlags flags = (doc.dirty ? ImGuiWindowFlags_UnsavedDocument : 0) |
			                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
			const bool visible = ImGui::Begin(title.c_str(), &window_open, flags);

			if (!window_open)
			{
				if (doc.dirty || doc.touched)
					doc.ask_close = true;
				else
					doc.open = false;
			}

			if (visible)
			{
				ImNodes::EditorContextSet(doc.nodes);

				AnimationSpriteComponent* anim_debug = nullptr;
				BehaviorTreeComponent* tree_debug = nullptr;
				std::vector<AnimationSpriteComponent*> anim_all;
				int debug_count = 0;
				if (doc.kind == Kind::Anim)
				{
					anim_debug = anim::DebugTarget(doc, &anim_all);
					debug_count = static_cast<int>(anim_all.size());
				}
				else
				{
					tree_debug = tree::DebugTarget(doc);
					debug_count = static_cast<int>(BehaviorTreeComponent::GetRunning(AssetPathOf(doc.path)).size());
				}

				if (doc.changed_on_disk)
				{
					ImGui::TextColored(kWarning, "The file changed on disk.");
					ImGui::SameLine();
					if (ImGui::SmallButton("Reload"))
						Load(doc);
					ImGui::SameLine();
					if (ImGui::SmallButton("Keep my version"))
						doc.changed_on_disk = false;
				}

				DrawToolbar(doc, debug_count);

				const ImGuiStyle& style = ImGui::GetStyle();
				const float total_w = ImGui::GetContentRegionAvail().x;
				doc.left_width = std::clamp(doc.left_width, 160.f, std::max(160.f, total_w * 0.4f));
				doc.right_width = std::clamp(doc.right_width, 200.f, std::max(200.f, total_w * 0.45f));
				const float center_w = std::max(100.f, total_w - doc.left_width - doc.right_width - 10.f - style.ItemSpacing.x * 4.f);
				const float body_h = ImGui::GetContentRegionAvail().y;

				// Left : parameters / animations / blackboard
				ImGui::BeginChild("##left", ImVec2(doc.left_width, body_h), ImGuiChildFlags_Borders);
				ImGui::TextDisabled("%s", kind);
				if (doc.kind == Kind::Anim)
					anim::DrawLeft(doc, anim_debug);
				else
					tree::DrawBlackboard(doc, tree_debug);
				ImGui::EndChild();

				ImGui::SameLine(0.f, 0.f);
				Splitter("##split_l", doc.left_width, 160.f, total_w * 0.4f);
				ImGui::SameLine(0.f, 0.f);

				// Center : the graph
				bool canvas_hovered = false;
				ImGui::BeginChild("##canvas", ImVec2(center_w, body_h), ImGuiChildFlags_Borders,
				                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
				{
					if (doc.reset_zoom)
					{
						const ImVec2 cursor = ImGui::GetCursorScreenPos();
						const ImVec2 avail = ImGui::GetContentRegionAvail();
						const ImVec2 center(cursor.x + avail.x * 0.5f, cursor.y + avail.y * 0.5f);
						ImNodes::EditorContextSetZoom(1.f, center);
						doc.reset_zoom = false;
					}
					if (doc.center_pending)
					{
						CenterView(doc, ImGui::GetContentRegionAvail());
						doc.center_pending = false;
					}
					// Nodes created during this frame (menus) are placed next frame, once drawn.
					doc.place_now.swap(doc.place_screen);
					doc.place_screen.clear();
					const bool placing = doc.place_all || !doc.place_now.empty();
					if (doc.kind == Kind::Anim)
						anim::DrawCanvas(doc, anim_debug);
					else
						tree::DrawCanvas(doc, tree_debug);
					canvas_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
					doc.place_all = false;
					doc.place_now.clear();
					// Where a new node landed belongs to the step that created it (not a new undo step).
					if (placing && !doc.touched)
					{
						doc.snapshot = Serialize(doc);
						doc.dirty = doc.snapshot != doc.saved;
					}

					// Selection asked by the code (new node, list of the Details...) :
					// once the node / link exists in ImNodes (drawn this frame).
					if (doc.select_node_next || doc.select_link_next)
					{
						const int node = doc.select_node_next;
						const int link = doc.select_link_next;
						doc.select_node_next = 0;
						doc.select_link_next = 0;
						// Created this frame (menu) : ImNodes knows it next frame.
						if (node && NodeExists(doc, node) && !doc.drawn_nodes.count(node))
							doc.select_node_next = node;
						else if (!node && link && LinkExists(doc, link) && !doc.drawn_links.count(link))
							doc.select_link_next = link;
						else if (node && NodeExists(doc, node))
						{
							ImNodes::ClearNodeSelection();
							ImNodes::ClearLinkSelection();
							ImNodes::SelectNode(node);
							if (doc.sel_node != node)
								doc.sel_aux = doc.kind == Kind::Tree && tree::FindAux(doc.bt, doc.sel_aux) ? doc.sel_aux : 0;
							doc.sel_node = node;
							doc.sel_link = 0;
						}
						else if (link && LinkExists(doc, link))
						{
							ImNodes::ClearNodeSelection();
							ImNodes::ClearLinkSelection();
							ImNodes::SelectLink(link);
							doc.sel_link = link;
							doc.sel_node = 0;
							doc.sel_aux = 0;
						}
					}

					// Selection of ImNodes -> Details.
					const auto nodes = SelectedNodes();
					const auto links = SelectedLinks();
					if (nodes.size() == 1 && links.empty())
					{
						if (doc.sel_node != nodes.front())
							doc.sel_aux = 0;
						doc.sel_node = nodes.front();
						doc.sel_link = 0;
					}
					else if (links.size() == 1 && nodes.empty())
					{
						doc.sel_link = links.front();
						doc.sel_node = 0;
						doc.sel_aux = 0;
					}
					else if (nodes.empty() && links.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && canvas_hovered &&
					         !ImGui::IsAnyItemHovered())
					{
						ClearSelection(doc);
					}
				}
				ImGui::EndChild();

				ImGui::SameLine(0.f, 0.f);
				{
					float right = doc.right_width;
					const float before = right;
					Splitter("##split_r", right, -100000.f, 100000.f);
					doc.right_width = std::clamp(doc.right_width - (right - before), 200.f, total_w * 0.45f);
				}
				ImGui::SameLine(0.f, 0.f);

				// Right : Details
				ImGui::BeginChild("##details", ImVec2(0.f, body_h), ImGuiChildFlags_Borders);
				if (doc.kind == Kind::Anim)
					anim::DrawDetails(doc, anim_debug);
				else
					tree::DrawDetails(doc, tree_debug);
				ImGui::EndChild();

				if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
					Shortcuts(doc, canvas_hovered);

				std::vector<std::function<void()>> actions;
				actions.swap(doc.pending);
				for (auto& action : actions)
					action();

				// Edits of this frame : one undo step once nothing is held.
				if (doc.touched && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
					Commit(doc);
			}

			if (doc.ask_close)
			{
				ImGui::OpenPopup("Unsaved graph");
				doc.ask_close = false;
			}
			if (ImGui::BeginPopupModal("Unsaved graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("Save the changes of %s ?", doc.path.filename().string().c_str());
				if (ImGui::Button("Save"))
				{
					if (Save(doc))
						doc.open = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Don't save"))
				{
					doc.open = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
					ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}

			ImGui::End();
		}

		bool RelativeInside(const stdfs::path& path, const stdfs::path& root, stdfs::path& relative)
		{
			std::error_code ec;
			const stdfs::path p = stdfs::weakly_canonical(stdfs::absolute(path, ec), ec);
			const stdfs::path r = stdfs::weakly_canonical(stdfs::absolute(root, ec), ec);
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

		std::string Extension(const stdfs::path& path)
		{
			std::string e = path.extension().string();
			std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return e;
		}

		void EnsureContext()
		{
			if (g_nodes_context)
				return;
			ImNodesContext* previous = ImNodes::GetCurrentContext();
			g_nodes_context = ImNodes::CreateContext();
			ImNodes::SetCurrentContext(g_nodes_context);
			ImNodes::StylePixel();
			// A dark canvas (not the color of the windows) : the nodes stand out.
			{
				ImNodesStyle& s = ImNodes::GetStyle();
				s.Colors[ImNodesCol_GridBackground] = IM_COL32(40, 42, 50, 255);
				s.Colors[ImNodesCol_GridLine] = IM_COL32(52, 55, 65, 255);
				s.Colors[ImNodesCol_GridLinePrimary] = IM_COL32(66, 70, 84, 255);
				s.Colors[ImNodesCol_Link] = IM_COL32(168, 176, 194, 255);
				s.Colors[ImNodesCol_LinkHovered] = IM_COL32(236, 196, 104, 255);
				s.Colors[ImNodesCol_LinkSelected] = IM_COL32(255, 170, 60, 255);
				s.Colors[ImNodesCol_Pin] = IM_COL32(150, 158, 176, 255);
				s.Colors[ImNodesCol_PinHovered] = IM_COL32(255, 190, 90, 255);
				s.Colors[ImNodesCol_BoxSelector] = IM_COL32(255, 170, 60, 40);
				s.Colors[ImNodesCol_BoxSelectorOutline] = IM_COL32(255, 170, 60, 200);
				s.Colors[ImNodesCol_NodeOutline] = IM_COL32(20, 20, 26, 255);
				s.LinkThickness = 2.5f;
				s.PinCircleRadius = 4.5f;
				s.PinTriangleSideLength = 10.f;
			}
			ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;
			ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
			if (previous)
				ImNodes::SetCurrentContext(previous);
		}
	}


	// =========================================================================
	// API
	// =========================================================================

	bool CanOpen(const stdfs::path& path)
	{
		const std::string e = Extension(path);
		return e == ".animgraph" || e == ".bt";
	}

	void Open(const stdfs::path& path)
	{
		std::error_code ec;
		const stdfs::path absolute = stdfs::weakly_canonical(stdfs::absolute(path, ec), ec);

		for (auto& doc : g_docs)
		{
			if (doc->path == absolute || stdfs::equivalent(doc->path, absolute, ec))
			{
				doc->focus_next = true;
				return;
			}
		}

		EnsureContext();
		auto doc = std::make_unique<Doc>();
		doc->kind = Extension(absolute) == ".bt" ? Kind::Tree : Kind::Anim;
		doc->path = absolute;
		doc->nodes = ImNodes::EditorContextCreate();
		Load(*doc);
		doc->focus_next = true;
		g_docs.push_back(std::move(doc));
	}

	void DrawAll(ImGuiID dock_id)
	{
		if (g_docs.empty())
			return;

		EnsureContext();
		ImNodesContext* previous = ImNodes::GetCurrentContext();
		ImNodes::SetCurrentContext(g_nodes_context);

		for (auto& doc : g_docs)
			DrawDoc(*doc, dock_id);

		for (auto& doc : g_docs)
		{
			if (!doc->open && doc->nodes)
			{
				ImNodes::EditorContextFree(doc->nodes);
				doc->nodes = nullptr;
			}
		}
		g_docs.erase(std::remove_if(g_docs.begin(), g_docs.end(), [](const std::unique_ptr<Doc>& d) { return !d->open; }),
		             g_docs.end());

		if (previous && previous != g_nodes_context)
			ImNodes::SetCurrentContext(previous);
	}

	bool HasFocus()
	{
		if (g_docs.empty())
			return false;
		for (const ImGuiWindow* w = ImGui::GetCurrentContext()->NavWindow; w; w = w->ParentWindow)
			if (std::strstr(w->Name, "###graph:"))
				return true;
		return false;
	}

	void PathMoved(const stdfs::path& from, const stdfs::path& to)
	{
		std::error_code ec;
		const stdfs::path destination = stdfs::weakly_canonical(stdfs::absolute(to, ec), ec);
		for (auto& doc : g_docs)
		{
			stdfs::path relative;
			if (!RelativeInside(doc->path, from, relative))
				continue;
			doc->path = relative.empty() ? destination : destination / relative;
			doc->write_time = stdfs::last_write_time(doc->path, ec);
			doc->focus_next = true;
		}
	}

	void PathDeleted(const stdfs::path& target)
	{
		for (auto& doc : g_docs)
		{
			stdfs::path relative;
			if (RelativeInside(doc->path, target, relative))
				doc->open = false;
		}
	}

	bool HasUnsavedChanges()
	{
		return std::any_of(g_docs.begin(), g_docs.end(), [](const auto& d) { return d->dirty || d->touched; });
	}

	void SaveAll()
	{
		for (auto& doc : g_docs)
			if (doc->dirty || doc->touched)
				Save(*doc);
	}

	std::string NewAnimGraphTemplate()
	{
		AnimGraphAsset g;
		g.entry_x = 0.f;
		g.entry_y = 0.f;
		g.any_x = 0.f;
		g.any_y = 220.f;

		AnimGraphParameter speed;
		speed.name = "speed";
		g.parameters.push_back(speed);

		AnimGraphAnimation idle;
		idle.name = "Idle";
		g.animations.push_back(idle);

		AnimGraphState state;
		state.id = 1;
		state.name = "Idle";
		state.motion = "Idle";
		state.x = 260.f;
		state.y = 0.f;
		g.states.push_back(state);
		g.entry_state = "Idle";
		return g.SaveToString();
	}

	std::string NewBehaviorTreeTemplate()
	{
		return BehaviorTreeAsset::MakeDefault().SaveToString();
	}

	void SetAssetFieldDrawer(std::function<bool(std::string& value)> drawer)
	{
		g_asset_drawer = std::move(drawer);
	}

	void Shutdown()
	{
		if (!g_nodes_context)
		{
			g_docs.clear();
			return;
		}
		ImNodesContext* previous = ImNodes::GetCurrentContext();
		ImNodes::SetCurrentContext(g_nodes_context);
		for (auto& doc : g_docs)
			if (doc->nodes)
				ImNodes::EditorContextFree(doc->nodes);
		g_docs.clear();
		ImNodes::DestroyContext(g_nodes_context);
		if (previous && previous != g_nodes_context)
			ImNodes::SetCurrentContext(previous);
		else
			ImNodes::SetCurrentContext(nullptr);
		g_nodes_context = nullptr;
	}
}
