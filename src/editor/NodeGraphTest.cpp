#include "NodeGraphTest.h"

#include <imgui/imgui.h>
#include <imgui_node/imnodes.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace lynx::editor::node_graph_test
{
	namespace
	{
		// ---------------------------------------------------------------------
		// Graph : a few kinds of nodes computing one number each
		// ---------------------------------------------------------------------

		enum class Kind
		{
			Number,
			Time,
			Sin,
			Add,
			Multiply,
			Output,
			Count
		};

		const char* KindName(Kind kind)
		{
			switch (kind)
			{
				case Kind::Number:   return "Number";
				case Kind::Time:     return "Time";
				case Kind::Sin:      return "Sin";
				case Kind::Add:      return "Add";
				case Kind::Multiply: return "Multiply";
				case Kind::Output:   return "Output";
				default:             return "?";
			}
		}

		int InputCount(Kind kind)
		{
			switch (kind)
			{
				case Kind::Sin:      return 1;
				case Kind::Add:      return 2;
				case Kind::Multiply: return 2;
				case Kind::Output:   return 1;
				default:             return 0;
			}
		}

		bool HasOutput(Kind kind)
		{
			return kind != Kind::Output;
		}

		// Attribute ids : node id * 8 + slot (0..6 inputs, 7 output).
		constexpr int kOutputSlot = 7;
		int InputAttribute(int node, int slot) { return node * 8 + slot; }
		int OutputAttribute(int node) { return node * 8 + kOutputSlot; }
		int NodeOfAttribute(int attribute) { return attribute / 8; }
		int SlotOfAttribute(int attribute) { return attribute % 8; }

		struct Node
		{
			int id = 0;
			Kind kind = Kind::Number;
			float value = 1.f;                  // Number : the value ; others : last result
			float inputs[2] = { 0.f, 1.f };     // used when an input has no link
			std::vector<float> history;         // Output : plot
		};

		struct LinkData
		{
			int id = 0;
			int from = 0;   // output attribute
			int to = 0;     // input attribute
		};

		struct Graph
		{
			std::vector<Node> nodes;
			std::vector<LinkData> links;
			int next_node = 1;
			int next_link = 1;
			std::vector<std::pair<int, ImVec2>> pending_positions;
		};

		ImNodesContext* g_context = nullptr;
		Graph g_graph;
		bool g_minimap = true;
		bool g_snap = false;
		bool g_initialized = false;
		float g_time = 0.f;
		ImVec2 g_popup_pos;
		bool g_focused = false;

		Node* FindNode(int id)
		{
			for (Node& node : g_graph.nodes)
				if (node.id == id)
					return &node;
			return nullptr;
		}

		int AddNode(Kind kind, const ImVec2& grid_pos)
		{
			Node node;
			node.id = g_graph.next_node++;
			node.kind = kind;
			if (kind == Kind::Add)
				node.inputs[1] = 0.f;
			g_graph.nodes.push_back(node);
			g_graph.pending_positions.emplace_back(node.id, grid_pos);
			return node.id;
		}

		void AddLink(int from, int to)
		{
			// One link per input : the new one replaces the old one.
			g_graph.links.erase(std::remove_if(g_graph.links.begin(), g_graph.links.end(),
			                                   [to](const LinkData& l) { return l.to == to; }),
			                    g_graph.links.end());
			g_graph.links.push_back({ g_graph.next_link++, from, to });
		}

		void RemoveNode(int id)
		{
			g_graph.links.erase(std::remove_if(g_graph.links.begin(), g_graph.links.end(),
			                                   [id](const LinkData& l)
			                                   { return NodeOfAttribute(l.from) == id || NodeOfAttribute(l.to) == id; }),
			                    g_graph.links.end());
			g_graph.nodes.erase(std::remove_if(g_graph.nodes.begin(), g_graph.nodes.end(),
			                                   [id](const Node& n) { return n.id == id; }),
			                    g_graph.nodes.end());
		}

		void ResetGraph()
		{
			g_graph = Graph();

			const int time = AddNode(Kind::Time, ImVec2(20.f, 40.f));
			const int sin = AddNode(Kind::Sin, ImVec2(200.f, 40.f));
			const int number = AddNode(Kind::Number, ImVec2(200.f, 180.f));
			const int multiply = AddNode(Kind::Multiply, ImVec2(380.f, 80.f));
			const int output = AddNode(Kind::Output, ImVec2(580.f, 60.f));

			if (Node* n = FindNode(number))
				n->value = 2.f;

			AddLink(OutputAttribute(time), InputAttribute(sin, 0));
			AddLink(OutputAttribute(sin), InputAttribute(multiply, 0));
			AddLink(OutputAttribute(number), InputAttribute(multiply, 1));
			AddLink(OutputAttribute(multiply), InputAttribute(output, 0));
		}

		// ---------------------------------------------------------------------
		// Evaluation (every frame, cycles give 0)
		// ---------------------------------------------------------------------

		float Evaluate(int node_id, std::unordered_map<int, int>& state, std::unordered_map<int, float>& cache);

		float InputValue(const Node& node, int slot, std::unordered_map<int, int>& state, std::unordered_map<int, float>& cache)
		{
			const int attribute = InputAttribute(node.id, slot);
			for (const LinkData& link : g_graph.links)
				if (link.to == attribute)
					return Evaluate(NodeOfAttribute(link.from), state, cache);
			return node.inputs[slot];
		}

		float Evaluate(int node_id, std::unordered_map<int, int>& state, std::unordered_map<int, float>& cache)
		{
			if (auto it = cache.find(node_id); it != cache.end())
				return it->second;
			if (state[node_id] == 1)
				return 0.f;   // cycle
			state[node_id] = 1;

			Node* node = FindNode(node_id);
			float result = 0.f;
			if (node)
			{
				switch (node->kind)
				{
					case Kind::Number:   result = node->value; break;
					case Kind::Time:     result = g_time; break;
					case Kind::Sin:      result = std::sin(InputValue(*node, 0, state, cache)); break;
					case Kind::Add:      result = InputValue(*node, 0, state, cache) + InputValue(*node, 1, state, cache); break;
					case Kind::Multiply: result = InputValue(*node, 0, state, cache) * InputValue(*node, 1, state, cache); break;
					case Kind::Output:   result = InputValue(*node, 0, state, cache); break;
					default: break;
				}
				if (node->kind != Kind::Number)
					node->value = result;
			}

			state[node_id] = 2;
			cache[node_id] = result;
			return result;
		}

		void EvaluateAll()
		{
			std::unordered_map<int, int> state;
			std::unordered_map<int, float> cache;
			for (Node& node : g_graph.nodes)
			{
				Evaluate(node.id, state, cache);
				if (node.kind == Kind::Output)
				{
					node.history.push_back(node.value);
					if (node.history.size() > 120)
						node.history.erase(node.history.begin());
				}
			}
		}

		// ---------------------------------------------------------------------
		// Drawing
		// ---------------------------------------------------------------------

		bool InputLinked(int attribute)
		{
			for (const LinkData& link : g_graph.links)
				if (link.to == attribute)
					return true;
			return false;
		}

		void DrawNode(Node& node)
		{
			const float width = ImGui::GetFontSize() * 6.f;

			ImNodes::BeginNode(node.id);

			ImNodes::BeginNodeTitleBar();
			ImGui::TextUnformatted(KindName(node.kind));
			ImNodes::EndNodeTitleBar();

			static const char* kInputNames[2] = { "a", "b" };
			const int inputs = InputCount(node.kind);
			for (int slot = 0; slot < inputs; ++slot)
			{
				const int attribute = InputAttribute(node.id, slot);
				ImNodes::BeginInputAttribute(attribute, ImNodesPinShape_CircleFilled);
				const char* name = inputs == 1 ? "in" : kInputNames[slot];
				if (InputLinked(attribute))
				{
					ImGui::TextUnformatted(name);
				}
				else
				{
					// No link : the value is edited in the node.
					ImGui::PushItemWidth(width);
					ImGui::DragFloat(name, &node.inputs[slot], 0.01f);
					ImGui::PopItemWidth();
				}
				ImNodes::EndInputAttribute();
			}

			if (node.kind == Kind::Number)
			{
				ImNodes::BeginStaticAttribute(node.id * 8 + 6);
				ImGui::PushItemWidth(width);
				ImGui::DragFloat("##value", &node.value, 0.01f);
				ImGui::PopItemWidth();
				ImNodes::EndStaticAttribute();
			}

			if (node.kind == Kind::Output)
			{
				ImNodes::BeginStaticAttribute(node.id * 8 + 6);
				ImGui::Text("%.3f", node.value);
				if (!node.history.empty())
				{
					ImGui::PlotLines("##plot", node.history.data(), static_cast<int>(node.history.size()), 0, nullptr,
					                 -2.5f, 2.5f, ImVec2(width * 1.4f, ImGui::GetFontSize() * 2.5f));
				}
				ImNodes::EndStaticAttribute();
			}

			if (HasOutput(node.kind))
			{
				ImNodes::BeginOutputAttribute(OutputAttribute(node.id), ImNodesPinShape_QuadFilled);
				char text[32];
				std::snprintf(text, sizeof(text), "%.2f", node.value);
				const float text_width = ImGui::CalcTextSize(text).x;
				ImGui::Indent(std::max(0.f, width - text_width));
				ImGui::TextDisabled("%s", text);
				ImGui::Unindent(std::max(0.f, width - text_width));
				ImNodes::EndOutputAttribute();
			}

			ImNodes::EndNode();
		}

		void DrawToolbar()
		{
			if (ImGui::Button("Reset graph"))
				ResetGraph();
			ImGui::SameLine();
			if (ImGui::Button("Theme"))
				ImNodes::StyleColorsPixel();   // after a change of the ImGui theme
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Take the colors of the current ImGui theme again.");
			ImGui::SameLine();
			ImGui::Checkbox("Mini-map", &g_minimap);
			ImGui::SameLine();
			if (ImGui::Checkbox("Snap to grid", &g_snap))
			{
				ImNodesStyle& style = ImNodes::GetStyle();
				if (g_snap)
					style.Flags |= ImNodesStyleFlags_GridSnapping;
				else
					style.Flags &= ~ImNodesStyleFlags_GridSnapping;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%d nodes, %d links", static_cast<int>(g_graph.nodes.size()),
			                    static_cast<int>(g_graph.links.size()));
			ImGui::TextDisabled("Right click : add a node. Drag a pin : link. Delete : remove. Middle mouse : pan.");
		}
	}


	void Draw(bool* open)
	{
		g_focused = false;
		if (open && !*open)
			return;

		if (!g_context)
		{
			g_context = ImNodes::CreateContext();
			ImNodes::SetCurrentContext(g_context);
			ImNodes::StylePixel();
			// Alt + left drag pans too (no middle button on a touchpad).
			ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;
			ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
		}
		ImNodes::SetCurrentContext(g_context);

		if (!g_initialized)
		{
			ResetGraph();
			g_initialized = true;
		}

		ImGui::SetNextWindowSize(ImVec2(900.f, 560.f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Node Graph (test)", open))
		{
			ImGui::End();
			return;
		}

		g_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		g_time += ImGui::GetIO().DeltaTime;
		EvaluateAll();

		DrawToolbar();

		ImNodes::BeginNodeEditor();

		// Right click on the canvas : add a node there.
		if (ImNodes::IsEditorHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
		    !ImGui::IsAnyItemHovered())
		{
			g_popup_pos = ImGui::GetMousePos();
			ImGui::OpenPopup("##AddNode");
		}
		if (ImGui::BeginPopup("##AddNode"))
		{
			ImGui::TextDisabled("Add node");
			ImGui::Separator();
			for (int k = 0; k < static_cast<int>(Kind::Count); ++k)
			{
				if (ImGui::MenuItem(KindName(static_cast<Kind>(k))))
				{
					// Placed where the right click was (not at a grid position).
					const int id = AddNode(static_cast<Kind>(k), ImVec2(0.f, 0.f));
					g_graph.pending_positions.pop_back();
					ImNodes::SetNodeScreenSpacePos(id, g_popup_pos);
				}
			}
			ImGui::EndPopup();
		}

		for (const auto& [id, pos] : g_graph.pending_positions)
			ImNodes::SetNodeGridSpacePos(id, pos);
		g_graph.pending_positions.clear();

		for (Node& node : g_graph.nodes)
			DrawNode(node);

		for (const LinkData& link : g_graph.links)
			ImNodes::Link(link.id, link.from, link.to);

		if (g_minimap)
			ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);

		ImNodes::EndNodeEditor();

		// --- Changes made by the user ------------------------------------------
		int start = 0, end = 0;
		if (ImNodes::IsLinkCreated(&start, &end))
		{
			// Always from an output to an input (the user may drag both ways).
			if (SlotOfAttribute(start) != kOutputSlot)
				std::swap(start, end);
			if (SlotOfAttribute(start) == kOutputSlot && SlotOfAttribute(end) != kOutputSlot &&
			    NodeOfAttribute(start) != NodeOfAttribute(end))
				AddLink(start, end);
		}

		int destroyed = 0;
		if (ImNodes::IsLinkDestroyed(&destroyed))
		{
			g_graph.links.erase(std::remove_if(g_graph.links.begin(), g_graph.links.end(),
			                                   [destroyed](const LinkData& l) { return l.id == destroyed; }),
			                    g_graph.links.end());
		}

		if (ImNodes::IsEditorHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput)
		{
			const int link_count = ImNodes::NumSelectedLinks();
			if (link_count > 0)
			{
				std::vector<int> ids(static_cast<size_t>(link_count));
				ImNodes::GetSelectedLinks(ids.data());
				for (int id : ids)
				{
					g_graph.links.erase(std::remove_if(g_graph.links.begin(), g_graph.links.end(),
					                                   [id](const LinkData& l) { return l.id == id; }),
					                    g_graph.links.end());
				}
				ImNodes::ClearLinkSelection();
			}

			const int node_count = ImNodes::NumSelectedNodes();
			if (node_count > 0)
			{
				std::vector<int> ids(static_cast<size_t>(node_count));
				ImNodes::GetSelectedNodes(ids.data());
				for (int id : ids)
					RemoveNode(id);
				ImNodes::ClearNodeSelection();
			}
		}

		ImGui::End();
	}


	bool HasFocus()
	{
		return g_focused;
	}


	void Shutdown()
	{
		if (g_context)
		{
			ImNodes::DestroyContext(g_context);
			g_context = nullptr;
		}
		g_initialized = false;
	}
}
