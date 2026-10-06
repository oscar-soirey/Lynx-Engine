#pragma once

// =============================================================================
// .dialogue files (JSON) : a graph of nodes. Shared by the runtime module
// (DialogueRunner) and the editor module (graph editor).
// -----------------------------------------------------------------------------
// {
//   "start": "n1",
//   "nodes": [
//     { "id": "n1", "type": "line", "speaker": "Bob", "text": "Hello {player_name} !", "next": "n2" },
//     { "id": "n2", "type": "choice", "speaker": "Bob", "text": "Help me ?",
//       "choices": [ { "text": "Yes", "next": "n3" },
//                    { "text": "Give a coin", "next": "n4", "condition": "Story.get('coins') > 0" } ] },
//     { "id": "n3", "type": "quest", "quest": "find_key", "action": "start", "next": "n5" },
//     { "id": "n4", "type": "set", "variable": "coins", "op": "add", "value": -1, "next": "n5" },
//     { "id": "n5", "type": "branch", "condition": "Story.get('met_bob')", "next": "n6", "else": "n7" },
//     { "id": "n6", "type": "event", "event": "OpenDoor", "script": "print('door')", "next": "n7" },
//     { "id": "n7", "type": "end" }
//   ]
// }
//
// Node types :
//   line    speaker + text, waits for "continue"            -> next
//   choice  speaker + text (optional), waits for a choice   -> choices[i].next
//           (a choice whose JS condition is false is hidden)
//   branch  JS condition                                    -> next (true) / else (false)
//   set     Story variable : op "set" (default) / "add" / "toggle", value (JSON)
//   event   DialogueEvents.OnDialogueEvent(event) to the actors that
//           implement it, then the JS `script` (optional)
//   quest   quest + action : "start" / "complete" / "fail" / "objective"
//           (objective : its id, marked done)
//   end     ends the dialogue (a missing "next" ends it too)
// "x" / "y" : position in the graph editor. {name} in a text : Story variable.
// =============================================================================

#include <string>
#include <vector>

namespace dialogue
{
	struct Choice
	{
		std::string text;
		std::string next;
		std::string condition;
	};

	struct Node
	{
		std::string id;
		std::string type = "line";

		std::string speaker;
		std::string text;
		std::string next;
		std::string else_next;      // branch : condition false
		std::string condition;      // branch
		std::vector<Choice> choices;

		std::string variable;       // set
		std::string op = "set";
		std::string value_json = "true";

		std::string event;          // event
		std::string script;

		std::string quest;          // quest
		std::string action = "start";
		std::string objective;

		float x = 0.f;
		float y = 0.f;
	};

	struct Asset
	{
		std::string start;
		std::vector<Node> nodes;

		Node* Find(const std::string& id);
		const Node* Find(const std::string& id) const;
		/** A free id ("n1", "n2"...). */
		std::string NewId() const;
	};

	/** false : not a dialogue (`error`). */
	bool Parse(const std::string& text, Asset& out, std::string& error);
	std::string Serialize(const Asset& asset);

	/** Every node type, in the order of the editor menus. */
	const std::vector<std::string>& NodeTypes();
}
