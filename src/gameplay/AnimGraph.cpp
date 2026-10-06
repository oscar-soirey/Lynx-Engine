#include "AnimGraph.h"

#include "../core/Filesystem.h"

#include <xml/tinyxml2.h>

#include <algorithm>
#include <cstdio>

namespace lynx
{
	namespace
	{
		constexpr int kVersion = 2;

		std::string Str(const tinyxml2::XMLElement* e, const char* name, const char* fallback = "")
		{
			const char* v = e->Attribute(name);
			return v ? v : fallback;
		}

		std::string Num(float v)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(v));
			return buffer;
		}
	}

	namespace
	{
		AnimGraphState ReadState(const tinyxml2::XMLElement* e)
		{
			AnimGraphState s;
			s.id = e->IntAttribute("id", 0);
			s.name = Str(e, "name");
			s.motion = Str(e, "motion");
			s.interruptible = e->BoolAttribute("interruptible", true);
			s.restart_on_enter = e->BoolAttribute("restart_on_enter", true);
			s.next = Str(e, "next");
			s.on_enter = Str(e, "on_enter");
			s.on_exit = Str(e, "on_exit");
			s.x = e->FloatAttribute("x", 0.f);
			s.y = e->FloatAttribute("y", 0.f);
			s.pose_x = e->FloatAttribute("pose_x", 0.f);
			s.pose_y = e->FloatAttribute("pose_y", 0.f);
			s.output_x = e->FloatAttribute("output_x", 320.f);
			s.output_y = e->FloatAttribute("output_y", 0.f);
			return s;
		}

		AnimGraphTransition ReadTransition(const tinyxml2::XMLElement* e)
		{
			AnimGraphTransition t;
			t.id = e->IntAttribute("id", 0);
			t.from = Str(e, "from");
			if (t.from == "*")
				t.from.clear();
			t.to = Str(e, "to");
			t.priority = e->IntAttribute("priority", 0);
			t.wait_finished = e->BoolAttribute("wait_finished", false);
			for (const tinyxml2::XMLElement* c = e->FirstChildElement("Condition"); c; c = c->NextSiblingElement("Condition"))
			{
				AnimGraphCondition cond;
				cond.kind = Str(c, "kind", "compare");
				cond.param = Str(c, "param");
				cond.op = Str(c, "op", ">");
				cond.value = c->FloatAttribute("value", 0.f);
				cond.function = Str(c, "function");
				t.conditions.push_back(std::move(cond));
			}
			return t;
		}

		// <State> / <Transition> / <Entry> / <AnyState> d'une state machine (ou de la racine en v1).
		bool ReadMachineChild(const tinyxml2::XMLElement* e, AnimGraphStateMachine& m)
		{
			const std::string tag = e->Name();
			if (tag == "State")
				m.states.push_back(ReadState(e));
			else if (tag == "Transition")
				m.transitions.push_back(ReadTransition(e));
			else if (tag == "Entry")
			{
				m.entry_x = e->FloatAttribute("x", 0.f);
				m.entry_y = e->FloatAttribute("y", 0.f);
			}
			else if (tag == "AnyState")
			{
				m.any_x = e->FloatAttribute("x", 0.f);
				m.any_y = e->FloatAttribute("y", 220.f);
			}
			else
				return false;
			return true;
		}

		void WriteMachine(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* parent, const AnimGraphStateMachine& m)
		{
			using namespace tinyxml2;
			XMLElement* entry = doc.NewElement("Entry");
			entry->SetAttribute("x", Num(m.entry_x).c_str());
			entry->SetAttribute("y", Num(m.entry_y).c_str());
			parent->InsertEndChild(entry);

			XMLElement* any = doc.NewElement("AnyState");
			any->SetAttribute("x", Num(m.any_x).c_str());
			any->SetAttribute("y", Num(m.any_y).c_str());
			parent->InsertEndChild(any);

			for (const auto& s : m.states)
			{
				XMLElement* e = doc.NewElement("State");
				e->SetAttribute("id", s.id);
				e->SetAttribute("name", s.name.c_str());
				e->SetAttribute("motion", s.motion.c_str());
				e->SetAttribute("interruptible", s.interruptible);
				e->SetAttribute("restart_on_enter", s.restart_on_enter);
				if (!s.next.empty()) e->SetAttribute("next", s.next.c_str());
				if (!s.on_enter.empty()) e->SetAttribute("on_enter", s.on_enter.c_str());
				if (!s.on_exit.empty()) e->SetAttribute("on_exit", s.on_exit.c_str());
				e->SetAttribute("x", Num(s.x).c_str());
				e->SetAttribute("y", Num(s.y).c_str());
				e->SetAttribute("pose_x", Num(s.pose_x).c_str());
				e->SetAttribute("pose_y", Num(s.pose_y).c_str());
				e->SetAttribute("output_x", Num(s.output_x).c_str());
				e->SetAttribute("output_y", Num(s.output_y).c_str());
				parent->InsertEndChild(e);
			}

			for (const auto& t : m.transitions)
			{
				XMLElement* e = doc.NewElement("Transition");
				e->SetAttribute("id", t.id);
				e->SetAttribute("from", t.from.empty() ? "*" : t.from.c_str());
				e->SetAttribute("to", t.to.c_str());
				e->SetAttribute("priority", t.priority);
				e->SetAttribute("wait_finished", t.wait_finished);
				for (const auto& c : t.conditions)
				{
					XMLElement* ce = doc.NewElement("Condition");
					ce->SetAttribute("kind", c.kind.c_str());
					if (!c.param.empty()) ce->SetAttribute("param", c.param.c_str());
					if (c.kind == "compare")
					{
						ce->SetAttribute("op", c.op.c_str());
						ce->SetAttribute("value", Num(c.value).c_str());
					}
					if (!c.function.empty()) ce->SetAttribute("function", c.function.c_str());
					e->InsertEndChild(ce);
				}
				parent->InsertEndChild(e);
			}
		}
	}

	bool AnimGraphAsset::LoadFromString(const std::string& xml, std::string* error)
	{
		using namespace tinyxml2;

		XMLDocument doc;
		if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
		{
			if (error)
				*error = std::string("invalid XML : ") + doc.ErrorStr();
			return false;
		}

		const XMLElement* root = doc.FirstChildElement("AnimGraph");
		if (!root)
		{
			if (error)
				*error = "not an anim graph (no <AnimGraph>)";
			return false;
		}

		*this = AnimGraphAsset{};

		// Version 1 : une seule machine a etats a la racine.
		AnimGraphStateMachine legacy;
		legacy.name = "Locomotion";
		legacy.entry_state = Str(root, "entry");
		bool has_legacy = false;
		bool has_output = false;

		for (const XMLElement* e = root->FirstChildElement(); e; e = e->NextSiblingElement())
		{
			const std::string tag = e->Name();

			if (tag == "Parameter")
			{
				AnimGraphParameter p;
				p.name = Str(e, "name");
				p.type = Str(e, "type", "float");
				p.default_value = e->FloatAttribute("default", 0.f);
				parameters.push_back(std::move(p));
			}
			else if (tag == "Animation")
			{
				AnimGraphAnimation a;
				a.name = Str(e, "name");
				a.texture = Str(e, "texture");
				a.frames = std::max(1, e->IntAttribute("frames", 1));
				a.frame_time = e->FloatAttribute("frame_time", 0.1f);
				a.loop = e->BoolAttribute("loop", true);
				for (const XMLElement* n = e->FirstChildElement("Notify"); n; n = n->NextSiblingElement("Notify"))
					a.notifies.push_back({ n->IntAttribute("frame", 1), Str(n, "event") });
				animations.push_back(std::move(a));
			}
			else if (tag == "BlendSpace")
			{
				AnimGraphBlendSpace b;
				b.name = Str(e, "name");
				b.variable = Str(e, "variable");
				for (const XMLElement* s = e->FirstChildElement("Sample"); s; s = s->NextSiblingElement("Sample"))
					b.samples.push_back({ s->FloatAttribute("position", 0.f), Str(s, "animation") });
				blend_spaces.push_back(std::move(b));
			}
			else if (tag == "StateMachine")
			{
				AnimGraphStateMachine m;
				m.id = e->IntAttribute("id", 0);
				m.name = Str(e, "name");
				m.entry_state = Str(e, "entry");
				for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
					ReadMachineChild(c, m);
				state_machines.push_back(std::move(m));
			}
			else if (tag == "PoseNode")
			{
				AnimGraphPoseNode n;
				n.id = e->IntAttribute("id", 0);
				n.type = Str(e, "type", "animation");
				n.source = Str(e, "source");
				n.x = e->FloatAttribute("x", 0.f);
				n.y = e->FloatAttribute("y", 0.f);
				pose_nodes.push_back(std::move(n));
			}
			else if (tag == "Output")
			{
				has_output = true;
				output_source = e->IntAttribute("source", 0);
				output_x = e->FloatAttribute("x", 360.f);
				output_y = e->FloatAttribute("y", 0.f);
			}
			else if (ReadMachineChild(e, legacy))
				has_legacy = has_legacy || tag == "State" || tag == "Transition";
		}

		// Version 1 : [Locomotion] -> [Output Pose]
		if (has_legacy || (!has_output && !legacy.entry_state.empty()))
		{
			legacy.name = "Locomotion";
			for (int i = 2; FindStateMachine(legacy.name); ++i)
				legacy.name = "Locomotion" + std::to_string(i);
			state_machines.push_back(std::move(legacy));
			AnimGraphPoseNode n;
			n.type = "statemachine";
			n.source = state_machines.back().name;
			n.x = 0.f;
			n.y = 0.f;
			pose_nodes.push_back(n);
			if (!has_output)
			{
				output_x = 360.f;
				output_y = 0.f;
			}
		}

		// Ids manquants (fichiers anciens / ecrits a la main).
		for (auto& m : state_machines)
		{
			if (m.id <= 0)
				m.id = NewId();
			for (auto& st : m.states)
				if (st.id <= 0)
					st.id = NewId();
			for (auto& t : m.transitions)
				if (t.id <= 0)
					t.id = NewId();
		}
		for (auto& n : pose_nodes)
			if (n.id <= 0)
			{
				n.id = NewId();
				if (!has_output && n.type == "statemachine" && output_source == 0)
					output_source = n.id;   // la machine de la version 1 est branchee
			}

		return true;
	}

	std::string AnimGraphAsset::SaveToString() const
	{
		using namespace tinyxml2;

		XMLDocument doc;
		doc.InsertFirstChild(doc.NewDeclaration());
		XMLElement* root = doc.NewElement("AnimGraph");
		root->SetAttribute("version", kVersion);
		doc.InsertEndChild(root);

		XMLElement* output = doc.NewElement("Output");
		output->SetAttribute("source", output_source);
		output->SetAttribute("x", Num(output_x).c_str());
		output->SetAttribute("y", Num(output_y).c_str());
		root->InsertEndChild(output);

		for (const auto& p : parameters)
		{
			XMLElement* e = doc.NewElement("Parameter");
			e->SetAttribute("name", p.name.c_str());
			e->SetAttribute("type", p.type.c_str());
			e->SetAttribute("default", Num(p.default_value).c_str());
			root->InsertEndChild(e);
		}

		for (const auto& a : animations)
		{
			XMLElement* e = doc.NewElement("Animation");
			e->SetAttribute("name", a.name.c_str());
			e->SetAttribute("texture", a.texture.c_str());
			e->SetAttribute("frames", a.frames);
			e->SetAttribute("frame_time", Num(a.frame_time).c_str());
			e->SetAttribute("loop", a.loop);
			for (const auto& n : a.notifies)
			{
				XMLElement* ne = doc.NewElement("Notify");
				ne->SetAttribute("frame", n.frame);
				ne->SetAttribute("event", n.event.c_str());
				e->InsertEndChild(ne);
			}
			root->InsertEndChild(e);
		}

		for (const auto& b : blend_spaces)
		{
			XMLElement* e = doc.NewElement("BlendSpace");
			e->SetAttribute("name", b.name.c_str());
			e->SetAttribute("variable", b.variable.c_str());
			for (const auto& s : b.samples)
			{
				XMLElement* se = doc.NewElement("Sample");
				se->SetAttribute("position", Num(s.position).c_str());
				se->SetAttribute("animation", s.animation.c_str());
				e->InsertEndChild(se);
			}
			root->InsertEndChild(e);
		}

		for (const auto& n : pose_nodes)
		{
			XMLElement* e = doc.NewElement("PoseNode");
			e->SetAttribute("id", n.id);
			e->SetAttribute("type", n.type.c_str());
			e->SetAttribute("source", n.source.c_str());
			e->SetAttribute("x", Num(n.x).c_str());
			e->SetAttribute("y", Num(n.y).c_str());
			root->InsertEndChild(e);
		}

		for (const auto& m : state_machines)
		{
			XMLElement* e = doc.NewElement("StateMachine");
			e->SetAttribute("id", m.id);
			e->SetAttribute("name", m.name.c_str());
			e->SetAttribute("entry", m.entry_state.c_str());
			WriteMachine(doc, e, m);
			root->InsertEndChild(e);
		}

		XMLPrinter printer;
		doc.Print(&printer);
		return printer.CStr();
	}

	bool AnimGraphAsset::LoadFromAsset(const std::string& path, std::string* error)
	{
		const auto data = fs::ReadBinary(path);
		if (data.empty())
		{
			if (error)
				*error = "could not read assets/" + path;
			return false;
		}
		return LoadFromString(std::string(reinterpret_cast<const char*>(data.data()), data.size()), error);
	}

	AnimGraphCompiled AnimGraphAsset::Compile() const
	{
		AnimGraphCompiled out;
		const AnimGraphPoseNode* node = OutputNode();
		if (!node)
			return out;
		if (node->type == "statemachine")
		{
			if (const AnimGraphStateMachine* m = FindStateMachine(node->source))
			{
				out.states = m->states;
				out.transitions = m->transitions;
				out.entry_state = m->entry_state;
				out.state_machine = m->name;
			}
			return out;
		}
		// Une animation / un blend space seul : un etat qui le joue.
		if (!IsMotion(node->source))
			return out;
		AnimGraphState s;
		s.id = 1;
		s.name = node->source;
		s.motion = node->source;
		out.states.push_back(s);
		out.entry_state = s.name;
		return out;
	}

	AnimGraphState* AnimGraphStateMachine::FindState(const std::string& name)
	{
		for (auto& s : states)
			if (s.name == name)
				return &s;
		return nullptr;
	}

	const AnimGraphState* AnimGraphStateMachine::FindState(const std::string& name) const
	{
		for (const auto& s : states)
			if (s.name == name)
				return &s;
		return nullptr;
	}

	void AnimGraphStateMachine::RenameState(const std::string& from, const std::string& to)
	{
		if (from == to)
			return;
		for (auto& s : states)
		{
			if (s.name == from)
				s.name = to;
			if (s.next == from)
				s.next = to;
		}
		for (auto& t : transitions)
		{
			if (t.from == from)
				t.from = to;
			if (t.to == from)
				t.to = to;
		}
		if (entry_state == from)
			entry_state = to;
	}

	const AnimGraphParameter* AnimGraphAsset::FindParameter(const std::string& name) const
	{
		for (const auto& p : parameters)
			if (p.name == name)
				return &p;
		return nullptr;
	}

	bool AnimGraphAsset::IsAnimation(const std::string& name) const
	{
		for (const auto& a : animations)
			if (a.name == name)
				return true;
		return false;
	}

	bool AnimGraphAsset::IsBlendSpace(const std::string& name) const
	{
		for (const auto& b : blend_spaces)
			if (b.name == name)
				return true;
		return false;
	}

	bool AnimGraphAsset::IsMotion(const std::string& name) const
	{
		return IsAnimation(name) || IsBlendSpace(name);
	}

	AnimGraphStateMachine* AnimGraphAsset::FindStateMachine(const std::string& name)
	{
		for (auto& m : state_machines)
			if (m.name == name)
				return &m;
		return nullptr;
	}

	const AnimGraphStateMachine* AnimGraphAsset::FindStateMachine(const std::string& name) const
	{
		for (const auto& m : state_machines)
			if (m.name == name)
				return &m;
		return nullptr;
	}

	AnimGraphStateMachine* AnimGraphAsset::FindStateMachine(int id)
	{
		for (auto& m : state_machines)
			if (m.id == id)
				return &m;
		return nullptr;
	}

	AnimGraphPoseNode* AnimGraphAsset::FindPoseNode(int id)
	{
		for (auto& n : pose_nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	const AnimGraphPoseNode* AnimGraphAsset::FindPoseNode(int id) const
	{
		for (const auto& n : pose_nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	const AnimGraphPoseNode* AnimGraphAsset::OutputNode() const
	{
		return output_source > 0 ? FindPoseNode(output_source) : nullptr;
	}

	int AnimGraphAsset::NewId() const
	{
		int id = 1;
		for (const auto& m : state_machines)
		{
			id = std::max(id, m.id + 1);
			for (const auto& s : m.states)
				id = std::max(id, s.id + 1);
			for (const auto& t : m.transitions)
				id = std::max(id, t.id + 1);
		}
		for (const auto& n : pose_nodes)
			id = std::max(id, n.id + 1);
		return id;
	}

	void AnimGraphAsset::RenameMotion(const std::string& from, const std::string& to)
	{
		if (from == to)
			return;
		for (auto& a : animations)
			if (a.name == from)
				a.name = to;
		for (auto& b : blend_spaces)
		{
			if (b.name == from)
				b.name = to;
			for (auto& s : b.samples)
				if (s.animation == from)
					s.animation = to;
		}
		for (auto& m : state_machines)
			for (auto& s : m.states)
				if (s.motion == from)
					s.motion = to;
		for (auto& n : pose_nodes)
			if (n.type != "statemachine" && n.source == from)
				n.source = to;
	}

	void AnimGraphAsset::RenameStateMachine(const std::string& from, const std::string& to)
	{
		if (from == to)
			return;
		for (auto& m : state_machines)
			if (m.name == from)
				m.name = to;
		for (auto& n : pose_nodes)
			if (n.type == "statemachine" && n.source == from)
				n.source = to;
	}
}
