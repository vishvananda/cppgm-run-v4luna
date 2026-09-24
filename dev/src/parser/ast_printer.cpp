#include "parser/ast_parser.h"

#include <ostream>
#include <stdexcept>
#include <string>

namespace cppgm {
namespace {

using ast_tokens::Token;
const std::size_t none = static_cast<std::size_t>(-1);

const char* NodeName(NodeKind kind)
{
  switch (kind) {
  case NTranslationUnit: return "translation-unit";
  case NEmptyDeclaration: return "empty-declaration";
  case NSimpleDeclaration: return "simple-declaration";
  case NFunctionDefinition: return "function-definition";
  case NDeclSpecifierSeq: return "decl-specifier-seq";
  case NDeclSpecifier: return "decl-specifier";
  case NInitDeclaratorList: return "init-declarator-list";
  case NInitDeclarator: return "init-declarator";
  case NDeclarator: return "declarator";
  case NIdentifier: return "identifier";
  case NIdExpression: return "id-expression";
  case NParameterClause: return "parameter-clause";
  case NParameterDeclaration: return "parameter-declaration";
  case NInitializer: return "initializer";
  case NCompoundStatement: return "compound-statement";
  case NExpressionStatement: return "expression-statement";
  case NLiteral: return "literal";
  case NTaggedLiteral: return "literal";
  case NKeywordLiteral: return "keyword-literal";
  case NBinaryExpression: return "binary-expression";
  case NAssignmentExpression: return "assignment-expression";
  case NConditionalExpression: return "conditional-expression";
  case NUnaryExpression: return "unary-expression";
  case NPostfixExpression: return "postfix-expression";
  case NParenthesizedExpression: return "parenthesized-expression";
  case NCallExpression: return "call-expression";
  case NArgumentList: return "argument-list";
  case NSubscriptExpression: return "subscript-expression";
  case NMemberExpression: return "member-expression";
  case NReturnStatement: return "return-statement";
  case NIfStatement: return "if-statement";
  case NCondition: return "condition";
  case NConditionDeclaration: return "condition-declaration";
  case NThen: return "then";
  case NElse: return "else";
  case NWhileStatement: return "while-statement";
  case NForStatement: return "for-statement";
  case NForInitStatement: return "for-init-statement";
  case NIteration: return "iteration";
  case NBreakStatement: return "break-statement";
  case NContinueStatement: return "continue-statement";
  case NGotoStatement: return "goto-statement";
  case NThrowStatement: return "throw-statement";
  case NParenInitializer: return "paren-initializer";
  case NBracedInitList: return "braced-init-list";
  case NPtrOperator: return "ptr-operator";
  case NCvQualifier: return "cv-qualifier";
  case NArraySuffix: return "array-suffix";
  case NNestedDeclarator: return "nested-declarator";
  case NParameterPack: return "parameter-pack";
  case NStaticAssertDeclaration: return "static-assert-declaration";
  case NMessage: return "message";
  case NNamespaceDefinition: return "namespace-definition";
  case NUsingDirective: return "using-directive";
  case NUsingDeclaration: return "using-declaration";
  case NAliasDeclaration: return "alias-declaration";
  case NTarget: return "target";
  case NTypeId: return "type-id";
  case NTypeSpecifierSeq: return "type-specifier-seq";
  case NTypeName: return "type-name";
  case NSizeofExpression: return "sizeof-expression";
  case NSizeofPackExpression: return "sizeof-pack-expression";
  case NTemplateTemplateParameter: return "template-template-parameter";
  case NTemplateDeclaration: return "template-declaration";
  case NTemplateParameterClause: return "template-parameter-clause";
  case NTemplateParameterList: return "template-parameter-list";
  case NTypeParameter: return "type-parameter";
  case NNonTypeTemplateParameter: return "non-type-template-parameter";
  case NParameterKey: return "parameter-key";
  case NDefaultTemplateArgument: return "default-template-argument";
  case NClassSpecifier: return "class-specifier";
  case NClassForwardDeclaration: return "class-forward-declaration";
  case NClassKey: return "class-key";
  case NBaseClause: return "base-clause";
  case NBaseSpecifier: return "base-specifier";
  case NBaseName: return "base-name";
  case NAccessSpecifier: return "access-specifier";
  case NEnumSpecifier: return "enum-specifier";
  case NEnumKey: return "enum-key";
  case NEnumerator: return "enumerator";
  case NBitFieldDeclaration: return "bit-field-declaration";
  case NBitFieldDeclarator: return "bit-field-declarator";
  case NInlineMarker: return "inline";
  case NTypeSpecifier: return "type-specifier";
  case NNamespaceAliasDefinition: return "namespace-alias-definition";
  case NSpecialMemberDeclaration: return "special-member-declaration";
  case NSpecialMemberDefinition: return "special-member-definition";
  case NCtorInitializer: return "ctor-initializer";
  case NMemInitializer: return "mem-initializer";
  case NMemInitializerId: return "mem-initializer-id";
  case NParenArgumentList: return "paren-argument-list";
  case NVirtualBase: return "virtual";
  case NSpecialInitializer: return "special-initializer";
  case NAbstractDeclarator: return "abstract-declarator";
  case NCastExpression: return "cast-expression";
  case NTypeTraitExpression: return "type-trait-expression";
  case NNewExpression: return "new-expression";
  case NDeleteExpression: return "delete-expression";
  case NGlobalScope: return "global-scope";
  case NArrayDelete: return "array-delete";
  case NLambdaExpression: return "lambda-expression";
  case NLambdaIntroducer: return "lambda-introducer";
  case NLambdaDeclarator: return "lambda-declarator";
  case NTrailingReturnType: return "trailing-return-type";
  case NRangeForStatement: return "range-for-statement";
  case NRangeDeclaration: return "range-declaration";
  case NRangeInitializer: return "range-initializer";
  case NSwitchStatement: return "switch-statement";
  case NCaseStatement: return "case-statement";
  case NDefaultStatement: return "default-statement";
  case NDoStatement: return "do-statement";
  case NTryBlock: return "try-block";
  case NHandler: return "handler";
  case NExceptionDeclaration: return "exception-declaration";
  case NEllipsis: return "ellipsis";
  case NLabeledStatement: return "labeled-statement";
  case NNoexceptSpecification: return "noexcept-specification";
  case NLambdaSpecifier: return "lambda-specifier";
  case NFunctionQualifier: return "function-qualifier";
  case NRefQualifier: return "ref-qualifier";
  case NVirtSpecifier: return "virt-specifier";
  case NDefaultArgument: return "default-argument";
  case NLinkageSpecification: return "linkage-specification";
  case NDecltypeSpecifier: return "decltype-specifier";
  case NPlacement: return "placement";
  case NPackExpansionExpression: return "pack-expansion-expression";
  case NPackExpansion: return "pack-expansion";
  case NNameComponent: return "name-component";
  case NNameComponentToken: return "name-component-token";
  case NTemplateIdSyntax: return "template-id-syntax";
  case NTemplateArgumentList: return "template-argument-list";
  case NTypeTemplateArgument: return "type-template-argument";
  case NExpressionTemplateArgument: return "expression-template-argument";
  case NExplicitInstantiation: return "explicit-instantiation-declaration";
  case NFunctionTryBlock: return "function-try-block";
  case NMemberSpecifiers: return "member-specifiers";
  case NSpecifier: return "specifier";
  }
  return "syntax-node";
}

bool has_token(NodeKind kind)
{
  return kind == NDeclSpecifier || kind == NIdentifier || kind == NIdExpression ||
      kind == NLiteral || kind == NKeywordLiteral || kind == NBinaryExpression ||
      kind == NAssignmentExpression || kind == NUnaryExpression || kind == NPostfixExpression ||
      kind == NMemberExpression || kind == NPtrOperator || kind == NCvQualifier ||
      kind == NRefQualifier || kind == NCastExpression || kind == NTypeTraitExpression ||
      kind == NLambdaSpecifier || kind == NVirtSpecifier || kind == NGotoStatement ||
      kind == NLabeledStatement || kind == NSpecifier || kind == NPackExpansion ||
      kind == NTaggedLiteral;
}

void PrintNode(const Ast& ast, std::size_t id, std::size_t depth, std::ostream& out)
{
  const AstNode& n = ast.nodes[id];
  out << std::string(depth * 2, ' ') << NodeName(n.kind);
  if (n.composite != none) out << ' ' << ast.composite_atoms[n.composite];
  else if (n.atom != none && has_token(n.kind)) {
    const Token& t = ast.tokens[n.atom];
    if (n.kind == NIdentifier || n.kind == NIdExpression || n.kind == NLiteral ||
        n.kind == NGotoStatement || n.kind == NLabeledStatement)
      out << ' ' << t.text;
    else if (n.kind == NVirtSpecifier)
      out << " TT_IDENTIFIER:" << t.text;
    else if (n.kind == NSpecifier && t.text == "explicit")
      out << ' ' << t.text;
    else if (n.kind == NCastExpression && t.tag == "OP_LPAREN")
      out << " OP_LPAREN:";
    else
      out << ' ' << t.tag << ':' << t.text;
  }
  if (n.atom != none && (n.kind == NClassKey || n.kind == NEnumKey || n.kind == NParameterKey ||
      n.kind == NTypeSpecifier || n.kind == NAccessSpecifier || n.kind == NVirtualBase)) {
    const Token& t = ast.tokens[n.atom];
    out << ' ' << t.tag << ':' << t.text;
  }
  if (n.atom != none && n.kind == NEnumerator) out << ' ' << ast.tokens[n.atom].text;
  if (n.atom != none && (n.kind == NAliasDeclaration || n.kind == NNamespaceAliasDefinition))
    out << ' ' << ast.tokens[n.atom].text;
  if (n.atom != none && (n.kind == NSpecialMemberDeclaration || n.kind == NSpecialMemberDefinition))
    out << ' ' << ast.tokens[n.atom].text;
  if (n.atom != none && n.kind == NSpecialInitializer) out << ' ' << ast.tokens[n.atom].text;
  if (n.atom != none && n.kind == NParameterPack) out << " ...";
  if (n.atom != none && n.kind == NEllipsis) out << " ...";
  if (n.kind == NNamespaceDefinition) {
    if (n.atom == none) out << " <unnamed>";
    else out << ' ' << ast.tokens[n.atom].text;
  }
  if (n.kind == NStaticAssertDeclaration && n.atom != none) out << ' ' << ast.tokens[n.atom].text;
  if (n.kind == NMessage && n.atom != none) out << ' ' << ast.tokens[n.atom].text;
  out << '\n';
  for (std::size_t c = n.first_child; c != none; c = ast.nodes[c].next_sibling)
    PrintNode(ast, c, depth + 1, out);
}

}  // namespace

void PrintAst(const Ast& ast, std::ostream& out)
{
  if (ast.nodes.empty()) throw std::runtime_error("internal empty AST");
  PrintNode(ast, 0, 0, out);
}


}  // namespace cppgm
