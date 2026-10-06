#include "DialogueData.h"

#include <json/json.hpp>

#include <algorithm>

namespace dialogue
{
	using Json = nlohmann::json;

	namespace
	{
		std::string Str(const Json& j, const char* key, const std::string& fallback = {})
		{
			const auto it = j.find(key);
			if (it == j.end())
				return fallback;
			if (it->is_string())
				return it->get<std::string>();
			if (it->is_null())
				return fallback;
			return it->dump();
		}

		float Num(const Json& j, const char* key)
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_number() ? it->get<float>() : 0.f;
		}
	}

	Node* Asset::Find(const std::string& id)
	{
		for (Node& n : nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	const Node* Asset::Find(const std::string& id) const
	{
		for (const Node& n : nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	std::string Asset::NewId() const
	{
		for (int i = 1;; ++i)
		{
			const std::string id = "n" + std::to_string(i);
			if (!Find(id))
				return id;
		}
	}

	const std::vector<std::string>& NodeTypes()
	{
		static const std::vector<std::string> types = { "line", "choice", "branch", "set", "event", "quest", "end" };
		return types;
	}

	bool Parse(const std::string& text, Asset& out, std::string& error)
	{
		out = Asset{};
		const Json j = Json::parse(text, nullptr, false, true);
		if (!j.is_object())
		{
			error = "not a JSON object";
			return false;
		}

		out.start = Str(j, "start");

		const auto nodes = j.find("nodes");
		if (nodes != j.end() && nodes->is_array())
		{
			for (const Json& jn : *nodes)
			{
				if (!jn.is_object())
					continue;

				Node n;
				n.id = Str(jn, "id");
				n.type = Str(jn, "type", "line");
				n.speaker = Str(jn, "speaker");
				n.text = Str(jn, "text");
				n.next = Str(jn, "next");
				n.else_next = Str(jn, "else");
				n.condition = Str(jn, "condition");
				n.variable = Str(jn, "variable");
				n.op = Str(jn, "op", "set");
				n.event = Str(jn, "event");
				n.script = Str(jn, "script");
				n.quest = Str(jn, "quest");
				n.action = Str(jn, "action", "start");
				n.objective = Str(jn, "objective");
				n.x = Num(jn, "x");
				n.y = Num(jn, "y");

				const auto value = jn.find("value");
				if (value != jn.end())
					n.value_json = value->dump();

				const auto choices = jn.find("choices");
				if (choices != jn.end() && choices->is_array())
					for (const Json& jc : *choices)
						if (jc.is_object())
							n.choices.push_back({ Str(jc, "text"), Str(jc, "next"), Str(jc, "condition") });

				if (n.id.empty())
					n.id = out.NewId();
				out.nodes.push_back(std::move(n));
			}
		}

		if (out.start.empty() && !out.nodes.empty())
			out.start = out.nodes.front().id;
		return true;
	}

	std::string Serialize(const Asset& asset)
	{
		Json j = Json::object();
		j["start"] = asset.start;
		Json nodes = Json::array();

		for (const Node& n : asset.nodes)
		{
			Json jn = Json::object();
			jn["id"] = n.id;
			jn["type"] = n.type;

			if (n.type == "line" || n.type == "choice")
			{
				if (!n.speaker.empty()) jn["speaker"] = n.speaker;
				if (!n.text.empty() || n.type == "line") jn["text"] = n.text;
			}
			if (n.type == "choice")
			{
				Json choices = Json::array();
				for (const Choice& c : n.choices)
				{
					Json jc = { { "text", c.text }, { "next", c.next } };
					if (!c.condition.empty())
						jc["condition"] = c.condition;
					choices.push_back(jc);
				}
				jn["choices"] = choices;
			}
			if (n.type == "branch")
			{
				jn["condition"] = n.condition;
				jn["else"] = n.else_next;
			}
			if (n.type == "set")
			{
				jn["variable"] = n.variable;
				jn["op"] = n.op;
				const Json value = Json::parse(n.value_json, nullptr, false);
				jn["value"] = value.is_discarded() ? Json(n.value_json) : value;
			}
			if (n.type == "event")
			{
				jn["event"] = n.event;
				if (!n.script.empty()) jn["script"] = n.script;
			}
			if (n.type == "quest")
			{
				jn["quest"] = n.quest;
				jn["action"] = n.action;
				if (!n.objective.empty()) jn["objective"] = n.objective;
			}
			if (n.type != "choice" && n.type != "end" && !n.next.empty())
				jn["next"] = n.next;

			jn["x"] = n.x;
			jn["y"] = n.y;
			nodes.push_back(jn);
		}

		j["nodes"] = nodes;
		return j.dump(2) + "\n";
	}
}
