#include "CommandRegistry.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace lynx::editor::commands
{
	namespace
	{
		std::map<std::string, CommandInfo>& Registry()
		{
			static std::map<std::string, CommandInfo> registry;
			return registry;
		}

		std::function<void(const std::string&)> g_begin_group;
		std::function<void()> g_end_group;
		std::function<void(const std::string&)> g_log;

		void RegisterBuiltins();

		// Built-in commands are added the first time the registry is used.
		void EnsureBuiltins()
		{
			static bool done = false;

			if (done)
				return;

			done = true;
			RegisterBuiltins();
		}


		// ---------------------------------------------------------------------
		// batch : runs commands one after the other (async ones included)
		// ---------------------------------------------------------------------

		struct BatchState
		{
			Json commands;
			Json results = Json::array();
			size_t index = 0;
			bool stop_on_error = true;
			bool grouped = false;
			bool failed = false;
			CommandContext context;
			Respond respond;
		};

		void BatchStep(const std::shared_ptr<BatchState>& state)
		{
			// Loop while the commands answer synchronously, recurse when one
			// answers later.
			while (state->index < state->commands.size())
			{
				const Json& entry = state->commands[state->index];

				std::string name;
				Json params = Json::object();

				if (entry.is_object())
				{
					if (entry.contains("command") && entry["command"].is_string())
						name = entry["command"].get<std::string>();

					if (entry.contains("params") && entry["params"].is_object())
						params = entry["params"];
				}

				if (name.empty() || name == "batch")
				{
					state->results.push_back({ { "ok", false }, { "error", "invalid batch entry (\"command\" missing, or nested batch)" } });
					state->failed = true;
					++state->index;

					if (state->stop_on_error)
						break;

					continue;
				}

				// 0 : running, 1 : answered during Execute(), 2 : answers later.
				auto mode = std::make_shared<int>(0);

				Execute(name, params, state->context,
					[state, mode](bool ok, Json value)
					{
						Json result = { { "ok", ok } };
						result[ok ? "result" : "error"] = std::move(value);
						state->results.push_back(std::move(result));

						if (!ok)
							state->failed = true;

						++state->index;

						if (*mode == 2)
						{
							// Answered later : continue from here.
							if (state->failed && state->stop_on_error)
								state->index = state->commands.size();

							BatchStep(state);
						}
						else
						{
							*mode = 1;
						}
					});

				if (*mode == 0)
				{
					*mode = 2;
					return;
				}

				if (state->failed && state->stop_on_error)
					break;
			}

			// Done.
			if (state->grouped)
				EndUndoGroup();

			Json result = {
				{ "ok", !state->failed },
				{ "count", state->results.size() },
				{ "results", std::move(state->results) },
			};

			Respond respond = std::move(state->respond);
			respond(true, std::move(result));
		}


		void RegisterBuiltins()
		{
			RegisterAsync(
				"batch",
				"Runs several commands in one call, in order. They form ONE undo step. "
				"Result : {ok, count, results:[{ok, result|error}]}.",
				{
					{ "commands", "array", "List of {\"command\": name, \"params\": {...}}.", true },
					{ "stop_on_error", "boolean", "Stop at the first error (default true).", false },
					{ "undo_name", "string", "Name of the undo step (log only).", false },
				},
				[](const Json& params, const CommandContext& context, Respond respond)
				{
					if (!params.contains("commands") || !params["commands"].is_array())
						throw CommandError("parameter 'commands' must be an array");

					auto state = std::make_shared<BatchState>();
					state->commands = params["commands"];
					state->stop_on_error = GetBool(params, "stop_on_error", true);
					state->context = context;
					state->respond = std::move(respond);

					BeginUndoGroup(GetString(params, "undo_name", "batch"));
					state->grouped = true;

					BatchStep(state);
				});

			Register(
				"help",
				"Lists the commands (name, description, parameters). With 'name' : only that command.",
				{
					{ "name", "string", "A command name (optional).", false },
					{ "filter", "string", "Only the commands whose name contains this text.", false },
				},
				[](const Json& params, const CommandContext&) -> Json
				{
					if (Has(params, "name"))
					{
						const std::string name = GetString(params, "name");
						const CommandInfo* command = Find(name);

						if (!command)
							throw CommandError("unknown command '" + name + "'");

						return Describe(*command);
					}

					const std::string filter = GetString(params, "filter", "");
					Json list = Json::array();

					for (const CommandInfo* command : List())
					{
						if (filter.empty() || command->name.find(filter) != std::string::npos)
							list.push_back(Describe(*command));
					}

					return list;
				});

			Register(
				"undo.begin_group",
				"Starts an undo group : the changes until undo.end_group are undone by ONE Ctrl+Z. "
				"Groups left open are closed when the client disconnects.",
				{ { "name", "string", "Name of the group (log only).", false } },
				[](const Json& params, const CommandContext&) -> Json
				{
					BeginUndoGroup(GetString(params, "name", "script"));
					return Json::object();
				});

			Register(
				"undo.end_group",
				"Ends the group started by undo.begin_group.",
				{},
				[](const Json&, const CommandContext&) -> Json
				{
					EndUndoGroup();
					return Json::object();
				});
		}
	}


	void Register(
		const std::string& name,
		const std::string& description,
		std::vector<ParamInfo> params,
		SyncHandler handler)
	{
		RegisterAsync(
			name,
			description,
			std::move(params),
			[handler = std::move(handler)](const Json& p, const CommandContext& context, Respond respond)
			{
				Json result = handler(p, context);
				respond(true, std::move(result));
			});
	}


	void RegisterAsync(
		const std::string& name,
		const std::string& description,
		std::vector<ParamInfo> params,
		AsyncHandler handler)
	{
		CommandInfo info;
		info.name = name;
		info.description = description;
		info.params = std::move(params);
		info.handler = std::move(handler);

		Registry()[name] = std::move(info);
	}


	const CommandInfo* Find(const std::string& name)
	{
		EnsureBuiltins();

		auto it = Registry().find(name);
		return it == Registry().end() ? nullptr : &it->second;
	}


	std::vector<const CommandInfo*> List()
	{
		EnsureBuiltins();

		std::vector<const CommandInfo*> list;
		list.reserve(Registry().size());

		for (const auto& [name, info] : Registry())
			list.push_back(&info);

		return list;
	}


	Json Describe(const CommandInfo& command)
	{
		Json params = Json::array();

		for (const ParamInfo& param : command.params)
		{
			params.push_back({
				{ "name", param.name },
				{ "type", param.type },
				{ "description", param.description },
				{ "required", param.required },
			});
		}

		return {
			{ "name", command.name },
			{ "description", command.description },
			{ "params", std::move(params) },
		};
	}


	void Execute(
		const std::string& name,
		const Json& params,
		const CommandContext& context,
		Respond respond)
	{
		const CommandInfo* command = Find(name);

		if (!command)
		{
			respond(false, "unknown command '" + name + "' (see the 'help' command)");
			return;
		}

		const Json& safe_params = params.is_object() ? params : Json::object();

		// The handler may throw after calling respond (bug) : answer once only.
		auto answered = std::make_shared<bool>(false);

		Respond once = [answered, respond](bool ok, Json value)
		{
			if (*answered)
				return;

			*answered = true;
			respond(ok, std::move(value));
		};

		try
		{
			command->handler(safe_params, context, once);
		}
		catch (const CommandError& error)
		{
			once(false, error.what());
		}
		catch (const Json::exception& error)
		{
			once(false, std::string("bad parameter : ") + error.what());
		}
		catch (const std::exception& error)
		{
			once(false, std::string("error : ") + error.what());
		}
	}


	// -------------------------------------------------------------------------
	// Undo groups
	// -------------------------------------------------------------------------

	void SetUndoGroupHooks(std::function<void(const std::string&)> begin, std::function<void()> end)
	{
		g_begin_group = std::move(begin);
		g_end_group = std::move(end);
	}

	void BeginUndoGroup(const std::string& name)
	{
		if (g_begin_group)
			g_begin_group(name);
	}

	void EndUndoGroup()
	{
		if (g_end_group)
			g_end_group();
	}


	// -------------------------------------------------------------------------
	// Log
	// -------------------------------------------------------------------------

	void SetLogCallback(std::function<void(const std::string&)> callback)
	{
		g_log = std::move(callback);
	}

	void Log(const std::string& line)
	{
		if (g_log)
			g_log(line);
	}


	// -------------------------------------------------------------------------
	// Parameters
	// -------------------------------------------------------------------------

	bool Has(const Json& params, const char* name)
	{
		return params.is_object() && params.contains(name) && !params[name].is_null();
	}

	namespace
	{
		const Json& Require(const Json& params, const char* name)
		{
			if (!Has(params, name))
				throw CommandError(std::string("missing parameter '") + name + "'");

			return params[name];
		}
	}

	std::string GetString(const Json& params, const char* name)
	{
		const Json& value = Require(params, name);

		if (value.is_string())
			return value.get<std::string>();

		if (value.is_number() || value.is_boolean())
			return value.dump();

		throw CommandError(std::string("parameter '") + name + "' must be a string");
	}

	std::string GetString(const Json& params, const char* name, const std::string& fallback)
	{
		return Has(params, name) ? GetString(params, name) : fallback;
	}

	double GetNumber(const Json& params, const char* name)
	{
		const Json& value = Require(params, name);

		if (value.is_number())
			return value.get<double>();

		if (value.is_boolean())
			return value.get<bool>() ? 1.0 : 0.0;

		if (value.is_string())
		{
			try
			{
				size_t used = 0;
				const std::string text = value.get<std::string>();
				const double number = std::stod(text, &used);

				if (used == text.size())
					return number;
			}
			catch (...)
			{
			}
		}

		throw CommandError(std::string("parameter '") + name + "' must be a number");
	}

	double GetNumber(const Json& params, const char* name, double fallback)
	{
		return Has(params, name) ? GetNumber(params, name) : fallback;
	}

	int GetInt(const Json& params, const char* name)
	{
		return static_cast<int>(std::lround(GetNumber(params, name)));
	}

	int GetInt(const Json& params, const char* name, int fallback)
	{
		return Has(params, name) ? GetInt(params, name) : fallback;
	}

	bool GetBool(const Json& params, const char* name, bool fallback)
	{
		if (!Has(params, name))
			return fallback;

		const Json& value = params[name];

		if (value.is_boolean())
			return value.get<bool>();

		if (value.is_number())
			return value.get<double>() != 0.0;

		if (value.is_string())
		{
			const std::string text = value.get<std::string>();

			if (text == "true" || text == "1" || text == "yes")
				return true;

			if (text == "false" || text == "0" || text == "no")
				return false;
		}

		throw CommandError(std::string("parameter '") + name + "' must be a boolean");
	}

	bool ReadVector(const Json& value, float* out, int n)
	{
		static const char* const kNames[] = { "x", "y", "z", "w" };

		if (value.is_array())
		{
			if (value.size() < 2 || static_cast<int>(value.size()) > n)
				return false;

			for (int i = 0; i < n; ++i)
			{
				if (i < static_cast<int>(value.size()))
				{
					if (!value[i].is_number())
						return false;

					out[i] = value[i].get<float>();
				}
				else
				{
					out[i] = 0.f;
				}
			}

			return true;
		}

		if (value.is_object())
		{
			for (int i = 0; i < n; ++i)
			{
				if (value.contains(kNames[i]) && value[kNames[i]].is_number())
					out[i] = value[kNames[i]].get<float>();
				else if (i < 2)
					return false;
				else
					out[i] = 0.f;
			}

			return true;
		}

		return false;
	}

	void GetVector(const Json& params, const char* name, float* out, int n)
	{
		const Json& value = Require(params, name);

		if (!ReadVector(value, out, n))
		{
			throw CommandError(
				std::string("parameter '") + name + "' must be [x, y" + (n > 2 ? ", z]" : "]") +
				" or {\"x\":.., \"y\":..}");
		}
	}
}
