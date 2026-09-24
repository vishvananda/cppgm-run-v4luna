#pragma once

#include "parser/ast_parser.h"

#include <string>
#include <vector>

namespace cppgm {
namespace pa7 {

std::vector<std::string> SplitNameSpelling(const std::string& spelling,
                                           bool& absolute);
std::size_t FindDecltypeSpecifier(const Ast& ast, std::size_t node);
std::size_t FindFirstParameterClause(const Ast& ast, std::size_t node);
std::string FindDeclaratorName(const Ast& ast, std::size_t node);

}  // namespace pa7
}  // namespace cppgm
