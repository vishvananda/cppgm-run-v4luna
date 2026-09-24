#include "semantic/pa6.h"

#include <cassert>
#include <string>
#include <vector>

namespace {

std::vector<cppgm::pa6::Id> named_functions(
    const cppgm::pa6::SemanticUnit& unit, cppgm::pa6::Id scope,
    const std::string& name)
{
  std::vector<cppgm::pa6::Id> result;
  const cppgm::pa6::ScopeRecord& record = unit.scope(scope);
  for (std::size_t i = 0; i < record.bindings.size(); ++i) {
    const cppgm::pa6::Id id = record.bindings[i];
    const cppgm::pa6::Binding& binding = unit.binding(id);
    if (binding.kind == cppgm::pa6::FunctionBinding && binding.name == name)
      result.push_back(id);
  }
  return result;
}

void expect_same_entity(const cppgm::pa6::SemanticUnit& unit,
                        const std::vector<cppgm::pa6::Id>& bindings)
{
  assert(bindings.size() == 2);
  const cppgm::pa6::Id entity = unit.binding(bindings[0]).entity;
  assert(entity != cppgm::pa6::InvalidId);
  assert(unit.binding(bindings[1]).entity == entity);
}

cppgm::pa6::Id template_type(const cppgm::pa6::SemanticUnit& unit,
                             cppgm::pa6::Id parent,
                             const std::string& name)
{
  const cppgm::pa6::ScopeRecord& scope = unit.scope(parent);
  for (std::size_t i = 0; i < scope.children.size(); ++i) {
    const cppgm::pa6::Id child = scope.children[i];
    if (unit.scope(child).kind != cppgm::pa6::TemplateScope) continue;
    const cppgm::pa6::Id binding = unit.lookup(child, name, true);
    if (binding != cppgm::pa6::InvalidId) return binding;
  }
  return cppgm::pa6::InvalidId;
}

}  // namespace

int main(int argc, char** argv)
{
  assert(argc == 2);
  cppgm::pa6::SemanticUnit unit = cppgm::pa6::AnalyzeTranslationUnit(argv[1]);
  assert(!unit.ast().nodes.empty());
  const cppgm::pa6::Id global = unit.global_scope();
  expect_same_entity(unit, named_functions(unit, global, "equivalent"));
  expect_same_entity(unit, named_functions(unit, global, "adjusted"));

  const cppgm::pa6::Id class_binding = unit.lookup(global, "MemberQualifiers", true);
  assert(class_binding != cppgm::pa6::InvalidId);
  const cppgm::pa6::Id class_entity = unit.binding(class_binding).entity;
  assert(class_entity != cppgm::pa6::InvalidId);
  const cppgm::pa6::Id class_scope = unit.entity(class_entity).scope;
  const std::vector<cppgm::pa6::Id> cv = named_functions(unit, class_scope, "cv");
  const std::vector<cppgm::pa6::Id> ref = named_functions(unit, class_scope, "ref");
  assert(cv.size() == 2 && ref.size() == 2);
  assert(unit.binding(cv[0]).entity != unit.binding(cv[1]).entity);
  assert(unit.binding(ref[0]).entity != unit.binding(ref[1]).entity);
  const cppgm::pa6::Type& cv_first = unit.type(unit.binding(cv[0]).type);
  const cppgm::pa6::Type& cv_second = unit.type(unit.binding(cv[1]).type);
  assert(cv_first.member_const != cv_second.member_const);
  assert(cv_first.member_volatile != cv_second.member_volatile);
  const cppgm::pa6::Type& ref_first = unit.type(unit.binding(ref[0]).type);
  const cppgm::pa6::Type& ref_second = unit.type(unit.binding(ref[1]).type);
  assert(ref_first.ref_qualifier != ref_second.ref_qualifier);

  const cppgm::pa6::Id operators_binding = unit.lookup(global, "Operators", true);
  assert(operators_binding != cppgm::pa6::InvalidId);
  const cppgm::pa6::Id operators_scope =
      unit.entity(unit.binding(operators_binding).entity).scope;
  const std::vector<cppgm::pa6::Id> plus =
      named_functions(unit, operators_scope, "operator+");
  const std::vector<cppgm::pa6::Id> minus =
      named_functions(unit, operators_scope, "operator-");
  assert(plus.size() == 2 && minus.size() == 1);
  assert(unit.binding(plus[0]).entity == unit.binding(plus[1]).entity);
  assert(unit.binding(plus[0]).entity != unit.binding(minus[0]).entity);

  const cppgm::pa6::Id first_binding = template_type(unit, global, "FirstTemplate");
  const cppgm::pa6::Id second_binding = template_type(unit, global, "SecondTemplate");
  assert(first_binding != cppgm::pa6::InvalidId);
  assert(second_binding != cppgm::pa6::InvalidId);
  const cppgm::pa6::Id first_scope = unit.entity(unit.binding(first_binding).entity).scope;
  const cppgm::pa6::Id second_scope = unit.entity(unit.binding(second_binding).entity).scope;
  const cppgm::pa6::Id first_member = unit.lookup(first_scope, "value");
  const cppgm::pa6::Id second_member = unit.lookup(second_scope, "value");
  assert(first_member != cppgm::pa6::InvalidId);
  assert(second_member != cppgm::pa6::InvalidId);
  const cppgm::pa6::Type& first_template_type =
      unit.type(unit.binding(first_member).type);
  const cppgm::pa6::Type& second_template_type =
      unit.type(unit.binding(second_member).type);
  assert(first_template_type.kind == cppgm::pa6::TemplateParameterType);
  assert(second_template_type.kind == cppgm::pa6::TemplateParameterType);
  assert(first_template_type.declaration != second_template_type.declaration);
  assert(unit.binding(first_member).type != unit.binding(second_member).type);
  return 0;
}
