#pragma once

#include "parser/ast_parser.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace cppgm {

namespace pa6 {

typedef std::size_t Id;
static const Id InvalidId = static_cast<Id>(-1);

enum TypeKind {
  InvalidType, FundamentalType, NamedType, TemplateParameterType,
  QualifiedType, PointerType, LvalueReferenceType, RvalueReferenceType,
  ArrayType, FunctionType
};

enum RefQualifier { NoRefQualifier, LvalueRefQualifier, RvalueRefQualifier };

struct Type
{
  TypeKind kind;
  std::string atom;
  Id base;
  Id entity;
  Id declaration;
  long long bound;
  bool variadic;
  bool member_const;
  bool member_volatile;
  RefQualifier ref_qualifier;
  std::vector<Id> parameters;
  Type() : kind(InvalidType), base(InvalidId), entity(InvalidId),
      declaration(InvalidId), bound(0), variadic(false), member_const(false),
      member_volatile(false), ref_qualifier(NoRefQualifier) {}
};

enum ScopeKind { NamespaceScope, TemplateScope, ClassScope, EnumScope,
                 FunctionScope, BlockScope };
enum EntityKind { NamespaceEntity, ClassEntity, EnumEntity, ObjectEntity,
                  FunctionEntity, EnumeratorEntity, AliasNamespaceEntity };
enum BindingKind { TypeBinding, AliasBinding, EnumeratorBinding,
                   FunctionBinding, VariableBinding, ParameterBinding,
                   NamespaceBinding, TemplateNameBinding };

struct ScopeRecord
{
  ScopeKind kind;
  std::string name;
  Id parent;
  Id entity;
  bool inline_namespace;
  std::vector<Id> bindings;
  std::vector<Id> output_order;
  std::vector<Id> children;
  std::vector<Id> using_directives;
  ScopeRecord(ScopeKind k = BlockScope, const std::string& n = std::string(),
              Id p = InvalidId, Id e = InvalidId)
      : kind(k), name(n), parent(p), entity(e), inline_namespace(false) {}
};

struct Entity
{
  EntityKind kind;
  std::string name;
  Id scope;
  Id type;
  bool complete;
  bool defined;
  bool scoped_enum;
  bool is_union;
  std::string class_key;
  Id underlying;
  Entity() : kind(ClassEntity), scope(InvalidId), type(InvalidId), complete(false),
      defined(false), scoped_enum(false), is_union(false), underlying(InvalidId) {}
};

struct Binding
{
  BindingKind kind;
  Id name_id;
  std::string name;
  Id type;
  Id entity;
  Id scope;
  Id previous_same_name;
  bool output;
  bool static_storage;
  bool namespace_alias;
  std::string display_override;
  std::string type_override;
  bool has_value;
  long long value;
  Binding() : kind(VariableBinding), name_id(InvalidId), type(InvalidId),
      entity(InvalidId), scope(InvalidId), previous_same_name(InvalidId), output(true),
      static_storage(false), namespace_alias(false), has_value(false), value(0) {}
};

// One translation-unit-owned semantic graph. Its IDs refer to the retained
// AST and are valid for the lifetime of this object. Later semantic stages can
// extend their own fact records while reusing these declarations, scopes and
// canonical types without parsing or printing the source again.
class SemanticUnit
{
public:
  ~SemanticUnit();
  SemanticUnit(SemanticUnit&& other);
  SemanticUnit& operator=(SemanticUnit&& other);
  SemanticUnit(const SemanticUnit&) = delete;
  SemanticUnit& operator=(const SemanticUnit&) = delete;

  const Ast& ast() const;
  Id global_scope() const;
  std::size_t type_count() const;
  std::size_t scope_count() const;
  std::size_t entity_count() const;
  std::size_t binding_count() const;
  const Type& type(Id id) const;
  const ScopeRecord& scope(Id id) const;
  const Entity& entity(Id id) const;
  const Binding& binding(Id id) const;
  Id lookup(Id scope, const std::string& name,
            bool types_only = false, bool namespaces_only = false) const;
  void print(std::ostream& out) const;

private:
  struct Impl;
  explicit SemanticUnit(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
  friend SemanticUnit AnalyzeTranslationUnit(Ast&& ast);
};

SemanticUnit AnalyzeTranslationUnit(Ast&& ast);
SemanticUnit AnalyzeTranslationUnit(const std::string& path);

}  // namespace pa6

void EmitTypes(const std::vector<std::string>& inputs,
               const std::string& output);

}  // namespace cppgm
