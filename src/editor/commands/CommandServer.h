#pragma once

// =============================================================================
// Command server (editor)
// -----------------------------------------------------------------------------
// Local TCP server (127.0.0.1 only) : Python scripts and AI tools (MCP server,
// see python/README.md) send commands to the running editor.
//
// Protocol : one JSON object per line (UTF-8, '\n'), in both directions.
//
//   -> {"id": 1, "command": "auth", "params": {"token": "..."}}     (first)
//   <- {"id": 1, "ok": true, "result": {...}}
//   -> {"id": 2, "command": "actor.spawn", "params": {"class": "Enemy"}}
//   <- {"id": 2, "ok": true, "result": {"id": "Enemy_2"}}
//   <- {"id": 3, "ok": false, "error": "unknown actor 'Foo'"}
//
// The port and the token are written to <project>/.lynx/editor.json (and to
// the "last editor" file of the temp folder) while the editor runs ; scripts
// started from the editor also get them in LYNX_EDITOR_PORT / _TOKEN.
//
// Poll() is called once per frame, between two frames : the commands run on
// the main thread.
// =============================================================================

#include <filesystem>
#include <string>

namespace lynx::editor::command_server
{
	constexpr int kDefaultPort = 7420;

	// Listens on the first free port from `preferred_port` (20 tries). Writes
	// the connection file. false = no server (the editor works without it).
	bool Start(const std::filesystem::path& project_root, int preferred_port = kDefaultPort);

	// Accepts clients, reads their requests and runs them, at most for about
	// `budget_ms` (big batches continue at the next frame).
	void Poll(double budget_ms = 8.0);

	// Called by a command that changes the level at the next frame (Stop,
	// reload...) : the following requests wait for the next Poll().
	void YieldFrame();

	// Closes everything and removes the connection file.
	void Stop();

	bool IsRunning();
	int Port();
	const std::string& Token();
	int ClientCount();

	// <project>/.lynx/editor.json
	std::filesystem::path ConnectionFile();

	// Connection file common to every project : the MCP server finds the
	// running editor with it when it is not given a project.
	std::filesystem::path LastEditorFile();
}
