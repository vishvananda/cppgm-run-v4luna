#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "preprocess/preprocessor.h"

// Evaluate one controlling expression per logical line in source. Tokenizer
// failures propagate as exceptions; expression errors are written per line.
void EvaluateControlExpressions(const std::string& source, std::ostream& output);

// Evaluate a phase-4 controlling expression without serializing and
// retokenizing its already-classified preprocessing tokens.
void EvaluateControlExpressionTokens(
	const std::vector<PreprocessingToken>& tokens, std::ostream& output);
