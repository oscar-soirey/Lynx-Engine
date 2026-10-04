#pragma once

// =============================================================================
// Editor commands
// -----------------------------------------------------------------------------
// Every action an external tool (Python script, AI model through MCP, editor
// console) can do on the editor is a named command :
//
//     "actor.spawn"  { "class": "Enemy", "location": [3, 4, 0] }
//         -> { "id": "Enemy_2" }
//
// Commands are registered once (see EditorCommands.inl) and always executed on
// the editor main thread, between two frames : they can use the engine, the
// level and HRL freely.
//
// A handler returns its result as JSON, or throws CommandError (or any
// std::exception) for an error.
// =============================================================================

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <json/json.hpp>

namespace lynx::editor::commands
{
	using Json = nlohmann::ordered_json;

	// Error shown to the caller (bad parameter, unknown actor...).
	class CommandError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	// Who runs the command.
	struct CommandContext
	{
		int client = 0;               // server client id, 0 = editor itself (console)
		std::string source;           // "console", "tcp:3", ...
	};

	struct ParamInfo
	{
		std::string name;
		// "string", "integer", "number", "boolean", "vec2", "vec3", "array",
		// "object", "any". Used for the docs and the MCP tool schemas.
		std::string type;
		std::string description;
		bool required = false;
	};

	// Called once with the result : ok = false -> `value` is the error text.
	using Respond = std::function<void(bool ok, Json value)>;

	using SyncHandler = std::function<Json(const Json& params, const CommandContext& context)>;

	// Answers later (ex : a screenshot of the next frame). `respond` MUST be
	// called exactly once.
	using AsyncHandler = std::function<void(const Json& params, const CommandContext& context, Respond respond)>;

	struct CommandInfo
	{
		std::string name;
		std::string description;
		std::vector<ParamInfo> params;
		AsyncHandler handler;
	};


	void Register(
		const std::string& name,
		const std::string& description,
		std::vector<ParamInfo> params,
		SyncHandler handler
	);

	void RegisterAsync(
		const std::string& name,
		const std::string& description,
		std::vector<ParamInfo> params,
		AsyncHandler handler
	);

	// nullptr when unknown.
	const CommandInfo* Find(const std::string& name);

	// Sorted by name.
	std::vector<const CommandInfo*> List();

	// { "name", "description", "params": [ {name, type, description, required} ] }
	Json Describe(const CommandInfo& command);

	// Runs a command (main thread). Errors (unknown command, exception) are
	// given to `respond` with ok = false. "batch" is built in (see the .cpp).
	void Execute(
		const std::string& name,
		const Json& params,
		const CommandContext& context,
		Respond respond
	);


	// -------------------------------------------------------------------------
	// Undo groups : every change done between Begin and End is ONE Ctrl+Z.
	// The editor installs the functions (a batch is always one group).
	// -------------------------------------------------------------------------

	void SetUndoGroupHooks(std::function<void(const std::string& name)> begin, std::function<void()> end);
	void BeginUndoGroup(const std::string& name);
	void EndUndoGroup();


	// -------------------------------------------------------------------------
	// Log of the executed commands (shown in the Commands window).
	// -------------------------------------------------------------------------

	void SetLogCallback(std::function<void(const std::string& line)> callback);
	void Log(const std::string& line);


	// -------------------------------------------------------------------------
	// Parameter helpers (throw CommandError with a clear message)
	// -------------------------------------------------------------------------

	bool Has(const Json& params, const char* name);

	std::string GetString(const Json& params, const char* name);
	std::string GetString(const Json& params, const char* name, const std::string& fallback);

	double GetNumber(const Json& params, const char* name);
	double GetNumber(const Json& params, const char* name, double fallback);

	int GetInt(const Json& params, const char* name);
	int GetInt(const Json& params, const char* name, int fallback);

	bool GetBool(const Json& params, const char* name, bool fallback);

	// [x, y, (z)] or {"x":, "y":, ("z":)} ; n = 2 or 3 (missing z = 0).
	bool ReadVector(const Json& value, float* out, int n);
	void GetVector(const Json& params, const char* name, float* out, int n);
}
