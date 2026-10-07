#include "JsLanguage.h"

#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace lynx::editor::js_language
{
	namespace
	{
#include "JsApiDocs.inl"

		// =====================================================================
		// Small helpers
		// =====================================================================

		bool IsIdStart(unsigned char c) { return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
		bool IsIdChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80; }

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool StartsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

		const std::unordered_set<std::string>& Keywords()
		{
			static const std::unordered_set<std::string> k = {
				"break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else",
				"export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "new",
				"null", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while",
				"with", "yield", "let", "static", "await", "async", "of", "get", "set", "undefined",
			};
			return k;
		}

		// Words that are keywords only in some places (a variable can have these names).
		bool IsContextualKeyword(const std::string& s)
		{
			return s == "of" || s == "get" || s == "set" || s == "async" || s == "static" || s == "let" || s == "await" ||
			       s == "yield" || s == "undefined";
		}

		// =====================================================================
		// Environment : engine globals, members, documentation, project
		// =====================================================================

		struct ProjectClass
		{
			std::string name;
			std::string base;
			fs::path file;
			int line = 0, start = 0, end = 0;
			struct Member
			{
				std::string name;
				bool method = false;
				bool is_static = false;
				std::string params;   // "(a, b)"
				int line = 0, start = 0, end = 0;
			};
			std::vector<Member> members;
		};

		struct Environment
		{
			std::mutex mutex;
			// name -> 'f' function, 'c' class, 'o' object, 'v' value
			std::map<std::string, char> globals;
			// "Level", "Actor.prototype"... -> name -> kind ('f' function, 'c' class, 'g' accessor, 'p' value)
			std::map<std::string, std::map<std::string, char>> members;
			// class -> base class (engine classes)
			std::map<std::string, std::string> bases;
			bool introspected = false;

			// documentation : owner -> name -> entry
			std::map<std::string, std::map<std::string, const ApiEntry*>> docs;

			// project
			fs::path root;
			std::map<std::string, ProjectClass> classes;        // classes of the .js files of assets/
			std::map<fs::path, fs::file_time_type> scanned;    // file -> time it was read
			std::vector<std::string> actions, axes;             // input.json
			std::vector<std::string> widgets, scripts, images, sounds, animgraphs, trees;

			std::function<bool(const std::string&, const std::string&, std::string&)> check_syntax;
		};

		Environment& Env()
		{
			static Environment env;
			static bool docs_ready = false;
			if (!docs_ready)
			{
				docs_ready = true;
				for (const ApiEntry& e : kApiEntries)
					env.docs[e.owner][e.name] = &e;
				for (const char* name : kBuiltinGlobals)
					env.globals[name] = std::isupper(static_cast<unsigned char>(name[0])) ? 'c' : 'f';
				for (const char* name : { "Level", "Input", "Engine", "UI", "BT", "console", "Math", "JSON", "Reflect", "Atomics" })
					env.globals[name] = 'o';
				for (const char* name : { "globalThis" })
					env.globals[name] = 'o';
			}
			return env;
		}

		const ApiEntry* Doc(const std::string& owner, const std::string& name)
		{
			Environment& env = Env();
			auto o = env.docs.find(owner);
			if (o == env.docs.end())
				return nullptr;
			auto n = o->second.find(name);
			return n == o->second.end() ? nullptr : n->second;
		}

		std::string ReadText(const fs::path& path)
		{
			std::ifstream in(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		std::vector<std::string> SplitLines(const std::string& text)
		{
			std::vector<std::string> lines(1);
			for (char c : text)
			{
				if (c == '\n')
					lines.emplace_back();
				else if (c != '\r')
					lines.back().push_back(c);
			}
			return lines;
		}

		// =====================================================================
		// Lexer
		// =====================================================================

		enum class Tok { Ident, Keyword, Number, String, Template, Regex, Punct, Private };

		struct Token
		{
			Tok type = Tok::Punct;
			std::string text;
			int line = 0, start = 0;       // byte index in the line
			int end_line = 0, end = 0;
			bool newline_before = false;   // a line break before it (ASI)
			bool template_close = false;   // "}" that ends a ${ } of a template
			bool unterminated = false;
		};

		struct Range
		{
			int line, start, end_line, end;
		};

		struct Lexed
		{
			std::vector<Token> tokens;
			std::vector<Range> comments;
			std::vector<Range> strings;    // string / template text (for completion in strings)
		};

		Lexed Lex(const std::vector<std::string>& lines)
		{
			Lexed out;
			int line = 0, col = 0;
			auto at_end = [&]() { return line >= (int)lines.size(); };
			auto peek = [&](int k = 0) -> char
			{
				int l = line, c = col + k;
				while (l < (int)lines.size())
				{
					if (c < (int)lines[l].size())
						return lines[l][c];
					if (c == (int)lines[l].size())
						return l + 1 < (int)lines.size() ? '\n' : '\0';
					c -= (int)lines[l].size() + 1;
					++l;
				}
				return '\0';
			};
			auto advance = [&]()
			{
				if (col < (int)lines[line].size())
					++col;
				else
				{
					++line;
					col = 0;
				}
			};

			std::vector<int> template_depth;   // braces open inside each ${ }
			bool newline = true;

			auto prev_allows_regex = [&]() -> bool
			{
				if (out.tokens.empty())
					return true;
				const Token& p = out.tokens.back();
				switch (p.type)
				{
				case Tok::Number: case Tok::String: case Tok::Template: case Tok::Regex: case Tok::Private: return false;
				case Tok::Ident: return false;
				case Tok::Keyword:
					return !(p.text == "this" || p.text == "super" || p.text == "true" || p.text == "false" ||
					         p.text == "null" || p.text == "undefined");
				case Tok::Punct: return !(p.text == ")" || p.text == "]" || (p.text == "}" && p.template_close));
				}
				return true;
			};

			// Text of a template, from after ` or } to ` or ${
			auto read_template = [&](Token& t)
			{
				t.type = Tok::Template;
				const int sl = line, sc = col;
				while (!at_end())
				{
					const char c = peek();
					if (c == '\0')
						break;
					if (c == '\\')
					{
						advance();
						advance();
						continue;
					}
					if (c == '`')
					{
						advance();
						t.end_line = line;
						t.end = col;
						out.strings.push_back({ sl, sc, line, col - 1 });
						return false;   // finished
					}
					if (c == '$' && peek(1) == '{')
					{
						t.end_line = line;
						t.end = col;
						out.strings.push_back({ sl, sc, line, col });
						advance();
						advance();
						return true;    // an expression follows
					}
					advance();
				}
				t.unterminated = true;
				t.end_line = line;
				t.end = col;
				out.strings.push_back({ sl, sc, line, col });
				return false;
			};

			static const char* const kPuncts[] = {
				">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=",
				"=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=",
				"&=", "|=", "^=", "**", "<<", ">>",
			};

			while (!at_end())
			{
				const char c = peek();
				if (c == '\0')
					break;
				if (c == '\n')
				{
					advance();
					newline = true;
					continue;
				}
				if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v')
				{
					advance();
					continue;
				}
				// comments
				if (c == '/' && peek(1) == '/')
				{
					out.comments.push_back({ line, col, line, (int)lines[line].size() });
					col = (int)lines[line].size();
					continue;
				}
				if (c == '/' && peek(1) == '*')
				{
					const int sl = line, sc = col;
					advance();
					advance();
					while (!at_end() && !(peek() == '*' && peek(1) == '/') && peek() != '\0')
					{
						if (peek() == '\n')
							newline = true;
						advance();
					}
					if (!at_end() && peek() != '\0')
					{
						advance();
						advance();
					}
					out.comments.push_back({ sl, sc, line, col });
					continue;
				}

				Token t;
				t.line = line;
				t.start = col;
				t.newline_before = newline;
				newline = false;

				if (IsIdStart(static_cast<unsigned char>(c)) || (c == '#' && IsIdStart(static_cast<unsigned char>(peek(1)))))
				{
					std::string text;
					text.push_back(c);
					advance();
					while (line == t.line && col < (int)lines[line].size() && IsIdChar(static_cast<unsigned char>(lines[line][col])))
					{
						text.push_back(lines[line][col]);
						++col;
					}
					t.text = text;
					t.type = c == '#' ? Tok::Private : (Keywords().count(text) ? Tok::Keyword : Tok::Ident);
				}
				else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))))
				{
					t.type = Tok::Number;
					while (line == t.line && col < (int)lines[line].size())
					{
						const char d = lines[line][col];
						if (std::isalnum(static_cast<unsigned char>(d)) || d == '.' || d == '_' ||
						    ((d == '+' || d == '-') && col > 0 && (lines[line][col - 1] == 'e' || lines[line][col - 1] == 'E') &&
						     !(t.text.size() > 1 && (t.text[1] == 'x' || t.text[1] == 'X'))))
						{
							t.text.push_back(d);
							++col;
						}
						else
							break;
					}
				}
				else if (c == '"' || c == '\'')
				{
					t.type = Tok::String;
					const char quote = c;
					advance();
					t.text.push_back(quote);
					bool closed = false;
					while (line == t.line && col < (int)lines[line].size())
					{
						const char d = lines[line][col];
						if (d == '\\')
						{
							t.text.push_back(d);
							++col;
							if (col < (int)lines[line].size())
							{
								t.text.push_back(lines[line][col]);
								++col;
							}
							continue;
						}
						t.text.push_back(d);
						++col;
						if (d == quote)
						{
							closed = true;
							break;
						}
					}
					t.unterminated = !closed;
					out.strings.push_back({ t.line, t.start, line, col });
				}
				else if (c == '`')
				{
					advance();
					t.text = "`";
					const bool expression = read_template(t);
					out.tokens.push_back(t);
					if (expression)
					{
						Token open;
						open.type = Tok::Punct;
						open.text = "${";
						open.line = open.end_line = line;
						open.start = col - 2;
						open.end = col;
						out.tokens.push_back(open);
						template_depth.push_back(0);
					}
					continue;
				}
				else if (c == '/' && prev_allows_regex())
				{
					t.type = Tok::Regex;
					advance();
					bool in_class = false;
					while (line == t.line && col < (int)lines[line].size())
					{
						const char d = lines[line][col];
						++col;
						if (d == '\\')
						{
							++col;
							continue;
						}
						if (d == '[')
							in_class = true;
						else if (d == ']')
							in_class = false;
						else if (d == '/' && !in_class)
							break;
					}
					while (line == t.line && col < (int)lines[line].size() && std::isalpha(static_cast<unsigned char>(lines[line][col])))
						++col;
					t.text = lines[t.line].substr(t.start, col - t.start);
				}
				else
				{
					t.type = Tok::Punct;
					std::string text;
					for (const char* p : kPuncts)
					{
						const size_t n = std::strlen(p);
						bool same = true;
						for (size_t k = 0; k < n && same; ++k)
							same = peek(static_cast<int>(k)) == p[k];
						if (same)
						{
							// "?." followed by a digit is "?" then ".5"
							if (std::strcmp(p, "?.") == 0 && std::isdigit(static_cast<unsigned char>(peek(2))))
								continue;
							text = p;
							break;
						}
					}
					if (text.empty())
						text = std::string(1, c);
					for (size_t k = 0; k < text.size(); ++k)
						advance();
					t.text = text;

					if (text == "{" && !template_depth.empty())
						++template_depth.back();
					else if (text == "}" && !template_depth.empty())
					{
						if (template_depth.back() == 0)
						{
							template_depth.pop_back();
							t.template_close = true;
							t.end_line = line;
							t.end = col;
							out.tokens.push_back(t);
							// the rest of the template
							Token rest;
							rest.line = line;
							rest.start = col;
							rest.text = "`";
							const bool expression = read_template(rest);
							out.tokens.push_back(rest);
							if (expression)
							{
								Token open;
								open.type = Tok::Punct;
								open.text = "${";
								open.line = open.end_line = line;
								open.start = col - 2;
								open.end = col;
								out.tokens.push_back(open);
								template_depth.push_back(0);
							}
							continue;
						}
						--template_depth.back();
					}
				}
				t.end_line = line;
				t.end = col;
				out.tokens.push_back(t);
			}
			return out;
		}

		bool Inside(const Range& r, int line, int index)
		{
			if (line < r.line || line > r.end_line)
				return false;
			if (line == r.line && index <= r.start)
				return false;
			if (line == r.end_line && index > r.end)
				return false;
			return true;
		}

		// =====================================================================
		// Analysis : scopes, declarations, references
		// =====================================================================

		enum class DeclKind { Var, Let, Const, Function, Class, Param, CatchParam, Import };

		struct Decl
		{
			std::string name;
			DeclKind kind = DeclKind::Let;
			int scope = 0;
			int tok = -1;
			int uses = 0;
			int writes = 0;
			int init_begin = -1, init_end = -1;   // tokens of the initializer
			std::string params;                   // functions : "(a, b)"
			int class_index = -1;                 // classes
		};

		enum class ScopeKind { Global, Function, Block, Class, ArrowExpr, For };

		struct Scope
		{
			int parent = -1;
			ScopeKind kind = ScopeKind::Global;
			int begin = 0, end = 1 << 30;   // tokens
			int class_index = -1;           // class body, and the methods of the class
			size_t depth = 0;               // ArrowExpr / For without braces : context depth
			bool single_statement = false;  // For without braces
			std::vector<int> decls;
		};

		struct ClassInfo
		{
			std::string name;
			std::string base;        // "Actor", "Pawn"... (text of the extends expression)
			int tok = -1;
			int scope = -1;
			struct Member
			{
				std::string name;
				bool method = false;
				bool is_static = false;
				std::string params;
				int tok = -1;
				int init_begin = -1, init_end = -1;
			};
			std::vector<Member> members;
		};

		enum class Role : uint8_t { None, Ref, Decl, Member, Key, Label, ClassMember };

		struct Ref
		{
			int tok;
			int scope;
			bool write;
			bool typeof_operand;
		};

		struct Analysis
		{
			Lexed lex;
			std::vector<Scope> scopes;
			std::vector<Decl> decls;
			std::vector<ClassInfo> classes;
			std::vector<Ref> refs;
			std::vector<int> tok_scope;   // scope after the token
			std::vector<Role> role;
			std::vector<int> link;        // Ref : decl (-1 : global) ; Decl : decl ; ClassMember : class
			std::vector<int> match;       // matching bracket
			std::vector<Diagnostic> diagnostics;
		};

		bool IsAssignOp(const std::string& s)
		{
			static const std::unordered_set<std::string> ops = {
				"=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=", ">>>=", "&=", "|=", "^=", "&&=", "||=", "?\?=",
			};
			return ops.count(s) != 0;
		}

		class Analyzer
		{
		public:
			Analysis a;

			void Run(const std::vector<std::string>& lines)
			{
				a.lex = Lex(lines);
				const auto& T = a.lex.tokens;
				const int N = static_cast<int>(T.size());
				a.tok_scope.assign(N, 0);
				a.role.assign(N, Role::None);
				a.link.assign(N, -1);
				a.match.assign(N, -1);
				ComputeMatches();

				a.scopes.push_back(Scope{});
				scope = 0;

				for (int i = 0; i < N; ++i)
				{
					Step(i);
					a.tok_scope[i] = scope;
				}
				while (scope > 0)
					CloseScope(N);
				a.scopes[0].end = N;

				Resolve();
			}

		private:
			struct Ctx
			{
				enum Type { Paren, Bracket, Block, Object, ClassBody, TemplateExpr, Pattern, Params } type = Block;
				int open = -1;
				int scopes = 0;            // scopes closed with it
				bool expect_key = true;    // Object / ClassBody
				bool in_default = false;   // Pattern / Params
				DeclKind bind = DeclKind::Let;
				bool to_params = false;    // Pattern inside Params
				bool pattern_object = false;
				int class_index = -1;
				bool for_head = false;
				int pending_member = -1;   // ClassBody : field whose initializer is being read
				bool next_static = false;
				int function_decl = -1;    // Params : the function declaration
				int method_class = -1;     // Params : class of the method
				int method_member = -1;
				bool arrow = false;
			};
			std::vector<Ctx> ctx;
			int scope = 0;

			struct DeclMode
			{
				DeclKind kind;
				size_t depth;
				bool expect_binding;
				bool in_init;
				int last_decl;
			};
			std::vector<DeclMode> modes;

			std::vector<std::pair<std::string, int>> pending_params;
			size_t pending_refs_begin = 0;   // refs made inside the parameters (default values)
			DeclKind pending_param_kind = DeclKind::Param;
			bool pending_body = false;
			int pending_body_class = -1;
			bool pending_class_body = false;
			int pending_class = -1;
			int pending_for_scope = -1;
			bool expect_fn_params = false;
			int expect_fn_decl = -1;
			bool expect_catch_params = false;
			bool expect_for_head = false;
			bool method_params_next = false;
			int method_class = -1;
			int method_member = -1;
			int typeof_next = -1;
			bool case_label = false;     // case / default : its ":" is not an object key
			int case_colon = -1;

			const Token& Tk(int i) const { return a.lex.tokens[i]; }
			int Count() const { return static_cast<int>(a.lex.tokens.size()); }
			bool Is(int i, const char* text) const
			{
				if (i < 0 || i >= Count())
					return false;
				const Token& t = Tk(i);
				return (t.type == Tok::Punct || t.type == Tok::Keyword) && t.text == text;
			}
			bool IsName(int i) const
			{
				if (i < 0 || i >= Count())
					return false;
				const Token& t = Tk(i);
				return t.type == Tok::Ident || t.type == Tok::Private || (t.type == Tok::Keyword && IsContextualKeyword(t.text));
			}

			void ComputeMatches()
			{
				std::vector<int> stack;
				for (int i = 0; i < Count(); ++i)
				{
					const Token& t = Tk(i);
					if (t.type != Tok::Punct)
						continue;
					if (t.text == "(" || t.text == "[" || t.text == "{" || t.text == "${")
						stack.push_back(i);
					else if (t.text == ")" || t.text == "]" || t.text == "}")
					{
						if (!stack.empty())
						{
							a.match[i] = stack.back();
							a.match[stack.back()] = i;
							stack.pop_back();
						}
					}
				}
			}

			int NewScope(ScopeKind kind, int begin)
			{
				Scope s;
				s.parent = scope;
				s.kind = kind;
				s.begin = begin;
				s.class_index = a.scopes[scope].class_index;
				a.scopes.push_back(s);
				scope = static_cast<int>(a.scopes.size()) - 1;
				return scope;
			}

			void CloseScope(int end)
			{
				a.scopes[scope].end = end;
				scope = std::max(0, a.scopes[scope].parent);
			}

			// The function scope that holds `var` / function declarations.
			int FunctionScope(int s) const
			{
				while (s > 0 && a.scopes[s].kind != ScopeKind::Function && a.scopes[s].kind != ScopeKind::ArrowExpr)
					s = a.scopes[s].parent;
				return s;
			}

			int Declare(const std::string& name, DeclKind kind, int tok, int in_scope)
			{
				Decl d;
				d.name = name;
				d.kind = kind;
				d.scope = in_scope;
				d.tok = tok;
				a.decls.push_back(d);
				const int index = static_cast<int>(a.decls.size()) - 1;
				a.scopes[in_scope].decls.push_back(index);
				if (tok >= 0)
				{
					a.role[tok] = Role::Decl;
					a.link[tok] = index;
				}
				return index;
			}

			int DeclScope(DeclKind kind) const
			{
				return kind == DeclKind::Var ? FunctionScope(scope) : scope;
			}

			std::string TokensText(int from, int to) const   // [from, to] joined, spaces where the source had some
			{
				std::string text;
				for (int k = from; k <= to && k < Count(); ++k)
				{
					if (k > from && (Tk(k).start > Tk(k - 1).end || Tk(k).line != Tk(k - 1).line) &&
					    !(Tk(k).type == Tok::Punct && (Tk(k).text == "," || Tk(k).text == ")")))
						text.push_back(' ');
					text += Tk(k).text;
				}
				return text;
			}

			// The token `next` continues the expression of `prev` (no automatic semicolon).
			bool Continues(int prev, int next) const
			{
				if (prev < 0)
					return true;
				static const std::unordered_set<std::string> after = {
					"=", "+", "-", "*", "/", "%", "**", "&&", "||", "??", "?", ":", ",", "(", "[", "{", ".", "?.", "=>",
					"<", ">", "<=", ">=", "==", "!=", "===", "!==", "&", "|", "^", "<<", ">>", ">>>", "!", "~", "...",
					"+=", "-=", "*=", "/=", "%=", "**=", "&&=", "||=", "?\?=", "&=", "|=", "^=", "<<=", ">>=", "${",
				};
				static const std::unordered_set<std::string> before = {
					".", "?.", "(", "[", ")", "]", "}", ",", "?", ":", "=", "+=", "-=", "*=", "/=", "&&", "||", "??",
					"==", "!=", "===", "!==", "<", ">", "<=", ">=", "*", "/", "%", "**", "|", "&", "^", "=>", "+", "-",
				};
				const Token& p = Tk(prev);
				const Token& n = Tk(next);
				if (p.type == Tok::Punct && after.count(p.text))
					return true;
				if (p.type == Tok::Keyword && (p.text == "new" || p.text == "typeof" || p.text == "instanceof" ||
				                               p.text == "in" || p.text == "of" || p.text == "await" || p.text == "void" ||
				                               p.text == "delete" || p.text == "extends"))
					return true;
				if (n.type == Tok::Punct && before.count(n.text) && !(n.text == "+" || n.text == "-") )
					return true;
				if (n.type == Tok::Keyword && (n.text == "instanceof" || n.text == "in" || n.text == "of"))
					return true;
				return false;
			}

			bool IsObjectBrace(int i) const
			{
				if (i == 0)
					return false;
				const Token& p = Tk(i - 1);
				if (!ctx.empty())
				{
					const Ctx& top = ctx.back();
					if ((top.type == Ctx::Object && !top.expect_key) || top.type == Ctx::Paren || top.type == Ctx::Bracket ||
					    top.type == Ctx::TemplateExpr)
					{
						if (p.type == Tok::Punct && (p.text == ")" ))
							return false;
						if (!(p.type == Tok::Punct && (p.text == "=>")))
							return true;
					}
				}
				if (i - 1 == case_colon)
					return false;   // case 1: { ... }
				if (p.type == Tok::Punct)
				{
					static const std::unordered_set<std::string> obj = {
						"=", "(", ",", ":", "[", "?", "&&", "||", "??", "!", "+", "-", "*", "...", "==", "===", "!=",
						"!==", "+=", "-=", "||=", "&&=", "?\?=", "${", "<", ">",
					};
					return obj.count(p.text) != 0;
				}
				if (p.type == Tok::Keyword)
					return p.text == "return" || p.text == "yield" || p.text == "await" || p.text == "typeof" ||
					       p.text == "in" || p.text == "of" || p.text == "case" || p.text == "throw" || p.text == "void";
				return false;
			}

			void EndMode(int at)
			{
				DeclMode& m = modes.back();
				if (m.last_decl >= 0 && m.in_init && a.decls[m.last_decl].init_end < 0)
					a.decls[m.last_decl].init_end = at;
				modes.pop_back();
			}

			int CurrentClass() const
			{
				return a.scopes[scope].class_index;
			}

			void AddParamsAsDecls(int in_scope)
			{
				for (const auto& [name, tok] : pending_params)
					Declare(name, pending_param_kind, tok, in_scope);
				// default values see the parameters before them : (a, b = a)
				for (size_t r = pending_refs_begin; r < a.refs.size(); ++r)
					if (a.refs[r].scope == a.scopes[in_scope].parent)
						a.refs[r].scope = in_scope;
				pending_refs_begin = a.refs.size();
				pending_params.clear();
			}

			void Step(int i)
			{
				const Token& t = Tk(i);
				const int N = Count();
				auto next_is = [&](const char* text) { return Is(i + 1, text); };

				// --- end of an arrow function with an expression body -------------------
				while (a.scopes[scope].kind == ScopeKind::ArrowExpr)
				{
					const Scope& s = a.scopes[scope];
					bool close = ctx.size() < s.depth;
					if (!close && ctx.size() == s.depth && t.type == Tok::Punct &&
					    (t.text == "," || t.text == ";" || t.text == ")" || t.text == "]" || t.text == "}"))
						close = true;
					if (!close && ctx.size() == s.depth && t.newline_before && i > 0 && !Continues(i - 1, i))
						close = true;
					if (!close)
						break;
					CloseScope(i - 1);
				}

				// --- declarations (let a = 1, b) : end of the statement ---------------
				while (!modes.empty() && modes.back().depth > ctx.size())
					EndMode(i);
				if (!modes.empty() && modes.back().depth == ctx.size() && modes.back().in_init && t.newline_before &&
				    i > 0 && !Continues(i - 1, i))
					EndMode(i);

				// --- class body : a new line after a field starts a new member ------
				if (!ctx.empty() && ctx.back().type == Ctx::ClassBody && !ctx.back().expect_key && t.newline_before &&
				    i > 0 && !Continues(i - 1, i))
				{
					Ctx& c = ctx.back();
					if (c.pending_member >= 0)
					{
						auto& m = a.classes[c.class_index].members[c.pending_member];
						if (m.init_begin >= 0 && m.init_end < 0)
							m.init_end = i;
					}
					c.pending_member = -1;
					c.expect_key = true;
				}

				const bool binding = !modes.empty() && modes.back().depth == ctx.size() && modes.back().expect_binding;

				if (t.type == Tok::Punct)
				{
					const std::string& p = t.text;
					if (binding && (p == "{" || p == "["))
					{
						Ctx c;
						c.type = Ctx::Pattern;
						c.open = i;
						c.bind = modes.back().kind;
						c.pattern_object = p == "{";
						modes.back().expect_binding = false;
						ctx.push_back(c);
						return;
					}
					if (!ctx.empty() && (ctx.back().type == Ctx::Pattern || ctx.back().type == Ctx::Params) &&
					    !ctx.back().in_default && (p == "{" || p == "["))
					{
						Ctx c;
						c.type = Ctx::Pattern;
						c.open = i;
						c.bind = ctx.back().bind;
						c.to_params = ctx.back().type == Ctx::Params || ctx.back().to_params;
						c.pattern_object = p == "{";
						ctx.push_back(c);
						return;
					}

					if (p == "{")
					{
						Ctx c;
						c.type = Ctx::Block;
						c.open = i;
						if (pending_class_body)
						{
							c.type = Ctx::ClassBody;
							c.class_index = pending_class;
							NewScope(ScopeKind::Class, i);
							a.scopes[scope].class_index = pending_class;
							if (pending_class >= 0)
								a.classes[pending_class].scope = scope;
							c.scopes = 1;
							pending_class_body = false;
						}
						else if (pending_body)
						{
							const bool is_catch = pending_param_kind == DeclKind::CatchParam;
							NewScope(is_catch ? ScopeKind::Block : ScopeKind::Function, i);
							if (pending_body_class >= 0)
								a.scopes[scope].class_index = pending_body_class;
							AddParamsAsDecls(scope);
							c.scopes = 1;
							pending_body = false;
							pending_body_class = -1;
						}
						else if (IsObjectBrace(i))
						{
							c.type = Ctx::Object;
						}
						else
						{
							NewScope(ScopeKind::Block, i);
							c.scopes = 1;
							if (pending_for_scope >= 0 && i > 0 && Is(i - 1, ")"))
								c.scopes = 2;   // the for scope closes with its body
						}
						pending_for_scope = -1;
						ctx.push_back(c);
						return;
					}
					if (p == "}")
					{
						if (ctx.empty())
							return;
						Ctx c = ctx.back();
						ctx.pop_back();
						for (int k = 0; k < c.scopes; ++k)
							CloseScope(i);
						// end of a method body : the next member
						if (!ctx.empty() && ctx.back().type == Ctx::ClassBody && c.type == Ctx::Block)
						{
							ctx.back().expect_key = true;
							ctx.back().pending_member = -1;
						}
						return;
					}
					if (p == "${")
					{
						Ctx c;
						c.type = Ctx::TemplateExpr;
						c.open = i;
						ctx.push_back(c);
						return;
					}
					if (p == "(")
					{
						Ctx c;
						c.type = Ctx::Paren;
						c.open = i;
						const bool arrow = a.match[i] >= 0 && Is(a.match[i] + 1, "=>");
						if (expect_fn_params || expect_catch_params || method_params_next || arrow)
						{
							c.type = Ctx::Params;
							c.bind = expect_catch_params ? DeclKind::CatchParam : DeclKind::Param;
							c.function_decl = expect_fn_params ? expect_fn_decl : -1;
							c.method_class = method_params_next ? method_class : -1;
							c.method_member = method_params_next ? method_member : -1;
							c.arrow = arrow && !expect_fn_params && !method_params_next;
							pending_params.clear();
							pending_refs_begin = a.refs.size();
							pending_param_kind = c.bind;
							expect_fn_params = expect_catch_params = method_params_next = false;
							expect_fn_decl = -1;
						}
						else if (expect_for_head)
						{
							c.for_head = true;
							NewScope(ScopeKind::For, i);
							expect_for_head = false;
						}
						ctx.push_back(c);
						return;
					}
					if (p == ")" || p == "]")
					{
						if (ctx.empty())
							return;
						Ctx c = ctx.back();
						ctx.pop_back();
						for (int k = 0; k < c.scopes; ++k)
							CloseScope(i);
						if (c.type == Ctx::Params)
						{
							const std::string params = TokensText(c.open, i);
							if (c.function_decl >= 0)
								a.decls[c.function_decl].params = params;
							if (c.method_class >= 0 && c.method_member >= 0)
								a.classes[c.method_class].members[c.method_member].params = params;
							if (Is(i + 1, "{") && !c.arrow)
							{
								pending_body = true;
								pending_body_class = c.method_class >= 0 ? c.method_class : CurrentClass();
							}
						}
						else if (c.for_head)
						{
							pending_for_scope = scope;
							if (!Is(i + 1, "{"))
							{
								a.scopes[scope].single_statement = true;
								a.scopes[scope].depth = ctx.size();
							}
						}
						return;
					}
					if (p == "[")
					{
						Ctx c;
						c.type = Ctx::Bracket;
						c.open = i;
						ctx.push_back(c);
						return;
					}
					if (p == ";")
					{
						if (!modes.empty() && modes.back().depth == ctx.size())
							EndMode(i);
						if (!ctx.empty() && ctx.back().type == Ctx::ClassBody)
						{
							Ctx& c = ctx.back();
							if (c.pending_member >= 0)
							{
								auto& m = a.classes[c.class_index].members[c.pending_member];
								if (m.init_begin >= 0 && m.init_end < 0)
									m.init_end = i;
							}
							c.pending_member = -1;
							c.expect_key = true;
						}
						// for (...) statement; : its scope ends here
						if (a.scopes[scope].kind == ScopeKind::For && a.scopes[scope].single_statement &&
						    a.scopes[scope].depth == ctx.size())
							CloseScope(i);
						return;
					}
					if (p == ",")
					{
						if (!modes.empty() && modes.back().depth == ctx.size())
						{
							DeclMode& m = modes.back();
							if (m.last_decl >= 0 && m.in_init && a.decls[m.last_decl].init_end < 0)
								a.decls[m.last_decl].init_end = i;
							m.expect_binding = true;
							m.in_init = false;
						}
						if (!ctx.empty())
						{
							Ctx& c = ctx.back();
							if (c.type == Ctx::Object)
								c.expect_key = true;
							if (c.type == Ctx::Pattern || c.type == Ctx::Params)
								c.in_default = false;
						}
						return;
					}
					if (p == "=")
					{
						if (!modes.empty() && modes.back().depth == ctx.size() && !modes.back().expect_binding &&
						    !modes.back().in_init)
						{
							modes.back().in_init = true;
							if (modes.back().last_decl >= 0)
								a.decls[modes.back().last_decl].init_begin = i + 1;
						}
						if (!ctx.empty())
						{
							Ctx& c = ctx.back();
							if (c.type == Ctx::Pattern || c.type == Ctx::Params)
								c.in_default = true;
							if (c.type == Ctx::ClassBody && c.pending_member >= 0)
							{
								a.classes[c.class_index].members[c.pending_member].init_begin = i + 1;
								c.expect_key = false;
							}
						}
						return;
					}
					if (p == ":" && case_label)
					{
						case_label = false;
						case_colon = i;
						return;
					}
					if (p == "=>")
					{
						if (Is(i + 1, "{"))
						{
							pending_body = true;
							pending_body_class = CurrentClass();
						}
						else
						{
							NewScope(ScopeKind::ArrowExpr, i + 1);
							a.scopes[scope].depth = ctx.size();
							AddParamsAsDecls(scope);
						}
						return;
					}
					return;
				}

				if (t.type == Tok::Keyword && !IsContextualKeyword(t.text))
				{
					const std::string& k = t.text;
					// obj.default, {case: 1} : names, not keywords
					if (i > 0 && (Is(i - 1, ".") || Is(i - 1, "?.")))
					{
						a.role[i] = Role::Member;
						return;
					}
					if (!ctx.empty() && ctx.back().type == Ctx::Object && ctx.back().expect_key && (next_is(":") || next_is("(")))
					{
						a.role[i] = Role::Key;
						ctx.back().expect_key = false;
						if (next_is("("))
						{
							method_params_next = true;
							method_class = -1;
							method_member = -1;
						}
						return;
					}
					if (k == "var" || k == "const")
					{
						DeclMode m{ k == "var" ? DeclKind::Var : DeclKind::Const, ctx.size(), true, false, -1 };
						modes.push_back(m);
						return;
					}
					if (k == "function")
					{
						expect_fn_params = true;
						int n = i + 1;
						if (Is(n, "*"))
							++n;
						expect_fn_decl = -1;
						if (IsName(n) && !Is(n, "("))
						{
							// a declaration : hoisted to the function scope
							expect_fn_decl = Declare(Tk(n).text, DeclKind::Function, n, FunctionScope(scope));
							skip_name = n;
						}
						return;
					}
					if (k == "class")
					{
						ClassInfo info;
						int n = i + 1;
						if (IsName(n) && !Is(n, "extends"))
						{
							info.name = Tk(n).text;
							info.tok = n;
							const int d = Declare(info.name, DeclKind::Class, n, scope);
							a.decls[d].class_index = static_cast<int>(a.classes.size());
							skip_name = n;
							++n;
						}
						if (Is(n, "extends"))
						{
							int e = n + 1;
							std::string base;
							while (e < N && !Is(e, "{"))
							{
								base += Tk(e).text;
								++e;
							}
							info.base = base;
						}
						a.classes.push_back(info);
						pending_class = static_cast<int>(a.classes.size()) - 1;
						pending_class_body = true;
						return;
					}
					if (k == "catch")
					{
						if (Is(i + 1, "("))
							expect_catch_params = true;
						return;
					}
					if (k == "for")
					{
						expect_for_head = true;
						return;
					}
					if (k == "case" || (k == "default" && Is(i + 1, ":")))
					{
						case_label = true;
						return;
					}
					if (k == "break" || k == "continue")
					{
						if (IsName(i + 1) && !Tk(i + 1).newline_before)
							a.role[i + 1] = Role::Label;
						return;
					}
					if (k == "typeof")
					{
						typeof_next = i + 1;
						return;
					}
					if (k == "import")
					{
						// import a from "x" / import { a, b as c } from "x"
						for (int n = i + 1; n < N && !Is(n, ";") && !(Tk(n).type == Tok::String); ++n)
						{
							if (Is(n, "from"))
								break;
							if (IsName(n) && !Is(n + 1, "as") && Tk(n).text != "as")
							{
								Declare(Tk(n).text, DeclKind::Import, n, 0);
								a.role[n] = Role::Decl;
							}
						}
						return;
					}
					return;
				}

				// --- names (identifiers, #private, contextual keywords) ----------------
				if (!IsName(i))
					return;
				if (skip_name == i)
				{
					skip_name = -1;
					return;
				}
				if (a.role[i] != Role::None)
					return;   // already decided (label, import...)
				const std::string& name = t.text;
				const bool contextual = t.type == Tok::Keyword;

				// obj.name
				if (i > 0 && (Is(i - 1, ".") || Is(i - 1, "?.")))
				{
					a.role[i] = Role::Member;
					a.link[i] = i - 1;
					// this.field = value : a field of the class
					if (i >= 2 && Is(i - 2, "this") && i + 1 < N && Tk(i + 1).type == Tok::Punct && Tk(i + 1).text == "=")
					{
						const int c = CurrentClass();
						if (c >= 0)
						{
							auto& members = a.classes[c].members;
							const bool known = std::any_of(members.begin(), members.end(),
							                               [&](const ClassInfo::Member& m) { return m.name == name; });
							if (!known)
							{
								ClassInfo::Member m;
								m.name = name;
								m.tok = i;
								m.init_begin = i + 2;
								m.init_end = ExpressionEnd(i + 2);
								members.push_back(m);
							}
						}
					}
					return;
				}

				// let / const / var binding
				if (!modes.empty() && modes.back().depth == ctx.size() && modes.back().expect_binding)
				{
					DeclMode& m = modes.back();
					m.last_decl = Declare(name, m.kind, i, DeclScope(m.kind));
					m.expect_binding = false;
					return;
				}
				if (contextual && name == "let" && (IsName(i + 1) || Is(i + 1, "{") || Is(i + 1, "[")))
				{
					DeclMode m{ DeclKind::Let, ctx.size(), true, false, -1 };
					modes.push_back(m);
					return;
				}

				Ctx* top = ctx.empty() ? nullptr : &ctx.back();

				// patterns / parameters
				if (top && (top->type == Ctx::Pattern || top->type == Ctx::Params) && !top->in_default)
				{
					if (top->type == Ctx::Pattern && top->pattern_object && next_is(":"))
					{
						a.role[i] = Role::Key;
						return;
					}
					if (top->type == Ctx::Params || top->to_params)
					{
						pending_params.emplace_back(name, i);
						a.role[i] = Role::Decl;
					}
					else
					{
						const int d = Declare(name, top->bind, i, DeclScope(top->bind));
						if (!modes.empty())
							modes.back().last_decl = d;
					}
					return;
				}

				// object literal keys
				if (top && top->type == Ctx::Object && top->expect_key)
				{
					if (contextual && (name == "get" || name == "set" || name == "async" || name == "static") &&
					    (IsName(i + 1) || Is(i + 1, "[") || Is(i + 1, "*")))
						return;   // modifier
					if (next_is(":"))
					{
						a.role[i] = Role::Key;
						top->expect_key = false;
						return;
					}
					if (next_is("("))
					{
						a.role[i] = Role::Key;
						top->expect_key = false;
						method_params_next = true;
						method_class = -1;
						method_member = -1;
						return;
					}
					top->expect_key = false;   // shorthand { a } : a reference
				}

				// class members
				if (top && top->type == Ctx::ClassBody && top->expect_key)
				{
					if (contextual && (name == "get" || name == "set" || name == "async" || name == "static") &&
					    (IsName(i + 1) || Is(i + 1, "[") || Is(i + 1, "*") || Is(i + 1, "{")))
					{
						if (name == "static")
							top->next_static = true;
						return;
					}
					ClassInfo::Member m;
					m.name = name;
					m.tok = i;
					m.is_static = top->next_static;
					top->next_static = false;
					a.role[i] = Role::ClassMember;
					a.link[i] = top->class_index;
					if (top->class_index >= 0)
					{
						auto& members = a.classes[top->class_index].members;
						m.method = next_is("(");
						members.push_back(m);
						const int index = static_cast<int>(members.size()) - 1;
						if (m.method)
						{
							method_params_next = true;
							method_class = top->class_index;
							method_member = index;
						}
						else
						{
							top->pending_member = index;
							top->expect_key = false;
						}
					}
					return;
				}

				// x => ... : a parameter
				if (next_is("=>"))
				{
					pending_params.clear();
					pending_params.emplace_back(name, i);
					pending_refs_begin = a.refs.size();
					pending_param_kind = DeclKind::Param;
					a.role[i] = Role::Decl;
					return;
				}

				// label:
				if (next_is(":") && (!top || top->type == Ctx::Block) && (i == 0 || Is(i - 1, ";") || Is(i - 1, "{") || Is(i - 1, "}")))
				{
					a.role[i] = Role::Label;
					return;
				}

				if (contextual && (name == "async" || name == "await" || name == "yield" || name == "of" || name == "static" ||
				                   name == "get" || name == "set" || name == "let"))
					return;   // used as a keyword here
				if (contextual && name == "undefined")
					return;

				// a reference
				Ref r;
				r.tok = i;
				r.scope = scope;
				r.write = (i + 1 < N && Tk(i + 1).type == Tok::Punct &&
				           (IsAssignOp(Tk(i + 1).text) || Tk(i + 1).text == "++" || Tk(i + 1).text == "--")) ||
				          (i > 0 && (Is(i - 1, "++") || Is(i - 1, "--")));
				r.typeof_operand = typeof_next == i;
				a.refs.push_back(r);
				a.role[i] = Role::Ref;
			}

			int skip_name = -1;

		public:
			// End of the expression that starts at `begin` (exclusive).
			int ExpressionEnd(int begin) const
			{
				int depth = 0;
				for (int k = begin; k < Count(); ++k)
				{
					const Token& t = Tk(k);
					if (t.type == Tok::Punct)
					{
						if (t.text == "(" || t.text == "[" || t.text == "{" || t.text == "${")
							++depth;
						else if (t.text == ")" || t.text == "]" || t.text == "}")
						{
							if (depth == 0)
								return k;
							--depth;
						}
						else if (depth == 0 && (t.text == ";" || t.text == ","))
							return k;
					}
					if (depth == 0 && k > begin && t.newline_before && !Continues(k - 1, k))
						return k;
				}
				return Count();
			}

		private:
			void Resolve();
		};

		Diagnostic MakeDiagnostic(const Token& t, Severity severity, const std::string& message)
		{
			Diagnostic d;
			d.line = t.line;
			d.start = t.start;
			d.end_line = t.end_line;
			d.end = t.end;
			d.severity = severity;
			d.message = message;
			return d;
		}

		bool IsKnownGlobal(const std::string& name)
		{
			Environment& env = Env();
			if (env.globals.count(name) || env.classes.count(name))
				return true;
			static const std::unordered_set<std::string> implicit = {
				"parent", "globalThis", "undefined", "NaN", "Infinity", "arguments", "require", "module", "exports",
			};
			return implicit.count(name) != 0;
		}

		void Analyzer::Resolve()
		{
			Environment& env = Env();

			// static properties = { hp: 100, speed: 2.5 } : instance properties (this.hp)
			for (ClassInfo& c : a.classes)
			{
				std::vector<ClassInfo::Member> added;
				for (const ClassInfo::Member& m : c.members)
				{
					if (!m.is_static || m.name != "properties" || m.init_begin < 0 || !Is(m.init_begin, "{"))
						continue;
					const int close = a.match[m.init_begin];
					for (int k = m.init_begin + 1; k > 0 && k < close; ++k)
					{
						if (a.role[k] == Role::Key && Is(k + 1, ":"))
						{
							ClassInfo::Member p;
							p.name = Tk(k).text;
							p.tok = k;
							p.init_begin = k + 2;
							// { value: 3, type: "int" } : the value
							if (Is(k + 2, "{") && k + 3 < Count() && Tk(k + 3).text == "value" && Is(k + 4, ":"))
								p.init_begin = k + 5;
							p.init_end = ExpressionEnd(p.init_begin);
							added.push_back(p);
						}
						if ((Is(k, "{") || Is(k, "[") || Is(k, "(")) && a.match[k] > k)
							k = a.match[k];
					}
				}
				for (const ClassInfo::Member& p : added)
					if (std::none_of(c.members.begin(), c.members.end(),
					                 [&](const ClassInfo::Member& m) { return m.name == p.name && !m.is_static; }))
						c.members.push_back(p);
			}
			auto lookup = [&](const std::string& name, int s) -> int
			{
				for (; s >= 0; s = a.scopes[s].parent)
					for (int d : a.scopes[s].decls)
						if (a.decls[d].name == name)
							return d;
				return -1;
			};

			for (const Ref& r : a.refs)
			{
				const Token& t = Tk(r.tok);
				const int d = lookup(t.text, r.scope);
				a.link[r.tok] = d;
				const bool plain_assign = r.write && r.tok + 1 < Count() && Is(r.tok + 1, "=");
				if (d >= 0)
				{
					Decl& decl = a.decls[d];
					if (r.write)
						++decl.writes;
					if (!plain_assign)
						++decl.uses;
					if (r.write && (decl.kind == DeclKind::Const || decl.kind == DeclKind::Import))
						a.diagnostics.push_back(MakeDiagnostic(t, Severity::Error,
							"Cannot assign to '" + t.text + "' : it is a constant."));
					continue;
				}
				if (r.typeof_operand || IsKnownGlobal(t.text))
					continue;
				a.diagnostics.push_back(MakeDiagnostic(t, Severity::Warning, plain_assign
					? "'" + t.text + "' is not declared : this assignment creates a global variable (let / const ?)."
					: "'" + t.text + "' is not defined."));
			}

			// Declared twice in the same scope
			for (const Scope& s : a.scopes)
			{
				std::unordered_map<std::string, int> seen;
				for (int d : s.decls)
				{
					const Decl& decl = a.decls[d];
					auto it = seen.find(decl.name);
					if (it == seen.end())
					{
						seen[decl.name] = d;
						continue;
					}
					const Decl& first = a.decls[it->second];
					auto lexical = [](DeclKind k) { return k == DeclKind::Let || k == DeclKind::Const || k == DeclKind::Class; };
					if ((lexical(decl.kind) || lexical(first.kind)) && decl.tok >= 0)
						a.diagnostics.push_back(MakeDiagnostic(Tk(decl.tok), Severity::Error,
							"'" + decl.name + "' has already been declared (line " + std::to_string(Tk(first.tok).line + 1) + ")."));
				}
			}

			// Never read (not at the top level of the file : the engine calls those)
			for (const Decl& decl : a.decls)
			{
				if (decl.scope == 0 || decl.uses > 0 || decl.tok < 0 || decl.name.empty() || decl.name[0] == '_')
					continue;
				if (decl.kind == DeclKind::Param || decl.kind == DeclKind::CatchParam || decl.kind == DeclKind::Import)
					continue;
				a.diagnostics.push_back(MakeDiagnostic(Tk(decl.tok), Severity::Hint, decl.writes > 0
					? "'" + decl.name + "' is assigned but its value is never read."
					: "'" + decl.name + "' is declared but never used."));
			}

			// Members that don't exist on the engine objects (Level.fnd)
			if (env.introspected)
			{
				for (int i = 2; i < Count(); ++i)
				{
					if (a.role[i] != Role::Member || !Is(i - 1, ".") || a.role[i - 2] != Role::Ref || a.link[i - 2] >= 0)
						continue;
					const std::string& owner = Tk(i - 2).text;
					auto g = env.globals.find(owner);
					if (g == env.globals.end() || g->second != 'o')
						continue;
					auto m = env.members.find(owner);
					if (m == env.members.end() || m->second.empty())
						continue;
					const std::string& name = Tk(i).text;
					if (m->second.count(name) || Doc(owner, name))
						continue;
					if (std::find_if(std::begin(kObjectMembers), std::end(kObjectMembers),
					                 [&](const char* n) { return name == n; }) != std::end(kObjectMembers))
						continue;
					a.diagnostics.push_back(MakeDiagnostic(Tk(i), Severity::Warning,
						"'" + name + "' does not exist on '" + owner + "'."));
				}
			}

			// Lexer
			for (const Token& t : a.lex.tokens)
				if (t.unterminated)
					a.diagnostics.push_back(MakeDiagnostic(t, Severity::Error,
						t.type == Tok::Template ? "Unterminated template string." : "Unterminated string."));
		}

		// =====================================================================
		// Types (what follows a ".")
		// =====================================================================
		//   "NS:Level"          namespace object of the engine
		//   "Actor", "Vec3"...  documented types (JsApiDocs.inl)
		//   "Component:Camera"  a component
		//   "Native:Pawn"       an engine / game C++ class (an actor)
		//   "Class:Enemy"       instance of a JS class (this file or the project)
		//   "ClassRef:Enemy"    the class itself (static members)
		//   "Inst:Map"          instance of a built-in class
		//   "String", "Number", "Array", "Object", "RegExp", "" (unknown)

		bool IsDocType(const std::string& t)
		{
			Environment& env = Env();
			return env.docs.count(t) != 0;
		}

		std::string ShowType(const std::string& t)
		{
			if (StartsWith(t, "NS:")) return t.substr(3);
			if (StartsWith(t, "Component:")) return t.substr(10) + " component";
			if (StartsWith(t, "Native:")) return t.substr(7);
			if (StartsWith(t, "Class:")) return t.substr(6);
			if (StartsWith(t, "ClassRef:")) return "typeof " + t.substr(9);
			if (StartsWith(t, "Inst:")) return t.substr(5);
			if (t == "String") return "string";
			if (t == "Number") return "number";
			if (t == "Array") return "array";
			if (t == "Object") return "object";
			return t;
		}

		struct Queries
		{
			const Analysis& a;
			fs::path file;
			mutable int guard = 0;

			const Token& Tk(int i) const { return a.lex.tokens[i]; }
			int Count() const { return static_cast<int>(a.lex.tokens.size()); }
			bool Is(int i, const char* text) const
			{
				if (i < 0 || i >= Count())
					return false;
				const Token& t = Tk(i);
				return (t.type == Tok::Punct || t.type == Tok::Keyword) && t.text == text;
			}
			bool IsName(int i) const
			{
				if (i < 0 || i >= Count())
					return false;
				const Token& t = Tk(i);
				return t.type == Tok::Ident || t.type == Tok::Private || (t.type == Tok::Keyword && IsContextualKeyword(t.text));
			}

			// --- positions --------------------------------------------------------------
			// Last token that starts before (line, index).
			int TokenBefore(int line, int index) const
			{
				int lo = 0, hi = Count() - 1, found = -1;
				while (lo <= hi)
				{
					const int mid = (lo + hi) / 2;
					const Token& t = Tk(mid);
					if (t.line < line || (t.line == line && t.start < index))
					{
						found = mid;
						lo = mid + 1;
					}
					else
						hi = mid - 1;
				}
				return found;
			}

			int TokenAt(int line, int index) const   // token that contains the position (end included)
			{
				const int t = TokenBefore(line, index + 1);
				if (t < 0)
					return -1;
				const Token& k = Tk(t);
				if (k.line == line && index >= k.start && index <= k.end)
					return t;
				return -1;
			}

			int ScopeAt(int line, int index) const
			{
				const int t = TokenBefore(line, index);
				if (t < 0)
					return 0;
				// a token that ends after the position (the word being typed) : the scope before it
				const Token& k = Tk(t);
				if (k.line == line && k.end >= index && t > 0)
					return a.tok_scope[t - 1];
				return a.tok_scope[t];
			}

			// --- classes ------------------------------------------------------------------
			const ClassInfo* LocalClass(const std::string& name) const
			{
				for (const ClassInfo& c : a.classes)
					if (c.name == name)
						return &c;
				return nullptr;
			}

			const ProjectClass* ProjectClassNamed(const std::string& name) const
			{
				Environment& env = Env();
				auto it = env.classes.find(name);
				if (it == env.classes.end())
					return nullptr;
				// this file's classes are read from the document itself
				std::error_code ec;
				if (!file.empty() && fs::equivalent(it->second.file, file, ec))
					return nullptr;
				return &it->second;
			}

			std::string BaseOf(const std::string& class_name) const
			{
				if (const ClassInfo* c = LocalClass(class_name))
					return c->base;
				if (const ProjectClass* p = ProjectClassNamed(class_name))
					return p->base;
				return "";
			}

			// Type of `this` inside an object of class `base` (engine base classes).
			std::string TypeOfBase(const std::string& base) const
			{
				if (base.empty())
					return "";
				if (LocalClass(base) || ProjectClassNamed(base))
					return "Class:" + base;
				if (base == "UserWidget")
					return "Widget";
				if (base == "BTTask" || base == "BTDecorator" || base == "BTService")
					return "BTNode";
				return "Native:" + base;
			}

			// --- type of a declaration / expression ---------------------------------------
			std::string TypeOfDecl(int d) const
			{
				if (d < 0 || guard > 24)
					return "";
				const Decl& decl = a.decls[d];
				if (decl.kind == DeclKind::Class)
					return "ClassRef:" + decl.name;
				if (decl.init_begin < 0)
					return "";
				++guard;
				const int end = decl.init_end >= 0 ? decl.init_end : Count();
				std::string t = TypeOfExpression(decl.init_begin, end);
				--guard;
				return t;
			}

			int ChainEnd(int begin, int end) const
			{
				int k = begin;
				if (k >= end)
					return k;
				if (Is(k, "new"))
					++k;
				if (Is(k, "(") || Is(k, "[") || Is(k, "{"))
					k = a.match[k] >= 0 ? a.match[k] + 1 : end;
				else
					++k;
				while (k < end)
				{
					if ((Is(k, ".") || Is(k, "?.")) && IsName(k + 1))
						k += 2;
					else if ((Is(k, "(") || Is(k, "[")) && a.match[k] > k)
						k = a.match[k] + 1;
					else
						break;
				}
				return std::min(k, end);
			}

			std::string TypeOfExpression(int begin, int end) const
			{
				while (begin < end && (Is(begin, "await") || Is(begin, "void")))
					++begin;
				if (begin >= end)
					return "";
				return TypeOfChain(begin, ChainEnd(begin, end));
			}

			std::string TypeOfPrimary(int k) const
			{
				const Token& t = Tk(k);
				Environment& env = Env();
				switch (t.type)
				{
				case Tok::String: case Tok::Template: return "String";
				case Tok::Number: return "Number";
				case Tok::Regex: return "RegExp";
				default: break;
				}
				if (Is(k, "["))
					return "Array";
				if (Is(k, "{"))
					return "Object";
				if (Is(k, "this"))
				{
					const int c = a.scopes[a.tok_scope[k]].class_index;
					if (c >= 0 && !a.classes[c].name.empty())
						return "Class:" + a.classes[c].name;
					if (c >= 0)
						return TypeOfBase(a.classes[c].base);
					return "";
				}
				if (Is(k, "("))
					return a.match[k] > k ? TypeOfExpression(k + 1, a.match[k]) : "";
				if (!IsName(k))
					return "";
				if (a.role[k] == Role::Ref || a.role[k] == Role::Decl)
				{
					const int d = a.link[k];
					if (d >= 0)
						return TypeOfDecl(d);
				}
				const std::string& name = t.text;
				if (name == "parent")
					return "Actor";
				if (const ApiEntry* e = Doc("", name))
					if (e->returns[0] && !e->function)
						return e->returns;
				auto g = env.globals.find(name);
				if (g != env.globals.end())
				{
					if (g->second == 'o')
						return "NS:" + name;
					if (g->second == 'c')
						return "ClassRef:" + name;
				}
				if (LocalClass(name) || ProjectClassNamed(name))
					return "ClassRef:" + name;
				return "";
			}

			std::string TypeOfChain(int begin, int end) const
			{
				if (begin >= end || guard > 24)
					return "";
				int k = begin;
				std::string type;
				if (Is(k, "new") && IsName(k + 1))
				{
					const std::string& name = Tk(k + 1).text;
					type = (LocalClass(name) || ProjectClassNamed(name)) ? "Class:" + name : "Inst:" + name;
					k += 2;
				}
				else if (IsName(k) && Is(k + 1, "("))
				{
					// global function call : vec3(...)
					const std::string& name = Tk(k).text;
					const ApiEntry* e = (a.link[k] < 0) ? Doc("", name) : nullptr;
					type = e && e->function ? e->returns : "";
					k += 1;
				}
				else
				{
					type = TypeOfPrimary(k);
					k = (Is(k, "(") || Is(k, "[") || Is(k, "{")) && a.match[k] > k ? a.match[k] + 1 : k + 1;
				}
				while (k < end)
				{
					if ((Is(k, ".") || Is(k, "?.")) && IsName(k + 1))
					{
						const int call = Is(k + 2, "(") ? k + 2 : -1;
						type = MemberType(type, Tk(k + 1).text, call);
						k += 2;
					}
					else if (Is(k, "(") && a.match[k] > k)
						k = a.match[k] + 1;     // call : the type was given by the member
					else if (Is(k, "[") && a.match[k] > k)
					{
						type = "";
						k = a.match[k] + 1;
					}
					else
						break;
				}
				return type;
			}

			std::string ReturnsOf(const ApiEntry* e, int call) const
			{
				if (!e)
					return "";
				std::string r = e->returns;
				if (r == "Component:0")
				{
					if (call >= 0 && call + 1 < Count() && Tk(call + 1).type == Tok::String && Tk(call + 1).text.size() >= 2)
					{
						const std::string& s = Tk(call + 1).text;
						return "Component:" + s.substr(1, s.size() - 2);
					}
					return "Component:";
				}
				return r;
			}

			std::string MemberType(const std::string& owner, const std::string& name, int call) const
			{
				if (owner.empty())
					return "";
				if (StartsWith(owner, "NS:"))
					return ReturnsOf(Doc(owner.substr(3), name), call);
				if (StartsWith(owner, "Component:"))
				{
					const ApiEntry* e = Doc(owner.substr(10), name);
					return ReturnsOf(e ? e : Doc("Component", name), call);
				}
				if (StartsWith(owner, "Native:"))
					return ReturnsOf(Doc("Actor", name), call);
				if (StartsWith(owner, "Class:"))
				{
					const std::string cls = owner.substr(6);
					if (const ClassInfo* c = LocalClass(cls))
					{
						for (const auto& m : c->members)
							if (m.name == name && !m.is_static)
							{
								if (m.method || m.init_begin < 0 || guard > 24)
									return "";
								++guard;
								const int end = m.init_end >= 0 ? m.init_end : Count();
								std::string t = TypeOfExpression(m.init_begin, end);
								--guard;
								return t;
							}
					}
					guard += 1;
					const std::string base = TypeOfBase(BaseOf(cls));
					std::string t = guard > 24 ? "" : MemberType(base, name, call);
					guard -= 1;
					return t;
				}
				if (IsDocType(owner))
					return ReturnsOf(Doc(owner, name), call);
				return "";
			}

			// --- members of a type (completion after ".") ---------------------------------
			void AddEnvMembers(const std::string& key, std::vector<CompletionItem>& out, std::set<std::string>& seen,
			                   const std::string& doc_owner) const
			{
				Environment& env = Env();
				auto it = env.members.find(key);
				if (it == env.members.end())
					return;
				for (const auto& [name, kind] : it->second)
				{
					if (!seen.insert(name).second)
						continue;
					CompletionItem item;
					item.label = name;
					item.kind = kind == 'f' ? Kind::Method : kind == 'c' ? Kind::Class : Kind::Property;
					if (const ApiEntry* e = doc_owner.empty() ? nullptr : Doc(doc_owner, name))
					{
						item.detail = e->signature;
						item.doc = e->doc;
					}
					else
						item.detail = kind == 'f' ? name + "()" : name;
					out.push_back(item);
				}
			}

			void AddDocMembers(const std::string& owner, std::vector<CompletionItem>& out, std::set<std::string>& seen) const
			{
				Environment& env = Env();
				auto it = env.docs.find(owner);
				if (it == env.docs.end())
					return;
				for (const auto& [name, e] : it->second)
				{
					if (!seen.insert(name).second)
						continue;
					CompletionItem item;
					item.label = name;
					item.kind = e->function ? Kind::Method : Kind::Property;
					item.detail = e->signature;
					item.doc = e->doc;
					out.push_back(item);
				}
			}

			void AddClassMembers(const std::string& cls, bool statics, std::vector<CompletionItem>& out,
			                     std::set<std::string>& seen, int depth = 0) const
			{
				if (depth > 12)
					return;
				if (const ClassInfo* c = LocalClass(cls))
				{
					for (const auto& m : c->members)
					{
						if (m.is_static != statics || !seen.insert(m.name).second)
							continue;
						CompletionItem item;
						item.label = m.name;
						item.kind = m.method ? Kind::Method : Kind::Property;
						item.detail = m.method ? m.name + m.params : m.name;
						if (const ApiEntry* e = m.method ? Doc("@lifecycle", m.name) : nullptr)
							item.doc = e->doc;
						item.doc += (item.doc.empty() ? "" : "\n") + std::string("class ") + cls;
						out.push_back(item);
					}
				}
				else if (const ProjectClass* p = ProjectClassNamed(cls))
				{
					for (const auto& m : p->members)
					{
						if (m.is_static != statics || !seen.insert(m.name).second)
							continue;
						CompletionItem item;
						item.label = m.name;
						item.kind = m.method ? Kind::Method : Kind::Property;
						item.detail = m.method ? m.name + m.params : m.name;
						item.doc = "class " + cls + " (" + p->file.filename().string() + ")";
						out.push_back(item);
					}
				}
				if (statics)
					return;
				const std::string base = BaseOf(cls);
				if (base.empty())
					return;
				if (LocalClass(base) || ProjectClassNamed(base))
					AddClassMembers(base, false, out, seen, depth + 1);
				else
					AddTypeMembers(TypeOfBase(base), out, seen);
			}

			void AddTypeMembers(const std::string& type, std::vector<CompletionItem>& out, std::set<std::string>& seen) const
			{
				if (StartsWith(type, "NS:"))
				{
					const std::string ns = type.substr(3);
					AddDocMembers(ns, out, seen);
					AddEnvMembers(ns, out, seen, ns);
				}
				else if (StartsWith(type, "Component:"))
				{
					const std::string c = type.substr(10);
					AddDocMembers(c, out, seen);
					AddDocMembers("Component", out, seen);
				}
				else if (StartsWith(type, "Native:"))
				{
					AddDocMembers("Actor", out, seen);
					AddEnvMembers(type.substr(7) + ".prototype", out, seen, "Actor");
					AddEnvMembers("Actor.prototype", out, seen, "Actor");
				}
				else if (type == "Actor")
				{
					AddDocMembers("Actor", out, seen);
					AddEnvMembers("Actor.prototype", out, seen, "Actor");
				}
				else if (StartsWith(type, "Class:"))
					AddClassMembers(type.substr(6), false, out, seen);
				else if (StartsWith(type, "ClassRef:"))
				{
					AddClassMembers(type.substr(9), true, out, seen);
					AddEnvMembers(type.substr(9), out, seen, "");
				}
				else if (StartsWith(type, "Inst:"))
					AddEnvMembers(type.substr(5) + ".prototype", out, seen, "");
				else if (type == "String" || type == "Array" || type == "Number" || type == "RegExp" || type == "Object")
				{
					AddEnvMembers(type + ".prototype", out, seen, "");
					static const char* const kString[] = { "length", "split", "trim", "toUpperCase", "toLowerCase", "includes",
						"startsWith", "endsWith", "indexOf", "slice", "substring", "replace", "replaceAll", "padStart", "padEnd", "repeat", "at" };
					static const char* const kArray[] = { "length", "push", "pop", "shift", "unshift", "map", "filter", "forEach",
						"find", "findIndex", "includes", "indexOf", "some", "every", "reduce", "slice", "splice", "join", "sort",
						"reverse", "concat", "at", "flat", "fill" };
					static const char* const kNumber[] = { "toFixed", "toString", "toPrecision" };
					auto add = [&](const char* const* names, size_t count)
					{
						for (size_t n = 0; n < count; ++n)
						{
							if (!seen.insert(names[n]).second)
								continue;
							CompletionItem item;
							item.label = names[n];
							item.kind = std::strcmp(names[n], "length") == 0 ? Kind::Property : Kind::Method;
							item.detail = item.kind == Kind::Method ? item.label + "()" : item.label;
							out.push_back(item);
						}
					};
					if (type == "String")
						add(kString, std::size(kString));
					else if (type == "Array")
						add(kArray, std::size(kArray));
					else if (type == "Number")
						add(kNumber, std::size(kNumber));
				}
				else if (IsDocType(type))
					AddDocMembers(type, out, seen);
			}

			// --- the chain before a "." ----------------------------------------------------
			int ChainBegin(int last) const   // `last` : last token of the chain
			{
				int k = last;
				for (int guard_steps = 0; k >= 0 && guard_steps < 256; ++guard_steps)
				{
					if (Is(k, ")") || Is(k, "]"))
					{
						const int open = a.match[k];
						if (open < 0 || open > k)
							return k;
						const int prev = open - 1;
						// a call / an index on something : continue before it
						if (prev >= 0 && (IsName(prev) || Is(prev, ")") || Is(prev, "]") || Is(prev, "this")))
						{
							k = prev;
							continue;
						}
						return open;   // (expression) or [array]
					}
					if (k > 0 && (Is(k - 1, ".") || Is(k - 1, "?.")))
					{
						k -= 2;
						continue;
					}
					if (k > 0 && Is(k - 1, "new"))
						return k - 1;
					return k;
				}
				return std::max(0, k);
			}

			std::string TypeBeforeDot(int dot) const
			{
				if (dot <= 0)
					return "";
				const int begin = ChainBegin(dot - 1);
				return TypeOfChain(begin, dot);
			}

			// --- documentation of a member ---------------------------------------------------
			bool MemberDoc(const std::string& owner, const std::string& name, std::string& signature, std::string& doc) const
			{
				std::string o = owner;
				const ApiEntry* e = nullptr;
				if (StartsWith(o, "NS:"))
					e = Doc(o.substr(3), name);
				else if (StartsWith(o, "Component:"))
				{
					e = Doc(o.substr(10), name);
					if (!e)
						e = Doc("Component", name);
				}
				else if (StartsWith(o, "Native:") || o == "Actor")
					e = Doc("Actor", name);
				else if (StartsWith(o, "Class:") || StartsWith(o, "ClassRef:"))
				{
					const bool statics = StartsWith(o, "ClassRef:");
					std::string cls = o.substr(statics ? 9 : 6);
					for (int depth = 0; depth < 12 && !cls.empty(); ++depth)
					{
						if (const ClassInfo* c = LocalClass(cls))
						{
							for (const auto& m : c->members)
								if (m.name == name && m.is_static == statics)
								{
									signature = std::string(m.method ? "(method) " : "(property) ") + cls + "." + name + (m.method ? m.params : "");
									if (const ApiEntry* l = m.method ? Doc("@lifecycle", name) : nullptr)
										doc = l->doc;
									return true;
								}
						}
						else if (const ProjectClass* p = ProjectClassNamed(cls))
						{
							for (const auto& m : p->members)
								if (m.name == name && m.is_static == statics)
								{
									signature = std::string(m.method ? "(method) " : "(property) ") + cls + "." + name + (m.method ? m.params : "");
									doc = "Defined in " + p->file.filename().string() + ", line " + std::to_string(m.line + 1);
									return true;
								}
						}
						if (statics)
							break;
						const std::string base = BaseOf(cls);
						if (LocalClass(base) || ProjectClassNamed(base))
							cls = base;
						else
						{
							return MemberDoc(TypeOfBase(base), name, signature, doc);
						}
					}
					return false;
				}
				else if (IsDocType(o))
					e = Doc(o, name);
				if (!e)
					return false;
				signature = e->signature;
				doc = e->doc;
				return true;
			}
		};

		// =====================================================================
		// Completion helpers
		// =====================================================================

		// Score of `label` for the typed `prefix` (-1 : no match).
		int MatchScore(const std::string& label, const std::string& prefix)
		{
			if (prefix.empty())
				return 1;
			if (StartsWith(label, prefix))
				return 400 - static_cast<int>(std::min<size_t>(label.size(), 100));
			const std::string l = Lower(label), p = Lower(prefix);
			if (StartsWith(l, p))
				return 300 - static_cast<int>(std::min<size_t>(label.size(), 100));
			// camel humps / subsequence : "gc" -> getComponent
			size_t k = 0;
			int gaps = 0;
			for (size_t i = 0; i < l.size() && k < p.size(); ++i)
			{
				if (l[i] == p[k])
					++k;
				else
					++gaps;
			}
			if (k == p.size() && l[0] == p[0])
				return 150 - std::min(gaps, 100);
			if (l.find(p) != std::string::npos && p.size() >= 2)
				return 100;
			return -1;
		}

		const char* KindWord(DeclKind k)
		{
			switch (k)
			{
			case DeclKind::Var: return "var";
			case DeclKind::Let: return "let";
			case DeclKind::Const: return "const";
			case DeclKind::Function: return "function";
			case DeclKind::Class: return "class";
			case DeclKind::Param: return "(parameter)";
			case DeclKind::CatchParam: return "(exception)";
			case DeclKind::Import: return "import";
			}
			return "";
		}

		Kind ItemKind(DeclKind k)
		{
			switch (k)
			{
			case DeclKind::Const: return Kind::Constant;
			case DeclKind::Function: return Kind::Function;
			case DeclKind::Class: return Kind::Class;
			case DeclKind::Param: case DeclKind::CatchParam: return Kind::Parameter;
			default: return Kind::Variable;
			}
		}

		// Ranges of the parameters in "name(a, b = 1, ...rest)"
		std::vector<std::pair<int, int>> ParamRanges(const std::string& label)
		{
			std::vector<std::pair<int, int>> out;
			const size_t open = label.find('(');
			if (open == std::string::npos)
				return out;
			int depth = 0;
			int start = static_cast<int>(open) + 1;
			for (size_t i = open + 1; i < label.size(); ++i)
			{
				const char c = label[i];
				if (c == '(' || c == '[' || c == '{')
					++depth;
				else if ((c == ')' || c == ']' || c == '}') && depth > 0)
					--depth;
				else if ((c == ',' || c == ')') && depth == 0)
				{
					int a = start, b = static_cast<int>(i);
					while (a < b && label[a] == ' ')
						++a;
					if (b > a)
						out.emplace_back(a, b);
					start = static_cast<int>(i) + 1;
					if (c == ')')
						break;
				}
			}
			return out;
		}
	}


	// =========================================================================
	// Environment
	// =========================================================================

	void SetProjectRoot(const fs::path& root)
	{
		Environment& env = Env();
		if (env.root == root)
			return;
		env.root = root;
		env.classes.clear();
		env.scanned.clear();
	}

	void SetSyntaxChecker(const std::function<bool(const std::string&, const std::string&, std::string&)>& check)
	{
		Env().check_syntax = check;
	}

	namespace
	{
		void ScanProject()
		{
			Environment& env = Env();
			if (env.root.empty())
				return;
			const fs::path assets = env.root / "assets";
			std::error_code ec;
			if (!fs::is_directory(assets, ec))
				return;

			env.widgets.clear();
			env.scripts.clear();
			env.images.clear();
			env.sounds.clear();
			env.animgraphs.clear();
			env.trees.clear();

			std::set<fs::path> alive;
			int files = 0;
			fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec);
			for (; !ec && it != fs::recursive_directory_iterator() && files < 20000; it.increment(ec), ++files)
			{
				std::error_code e2;
				if (!it->is_regular_file(e2))
					continue;
				const fs::path& path = it->path();
				const std::string ext = Lower(path.extension().string());
				const std::string rel = fs::relative(path, assets, e2).generic_string();
				if (ext == ".widget") env.widgets.push_back(rel);
				else if (ext == ".js") env.scripts.push_back(rel);
				else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") env.images.push_back(rel);
				else if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") env.sounds.push_back(rel);
				else if (ext == ".animgraph") env.animgraphs.push_back(rel);
				else if (ext == ".bt") env.trees.push_back(rel);
				if (ext != ".js")
					continue;

				alive.insert(path);
				const auto time = fs::last_write_time(path, e2);
				auto known = env.scanned.find(path);
				if (known != env.scanned.end() && known->second == time)
					continue;
				env.scanned[path] = time;

				// classes of this file
				for (auto c = env.classes.begin(); c != env.classes.end();)
					c = c->second.file == path ? env.classes.erase(c) : std::next(c);
				Analyzer analyzer;
				analyzer.Run(SplitLines(ReadText(path)));
				const Analysis& an = analyzer.a;
				for (const ClassInfo& info : an.classes)
				{
					if (info.name.empty() || info.tok < 0)
						continue;
					ProjectClass pc;
					pc.name = info.name;
					pc.base = info.base;
					pc.file = path;
					pc.line = an.lex.tokens[info.tok].line;
					pc.start = an.lex.tokens[info.tok].start;
					pc.end = an.lex.tokens[info.tok].end;
					for (const auto& m : info.members)
					{
						ProjectClass::Member pm;
						pm.name = m.name;
						pm.method = m.method;
						pm.is_static = m.is_static;
						pm.params = m.params;
						if (m.tok >= 0)
						{
							pm.line = an.lex.tokens[m.tok].line;
							pm.start = an.lex.tokens[m.tok].start;
							pm.end = an.lex.tokens[m.tok].end;
						}
						pc.members.push_back(pm);
					}
					env.classes[pc.name] = pc;
				}
			}
			// deleted files
			for (auto s = env.scanned.begin(); s != env.scanned.end();)
			{
				if (alive.count(s->first))
				{
					++s;
					continue;
				}
				for (auto c = env.classes.begin(); c != env.classes.end();)
					c = c->second.file == s->first ? env.classes.erase(c) : std::next(c);
				s = env.scanned.erase(s);
			}

			// input.json : actions and axes
			env.actions.clear();
			env.axes.clear();
			const nlohmann::json input = nlohmann::json::parse(ReadText(env.root / "input.json"), nullptr, false);
			if (input.is_object())
			{
				if (input.contains("action-mappings") && input["action-mappings"].is_object())
					for (auto m = input["action-mappings"].begin(); m != input["action-mappings"].end(); ++m)
						env.actions.push_back(m.key());
				if (input.contains("axis-mappings") && input["axis-mappings"].is_object())
					for (auto m = input["axis-mappings"].begin(); m != input["axis-mappings"].end(); ++m)
						env.axes.push_back(m.key());
			}
		}
	}

	void RefreshEnvironment(const std::function<bool(const std::string&, std::string&)>& evaluate)
	{
		Environment& env = Env();
		if (evaluate)
		{
			static const char* const kIntrospect = R"JS((() => {
  const kindOf = (d) => !d ? 'p' : (typeof d.value === 'function'
      ? ((/^[A-Z]/.test(d.value.name || '') && d.value.prototype) ? 'c' : 'f')
      : ((d.get || d.set) ? 'g' : 'p'));
  const names = (o) => {
    const out = {};
    for (let p = o; p && p !== Object.prototype && p !== Function.prototype; p = Object.getPrototypeOf(p)) {
      for (const n of Object.getOwnPropertyNames(p)) {
        if (n === 'constructor' || n.startsWith('__') || n in out) continue;
        let d; try { d = Object.getOwnPropertyDescriptor(p, n); } catch (e) {}
        out[n] = kindOf(d);
      }
    }
    return out;
  };
  const res = { globals: {}, members: {}, bases: {} };
  for (const n of Object.getOwnPropertyNames(globalThis)) {
    if (n.startsWith('__')) continue;
    let v, t = 'v';
    try { v = globalThis[n]; } catch (e) { continue; }
    if (typeof v === 'function') t = (v.prototype && /^[A-Z]/.test(n)) ? 'c' : 'f';
    else if (v && typeof v === 'object') t = 'o';
    res.globals[n] = t;
    try {
      if (t === 'o') res.members[n] = names(v);
      else if (t === 'c') {
        res.members[n] = names(v);
        res.members[n + '.prototype'] = names(v.prototype);
        const b = Object.getPrototypeOf(v);
        if (b && b.name) res.bases[n] = b.name;
      }
    } catch (e) {}
  }
  return res;
})())JS";
			std::string json;
			if (evaluate(kIntrospect, json))
			{
				const nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
				if (doc.is_object() && doc.contains("globals") && doc["globals"].is_object() && !doc["globals"].empty())
				{
					std::map<std::string, char> globals;
					for (auto g = doc["globals"].begin(); g != doc["globals"].end(); ++g)
						if (g.value().is_string() && !g.value().get<std::string>().empty())
							globals[g.key()] = g.value().get<std::string>()[0];
					// the engine globals are always known (stubs before the scripts are loaded)
					for (const auto& [name, kind] : env.globals)
						globals.emplace(name, kind);
					env.globals = std::move(globals);
					env.members.clear();
					if (doc.contains("members") && doc["members"].is_object())
						for (auto m = doc["members"].begin(); m != doc["members"].end(); ++m)
						{
							if (!m.value().is_object())
								continue;
							auto& target = env.members[m.key()];
							for (auto n = m.value().begin(); n != m.value().end(); ++n)
								if (n.value().is_string() && !n.value().get<std::string>().empty())
									target[n.key()] = n.value().get<std::string>()[0];
						}
					env.bases.clear();
					if (doc.contains("bases") && doc["bases"].is_object())
						for (auto b = doc["bases"].begin(); b != doc["bases"].end(); ++b)
							if (b.value().is_string())
								env.bases[b.key()] = b.value().get<std::string>();
					env.introspected = true;
				}
			}
		}
		ScanProject();
	}


	// =========================================================================
	// Document
	// =========================================================================

	struct Document::Impl
	{
		Analysis analysis;
		fs::path file;
		std::vector<std::string> lines;
		std::vector<Diagnostic> diagnostics;   // analysis + syntax
		std::vector<Diagnostic> syntax;        // last QuickJS check
	};

	Document::Document() : m(std::make_unique<Impl>()) {}
	Document::~Document() = default;

	namespace
	{
		bool ParseSyntaxError(const std::string& message, const std::string& name, int& line, int& column)
		{
			// "SyntaxError: unexpected token in expression: ')'\n    at Enemy.js:12:5"
			const std::regex position(std::regex_replace(name, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)") + R"(:(\d+):(\d+))");
			std::smatch match;
			if (std::regex_search(message, match, position))
			{
				line = std::stoi(match.str(1));
				column = std::stoi(match.str(2));
				return true;
			}
			static const std::regex any(R"(:(\d+):(\d+))");
			if (std::regex_search(message, match, any))
			{
				line = std::stoi(match.str(1));
				column = std::stoi(match.str(2));
				return true;
			}
			return false;
		}
	}

	void Document::Update(const std::vector<std::string>& lines, const fs::path& file, bool check_syntax)
	{
		m->file = file;
		m->lines = lines;
		Analyzer analyzer;
		analyzer.Run(lines);
		m->analysis = std::move(analyzer.a);

		Environment& env = Env();
		if (check_syntax && env.check_syntax)
		{
			m->syntax.clear();
			std::string code;
			for (size_t i = 0; i < lines.size(); ++i)
			{
				code += lines[i];
				if (i + 1 < lines.size())
					code += '\n';
			}
			std::string error;
			const std::string name = file.filename().string().empty() ? "script.js" : file.filename().string();
			if (!env.check_syntax(code, name, error))
			{
				Diagnostic d;
				d.severity = Severity::Error;
				d.message = error.substr(0, error.find('\n'));
				int line = 0, column = 0;
				if (ParseSyntaxError(error, name, line, column) && line >= 1)
				{
					d.line = d.end_line = std::min(line - 1, static_cast<int>(lines.size()) - 1);
					d.start = std::max(0, column - 1);
					const std::string& text = lines[d.line];
					d.start = std::min(d.start, static_cast<int>(text.size()));
					// the token at this column
					int e = d.start;
					while (e < static_cast<int>(text.size()) && IsIdChar(static_cast<unsigned char>(text[e])))
						++e;
					d.end = e > d.start ? e : std::min(d.start + 1, static_cast<int>(text.size()));
				}
				else
				{
					d.line = d.end_line = 0;
					d.start = 0;
					d.end = lines.empty() ? 0 : static_cast<int>(lines[0].size());
				}
				m->syntax.push_back(d);
			}
		}

		m->diagnostics = m->syntax;
		for (const Diagnostic& d : m->analysis.diagnostics)
		{
			// The syntax error says it already
			bool same = false;
			for (const Diagnostic& s : m->syntax)
				same |= s.line == d.line && d.severity == Severity::Error;
			if (!same)
				m->diagnostics.push_back(d);
		}
		std::sort(m->diagnostics.begin(), m->diagnostics.end(), [](const Diagnostic& x, const Diagnostic& y)
		{
			return x.line != y.line ? x.line < y.line : x.start < y.start;
		});
	}

	const std::vector<Diagnostic>& Document::Diagnostics() const { return m->diagnostics; }

	int Document::ErrorCount() const
	{
		return static_cast<int>(std::count_if(m->diagnostics.begin(), m->diagnostics.end(),
		                                      [](const Diagnostic& d) { return d.severity == Severity::Error; }));
	}

	int Document::WarningCount() const
	{
		return static_cast<int>(std::count_if(m->diagnostics.begin(), m->diagnostics.end(),
		                                      [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
	}

	bool Document::IdentifierAt(int line, int index, int& start, int& end) const
	{
		if (line < 0 || line >= static_cast<int>(m->lines.size()))
			return false;
		const std::string& text = m->lines[line];
		int s = std::min(index, static_cast<int>(text.size()));
		int e = s;
		while (s > 0 && IsIdChar(static_cast<unsigned char>(text[s - 1])))
			--s;
		while (e < static_cast<int>(text.size()) && IsIdChar(static_cast<unsigned char>(text[e])))
			++e;
		if (e <= s || std::isdigit(static_cast<unsigned char>(text[s])))
			return false;
		start = s;
		end = e;
		return true;
	}

	namespace
	{
		bool InComment(const Analysis& a, int line, int index)
		{
			for (const Range& r : a.lex.comments)
				if (Inside(r, line, index) || (r.line == line && r.end_line == line && index > r.start && index <= r.end))
					return true;
			return false;
		}

		// Completion of names inside a string : addComponent("|"), Input.pressed("|")...
		bool StringCompletion(const Queries& q, int str_tok, std::vector<CompletionItem>& items)
		{
			Environment& env = Env();
			const int open = str_tok - 1;
			std::string callee, owner_name, key;
			if (q.Is(open, "(") && q.IsName(open - 1))
			{
				callee = q.Tk(open - 1).text;
				if (q.Is(open - 2, ".") && q.IsName(open - 3))
					owner_name = q.Tk(open - 3).text;
			}
			else if (q.Is(open, ":") && q.IsName(open - 1))
				key = q.Tk(open - 1).text;
			else if (q.Is(open, "=") && q.IsName(open - 1))
				key = q.Tk(open - 1).text;   // sprite.texture = "..."

			auto add = [&](const std::vector<std::string>& names, Kind kind, const char* detail)
			{
				for (const std::string& n : names)
				{
					CompletionItem item;
					item.label = n;
					item.kind = kind;
					item.detail = detail;
					items.push_back(item);
				}
			};
			if (callee == "addComponent" || callee == "getComponent" || callee == "hasComponent" || callee == "removeComponent")
			{
				std::vector<std::string> names(std::begin(kComponentTypes), std::end(kComponentTypes));
				add(names, Kind::Class, "component");
				return true;
			}
			if (owner_name == "Input" && (callee == "pressed" || callee == "held" || callee == "released"))
			{
				add(env.actions, Kind::Value, "action (input.json)");
				return true;
			}
			if (owner_name == "Input" && callee == "axis")
			{
				add(env.axes, Kind::Value, "axis (input.json)");
				return true;
			}
			if (owner_name == "Level" && (callee == "spawn" || callee == "count"))
			{
				std::set<std::string> names;
				for (const auto& [name, kind] : env.globals)
				{
					if (kind != 'c')
						continue;
					// actor classes : Actor and what extends it
					std::string b = name;
					for (int depth = 0; depth < 16 && !b.empty() && b != "Actor"; ++depth)
					{
						auto it = env.bases.find(b);
						b = it == env.bases.end() ? "" : it->second;
					}
					if (b == "Actor")
						names.insert(name);
				}
				for (const auto& [name, pc] : env.classes)
					names.insert(name);
				if (names.empty())
					names = { "Actor", "PointLightActor", "SpotLightActor", "DirectionalLightActor", "SkyLightActor",
					          "SpriteActor", "SoundActor", "ColliderActor" };
				add(std::vector<std::string>(names.begin(), names.end()), Kind::Class, "actor class");
				return true;
			}
			if (callee == "create" || callee == "createWidget")
			{
				add(env.widgets, Kind::Value, "widget");
				return true;
			}
			if (callee == "addScript" || callee == "removeScript" || callee == "hasScript")
			{
				add(env.scripts, Kind::Value, "script");
				return true;
			}
			if (key == "texture" || callee == "setAnimation" || callee == "addAnimation")
			{
				add(env.images, Kind::Value, "image");
				return true;
			}
			if (key == "sound")
			{
				add(env.sounds, Kind::Value, "sound");
				return true;
			}
			if (key == "graph" || callee == "loadGraph")
			{
				add(env.animgraphs, Kind::Value, "anim graph");
				return true;
			}
			if (key == "behaviorTree" || (callee == "load" && !env.trees.empty()))
			{
				add(env.trees, Kind::Value, "behavior tree");
				return true;
			}
			return false;
		}
	}

	Completion Document::Complete(int line, int index, bool explicit_request) const
	{
		Completion out;
		out.line = line;
		const Analysis& a = m->analysis;
		Queries q{ a, m->file };
		if (line < 0 || line >= static_cast<int>(m->lines.size()))
			return out;
		const std::string& text = m->lines[line];
		index = std::min(index, static_cast<int>(text.size()));
		if (InComment(a, line, index))
			return out;

		// --- inside a string ---------------------------------------------------------
		const int at = q.TokenBefore(line, index);
		if (at >= 0)
		{
			const Token& t = a.lex.tokens[at];
			if (t.type == Tok::String && t.line == line && index > t.start && (index < t.end || t.unterminated))
			{
				std::vector<CompletionItem> items;
				if (!StringCompletion(q, at, items))
					return out;
				out.in_string = true;
				out.start = t.start + 1;
				out.end = t.unterminated ? static_cast<int>(text.size()) : t.end - 1;
				const std::string prefix = text.substr(out.start, index - out.start);
				for (CompletionItem& item : items)
				{
					const int score = MatchScore(item.label, prefix);
					if (score < 0)
						continue;
					item.score = score;
					out.items.push_back(item);
				}
				std::stable_sort(out.items.begin(), out.items.end(),
				                 [](const CompletionItem& x, const CompletionItem& y) { return x.score > y.score; });
				return out;
			}
			if ((t.type == Tok::String || t.type == Tok::Template || t.type == Tok::Regex) && t.line <= line &&
			    (line < t.end_line || (line == t.end_line && index < t.end)) && (line > t.line || index > t.start))
				return out;   // other strings : nothing
		}

		// --- the word being typed ----------------------------------------------------------
		int start = index;
		while (start > 0 && IsIdChar(static_cast<unsigned char>(text[start - 1])))
			--start;
		if (start < index && std::isdigit(static_cast<unsigned char>(text[start])))
			return out;   // a number
		if (start > 0 && text[start - 1] == '#')
			--start;
		int end = index;
		while (end < static_cast<int>(text.size()) && IsIdChar(static_cast<unsigned char>(text[end])))
			++end;
		out.start = start;
		out.end = end;
		const std::string prefix = text.substr(start, index - start);

		std::vector<CompletionItem> items;
		std::set<std::string> seen;

		// after "." : members
		int dot_col = start - 1;
		while (dot_col >= 0 && (text[dot_col] == ' ' || text[dot_col] == '\t'))
			--dot_col;
		if (dot_col >= 0 && text[dot_col] == '.' && !(dot_col > 0 && text[dot_col - 1] == '.'))
		{
			const int dot = q.TokenBefore(line, dot_col + 1);
			if (dot < 0 || !(q.Is(dot, ".") || q.Is(dot, "?.")))
				return out;
			const std::string type = q.TypeBeforeDot(dot);
			q.AddTypeMembers(type, items, seen);
			if (items.empty())
			{
				// unknown : the names used after "." in this file
				for (int k = 0; k < static_cast<int>(a.role.size()); ++k)
				{
					if (a.role[k] != Role::Member || k == at || (a.lex.tokens[k].line == line && a.lex.tokens[k].start == start))
						continue;
					const std::string& name = a.lex.tokens[k].text;
					if (!seen.insert(name).second)
						continue;
					CompletionItem item;
					item.label = name;
					item.kind = Kind::Property;
					item.detail = "(used in this file)";
					items.push_back(item);
				}
			}
			for (CompletionItem& item : items)
			{
				const int score = MatchScore(item.label, prefix);
				if (score < 0)
					continue;
				item.score = score + (item.kind == Kind::Method ? 2 : 0);
				out.items.push_back(item);
			}
		}
		else
		{
			if (prefix.empty() && !explicit_request)
				return out;
			// naming something (let x, function f, class C) : nothing to propose
			if (at >= 0)
			{
				const int before = (a.lex.tokens[at].line == line && a.lex.tokens[at].start == start) ? at - 1 : at;
				if (before >= 0 && (q.Is(before, "let") || q.Is(before, "const") || q.Is(before, "var") ||
				                    q.Is(before, "function") || q.Is(before, "class")))
					return out;
			}

			const int scope = q.ScopeAt(line, index);
			// variables visible here (the inner ones first)
			for (int s = scope; s >= 0; s = a.scopes[s].parent)
			{
				for (int d : a.scopes[s].decls)
				{
					const Decl& decl = a.decls[d];
					if (decl.tok >= 0 && a.lex.tokens[decl.tok].line == line && a.lex.tokens[decl.tok].start == start)
						continue;   // the word being typed
					if (!seen.insert(decl.name).second)
						continue;
					CompletionItem item;
					item.label = decl.name;
					item.kind = ItemKind(decl.kind);
					const std::string type = q.TypeOfDecl(d);
					if (decl.kind == DeclKind::Function)
						item.detail = "function " + decl.name + decl.params;
					else if (decl.kind == DeclKind::Class)
						item.detail = "class " + decl.name;
					else
						item.detail = std::string(KindWord(decl.kind)) + " " + decl.name + (type.empty() ? "" : " : " + ShowType(type));
					item.score = 60;
					items.push_back(item);
				}
			}
			// inside a class body : the lifecycle methods
			const int c = a.scopes[scope].class_index;
			const bool member_position = c >= 0 && a.scopes[scope].kind == ScopeKind::Class;
			if (member_position || scope == 0)
			{
				for (const auto& [name, e] : Env().docs["@lifecycle"])
				{
					if (!seen.insert(name).second)
						continue;
					CompletionItem item;
					item.label = name;
					item.kind = member_position ? Kind::Method : Kind::Function;
					item.detail = e->signature;
					item.doc = e->doc;
					item.score = 20;
					items.push_back(item);
				}
			}
			if (!member_position)
			{
				Environment& env = Env();
				for (const auto& [name, kind] : env.globals)
				{
					if (!seen.insert(name).second)
						continue;
					CompletionItem item;
					item.label = name;
					item.kind = kind == 'c' ? Kind::Class : kind == 'f' ? Kind::Function : kind == 'o' ? Kind::Namespace : Kind::Global;
					if (const ApiEntry* e = Doc("", name))
					{
						item.detail = e->signature;
						item.doc = e->doc;
					}
					else
						item.detail = kind == 'c' ? "class " + name : kind == 'f' ? name + "()" : name;
					item.score = 10;
					items.push_back(item);
				}
				for (const auto& [name, pc] : env.classes)
				{
					if (!seen.insert(name).second)
						continue;
					CompletionItem item;
					item.label = name;
					item.kind = Kind::Class;
					item.detail = "class " + name + (pc.base.empty() ? "" : " extends " + pc.base);
					item.doc = pc.file.filename().string();
					item.score = 15;
					items.push_back(item);
				}
				if (seen.insert("parent").second)
				{
					CompletionItem item;
					item.label = "parent";
					item.kind = Kind::Global;
					item.detail = "parent : Actor";
					item.doc = "The actor this script is attached to.";
					item.score = 12;
					items.push_back(item);
				}
			}
			for (const char* k : kKeywords)
			{
				if (!seen.insert(k).second)
					continue;
				CompletionItem item;
				item.label = k;
				item.kind = Kind::Keyword;
				item.score = 0;
				items.push_back(item);
			}
			for (CompletionItem& item : items)
			{
				const int score = MatchScore(item.label, prefix);
				if (score < 0)
					continue;
				item.score += score;
				out.items.push_back(item);
			}
		}

		std::stable_sort(out.items.begin(), out.items.end(), [](const CompletionItem& x, const CompletionItem& y)
		{
			if (x.score != y.score)
				return x.score > y.score;
			return x.label < y.label;
		});
		if (out.items.size() > 300)
			out.items.resize(300);
		// only the word itself : nothing to propose
		if (out.items.size() == 1 && out.items[0].label == prefix && !explicit_request)
			out.items.clear();
		return out;
	}

	bool Document::ShouldTrigger(int line, int index, char typed) const
	{
		if (line < 0 || line >= static_cast<int>(m->lines.size()))
			return false;
		const std::string& text = m->lines[line];
		if (InComment(m->analysis, line, std::min(index, static_cast<int>(text.size()))))
			return false;
		if (typed == '.')
			return !(index >= 2 && (std::isdigit(static_cast<unsigned char>(text[index - 2])) || text[index - 2] == '.'));
		if (typed == '"' || typed == '\'')
			return index >= 2 && (text[index - 2] == '(' || text[index - 2] == ' ' || text[index - 2] == ':');
		if (IsIdStart(static_cast<unsigned char>(typed)))
		{
			// the first letter of a word (not inside a string)
			const int start = index - 1;
			if (start > 0 && IsIdChar(static_cast<unsigned char>(text[start - 1])))
				return false;
			int quotes = 0;
			for (int k = 0; k < start; ++k)
				if (text[k] == '"' || text[k] == '\'' || text[k] == '`')
					++quotes;
			return quotes % 2 == 0;
		}
		return false;
	}

	Hover Document::HoverAt(int line, int index) const
	{
		Hover h;
		const Analysis& a = m->analysis;
		Queries q{ a, m->file };
		const int k = q.TokenAt(line, index);
		if (k < 0)
			return h;
		const Token& t = a.lex.tokens[k];
		if (!(q.IsName(k) || q.Is(k, "this")) || index >= t.end)
			return h;
		h.line = t.line;
		h.start = t.start;
		h.end = t.end;
		const Role role = a.role[k];

		auto decl_hover = [&](int d)
		{
			const Decl& decl = a.decls[d];
			const std::string type = q.TypeOfDecl(d);
			if (decl.kind == DeclKind::Function)
				h.signature = "function " + decl.name + decl.params;
			else if (decl.kind == DeclKind::Class)
			{
				const ClassInfo& c = a.classes[decl.class_index >= 0 ? decl.class_index : 0];
				h.signature = "class " + decl.name + (c.base.empty() ? "" : " extends " + c.base);
			}
			else
				h.signature = std::string(KindWord(decl.kind)) + " " + decl.name + (type.empty() ? "" : " : " + ShowType(type));
			if (decl.tok >= 0)
				h.doc = "Declared line " + std::to_string(a.lex.tokens[decl.tok].line + 1) + ".";
			if (decl.scope == 0 && decl.kind == DeclKind::Function)
				if (const ApiEntry* e = Doc("@lifecycle", decl.name))
					h.doc = e->doc;
			h.valid = true;
		};

		if (q.Is(k, "this"))
		{
			const std::string type = q.TypeOfPrimary(k);
			h.signature = "this" + (type.empty() ? std::string() : " : " + ShowType(type));
			h.valid = true;
			return h;
		}
		if ((role == Role::Ref || role == Role::Decl) && a.link[k] >= 0)
		{
			decl_hover(a.link[k]);
			return h;
		}
		if (role == Role::Decl)
		{
			h.signature = "(parameter) " + t.text;
			h.valid = true;
			return h;
		}
		if (role == Role::Ref)
		{
			const std::string& name = t.text;
			Environment& env = Env();
			if (const ApiEntry* e = Doc("", name))
			{
				h.signature = e->signature;
				h.doc = e->doc;
				h.valid = true;
				return h;
			}
			auto pc = env.classes.find(name);
			if (pc != env.classes.end())
			{
				h.signature = "class " + name + (pc->second.base.empty() ? "" : " extends " + pc->second.base);
				h.doc = "Defined in " + pc->second.file.filename().string() + ", line " + std::to_string(pc->second.line + 1) +
				        " (F12 : go to it).";
				h.valid = true;
				return h;
			}
			auto g = env.globals.find(name);
			if (g != env.globals.end())
			{
				h.signature = g->second == 'c' ? "class " + name : g->second == 'f' ? "function " + name + "()" :
				              g->second == 'o' ? "(namespace) " + name : "(global) " + name;
				auto b = env.bases.find(name);
				if (b != env.bases.end() && !b->second.empty() && b->second != "Object")
					h.signature += " extends " + b->second;
				h.valid = true;
				return h;
			}
			h.signature = name;
			h.doc = "Not defined.";
			h.valid = true;
			return h;
		}
		if (role == Role::Member)
		{
			const std::string owner = q.TypeBeforeDot(k - 1);
			std::string signature, doc;
			if (q.MemberDoc(owner, t.text, signature, doc))
			{
				h.signature = signature;
				h.doc = doc;
				h.valid = true;
				return h;
			}
			Environment& env = Env();
			std::string key = StartsWith(owner, "NS:") ? owner.substr(3) : StartsWith(owner, "Inst:") ? owner.substr(5) + ".prototype" : "";
			auto members = env.members.find(key);
			if (members != env.members.end())
			{
				auto mk = members->second.find(t.text);
				if (mk != members->second.end())
				{
					h.signature = std::string(mk->second == 'f' ? "(method) " : "(property) ") + ShowType(owner) + "." + t.text +
					              (mk->second == 'f' ? "()" : "");
					h.valid = true;
					return h;
				}
			}
			if (!owner.empty())
			{
				h.signature = "(member) " + ShowType(owner) + "." + t.text;
				h.valid = true;
			}
			return h;
		}
		if (role == Role::ClassMember && a.link[k] >= 0)
		{
			const ClassInfo& c = a.classes[a.link[k]];
			for (const auto& mbr : c.members)
				if (mbr.tok == k)
				{
					h.signature = std::string(mbr.method ? "(method) " : "(property) ") + c.name + "." + mbr.name +
					              (mbr.method ? mbr.params : "");
					if (const ApiEntry* e = mbr.method ? Doc("@lifecycle", mbr.name) : nullptr)
						h.doc = e->doc;
					h.valid = true;
				}
			return h;
		}
		return h;
	}

	Signature Document::SignatureAt(int line, int index) const
	{
		Signature s;
		const Analysis& a = m->analysis;
		Queries q{ a, m->file };
		const int last = q.TokenBefore(line, index);
		// the "(" of the call around the cursor
		int depth = 0, commas = 0, open = -1;
		for (int k = last; k >= 0 && k > last - 400; --k)
		{
			const Token& t = a.lex.tokens[k];
			if (t.type != Tok::Punct)
				continue;
			if (t.text == ")" || t.text == "]" || t.text == "}")
			{
				if (k == last && t.line == line && t.end <= index && t.text == ")")
				{
					++depth;
					continue;
				}
				++depth;
			}
			else if (t.text == "(" || t.text == "[" || t.text == "{" || t.text == "${")
			{
				if (depth == 0)
				{
					if (t.text == "(")
					{
						open = k;
						break;
					}
					// a block (function body...) : not inside a call
					if (t.text == "{" && k > 0 && a.tok_scope[k] != a.tok_scope[k - 1])
						break;
					// inside an object / array given as an argument : the call around it
					commas = 0;
					continue;
				}
				--depth;
			}
			else if (t.text == "," && depth == 0)
				++commas;
			else if (t.text == ";" && depth == 0)
				break;
		}
		if (open <= 0)
			return s;
		// closed before the cursor : not in the call any more
		if (a.match[open] >= 0)
		{
			const Token& close = a.lex.tokens[a.match[open]];
			if (close.line < line || (close.line == line && close.start < index))
				return s;
		}
		const int callee = open - 1;
		if (!q.IsName(callee))
			return s;
		const std::string& name = a.lex.tokens[callee].text;
		std::string label, doc;
		if (a.role[callee] == Role::Member)
		{
			const std::string owner = q.TypeBeforeDot(callee - 1);
			std::string signature;
			if (q.MemberDoc(owner, name, signature, doc))
			{
				// "(method) Enemy.TakeDamage(n)" -> "TakeDamage(n)"
				const size_t paren = signature.find('(', signature.find(')') == std::string::npos ? 0 : signature.find(')') + 1);
				label = signature;
				const size_t dot = signature.rfind('.', paren);
				if (StartsWith(signature, "(") && dot != std::string::npos)
					label = signature.substr(dot + 1);
			}
		}
		else if (a.link[callee] >= 0)
		{
			const Decl& d = a.decls[a.link[callee]];
			if (d.kind == DeclKind::Function)
				label = d.name + d.params;
			else if (d.kind == DeclKind::Class && d.class_index >= 0)
			{
				for (const auto& mbr : a.classes[d.class_index].members)
					if (mbr.name == "constructor")
						label = d.name + mbr.params;
			}
			else if (d.init_begin >= 0)
			{
				// const f = (a, b) => ... / function (a, b)
				int k = d.init_begin;
				if (q.Is(k, "async"))
					++k;
				if (q.Is(k, "function"))
				{
					++k;
					if (q.IsName(k))
						++k;
				}
				if (q.Is(k, "(") && a.match[k] > k)
				{
					std::string params;
					for (int p = k; p <= a.match[k]; ++p)
					{
						if (p > k && p - 1 > k && a.lex.tokens[p].text != "," && a.lex.tokens[p].text != ")" &&
						    a.lex.tokens[p - 1].text != "(")
							params += ' ';
						params += a.lex.tokens[p].text;
					}
					label = d.name + params;
				}
			}
		}
		else if (const ApiEntry* e = Doc("", name))
		{
			if (e->function)
			{
				label = e->signature;
				doc = e->doc;
			}
		}
		if (label.empty() || label.find('(') == std::string::npos)
			return s;
		s.valid = true;
		s.label = label;
		s.params = ParamRanges(label);
		s.active = std::min(commas, std::max(0, static_cast<int>(s.params.size()) - 1));
		// "...values" : every argument goes there
		if (!s.params.empty() && commas >= static_cast<int>(s.params.size()))
		{
			const auto& lastp = s.params.back();
			if (label.compare(lastp.first, 3, "...") == 0)
				s.active = static_cast<int>(s.params.size()) - 1;
		}
		s.doc = doc;
		return s;
	}

	Location Document::DefinitionAt(int line, int index) const
	{
		Location loc;
		const Analysis& a = m->analysis;
		Queries q{ a, m->file };
		const int k = q.TokenAt(line, index);
		if (k < 0 || !q.IsName(k))
			return loc;
		const Token& t = a.lex.tokens[k];
		auto at_token = [&](int tok)
		{
			loc.valid = true;
			loc.line = a.lex.tokens[tok].line;
			loc.start = a.lex.tokens[tok].start;
			loc.end = a.lex.tokens[tok].end;
		};
		const Role role = a.role[k];
		if ((role == Role::Ref || role == Role::Decl) && a.link[k] >= 0)
		{
			const Decl& d = a.decls[a.link[k]];
			if (d.tok >= 0)
				at_token(d.tok);
			return loc;
		}
		Environment& env = Env();
		if (role == Role::Ref)
		{
			auto pc = env.classes.find(t.text);
			if (pc != env.classes.end())
			{
				loc.valid = true;
				loc.file = pc->second.file;
				loc.line = pc->second.line;
				loc.start = pc->second.start;
				loc.end = pc->second.end;
			}
			return loc;
		}
		if (role == Role::Member)
		{
			std::string owner = q.TypeBeforeDot(k - 1);
			if (!(StartsWith(owner, "Class:") || StartsWith(owner, "ClassRef:")))
				return loc;
			std::string cls = owner.substr(StartsWith(owner, "Class:") ? 6 : 9);
			for (int depth = 0; depth < 12 && !cls.empty(); ++depth)
			{
				if (const ClassInfo* c = q.LocalClass(cls))
				{
					for (const auto& mbr : c->members)
						if (mbr.name == t.text && mbr.tok >= 0)
						{
							at_token(mbr.tok);
							return loc;
						}
				}
				else if (const ProjectClass* p = q.ProjectClassNamed(cls))
				{
					for (const auto& mbr : p->members)
						if (mbr.name == t.text)
						{
							loc.valid = true;
							loc.file = p->file;
							loc.line = mbr.line;
							loc.start = mbr.start;
							loc.end = mbr.end;
							return loc;
						}
				}
				cls = q.BaseOf(cls);
			}
		}
		return loc;
	}


	// =========================================================================
	// Engine API for Lynxie
	// =========================================================================

	namespace
	{
		std::string LowerAscii(std::string text)
		{
			for (char& c : text)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return text;
		}

		std::vector<std::string> Words(const std::string& text)
		{
			std::vector<std::string> words;
			std::string word;
			for (char c : text)
			{
				if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
					word += c;
				else if (!word.empty())
				{
					words.push_back(word);
					word.clear();
				}
			}
			if (!word.empty())
				words.push_back(word);
			return words;
		}

		int EditDistance(const std::string& a, const std::string& b)
		{
			std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
			for (size_t j = 0; j <= b.size(); ++j)
				prev[j] = static_cast<int>(j);
			for (size_t i = 1; i <= a.size(); ++i)
			{
				cur[0] = static_cast<int>(i);
				for (size_t j = 1; j <= b.size(); ++j)
				{
					const int cost = std::tolower(static_cast<unsigned char>(a[i - 1])) ==
					                 std::tolower(static_cast<unsigned char>(b[j - 1])) ? 0 : 1;
					cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
				}
				std::swap(prev, cur);
			}
			return prev[b.size()];
		}

		// The closest names first (typo, other case, prefix).
		std::vector<std::string> Closest(const std::string& name, std::vector<std::string> candidates, size_t count)
		{
			const std::string lower = LowerAscii(name);
			std::stable_sort(candidates.begin(), candidates.end(), [&](const std::string& x, const std::string& y)
			{
				auto score = [&](const std::string& c)
				{
					const std::string lc = LowerAscii(c);
					int d = EditDistance(lower, lc);
					if (lc.find(lower) != std::string::npos || lower.find(lc) != std::string::npos)
						d -= 3;
					return d;
				};
				return score(x) < score(y);
			});
			if (candidates.size() > count)
				candidates.resize(count);
			return candidates;
		}

		// Every documented + introspected member name of an owner.
		std::vector<std::string> MemberNames(const std::string& owner)
		{
			Environment& env = Env();
			std::set<std::string> names;
			auto d = env.docs.find(owner);
			if (d != env.docs.end())
				for (const auto& [name, entry] : d->second)
					names.insert(name);
			auto m = env.members.find(owner);
			if (m != env.members.end())
				for (const auto& [name, kind] : m->second)
					names.insert(name);
			return std::vector<std::string>(names.begin(), names.end());
		}

		ApiInfo ToInfo(const ApiEntry& e)
		{
			return { e.owner, e.name, e.signature, e.doc, e.function };
		}
	}

	std::vector<ApiInfo> SearchApi(const std::string& query, size_t max_results)
	{
		std::vector<ApiInfo> out;
		const std::vector<std::string> words = Words(LowerAscii(query));

		if (words.empty())
		{
			// The owners : where to look.
			std::set<std::string> owners;
			for (const ApiEntry& e : kApiEntries)
				if (e.owner[0])
					owners.insert(e.owner);
			for (const std::string& owner : owners)
				out.push_back({ owner, "", owner, "", false });
			return out;
		}

		std::vector<std::pair<int, ApiInfo>> scored;
		for (const ApiEntry& e : kApiEntries)
		{
			const std::string owner = LowerAscii(e.owner), name = LowerAscii(e.name), doc = LowerAscii(e.doc);
			int score = 0;
			for (const std::string& w : words)
			{
				if (owner == w) score += 6;
				else if (!owner.empty() && owner.find(w) != std::string::npos) score += 3;
				if (name == w) score += 8;
				else if (name.find(w) != std::string::npos) score += 4;
				if (w.size() >= 4 && doc.find(w) != std::string::npos) score += 1;
			}
			if (score > 0)
				scored.emplace_back(score, ToInfo(e));
		}

		// Introspected members without documentation (the game's own objects...).
		{
			Environment& env = Env();
			std::lock_guard<std::mutex> lock(env.mutex);
			for (const auto& [owner, members] : env.members)
			{
				const std::string lo = LowerAscii(owner);
				for (const auto& [name, kind] : members)
				{
					if (Doc(owner, name))
						continue;
					const std::string ln = LowerAscii(name);
					int score = 0;
					for (const std::string& w : words)
					{
						if (lo == w) score += 5;
						if (ln == w) score += 7;
						else if (ln.find(w) != std::string::npos) score += 3;
					}
					if (score > 0)
						scored.emplace_back(score, ApiInfo{ owner, name, kind == 'f' ? name + "(...)" : name, "", kind == 'f' });
				}
			}
		}

		std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (auto& [score, info] : scored)
		{
			if (out.size() >= max_results)
				break;
			out.push_back(std::move(info));
		}
		return out;
	}

	std::string ApiReference(const std::string& text, size_t max_chars)
	{
		// Owners named in the text (word match, any case) ; always the globals and Actor.
		std::set<std::string> owners_all;
		for (const ApiEntry& e : kApiEntries)
			owners_all.insert(e.owner);

		std::set<std::string> text_words;
		for (const std::string& w : Words(text))
			text_words.insert(LowerAscii(w));

		std::vector<std::string> owners = { "", "Actor" };
		for (const std::string& owner : owners_all)
			if (!owner.empty() && owner != "Actor" && text_words.count(LowerAscii(owner)))
				owners.push_back(owner);

		std::string out;
		std::vector<std::string> left_out;
		for (const std::string& owner : owners)
		{
			std::string block = owner.empty() ? "Globals :\n" : owner + " :\n";
			for (const ApiEntry& e : kApiEntries)
			{
				if (owner != e.owner)
					continue;
				std::string line = "  " + std::string(e.signature);
				if (e.doc[0])
				{
					std::string doc = e.doc;
					const size_t nl = doc.find('\n');
					if (nl != std::string::npos)
						doc.resize(nl);
					if (doc.size() > 110)
						doc = doc.substr(0, 107) + "...";
					line += "  -- " + doc;
				}
				block += line + "\n";
			}
			if (out.size() + block.size() > max_chars)
			{
				left_out.push_back(owner.empty() ? "globals" : owner);
				continue;
			}
			out += block;
		}

		std::string others;
		for (const std::string& owner : owners_all)
			if (!owner.empty() && std::find(owners.begin(), owners.end(), owner) == owners.end())
				others += (others.empty() ? "" : ", ") + owner;
		if (!others.empty())
			out += "Other engine objects / components (members not listed here) : " + others + ".\n";
		if (!left_out.empty())
		{
			out += "Not shown (too long) : ";
			for (size_t i = 0; i < left_out.size(); ++i)
				out += (i ? ", " : "") + left_out[i];
			out += ".\n";
		}
		return out;
	}

	std::vector<std::string> CheckEngineApi(const std::string& code, const fs::path& file, const std::string& previous_code)
	{
		std::vector<std::string> problems;
		if (!Env().introspected)
			return problems;   // the real globals are unknown : nothing reliable to say

		auto collect = [&](const std::string& text)
		{
			std::vector<std::pair<int, std::string>> found;   // line, message
			Document doc;
			doc.Update(SplitLines(text), file, false);
			for (const Diagnostic& d : doc.Diagnostics())
			{
				const bool member = d.message.find("' does not exist on '") != std::string::npos;
				const bool undefined = d.message.find("' is not defined.") != std::string::npos;
				if (member || undefined)
					found.emplace_back(d.line, d.message);
			}
			return found;
		};

		std::multiset<std::string> before;
		if (!previous_code.empty())
			for (const auto& [line, message] : collect(previous_code))
				before.insert(message);

		std::set<std::string> reported;
		for (const auto& [line, message] : collect(code))
		{
			auto it = before.find(message);
			if (it != before.end())
			{
				before.erase(it);   // already there before the edit : not Lynxie's
				continue;
			}
			if (!reported.insert(message).second)
				continue;

			std::string hint;
			const size_t q1 = message.find('\'');
			const size_t q2 = message.find('\'', q1 + 1);
			const std::string name = q1 != std::string::npos && q2 != std::string::npos
				? message.substr(q1 + 1, q2 - q1 - 1) : std::string();
			const size_t on = message.find("' does not exist on '");
			if (on != std::string::npos && !name.empty())
			{
				const size_t o1 = on + std::strlen("' does not exist on '");
				const std::string owner = message.substr(o1, message.find('\'', o1) - o1);
				std::vector<std::string> members;
				{
					std::lock_guard<std::mutex> lock(Env().mutex);
					members = MemberNames(owner);
				}
				const std::vector<std::string> best = Closest(name, members, 8);
				if (!best.empty())
				{
					hint = " Real members of " + owner + " : ";
					for (size_t i = 0; i < best.size(); ++i)
						hint += (i ? ", " : "") + best[i];
					hint += ".";
				}
			}
			else if (!name.empty())
			{
				std::vector<std::string> globals;
				{
					std::lock_guard<std::mutex> lock(Env().mutex);
					for (const auto& [g, kind] : Env().globals)
						globals.push_back(g);
					for (const auto& [c, info] : Env().classes)
						globals.push_back(c);
				}
				const std::vector<std::string> best = Closest(name, globals, 5);
				if (!best.empty())
				{
					hint = " Closest existing names : ";
					for (size_t i = 0; i < best.size(); ++i)
						hint += (i ? ", " : "") + best[i];
					hint += ".";
				}
			}
			problems.push_back("line " + std::to_string(line + 1) + " : " + message + hint);
		}
		return problems;
	}
}
