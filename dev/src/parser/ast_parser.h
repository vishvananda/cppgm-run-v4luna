#pragma once

#include "parser/ast_tokens.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace cppgm {

enum NodeKind
{
  NTranslationUnit, NEmptyDeclaration, NSimpleDeclaration, NFunctionDefinition,
  NDeclSpecifierSeq, NDeclSpecifier, NInitDeclaratorList, NInitDeclarator,
  NDeclarator, NIdentifier, NIdExpression, NParameterClause, NParameterDeclaration,
  NInitializer, NCompoundStatement, NExpressionStatement, NLiteral, NKeywordLiteral,
  NBinaryExpression, NAssignmentExpression, NConditionalExpression, NUnaryExpression,
  NPostfixExpression, NParenthesizedExpression, NCallExpression, NArgumentList,
  NSubscriptExpression, NMemberExpression, NReturnStatement, NIfStatement,
  NCondition, NThen, NElse, NWhileStatement, NForStatement, NForInitStatement,
  NIteration, NBreakStatement, NContinueStatement, NGotoStatement, NThrowStatement,
  NParenInitializer, NBracedInitList, NPtrOperator, NCvQualifier, NArraySuffix,
  NNestedDeclarator, NParameterPack, NStaticAssertDeclaration, NMessage,
  NNamespaceDefinition, NUsingDirective, NUsingDeclaration, NAliasDeclaration,
  NTarget, NTypeId, NTypeSpecifierSeq, NTypeName, NSizeofExpression,
  NTemplateDeclaration, NTemplateParameterClause, NTemplateParameterList,
  NTypeParameter, NNonTypeTemplateParameter, NParameterKey, NDefaultTemplateArgument,
  NClassSpecifier, NClassForwardDeclaration, NClassKey, NBaseClause, NBaseSpecifier,
  NBaseName, NAccessSpecifier, NEnumSpecifier, NEnumKey, NEnumerator,
  NBitFieldDeclaration, NBitFieldDeclarator, NInlineMarker, NTypeSpecifier,
  NNamespaceAliasDefinition, NSpecialMemberDeclaration, NSpecialMemberDefinition,
  NCtorInitializer, NMemInitializer, NMemInitializerId, NParenArgumentList,
  NVirtualBase, NSpecialInitializer, NAbstractDeclarator, NCastExpression,
  NTypeTraitExpression, NNewExpression, NDeleteExpression, NGlobalScope,
  NArrayDelete, NLambdaExpression, NLambdaIntroducer, NLambdaDeclarator,
  NTrailingReturnType, NRangeForStatement, NRangeDeclaration, NRangeInitializer,
  NSwitchStatement, NCaseStatement, NDefaultStatement, NDoStatement, NTryBlock,
  NHandler, NExceptionDeclaration, NEllipsis, NLabeledStatement,
  NNoexceptSpecification, NLambdaSpecifier, NFunctionQualifier, NRefQualifier,
  NVirtSpecifier, NDefaultArgument, NSizeofPackExpression, NTemplateTemplateParameter,
  NConditionDeclaration, NLinkageSpecification, NDecltypeSpecifier,
  NExplicitInstantiation, NFunctionTryBlock, NMemberSpecifiers, NSpecifier,
  NPlacement, NPackExpansionExpression, NPackExpansion, NTaggedLiteral,
  NNameComponent, NNameComponentToken, NTemplateIdSyntax, NTemplateArgumentList,
  NTypeTemplateArgument, NExpressionTemplateArgument
};

struct AstNode
{
  NodeKind kind;
  std::size_t atom;
  std::size_t composite;
  std::size_t first_child;
  std::size_t last_child;
  std::size_t next_sibling;
  std::size_t first_aux_child;
  std::size_t last_aux_child;
  std::size_t next_aux_sibling;
  std::size_t source_file_id;
  std::size_t line;
  std::size_t column;
  std::size_t source_end_token_index;
};

// A translation-unit-owned syntax graph. The ordinary child links form the
// printable tree. Auxiliary links attach structured name syntax, including
// template-id arguments, without changing that stable dump. Both graphs use
// the same node arena. Node locations identify the source token that starts
// each syntax construct where one is available.
//
// `tokens` is a source-ordered compact pool of tokens referenced by nodes,
// not a retained lexical stream. Token text is borrowed from the
// preprocessor's stable identifier table or this AST's spelling set; atom
// fields index `tokens`, and composite fields index `composite_atoms`.
struct Ast
{
  Ast();
  ~Ast();
  Ast(Ast&& other);
  Ast& operator=(Ast&& other);
  Ast(const Ast&) = delete;
  Ast& operator=(const Ast&) = delete;

  std::size_t add(NodeKind kind, std::size_t atom = static_cast<std::size_t>(-1),
                  std::size_t composite = static_cast<std::size_t>(-1));
  void append(std::size_t parent, std::size_t child);
  void append_aux(std::size_t parent, std::size_t child);
  void set_location(std::size_t node, std::size_t source_file_id,
                    std::size_t line, std::size_t column);
  void compact_tokens();
  const std::string* intern_spelling(const std::string& spelling);
  std::size_t register_identifier(std::size_t identifier_id,
                                  const std::string& spelling);
  std::size_t intern_name(const std::string& spelling);
  std::size_t find_name(const std::string& spelling) const;
  PreprocessingMetadata& preprocessing_metadata();
  const PreprocessingMetadata& preprocessing_metadata() const;
  const std::string& source_file(std::size_t id) const;

  std::vector<ast_tokens::Token> tokens;
  std::vector<AstNode> nodes;
  std::vector<std::string> composite_atoms;

private:
  struct SpellingSet;
  std::unique_ptr<SpellingSet> spellings_;
  std::unique_ptr<PreprocessingMetadata> metadata_;
};

// Parse directly from the shared preprocessing pipeline. The returned AST
// owns all spelling and location metadata required by later compiler stages.
Ast ParseTranslationUnit(const std::string& path);
Ast ParseTranslationUnitSource(const std::string& source,
                               const std::string& path);
void PrintAst(const Ast& ast, std::ostream& out);
void EmitAst(const std::vector<std::string>& inputs,
             const std::string& output);

}  // namespace cppgm
