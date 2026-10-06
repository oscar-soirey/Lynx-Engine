#include "Story.h"

#include <Lynx.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace story
{
	using Json = nlohmann::json;

	namespace
	{
		Json g_vars = Json::object();
		std::vector<Quest> g_quests;

		void Notify(const char* function, lynx::InterfaceArgs args)
		{
			lynx::interfaces::Broadcast("StoryEvents", function, args);
		}

		Quest QuestFromJson(const Json& j)
		{
			Quest q;
			q.id = j.value("id", std::string());
			q.title = j.value("title", q.id);
			q.description = j.value("description", std::string());
			q.state = j.value("state", std::string("inactive"));
			if (j.contains("objectives") && j["objectives"].is_array())
				for (const Json& o : j["objectives"])
					if (o.is_object())
						q.objectives.push_back({ o.value("id", std::string()), o.value("text", std::string()),
						                         o.value("done", false) });
			return q;
		}

		Json QuestToJson(const Quest& q)
		{
			Json objectives = Json::array();
			for (const Objective& o : q.objectives)
				objectives.push_back({ { "id", o.id }, { "text", o.text }, { "done", o.done } });
			return { { "id", q.id }, { "title", q.title }, { "description", q.description },
			         { "state", q.state }, { "objectives", objectives } };
		}

		std::filesystem::path SaveFile(const std::string& slot)
		{
			return std::filesystem::path("saves") / (slot + ".story.json");
		}
	}

	std::vector<Quest> LoadQuestDeclarations()
	{
		std::vector<Quest> out;
		if (!lynx::fs::Exists("story/quests.json"))
			return out;

		const auto data = lynx::fs::ReadBinary("story/quests.json");
		const Json j = Json::parse(data.begin(), data.end(), nullptr, false, true);
		if (!j.is_array())
		{
			std::cout << "[Story] assets/story/quests.json : a JSON array is expected\n";
			return out;
		}
		for (const Json& q : j)
			if (q.is_object())
				out.push_back(QuestFromJson(q));
		return out;
	}

	void Reset()
	{
		g_vars = Json::object();
		g_quests = LoadQuestDeclarations();
	}

	Json& Variables()
	{
		return g_vars;
	}

	Json Get(const std::string& name)
	{
		const auto it = g_vars.find(name);
		return it != g_vars.end() ? *it : Json();
	}

	bool Has(const std::string& name)
	{
		return g_vars.contains(name);
	}

	void Set(const std::string& name, const Json& value)
	{
		if (name.empty())
			return;
		g_vars[name] = value;
		Notify("OnStoryVariableChanged", { name });
	}

	void Add(const std::string& name, double delta)
	{
		const Json current = Get(name);
		const double v = current.is_number() ? current.get<double>() : 0.0;
		const double result = v + delta;
		// Integers stay integers in the saves.
		if (result == static_cast<double>(static_cast<long long>(result)))
			Set(name, static_cast<long long>(result));
		else
			Set(name, result);
	}

	std::string Format(const std::string& text)
	{
		std::string out;
		out.reserve(text.size());
		for (size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '{')
			{
				const size_t end = text.find('}', i + 1);
				if (end != std::string::npos)
				{
					const std::string name = text.substr(i + 1, end - i - 1);
					if (Has(name))
					{
						const Json v = Get(name);
						out += v.is_string() ? v.get<std::string>() : v.dump();
						i = end;
						continue;
					}
				}
			}
			out += text[i];
		}
		return out;
	}

	std::vector<Quest>& Quests()
	{
		return g_quests;
	}

	Quest* FindQuest(const std::string& id)
	{
		for (Quest& q : g_quests)
			if (q.id == id)
				return &q;
		return nullptr;
	}

	Quest& GetOrCreateQuest(const std::string& id)
	{
		if (Quest* q = FindQuest(id))
			return *q;
		Quest q;
		q.id = id;
		q.title = id;
		g_quests.push_back(q);
		return g_quests.back();
	}

	void SetQuestState(const std::string& id, const std::string& state)
	{
		if (id.empty())
			return;
		Quest& q = GetOrCreateQuest(id);
		if (q.state == state)
			return;
		q.state = state;
		if (state == "completed")
			for (Objective& o : q.objectives)
				o.done = true;
		Notify("OnQuestChanged", { id, state });
	}

	void CompleteObjective(const std::string& quest, const std::string& objective)
	{
		Quest& q = GetOrCreateQuest(quest);
		if (q.state == "inactive")
			q.state = "active";

		Objective* found = nullptr;
		for (Objective& o : q.objectives)
			if (o.id == objective)
				found = &o;
		if (!found)
		{
			q.objectives.push_back({ objective, objective, false });
			found = &q.objectives.back();
		}
		found->done = true;
		Notify("OnQuestChanged", { quest, q.state });
	}

	Json ToJson()
	{
		Json quests = Json::array();
		for (const Quest& q : g_quests)
			quests.push_back(QuestToJson(q));
		return { { "variables", g_vars }, { "quests", quests } };
	}

	void FromJson(const Json& j)
	{
		g_vars = j.contains("variables") && j["variables"].is_object() ? j["variables"] : Json::object();
		g_quests.clear();
		if (j.contains("quests") && j["quests"].is_array())
			for (const Json& q : j["quests"])
				if (q.is_object())
					g_quests.push_back(QuestFromJson(q));
	}

	bool Save(const std::string& slot)
	{
		std::error_code ec;
		std::filesystem::create_directories("saves", ec);
		std::ofstream out(SaveFile(slot), std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << ToJson().dump(2) << "\n";
		return true;
	}

	bool Load(const std::string& slot)
	{
		std::ifstream in(SaveFile(slot), std::ios::binary);
		if (!in)
			return false;
		const Json j = Json::parse(in, nullptr, false);
		if (!j.is_object())
			return false;
		FromJson(j);
		return true;
	}
}
