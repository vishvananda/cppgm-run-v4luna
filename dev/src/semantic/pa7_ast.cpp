#include "semantic/pa7_ast.h"

namespace cppgm {
namespace pa7 {
namespace {
const std::size_t none = static_cast<std::size_t>(-1);
}

std::vector<std::string> SplitNameSpelling(const std::string& spelling,
                                           bool& absolute)
{
  std::vector<std::string> parts;
  absolute = spelling.compare(0, 2, "::") == 0;
  std::size_t start = absolute ? 2 : 0;
  while (start < spelling.size()) {
    std::size_t end = spelling.find("::", start);
    if (end == std::string::npos) end = spelling.size();
    std::string part = spelling.substr(start, end - start);
    const std::size_t angle = part.find('<');
    if (angle != std::string::npos) part.erase(angle);
    if (!part.empty()) parts.push_back(part);
    if (end == spelling.size()) break;
    start = end + 2;
  }
  return parts;
}

std::size_t FindDecltypeSpecifier(const Ast& ast, std::size_t node)
{
  if (node == none || node >= ast.nodes.size()) return none;
  for (std::size_t child = ast.nodes[node].first_aux_child; child != none;
       child = ast.nodes[child].next_aux_sibling)
    if (ast.nodes[child].kind == NDecltypeSpecifier) return child;
  return none;
}

std::size_t FindFirstParameterClause(const Ast& ast, std::size_t node)
{
  if (node == none || node >= ast.nodes.size()) return none;
  if (ast.nodes[node].kind == NParameterClause) return node;
  for (std::size_t child = ast.nodes[node].first_child; child != none;
       child = ast.nodes[child].next_sibling) {
    const std::size_t result = FindFirstParameterClause(ast, child);
    if (result != none) return result;
  }
  return none;
}

std::string FindDeclaratorName(const Ast& ast, std::size_t node)
{
  if (node == none || node >= ast.nodes.size()) return std::string();
  if (ast.nodes[node].kind == NIdentifier || ast.nodes[node].kind == NIdExpression) {
    const AstNode& syntax = ast.nodes[node];
    if (syntax.composite != none && syntax.composite < ast.composite_atoms.size())
      return ast.composite_atoms[syntax.composite];
    return syntax.atom != none && syntax.atom < ast.tokens.size()
        ? ast.tokens[syntax.atom].text.get() : std::string();
  }
  for (std::size_t child = ast.nodes[node].first_child; child != none;
       child = ast.nodes[child].next_sibling) {
    const std::string result = FindDeclaratorName(ast, child);
    if (!result.empty()) return result;
  }
  return std::string();
}

}  // namespace pa7
}  // namespace cppgm
