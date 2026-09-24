#pragma once

#include <cstddef>
#include <deque>
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

// A located phase-4 token. Identifier spellings are interned once per
// translation unit; identifier_spelling is non-owning and remains valid while
// the associated PreprocessingMetadata lives. Other spellings are owned
// because macro expansion can synthesize them.
struct PreprocessingToken
{
	PreprocessingTokenKind kind;
	std::string spelling;
	std::size_t identifier_id;
	const std::string* identifier_spelling;
	std::size_t line;
	std::size_t column;
	std::size_t source_file_id;
	bool leading_space;
	bool placemarker;
	bool paste_result;
	bool macro_generated;
	std::vector<std::size_t> ucn_backslash_offsets;
	std::vector<std::size_t> unavailable_macros;
	std::size_t nested_context;

	PreprocessingToken()
		: kind(PP_TOKEN_OTHER), identifier_id(static_cast<std::size_t>(-1)),
		  identifier_spelling(0), line(1), column(1), source_file_id(0),
		  leading_space(false),
		  placemarker(false), paste_result(false), macro_generated(false),
		  nested_context(0) {}
};

// Stable backing storage for token identity and source locations. The deque
// owns each spelling once; identifier_slots is a flat open-addressed index
// from spelling hashes to compact IDs. Deque element addresses remain stable
// as names are interned. Metadata must outlive every token that refers to it.
struct PreprocessingMetadata
{
	PreprocessingMetadata() : identifier_slots(16, 0) {}
	PreprocessingMetadata(const PreprocessingMetadata&) = delete;
	PreprocessingMetadata& operator=(const PreprocessingMetadata&) = delete;

	std::deque<std::string> identifiers;
	std::vector<std::size_t> identifier_slots;
	std::vector<std::string> source_files;
};

struct IPreprocessedTokenSink
{
	virtual void emit_preprocessed_token(const PreprocessingToken&) = 0;
	virtual ~IPreprocessedTokenSink() {}
};

// Pull adapter for the streaming preprocessor. The source and metadata must
// outlive the cursor. The implementation bounds producer lookahead while
// keeping macro/include state inside one translation unit.
class PreprocessedTokenCursor
{
public:
	PreprocessedTokenCursor(const std::string& source, const std::string& path,
		PreprocessingMetadata& metadata, const std::string& build_date,
		const std::string& build_time);
	~PreprocessedTokenCursor();
	PreprocessedTokenCursor(const PreprocessedTokenCursor&) = delete;
	PreprocessedTokenCursor& operator=(const PreprocessedTokenCursor&) = delete;

	// Returns false at end of the primary translation unit; preprocessing
	// failures are rethrown after all already-produced tokens are consumed.
	bool next(PreprocessingToken& token);

private:
	struct Impl;
	Impl* impl_;
};

// Preprocess one primary source and all of its includes into a streaming
// token consumer. Macro, conditional, and pragma-once state is fresh for each
// call. The call resets metadata, so it must be dedicated to this translation
// unit and outlive every token retained by the consumer.
bool PreprocessTranslationUnit(const std::string& source,
	const std::string& path, IPreprocessedTokenSink& output,
	PreprocessingMetadata& metadata,
	const std::string& build_date, const std::string& build_time);
