#include "semantic/pa7_templates.h"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cppgm {
namespace pa7 {
namespace {

typedef pa6::Id Id;
const Id none = pa6::InvalidId;

Id strip_cv(const pa6::SemanticUnit& unit, Id id)
{
  while (id != none && id < unit.type_count() &&
         unit.type(id).kind == pa6::QualifiedType)
    id = unit.type(id).base;
  return id;
}

Id decay(pa6::SemanticUnit& unit, Id id)
{
  id = strip_cv(unit, id);
  if (id == none) return none;
  const pa6::Type& type = unit.type(id);
  if (type.kind == pa6::ArrayType) return unit.pointer_type(type.base);
  if (type.kind == pa6::FunctionType) return unit.pointer_type(id);
  return id;
}

bool deduce(pa6::SemanticUnit& unit, Id pattern, Id actual,
            const std::unordered_set<Id>& parameters,
            std::unordered_map<Id, Id>& substitutions, bool by_value)
{
  if (pattern == none || actual == none) return false;
  if (by_value) {
    pattern = decay(unit, pattern);
    actual = decay(unit, actual);
  }
  const pa6::Type p = unit.type(pattern);
  const pa6::Type a = unit.type(actual);
  if (p.kind == pa6::TemplateParameterType && parameters.count(p.declaration)) {
    const std::unordered_map<Id, Id>::const_iterator old =
        substitutions.find(p.declaration);
    if (old != substitutions.end()) return old->second == actual;
    substitutions[p.declaration] = actual;
    return true;
  }
  if (p.kind == pa6::QualifiedType)
    return deduce(unit, p.base, actual, parameters, substitutions, false);
  if (p.kind != a.kind) return false;
  if (p.kind == pa6::PointerType || p.kind == pa6::LvalueReferenceType ||
      p.kind == pa6::RvalueReferenceType || p.kind == pa6::ArrayType)
    return (p.kind != pa6::ArrayType || p.bound == a.bound) &&
        deduce(unit, p.base, a.base, parameters, substitutions, false);
  if (p.kind == pa6::MemberPointerType)
    return p.entity == a.entity &&
        deduce(unit, p.base, a.base, parameters, substitutions, false);
  if (p.kind == pa6::FunctionType) {
    if (p.variadic != a.variadic || p.parameters.size() != a.parameters.size() ||
        !deduce(unit, p.base, a.base, parameters, substitutions, false)) return false;
    for (std::size_t i = 0; i < p.parameters.size(); ++i)
      if (!deduce(unit, p.parameters[i], a.parameters[i], parameters,
                  substitutions, true)) return false;
    return true;
  }
  return strip_cv(unit, pattern) == strip_cv(unit, actual);
}

Id substitute(pa6::SemanticUnit& unit, Id id,
              const std::unordered_map<Id, Id>& substitutions)
{
  if (id == none || id >= unit.type_count()) return none;
  const pa6::Type original = unit.type(id);
  if (original.kind == pa6::TemplateParameterType) {
    const std::unordered_map<Id, Id>::const_iterator found =
        substitutions.find(original.declaration);
    return found == substitutions.end() ? id : found->second;
  }
  if (original.kind == pa6::QualifiedType) {
    const Id base = substitute(unit, original.base, substitutions);
    if (base == original.base) return id;
    const bool is_const = original.atom.find('c') != std::string::npos;
    const bool is_volatile = original.atom.find('v') != std::string::npos;
    return unit.qualified_type(base, is_const, is_volatile);
  }
  if (original.kind == pa6::PointerType ||
      original.kind == pa6::LvalueReferenceType ||
      original.kind == pa6::RvalueReferenceType ||
      original.kind == pa6::ArrayType) {
    const Id base = substitute(unit, original.base, substitutions);
    if (base == original.base) return id;
    if (original.kind == pa6::PointerType) return unit.pointer_type(base);
    if (original.kind == pa6::LvalueReferenceType) return unit.lvalue_reference_type(base);
    if (original.kind == pa6::RvalueReferenceType) return unit.rvalue_reference_type(base);
    return unit.array_type(base, original.bound);
  }
  if (original.kind == pa6::MemberPointerType) {
    const Id base = substitute(unit, original.base, substitutions);
    return base == original.base ? id : unit.member_pointer_type(original.entity, base);
  }
  if (original.kind == pa6::FunctionType) {
    pa6::Type function = original;
    function.base = substitute(unit, original.base, substitutions);
    function.parameters.clear();
    bool changed = function.base != original.base;
    for (std::size_t i = 0; i < original.parameters.size(); ++i) {
      const Id parameter = substitute(unit, original.parameters[i], substitutions);
      function.parameters.push_back(parameter);
      changed = changed || parameter != original.parameters[i];
    }
    return changed ? unit.function_type(function) : id;
  }
  return id;
}

bool has_parameter(const pa6::SemanticUnit& unit, Id id,
                   std::unordered_set<Id>& visited)
{
  if (id == none || id >= unit.type_count() || !visited.insert(id).second) return false;
  const pa6::Type& value = unit.type(id);
  if (value.kind == pa6::TemplateParameterType) return true;
  if ((value.kind == pa6::QualifiedType || value.kind == pa6::PointerType ||
       value.kind == pa6::LvalueReferenceType || value.kind == pa6::RvalueReferenceType ||
       value.kind == pa6::ArrayType || value.kind == pa6::MemberPointerType) &&
      has_parameter(unit, value.base, visited)) return true;
  if (value.kind == pa6::FunctionType) {
    if (has_parameter(unit, value.base, visited)) return true;
    for (std::size_t i = 0; i < value.parameters.size(); ++i)
      if (has_parameter(unit, value.parameters[i], visited)) return true;
  }
  return false;
}

}  // namespace

void IndexNamespaceFunctionTemplates(const pa6::SemanticUnit& unit,
                                     FunctionTemplateIndex& index)
{
  for (Id scope = 0; scope < unit.scope_count(); ++scope) {
    const pa6::ScopeRecord& owner = unit.scope(scope);
    if (owner.kind != pa6::NamespaceScope) continue;
    for (std::size_t i = 0; i < owner.children.size(); ++i) {
      const pa6::ScopeRecord& child = unit.scope(owner.children[i]);
      if (child.kind != pa6::TemplateScope) continue;
      for (std::size_t j = 0; j < child.bindings.size(); ++j) {
        const Id binding = child.bindings[j];
        const pa6::Binding& candidate = unit.binding(binding);
        if (candidate.kind == pa6::FunctionBinding)
          index[scope][candidate.name_id].push_back(binding);
      }
    }
  }
}

Id InstantiateFunctionTemplateType(pa6::SemanticUnit& unit, Id primary,
                                   const std::vector<Id>& explicit_types,
                                   const std::vector<Id>& argument_types,
                                   bool deduce_from_arguments)
{
  if (primary >= unit.binding_count() ||
      unit.binding(primary).kind != pa6::FunctionBinding) return none;
  const pa6::Binding& declaration = unit.binding(primary);
  const Id template_scope_id = declaration.scope;
  if (template_scope_id >= unit.scope_count() ||
      unit.scope(template_scope_id).kind != pa6::TemplateScope) return none;
  const pa6::ScopeRecord& template_scope = unit.scope(template_scope_id);
  std::vector<Id> parameters;
  for (std::size_t i = 0; i < template_scope.bindings.size(); ++i) {
    const pa6::Binding& candidate = unit.binding(template_scope.bindings[i]);
    if (candidate.kind == pa6::TypeBinding && candidate.type < unit.type_count() &&
        unit.type(candidate.type).kind == pa6::TemplateParameterType)
      parameters.push_back(candidate.type);
    else if (candidate.kind == pa6::VariableBinding)
      return none;
  }
  if (parameters.empty() || explicit_types.size() > parameters.size() ||
      (!deduce_from_arguments && explicit_types.size() != parameters.size())) return none;
  const Id pattern_id = declaration.type;
  if (pattern_id >= unit.type_count() || unit.type(pattern_id).kind != pa6::FunctionType)
    return none;
  const pa6::Type pattern = unit.type(pattern_id);
  if (deduce_from_arguments &&
      ((!pattern.variadic && argument_types.size() != pattern.parameters.size()) ||
       (pattern.variadic && argument_types.size() < pattern.parameters.size()))) return none;

  std::unordered_set<Id> parameter_declarations;
  std::unordered_map<Id, Id> substitutions;
  for (std::size_t i = 0; i < parameters.size(); ++i) {
    parameter_declarations.insert(unit.type(parameters[i]).declaration);
    if (i < explicit_types.size())
      substitutions[unit.type(parameters[i]).declaration] = explicit_types[i];
  }
  if (deduce_from_arguments) {
    for (std::size_t i = 0; i < pattern.parameters.size(); ++i) {
      const Id formal = pattern.parameters[i];
      const pa6::TypeKind kind = unit.type(formal).kind;
      const bool by_value = kind != pa6::LvalueReferenceType &&
          kind != pa6::RvalueReferenceType;
      if (!deduce(unit, formal, argument_types[i], parameter_declarations,
                  substitutions, by_value)) return none;
    }
  }
  if (substitutions.size() != parameters.size()) return none;
  const Id result = substitute(unit, pattern_id, substitutions);
  std::unordered_set<Id> visited;
  if (result == none || has_parameter(unit, result, visited)) return none;
  return result;
}

bool ExplicitFunctionTemplateTypes(pa6::SemanticUnit& unit, const Ast& ast,
                                   Id scope, Id node, std::vector<Id>& result)
{
  Id selected = none;
  for (Id part = ast.nodes[node].first_aux_child; part != none;
       part = ast.nodes[part].next_aux_sibling) {
    if (ast.nodes[part].kind != NNameComponent) continue;
    for (Id child = ast.nodes[part].first_child; child != none;
         child = ast.nodes[child].next_sibling)
      if (ast.nodes[child].kind == NTemplateIdSyntax) selected = child;
  }
  if (selected == none) return false;
  const Id list = ast.nodes[selected].first_child;
  for (Id argument = list == none ? none : ast.nodes[list].first_child;
       argument != none; argument = ast.nodes[argument].next_sibling) {
    if (ast.nodes[argument].kind != NTypeTemplateArgument)
      throw std::runtime_error("PA7 function template arguments must be types");
    const Id type_node = ast.nodes[argument].first_child;
    if (type_node == none) throw std::runtime_error("empty function template type argument");
    result.push_back(unit.resolve_type_node(type_node, scope));
  }
  return true;
}

}  // namespace pa7
}  // namespace cppgm
