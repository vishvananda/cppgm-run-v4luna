#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Consume PA1 preprocessing-token callbacks and write the PA2 posttoken view.
void PostTokenizeSource(const std::string& source);

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
