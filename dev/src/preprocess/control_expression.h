#pragma once

#include <iosfwd>
#include <string>

// Evaluate one controlling expression per logical line in source. Tokenizer
// failures propagate as exceptions; expression errors are written per line.
void EvaluateControlExpressions(const std::string& source, std::ostream& output);
