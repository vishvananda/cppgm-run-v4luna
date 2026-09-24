#include "parser/ast_parser.h"

#include <iostream>
#include <sstream>
#include <exception>
#include <string>

namespace {

bool has_kind(const cppgm::Ast& ast, cppgm::NodeKind kind)
{
  for (std::size_t i = 0; i < ast.nodes.size(); ++i)
    if (ast.nodes[i].kind == kind) return true;
  return false;
}

std::size_t find_composite(const cppgm::Ast& ast, cppgm::NodeKind kind,
                           const std::string& spelling)
{
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    const cppgm::AstNode& node = ast.nodes[i];
    if (node.kind == kind && node.composite != static_cast<std::size_t>(-1) &&
        ast.composite_atoms[node.composite] == spelling) return i;
  }
  return static_cast<std::size_t>(-1);
}

}  // namespace

int main()
{
  std::string malformed = "int broken = ;\n";
  for (std::size_t i = 0; i < 4096; ++i)
    malformed += std::string("int tail") + std::to_string(i) + ";\n";
  bool rejected_malformed_input = false;
  try {
    cppgm::Ast unused = cppgm::ParseTranslationUnitSource(malformed, "cancel-smoke.cpp");
    (void)unused;
  } catch (const std::exception&) {
    rejected_malformed_input = true;
  }
  if (!rejected_malformed_input) {
    std::cerr << "malformed input did not stop the streaming parser\n";
    return 1;
  }

  const std::string source =
      "namespace sample { template<class T> struct Box { T value; }; }\n"
      "template<int N> struct Value {};\n"
      "sample::Box<int> object;\n"
      "Value<1 + 2> computed;\n"
      "template<class T> int operator+(T, T);\n"
      "struct Add { Add operator+(const Add&) const; };\n"
      "int use() { return operator+<int>(1, 2); }\n";
  cppgm::Ast ast = cppgm::ParseTranslationUnitSource(source, "api-smoke.cpp");
  if (ast.nodes.empty() || ast.nodes[0].kind != cppgm::NTranslationUnit ||
      !has_kind(ast, cppgm::NTemplateDeclaration) ||
      !has_kind(ast, cppgm::NClassSpecifier) ||
      !has_kind(ast, cppgm::NNameComponent) ||
      !has_kind(ast, cppgm::NTemplateIdSyntax) ||
      !has_kind(ast, cppgm::NTypeTemplateArgument) ||
      !has_kind(ast, cppgm::NExpressionTemplateArgument)) {
    std::cerr << "structured AST did not retain template/class nodes\n";
    return 1;
  }
  const std::size_t type_name = find_composite(ast, cppgm::NTypeName,
                                                "sample::Box<int>");
  const std::size_t declaration_name = find_composite(ast, cppgm::NDeclSpecifier,
                                                       "sample::Box<int>");
  const std::size_t structured_name = type_name != static_cast<std::size_t>(-1) ?
      type_name : declaration_name;
  if (structured_name == static_cast<std::size_t>(-1) ||
      ast.nodes[structured_name].first_aux_child == static_cast<std::size_t>(-1)) {
    std::cerr << "qualified template name has no structured syntax edge\n";
    return 1;
  }
  const std::size_t sample_component = ast.nodes[structured_name].first_aux_child;
  if (ast.nodes[sample_component].kind != cppgm::NNameComponent ||
      ast.nodes[sample_component].atom == static_cast<std::size_t>(-1)) {
    std::cerr << "qualified name components were not retained\n";
    return 1;
  }
  const std::size_t box_component = ast.nodes[sample_component].next_aux_sibling;
  if (box_component == static_cast<std::size_t>(-1)) {
    std::cerr << "qualified name lost its second component\n";
    return 1;
  }
  const std::size_t box_template = ast.nodes[box_component].first_child;
  if (box_template == static_cast<std::size_t>(-1) ||
      ast.nodes[box_template].kind != cppgm::NTemplateIdSyntax) {
    std::cerr << "template-id was not attached to its name component\n";
    return 1;
  }
  const std::size_t argument_list = ast.nodes[box_template].first_child;
  const std::size_t template_argument = ast.nodes[argument_list].first_child;
  if (ast.nodes[template_argument].kind != cppgm::NTypeTemplateArgument ||
      !has_kind(ast, cppgm::NBinaryExpression)) {
    std::cerr << "template arguments were not preserved as typed syntax\n";
    return 1;
  }
  const std::size_t operator_name = find_composite(ast, cppgm::NIdExpression,
                                                    "operator+<int>");
  if (operator_name == static_cast<std::size_t>(-1) ||
      ast.nodes[operator_name].first_aux_child == static_cast<std::size_t>(-1)) {
    std::cerr << "operator template-id lost its structured name\n";
    return 1;
  }
  const std::size_t operator_component = ast.nodes[operator_name].first_aux_child;
  const std::size_t operator_token = ast.nodes[operator_component].first_child;
  const std::size_t operator_template = ast.nodes[operator_token].next_sibling;
  if (ast.nodes[operator_token].kind != cppgm::NNameComponentToken ||
      ast.tokens[ast.nodes[operator_token].atom].text != "+" ||
      operator_template == static_cast<std::size_t>(-1) ||
      ast.nodes[operator_template].kind != cppgm::NTemplateIdSyntax) {
    std::cerr << "operator punctuation and template arguments were not retained\n";
    return 1;
  }
  std::size_t structured_operator_declarations = 0;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    const cppgm::AstNode& node = ast.nodes[i];
    if (node.kind == cppgm::NIdentifier &&
        node.composite != static_cast<std::size_t>(-1) &&
        ast.composite_atoms[node.composite] == "operator+" &&
        node.first_aux_child != static_cast<std::size_t>(-1))
      ++structured_operator_declarations;
  }
  if (structured_operator_declarations < 2) {
    std::cerr << "class operator declaration lost its structured name\n";
    return 1;
  }
  if (ast.preprocessing_metadata().source_files.empty() ||
      ast.source_file(ast.nodes[0].source_file_id) != "api-smoke.cpp" ||
      ast.nodes[structured_name].line != 3 || ast.nodes[structured_name].column != 1) {
    std::cerr << "AST lost source-file identity\n";
    return 1;
  }
  for (std::size_t i = 0; i < ast.tokens.size(); ++i) {
    if (ast.tokens[i].identifier_id == static_cast<std::size_t>(-1) &&
        ast.tokens[i].category == cppgm::ast_tokens::IdentifierToken) {
      std::cerr << "AST identifier has no compact identity\n";
      return 1;
    }
  }
  std::ostringstream dump;
  cppgm::PrintAst(ast, dump);
  if (dump.str().find("translation-unit\n") != 0 ||
      dump.str().find("template-declaration") == std::string::npos) {
    std::cerr << "AST printer did not consume the public tree\n";
    return 1;
  }
  return 0;
}
