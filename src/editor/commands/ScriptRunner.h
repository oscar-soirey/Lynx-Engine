#pragma once

// =============================================================================
// Python command scripts (editor)
// -----------------------------------------------------------------------------
// Runs a .py file of the project (folder <project>/commands/) with Python, in
// its own process. The script talks to the editor through the command server
// with the "lynx_editor" module (python/ folder next to the editor, added to
// PYTHONPATH). Environment given to the script :
//
//   LYNX_EDITOR_PORT, LYNX_EDITOR_TOKEN   connection to this editor
//   LYNX_PROJECT                          project root
//   PYTHONPATH                            <editor>/python ; previous value
//
// Working directory : the project root. Output (stdout + stderr) is shown in
// the Commands window.
// =============================================================================

#include <filesystem>
#include <functional>
#include <string>

namespace lynx::editor::script_runner
{
	// `python_dir` : folder that contains the lynx_editor package.
	void Init(const std::filesystem::path& project_root, const std::filesystem::path& python_dir);

	// Python executable ("python" by default ; "py", "C:/Python312/python.exe"...).
	// Saved in <project>/.lynx/commands.json.
	std::string& PythonExecutable();
	void SaveSettings();

	// Starts the script. false if one is already running or it cannot start
	// (the reason is in the output).
	bool Run(const std::filesystem::path& script, const std::string& arguments);

	// Kills the running script.
	void Stop();

	bool IsRunning();
	const std::filesystem::path& CurrentScript();

	// Once per frame. Returns true ONCE when a script ended (`exit_code`).
	bool PollFinished(int& exit_code);

	// Output of the last script (complete lines, then the unfinished one).
	void ForEachLine(const std::function<void(const std::string& line)>& fn);
	size_t LineCount();
	void ClearOutput();
	void AppendOutputLine(const std::string& line);

	// Editor exit.
	void Shutdown();

	// <project>/commands
	std::filesystem::path ScriptsFolder();
	const std::filesystem::path& PythonFolder();
}
