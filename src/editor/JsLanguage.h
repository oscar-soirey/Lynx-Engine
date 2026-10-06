#pragma once

// =============================================================================
// JavaScript language service (code editors)
// -----------------------------------------------------------------------------
// What a language server does, inside the editor (no external process) :
//
//   - diagnostics : syntax errors (QuickJS, the engine's parser), variables
//     that are not defined, constants that are assigned, declarations made
//     twice, members that don't exist on the engine objects (Level.fnd),
//     variables never used ;
//   - completion : variables / functions / classes visible at the cursor,
//     globals, members after "." (engine API, actors, components, classes of
//     the file and of the project), names in strings ("SoundSource" for
//     addComponent, actions of input.json, classes for Level.spawn...) ;
//   - hover : signature, type and documentation ;
//   - signature help : parameters of the call around the cursor ;
//   - go to definition (in the file, or the file of a project class).
//
// The globals and the members of the engine objects are read from the running
// scripting context (lynx::EvaluateScript) ; the documentation comes from
// JsApiDocs.inl. Positions : line (0 based) and byte index in the line.
// =============================================================================

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lynx::editor::js_language
{
	enum class Severity { Error, Warning, Info, Hint };

	struct Diagnostic
	{
		int line = 0;
		int start = 0;          // byte index
		int end_line = 0;
		int end = 0;
		Severity severity = Severity::Error;
		std::string message;
	};

	enum class Kind { Variable, Constant, Parameter, Function, Class, Method, Property, Keyword, Global, Namespace, Value };

	struct CompletionItem
	{
		std::string label;
		Kind kind = Kind::Variable;
		std::string detail;     // signature / type
		std::string doc;
		int score = 0;
	};

	struct Completion
	{
		int line = 0;
		int start = 0;          // the range replaced by the item (the word typed)
		int end = 0;
		bool in_string = false; // names in a string ("SoundSource")
		std::vector<CompletionItem> items;
	};

	struct Hover
	{
		bool valid = false;
		int line = 0, start = 0, end = 0;
		std::string signature;  // "let speed : number"
		std::string doc;
	};

	struct Signature
	{
		bool valid = false;
		std::string label;      // "spawn(className, options?)"
		std::vector<std::pair<int, int>> params;   // ranges of the parameters in the label
		int active = 0;
		std::string doc;
	};

	struct Location
	{
		bool valid = false;
		std::filesystem::path file;   // empty : this document
		int line = 0, start = 0, end = 0;
	};

	// -------------------------------------------------------------------------
	// Environment (shared by the documents)
	// -------------------------------------------------------------------------

	/** Project root (assets/ : classes of the other files, input.json actions, widgets...). */
	void SetProjectRoot(const std::filesystem::path& root);

	/**
	 * Reads the globals and the members of the engine objects from the scripting
	 * context (`evaluate` : lynx::EvaluateScript ; empty : built-in lists), and
	 * scans the project again. Cheap enough to be called every few seconds.
	 */
	void RefreshEnvironment(const std::function<bool(const std::string& code, std::string& json)>& evaluate);

	/** Syntax check (lynx::CheckScriptSyntax) : false + message "file:line:col message". */
	void SetSyntaxChecker(const std::function<bool(const std::string& code, const std::string& name, std::string& error)>& check);

	// -------------------------------------------------------------------------
	// A document (one per open .js file)
	// -------------------------------------------------------------------------

	class Document
	{
	public:
		Document();
		~Document();
		Document(const Document&) = delete;
		Document& operator=(const Document&) = delete;

		/** New text : analysis (fast) ; `check_syntax` : also the QuickJS check (slower, debounced by the host). */
		void Update(const std::vector<std::string>& lines, const std::filesystem::path& file, bool check_syntax = true);

		const std::vector<Diagnostic>& Diagnostics() const;
		int ErrorCount() const;
		int WarningCount() const;

		/** `explicit_request` : Ctrl+Space (completes even without a word typed). */
		Completion Complete(int line, int index, bool explicit_request) const;
		/** A completion list should open after this character was typed. */
		bool ShouldTrigger(int line, int index, char typed) const;

		Hover HoverAt(int line, int index) const;
		Signature SignatureAt(int line, int index) const;
		Location DefinitionAt(int line, int index) const;

		/** The word of the identifier at the position (start / end : its range). */
		bool IdentifierAt(int line, int index, int& start, int& end) const;

		struct Impl;

	private:
		std::unique_ptr<Impl> m;
	};
}
