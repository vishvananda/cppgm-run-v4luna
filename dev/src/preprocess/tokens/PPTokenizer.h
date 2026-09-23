#pragma once

#include <string>

#include "preprocess/tokens/IPPTokenStream.h"

void TokenizePreprocessingSource(const std::string& source,
	IPPTokenStream& output);

// Validate a UTF-8 spelling against the C++11 identifier grammar used by the
// preprocessing tokenizer.
bool IsValidIdentifierName(const std::string& spelling);
