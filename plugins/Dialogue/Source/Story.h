#pragma once

// =============================================================================
// Story : variables (flags, counters, names...) and quests, shared by the
// dialogues, the game code and JavaScript. Reset at each game start ; saved
// / loaded on demand (saves/<slot>.story.json).
//
//   JS : Story.get("coins"), Story.set("met_bob", true), Story.add("coins", 1),
//        Story.save("slot1"), Story.load("slot1"), Story.reset()
//        Quest.start("find_key"), Quest.complete(...), Quest.fail(...),
//        Quest.objective("find_key", "open_chest"), Quest.state(id), Quest.list()
//
// Quests are declared in assets/story/quests.json :
//   [ { "id": "find_key", "title": "The lost key", "description": "...",
//       "objectives": [ { "id": "talk", "text": "Talk to Bob" } ] } ]
// (a quest started without declaration is created on the fly).
//
// Events (interface "StoryEvents", to the actors that implement it) :
//   OnStoryVariableChanged(name), OnQuestChanged(quest, state)
// =============================================================================

#include <json/json.hpp>

#include <string>
#include <vector>

namespace story
{
	struct Objective
	{
		std::string id;
		std::string text;
		bool done = false;
	};

	struct Quest
	{
		std::string id;
		std::string title;
		std::string description;
		std::string state = "inactive";   // inactive, active, completed, failed
		std::vector<Objective> objectives;
	};

	/** Variables emptied, quests back to the declarations (assets/story/quests.json). */
	void Reset();

	nlohmann::json Get(const std::string& name);
	bool Has(const std::string& name);
	void Set(const std::string& name, const nlohmann::json& value);
	/** Numbers : value += delta (missing = 0). */
	void Add(const std::string& name, double delta);
	nlohmann::json& Variables();

	/** "{coins} coins" -> "3 coins". */
	std::string Format(const std::string& text);

	std::vector<Quest>& Quests();
	Quest* FindQuest(const std::string& id);
	/** Creates it when not declared. */
	Quest& GetOrCreateQuest(const std::string& id);
	void SetQuestState(const std::string& id, const std::string& state);
	void CompleteObjective(const std::string& quest, const std::string& objective);

	/** Declared quests (assets/story/quests.json), for the editor too. */
	std::vector<Quest> LoadQuestDeclarations();

	nlohmann::json ToJson();
	void FromJson(const nlohmann::json& j);

	/** saves/<slot>.story.json (working directory). */
	bool Save(const std::string& slot);
	bool Load(const std::string& slot);
}
