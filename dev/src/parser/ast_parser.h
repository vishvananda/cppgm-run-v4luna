#pragma once

#include <string>
#include <vector>

namespace cppgm {

// Parse and print one or more PA5 translation units. Throws std::runtime_error
// for preprocessing or syntax errors; output is the PA5 deterministic AST view.
void EmitAst(const std::vector<std::string>& inputs, const std::string& output);

}  // namespace cppgm
