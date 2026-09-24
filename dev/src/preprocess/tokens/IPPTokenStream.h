#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct IPPTokenStream
{
	// Spellings are borrowed callback data. A consumer that needs a token
	// after its callback returns must copy or otherwise retain its own facts.
	// Location-aware consumers may override this hook. It is called before
	// each spelling or new-line event and reports the physical source line of
	// that event; legacy PA1-PA4 consumers intentionally need no location state.
	virtual void set_source_line(std::size_t line) { (void)line; }
	virtual void set_source_location(std::size_t line, std::size_t column)
		{ set_source_line(line); (void)column; }
	virtual void emit_whitespace_sequence() = 0;
	virtual void emit_new_line() = 0;
	// A newline inside a block comment is still visible to location tracking,
	// while preprocessing clients may treat the comment as a whitespace run.
	virtual bool emit_comment_new_line() { emit_new_line(); return false; }
	virtual void emit_header_name(const std::string& data) = 0;
	virtual void emit_identifier(const std::string& data) = 0;
	virtual void emit_pp_number(const std::string& data) = 0;
	virtual void emit_character_literal(const std::string& data) = 0;
	virtual void emit_user_defined_character_literal(const std::string& data) = 0;
	virtual void emit_string_literal(const std::string& data) = 0;
	virtual void emit_user_defined_string_literal(const std::string& data) = 0;
	// Literal spellings expose decoded UCNs to the PA1 output contract. Keep
	// offsets for UCNs that decode to backslash so later literal conversion
	// does not mistake them for the start of an escape sequence. The defaults
	// preserve older consumers that only need the decoded spelling.
	virtual void emit_character_literal(const std::string& data,
		const std::vector<std::size_t>& ucn_backslash_offsets)
		{ (void)ucn_backslash_offsets; emit_character_literal(data); }
	virtual void emit_user_defined_character_literal(const std::string& data,
		const std::vector<std::size_t>& ucn_backslash_offsets)
		{ (void)ucn_backslash_offsets; emit_user_defined_character_literal(data); }
	virtual void emit_string_literal(const std::string& data,
		const std::vector<std::size_t>& ucn_backslash_offsets)
		{ (void)ucn_backslash_offsets; emit_string_literal(data); }
	virtual void emit_user_defined_string_literal(const std::string& data,
		const std::vector<std::size_t>& ucn_backslash_offsets)
		{ (void)ucn_backslash_offsets; emit_user_defined_string_literal(data); }
	virtual void emit_preprocessing_op_or_punc(const std::string& data) = 0;
	virtual void emit_non_whitespace_char(const std::string& data) = 0;
	virtual void emit_eof() = 0;

	virtual ~IPPTokenStream() {}
};
