#include "parser/ast_parser.h"

#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

const std::size_t none = static_cast<std::size_t>(-1);

bool add_edge(const cppgm::Ast& ast, std::size_t child,
              std::vector<std::size_t>& pending)
{
  if (child == none || child >= ast.nodes.size()) return child == none;
  pending.push_back(child);
  return true;
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "usage: pa5-ast-metrics <source>\n";
    return 2;
  }

  struct stat info;
  if (stat(argv[1], &info) != 0) {
    std::cerr << "cannot stat source\n";
    return 2;
  }

  cppgm::Ast ast = cppgm::ParseTranslationUnit(argv[1]);
  std::vector<unsigned char> reached(ast.nodes.size(), 0);
  std::vector<std::size_t> pending;
  if (ast.nodes.empty()) return 1;
  pending.push_back(0);
  std::size_t duplicates = 0;
  std::size_t bad_edges = 0;
  std::size_t unlocated = 0;
  std::size_t bad_atoms = 0;
  std::size_t bad_composites = 0;
  std::size_t template_ids = 0;
  std::size_t type_arguments = 0;
  std::size_t expression_arguments = 0;
  std::size_t composite_bytes = 0;

  for (std::size_t i = 0; i < ast.composite_atoms.size(); ++i)
    composite_bytes += ast.composite_atoms[i].size();

  while (!pending.empty()) {
    const std::size_t id = pending.back();
    pending.pop_back();
    if (reached[id]) { ++duplicates; continue; }
    reached[id] = 1;
    const cppgm::AstNode& node = ast.nodes[id];
    if (node.source_file_id == none || node.line == none || node.column == none ||
        node.source_file_id >= ast.preprocessing_metadata().source_files.size())
      ++unlocated;
    if (node.atom != none && node.atom >= ast.tokens.size()) ++bad_atoms;
    if (node.composite != none && node.composite >= ast.composite_atoms.size()) ++bad_composites;
    if (node.kind == cppgm::NTemplateIdSyntax) ++template_ids;
    if (node.kind == cppgm::NTypeTemplateArgument) ++type_arguments;
    if (node.kind == cppgm::NExpressionTemplateArgument) ++expression_arguments;

    for (std::size_t child = node.first_child; child != none;
         child = ast.nodes[child].next_sibling) {
      if (child >= ast.nodes.size() || !add_edge(ast, child, pending)) {
        ++bad_edges;
        break;
      }
    }
    for (std::size_t child = node.first_aux_child; child != none;
         child = ast.nodes[child].next_aux_sibling) {
      if (child >= ast.nodes.size() || !add_edge(ast, child, pending)) {
        ++bad_edges;
        break;
      }
    }
  }

  std::size_t reachable = 0;
  for (std::size_t i = 0; i < reached.size(); ++i) reachable += reached[i] != 0;
  std::cout << "input_bytes\t" << static_cast<unsigned long long>(info.st_size) << '\n'
            << "retained_tokens\t" << ast.tokens.size() << '\n'
            << "ast_nodes\t" << ast.nodes.size() << '\n'
            << "reachable_nodes\t" << reachable << '\n'
            << "duplicate_edges\t" << duplicates << '\n'
            << "bad_edges\t" << bad_edges << '\n'
            << "unlocated_nodes\t" << unlocated << '\n'
            << "bad_atom_indexes\t" << bad_atoms << '\n'
            << "bad_composite_indexes\t" << bad_composites << '\n'
            << "composite_strings\t" << ast.composite_atoms.size() << '\n'
            << "composite_bytes\t" << composite_bytes << '\n'
            << "template_ids\t" << template_ids << '\n'
            << "type_template_arguments\t" << type_arguments << '\n'
            << "expression_template_arguments\t" << expression_arguments << '\n';

  return reachable == ast.nodes.size() && duplicates == 0 && bad_edges == 0 &&
      unlocated == 0 && bad_atoms == 0 && bad_composites == 0 ? 0 : 1;
}
