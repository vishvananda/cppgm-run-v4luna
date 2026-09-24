// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include "preprocess/preprocessor.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "postprocess/posttoken.h"
#include "preprocess/control_expression.h"
#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/tokens/PPTokenizer.h"

typedef std::pair<unsigned long long, unsigned long long> PreprocessorFileId;
bool GetPreprocessorFileId(const std::string&, PreprocessorFileId&);

namespace
{

bool IsWhitespace(const PreprocessingToken& token)
{
	return token.kind == PP_TOKEN_WHITESPACE || token.kind == PP_TOKEN_NEWLINE;
}

bool IsIdentifier(const PreprocessingToken& token, const std::string& spelling)
{
	return token.kind == PP_TOKEN_IDENTIFIER && token.identifier_spelling &&
		*token.identifier_spelling == spelling;
}

bool IsPunctuator(const PreprocessingToken& token, const std::string& spelling)
{
	return token.kind == PP_TOKEN_PUNCTUATOR && token.spelling == spelling;
}

const std::string& Spelling(const PreprocessingToken& token)
{
	return token.kind == PP_TOKEN_IDENTIFIER && token.identifier_spelling
		? *token.identifier_spelling : token.spelling;
}

struct IdentifierTable
{
	IdentifierTable(std::deque<std::string>& names,
		std::vector<std::size_t>& slots) : names(names), slots(slots) {}
	std::deque<std::string>& names;
	std::vector<std::size_t>& slots;

	void clear()
	{
		names.clear();
		slots.assign(16, 0);
	}

	std::size_t intern(const std::string& spelling)
	{
		if (slots.empty()) slots.assign(16, 0);
		std::size_t slot = findSlot(spelling);
		if (slots[slot] != 0) return slots[slot] - 1;
		if ((names.size() + 1) * 10 >= slots.size() * 7)
		{
			rehash(slots.size() * 2);
			slot = findSlot(spelling);
		}
		names.push_back(spelling);
		const std::size_t id = names.size() - 1;
		slots[slot] = id + 1;
		return id;
	}

	std::size_t findSlot(const std::string& spelling) const
	{
		const std::size_t mask = slots.size() - 1;
		std::size_t slot = std::hash<std::string>()(spelling) & mask;
		while (slots[slot] != 0 && names[slots[slot] - 1] != spelling)
			slot = (slot + 1) & mask;
		return slot;
	}

	void rehash(std::size_t capacity)
	{
		slots.assign(capacity, 0);
		for (std::size_t id = 0; id < names.size(); ++id)
		{
			const std::size_t mask = slots.size() - 1;
			std::size_t slot = std::hash<std::string>()(names[id]) & mask;
			while (slots[slot] != 0) slot = (slot + 1) & mask;
			slots[slot] = id + 1;
		}
	}
};

struct TokenCollector : IPPTokenStream
{
	explicit TokenCollector(IdentifierTable& identifiers)
		: identifiers_(identifiers), line_(1), column_(1), has_space_(false) {}
	std::vector<PreprocessingToken> tokens;
	IdentifierTable& identifiers_;
	std::size_t line_;
	std::size_t column_;
	bool has_space_;

	void set_source_location(std::size_t line, std::size_t column)
	{
		line_ = line;
		column_ = column;
	}

	void emit_whitespace_sequence()
	{
		if (tokens.empty() || tokens.back().kind != PP_TOKEN_WHITESPACE)
		{
			PreprocessingToken token;
			token.kind = PP_TOKEN_WHITESPACE;
			token.line = line_;
			token.column = column_;
			token.leading_space = true;
			tokens.push_back(token);
		}
		has_space_ = true;
	}

	void emit_new_line()
	{
		PreprocessingToken token;
		token.kind = PP_TOKEN_NEWLINE;
		token.line = line_;
		token.column = column_;
		token.leading_space = true;
		tokens.push_back(token);
		has_space_ = true;
	}

	bool emit_comment_new_line()
	{
		emit_whitespace_sequence();
		return true;
	}

	void emit_header_name(const std::string& s) { add(PP_TOKEN_HEADER_NAME, s); }
	void emit_identifier(const std::string& s) { add(PP_TOKEN_IDENTIFIER, s); }
	void emit_pp_number(const std::string& s) { add(PP_TOKEN_NUMBER, s); }
	void emit_character_literal(const std::string& s) { add(PP_TOKEN_CHARACTER, s); }
	void emit_user_defined_character_literal(const std::string& s)
		{ add(PP_TOKEN_USER_CHARACTER, s); }
	void emit_string_literal(const std::string& s) { add(PP_TOKEN_STRING, s); }
	void emit_user_defined_string_literal(const std::string& s)
		{ add(PP_TOKEN_USER_STRING, s); }
	void emit_character_literal(const std::string& s,
		const std::vector<std::size_t>& offsets)
		{ add(PP_TOKEN_CHARACTER, s, offsets); }
	void emit_user_defined_character_literal(const std::string& s,
		const std::vector<std::size_t>& offsets)
		{ add(PP_TOKEN_USER_CHARACTER, s, offsets); }
	void emit_string_literal(const std::string& s,
		const std::vector<std::size_t>& offsets)
		{ add(PP_TOKEN_STRING, s, offsets); }
	void emit_user_defined_string_literal(const std::string& s,
		const std::vector<std::size_t>& offsets)
		{ add(PP_TOKEN_USER_STRING, s, offsets); }
	void emit_preprocessing_op_or_punc(const std::string& s)
	{
		add(PP_TOKEN_PUNCTUATOR, s == "%:%:" ? "##" : s == "%:" ? "#" : s);
	}
	void emit_non_whitespace_char(const std::string& s) { add(PP_TOKEN_OTHER, s); }
	void emit_eof() {}

private:
	void add(PreprocessingTokenKind kind, const std::string& spelling,
		const std::vector<std::size_t>& offsets = std::vector<std::size_t>())
	{
		PreprocessingToken token;
		token.kind = kind;
		if (kind == PP_TOKEN_IDENTIFIER)
		{
			token.identifier_id = identifiers_.intern(spelling);
			token.identifier_spelling = &identifiers_.names[token.identifier_id];
		}
		else
			token.spelling = spelling;
		token.line = line_;
		token.column = column_;
		token.leading_space = has_space_;
		token.ucn_backslash_offsets = offsets;
		tokens.push_back(token);
		has_space_ = false;
	}
};

std::vector<PreprocessingToken> Tokenize(const std::string& source,
	IdentifierTable& identifiers)
{
	TokenCollector collector(identifiers);
	TokenizePreprocessingSource(source, collector);
	return collector.tokens;
}

std::size_t SkipWhitespace(const std::vector<PreprocessingToken>& tokens,
	std::size_t index)
{
	while (index < tokens.size() && IsWhitespace(tokens[index])) ++index;
	return index;
}

std::vector<PreprocessingToken> NonWhitespace(
	const std::vector<PreprocessingToken>& tokens)
{
	std::vector<PreprocessingToken> result;
	for (std::size_t i = 0; i < tokens.size(); ++i)
		if (!IsWhitespace(tokens[i])) result.push_back(tokens[i]);
	return result;
}

bool ContainsName(const std::vector<std::size_t>& names, std::size_t name)
{
	return std::find(names.begin(), names.end(), name) != names.end();
}

void AddName(std::vector<std::size_t>& names, std::size_t name)
{
	if (!ContainsName(names, name)) names.push_back(name);
}

std::vector<std::size_t> UnionNames(const std::vector<std::size_t>& a,
	const std::vector<std::size_t>& b)
{
	std::vector<std::size_t> result = a;
	for (std::size_t i = 0; i < b.size(); ++i) AddName(result, b[i]);
	return result;
}

bool IsAltOperator(const std::string& spelling)
{
	static const char* names[] = {
		"and", "and_eq", "bitand", "bitor", "compl", "not", "not_eq",
		"or", "or_eq", "xor", "xor_eq"
	};
	for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
		if (spelling == names[i]) return true;
	return false;
}

std::string QuoteString(const std::string& value)
{
	std::string result = "\"";
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		if (value[i] == '\\' || value[i] == '"') result.push_back('\\');
		result.push_back(value[i]);
	}
	result.push_back('"');
	return result;
}

void AppendUtf8(std::string& output, unsigned long value)
{
	if (value <= 0x7f) output.push_back(static_cast<char>(value));
	else if (value <= 0x7ff)
	{
		output.push_back(static_cast<char>(0xc0 | (value >> 6)));
		output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
	}
	else if (value <= 0xffff)
	{
		output.push_back(static_cast<char>(0xe0 | (value >> 12)));
		output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
	}
	else if (value <= 0x10ffff)
	{
		output.push_back(static_cast<char>(0xf0 | (value >> 18)));
		output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
	}
	else
		throw std::runtime_error("invalid universal character in string literal");
}

bool HexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		(c >= 'A' && c <= 'F');
}

unsigned long HexValue(char c)
{
	if (c >= '0' && c <= '9') return static_cast<unsigned long>(c - '0');
	if (c >= 'a' && c <= 'f') return static_cast<unsigned long>(c - 'a' + 10);
	return static_cast<unsigned long>(c - 'A' + 10);
}

std::string DecodeOrdinaryString(const std::string& spelling)
{
	const std::size_t quote = spelling.find('"');
	if (quote == std::string::npos || spelling.size() < quote + 2 ||
		spelling[spelling.size() - 1] != '"')
		throw std::runtime_error("expected ordinary string literal");
	std::string result;
	for (std::size_t i = quote + 1; i + 1 < spelling.size(); ++i)
	{
		char c = spelling[i];
		if (c != '\\')
		{
			result.push_back(c);
			continue;
		}
		if (++i + 1 >= spelling.size())
			throw std::runtime_error("invalid string escape");
		c = spelling[i];
		switch (c)
		{
		case '\'': result.push_back('\''); break;
		case '"': result.push_back('"'); break;
		case '?': result.push_back('?'); break;
		case '\\': result.push_back('\\'); break;
		case 'a': result.push_back('\a'); break;
		case 'b': result.push_back('\b'); break;
		case 'f': result.push_back('\f'); break;
		case 'n': result.push_back('\n'); break;
		case 'r': result.push_back('\r'); break;
		case 't': result.push_back('\t'); break;
		case 'v': result.push_back('\v'); break;
		case 'u':
		case 'U':
		{
			const std::size_t digits = c == 'u' ? 4 : 8;
			if (i + digits + 1 >= spelling.size())
				throw std::runtime_error("short universal character escape");
			unsigned long value = 0;
			for (std::size_t n = 0; n < digits; ++n)
			{
				if (!HexDigit(spelling[i + 1 + n]))
					throw std::runtime_error("invalid universal character escape");
				value = (value << 4) | HexValue(spelling[i + 1 + n]);
			}
			AppendUtf8(result, value);
			i += digits;
			break;
		}
		case 'x':
		{
			unsigned long value = 0;
			std::size_t digits = 0;
			while (i + 1 < spelling.size() - 1 && HexDigit(spelling[i + 1]))
			{
				value = (value << 4) | HexValue(spelling[++i]);
				++digits;
			}
			if (digits == 0) throw std::runtime_error("empty hex escape");
			result.push_back(static_cast<char>(value & 0xff));
			break;
		}
		default:
			if (c >= '0' && c <= '7')
			{
				unsigned long value = static_cast<unsigned long>(c - '0');
				for (std::size_t n = 1; n < 3 && i + 1 < spelling.size() - 1 &&
					spelling[i + 1] >= '0' && spelling[i + 1] <= '7'; ++n)
					value = (value << 3) | static_cast<unsigned long>(spelling[++i] - '0');
				result.push_back(static_cast<char>(value & 0xff));
			}
			else
				throw std::runtime_error("invalid string escape");
		}
	}
	return result;
}

std::string DirectoryPart(const std::string& path)
{
	const std::size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

enum BuiltinKind
{
	BUILTIN_NONE,
	BUILTIN_FILE,
	BUILTIN_LINE,
	BUILTIN_DATE,
	BUILTIN_TIME,
	BUILTIN_COUNTER,
	BUILTIN_ATTRIBUTE
};

struct Macro
{
	bool function_like;
	bool variadic;
	BuiltinKind builtin;
	std::size_t id;
	std::vector<std::size_t> parameters;
	std::vector<PreprocessingToken> replacement;

	Macro() : function_like(false), variadic(false), builtin(BUILTIN_NONE),
		id(std::numeric_limits<std::size_t>::max()) {}
};

struct DeferredExpansion
{
	bool active;
	bool saw_open;
	std::size_t scan_index;
	int depth;

	DeferredExpansion() : active(false), saw_open(false), scan_index(0), depth(0) {}
};

enum DeferredScanResult
{
	DEFERRED_WAIT,
	DEFERRED_NOT_CALL,
	DEFERRED_COMPLETE
};

enum PragmaScanPhase
{
	PRAGMA_EXPECT_OPEN,
	PRAGMA_EXPECT_STRING,
	PRAGMA_EXPECT_CLOSE
};

struct DeferredPragma
{
	bool active;
	PragmaScanPhase phase;
	std::size_t scan_index;
	std::size_t literal_index;

	DeferredPragma()
		: active(false), phase(PRAGMA_EXPECT_OPEN), scan_index(0), literal_index(0) {}
};

class Preprocessor
{
public:
	Preprocessor(const std::string& date, const std::string& time)
		: date_(date), time_(time), counter_(0) {}

	void addBuiltinObject(const std::string& name, const std::string& value)
	{
		Macro macro;
		macro.id = ensureMacroId(name);
		PreprocessingToken token;
		token.kind = PP_TOKEN_NUMBER;
		token.spelling = value;
		macro.replacement.push_back(token);
		macros_[macro.id] = macro;
	}

	void initialize()
	{
		identifiers_->clear();
		macros_.clear();
		variadic_parameter_id_ = ensureMacroId("__VA_ARGS__");
		nested_context_nodes_.clear();
		nested_context_nodes_.push_back(NestedContextNode());
		nested_context_slots_.assign(16, 0);
		addBuiltinObject("__CPPGM__", "201303L");
		addBuiltinObject("__cplusplus", "201103L");
		addBuiltinObject("__STDC_HOSTED__", "1");
		addBuiltinObject("__CPPGM_AUTHOR__", "\"Codex\"");
		for (std::unordered_map<std::size_t, Macro>::iterator i = macros_.begin();
			i != macros_.end(); ++i)
			if (i->second.replacement[0].spelling[0] == '"')
				i->second.replacement[0].kind = PP_TOKEN_STRING;
		Macro file; file.builtin = BUILTIN_FILE; file.id = ensureMacroId("__FILE__"); macros_[file.id] = file;
		Macro line; line.builtin = BUILTIN_LINE; line.id = ensureMacroId("__LINE__"); macros_[line.id] = line;
		Macro date; date.builtin = BUILTIN_DATE; date.id = ensureMacroId("__DATE__"); macros_[date.id] = date;
		Macro time; time.builtin = BUILTIN_TIME; time.id = ensureMacroId("__TIME__"); macros_[time.id] = time;
		Macro counter; counter.builtin = BUILTIN_COUNTER; counter.id = ensureMacroId("__COUNTER__"); macros_[counter.id] = counter;
		Macro attribute; attribute.function_like = true;
		attribute.parameters.push_back(ensureMacroId("attribute"));
		attribute.builtin = BUILTIN_ATTRIBUTE;
		attribute.id = ensureMacroId("__has_cpp_attribute");
		macros_[attribute.id] = attribute;
	}

	void process(const std::string& source, const std::string& path,
		IPreprocessedTokenSink& output, PreprocessingMetadata& metadata)
	{
		metadata.source_files.clear();
		IdentifierTable identifiers(metadata.identifiers,
			metadata.identifier_slots);
		identifiers_ = &identifiers;
		metadata_ = &metadata;
		output_ = &output;
		initialize();
		source_file_ids_.clear();
		once_files_.clear();
		include_depth_ = 0;
		processFile(source, path, 0);
	}

private:
	struct FileContext
	{
		std::string path;
		long long line_adjustment;
	};

	struct Conditional
	{
		bool parent_active;
		bool active;
		bool branch_taken;
		bool saw_else;
	};

	struct FileTokenConsumer : IPPTokenStream
	{
		FileTokenConsumer(Preprocessor& owner, FileContext& context,
			std::vector<Conditional>& conditions)
			: owner(owner), context(context), conditions(conditions),
			  current_line(1), current_column(1), has_space(false) {}

		Preprocessor& owner;
		FileContext& context;
		std::vector<Conditional>& conditions;
		std::vector<PreprocessingToken> line;
		std::deque<PreprocessingToken> text;
		std::deque<PreprocessingToken> pragma_tokens;
		DeferredExpansion expansion;
		DeferredPragma pragma_scan;
		std::size_t current_line;
		std::size_t current_column;
		bool has_space;

		void set_source_location(std::size_t line_number, std::size_t column)
		{
			current_line = line_number;
			current_column = column;
		}

		void emit_whitespace_sequence()
		{
			if (line.empty() || line.back().kind != PP_TOKEN_WHITESPACE)
			{
				PreprocessingToken token;
				token.kind = PP_TOKEN_WHITESPACE;
				token.line = current_line;
				token.column = current_column;
				token.leading_space = true;
				line.push_back(token);
			}
			has_space = true;
		}

		void emit_new_line()
		{
			processLine(true);
			has_space = true;
		}

		bool emit_comment_new_line()
		{
			emit_whitespace_sequence();
			return true;
		}

		void emit_header_name(const std::string& value)
			{ add(PP_TOKEN_HEADER_NAME, value); }
		void emit_identifier(const std::string& value)
			{ add(PP_TOKEN_IDENTIFIER, value); }
		void emit_pp_number(const std::string& value)
			{ add(PP_TOKEN_NUMBER, value); }
		void emit_character_literal(const std::string& value)
			{ add(PP_TOKEN_CHARACTER, value); }
		void emit_user_defined_character_literal(const std::string& value)
			{ add(PP_TOKEN_USER_CHARACTER, value); }
		void emit_string_literal(const std::string& value)
			{ add(PP_TOKEN_STRING, value); }
		void emit_user_defined_string_literal(const std::string& value)
			{ add(PP_TOKEN_USER_STRING, value); }
		void emit_character_literal(const std::string& value,
			const std::vector<std::size_t>& offsets)
			{ add(PP_TOKEN_CHARACTER, value, offsets); }
		void emit_user_defined_character_literal(const std::string& value,
			const std::vector<std::size_t>& offsets)
			{ add(PP_TOKEN_USER_CHARACTER, value, offsets); }
		void emit_string_literal(const std::string& value,
			const std::vector<std::size_t>& offsets)
			{ add(PP_TOKEN_STRING, value, offsets); }
		void emit_user_defined_string_literal(const std::string& value,
			const std::vector<std::size_t>& offsets)
			{ add(PP_TOKEN_USER_STRING, value, offsets); }
		void emit_preprocessing_op_or_punc(const std::string& value)
			{ add(PP_TOKEN_PUNCTUATOR, value == "%:%:" ? "##" :
				value == "%:" ? "#" : value); }
		void emit_non_whitespace_char(const std::string& value)
			{ add(PP_TOKEN_OTHER, value); }

		void emit_eof()
		{
			if (!line.empty()) processLine(false);
		}

	private:
		void add(PreprocessingTokenKind kind, const std::string& value,
			const std::vector<std::size_t>& offsets = std::vector<std::size_t>())
		{
			PreprocessingToken token;
			token.kind = kind;
			if (kind == PP_TOKEN_IDENTIFIER)
			{
				token.identifier_id = owner.identifiers_->intern(value);
				token.identifier_spelling =
					&owner.identifiers_->names[token.identifier_id];
			}
			else token.spelling = value;
			token.line = current_line;
			token.column = current_column;
			token.leading_space = has_space;
			token.ucn_backslash_offsets = offsets;
			line.push_back(token);
			has_space = false;
		}

		void processLine(bool has_newline)
		{
			const std::size_t after_line = current_line + 1;
			owner.processLine(line, has_newline, after_line, context,
				conditions, text, pragma_tokens, expansion, pragma_scan);
			line.clear();
		}
	};

	struct NestedContextNode
	{
		std::size_t child[2];
		bool terminal;
		NestedContextNode() : terminal(false)
			{ child[0] = child[1] = 0; }
	};

	std::unordered_map<std::size_t, Macro> macros_;
	std::vector<NestedContextNode> nested_context_nodes_;
	std::vector<std::size_t> nested_context_slots_;
	std::set<PreprocessorFileId> once_files_;
	IPreprocessedTokenSink* output_;
	PreprocessingMetadata* metadata_;
	IdentifierTable* identifiers_;
	std::unordered_map<std::string, std::size_t> source_file_ids_;
	std::string date_;
	std::string time_;
	unsigned long long counter_;
	std::size_t include_depth_;
	std::size_t variadic_parameter_id_;

	std::size_t ensureMacroId(const std::string& name)
	{
		return identifiers_->intern(name);
	}

	bool contextContains(std::size_t context, std::size_t macro_id) const
	{
		if (context >= nested_context_nodes_.size()) return false;
		std::size_t node = context;
		const std::size_t depth = sizeof(std::size_t) * 8;
		for (std::size_t bit = 0; bit < depth; ++bit)
		{
			const std::size_t branch =
				(macro_id >> (depth - bit - 1)) & 1;
			if (node == 0) return false;
				node = nested_context_nodes_[node].child[branch];
		}
		return node != 0 && nested_context_nodes_[node].terminal;
	}

	void rehashContextNodes(std::size_t capacity)
	{
		nested_context_slots_.assign(capacity, 0);
		for (std::size_t id = 1; id < nested_context_nodes_.size(); ++id)
		{
			const NestedContextNode& node = nested_context_nodes_[id];
			std::size_t hash = node.child[0];
			hash ^= node.child[1] + static_cast<std::size_t>(0x9e3779b9) +
				(hash << 6) + (hash >> 2);
			if (node.terminal) hash ^= static_cast<std::size_t>(0x85ebca6b);
			std::size_t slot = hash & (capacity - 1);
			while (nested_context_slots_[slot] != 0)
				slot = (slot + 1) & (capacity - 1);
			nested_context_slots_[slot] = id + 1;
		}
	}

	std::size_t internContextNode(std::size_t child0, std::size_t child1,
		bool terminal)
	{
		if (!terminal && child0 == 0 && child1 == 0) return 0;
		if ((nested_context_nodes_.size() + 1) * 10 >=
			nested_context_slots_.size() * 7)
			rehashContextNodes(nested_context_slots_.size() * 2);
		std::size_t hash = child0;
		hash ^= child1 + static_cast<std::size_t>(0x9e3779b9) +
			(hash << 6) + (hash >> 2);
		if (terminal) hash ^= static_cast<std::size_t>(0x85ebca6b);
		std::size_t slot = hash & (nested_context_slots_.size() - 1);
		while (nested_context_slots_[slot] != 0)
		{
			const std::size_t id = nested_context_slots_[slot] - 1;
			const NestedContextNode& node = nested_context_nodes_[id];
			if (node.child[0] == child0 && node.child[1] == child1 &&
				node.terminal == terminal)
				return id;
			slot = (slot + 1) & (nested_context_slots_.size() - 1);
		}
		NestedContextNode node;
		node.child[0] = child0;
		node.child[1] = child1;
		node.terminal = terminal;
		const std::size_t id = nested_context_nodes_.size();
		nested_context_nodes_.push_back(node);
		nested_context_slots_[slot] = id + 1;
		return id;
	}

	std::size_t addContextMacro(std::size_t root, std::size_t macro_id)
	{
		const std::size_t depth = sizeof(std::size_t) * 8;
		std::size_t path[sizeof(std::size_t) * 8 + 1];
		std::size_t branches[sizeof(std::size_t) * 8];
		path[0] = root;
		std::size_t node = root;
		for (std::size_t bit = 0; bit < depth; ++bit)
		{
			branches[bit] = (macro_id >> (depth - bit - 1)) & 1;
			node = node == 0 ? 0 :
				nested_context_nodes_[node].child[branches[bit]];
			path[bit + 1] = node;
		}
		if (node != 0 && nested_context_nodes_[node].terminal) return root;

		std::size_t replacement = internContextNode(0, 0, true);
		for (std::size_t bit = depth; bit > 0; --bit)
		{
			const NestedContextNode previous = path[bit - 1] == 0
				? NestedContextNode() : nested_context_nodes_[path[bit - 1]];
			std::size_t child0 = previous.child[0];
			std::size_t child1 = previous.child[1];
			if (branches[bit - 1] == 0) child0 = replacement;
			else child1 = replacement;
			replacement = internContextNode(child0, child1, false);
		}
		return replacement;
	}

	std::size_t makeContext(std::size_t parent,
		const std::vector<std::size_t>& added)
	{
		std::size_t root = parent < nested_context_nodes_.size() ? parent : 0;
		for (std::size_t i = 0; i < added.size(); ++i)
			if (added[i] < identifiers_->names.size())
				root = addContextMacro(root, added[i]);
		return root;
	}

	std::size_t directContext(const PreprocessingToken& head,
		std::size_t current_macro, bool detached_invocation)
	{
		// A source suffix can form a function invocation after its head token
		// has been expanded. Keep that suffix outside the earlier nesting chain.
		std::vector<std::size_t> added(1, current_macro);
		for (std::size_t i = 0; i < head.unavailable_macros.size(); ++i)
			added.push_back(head.unavailable_macros[i]);
		return makeContext(detached_invocation ? 0 : head.nested_context, added);
	}

	std::size_t substitutionContext(const PreprocessingToken& token,
		std::size_t current_macro, const PreprocessingToken& head)
	{
		if (token.kind != PP_TOKEN_IDENTIFIER) return 0;
		std::vector<std::size_t> added;
		// A source macro invocation paints its argument with the current macro.
		// For nested expansion tokens, only an ancestor matching this identifier
		// survives parameter substitution.
		if (!head.macro_generated) added.push_back(current_macro);
		if (token.identifier_id != static_cast<std::size_t>(-1))
		{
			if (contextContains(head.nested_context, token.identifier_id) ||
				ContainsName(head.unavailable_macros, token.identifier_id))
				added.push_back(token.identifier_id);
			if (token.identifier_id == current_macro)
				added.push_back(current_macro);
		}
		return makeContext(0, added);
	}

	bool isActive(const std::vector<Conditional>& conditions) const
	{
		return conditions.empty() || conditions.back().active;
	}

	long long logicalLine(const FileContext& context, std::size_t physical) const
	{
		if (physical > static_cast<std::size_t>(LLONG_MAX)) return LLONG_MAX;
		return static_cast<long long>(physical) + context.line_adjustment;
	}

	std::size_t internSourceFile(const std::string& path)
	{
		std::unordered_map<std::string, std::size_t>::const_iterator found =
			source_file_ids_.find(path);
		if (found != source_file_ids_.end()) return found->second;
		const std::size_t id = metadata_->source_files.size();
		metadata_->source_files.push_back(path);
		source_file_ids_[path] = id;
		return id;
	}

	void locateLine(std::vector<PreprocessingToken>& line,
		const FileContext& context)
	{
		const std::size_t file_id = internSourceFile(context.path);
		for (std::size_t i = 0; i < line.size(); ++i)
		{
			line[i].source_file_id = file_id;
			const long long presumed = logicalLine(context, line[i].line);
			if (presumed > 0 && static_cast<unsigned long long>(presumed) <=
				static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
				line[i].line = static_cast<std::size_t>(presumed);
		}
	}

	PreprocessingToken generated(PreprocessingTokenKind kind,
		const std::string& spelling, const PreprocessingToken& head) const
	{
		PreprocessingToken token;
		token.kind = kind;
		token.spelling = spelling;
		token.line = head.line;
		token.column = head.column;
		token.source_file_id = head.source_file_id;
		token.leading_space = head.leading_space;
		token.macro_generated = true;
		return token;
	}

	std::string normalizedPunctuator(const PreprocessingToken& token) const
	{
		if (token.spelling == "%:") return "#";
		if (token.spelling == "%:%:") return "##";
		return token.spelling;
	}

	void processFile(const std::string& source, const std::string& physical_path,
		long long line_adjustment)
	{
		if (++include_depth_ > 256)
			throw std::runtime_error("include nesting exceeds implementation limit");
		FileContext context;
		context.path = physical_path;
		context.line_adjustment = line_adjustment;
		std::vector<Conditional> conditions;
		FileTokenConsumer tokens(*this, context, conditions);
		TokenizePreprocessingSource(source, tokens);
		flushText(tokens.text, tokens.pragma_tokens, tokens.expansion,
			tokens.pragma_scan, context, true);
		if (!conditions.empty())
			throw std::runtime_error("unterminated conditional inclusion group");
		--include_depth_;
	}

	void processLine(std::vector<PreprocessingToken>& line, bool has_newline,
		std::size_t after_line, FileContext& context,
		std::vector<Conditional>& conditions,
		std::deque<PreprocessingToken>& text,
		std::deque<PreprocessingToken>& pragma_tokens,
		DeferredExpansion& expansion, DeferredPragma& pragma_scan)
	{
		locateLine(line, context);
		const std::size_t hash = SkipWhitespace(line, 0);
		const bool directive = hash < line.size() &&
			line[hash].kind == PP_TOKEN_PUNCTUATOR &&
			normalizedPunctuator(line[hash]) == "#";
		if (directive)
		{
			flushText(text, pragma_tokens, expansion, pragma_scan,
				context, true);
			processDirective(line, hash, after_line, context, conditions);
		}
		else if (isActive(conditions))
		{
			for (std::size_t i = 0; i < line.size(); ++i)
				if (IsIdentifier(line[i], "__VA_ARGS__"))
					throw std::runtime_error(
						"__VA_ARGS__ outside a variadic macro");
			for (std::size_t i = 0; i < line.size(); ++i)
			{
				if (IsWhitespace(line[i]) && !text.empty() &&
					IsWhitespace(text.back()))
					continue;
				text.push_back(line[i]);
			}
			if (has_newline)
			{
				PreprocessingToken space;
				space.kind = PP_TOKEN_WHITESPACE;
				const std::size_t physical_line = after_line - 1;
				const long long presumed = logicalLine(context, physical_line);
				space.line = presumed > 0 ? static_cast<std::size_t>(presumed) :
					physical_line;
				space.column = 1;
				space.source_file_id = internSourceFile(context.path);
				space.leading_space = true;
				if (text.empty() || !IsWhitespace(text.back()))
					text.push_back(space);
			}
			flushText(text, pragma_tokens, expansion, pragma_scan,
				context, false);
		}
	}

	void flushText(std::deque<PreprocessingToken>& text,
		std::deque<PreprocessingToken>& pragma_tokens,
		DeferredExpansion& expansion, DeferredPragma& pragma_scan,
		const FileContext& context, bool final)
	{
		const std::vector<PreprocessingToken> expanded =
			expandPending(text, context, expansion, final);
		consumePragmaOperators(pragma_tokens, expanded, pragma_scan,
			context, final);
	}

	void processDirective(const std::vector<PreprocessingToken>& line,
		std::size_t hash, std::size_t after_line, FileContext& context,
		std::vector<Conditional>& conditions)
	{
		std::size_t name_index = SkipWhitespace(line, hash + 1);
		if (name_index == line.size()) return; // null directive
		if (line[name_index].kind != PP_TOKEN_IDENTIFIER)
		{
			if (isActive(conditions))
				throw std::runtime_error("invalid preprocessing directive");
			return;
		}
		const std::string name = *line[name_index].identifier_spelling;
		const std::size_t args = name_index + 1;
		const bool parent_active = isActive(conditions);

		if (name == "if" || name == "ifdef" || name == "ifndef")
		{
			bool condition = false;
			if (parent_active)
			{
				if (name == "if")
					condition = evaluateIf(line, args, context);
				else
				{
					const std::vector<PreprocessingToken> rest(line.begin() + args,
						line.end());
					const std::vector<PreprocessingToken> words = NonWhitespace(rest);
					if (words.size() != 1 || words[0].kind != PP_TOKEN_IDENTIFIER)
						throw std::runtime_error("invalid #ifdef or #ifndef directive");
					condition = macros_.find(words[0].identifier_id) != macros_.end();
					if (name == "ifndef") condition = !condition;
				}
			}
			Conditional item;
			item.parent_active = parent_active;
			item.active = parent_active && condition;
			item.branch_taken = item.active;
			item.saw_else = false;
			conditions.push_back(item);
			return;
		}
		if (name == "elif")
		{
			if (conditions.empty()) throw std::runtime_error("#elif without #if");
			Conditional& item = conditions.back();
			if (item.saw_else) throw std::runtime_error("#elif after #else");
			const bool choose = item.parent_active && !item.branch_taken &&
				evaluateIf(line, args, context);
			item.active = choose;
			item.branch_taken = item.branch_taken || choose;
			return;
		}
		if (name == "else")
		{
			if (conditions.empty()) throw std::runtime_error("#else without #if");
			Conditional& item = conditions.back();
			if (item.saw_else) throw std::runtime_error("duplicate #else");
			item.saw_else = true;
			item.active = item.parent_active && !item.branch_taken;
			item.branch_taken = true;
			return;
		}
		if (name == "endif")
		{
			if (conditions.empty()) throw std::runtime_error("#endif without #if");
			conditions.pop_back();
			return;
		}

		if (!parent_active) return;
		if (name == "define")
			defineMacro(line, args);
		else if (name == "undef")
			undefineMacro(line, args);
		else if (name == "include")
			includeFile(line, args, context);
		else if (name == "line")
			lineControl(line, args, after_line, context);
		else if (name == "error")
			throw std::runtime_error("active #error directive");
		else if (name == "pragma")
			pragma(line, args, context);
		else
			throw std::runtime_error("invalid preprocessing directive");
	}

	void defineMacro(const std::vector<PreprocessingToken>& line, std::size_t args)
	{
		std::size_t i = SkipWhitespace(line, args);
		if (i == line.size() || line[i].kind != PP_TOKEN_IDENTIFIER)
			throw std::runtime_error("#define requires a macro name");
		const std::size_t name_id = line[i].identifier_id;
		if (name_id == variadic_parameter_id_)
			throw std::runtime_error("__VA_ARGS__ cannot be a macro name");
		++i;
		Macro macro;
		if (i < line.size() && IsPunctuator(line[i], "(") &&
			!line[i].leading_space)
		{
			macro.function_like = true;
			++i;
			i = SkipWhitespace(line, i);
			if (i < line.size() && IsPunctuator(line[i], ")"))
				++i;
			else
			{
				bool need_parameter = true;
				for (;;)
				{
					i = SkipWhitespace(line, i);
						if (i >= line.size())
							throw std::runtime_error("unterminated macro parameter list");
						if (IsPunctuator(line[i], "..."))
						{
							if (!need_parameter)
								throw std::runtime_error("invalid variadic macro parameters");
							macro.variadic = true;
							++i;
						i = SkipWhitespace(line, i);
						if (i == line.size() || !IsPunctuator(line[i], ")"))
							throw std::runtime_error("variadic parameter must be last");
						++i;
						break;
					}
						if (line[i].kind != PP_TOKEN_IDENTIFIER)
							throw std::runtime_error("invalid macro parameter");
						const std::size_t parameter = line[i++].identifier_id;
						if (parameter == variadic_parameter_id_)
							throw std::runtime_error("__VA_ARGS__ cannot be a parameter name");
					if (std::find(macro.parameters.begin(), macro.parameters.end(),
						parameter) != macro.parameters.end())
						throw std::runtime_error("duplicate macro parameter");
					macro.parameters.push_back(parameter);
							i = SkipWhitespace(line, i);
					if (i < line.size() && IsPunctuator(line[i], ")"))
					{
						++i;
						break;
					}
					if (i < line.size() && IsPunctuator(line[i], ","))
					{
						++i;
						need_parameter = true;
						std::size_t following = SkipWhitespace(line, i);
						if (following < line.size() &&
							IsPunctuator(line[following], "..."))
						{
							i = following;
							macro.variadic = true;
						++i;
						 i = SkipWhitespace(line, i);
						if (i == line.size() || !IsPunctuator(line[i], ")"))
							throw std::runtime_error("variadic parameter must be last");
						++i;
						break;
						}
						continue;
					}
					throw std::runtime_error("invalid macro parameter list");
				}
			}
		}
		else if (i < line.size() && !IsWhitespace(line[i]))
			throw std::runtime_error("object-like macro replacement requires whitespace");

		while (i < line.size() && IsWhitespace(line[i])) ++i;
		macro.replacement.assign(line.begin() + i, line.end());
		validateReplacement(macro);
		macro.id = name_id;
		std::unordered_map<std::size_t, Macro>::iterator previous =
			macros_.find(name_id);
		if (previous != macros_.end() && !sameDefinition(previous->second, macro))
			throw std::runtime_error("incompatible macro redefinition");
		macros_[name_id] = macro;
	}

	void validateReplacement(const Macro& macro)
	{
		std::vector<PreprocessingToken> words = NonWhitespace(macro.replacement);
		for (std::size_t i = 0; i < words.size(); ++i)
		{
			const std::string punct = normalizedPunctuator(words[i]);
			if (IsIdentifier(words[i], "__VA_ARGS__") && !macro.variadic)
				throw std::runtime_error("__VA_ARGS__ in a non-variadic macro");
			if (punct == "##" && (i == 0 || i + 1 == words.size()))
				throw std::runtime_error("## cannot begin or end a replacement list");
			if (punct == "#" && macro.function_like)
			{
				if (i + 1 == words.size() ||
					words[i + 1].kind != PP_TOKEN_IDENTIFIER ||
						(!isParameter(macro, words[i + 1].identifier_id) &&
						!(macro.variadic &&
							words[i + 1].identifier_id == variadic_parameter_id_)))
					throw std::runtime_error("# must precede a macro parameter");
			}
		}
	}

	bool isParameter(const Macro& macro, std::size_t name) const
	{
		return std::find(macro.parameters.begin(), macro.parameters.end(), name) !=
			macro.parameters.end();
	}

	bool sameDefinition(const Macro& a, const Macro& b) const
	{
		if (a.function_like != b.function_like || a.variadic != b.variadic ||
			a.parameters != b.parameters) return false;
		const std::vector<PreprocessingToken> left = NonWhitespace(a.replacement);
		const std::vector<PreprocessingToken> right = NonWhitespace(b.replacement);
		if (left.size() != right.size()) return false;
		for (std::size_t i = 0; i < left.size(); ++i)
		{
			if (left[i].kind != right[i].kind) return false;
			if (left[i].kind == PP_TOKEN_IDENTIFIER)
			{
				if (left[i].identifier_id != right[i].identifier_id) return false;
			}
			else if (left[i].spelling != right[i].spelling) return false;
			if (i > 0 && left[i].leading_space != right[i].leading_space)
				return false;
		}
		return true;
	}

	void undefineMacro(const std::vector<PreprocessingToken>& line, std::size_t args)
	{
		const std::vector<PreprocessingToken> rest(line.begin() + args, line.end());
		const std::vector<PreprocessingToken> words = NonWhitespace(rest);
		if (words.size() != 1 || words[0].kind != PP_TOKEN_IDENTIFIER)
			throw std::runtime_error("#undef requires one macro name");
		if (words[0].identifier_id == variadic_parameter_id_)
			throw std::runtime_error("__VA_ARGS__ cannot be undefined");
		macros_.erase(words[0].identifier_id);
	}

	std::vector<PreprocessingToken> substitute(const Macro& macro,
		const PreprocessingToken& head,
		const std::vector<std::vector<PreprocessingToken> >& arguments,
		const FileContext& context, bool detached_invocation)
	{
		std::vector<PreprocessingToken> result;
		std::vector<std::vector<PreprocessingToken> > expanded(arguments.size());
		std::vector<bool> has_expanded(arguments.size(), false);
		std::unordered_map<std::size_t, std::size_t> substitution_contexts;
		const std::size_t direct_context = directContext(head, macro.id,
			detached_invocation);
		for (std::size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const PreprocessingToken& token = macro.replacement[i];
			if (IsWhitespace(token))
			{
				if (result.empty() || !IsWhitespace(result.back()))
				{
					PreprocessingToken space = token;
					space.kind = PP_TOKEN_WHITESPACE;
					result.push_back(space);
				}
				continue;
			}
			if (IsPunctuator(token, "##") && macro.variadic)
			{
				std::size_t previous = result.size();
				while (previous > 0 && IsWhitespace(result[previous - 1])) --previous;
				const std::size_t next = SkipWhitespace(macro.replacement, i + 1);
				if (previous > 0 && Spelling(result[previous - 1]) == "," &&
					next < macro.replacement.size() &&
					IsIdentifier(macro.replacement[next], "__VA_ARGS__"))
				{
					const std::size_t variadic_index = parameterIndex(macro,
						variadic_parameter_id_, arguments.size());
					if (!NonWhitespace(arguments[variadic_index]).empty())
						continue; // GNU comma elision leaves the comma for nonempty packs.
				}
			}
			if (IsPunctuator(token, "#") && macro.function_like)
			{
				std::size_t param = SkipWhitespace(macro.replacement, i + 1);
				const std::size_t parameter =
					macro.replacement[param].identifier_id;
				const std::size_t argument_index = parameterIndex(macro, parameter,
					arguments.size());
				const std::string stringized = stringify(arguments[argument_index]);
				PreprocessingToken value = generated(PP_TOKEN_STRING, stringized, head);
				value.unavailable_macros = UnionNames(head.unavailable_macros,
					std::vector<std::size_t>(1, macro.id));
				result.push_back(value);
				i = param;
				continue;
			}
			if (token.kind == PP_TOKEN_IDENTIFIER)
			{
				const std::size_t argument_index = parameterIndex(macro,
					token.identifier_id, arguments.size());
				if (argument_index != arguments.size())
				{
					const bool pasted = adjacentToPaste(macro.replacement, i);
					const std::vector<PreprocessingToken>* value = &arguments[argument_index];
					if (!pasted)
					{
						if (!has_expanded[argument_index])
						{
							expanded[argument_index] = expand(arguments[argument_index], context);
							has_expanded[argument_index] = true;
						}
						value = &expanded[argument_index];
					}
					if (value->empty() && pasted)
					{
						PreprocessingToken marker = generated(PP_TOKEN_OTHER, "", head);
						marker.placemarker = true;
						result.push_back(marker);
					}
					else
					{
						for (std::size_t j = 0; j < value->size(); ++j)
						{
							PreprocessingToken argument_token = (*value)[j];
							argument_token.macro_generated = true;
							argument_token.line = head.line;
							argument_token.column = head.column;
							argument_token.source_file_id = head.source_file_id;
								std::size_t key = std::numeric_limits<std::size_t>::max();
								if (argument_token.kind == PP_TOKEN_IDENTIFIER)
								{
									key = argument_token.identifier_id;
								}
							std::unordered_map<std::size_t, std::size_t>::const_iterator cached =
								substitution_contexts.find(key);
							if (cached == substitution_contexts.end())
								cached = substitution_contexts.insert(std::make_pair(key,
									substitutionContext(argument_token, macro.id, head))).first;
							argument_token.nested_context = cached->second;
							result.push_back(argument_token);
							}
						}
					continue;
				}
				}
				PreprocessingToken copy = token;
				copy.line = head.line;
				copy.column = head.column;
				copy.source_file_id = head.source_file_id;
				copy.nested_context = direct_context;
				copy.macro_generated = true;
			result.push_back(copy);
		}
		applyPastes(result, macro.id, head);
		return result;
	}

	std::size_t parameterIndex(const Macro& macro, std::size_t name,
		std::size_t argument_count) const
	{
		for (std::size_t i = 0; i < macro.parameters.size(); ++i)
			if (macro.parameters[i] == name) return i;
		if (macro.variadic && name == variadic_parameter_id_)
			return argument_count == 0 ? 0 : argument_count - 1;
		return argument_count;
	}

	bool adjacentToPaste(const std::vector<PreprocessingToken>& tokens,
		std::size_t index) const
	{
		std::size_t left = index;
		while (left > 0 && IsWhitespace(tokens[left - 1])) --left;
		if (left > 0 && IsPunctuator(tokens[left - 1], "##")) return true;
		std::size_t right = index + 1;
		while (right < tokens.size() && IsWhitespace(tokens[right])) ++right;
		return right < tokens.size() && IsPunctuator(tokens[right], "##");
	}

	std::string stringify(const std::vector<PreprocessingToken>& argument) const
	{
		std::string body;
		bool pending_space = false;
		bool wrote_token = false;
		for (std::size_t i = 0; i < argument.size(); ++i)
		{
			if (IsWhitespace(argument[i]))
			{
				pending_space = true;
				continue;
			}
			if (wrote_token && (pending_space || argument[i].leading_space))
				body.push_back(' ');
			const bool literal = argument[i].kind == PP_TOKEN_STRING ||
				argument[i].kind == PP_TOKEN_USER_STRING ||
				argument[i].kind == PP_TOKEN_CHARACTER ||
				argument[i].kind == PP_TOKEN_USER_CHARACTER;
			const std::string& spelling = Spelling(argument[i]);
			for (std::size_t j = 0; j < spelling.size(); ++j)
			{
				if (literal && (spelling[j] == '\\' || spelling[j] == '"'))
					body.push_back('\\');
				body.push_back(spelling[j]);
			}
			wrote_token = true;
			pending_space = false;
		}
		return "\"" + body + "\"";
	}

	PreprocessingToken pasteTokens(const PreprocessingToken& a,
		const PreprocessingToken& b, std::size_t macro_id,
		const PreprocessingToken& head)
	{
		PreprocessingToken joined;
		if (a.placemarker && b.placemarker)
		{
			joined = a;
			joined.unavailable_macros = UnionNames(a.unavailable_macros,
				b.unavailable_macros);
		}
		else if (a.placemarker)
			joined = b;
		else if (b.placemarker)
		{
			if (Spelling(a) == ",")
			{
				joined.placemarker = true;
				joined.kind = PP_TOKEN_OTHER;
			}
			else joined = a;
		}
		else
		{
			const std::string spelling = Spelling(a) + Spelling(b);
			const std::vector<PreprocessingToken> pasted = Tokenize(spelling,
				*identifiers_);
			std::vector<PreprocessingToken> words = NonWhitespace(pasted);
			if (words.size() != 1 || words[0].kind == PP_TOKEN_OTHER ||
				words[0].kind == PP_TOKEN_HEADER_NAME)
				throw std::runtime_error("token paste does not form one preprocessing token: " +
					Spelling(a) + " ## " + Spelling(b));
			joined = words[0];
			joined.line = head.line;
			joined.column = head.column;
			joined.unavailable_macros = UnionNames(a.unavailable_macros,
				b.unavailable_macros);
		}
		joined.unavailable_macros = UnionNames(joined.unavailable_macros,
			UnionNames(a.unavailable_macros, b.unavailable_macros));
		joined.macro_generated = true;
		joined.line = head.line;
		joined.column = head.column;
		joined.source_file_id = head.source_file_id;
		joined.nested_context = substitutionContext(joined, macro_id, head);
		joined.paste_result = IsPunctuator(joined, "##");
		return joined;
	}

	void applyPastes(std::vector<PreprocessingToken>& tokens,
		std::size_t macro_id, const PreprocessingToken& head)
	{
		std::vector<PreprocessingToken> result;
		result.reserve(tokens.size());
		for (std::size_t i = 0; i < tokens.size(); ++i)
		{
			if (!IsPunctuator(tokens[i], "##") || tokens[i].paste_result)
			{
				result.push_back(tokens[i]);
				continue;
			}
			std::size_t left = result.size();
			while (left > 0 && IsWhitespace(result[left - 1])) --left;
			std::size_t right = i + 1;
			while (right < tokens.size() && IsWhitespace(tokens[right])) ++right;
			if (left == 0 || right == tokens.size())
				throw std::runtime_error("invalid token paste operands");
			const PreprocessingToken a = result[left - 1];
			const PreprocessingToken b = tokens[right];
			result.erase(result.begin() + left - 1, result.end());
			result.push_back(pasteTokens(a, b, macro_id, head));
			i = right;
		}
		result.erase(std::remove_if(result.begin(), result.end(),
			[](const PreprocessingToken& token) { return token.placemarker; }),
			result.end());
		tokens.swap(result);
	}

	std::vector<PreprocessingToken> expand(
		const std::vector<PreprocessingToken>& input, const FileContext& context)
	{
		std::deque<PreprocessingToken> pending(input.begin(), input.end());
		DeferredExpansion expansion;
		return expandPending(pending, context, expansion, true);
	}

	DeferredScanResult scanDeferred(
		const std::deque<PreprocessingToken>& tail,
		DeferredExpansion& expansion) const
	{
		if (!expansion.saw_open)
		{
			while (expansion.scan_index < tail.size() &&
				IsWhitespace(tail[expansion.scan_index]))
				++expansion.scan_index;
			if (expansion.scan_index == tail.size()) return DEFERRED_WAIT;
			if (!IsPunctuator(tail[expansion.scan_index], "("))
				return DEFERRED_NOT_CALL;
			expansion.saw_open = true;
			expansion.depth = 0;
			++expansion.scan_index;
		}
		while (expansion.scan_index < tail.size())
		{
			const PreprocessingToken& token = tail[expansion.scan_index++];
			if (IsPunctuator(token, "(")) ++expansion.depth;
			else if (IsPunctuator(token, ")"))
			{
				if (expansion.depth == 0) return DEFERRED_COMPLETE;
				--expansion.depth;
			}
		}
		return DEFERRED_WAIT;
	}

	std::vector<PreprocessingToken> expandPending(
		std::deque<PreprocessingToken>& pending, const FileContext& context,
		DeferredExpansion& expansion, bool final)
	{
		std::vector<PreprocessingToken> output;
		bool ready_deferred_invocation = false;
		if (expansion.active)
		{
			if (pending.empty())
				expansion = DeferredExpansion();
			else
			{
				const PreprocessingToken head = pending.front();
				pending.pop_front();
				const DeferredScanResult state = scanDeferred(pending, expansion);
				pending.push_front(head);
				if (state == DEFERRED_WAIT && !final) return output;
				ready_deferred_invocation = state == DEFERRED_COMPLETE;
				expansion = DeferredExpansion();
			}
		}
		while (!pending.empty())
		{
			PreprocessingToken token = pending.front();
			pending.pop_front();
			const bool already_scanned = ready_deferred_invocation;
			ready_deferred_invocation = false;
			if (token.kind != PP_TOKEN_IDENTIFIER ||
				ContainsName(token.unavailable_macros, token.identifier_id))
			{
				if (!token.placemarker) output.push_back(token);
				continue;
			}
			const std::unordered_map<std::size_t, Macro>::const_iterator found =
				macros_.find(token.identifier_id);
			if (found != macros_.end() &&
				contextContains(token.nested_context, token.identifier_id))
			{
				AddName(token.unavailable_macros, token.identifier_id);
				output.push_back(token);
				continue;
			}
			if (found == macros_.end())
			{
				output.push_back(token);
				continue;
			}
			const Macro& macro = found->second;
			std::vector<std::vector<PreprocessingToken> > arguments;
			bool detached_invocation = false;
			if (macro.function_like)
			{
				std::size_t open_at = 0;
				while (open_at < pending.size() && IsWhitespace(pending[open_at]))
					++open_at;
				if (open_at == pending.size())
				{
					if (!final)
					{
						expansion.active = true;
						expansion.scan_index = open_at;
						pending.push_front(token);
						break;
					}
					output.push_back(token);
					continue;
				}
				if (!IsPunctuator(pending[open_at], "("))
				{
					output.push_back(token);
					continue;
				}
				if (!final && !already_scanned)
				{
					DeferredExpansion probe;
					probe.active = true;
					probe.saw_open = true;
					probe.scan_index = open_at + 1;
					if (scanDeferred(pending, probe) == DEFERRED_WAIT)
					{
						expansion = probe;
						pending.push_front(token);
						break;
					}
				}
				detached_invocation = token.nested_context != 0 &&
					pending[open_at].nested_context == 0;
				for (std::size_t i = 0; i <= open_at; ++i)
					pending.pop_front();
				arguments = collectArguments(pending);
				validateArgumentCount(macro, arguments);
				if (macro.builtin == BUILTIN_ATTRIBUTE)
				{
					const bool known = attributeValue(arguments[0]) != 0;
					PreprocessingToken value = generated(PP_TOKEN_NUMBER,
						known ? "201803" : "0", token);
					value.unavailable_macros = UnionNames(token.unavailable_macros,
						std::vector<std::size_t>(1, macro.id));
					pending.push_front(value);
					continue;
				}
			}
			std::vector<PreprocessingToken> replacement;
			if (macro.builtin != BUILTIN_NONE &&
				macro.builtin != BUILTIN_ATTRIBUTE)
				replacement = expandBuiltin(macro.builtin, token, context);
			else
				replacement = substitute(macro, token,
					arguments, context, detached_invocation);
			for (std::vector<PreprocessingToken>::reverse_iterator i =
				replacement.rbegin(); i != replacement.rend(); ++i)
				pending.push_front(*i);
		}
		return output;
	}

	std::vector<std::vector<PreprocessingToken> > collectArguments(
		std::deque<PreprocessingToken>& pending)
	{
		std::vector<std::vector<PreprocessingToken> > arguments(1);
		int depth = 0;
		for (;;)
		{
			if (pending.empty())
				throw std::runtime_error("unterminated function-like macro invocation");
			PreprocessingToken token = pending.front();
			pending.pop_front();
			if (IsPunctuator(token, "("))
			{
				++depth;
				arguments.back().push_back(token);
			}
			else if (IsPunctuator(token, ")"))
			{
				if (depth == 0)
				{
					if (arguments.size() == 1 && NonWhitespace(arguments[0]).empty())
						arguments.clear();
					break;
				}
				--depth;
				arguments.back().push_back(token);
			}
			else if (IsPunctuator(token, ",") && depth == 0)
			{
				arguments.push_back(std::vector<PreprocessingToken>());
			}
			else
				arguments.back().push_back(token);
		}
		return arguments;
	}

	void validateArgumentCount(const Macro& macro,
		std::vector<std::vector<PreprocessingToken> >& arguments) const
	{
		const std::size_t fixed = macro.parameters.size();
		if (arguments.empty() && fixed == 1 && !macro.variadic)
			arguments.push_back(std::vector<PreprocessingToken>());
		if (macro.variadic)
		{
			if (arguments.size() < fixed)
				throw std::runtime_error("too few macro arguments");
			std::vector<PreprocessingToken> rest;
			for (std::size_t i = fixed; i < arguments.size(); ++i)
			{
				if (i != fixed)
				{
					PreprocessingToken comma;
					comma.kind = PP_TOKEN_PUNCTUATOR;
					comma.spelling = ",";
					rest.push_back(comma);
				}
				rest.insert(rest.end(), arguments[i].begin(), arguments[i].end());
			}
			arguments.resize(fixed + 1);
			arguments[fixed] = rest;
		}
		else if (arguments.size() != fixed)
			throw std::runtime_error("incorrect number of macro arguments");
	}

	unsigned long long attributeValue(
		const std::vector<PreprocessingToken>& argument) const
	{
		std::string name;
		for (std::size_t i = 0; i < argument.size(); ++i)
			if (!IsWhitespace(argument[i])) name += Spelling(argument[i]);
		return name == "no_unique_address" || name == "__no_unique_address__"
			? 201803ULL : 0ULL;
	}

	std::vector<PreprocessingToken> expandBuiltin(BuiltinKind builtin,
		const PreprocessingToken& head, const FileContext& context)
	{
		std::vector<PreprocessingToken> result;
		if (builtin == BUILTIN_FILE)
			result.push_back(generated(PP_TOKEN_STRING, QuoteString(context.path), head));
		else if (builtin == BUILTIN_LINE)
		{
			std::ostringstream value;
			value << head.line;
			result.push_back(generated(PP_TOKEN_NUMBER, value.str(), head));
		}
		else if (builtin == BUILTIN_DATE)
			result.push_back(generated(PP_TOKEN_STRING, QuoteString(date_), head));
		else if (builtin == BUILTIN_TIME)
			result.push_back(generated(PP_TOKEN_STRING, QuoteString(time_), head));
		else if (builtin == BUILTIN_COUNTER)
		{
			std::ostringstream value;
			value << counter_++;
			result.push_back(generated(PP_TOKEN_NUMBER, value.str(), head));
		}
		return result;
	}

	bool evaluateIf(const std::vector<PreprocessingToken>& line,
		std::size_t begin, const FileContext& context)
	{
		std::vector<PreprocessingToken> expression(line.begin() + begin, line.end());
		for (std::size_t i = 0; i < expression.size(); ++i)
			if (IsIdentifier(expression[i], "__VA_ARGS__"))
				throw std::runtime_error("__VA_ARGS__ outside a variadic macro");
		expression = replaceDefined(expression);
		expression = expand(expression, context);
		for (std::size_t i = 0; i < expression.size(); ++i)
		{
			if (expression[i].kind == PP_TOKEN_IDENTIFIER &&
				!IsIdentifier(expression[i], "true") &&
				!IsIdentifier(expression[i], "false") &&
				!IsAltOperator(Spelling(expression[i])))
				{
					expression[i].kind = PP_TOKEN_NUMBER;
					expression[i].spelling = "0";
					expression[i].identifier_id = static_cast<std::size_t>(-1);
					expression[i].identifier_spelling = 0;
				}
		}
		std::ostringstream evaluated;
		EvaluateControlExpressionTokens(expression, evaluated);
		const std::string value = evaluated.str();
		if (value.empty() || value.find("error") != std::string::npos)
			throw std::runtime_error("invalid controlling expression");
		std::istringstream parsed(value);
		long long result = 0;
		parsed >> result;
		if (!parsed) throw std::runtime_error("invalid controlling expression result");
		return result != 0;
	}

	std::vector<PreprocessingToken> replaceDefined(
		const std::vector<PreprocessingToken>& input)
	{
		std::vector<PreprocessingToken> output;
		for (std::size_t i = 0; i < input.size(); ++i)
		{
			if (!IsIdentifier(input[i], "defined"))
			{
				output.push_back(input[i]);
				continue;
			}
			std::size_t operand = SkipWhitespace(input, i + 1);
			bool paren = operand < input.size() && IsPunctuator(input[operand], "(");
			if (paren) operand = SkipWhitespace(input, operand + 1);
			if (operand == input.size() ||
				(input[operand].kind != PP_TOKEN_IDENTIFIER &&
				 !(input[operand].kind == PP_TOKEN_PUNCTUATOR &&
					IsAltOperator(input[operand].spelling))))
				throw std::runtime_error("defined requires an identifier");
			const bool is_defined = input[operand].kind == PP_TOKEN_IDENTIFIER &&
				macros_.find(input[operand].identifier_id) != macros_.end();
			std::size_t last = operand;
			if (paren)
			{
				last = SkipWhitespace(input, operand + 1);
				if (last == input.size() || !IsPunctuator(input[last], ")"))
					throw std::runtime_error("defined parenthesis is not closed");
			}
			PreprocessingToken value = input[i];
			value.kind = PP_TOKEN_NUMBER;
			value.spelling = is_defined ? "1" : "0";
			value.identifier_id = static_cast<std::size_t>(-1);
			value.identifier_spelling = 0;
			output.push_back(value);
			i = last;
		}
		return output;
	}

	void includeFile(const std::vector<PreprocessingToken>& line,
		std::size_t args, const FileContext& context)
	{
		std::vector<PreprocessingToken> expanded = expand(
			std::vector<PreprocessingToken>(line.begin() + args, line.end()), context);
		const std::vector<PreprocessingToken> words = NonWhitespace(expanded);
		if (words.size() != 1 || (words[0].kind != PP_TOKEN_HEADER_NAME &&
			words[0].kind != PP_TOKEN_STRING))
			throw std::runtime_error("invalid #include operand");
		std::string nextf;
		if (words[0].kind == PP_TOKEN_HEADER_NAME)
		{
			if (words[0].spelling.size() < 3)
				throw std::runtime_error("invalid header name");
			nextf = words[0].spelling.substr(1, words[0].spelling.size() - 2);
		}
		else
			nextf = DecodeOrdinaryString(words[0].spelling);
		const std::string pathrel = DirectoryPart(context.path) + nextf;
		std::string selected;
		std::ifstream candidate(pathrel.c_str(), std::ios::binary);
		if (candidate) selected = pathrel;
		else
		{
			std::ifstream fallback(nextf.c_str(), std::ios::binary);
			if (fallback) selected = nextf;
		}
		if (selected.empty()) throw std::runtime_error("include file not found");
		PreprocessorFileId file_id;
		if (GetPreprocessorFileId(selected, file_id) &&
			once_files_.find(file_id) != once_files_.end()) return;
		std::ifstream input(selected.c_str(), std::ios::binary);
		if (!input) throw std::runtime_error("unable to read include file");
		const std::string content((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		if (input.bad()) throw std::runtime_error("failed to read include file");
		processFile(content, selected, 0);
	}

	void lineControl(const std::vector<PreprocessingToken>& line,
		std::size_t args, std::size_t after_line, FileContext& context)
	{
		const std::vector<PreprocessingToken> expanded = NonWhitespace(expand(
			std::vector<PreprocessingToken>(line.begin() + args, line.end()), context));
		if (expanded.empty() || expanded.size() > 2 ||
			expanded[0].kind != PP_TOKEN_NUMBER)
			throw std::runtime_error("invalid #line directive");
		PPIntegralLiteral parsed;
		if (!ParsePPIntegralLiteral(expanded[0].spelling, parsed) ||
			parsed.value == 0 || parsed.value > static_cast<std::uint64_t>(LLONG_MAX))
			throw std::runtime_error("invalid #line number");
		if (expanded.size() == 2)
		{
			if (expanded[1].kind != PP_TOKEN_STRING)
				throw std::runtime_error("invalid #line filename");
			context.path = DecodeOrdinaryString(expanded[1].spelling);
		}
		context.line_adjustment = static_cast<long long>(parsed.value) -
			static_cast<long long>(after_line);
	}

	void pragma(const std::vector<PreprocessingToken>& line, std::size_t args,
		const FileContext& context)
	{
		const std::vector<PreprocessingToken> words = NonWhitespace(
			std::vector<PreprocessingToken>(line.begin() + args, line.end()));
		if (words.size() == 1 && IsIdentifier(words[0], "once"))
		{
			PreprocessorFileId id;
			if (GetPreprocessorFileId(context.path, id)) once_files_.insert(id);
			return;
		}
		if (!words.empty() && IsIdentifier(words[0], "cppgm_mock_unknown")) return;
		if (words.empty()) return;
		throw std::runtime_error("unsupported pragma");
	}

	void consumePragmaOperators(
		std::deque<PreprocessingToken>& pending,
		const std::vector<PreprocessingToken>& tokens,
		DeferredPragma& scan, const FileContext& context, bool final)
	{
		for (std::size_t i = 0; i < tokens.size(); ++i)
		{
			if (scan.active && IsWhitespace(tokens[i]) && !pending.empty() &&
				IsWhitespace(pending.back()))
				continue;
			pending.push_back(tokens[i]);
		}
		while (!pending.empty())
		{
			if (!IsIdentifier(pending.front(), "_Pragma"))
			{
				output_->emit_preprocessed_token(pending.front());
				pending.pop_front();
				continue;
			}
			if (!scan.active)
			{
				scan.active = true;
				scan.phase = PRAGMA_EXPECT_OPEN;
				scan.scan_index = 1;
			}
			bool complete = false;
			while (!complete)
			{
				if (scan.phase == PRAGMA_EXPECT_OPEN)
				{
					while (scan.scan_index < pending.size() &&
						IsWhitespace(pending[scan.scan_index]))
						++scan.scan_index;
					if (scan.scan_index == pending.size())
					{
						if (!final) return;
						throw std::runtime_error(
							"_Pragma must be followed by a string operand");
					}
					if (!IsPunctuator(pending[scan.scan_index], "("))
						throw std::runtime_error(
							"_Pragma must be followed by a string operand");
					++scan.scan_index;
					scan.phase = PRAGMA_EXPECT_STRING;
				}
				else if (scan.phase == PRAGMA_EXPECT_STRING)
				{
					while (scan.scan_index < pending.size() &&
						IsWhitespace(pending[scan.scan_index]))
						++scan.scan_index;
					if (scan.scan_index == pending.size())
					{
						if (!final) return;
						throw std::runtime_error("_Pragma requires a string literal");
					}
					if (pending[scan.scan_index].kind != PP_TOKEN_STRING)
						throw std::runtime_error("_Pragma requires a string literal");
					scan.literal_index = scan.scan_index++;
					scan.phase = PRAGMA_EXPECT_CLOSE;
				}
				else
				{
					while (scan.scan_index < pending.size() &&
						IsWhitespace(pending[scan.scan_index]))
						++scan.scan_index;
					if (scan.scan_index == pending.size())
					{
						if (!final) return;
						throw std::runtime_error(
							"_Pragma invocation is not closed");
					}
					if (!IsPunctuator(pending[scan.scan_index], ")"))
						throw std::runtime_error(
							"_Pragma invocation is not closed");
					const std::string body = DecodeOrdinaryString(
						pending[scan.literal_index].spelling);
					std::istringstream pragma_input(body);
					std::string pragma_name;
					pragma_input >> pragma_name;
					if (pragma_name == "once")
					{
						PreprocessorFileId id;
						if (GetPreprocessorFileId(context.path, id))
							once_files_.insert(id);
					}
					else if (pragma_name != "cppgm_mock_unknown")
						throw std::runtime_error("unsupported _Pragma operand");
					for (std::size_t i = 0; i <= scan.scan_index; ++i)
						pending.pop_front();
					scan = DeferredPragma();
					complete = true;
				}
			}
		}
	}


};

} // namespace

bool PreprocessTranslationUnit(const std::string& source, const std::string& path,
	IPreprocessedTokenSink& output, PreprocessingMetadata& metadata,
	const std::string& build_date, const std::string& build_time)
{
	Preprocessor preprocessor(build_date, build_time);
	preprocessor.process(source, path, output, metadata);
	return true;
}
