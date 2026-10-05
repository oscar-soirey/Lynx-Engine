#include "CommandsWindow.h"

#include "CommandRegistry.h"
#include "CommandServer.h"
#include "ScriptRunner.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#endif

namespace fs = std::filesystem;

namespace lynx::editor::commands_window
{
	namespace
	{
		using commands::Json;
		using Clock = std::chrono::steady_clock;

		// ---------------------------------------------------------------------
		// Local model chat (Ollama HTTP API)
		// ---------------------------------------------------------------------

		struct LocalAiState
		{
			std::mutex mutex;
			std::thread worker;
			bool busy = false;       // UI thread only
			bool done = false;       // guarded by mutex
			bool refresh_started = false;
			bool is_model_result = false;
			std::vector<std::string> models;
			int selected_model = -1;
			std::string status = "Ollama local server : not checked";
			std::string generated_code;
			std::vector<std::pair<std::string, std::string>> conversation;
			std::vector<Json> messages;
			std::string model;               // model of the last request (auto-fix uses it)
			std::string last_request;        // last request typed by the user (context of the fixes)
			bool answer_has_code = false;    // last answer contained a ```python block
			std::string last_context;        // system prompt of the last request (debug view)
			int last_context_tokens = 0;     // rough estimate
			int last_num_ctx = 0;
		};

		LocalAiState g_local_ai;
		char g_ai_prompt[4096] = {};
		std::vector<char> g_ai_code_buffer(256 * 1024, '\0');
		bool g_ai_code_loaded = false;

#ifdef _WIN32
		bool OllamaRequest(const wchar_t* method, const wchar_t* path, const std::string& body,
		                  std::string& response, std::string& error)
		{
			HINTERNET session = WinHttpOpen(L"LynxEditor/LocalAI", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if (!session)
			{
				error = "Could not initialize the local HTTP client.";
				return false;
			}

			WinHttpSetTimeouts(session, 3000, 3000, 3000, 180000);
			HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", 11434, 0);
			HINTERNET request = connection ? WinHttpOpenRequest(connection, method, path, nullptr,
			                                                     WINHTTP_NO_REFERER,
			                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
			bool ok = false;

			if (request)
			{
				const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\n";
				const void* data = body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data();
				const DWORD data_size = static_cast<DWORD>(body.size());
				if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
				                      const_cast<void*>(data), data_size, data_size, 0) &&
				    WinHttpReceiveResponse(request, nullptr))
				{
					DWORD status_code = 0;
					DWORD status_size = sizeof(status_code);
					WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					                   WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
					                   WINHTTP_NO_HEADER_INDEX);
					DWORD available = 0;
					while (WinHttpQueryDataAvailable(request, &available) && available > 0)
					{
						const size_t old_size = response.size();
						response.resize(old_size + available);
						DWORD read = 0;
						if (!WinHttpReadData(request, response.data() + old_size, available, &read))
						{
							response.resize(old_size);
							break;
						}
						response.resize(old_size + read);
					}
					ok = status_code >= 200 && status_code < 300;
					if (!ok)
						error = "Ollama returned HTTP " + std::to_string(status_code) + ": " + response;
				}
				else
				{
					error = "Cannot reach Ollama at http://localhost:11434. Start Ollama and install a model.";
				}
			}
			else
			{
				error = "Could not connect to Ollama at http://localhost:11434.";
			}

			if (request) WinHttpCloseHandle(request);
			if (connection) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return ok;
		}
#endif

		void StartModelRefresh()
		{
			if (g_local_ai.busy)
				return;
			if (g_local_ai.worker.joinable())
				g_local_ai.worker.join();
			g_local_ai.busy = true;
			g_local_ai.refresh_started = true;
			g_local_ai.done = false;
			g_local_ai.worker = std::thread([]()
			{
				std::vector<std::string> models;
				std::string status;
#ifdef _WIN32
				std::string response;
				if (OllamaRequest(L"GET", L"/api/tags", {}, response, status))
				{
					try
					{
						const Json root = Json::parse(response);
						for (const Json& item : root.value("models", Json::array()))
							if (item.contains("name") && item["name"].is_string())
								models.push_back(item["name"].get<std::string>());
						status = models.empty() ? "No local model found. Install one with `ollama pull <model>`."
						                       : "Connected to Ollama. Choose an installed model.";
					}
					catch (const std::exception& e) { status = std::string("Invalid Ollama model list: ") + e.what(); }
				}
#else
				status = "Local Ollama integration is currently available on Windows only.";
#endif
				std::lock_guard<std::mutex> lock(g_local_ai.mutex);
				g_local_ai.models = std::move(models);
				g_local_ai.status = std::move(status);
				if (g_local_ai.selected_model >= static_cast<int>(g_local_ai.models.size()))
					g_local_ai.selected_model = g_local_ai.models.empty() ? -1 : 0;
				else if (g_local_ai.selected_model < 0 && !g_local_ai.models.empty())
					g_local_ai.selected_model = 0;
				g_local_ai.is_model_result = true;
				g_local_ai.done = true;
			});
		}

		std::string ExtractPython(const std::string& answer)
		{
			size_t fence = answer.find("```python");
			size_t marker_size = 9;
			if (fence == std::string::npos)
			{
				fence = answer.find("```");
				marker_size = 3;
			}
			if (fence == std::string::npos)
				return {};
			size_t start = answer.find('\n', fence + marker_size);
			if (start == std::string::npos)
				return {};
			++start;
			const size_t end = answer.find("```", start);
			return end == std::string::npos ? std::string() : answer.substr(start, end - start);
		}

		// Answer without its ``` blocks (shown in the chat). An unclosed block
		// is removed up to the end.
		std::string StripCodeBlocks(const std::string& answer)
		{
			std::string out;
			bool had_code = false;
			size_t position = 0;

			while (position < answer.size())
			{
				const size_t fence = answer.find("```", position);
				if (fence == std::string::npos)
				{
					out += answer.substr(position);
					break;
				}
				out += answer.substr(position, fence - position);
				had_code = true;
				const size_t close = answer.find("```", fence + 3);
				if (close == std::string::npos)
					break;
				position = close + 3;
			}

			// Trim the blank lines left where the blocks were.
			std::string clean;
			int newlines = 0;
			for (const char c : out)
			{
				if (c == '\n')
				{
					if (++newlines > 2)
						continue;
				}
				else if (c != ' ' && c != '\t' && c != '\r')
				{
					newlines = 0;
				}
				clean += c;
			}
			const size_t first = clean.find_first_not_of(" \t\r\n");
			const size_t last = clean.find_last_not_of(" \t\r\n");
			clean = first == std::string::npos ? std::string() : clean.substr(first, last - first + 1);

			if (had_code)
				clean += std::string(clean.empty() ? "" : "\n\n") + "(Script : voir \"Generated script\" en bas.)";

			return clean;
		}

		bool LooksLikePython(const std::string& code)
		{
			size_t start = 0;
			while (start < code.size())
			{
				const size_t end = code.find('\n', start);
				std::string line = code.substr(start, end == std::string::npos ? std::string::npos : end - start);
				const size_t first = line.find_first_not_of(" \t");
				if (first != std::string::npos)
				{
					line.erase(0, first);
					if (line.rfind("//", 0) == 0 || line.rfind("let ", 0) == 0 ||
						line.rfind("const ", 0) == 0 || line.rfind("function ", 0) == 0 ||
						line.rfind("var ", 0) == 0)
						return false;
				}
				if (end == std::string::npos) break;
				start = end + 1;
			}
			return true;
		}

		// ---------------------------------------------------------------------
		// Context given to the local model
		// ---------------------------------------------------------------------
		// A small local model cannot explore the scene by itself : everything it
		// needs to resolve "move the voxel character to the right" (which actor,
		// which axis, how far) is computed here and written in the system prompt,
		// rebuilt at every message so it is always the CURRENT scene.

		constexpr int kMaxContextActors = 150;   // actors listed in the prompt
		constexpr size_t kMaxHistoryMessages = 12;  // user/assistant messages kept

		// Runs a SYNC command right now (UI thread = editor main thread).
		bool RunNow(const std::string& name, const Json& params, Json& out)
		{
			bool answered = false;
			bool ok = false;
			commands::CommandContext context;
			context.source = "local-ai";
			commands::Execute(name, params, context, [&](bool success, Json value)
			{
				answered = true;
				ok = success;
				out = std::move(value);
			});
			return answered && ok;
		}

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		bool Contains(const std::string& text, const std::string& part)
		{
			return !part.empty() && text.find(part) != std::string::npos;
		}

		std::string Num(double value)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.3f", value);
			std::string s = text;
			while (!s.empty() && s.back() == '0') s.pop_back();
			if (!s.empty() && s.back() == '.') s.pop_back();
			return s == "-0" ? "0" : s;
		}

		std::string Vec(const Json& value)
		{
			if (!value.is_array())
				return value.dump();
			std::string out = "[";
			for (size_t i = 0; i < value.size(); ++i)
			{
				if (i) out += ", ";
				out += value[i].is_number() ? Num(value[i].get<double>()) : value[i].dump();
			}
			return out + "]";
		}

		// json.value(key, fallback) that never throws : `json` not an object, key
		// missing, or a value of another type (null...) -> fallback.
		template <typename T>
		T Field(const Json& json, const char* key, T fallback)
		{
			if (!json.is_object())
				return fallback;
			const auto it = json.find(key);
			if (it == json.end())
				return fallback;
			try { return it->template get<T>(); }
			catch (const Json::exception&) { return fallback; }
		}

		std::string Truncate(std::string text, size_t max)
		{
			if (text.size() > max)
				text = text.substr(0, max) + "...";
			return text;
		}

		// Words of the request (lower case, >= 3 bytes, without common words).
		std::vector<std::string> RequestWords(const std::string& prompt, bool skip_common = true)
		{
			static const char* const kStop[] = {
				"les", "des", "une", "un", "le", "la", "de", "du", "vers", "dans", "sur", "sous", "avec", "pour",
				"par", "plus", "moins", "cases", "case", "voxels", "voxel", "fois", "peu", "the", "and", "to",
				"move", "deplace", "déplace", "bouge", "mets", "met", "place", "droite", "gauche", "haut", "bas",
				"right", "left", "up", "down", "fais", "fait", "est", "qui", "que", "son", "ses", "tout", "tous",
			};
			std::vector<std::string> words;
			std::string word;
			const std::string text = Lower(prompt) + " ";
			for (const char c : text)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (std::isalnum(u) || u >= 0x80 || c == '_')
				{
					word += c;
					continue;
				}
				bool stop = word.empty() || (skip_common && word.size() < 3);
				for (const char* s : kStop)
					stop = stop || (skip_common && word == s);
				if (!stop)
					words.push_back(word);
				word.clear();
			}
			return words;
		}

		// French / English words of a request -> parts of class / id names.
		const std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>& Synonyms()
		{
			static const std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> kSynonyms = {
				{ { "personnage", "perso", "joueur", "héros", "heros", "hero", "player", "character", "avatar", "bonhomme" },
				  { "player", "character", "hero", "humanoid", "pawn", "avatar", "perso" } },
				{ { "ennemi", "ennemis", "monstre", "monstres", "enemy", "enemies", "monster", "méchant" },
				  { "enemy", "monster", "mob", "foe", "ennemi" } },
				{ { "caméra", "camera" }, { "camera", "cam" } },
				{ { "lumière", "lumiere", "lampe", "torche", "light" }, { "light", "lamp", "torch" } },
				{ { "musique", "sound", "audio", "bruit" }, { "sound", "audio", "music" } },
				{ { "porte", "door" }, { "door" } },
				{ { "pièce", "piece", "coin", "bonus" }, { "coin", "pickup", "bonus" } },
				{ { "voxel", "voxels" }, { "voxel" } },
			};
			return kSynonyms;
		}

		struct ActorEntry
		{
			std::string id;
			std::string cls;
			Json location;
			std::string cell;   // "[x, y]" or "outside"
			int score = 0;
		};

		// Python syntax of a command call, read from AI_API_REFERENCE.md :
		// "lynx.call" + dict or keywords. Empty when the reference has none.
		struct CallSyntax
		{
			std::string function;
			bool keywords = false;
		};

		CallSyntax DetectCallSyntax(const std::string& reference)
		{
			for (const char* candidate : { "lynx.call(", "lynx.command(", "lynx.run(", "lynx.execute(",
			                               "lynx.send(", "lynx.cmd(", "lynx.invoke(" })
			{
				const size_t at = reference.find(candidate);
				if (at == std::string::npos)
					continue;
				CallSyntax syntax;
				syntax.function = std::string(candidate, std::strlen(candidate) - 1);
				// lynx.call("name", {...})  or  lynx.call("name", id=...)
				const size_t comma = reference.find(',', at);
				if (comma != std::string::npos && comma < at + 80)
				{
					const size_t next = reference.find_first_not_of(" \t", comma + 1);
					syntax.keywords = next != std::string::npos && reference[next] != '{';
				}
				return syntax;
			}
			return {};
		}

		std::string FormatCall(const CallSyntax& syntax, const std::string& name, const std::string& dict_args,
		                       const std::string& keyword_args)
		{
			return syntax.function + "(\"" + name + "\", " + (syntax.keywords ? keyword_args : dict_args) + ")";
		}

		// Live state of the editor, as compact text.
		std::string BuildSceneContext(const std::string& prompt)
		{
			std::string out = "=== LIVE SCENE STATE (captured just now, this is the truth) ===\n";

			Json info;
			if (!RunNow("editor.info", Json::object(), info) || !info.is_object())
				return out + "Editor state unavailable.\n";

			const std::string selected = Field(info, "selected_actor", std::string());
			const bool playing = Field(info, "playing", false);

			out += "Project: " + Field(info, "project", std::string("?")) +
			       " | level loaded: " + (Field(info, "level_loaded", false) ? "yes" : "no") +
			       " | play mode: " + (playing ? "YES (changes are lost at Stop, voxel edits refused)" : "no") +
			       " | unsaved changes: " + (Field(info, "unsaved_changes", false) ? "yes" : "no") + "\n";
			out += "Editor camera (world x, y, height z): " + Vec(Field(info, "camera", Json::array())) + "\n";
			out += "Selected actor: " + (selected.empty() ? std::string("none") : selected) + "\n";

			// --- Units and directions (measured, not assumed) -----------------
			Json c00, c11;
			double cell_w = 1.0, cell_h = 1.0;
			bool have_cells = RunNow("voxel.cell_to_world", Json{ {"x", 0}, {"y", 0} }, c00) &&
			                  RunNow("voxel.cell_to_world", Json{ {"x", 1}, {"y", 1} }, c11);
			if (have_cells)
			{
				cell_w = Field(c11, "x", 1.0) - Field(c00, "x", 0.0);
				cell_h = Field(c11, "y", 1.0) - Field(c00, "y", 0.0);
				have_cells = cell_w != 0.0 && cell_h != 0.0;
			}

			out += "\nCOORDINATES:\n";
			out += "- Actors use WORLD units (transform.location [x, y, z]). Voxels use integer CELLS [x, y].\n";
			if (have_cells)
			{
				out += "- 1 voxel cell = " + Num(std::abs(cell_w)) + " world units in x, " + Num(std::abs(cell_h)) +
				       " in y. Cell [0, 0] center = world " + Vec(Json::array({ Field(c00, "x", 0.0), Field(c00, "y", 0.0) })) + ".\n";
				const std::string one_right = Vec(Json::array({ cell_w, 0, 0 }));
				const std::string one_left = Vec(Json::array({ -cell_w, 0, 0 }));
				const std::string one_up = Vec(Json::array({ 0, cell_h, 0 }));
				const std::string one_down = Vec(Json::array({ 0, -cell_h, 0 }));
				out += "- Screen directions for actors (offset of ONE cell, use actor.set_transform with relative=true):\n"
				       "    droite / right -> location " + one_right + "\n"
				       "    gauche / left  -> location " + one_left + "\n"
				       "    haut / up / monter -> location " + one_up + "\n"
				       "    bas / down / descendre -> location " + one_down + "\n";
				out += "- Voxel cells: x grows to the right, y grows upward (voxel.get_region rows[0] is the bottom row).\n";
			}
			else
			{
				out += "- Voxel/world conversion unavailable right now (no voxel world ?). Assume x = right, y = up.\n";
			}
			out += "- No distance given (\"vers la droite\", \"un peu\") = 1 cell. \"de N cases/voxels/blocs\" = N cells. "
			       "z is depth/layer : never change it for a left/right/up/down move.\n";

			// --- Actors -------------------------------------------------------
			Json actors;
			std::vector<ActorEntry> entries;
			if (RunNow("level.list_actors", Json{ {"limit", kMaxContextActors + 1} }, actors) && actors.is_array())
			{
				for (const Json& actor : actors)
				{
					ActorEntry entry;
					entry.id = Field(actor, "id", std::string());
					entry.cls = Field(actor, "class", std::string());
					entry.location = Field(actor, "location", Json::array());
					entry.cell = "outside";
					if (entry.location.is_array() && entry.location.size() >= 2)
					{
						Json cell;
						if (RunNow("voxel.world_to_cell", Json{ {"x", entry.location[0]}, {"y", entry.location[1]} }, cell))
							entry.cell = Vec(Json::array({ Field(cell, "x", 0), Field(cell, "y", 0) }));
					}
					entries.push_back(std::move(entry));
				}
			}

			const bool truncated = static_cast<int>(entries.size()) > kMaxContextActors;
			if (truncated)
				entries.resize(kMaxContextActors);

			// Which actor does the request talk about ?
			const std::string lower_prompt = Lower(prompt);
			const std::vector<std::string> words = RequestWords(prompt);
			const std::vector<std::string> all_words = RequestWords(prompt, false);
			const bool mentions_selection = Contains(lower_prompt, "sélection") || Contains(lower_prompt, "selection") ||
			                                Contains(lower_prompt, "selected") || Contains(lower_prompt, "celui-ci") ||
			                                Contains(lower_prompt, "celui-là");
			for (ActorEntry& entry : entries)
			{
				const std::string id = Lower(entry.id);
				const std::string cls = Lower(entry.cls);
				for (const std::string& word : words)
				{
					if (Contains(id, word) || Contains(cls, word)) entry.score += 4;     // named directly
				}
				for (const auto& [request_words, name_parts] : Synonyms())
				{
					// Whole words only ("son" must not match "personnage").
					bool asked = false;
					for (const std::string& w : request_words)
						for (const std::string& word : all_words)
							asked = asked || word == w || (w.size() >= 5 && word.rfind(w, 0) == 0);
					if (!asked)
						continue;
					for (const std::string& part : name_parts)
					{
						if (Contains(id, part) || Contains(cls, part))
						{
							entry.score += 3;
							break;
						}
					}
				}
				if (!selected.empty() && entry.id == selected)
					entry.score += mentions_selection ? 10 : 1;
			}

			out += "\nACTORS (" + std::to_string(entries.size()) + (truncated ? "+, list truncated" : "") +
			       ") : id | class | world location | voxel cell\n";
			if (entries.empty())
				out += "  (no actor in the level)\n";
			for (const ActorEntry& entry : entries)
			{
				out += "  " + entry.id + " | " + entry.cls + " | " + Vec(entry.location) + " | " + entry.cell +
				       (entry.id == selected ? "  <- SELECTED" : "") + "\n";
			}

			std::vector<const ActorEntry*> ranked;
			for (const ActorEntry& entry : entries)
				if (entry.score > 0)
					ranked.push_back(&entry);
			std::stable_sort(ranked.begin(), ranked.end(),
			                 [](const ActorEntry* a, const ActorEntry* b) { return a->score > b->score; });
			if (ranked.size() > 5)
				ranked.resize(5);

			out += "\nTARGET RESOLUTION for the current request:\n";
			if (ranked.empty())
			{
				out += "- No actor id/class matches the words of the request.";
				out += selected.empty() ? " Nothing is selected.\n" : " The selected actor is " + selected + ".\n";
				out += "- If the request is about an actor and the target is not obvious, DO NOT write code : "
				       "ask in French which actor (quote a few ids from the list).\n";
			}
			else
			{
				out += "- Likely targets, best first : ";
				for (size_t i = 0; i < ranked.size(); ++i)
					out += (i ? ", " : "") + ranked[i]->id + " (" + ranked[i]->cls + ", score " + std::to_string(ranked[i]->score) + ")";
				out += "\n";
				const bool clear = ranked.size() == 1 || ranked[0]->score > ranked[1]->score;
				out += clear
					? "- Use " + ranked[0]->id + " unless the user clearly means another actor.\n"
					: "- Several actors match equally : apply to all of them only if the user said \"tous\"/\"les\" (plural), else ask which one.\n";
			}

			// --- Details (properties, functions) : selected actor + best targets -
			// The model needs the exact property names ("hp", "open"...) to
			// change them : they are given for the actors the request is about.
			const auto append_details = [&](const std::string& id, const char* title)
			{
				Json details;
				if (!RunNow("actor.get", Json{ {"id", id} }, details) || !details.is_object())
					return;

				out += std::string("\n") + title + " (" + id + ", class " + Field(details, "class", std::string()) + "):\n";
				// Local copies : `details.value(...).items()` would iterate a
				// destroyed temporary (crash "cannot use value() with null").
				const Json properties = Field(details, "properties", Json::object());
				const Json function_list = Field(details, "functions", Json::array());

				int count = 0;
				for (const auto& [name, property] : properties.items())
				{
					if (++count > 40) { out += "  ...\n"; break; }
					const std::string type = property.is_object() ? Field(property, "type", std::string()) : std::string("?");
					const std::string value = property.is_object() && property.contains("value") ? property["value"].dump() : "?";
					out += "  " + name + " (" + type + ") = " + Truncate(value, 80) + "\n";
				}
				std::string functions;
				for (const Json& function : function_list)
					if (function.is_object())
						functions += (functions.empty() ? "" : ", ") + Field(function, "name", std::string()) +
						             "/" + std::to_string(Field(function, "arity", 0));
				if (!functions.empty())
					out += "  functions (actor.call): " + functions + "\n";
			};

			if (!selected.empty())
				append_details(selected, "SELECTED ACTOR DETAILS");

			int detailed = 0;
			for (const ActorEntry* entry : ranked)
			{
				if (entry->id == selected)
					continue;
				if (++detailed > 2)
					break;
				append_details(entry->id, "TARGET DETAILS (property names for actor.set_property)");
			}

			// --- Classes and voxel types --------------------------------------
			Json classes;
			if (RunNow("level.list_classes", Json::object(), classes) && classes.is_array())
			{
				std::string list;
				for (const Json& name : classes)
					if (name.is_string())
						list += (list.empty() ? "" : ", ") + name.get<std::string>();
				out += "\nSPAWNABLE CLASSES (actor.spawn): " + (list.empty() ? std::string("none") : list) + "\n";
			}

			Json types;
			if (RunNow("voxel.types", Json::object(), types) && types.is_array())
			{
				out += "VOXEL TYPES (voxel.set / voxel.fill 'type', 0 = empty):";
				for (const Json& type : types)
					out += " " + std::to_string(Field(type, "type", 0)) + "=" + Field(type, "name", std::string()) +
					       (Field(type, "indestructible", false) ? "(indestructible)" : "");
				out += "\n";
			}

			return out;
		}

		// Never throws : a bad value somewhere must not close the editor.
		std::string SafeSceneContext(const std::string& prompt)
		{
			try
			{
				return BuildSceneContext(prompt);
			}
			catch (const std::exception& error)
			{
				std::cerr << "[Lynxie] scene context error : " << error.what() << "\n";
				return std::string("=== LIVE SCENE STATE ===\n(unavailable : ") + error.what() +
				       ")\nAsk the user which actor they mean before writing code.\n";
			}
		}

		// One block per command, much shorter than the JSON dump.
		std::string CompactCommandReference()
		{
			std::string out;
			for (const commands::CommandInfo* command : commands::List())
			{
				if (command->name.rfind("ai.", 0) == 0)
					continue;   // tools for the evaluation, not for the model
				out += "- " + command->name + " : " + command->description + "\n";
				for (const commands::ParamInfo& param : command->params)
					out += "    " + param.name + " (" + param.type + (param.required ? ", required" : "") + ") " +
					       param.description + "\n";
			}
			return out;
		}

		std::string WorkedExample(const CallSyntax& syntax)
		{
			std::string out =
				"=== WORKED EXAMPLE (illustration only : the ids and numbers of a real answer come from the LIVE SCENE STATE) ===\n"
				"Scene: actor Hero_1 (class VoxelCharacter) at [4, 2, 0], 1 cell = 1 world unit.\n"
				"User: déplace le personnage de 3 cases vers la droite\n"
				"Thinking: target = Hero_1 (best match). Right = +x. 3 cells = [3, 0, 0]. "
				"Command = actor.set_transform with relative=true.\n"
				"Assistant:\n"
				"Je déplace **Hero_1** de 3 cases vers la droite (+3 en x, nouvelle position [7, 2, 0]).\n";

			if (syntax.function.empty())
			{
				out += "(then one ```python block that checks Hero_1 exists, then runs the command actor.set_transform "
				       "{\"id\": \"Hero_1\", \"location\": [3, 0, 0], \"relative\": true} with the command call "
				       "function of the Python API reference, inside lynx.undo_group).\n";
				return out;
			}

			out +=
				"```python\n"
				"import lynx_editor as lynx\n"
				"\n"
				"TARGET = \"Hero_1\"\n"
				"OFFSET = [3, 0, 0]  # 3 cells to the right\n"
				"\n"
				"if TARGET not in [a.id for a in lynx.actors()]:\n"
				"    print(\"Acteur introuvable :\", TARGET)\n"
				"else:\n"
				"    with lynx.undo_group(\"Déplacer \" + TARGET):\n"
				"        result = " + FormatCall(syntax, "actor.set_transform",
					"{\"id\": TARGET, \"location\": OFFSET, \"relative\": True}",
					"id=TARGET, location=OFFSET, relative=True") + "\n"
				"    print(\"Nouvelle position :\", result)\n"
				"```\n";
			return out;
		}

		std::string ReadPythonReference()
		{
			std::string reference;
			std::ifstream file(script_runner::PythonFolder() / "AI_API_REFERENCE.md", std::ios::binary);
			if (file)
				reference.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			return reference;
		}

		// The whole system prompt for `prompt` (rules, live scene, example,
		// references). Also given to external tools by the "ai.context"
		// command : the evaluation measures exactly what Lynxie receives.
		std::string BuildSystemPrompt(const std::string& prompt)
		{
			const std::string python_reference = ReadPythonReference();
			const CallSyntax call_syntax = DetectCallSyntax(python_reference);

			return std::string(
				"You are Lynxie, the assistant of the Lynx 2D voxel game editor. The user speaks French. "
				"You turn a request into ONE short Python script that drives the open editor.\n\n"
				"RULES:\n"
				"1. The LIVE SCENE STATE below is the current level : take actor ids, positions, classes and voxel types from it. "
				"Never invent an id ; copy it exactly.\n"
				"2. Find the target with TARGET RESOLUTION. 'le personnage', 'le joueur', 'le perso' = the best-ranked actor. "
				"If there is no clear target, ask a short question in French and write NO code.\n"
				"3. Convert directions and distances with COORDINATES (droite = +x, gauche = -x, haut = +y, bas = -y, "
				"default distance = 1 cell). To move an actor use actor.set_transform with relative=true and a location offset. "
				"To place or spawn at a voxel cell use the cell -> world conversion (or actor.spawn 'voxel').\n"
				"4. Use only the commands of the COMMAND REFERENCE and the functions of the PYTHON API REFERENCE. "
				"Property names come from the DETAILS blocks (or actor.get) : never guess them. "
				"Start with `import lynx_editor as lynx`, wrap changes in `with lynx.undo_group(...)`, check the target exists, print the result.\n"
				"5. Answer format : one or two French sentences saying what will change (actor, old -> new position), "
				"then exactly one ```python block. Comments with #. No JavaScript, no pseudocode.\n"
				"6. A question about the scene (\"où est...\", \"combien...\") is answered directly from the LIVE SCENE STATE, without code.\n"
				"7. If a script fails, you receive its output : fix the cause and send the COMPLETE corrected script again.\n\n") +
				SafeSceneContext(prompt) + "\n" +
				WorkedExample(call_syntax) + "\n" +
				"=== PYTHON API REFERENCE (lynx_editor module) ===\n" +
				(python_reference.empty()
					? std::string("(AI_API_REFERENCE.md not found : only use lynx.info(), lynx.actors() [a.id, a.cls, a.location] and lynx.undo_group(name).)\n")
					: python_reference) +
				"\n\n=== COMMAND REFERENCE ===\n" + CompactCommandReference();
		}

		// Message sent to the model when its script failed (auto-fix, evaluation).
		std::string BuildFixMessage(int exit_code, const std::string& output)
		{
			return
				"The script failed (exit code " + std::to_string(exit_code) + "). Its changes were undone : "
				"the LIVE SCENE STATE is up to date.\n"
				"Output (last lines):\n```\n" + output + "\n```\n"
				"Find the cause (wrong command, parameter, id, property name, Python error...) and answer with "
				"ONE complete corrected ```python script (not a diff), with one French sentence before it. "
				"If the request cannot be done with the documented API, say so in French and write no code.";
		}

		// Ollama context size for a prompt of `characters` characters.
		int NumCtxFor(size_t characters, int& estimated_tokens)
		{
			estimated_tokens = static_cast<int>(characters / 3) + 64;
			int num_ctx = 8192;
			while (num_ctx < estimated_tokens + 2048 && num_ctx < 32768)
				num_ctx *= 2;
			return num_ctx;
		}

		// `display` : text shown in the chat instead of `prompt` (auto-fix).
		// `context_request` : request used to build the scene context (target
		// resolution...) : the user's request, not the text of an error.
		void StartChatRequest(const std::string& model, const std::string& prompt, const std::string& display = {},
		                      const std::string& context_request = {})
		{
			if (g_local_ai.busy || model.empty() || prompt.empty())
				return;
			if (g_local_ai.worker.joinable())
				g_local_ai.worker.join();

			g_local_ai.model = model;
			if (context_request.empty())
				g_local_ai.last_request = prompt;
			g_local_ai.messages.push_back({ {"role", "user"}, {"content", prompt} });
			g_local_ai.conversation.emplace_back("You", display.empty() ? prompt : display);

			const std::string system = BuildSystemPrompt(context_request.empty() ? prompt : context_request);

			// Keep the end of the conversation only (the scene state is always fresh).
			std::vector<Json> history = g_local_ai.messages;
			if (history.size() > kMaxHistoryMessages)
				history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(kMaxHistoryMessages));

			Json request_messages = Json::array({ Json{{"role", "system"}, {"content", system}} });
			size_t characters = system.size();
			for (const Json& message : history)
			{
				request_messages.push_back(message);
				characters += Field(message, "content", std::string()).size();
			}

			// Ollama cuts the prompt at num_ctx tokens (2048 / 4096 by default) :
			// without this the model never sees most of the context.
			int estimated_tokens = 0;
			const int num_ctx = NumCtxFor(characters, estimated_tokens);

			g_local_ai.last_context = system;
			g_local_ai.last_context_tokens = estimated_tokens;
			g_local_ai.last_num_ctx = num_ctx;

			const Json request_body = {
				{"model", model},
				{"stream", false},
				{"messages", request_messages},
				{"options", { {"num_ctx", num_ctx}, {"temperature", 0.2} }},
			};
			g_local_ai.busy = true;
			g_local_ai.done = false;
			g_local_ai.worker = std::thread([request_body]()
			{
				std::string answer;
				std::string status;
#ifdef _WIN32
				std::string response;
				// replace : invalid UTF-8 (a Latin-1 actor name...) must not throw.
				const std::string body = request_body.dump(-1, ' ', false, Json::error_handler_t::replace);
				if (OllamaRequest(L"POST", L"/api/chat", body, response, status))
				{
					try
					{
						const Json root = Json::parse(response);
						const Json& content = root.at("message").at("content");
						answer = content.is_string() ? content.get<std::string>() : std::string();
						if (answer.empty())
							throw std::runtime_error("empty answer (the model returned no text)");
						status = "Lynxie answered. Review the generated script before running it.";
					}
					catch (const std::exception& e) { status = std::string("Invalid Ollama response: ") + e.what(); }
				}
#else
				status = "Local Ollama integration is currently available on Windows only.";
#endif
				std::lock_guard<std::mutex> lock(g_local_ai.mutex);
				g_local_ai.status = std::move(status);
				g_local_ai.answer_has_code = !answer.empty() && !ExtractPython(answer).empty();
				if (!answer.empty())
				{
					// The chat shows the explanation only ; the code goes to the
					// generated script editor below (the history keeps everything).
					g_local_ai.conversation.emplace_back("Lynxie", StripCodeBlocks(answer));
					g_local_ai.messages.push_back({ {"role", "assistant"}, {"content", answer} });
					// A plain answer (question, explanation) keeps the previous script.
					if (std::string code = ExtractPython(answer); !code.empty())
						g_local_ai.generated_code = std::move(code);
				}
				else if (!g_local_ai.messages.empty() && Field(g_local_ai.messages.back(), "role", std::string()) == "user")
				{
					g_local_ai.messages.pop_back();
					if (!g_local_ai.conversation.empty()) g_local_ai.conversation.pop_back();
				}
				g_local_ai.is_model_result = false;
				g_local_ai.done = true;
			});
		}

		// ---------------------------------------------------------------------
		// Auto-fix : run -> error -> undo -> the model corrects -> run again
		// ---------------------------------------------------------------------

		struct AiRunState
		{
			bool auto_fix = true;
			int max_attempts = 3;

			bool script_running = false;   // ai_output.py launched by Lynxie
			bool waiting_fix = false;      // a fix was asked : run the answer
			int attempt = 0;               // current attempt (1 = first run)
			long long undo_before = -1;    // editor undo steps before the run
		};

		AiRunState g_ai_run;

		long long UndoSteps()
		{
			Json info;
			return RunNow("editor.info", Json::object(), info) ? Field(info, "undo_steps", -1LL) : -1LL;
		}

		// Undoes what was done since `before` (a failed script : no half change).
		long long UndoSince(long long before)
		{
			const long long now = UndoSteps();
			if (before < 0 || now <= before)
				return 0;
			Json result;
			RunNow("editor.undo", Json{ {"count", now - before} }, result);
			return now - before;
		}

		// End of the output of the last script (without the runner's lines).
		std::string LastScriptOutput(size_t max_lines = 60, size_t max_chars = 6000)
		{
			std::vector<std::string> lines;
			script_runner::ForEachLine([&](const std::string& line)
			{
				if (line.rfind("> ", 0) == 0 || line.rfind("--- ", 0) == 0)
					return;
				lines.push_back(line);
			});
			if (lines.size() > max_lines)
				lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(max_lines));
			std::string out;
			for (const std::string& line : lines)
				out += line + "\n";
			if (out.size() > max_chars)
				out = "...\n" + out.substr(out.size() - max_chars);
			return out.empty() ? std::string("(no output)") : out;
		}

		void ChatNote(const std::string& text)
		{
			std::lock_guard<std::mutex> lock(g_local_ai.mutex);
			g_local_ai.conversation.emplace_back("Lynx", text);
		}

		bool RunAiScript(const std::string& code, int attempt)
		{
			std::error_code ec;
			const fs::path folder = script_runner::ScriptsFolder();
			fs::create_directories(folder, ec);
			const fs::path output = folder / "ai_output.py";

			{
				std::ofstream file(output, std::ios::binary | std::ios::trunc);
				if (!file)
				{
					g_local_ai.status = "Could not write commands/ai_output.py.";
					return false;
				}
				file.write(code.data(), static_cast<std::streamsize>(code.size()));
			}

			g_ai_run.undo_before = UndoSteps();
			g_ai_run.attempt = attempt;

			if (!script_runner::Run(output, {}))
			{
				g_local_ai.status = "Could not start the Python script. Check the Scripts tab.";
				return false;
			}

			g_ai_run.script_running = true;
			g_local_ai.status = "Running commands/ai_output.py (attempt " + std::to_string(attempt) + ")...";
			return true;
		}

		// A run failed (`reason` : script output, or why it was not run).
		void HandleAttemptFailure(int exit_code, const std::string& reason)
		{
			const int attempt = g_ai_run.attempt;
			const bool retry = g_ai_run.auto_fix && attempt < g_ai_run.max_attempts && !g_local_ai.model.empty();

			if (!retry)
			{
				std::string tail = reason;
				if (tail.size() > 600)
					tail = "..." + tail.substr(tail.size() - 600);
				ChatNote("[Erreur] Le script a échoué" +
				         std::string(attempt > 1 ? " après " + std::to_string(attempt) + " essais" : "") +
				         " ; ses changements ont été annulés.\n" + tail);
				g_local_ai.status = "The script failed. See Commands > Scripts for the full output.";
				return;
			}

			ChatNote("[Erreur] Le script a échoué (essai " + std::to_string(attempt) + "/" +
			         std::to_string(g_ai_run.max_attempts) + "), changements annulés. Lynxie corrige...");

			g_ai_run.waiting_fix = true;
			g_ai_run.attempt = attempt + 1;
			StartChatRequest(g_local_ai.model, BuildFixMessage(exit_code, reason),
			                 "(sortie d'erreur du script envoyée à Lynxie)",
			                 g_local_ai.last_request.empty() ? std::string("(script fix)") : g_local_ai.last_request);

			if (!g_local_ai.busy)   // could not start the request
				g_ai_run.waiting_fix = false;
		}

		// script_runner said ai_output.py ended.
		void OnAiScriptFinished(int exit_code)
		{
			g_ai_run.script_running = false;

			if (exit_code == 0)
			{
				ChatNote("[OK] Script exécuté sans erreur" +
				         std::string(g_ai_run.attempt > 1 ? " (essai " + std::to_string(g_ai_run.attempt) + ")." : "."));
				g_local_ai.status = "Script done. Ctrl+Z undoes it.";
				return;
			}

			const std::string output = LastScriptOutput();
			UndoSince(g_ai_run.undo_before);
			HandleAttemptFailure(exit_code, output);
		}

		// The model answered a fix request : run its new script.
		void OnFixAnswer()
		{
			g_ai_run.waiting_fix = false;

			if (!g_local_ai.answer_has_code)
			{
				ChatNote("Lynxie n'a pas proposé de script corrigé.");
				return;
			}

			if (!LooksLikePython(g_local_ai.generated_code))
			{
				HandleAttemptFailure(1, "The answer is not Python (JavaScript syntax : // comments or let/const/var/function).");
				return;
			}

			RunAiScript(g_local_ai.generated_code, g_ai_run.attempt);
		}

		// Commands used by tools/lynxie_eval (same prompt as the chat).
		void RegisterAiCommands()
		{
			commands::Register("ai.context",
				"Lynxie : the system prompt built for a request (rules, live scene state, references), "
				"and the Ollama options. Used by the evaluation tools.",
				{ { "prompt", "string", "The user request.", true } },
				[](const Json& params, const commands::CommandContext&) -> Json
				{
					const std::string system = BuildSystemPrompt(commands::GetString(params, "prompt"));
					int estimated = 0;
					const int num_ctx = NumCtxFor(system.size() + 4000, estimated);
					return { {"system", system}, {"estimated_tokens", estimated}, {"num_ctx", num_ctx},
					         {"temperature", 0.2}, {"max_attempts", g_ai_run.max_attempts} };
				});

			commands::Register("ai.fix_message",
				"Lynxie : the message sent to the model when its script failed.",
				{
					{ "exit_code", "integer", "Exit code of the script.", false },
					{ "output", "string", "End of the script output.", true },
				},
				[](const Json& params, const commands::CommandContext&) -> Json
				{
					return { {"message", BuildFixMessage(commands::GetInt(params, "exit_code", 1),
					                                     commands::GetString(params, "output"))} };
				});
		}

		// ---------------------------------------------------------------------
		// Text logs
		// ---------------------------------------------------------------------

		struct LogLine
		{
			std::string text;
			int kind = 0;   // 0 normal, 1 command, 2 ok, 3 error
		};

		constexpr size_t kMaxLog = 4000;

		std::deque<LogLine> g_console;
		std::deque<LogLine> g_activity;
		bool g_console_scroll = false;

		void Push(std::deque<LogLine>& log, const std::string& text, int kind)
		{
			// Multi-line text : one entry per line.
			size_t start = 0;

			while (start <= text.size())
			{
				const size_t end = text.find('\n', start);
				log.push_back({ text.substr(start, end == std::string::npos ? std::string::npos : end - start), kind });

				if (end == std::string::npos)
					break;

				start = end + 1;
			}

			while (log.size() > kMaxLog)
				log.pop_front();
		}

		void DrawLog(const char* id, std::deque<LogLine>& log, bool& scroll)
		{
			ImGui::BeginChild(id, ImVec2(0.f, 0.f), true, ImGuiWindowFlags_HorizontalScrollbar);

			const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(log.size()));

			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
				{
					const LogLine& line = log[static_cast<size_t>(i)];

					static const ImVec4 kColors[] = {
						ImVec4(0.85f, 0.85f, 0.85f, 1.f),
						ImVec4(0.55f, 0.75f, 1.00f, 1.f),
						ImVec4(0.55f, 0.85f, 0.55f, 1.f),
						ImVec4(0.95f, 0.45f, 0.45f, 1.f),
					};

					ImGui::PushStyleColor(ImGuiCol_Text, kColors[std::clamp(line.kind, 0, 3)]);
					ImGui::TextUnformatted(line.text.c_str());
					ImGui::PopStyleColor();
				}
			}

			if (scroll || at_bottom)
			{
				ImGui::SetScrollHereY(1.f);
				scroll = false;
			}

			ImGui::EndChild();
		}

		// Results can be huge (screenshots, file contents) : shortened.
		std::string Shorten(const Json& value)
		{
			Json copy = value;

			if (copy.is_object() && copy.contains("data") && copy["data"].is_string() &&
				copy["data"].get<std::string>().size() > 200)
			{
				copy["data"] = "<" + std::to_string(copy["data"].get<std::string>().size()) + " base64 characters>";
			}

			std::string text = copy.dump(2, ' ', false, Json::error_handler_t::replace);

			if (text.size() > 20000)
				text = text.substr(0, 20000) + "\n... (" + std::to_string(text.size()) + " characters)";

			return text;
		}


		// ---------------------------------------------------------------------
		// Console
		// ---------------------------------------------------------------------

		char g_input[4096] = {};
		std::vector<std::string> g_history;
		int g_history_pos = -1;

		// "name {json}" or "name key=value key=value" (values : JSON, or text).
		bool ParseCommandLine(const std::string& line, std::string& name, Json& params, std::string& error)
		{
			size_t i = line.find_first_not_of(" \t");

			if (i == std::string::npos)
				return false;

			const size_t name_end = line.find_first_of(" \t", i);
			name = line.substr(i, name_end == std::string::npos ? std::string::npos : name_end - i);
			params = Json::object();

			if (name_end == std::string::npos)
				return true;

			const std::string rest = line.substr(name_end);
			const size_t first = rest.find_first_not_of(" \t");

			if (first == std::string::npos)
				return true;

			if (rest[first] == '{')
			{
				try
				{
					params = Json::parse(rest.substr(first));
					return true;
				}
				catch (const Json::exception& e)
				{
					error = std::string("invalid JSON : ") + e.what();
					return false;
				}
			}

			// key=value pairs ; a value may be JSON with spaces inside [] {} "".
			size_t p = first;

			while (p < rest.size())
			{
				while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t'))
					++p;

				if (p >= rest.size())
					break;

				const size_t equal = rest.find('=', p);

				if (equal == std::string::npos)
				{
					error = "expected key=value near \"" + rest.substr(p) + "\"";
					return false;
				}

				const std::string key = rest.substr(p, equal - p);
				size_t q = equal + 1;
				int depth = 0;
				bool in_string = false;

				while (q < rest.size())
				{
					const char c = rest[q];

					if (in_string)
					{
						if (c == '\\')
							++q;
						else if (c == '"')
							in_string = false;
					}
					else if (c == '"')
						in_string = true;
					else if (c == '[' || c == '{')
						++depth;
					else if (c == ']' || c == '}')
						--depth;
					else if ((c == ' ' || c == '\t') && depth <= 0)
						break;

					++q;
				}

				const std::string text = rest.substr(equal + 1, q - equal - 1);

				try
				{
					params[key] = Json::parse(text);
				}
				catch (...)
				{
					params[key] = text;   // plain word
				}

				p = q;
			}

			return true;
		}

		void RunConsoleLine(const std::string& line)
		{
			std::string name;
			Json params;
			std::string error;

			Push(g_console, "> " + line, 1);
			g_console_scroll = true;

			if (!ParseCommandLine(line, name, params, error))
			{
				if (!error.empty())
					Push(g_console, error, 3);

				return;
			}

			commands::CommandContext context;
			context.source = "console";

			commands::Execute(name, params, context, [](bool ok, Json value)
			{
				if (ok)
					Push(g_console, Shorten(value), 2);
				else
					Push(g_console, value.is_string() ? value.get<std::string>() : value.dump(), 3);

				g_console_scroll = true;
			});
		}

		int ConsoleCallback(ImGuiInputTextCallbackData* data)
		{
			if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || g_history.empty())
				return 0;

			if (data->EventKey == ImGuiKey_UpArrow)
				g_history_pos = g_history_pos < 0 ? static_cast<int>(g_history.size()) - 1 : std::max(0, g_history_pos - 1);
			else if (data->EventKey == ImGuiKey_DownArrow && g_history_pos >= 0)
				g_history_pos = g_history_pos + 1 >= static_cast<int>(g_history.size()) ? -1 : g_history_pos + 1;

			data->DeleteChars(0, data->BufTextLen);

			if (g_history_pos >= 0)
				data->InsertChars(0, g_history[static_cast<size_t>(g_history_pos)].c_str());

			return 0;
		}

		void DrawConsole()
		{
			ImGui::TextDisabled("command {json}   or   command key=value ...      ex : actor.spawn class=Enemy location=[2,3,0]");

			const float input_height = ImGui::GetFrameHeightWithSpacing();
			ImGui::BeginChild("##ConsoleOut", ImVec2(0.f, -input_height));
			DrawLog("##ConsoleLog", g_console, g_console_scroll);
			ImGui::EndChild();

			ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Clear").x - ImGui::GetStyle().FramePadding.x * 2.f - ImGui::GetStyle().ItemSpacing.x);

			bool reclaim = false;

			if (ImGui::InputText("##ConsoleInput", g_input, sizeof(g_input),
			                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
			                     ConsoleCallback))
			{
				const std::string line = g_input;

				if (!line.empty())
				{
					if (g_history.empty() || g_history.back() != line)
						g_history.push_back(line);

					g_history_pos = -1;
					RunConsoleLine(line);
				}

				g_input[0] = '\0';
				reclaim = true;
			}

			if (reclaim)
				ImGui::SetKeyboardFocusHere(-1);

			ImGui::SameLine();

			if (ImGui::Button("Clear"))
				g_console.clear();
		}


		// ---------------------------------------------------------------------
		// Scripts
		// ---------------------------------------------------------------------

		struct ScriptFile
		{
			fs::path path;
			std::string label;         // relative to commands/
			std::string description;   // first docstring line
		};

		std::vector<ScriptFile> g_scripts;
		Clock::time_point g_last_scan{};
		int g_selected = -1;
		char g_arguments[1024] = {};
		char g_python[512] = {};
		bool g_python_loaded = false;
		bool g_output_scroll = false;
		size_t g_output_lines = 0;

		std::string ReadDescription(const fs::path& file)
		{
			std::ifstream in(file);
			std::string line;

			for (int i = 0; i < 6 && std::getline(in, line); ++i)
			{
				const size_t start = line.find_first_not_of(" \t");

				if (start == std::string::npos || line[start] == '#')
					continue;

				for (const char* quote : { "\"\"\"", "'''" })
				{
					if (line.compare(start, 3, quote) == 0)
					{
						std::string text = line.substr(start + 3);
						const size_t end = text.find(quote);

						if (end != std::string::npos)
							text = text.substr(0, end);

						if (text.empty() && std::getline(in, line))
							text = line;

						const size_t first = text.find_first_not_of(" \t");
						return first == std::string::npos ? std::string() : text.substr(first);
					}
				}

				return {};
			}

			return {};
		}

		void ScanScripts(bool force)
		{
			const Clock::time_point now = Clock::now();

			if (!force && now - g_last_scan < std::chrono::seconds(2))
				return;

			g_last_scan = now;

			const fs::path folder = script_runner::ScriptsFolder();
			const fs::path selected = (g_selected >= 0 && g_selected < static_cast<int>(g_scripts.size()))
				? g_scripts[static_cast<size_t>(g_selected)].path : fs::path();

			std::vector<ScriptFile> scripts;
			std::error_code ec;

			fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);

			for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
			{
				if (it->path().filename() == "__pycache__")
				{
					it.disable_recursion_pending();
					continue;
				}

				if (!it->is_regular_file() || it->path().extension() != ".py")
					continue;

				ScriptFile file;
				file.path = it->path();
				file.label = fs::relative(it->path(), folder, ec).generic_string();
				file.description = ReadDescription(it->path());
				scripts.push_back(std::move(file));
			}

			std::sort(scripts.begin(), scripts.end(),
			          [](const ScriptFile& a, const ScriptFile& b) { return a.label < b.label; });

			g_scripts = std::move(scripts);
			g_selected = -1;

			for (size_t i = 0; i < g_scripts.size(); ++i)
			{
				if (g_scripts[i].path == selected)
					g_selected = static_cast<int>(i);
			}
		}

		void CreateNewScript()
		{
			const fs::path folder = script_runner::ScriptsFolder();
			std::error_code ec;
			fs::create_directories(folder, ec);

			fs::path file = folder / "new_command.py";

			for (int i = 2; fs::exists(file, ec); ++i)
				file = folder / ("new_command_" + std::to_string(i) + ".py");

			std::ofstream out(file);
			out <<
				"\"\"\"New command : describe it here (shown in the editor).\"\"\"\n"
				"\n"
				"import lynx_editor as lynx\n"
				"\n"
				"\n"
				"def main():\n"
				"    info = lynx.info()\n"
				"    print(\"Project :\", info[\"project\"], \"-\", info[\"actor_count\"], \"actors\")\n"
				"\n"
				"    # Every change of this block is ONE Ctrl+Z in the editor.\n"
				"    with lynx.undo_group(\"New command\"):\n"
				"        for actor in lynx.actors():\n"
				"            print(actor.id, actor.cls, actor.location)\n"
				"\n"
				"\n"
				"if __name__ == \"__main__\":\n"
				"    main()\n";

			ScanScripts(true);

			for (size_t i = 0; i < g_scripts.size(); ++i)
			{
				if (g_scripts[i].path == file)
					g_selected = static_cast<int>(i);
			}
		}

		void OpenPath(const fs::path& path)
		{
#ifdef _WIN32
			ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
			(void)path;
#endif
		}

		void DrawScripts()
		{
			if (!g_python_loaded)
			{
				g_python_loaded = true;
				std::snprintf(g_python, sizeof(g_python), "%s", script_runner::PythonExecutable().c_str());
			}

			ScanScripts(false);

			const bool running = script_runner::IsRunning();

			// --- Left : script list ------------------------------------------

			ImGui::BeginChild("##ScriptList", ImVec2(ImGui::GetContentRegionAvail().x * 0.32f, 0.f), true);

			if (ImGui::SmallButton("New"))
				CreateNewScript();

			ImGui::SameLine();

			if (ImGui::SmallButton("Refresh"))
				ScanScripts(true);

			ImGui::SameLine();

			if (ImGui::SmallButton("Folder"))
			{
				std::error_code ec;
				fs::create_directories(script_runner::ScriptsFolder(), ec);
				OpenPath(script_runner::ScriptsFolder());
			}

			ImGui::Separator();

			if (g_scripts.empty())
				ImGui::TextWrapped("No script. Put .py files in the \"commands\" folder of the project, or click New.");

			for (size_t i = 0; i < g_scripts.size(); ++i)
			{
				const ScriptFile& script = g_scripts[i];

				if (ImGui::Selectable(script.label.c_str(), g_selected == static_cast<int>(i),
				                      ImGuiSelectableFlags_AllowDoubleClick))
				{
					g_selected = static_cast<int>(i);

					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !running)
						script_runner::Run(script.path, g_arguments);
				}

				if (!script.description.empty() && ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", script.description.c_str());
			}

			ImGui::EndChild();
			ImGui::SameLine();

			// --- Right : run / output ----------------------------------------

			ImGui::BeginGroup();

			const ScriptFile* selected =
				(g_selected >= 0 && g_selected < static_cast<int>(g_scripts.size()))
					? &g_scripts[static_cast<size_t>(g_selected)] : nullptr;

			if (selected)
			{
				ImGui::TextUnformatted(selected->label.c_str());

				if (!selected->description.empty())
					ImGui::TextDisabled("%s", selected->description.c_str());
			}
			else
			{
				ImGui::TextDisabled("Select a script");
			}

			ImGui::SetNextItemWidth(260.f);
			ImGui::InputTextWithHint("##Args", "arguments", g_arguments, sizeof(g_arguments));
			ImGui::SameLine();

			ImGui::BeginDisabled(!selected || running);

			if (ImGui::Button("Run") && selected)
			{
				script_runner::Run(selected->path, g_arguments);
				g_output_scroll = true;
			}

			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!running);

			if (ImGui::Button("Stop"))
				script_runner::Stop();

			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!selected);

			if (ImGui::Button("Edit") && selected)
				OpenPath(selected->path);

			ImGui::EndDisabled();
			ImGui::SameLine();

			if (ImGui::Button("Clear"))
				script_runner::ClearOutput();

			if (running)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.f), "Running...");
			}

			// Output.
			ImGui::BeginChild("##ScriptOutput", ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing()), true,
			                  ImGuiWindowFlags_HorizontalScrollbar);

			const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;

			script_runner::ForEachLine([](const std::string& line)
			{
				const bool error = line.find("Error") != std::string::npos ||
				                   line.find("ERROR") != std::string::npos ||
				                   line.find("Traceback") != std::string::npos ||
				                   line.rfind("--- Failed", 0) == 0;

				if (error)
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.f));

				ImGui::TextUnformatted(line.c_str());

				if (error)
					ImGui::PopStyleColor();
			});

			const size_t count = script_runner::LineCount();

			if (g_output_scroll || (at_bottom && count != g_output_lines))
			{
				ImGui::SetScrollHereY(1.f);
				g_output_scroll = false;
			}

			g_output_lines = count;

			ImGui::EndChild();

			// Python executable.
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Python");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(260.f);

			if (ImGui::InputText("##Python", g_python, sizeof(g_python)))
			{
				script_runner::PythonExecutable() = g_python;
				script_runner::SaveSettings();
			}

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Python 3 executable : python, py, or a full path (C:/Python312/python.exe).");

			ImGui::EndGroup();
		}

		void DrawLocalAI()
		{
			std::lock_guard<std::mutex> ai_lock(g_local_ai.mutex);
			if (!g_local_ai.refresh_started)
				StartModelRefresh();

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Local model");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(280.f);
			const char* preview = g_local_ai.selected_model >= 0 &&
			                      g_local_ai.selected_model < static_cast<int>(g_local_ai.models.size())
				? g_local_ai.models[static_cast<size_t>(g_local_ai.selected_model)].c_str()
				: (g_local_ai.busy ? "Loading models..." : "No model selected");
			if (ImGui::BeginCombo("##LocalModel", preview))
			{
				for (size_t i = 0; i < g_local_ai.models.size(); ++i)
				{
					const bool selected = g_local_ai.selected_model == static_cast<int>(i);
					if (ImGui::Selectable(g_local_ai.models[i].c_str(), selected))
						g_local_ai.selected_model = static_cast<int>(i);
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(g_local_ai.busy);
			if (ImGui::Button("Refresh models"))
				StartModelRefresh();
			ImGui::EndDisabled();
			ImGui::TextDisabled("Ollama on this computer · models already installed locally");
			ImGui::TextWrapped("%s", g_local_ai.status.c_str());

			// What the model really received (scene state, rules, references).
			if (!g_local_ai.last_context.empty() &&
			    ImGui::CollapsingHeader("Context sent to the model"))
			{
				ImGui::TextDisabled("~%d tokens  ·  num_ctx %d  ·  rebuilt from the live scene at every message",
				                    g_local_ai.last_context_tokens, g_local_ai.last_num_ctx);
				if (g_local_ai.last_context.find("AI_API_REFERENCE.md not found") != std::string::npos)
					ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f),
					                   "python/AI_API_REFERENCE.md is missing : the model does not know the Python API.");
				ImGui::SameLine();
				if (ImGui::SmallButton("Copy##aicontext"))
					ImGui::SetClipboardText(g_local_ai.last_context.c_str());
				ImGui::InputTextMultiline("##AIContext", g_local_ai.last_context.data(), g_local_ai.last_context.size() + 1,
				                          ImVec2(-1.f, ImGui::GetTextLineHeight() * 12.f), ImGuiInputTextFlags_ReadOnly);
			}

			ImGui::BeginChild("##LocalAIConversation", ImVec2(0.f, ImGui::GetContentRegionAvail().y * 0.40f), true,
			                  ImGuiWindowFlags_HorizontalScrollbar);
			for (const auto& [speaker, text] : g_local_ai.conversation)
			{
				ImGui::PushStyleColor(ImGuiCol_Text,
					speaker == "You"  ? ImVec4(0.55f, 0.75f, 1.f, 1.f) :
					speaker == "Lynx" ? ImVec4(0.75f, 0.75f, 0.75f, 1.f) :
					                    ImVec4(0.60f, 0.90f, 0.68f, 1.f));
				ImGui::TextUnformatted(speaker.c_str());
				ImGui::PopStyleColor();
				ImGui::PushTextWrapPos(0.f);
				ImGui::TextUnformatted(text.c_str());
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
			}
			if (g_local_ai.conversation.empty())
				ImGui::TextDisabled("Ask Lynxie something about the scene, or what it should do.");
			ImGui::EndChild();

			ImGui::TextUnformatted("Message");
			ImGui::BeginDisabled(g_local_ai.busy || g_local_ai.selected_model < 0);
			const float send_width = ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2.f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - send_width - ImGui::GetStyle().ItemSpacing.x);
			const bool enter = ImGui::InputTextWithHint("##LocalAIMessage", "Describe what the script should do...",
			                                          g_ai_prompt, sizeof(g_ai_prompt),
			                                          ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if (ImGui::Button("Send") || enter)
			{
				const std::string prompt = g_ai_prompt;
				const std::string model = g_local_ai.models[static_cast<size_t>(g_local_ai.selected_model)];
				if (!prompt.empty())
				{
					g_ai_prompt[0] = '\0';
					StartChatRequest(model, prompt);
				}
			}
			ImGui::EndDisabled();
			if (g_local_ai.busy)
				ImGui::TextDisabled("Lynxie is thinking...");

			ImGui::Separator();
			ImGui::TextUnformatted("Generated script : commands/ai_output.py");
			ImGui::TextDisabled("Review or edit the Python code before running it.");
			if (!g_local_ai.generated_code.empty())
			{
				if (!g_ai_code_loaded)
				{
					const size_t count = std::min(g_local_ai.generated_code.size(), g_ai_code_buffer.size() - 1);
					std::memcpy(g_ai_code_buffer.data(), g_local_ai.generated_code.data(), count);
					g_ai_code_buffer[count] = '\0';
					g_ai_code_loaded = true;
				}
				if (ImGui::InputTextMultiline("##GeneratedPython", g_ai_code_buffer.data(), g_ai_code_buffer.size(),
				                              ImVec2(-1.f, -ImGui::GetFrameHeightWithSpacing() * 2.f),
				                              ImGuiInputTextFlags_AllowTabInput))
					g_local_ai.generated_code = g_ai_code_buffer.data();
				if (!LooksLikePython(g_local_ai.generated_code))
					ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.f),
					                   "This looks like JavaScript, not Python (for example // comments or let/const). Ask the model to regenerate it.");
			}
			else
			{
				ImGui::BeginChild("##NoGeneratedPython", ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing() * 2.f), true);
				ImGui::TextDisabled("The model's response will appear here. Ask it to generate a script in the chat above.");
				ImGui::EndChild();
			}

			const bool can_run = !g_local_ai.generated_code.empty() && LooksLikePython(g_local_ai.generated_code) &&
			                     !script_runner::IsRunning() && !g_local_ai.busy && !g_ai_run.waiting_fix;
			ImGui::BeginDisabled(!can_run);
			if (ImGui::Button("Save and run Python output"))
				RunAiScript(g_local_ai.generated_code, 1);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Checkbox("Auto-fix errors", &g_ai_run.auto_fix);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("If the script fails : its changes are undone, the error goes back to Lynxie, "
				                  "and the corrected script runs again.");
			if (g_ai_run.auto_fix)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90.f);
				ImGui::SliderInt("##AiAttempts", &g_ai_run.max_attempts, 2, 5, "%d tries");
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear chat"))
			{
				g_local_ai.messages.clear();
				g_local_ai.conversation.clear();
				g_local_ai.generated_code.clear();
				g_ai_code_loaded = false;
			}
		}


		// ---------------------------------------------------------------------
		// Reference
		// ---------------------------------------------------------------------

		char g_filter[128] = {};

		void DrawReference()
		{
			ImGui::SetNextItemWidth(-1.f);
			ImGui::InputTextWithHint("##RefFilter", "Filter (actor, voxel, asset...)", g_filter, sizeof(g_filter));

			ImGui::BeginChild("##Reference", ImVec2(0.f, 0.f), true);

			const std::string filter = g_filter;

			for (const commands::CommandInfo* command : commands::List())
			{
				if (!filter.empty() && command->name.find(filter) == std::string::npos &&
					command->description.find(filter) == std::string::npos)
					continue;

				if (!ImGui::TreeNode(command->name.c_str()))
				{
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", command->description.c_str());

					continue;
				}

				ImGui::PushTextWrapPos(0.f);
				ImGui::TextUnformatted(command->description.c_str());
				ImGui::PopTextWrapPos();

				for (const commands::ParamInfo& param : command->params)
				{
					ImGui::Bullet();
					ImGui::TextColored(param.required ? ImVec4(1.f, 0.85f, 0.4f, 1.f) : ImVec4(0.7f, 0.8f, 1.f, 1.f),
					                   "%s", param.name.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("(%s%s)", param.type.c_str(), param.required ? ", required" : "");
					ImGui::SameLine();
					ImGui::PushTextWrapPos(0.f);
					ImGui::TextUnformatted(param.description.c_str());
					ImGui::PopTextWrapPos();
				}

				if (ImGui::SmallButton("Try in console"))
				{
					std::snprintf(g_input, sizeof(g_input), "%s ", command->name.c_str());
				}

				ImGui::TreePop();
			}

			ImGui::EndChild();
		}


		// ---------------------------------------------------------------------
		// AI / MCP
		// ---------------------------------------------------------------------

		bool g_activity_scroll = false;

		std::string JsonPath(const fs::path& path)
		{
			return Json(path.generic_string()).dump();
		}

		void DrawAi()
		{
			if (command_server::IsRunning())
			{
				ImGui::Text("Command server : 127.0.0.1:%d   -   %d client(s) connected",
				            command_server::Port(), command_server::ClientCount());
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.f), "Command server not running (see the console).");
			}

			ImGui::TextDisabled("Connection file : %s", command_server::ConnectionFile().generic_string().c_str());

			ImGui::Spacing();
			ImGui::TextWrapped(
				"An AI model acts on the editor through the MCP server python/lynx_mcp.py (every command "
				"becomes a tool). Add it to your MCP client (Claude Desktop, Claude Code...) :");

			const fs::path mcp = script_runner::PythonFolder() / "lynx_mcp.py";
			const std::string python = script_runner::PythonExecutable();

			const std::string desktop =
				"{\n"
				"  \"mcpServers\": {\n"
				"    \"lynx\": {\n"
				"      \"command\": " + Json(python).dump() + ",\n"
				"      \"args\": [" + JsonPath(mcp) + "]\n"
				"    }\n"
				"  }\n"
				"}";

			const std::string code =
				"claude mcp add lynx -- " + python + " \"" + mcp.generic_string() + "\"";

			ImGui::Spacing();
			ImGui::TextUnformatted("Claude Desktop (claude_desktop_config.json) :");
			ImGui::InputTextMultiline("##McpDesktop", const_cast<char*>(desktop.c_str()), desktop.size() + 1,
			                          ImVec2(-1.f, ImGui::GetTextLineHeight() * 8.5f), ImGuiInputTextFlags_ReadOnly);

			if (ImGui::SmallButton("Copy##desktop"))
				ImGui::SetClipboardText(desktop.c_str());

			ImGui::Spacing();
			ImGui::TextUnformatted("Claude Code :");
			ImGui::InputText("##McpCode", const_cast<char*>(code.c_str()), code.size() + 1, ImGuiInputTextFlags_ReadOnly);

			if (ImGui::SmallButton("Copy##code"))
				ImGui::SetClipboardText(code.c_str());

			ImGui::Spacing();
			ImGui::TextWrapped("Without --project, the MCP server connects to the editor started last. "
			                   "Add \"--project\", \"<project folder>\" to the args to pin a project.");

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::TextUnformatted("Activity");
			ImGui::SameLine();

			if (ImGui::SmallButton("Clear##activity"))
				g_activity.clear();

			DrawLog("##Activity", g_activity, g_activity_scroll);
		}
	}


	void Update()
	{
		static bool installed = false;

		if (!installed)
		{
			installed = true;

			commands::SetLogCallback([](const std::string& line)
			{
				Push(g_activity, line, line.find("error") != std::string::npos ? 3 : 0);
			});

			RegisterAiCommands();
		}

		int exit_code = 0;

		if (script_runner::PollFinished(exit_code))
		{
			g_output_scroll = true;

			if (g_ai_run.script_running)
				OnAiScriptFinished(exit_code);
		}

		bool completed = false;
		bool was_model_result = false;
		{
			std::lock_guard<std::mutex> lock(g_local_ai.mutex);
			if (g_local_ai.done)
			{
				completed = true;
				was_model_result = g_local_ai.is_model_result;
				g_local_ai.done = false;
			}
		}
		if (completed)
		{
			if (g_local_ai.worker.joinable())
				g_local_ai.worker.join();
			g_local_ai.busy = false;
			if (!was_model_result)
			{
				g_ai_code_loaded = false;

				if (g_ai_run.waiting_fix)
					OnFixAnswer();
			}
		}
	}

	void Shutdown()
	{
		if (g_local_ai.worker.joinable())
			g_local_ai.worker.join();
	}


	void Draw(bool* open)
	{
		if (open && !*open)
			return;

		ImGui::SetNextWindowSize(ImVec2(900.f, 480.f), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Commands", open))
		{
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("##CommandsTabs"))
		{
			if (ImGui::BeginTabItem("Scripts"))
			{
				DrawScripts();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Lynxie"))
			{
				DrawLocalAI();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Console"))
			{
				DrawConsole();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Reference"))
			{
				DrawReference();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("AI / MCP"))
			{
				DrawAi();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}
