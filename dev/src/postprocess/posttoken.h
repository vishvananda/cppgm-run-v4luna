#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "preprocess/preprocessor.h"

// Consume PA1 preprocessing-token callbacks and write the PA2 posttoken view.
void PostTokenizeSource(const std::string& source);

// Convert already-tokenized phase-4 output directly to the PA2 token view.
// Returns false if any token is not a valid phase-7 token.
bool PostTokenizePreprocessingTokens(
	const std::vector<PreprocessingToken>& tokens, std::ostream& output,
	bool emit_eof);

// Typed facts used by PA3 after PA2's literal grammar and type selection.
struct PPIntegralLiteral
{
	std::uint64_t value;
	bool is_unsigned;
};

bool ParsePPIntegralLiteral(const std::string& spelling,
	PPIntegralLiteral& result);
bool ParsePPCharacterLiteral(const std::string& spelling,
	const std::vector<std::size_t>& ucn_backslash_offsets,
	PPIntegralLiteral& result);
