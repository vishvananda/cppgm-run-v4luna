#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

enum PreprocessingTokenKind
{
	PP_TOKEN_WHITESPACE,
	PP_TOKEN_NEWLINE,
	PP_TOKEN_HEADER_NAME,
	PP_TOKEN_IDENTIFIER,
	PP_TOKEN_NUMBER,
	PP_TOKEN_CHARACTER,
	PP_TOKEN_USER_CHARACTER,
	PP_TOKEN_STRING,
	PP_TOKEN_USER_STRING,
	PP_TOKEN_PUNCTUATOR,
	PP_TOKEN_OTHER,
	PP_TOKEN_EOF
};

// A located phase-4 token. Spellings are owned here because macro expansion
// can synthesize tokens; source-origin fields stay compact and are inherited
// by replacement-list tokens from the invocation head.
struct PreprocessingToken
{
	PreprocessingTokenKind kind;
	std::string spelling;
	std::size_t line;
	std::size_t column;
	std::size_t source_file_id;
	bool leading_space;
	bool placemarker;
	bool paste_result;
	bool macro_generated;
	std::vector<std::size_t> ucn_backslash_offsets;
	std::vector<std::string> unavailable_macros;
	std::size_t nested_context;

	PreprocessingToken()
		: kind(PP_TOKEN_OTHER), line(1), column(1), source_file_id(0),
		  leading_space(false),
		  placemarker(false), paste_result(false), macro_generated(false),
		  nested_context(0) {}
};

// Source paths are interned once per translation unit; each token's
// source_file_id indexes this table.
struct PreprocessedTranslationUnit
{
	std::vector<PreprocessingToken> tokens;
	std::vector<std::string> source_files;
};

// Preprocess one primary source and all of its includes. Macro, conditional,
// and pragma-once state is fresh for each call. The returned tokens are the
// structured output used by the textual PA4 view and by subsequent stages.
bool PreprocessTranslationUnit(const std::string& source,
	const std::string& path, PreprocessedTranslationUnit& output,
	const std::string& build_date, const std::string& build_time);
