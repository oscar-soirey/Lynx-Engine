// =============================================================================
// Plugin Dialogue : editor module
// -----------------------------------------------------------------------------
//   - .dialogue files : node graph editor (double-click in the Content
//     Browser, New file > Dialogue)
//   - window "Story" : quests of the game (assets/story/quests.json) and,
//     while playing, the live variables / quests
// =============================================================================

#include <editor/EditorPluginAPI.h>
#include <Lynx.h>

#include <imnodes.h>
#include <json/json.hpp>

#include "DialogueData.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <sstream>

using Json = nlohmann::json;
namespace sfs = std::filesystem;

namespace
{
	lynx::editor_api::EditorAPI* g_api = nullptr;
	ImNodesContext* g_nodes = nullptr;

	// ---- std::string in ImGui ------------------------------------------------

	int ResizeCallback(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			auto* str = static_cast<std::string*>(data->UserData);
			str->resize(static_cast<size_t>(data->BufTextLen));
			data->Buf = str->data();
		}
		return 0;
	}

	bool InputString(const char* label, std::string& value, bool multiline = false, float height = 0.f,
	                 ImGuiInputTextFlags extra = 0)
	{
		const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | extra;
		if (multiline)
			return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1,
			                                 ImVec2(-FLT_MIN, height), flags, ResizeCallback, &value);
		return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ResizeCallback, &value);
	}

	std::string ReadFile(const sfs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::stringstream s;
		s << in.rdbuf();
		return s.str();
	}

	std::string Utf8(const sfs::path& p)
	{
		const auto u8 = p.u8string();
		return std::string(u8.begin(), u8.end());
	}

	std::string Shorten(const std::string& text, size_t max)
	{
		std::string one_line = text;
		std::replace(one_line.begin(), one_line.end(), '\n', ' ');
		return one_line.size() > max ? one_line.substr(0, max - 3) + "..." : one_line;
	}

	ImU32 TypeColor(const std::string& type)
	{
		if (type == "line") return IM_COL32(60, 110, 170, 255);
		if (type == "choice") return IM_COL32(150, 95, 40, 255);
		if (type == "branch") return IM_COL32(120, 70, 150, 255);
		if (type == "set") return IM_COL32(60, 130, 90, 255);
		if (type == "event") return IM_COL32(160, 60, 70, 255);
		if (type == "quest") return IM_COL32(150, 130, 40, 255);
		return IM_COL32(80, 80, 85, 255);
	}


	// =========================================================================
	// Dialogue graph editor
	// =========================================================================

	struct Document
	{
		sfs::path path;
		std::string title;            // ImGui window name
		dialogue::Asset asset;
		std::string error;
		ImNodesEditorContext* nodes = nullptr;
		std::map<std::string, int> handles;   // node id -> imnodes id (stable)
		int next_handle = 1;
		bool dirty = false;
		bool place_nodes = true;              // positions -> imnodes (load, undo)
		std::string selected;                 // node id
		std::string pending_new;              // node placed at the mouse next frame
		ImVec2 pending_pos{ 0.f, 0.f };

		int Handle(const std::string& id)
		{
			auto it = handles.find(id);
			if (it != handles.end())
				return it->second;
			handles[id] = next_handle;
			return next_handle++;
		}

		dialogue::Node* FromHandle(int handle)
		{
			for (auto& [id, h] : handles)
				if (h == handle)
					return asset.Find(id);
			return nullptr;
		}
	};

	std::list<Document> g_docs;

	constexpr int kPins = 64;   // attributes per node : input, then outputs
	int InputPin(int handle) { return handle * kPins; }
	int OutputPin(int handle, int slot) { return handle * kPins + 1 + slot; }

	// Output slots of a node : next / (true, else) / one per choice.
	int OutputCount(const dialogue::Node& n)
	{
		if (n.type == "end") return 0;
		if (n.type == "branch") return 2;
		if (n.type == "choice") return static_cast<int>(n.choices.size());
		return 1;
	}

	std::string* OutputTarget(dialogue::Node& n, int slot)
	{
		if (n.type == "choice")
			return slot >= 0 && slot < static_cast<int>(n.choices.size()) ? &n.choices[slot].next : nullptr;
		if (n.type == "branch")
			return slot == 0 ? &n.next : slot == 1 ? &n.else_next : nullptr;
		if (n.type == "end")
			return nullptr;
		return slot == 0 ? &n.next : nullptr;
	}

	bool Save(Document& doc)
	{
		std::ofstream out(doc.path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			if (g_api) g_api->message(("Could not write " + Utf8(doc.path)).c_str());
			return false;
		}
		out << dialogue::Serialize(doc.asset);
		doc.dirty = false;
		return true;
	}

	void DeleteNode(Document& doc, const std::string& id)
	{
		auto& nodes = doc.asset.nodes;
		nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const dialogue::Node& n) { return n.id == id; }), nodes.end());
		for (dialogue::Node& n : nodes)
		{
			if (n.next == id) n.next.clear();
			if (n.else_next == id) n.else_next.clear();
			for (auto& c : n.choices)
				if (c.next == id) c.next.clear();
		}
		if (doc.asset.start == id)
			doc.asset.start = nodes.empty() ? std::string() : nodes.front().id;
		if (doc.selected == id)
			doc.selected.clear();
		doc.dirty = true;
	}

	dialogue::Node& AddNode(Document& doc, const std::string& type)
	{
		dialogue::Node n;
		n.id = doc.asset.NewId();
		n.type = type;
		if (type == "line") n.text = "...";
		if (type == "choice") n.choices = { { "Yes", "", "" }, { "No", "", "" } };
		if (type == "set") { n.variable = "flag"; n.value_json = "true"; }
		if (type == "event") n.event = "MyEvent";
		if (type == "quest") n.quest = "my_quest";
		doc.asset.nodes.push_back(n);
		if (doc.asset.start.empty())
			doc.asset.start = n.id;
		doc.dirty = true;
		return doc.asset.nodes.back();
	}

	void DrawProperties(Document& doc)
	{
		dialogue::Node* n = doc.selected.empty() ? nullptr : doc.asset.Find(doc.selected);
		if (!n)
		{
			ImGui::TextDisabled("Select a node.");
			ImGui::Spacing();
			ImGui::TextWrapped("Right click in the graph : add a node. Drag from a pin to another node to link "
			                   "them. Del : delete the selection. {name} in a text : a Story variable. "
			                   "Conditions are JavaScript (Story.get('coins') > 2).");
			return;
		}

		bool changed = false;
		ImGui::Text("%s  (%s)", n->id.c_str(), n->type.c_str());
		if (doc.asset.start == n->id)
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.f), "Start node");
		else if (ImGui::SmallButton("Set as start"))
		{
			doc.asset.start = n->id;
			changed = true;
		}
		ImGui::Separator();
		ImGui::PushItemWidth(-FLT_MIN);

		if (n->type == "line" || n->type == "choice")
		{
			ImGui::TextUnformatted("Speaker (empty : the NPC)");
			changed |= InputString("##speaker", n->speaker);
			ImGui::TextUnformatted("Text");
			changed |= InputString("##text", n->text, true, 110.f);
		}

		if (n->type == "choice")
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Choices (condition : JS, empty = always)");
			for (size_t i = 0; i < n->choices.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				ImGui::Text("%d.", static_cast<int>(i) + 1);
				ImGui::SameLine();
				if (ImGui::SmallButton("x"))
				{
					n->choices.erase(n->choices.begin() + static_cast<long>(i));
					changed = true;
					ImGui::PopID();
					break;
				}
				changed |= InputString("##ctext", n->choices[i].text);
				ImGui::TextDisabled("if");
				ImGui::SameLine();
				changed |= InputString("##ccond", n->choices[i].condition);
				ImGui::PopID();
			}
			if (ImGui::Button("+ Choice") && n->choices.size() < kPins - 2)
			{
				n->choices.push_back({ "...", "", "" });
				changed = true;
			}
		}

		if (n->type == "branch")
		{
			ImGui::TextUnformatted("Condition (JS) : top pin = true, bottom = false");
			changed |= InputString("##cond", n->condition, true, 60.f);
		}

		if (n->type == "set")
		{
			ImGui::TextUnformatted("Variable");
			changed |= InputString("##var", n->variable);
			const char* ops[] = { "set", "add", "toggle" };
			int op = n->op == "add" ? 1 : n->op == "toggle" ? 2 : 0;
			if (ImGui::Combo("##op", &op, ops, 3))
			{
				n->op = ops[op];
				changed = true;
			}
			if (n->op != "toggle")
			{
				ImGui::TextUnformatted("Value (JSON : 3, true, \"text\")");
				changed |= InputString("##value", n->value_json);
			}
		}

		if (n->type == "event")
		{
			ImGui::TextUnformatted("Event (DialogueEvents.OnDialogueEvent)");
			changed |= InputString("##event", n->event);
			ImGui::TextUnformatted("JavaScript (optional)");
			changed |= InputString("##script", n->script, true, 90.f);
		}

		if (n->type == "quest")
		{
			ImGui::TextUnformatted("Quest id");
			changed |= InputString("##quest", n->quest);
			const char* actions[] = { "start", "complete", "fail", "objective" };
			int action = 0;
			for (int i = 0; i < 4; ++i)
				if (n->action == actions[i])
					action = i;
			if (ImGui::Combo("##action", &action, actions, 4))
			{
				n->action = actions[action];
				changed = true;
			}
			if (n->action == "objective")
			{
				ImGui::TextUnformatted("Objective id");
				changed |= InputString("##objective", n->objective);
			}
		}

		ImGui::PopItemWidth();
		ImGui::Separator();
		if (ImGui::Button("Delete node"))
		{
			DeleteNode(doc, n->id);
			return;
		}

		if (changed)
			doc.dirty = true;
	}

	void DrawGraph(Document& doc)
	{
		ImNodes::EditorContextSet(doc.nodes);
		ImNodes::BeginNodeEditor();

		for (dialogue::Node& n : doc.asset.nodes)
		{
			const int h = doc.Handle(n.id);
			const bool start = doc.asset.start == n.id;

			ImNodes::PushColorStyle(ImNodesCol_TitleBar, TypeColor(n.type));
			ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, TypeColor(n.type));
			ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, TypeColor(n.type));
			ImNodes::BeginNode(h);

			ImNodes::BeginNodeTitleBar();
			ImGui::Text("%s%s  %s", start ? "> " : "", n.type.c_str(), n.id.c_str());
			ImNodes::EndNodeTitleBar();

			ImNodes::BeginInputAttribute(InputPin(h));
			if (n.type == "line" || n.type == "choice")
			{
				if (!n.speaker.empty())
					ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "%s", n.speaker.c_str());
				ImGui::TextUnformatted(Shorten(n.text, 36).c_str());
			}
			else if (n.type == "branch")
				ImGui::Text("if %s", Shorten(n.condition, 30).c_str());
			else if (n.type == "set")
				ImGui::Text("%s %s %s", n.variable.c_str(), n.op.c_str(), n.op == "toggle" ? "" : Shorten(n.value_json, 16).c_str());
			else if (n.type == "event")
				ImGui::Text("%s", n.event.c_str());
			else if (n.type == "quest")
				ImGui::Text("%s %s", n.action.c_str(), n.quest.c_str());
			else
				ImGui::TextUnformatted("end");
			ImNodes::EndInputAttribute();

			const int outputs = OutputCount(n);
			for (int slot = 0; slot < outputs; ++slot)
			{
				ImNodes::BeginOutputAttribute(OutputPin(h, slot));
				if (n.type == "choice")
					ImGui::Text("%d. %s", slot + 1, Shorten(n.choices[slot].text, 24).c_str());
				else if (n.type == "branch")
					ImGui::TextUnformatted(slot == 0 ? "true" : "false");
				else
					ImGui::TextUnformatted("next");
				ImNodes::EndOutputAttribute();
			}

			ImNodes::EndNode();
			ImNodes::PopColorStyle();
			ImNodes::PopColorStyle();
			ImNodes::PopColorStyle();

			if (doc.place_nodes)
				ImNodes::SetNodeGridSpacePos(h, ImVec2(n.x, n.y));
			if (doc.pending_new == n.id)
			{
				ImNodes::SetNodeScreenSpacePos(h, doc.pending_pos);
				doc.pending_new.clear();
			}
		}
		doc.place_nodes = false;

		// Links
		for (dialogue::Node& n : doc.asset.nodes)
		{
			const int h = doc.Handle(n.id);
			for (int slot = 0; slot < OutputCount(n); ++slot)
			{
				const std::string* target = OutputTarget(n, slot);
				if (target && !target->empty() && doc.asset.Find(*target))
					ImNodes::Link(OutputPin(h, slot), OutputPin(h, slot), InputPin(doc.Handle(*target)));
			}
		}

		ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
		ImNodes::EndNodeEditor();

		// Positions back into the asset.
		for (dialogue::Node& n : doc.asset.nodes)
		{
			const ImVec2 p = ImNodes::GetNodeGridSpacePos(doc.Handle(n.id));
			if (p.x != n.x || p.y != n.y)
			{
				n.x = p.x;
				n.y = p.y;
				doc.dirty = true;
			}
		}

		// New link (output -> input ; the other way works too).
		int a = 0, b = 0;
		if (ImNodes::IsLinkCreated(&a, &b))
		{
			if (a % kPins == 0)
				std::swap(a, b);
			dialogue::Node* from = doc.FromHandle(a / kPins);
			dialogue::Node* to = doc.FromHandle(b / kPins);
			if (from && to && b % kPins == 0)
				if (std::string* target = OutputTarget(*from, a % kPins - 1))
				{
					*target = to->id;
					doc.dirty = true;
				}
		}

		int destroyed = 0;
		if (ImNodes::IsLinkDestroyed(&destroyed))
			if (dialogue::Node* from = doc.FromHandle(destroyed / kPins))
				if (std::string* target = OutputTarget(*from, destroyed % kPins - 1))
				{
					target->clear();
					doc.dirty = true;
				}

		// Selection -> properties
		if (ImNodes::NumSelectedNodes() == 1)
		{
			int selected = 0;
			ImNodes::GetSelectedNodes(&selected);
			if (dialogue::Node* n = doc.FromHandle(selected))
				doc.selected = n->id;
		}

		// Delete
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
		    !ImGui::GetIO().WantTextInput)
		{
			const int links = ImNodes::NumSelectedLinks();
			if (links > 0)
			{
				std::vector<int> ids(static_cast<size_t>(links));
				ImNodes::GetSelectedLinks(ids.data());
				for (int id : ids)
					if (dialogue::Node* from = doc.FromHandle(id / kPins))
						if (std::string* target = OutputTarget(*from, id % kPins - 1))
							target->clear();
				ImNodes::ClearLinkSelection();
				doc.dirty = true;
			}
			const int count = ImNodes::NumSelectedNodes();
			if (count > 0)
			{
				std::vector<int> ids(static_cast<size_t>(count));
				ImNodes::GetSelectedNodes(ids.data());
				std::vector<std::string> names;
				for (int id : ids)
					if (dialogue::Node* n = doc.FromHandle(id))
						names.push_back(n->id);
				ImNodes::ClearNodeSelection();
				for (const std::string& id : names)
					DeleteNode(doc, id);
			}
		}

		// Right click : add a node
		if (ImNodes::IsEditorHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			doc.pending_pos = ImGui::GetMousePos();
			ImGui::OpenPopup("##addnode");
		}
		if (ImGui::BeginPopup("##addnode"))
		{
			ImGui::TextDisabled("Add a node");
			for (const std::string& type : dialogue::NodeTypes())
				if (ImGui::MenuItem(type.c_str()))
				{
					dialogue::Node& n = AddNode(doc, type);
					doc.pending_new = n.id;
					doc.selected = n.id;
				}
			ImGui::EndPopup();
		}
	}

	void DrawDocument(bool* open, void* user)
	{
		Document& doc = *static_cast<Document*>(user);
		ImGui::SetNextWindowSize(ImVec2(1100.f, 650.f), ImGuiCond_FirstUseEver);

		const std::string window = doc.title;
		if (!ImGui::Begin(window.c_str(), open, doc.dirty ? ImGuiWindowFlags_UnsavedDocument : 0))
		{
			ImGui::End();
			return;
		}

		if (!doc.error.empty())
		{
			ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "%s", doc.error.c_str());
			ImGui::End();
			return;
		}

		if (ImGui::Button("Save") ||
		    (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
		     ImGui::IsKeyPressed(ImGuiKey_S)))
			Save(doc);
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
		{
			std::string error;
			dialogue::Parse(ReadFile(doc.path), doc.asset, error);
			doc.place_nodes = true;
			doc.dirty = false;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", Utf8(doc.path.filename()).c_str());

		const float side = 340.f;
		ImGui::BeginChild("##graph", ImVec2(ImGui::GetContentRegionAvail().x - side, 0.f), true);
		DrawGraph(doc);
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##props", ImVec2(0.f, 0.f), true);
		DrawProperties(doc);
		ImGui::EndChild();

		ImGui::End();
	}

	bool OpenDialogue(const char* path_utf8, void*)
	{
		const sfs::path path = sfs::path(reinterpret_cast<const char8_t*>(path_utf8));

		for (Document& doc : g_docs)
			if (doc.path == path)
			{
				g_api->open_window(doc.title.c_str());
				return true;
			}

		g_docs.emplace_back();
		Document& doc = g_docs.back();
		doc.path = path;
		doc.title = "Dialogue - " + Utf8(path.filename()) + "##" + path_utf8;
		doc.nodes = ImNodes::EditorContextCreate();

		const std::string text = ReadFile(path);
		std::string error;
		if (!text.empty() && !dialogue::Parse(text, doc.asset, error))
			doc.error = "Not a dialogue file : " + error;

		g_api->add_window("Dialogue", doc.title.c_str(), DrawDocument, &doc, true);
		g_api->open_window(doc.title.c_str());
		return true;
	}


	// =========================================================================
	// Story window
	// =========================================================================

	struct QuestDecl
	{
		std::string id, title, description;
		std::vector<std::pair<std::string, std::string>> objectives;
	};

	std::vector<QuestDecl> g_quests;
	bool g_quests_loaded = false;
	bool g_quests_dirty = false;

	sfs::path QuestsFile()
	{
		return sfs::path(reinterpret_cast<const char8_t*>(g_api->assets_root())) / "story" / "quests.json";
	}

	void LoadQuests()
	{
		g_quests.clear();
		g_quests_loaded = true;
		g_quests_dirty = false;
		const Json j = Json::parse(ReadFile(QuestsFile()), nullptr, false, true);
		if (!j.is_array())
			return;
		for (const Json& q : j)
		{
			if (!q.is_object())
				continue;
			QuestDecl d;
			d.id = q.value("id", std::string());
			d.title = q.value("title", std::string());
			d.description = q.value("description", std::string());
			if (q.contains("objectives") && q["objectives"].is_array())
				for (const Json& o : q["objectives"])
					d.objectives.emplace_back(o.value("id", std::string()), o.value("text", std::string()));
			g_quests.push_back(d);
		}
	}

	void SaveQuests()
	{
		Json j = Json::array();
		for (const QuestDecl& d : g_quests)
		{
			Json objectives = Json::array();
			for (const auto& [id, text] : d.objectives)
				objectives.push_back({ { "id", id }, { "text", text } });
			j.push_back({ { "id", d.id }, { "title", d.title }, { "description", d.description }, { "objectives", objectives } });
		}
		std::error_code ec;
		sfs::create_directories(QuestsFile().parent_path(), ec);
		std::ofstream out(QuestsFile(), std::ios::binary | std::ios::trunc);
		out << j.dump(2) << "\n";
		g_quests_dirty = false;
	}

	void DrawLiveState()
	{
		std::string result, error;
		if (!lynx::EvaluateScript("Story._all()", result, error, "<Story window>"))
		{
			ImGui::TextDisabled("Story not available : %s", error.c_str());
			return;
		}
		const Json outer = Json::parse(result, nullptr, false);
		const Json state = outer.is_string() ? Json::parse(outer.get<std::string>(), nullptr, false) : Json();
		if (!state.is_object())
			return;

		ImGui::TextUnformatted("Variables (live)");
		if (ImGui::BeginTable("##vars", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
		{
			for (auto it = state["variables"].begin(); it != state["variables"].end(); ++it)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(it.key().c_str());
				ImGui::TableNextColumn();
				std::string value = it.value().dump();
				ImGui::PushID(it.key().c_str());
				ImGui::SetNextItemWidth(-FLT_MIN);
				// Enter : the new value (JSON) goes to the game.
				if (InputString("##v", value, false, 0.f, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					const Json parsed = Json::parse(value, nullptr, false);
					if (!parsed.is_discarded())
					{
						const std::string code = "Story.set(" + Json(it.key()).dump() + ", " + parsed.dump() + ")";
						lynx::ExecuteScript(code.c_str(), "<Story window>");
					}
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Quests (live)");
		for (const Json& q : state["quests"])
		{
			const std::string st = q.value("state", std::string());
			const ImVec4 color = st == "active" ? ImVec4(1.f, 0.85f, 0.3f, 1.f)
			                   : st == "completed" ? ImVec4(0.4f, 0.9f, 0.4f, 1.f)
			                   : st == "failed" ? ImVec4(1.f, 0.4f, 0.35f, 1.f)
			                   : ImVec4(0.6f, 0.6f, 0.6f, 1.f);
			ImGui::TextColored(color, "[%s] %s", st.c_str(), q.value("title", q.value("id", std::string())).c_str());
			for (const Json& o : q["objectives"])
				ImGui::BulletText("%s %s", o.value("done", false) ? "(done)" : "", o.value("text", std::string()).c_str());
		}
	}

	void DrawQuestDeclarations()
	{
		if (!g_quests_loaded)
			LoadQuests();

		if (ImGui::Button("Save"))
			SaveQuests();
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
			LoadQuests();
		ImGui::SameLine();
		if (ImGui::Button("+ Quest"))
		{
			g_quests.push_back({ "quest_" + std::to_string(g_quests.size() + 1), "New quest", "", {} });
			g_quests_dirty = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled(g_quests_dirty ? "assets/story/quests.json (not saved)" : "assets/story/quests.json");
		ImGui::Separator();

		for (size_t i = 0; i < g_quests.size(); ++i)
		{
			QuestDecl& q = g_quests[i];
			ImGui::PushID(static_cast<int>(i));
			const std::string header = (q.title.empty() ? q.id : q.title) + "###quest";
			if (ImGui::CollapsingHeader(header.c_str()))
			{
				ImGui::PushItemWidth(-FLT_MIN);
				ImGui::TextUnformatted("Id");
				g_quests_dirty |= InputString("##id", q.id);
				ImGui::TextUnformatted("Title");
				g_quests_dirty |= InputString("##title", q.title);
				ImGui::TextUnformatted("Description");
				g_quests_dirty |= InputString("##desc", q.description, true, 60.f);
				ImGui::TextUnformatted("Objectives (id, text)");
				for (size_t o = 0; o < q.objectives.size(); ++o)
				{
					ImGui::PushID(static_cast<int>(o));
					ImGui::SetNextItemWidth(120.f);
					g_quests_dirty |= InputString("##oid", q.objectives[o].first);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.f);
					g_quests_dirty |= InputString("##otext", q.objectives[o].second);
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
					{
						q.objectives.erase(q.objectives.begin() + static_cast<long>(o));
						g_quests_dirty = true;
						ImGui::PopID();
						break;
					}
					ImGui::PopID();
				}
				ImGui::PopItemWidth();
				if (ImGui::SmallButton("+ Objective"))
				{
					q.objectives.emplace_back("objective_" + std::to_string(q.objectives.size() + 1), "...");
					g_quests_dirty = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete quest"))
				{
					g_quests.erase(g_quests.begin() + static_cast<long>(i));
					g_quests_dirty = true;
					ImGui::PopID();
					break;
				}
			}
			ImGui::PopID();
		}
	}

	void DrawStory(bool* open, void*)
	{
		ImGui::SetNextWindowSize(ImVec2(520.f, 560.f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Story", open))
		{
			ImGui::End();
			return;
		}

		if (g_api->is_playing())
			DrawLiveState();
		else
			DrawQuestDeclarations();

		ImGui::End();
	}

	void OpenStory(void*)
	{
		g_api->open_window("Story");
	}
}


LYNX_EDITOR_PLUGIN_STARTUP(api)
{
	LYNX_EDITOR_PLUGIN_INIT(api);
	g_api = api;

	ImNodes::SetImGuiContext(api->imgui);
	g_nodes = ImNodes::CreateContext();
	ImNodes::StyleColorsDark();
	ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;

	api->add_file_editor("Dialogue", ".dialogue", OpenDialogue, nullptr);
	api->add_new_file("Dialogue", "Dialogue (.dialogue)", "NewDialogue", ".dialogue",
		"{\n"
		"  \"start\": \"n1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"n1\", \"type\": \"line\", \"speaker\": \"\", \"text\": \"Hello !\", \"next\": \"n2\", \"x\": 40, \"y\": 60 },\n"
		"    { \"id\": \"n2\", \"type\": \"end\", \"x\": 360, \"y\": 60 }\n"
		"  ]\n"
		"}\n");
	api->add_window("Dialogue", "Story", DrawStory, nullptr, false);
	api->add_menu_item("Dialogue", "Story (variables, quests)", OpenStory, nullptr);
}

LYNX_EDITOR_PLUGIN_SHUTDOWN()
{
	for (Document& doc : g_docs)
		if (doc.nodes)
			ImNodes::EditorContextFree(doc.nodes);
	g_docs.clear();
	if (g_nodes)
		ImNodes::DestroyContext(g_nodes);
	g_nodes = nullptr;
}
