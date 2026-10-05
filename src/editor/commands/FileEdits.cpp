#include "FileEdits.h"

#include "../../scripting/Scripting.h"

#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace lynx::editor::file_edits
{
	namespace
	{
		struct Backup
		{
			fs::path path;
			std::string relative;
			bool existed = false;
			std::string content;
		};

		std::vector<Backup> g_last_edit;

		std::string Trim(const std::string& text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return {};
			const size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		bool StartsWithRun(const std::string& line, char c, size_t count)
		{
			const std::string trimmed = Trim(line);
			return trimmed.size() >= count && std::all_of(trimmed.begin(), trimmed.begin() + count,
			                                              [c](char x) { return x == c; });
		}

		bool IsSearchLine(const std::string& line)
		{
			return StartsWithRun(line, '<', 5) && Trim(line).find("SEARCH") != std::string::npos;
		}

		bool IsSeparatorLine(const std::string& line)
		{
			const std::string trimmed = Trim(line);
			return trimmed.size() >= 5 && std::all_of(trimmed.begin(), trimmed.end(), [](char c) { return c == '='; });
		}

		bool IsReplaceEndLine(const std::string& line)
		{
			return StartsWithRun(line, '>', 5) && Trim(line).find("REPLACE") != std::string::npos;
		}

		// "FILE: path", "File : path", "**path**", "`path`", "path" (with an extension).
		std::string PathFromLine(const std::string& line)
		{
			std::string text = Trim(line);
			for (const char* prefix : { "FILE:", "File:", "file:", "FILE :", "File :", "Fichier :", "Fichier:" })
			{
				if (text.rfind(prefix, 0) == 0)
				{
					text = Trim(text.substr(std::char_traits<char>::length(prefix)));
					break;
				}
			}
			text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '`' || c == '*'; }), text.end());
			text = Trim(text);
			if (text.empty() || text.find(' ') != std::string::npos || text.find('.') == std::string::npos ||
			    text.find("```") != std::string::npos || text.size() > 200)
				return {};
			return text;
		}

		bool IsFenceLine(const std::string& line)
		{
			return Trim(line).rfind("```", 0) == 0;
		}

		std::vector<std::string> SplitLines(const std::string& text)
		{
			std::vector<std::string> lines;
			size_t start = 0;
			while (start <= text.size())
			{
				const size_t end = text.find('\n', start);
				std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(std::move(line));
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			return lines;
		}

		std::string ReadFile(const fs::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		bool WriteFile(const fs::path& path, const std::string& text)
		{
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(text.data(), static_cast<std::streamsize>(text.size()));
			return static_cast<bool>(file);
		}

		std::string RightTrim(std::string line)
		{
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
				line.pop_back();
			return line;
		}

		// Positions (in lines) where `search` matches `file`, comparing lines
		// without trailing spaces. Leading / trailing empty lines of the
		// search are ignored (models often add them).
		std::vector<size_t> FindLines(const std::vector<std::string>& file, std::vector<std::string> search)
		{
			while (!search.empty() && Trim(search.front()).empty())
				search.erase(search.begin());
			while (!search.empty() && Trim(search.back()).empty())
				search.pop_back();

			std::vector<size_t> matches;
			if (search.empty() || search.size() > file.size())
				return matches;

			for (size_t i = 0; i + search.size() <= file.size(); ++i)
			{
				bool same = true;
				for (size_t j = 0; j < search.size() && same; ++j)
					same = RightTrim(file[i + j]) == RightTrim(search[j]);
				if (same)
					matches.push_back(i);
			}
			return matches;
		}

		size_t SearchLineCount(std::vector<std::string> search)
		{
			while (!search.empty() && Trim(search.front()).empty())
				search.erase(search.begin());
			while (!search.empty() && Trim(search.back()).empty())
				search.pop_back();
			return search.size();
		}

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		bool IsAllowedFile(const std::string& relative)
		{
			static const char* const kExtensions[] = {
				".js", ".cpp", ".h", ".hpp", ".inl", ".c", ".py", ".json", ".xml", ".txt", ".md", ".cfg", ".ini", ".glsl",
			};
			const std::string extension = Lower(fs::path(relative).extension().string());
			for (const char* e : kExtensions)
				if (extension == e)
					return true;
			return fs::path(relative).filename() == "CMakeLists.txt";
		}

		// Project relative path -> absolute path inside the project, or empty.
		fs::path Resolve(const fs::path& root, std::string relative, std::string& error)
		{
			std::replace(relative.begin(), relative.end(), '\\', '/');
			while (relative.rfind("./", 0) == 0)
				relative.erase(0, 2);

			const fs::path rel = fs::path(relative).lexically_normal();
			if (relative.empty() || rel.is_absolute() || rel.has_root_name() || rel.empty() ||
			    rel.begin()->string() == "..")
			{
				error = "\"" + relative + "\" : use a path from the project root (ex : assets/classes/Enemy.js)";
				return {};
			}
			const std::string first = rel.begin()->string();
			if (first == "build" || first == ".lynx" || first == ".git")
			{
				error = "\"" + relative + "\" : files in " + first + "/ cannot be edited";
				return {};
			}
			if (!IsAllowedFile(relative))
			{
				error = "\"" + relative + "\" : only text / source files can be edited";
				return {};
			}
			return root / rel;
		}

		// .js : compiled ; .json : parsed. Comments are allowed in voxels.json.
		bool Validate(const std::string& relative, const std::string& text, std::string& error)
		{
			const std::string extension = Lower(fs::path(relative).extension().string());
			if (extension == ".js")
			{
				std::string message;
				if (!lynx::CheckScriptSyntax(text, relative.c_str(), message))
				{
					error = relative + " is not valid JavaScript : " + message;
					return false;
				}
			}
			else if (extension == ".json")
			{
				if (nlohmann::json::parse(text, nullptr, false, true).is_discarded())
				{
					error = relative + " would not be valid JSON";
					return false;
				}
			}
			return true;
		}
	}


	std::vector<Block> Parse(const std::string& answer)
	{
		std::vector<Block> blocks;
		const std::vector<std::string> lines = SplitLines(answer);

		std::string current_path;
		enum class State { Text, Search, Replace } state = State::Text;
		Block block;
		std::vector<std::string> search, replace;

		for (const std::string& line : lines)
		{
			switch (state)
			{
			case State::Text:
				if (IsSearchLine(line))
				{
					// "<<<<<<< SEARCH assets/x.js" : path on the same line
					const std::string trimmed = Trim(line);
					const std::string after = Trim(trimmed.substr(trimmed.find("SEARCH") + 6));
					const std::string inline_path = PathFromLine(after);
					block = Block{};
					block.path = inline_path.empty() ? current_path : inline_path;
					search.clear();
					replace.clear();
					state = State::Search;
				}
				else if (!IsFenceLine(line))
				{
					const std::string path = PathFromLine(line);
					if (!path.empty())
						current_path = path;
				}
				break;

			case State::Search:
				if (IsSeparatorLine(line))
					state = State::Replace;
				else
					search.push_back(line);
				break;

			case State::Replace:
				if (IsReplaceEndLine(line))
				{
					auto join = [](const std::vector<std::string>& v)
					{
						std::string out;
						for (size_t i = 0; i < v.size(); ++i)
							out += v[i] + (i + 1 < v.size() ? "\n" : "");
						return out;
					};
					block.search = join(search);
					block.replace = join(replace);
					blocks.push_back(block);
					current_path = block.path;   // several blocks for the same file
					state = State::Text;
				}
				else
				{
					replace.push_back(line);
				}
				break;
			}
		}
		return blocks;
	}


	std::string StripBlocks(const std::string& answer)
	{
		const std::vector<std::string> lines = SplitLines(answer);
		std::vector<std::string> out;
		std::vector<std::string> fence;   // lines of the current ``` block
		bool in_fence = false;
		bool fence_has_edit = false;
		std::string edits;                // "[edit] path" lines of the current fence

		std::string current_path;
		bool in_block = false;
		bool after_separator = false;

		auto emit = [&](const std::string& line) { (in_fence ? fence : out).push_back(line); };

		for (const std::string& line : lines)
		{
			if (!in_block && IsFenceLine(line))
			{
				if (!in_fence)
				{
					in_fence = true;
					fence_has_edit = false;
					fence.clear();
					fence.push_back(line);
				}
				else
				{
					fence.push_back(line);
					in_fence = false;
					if (!fence_has_edit)
						out.insert(out.end(), fence.begin(), fence.end());
				}
				continue;
			}

			if (!in_block && IsSearchLine(line))
			{
				in_block = true;
				after_separator = false;
				const std::string trimmed = Trim(line);
				const std::string inline_path = PathFromLine(Trim(trimmed.substr(trimmed.find("SEARCH") + 6)));
				if (!inline_path.empty())
					current_path = inline_path;
				// The "FILE: path" line just before : replaced by the edit line.
				std::vector<std::string>& target = in_fence ? fence : out;
				if (!target.empty() && !PathFromLine(target.back()).empty() && PathFromLine(target.back()) == current_path)
					target.pop_back();
				if (in_fence)
					fence_has_edit = true;
				// (a ``` block that contains an edit is dropped : the note goes to the output)
				out.push_back("[edit] " + (current_path.empty() ? std::string("(no file)") : current_path));
				continue;
			}

			if (in_block)
			{
				if (IsSeparatorLine(line))
					after_separator = true;
				else if (after_separator && IsReplaceEndLine(line))
					in_block = false;
				continue;
			}

			const std::string path = PathFromLine(line);
			if (!path.empty())
				current_path = path;
			emit(line);
		}
		if (in_fence && !fence_has_edit)
			out.insert(out.end(), fence.begin(), fence.end());

		std::string text;
		for (size_t i = 0; i < out.size(); ++i)
			text += out[i] + (i + 1 < out.size() ? "\n" : "");
		return Trim(text);
	}


	Result Apply(const std::vector<Block>& blocks, const fs::path& root)
	{
		Result result;
		if (blocks.empty())
		{
			result.message = "no edit block";
			return result;
		}

		// New content of every file, built in memory first : all or nothing.
		struct Pending
		{
			fs::path path;
			std::string relative;
			bool existed = false;
			std::string original;
			std::vector<std::string> lines;
			int added = 0;
			int removed = 0;
		};
		std::map<std::string, Pending> files;
		std::vector<std::string> order;
		std::string errors;

		auto fail = [&](const std::string& relative, const std::string& message)
		{
			errors += "- " + message + "\n";
			if (!relative.empty() &&
			    std::find(result.failed_files.begin(), result.failed_files.end(), relative) == result.failed_files.end())
				result.failed_files.push_back(relative);
		};

		for (const Block& block : blocks)
		{
			std::string error;
			const fs::path path = Resolve(root, block.path, error);
			if (path.empty())
			{
				fail({}, error.empty() ? "a block has no file path (write FILE: <path> before <<<<<<< SEARCH)" : error);
				continue;
			}
			const std::string relative = fs::path(block.path).lexically_normal().generic_string();

			if (!files.count(relative))
			{
				Pending pending;
				pending.path = path;
				pending.relative = relative;
				std::error_code ec;
				pending.existed = fs::is_regular_file(path, ec);
				if (pending.existed)
				{
					pending.original = ReadFile(path);
					pending.lines = SplitLines(pending.original);
				}
				files[relative] = std::move(pending);
				order.push_back(relative);
			}
			Pending& file = files[relative];

			const std::vector<std::string> search = SplitLines(block.search);
			const std::vector<std::string> replace = block.replace.empty() ? std::vector<std::string>{} : SplitLines(block.replace);

			if (SearchLineCount(search) == 0)
			{
				// Empty SEARCH : new file.
				if (file.existed || !file.lines.empty())
				{
					fail(relative, relative + " already exists : an empty SEARCH only creates new files. "
					               "SEARCH the lines to change instead");
					continue;
				}
				file.lines = replace;
				file.added += static_cast<int>(replace.size());
				continue;
			}

			if (!file.existed && file.lines.empty())
			{
				fail(relative, relative + " does not exist (to create it, leave SEARCH empty)");
				continue;
			}

			const std::vector<size_t> matches = FindLines(file.lines, search);
			if (matches.size() != 1)
			{
				fail(relative, matches.empty()
					? "SEARCH text not found in " + relative + " (copy the lines EXACTLY from the file, same spaces)"
					: "SEARCH text found " + std::to_string(matches.size()) + " times in " + relative +
					  " : add more surrounding lines so it is unique");
				continue;
			}

			// Leading empty lines of the search were skipped by FindLines.
			const size_t count = SearchLineCount(search);
			file.lines.erase(file.lines.begin() + static_cast<long>(matches[0]),
			                 file.lines.begin() + static_cast<long>(matches[0] + count));
			// Blank lines around the SEARCH were not part of the match : the same
			// number of blank lines around the REPLACE is dropped.
			std::vector<std::string> inserted = replace;
			for (size_t i = 0; i < search.size() && Trim(search[i]).empty(); ++i)
				if (!inserted.empty() && Trim(inserted.front()).empty())
					inserted.erase(inserted.begin());
			for (size_t i = search.size(); i > 0 && Trim(search[i - 1]).empty(); --i)
				if (!inserted.empty() && Trim(inserted.back()).empty())
					inserted.pop_back();
			file.lines.insert(file.lines.begin() + static_cast<long>(matches[0]), inserted.begin(), inserted.end());
			file.removed += static_cast<int>(count);
			file.added += static_cast<int>(inserted.size());
		}

		// Checks the results before writing anything.
		std::map<std::string, std::string> contents;
		for (const std::string& relative : order)
		{
			const Pending& file = files[relative];
			const bool crlf = file.original.find("\r\n") != std::string::npos;
			std::string text;
			for (size_t i = 0; i < file.lines.size(); ++i)
				text += file.lines[i] + (i + 1 < file.lines.size() ? (crlf ? "\r\n" : "\n") : "");
			if (!file.original.empty() && (file.original.back() == '\n') && (text.empty() || text.back() != '\n'))
				text += crlf ? "\r\n" : "\n";

			std::string error;
			if (errors.empty() && !Validate(relative, text, error))
				fail(relative, error);
			contents[relative] = std::move(text);
		}

		if (!errors.empty())
		{
			result.message = "Nothing was written :\n" + errors;
			return result;
		}

		// Write, keeping the previous content for Revert.
		std::vector<Backup> backups;
		for (const std::string& relative : order)
		{
			const Pending& file = files[relative];
			backups.push_back({ file.path, relative, file.existed, file.original });
			if (!WriteFile(file.path, contents[relative]))
			{
				// Put back what was already written.
				for (const Backup& b : backups)
				{
					std::error_code ec;
					if (b.existed)
						WriteFile(b.path, b.content);
					else
						fs::remove(b.path, ec);
				}
				result.message = "Could not write " + relative + " : nothing changed.";
				result.failed_files.push_back(relative);
				return result;
			}

			const std::string extension = Lower(fs::path(relative).extension().string());
			result.cpp_changed = result.cpp_changed || extension == ".cpp" || extension == ".h" ||
			                     extension == ".hpp" || extension == ".inl" || extension == ".c" ||
			                     fs::path(relative).filename() == "CMakeLists.txt";
			result.files.push_back(relative);
			result.message += (result.message.empty() ? "" : ", ") + std::string(file.existed ? "edited " : "created ") +
			                  relative + " (+" + std::to_string(file.added) + " -" + std::to_string(file.removed) + " lines)";
		}

		g_last_edit = std::move(backups);
		result.ok = true;
		return result;
	}


	bool CanRevert()
	{
		return !g_last_edit.empty();
	}


	std::string RevertDescription()
	{
		std::string text;
		for (const Backup& b : g_last_edit)
			text += (text.empty() ? "" : ", ") + b.relative;
		return text;
	}


	std::string Revert()
	{
		std::string text;
		for (const Backup& b : g_last_edit)
		{
			std::error_code ec;
			if (b.existed)
				WriteFile(b.path, b.content);
			else
				fs::remove(b.path, ec);
			text += (text.empty() ? "" : ", ") + std::string(b.existed ? "restored " : "deleted ") + b.relative;
		}
		g_last_edit.clear();
		return text;
	}
}
