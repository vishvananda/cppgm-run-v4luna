#pragma once

#include "semantic/pa6.h"

#include <vector>
#include <unordered_map>

namespace cppgm {
namespace pa7 {

typedef std::unordered_map<pa6::Id,
    std::unordered_map<pa6::Id, std::vector<pa6::Id> > > FunctionTemplateIndex;

void IndexNamespaceFunctionTemplates(const pa6::SemanticUnit& unit,
                                     FunctionTemplateIndex& index);

// Instantiate a type-only function template from explicit type arguments or
// function-call argument types. Returns InvalidId when deduction is not
// supported or does not produce a complete specialization.
pa6::Id InstantiateFunctionTemplateType(
    pa6::SemanticUnit& unit, pa6::Id primary,
    const std::vector<pa6::Id>& explicit_types,
    const std::vector<pa6::Id>& argument_types,
    bool deduce_from_arguments);

bool ExplicitFunctionTemplateTypes(
    pa6::SemanticUnit& unit, const Ast& ast, pa6::Id scope, pa6::Id node,
    std::vector<pa6::Id>& result);

}  // namespace pa7
}  // namespace cppgm
