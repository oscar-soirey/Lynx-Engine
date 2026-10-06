#include "AnimGraph.h"

#include "../core/Filesystem.h"

#include <xml/tinyxml2.h>

#include <algorithm>
#include <cstdio>

namespace lynx
{
	namespace
	{
		constexpr int kVersion = 1;

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
		entry_state = Str(root, "entry");

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
			else if (tag == "State")
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
				states.push_back(std::move(s));
			}
			else if (tag == "Transition")
			{
				AnimGraphTransition t;
				t.id = e->IntAttribute("id", 0);
				t.from = Str(e, "from");
				if (t.from == "*")
					t.from.clear();
				t.to = Str(e, "to");
				t.priority = e->IntAttribute("priority", 0);
				t.wait_finished = e->BoolAttribute("wait_finished", false);
				for (const XMLElement* c = e->FirstChildElement("Condition"); c; c = c->NextSiblingElement("Condition"))
				{
					AnimGraphCondition cond;
					cond.kind = Str(c, "kind", "compare");
					cond.param = Str(c, "param");
					cond.op = Str(c, "op", ">");
					cond.value = c->FloatAttribute("value", 0.f);
					cond.function = Str(c, "function");
					t.conditions.push_back(std::move(cond));
				}
				transitions.push_back(std::move(t));
			}
			else if (tag == "Entry")
			{
				entry_x = e->FloatAttribute("x", 0.f);
				entry_y = e->FloatAttribute("y", 0.f);
			}
			else if (tag == "AnyState")
			{
				any_x = e->FloatAttribute("x", 0.f);
				any_y = e->FloatAttribute("y", 200.f);
			}
		}

		// Fichiers anciens / ecrits a la main : ids manquants.
		for (auto& s : states)
			if (s.id <= 0)
				s.id = NewId();
		for (auto& t : transitions)
			if (t.id <= 0)
				t.id = NewId();

		return true;
	}

	std::string AnimGraphAsset::SaveToString() const
	{
		using namespace tinyxml2;

		XMLDocument doc;
		doc.InsertFirstChild(doc.NewDeclaration());
		XMLElement* root = doc.NewElement("AnimGraph");
		root->SetAttribute("version", kVersion);
		root->SetAttribute("entry", entry_state.c_str());
		doc.InsertEndChild(root);

		XMLElement* entry = doc.NewElement("Entry");
		entry->SetAttribute("x", Num(entry_x).c_str());
		entry->SetAttribute("y", Num(entry_y).c_str());
		root->InsertEndChild(entry);

		XMLElement* any = doc.NewElement("AnyState");
		any->SetAttribute("x", Num(any_x).c_str());
		any->SetAttribute("y", Num(any_y).c_str());
		root->InsertEndChild(any);

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

		for (const auto& s : states)
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
			root->InsertEndChild(e);
		}

		for (const auto& t : transitions)
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

	AnimGraphState* AnimGraphAsset::FindState(const std::string& name)
	{
		for (auto& s : states)
			if (s.name == name)
				return &s;
		return nullptr;
	}

	const AnimGraphParameter* AnimGraphAsset::FindParameter(const std::string& name) const
	{
		for (const auto& p : parameters)
			if (p.name == name)
				return &p;
		return nullptr;
	}

	bool AnimGraphAsset::IsMotion(const std::string& name) const
	{
		for (const auto& a : animations)
			if (a.name == name)
				return true;
		for (const auto& b : blend_spaces)
			if (b.name == name)
				return true;
		return false;
	}

	int AnimGraphAsset::NewId() const
	{
		int id = 1;
		for (const auto& s : states)
			id = std::max(id, s.id + 1);
		for (const auto& t : transitions)
			id = std::max(id, t.id + 1);
		return id;
	}

	void AnimGraphAsset::RenameState(const std::string& from, const std::string& to)
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
}
