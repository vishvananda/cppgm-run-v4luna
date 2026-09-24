#include "semantic/pa6.h"
#include "semantic/pa7_ast.h"
#include "semantic/pa7_lookup.h"
#include "semantic/pa7_templates.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cppgm {
namespace {

typedef pa6::Id Id;
const Id none = pa6::InvalidId;

enum ValueCategory { LValue, XValue, PRValue };

struct Conversion
{
  bool viable;
  int rank;
  int detail;
  Conversion(bool v = false, int r = 100, int d = 0)
      : viable(v), rank(r), detail(d) {}
};

struct ExpressionFact
{
  Id type;
  Id display_type;
  ValueCategory category;
  Id binding;
  std::vector<Id> overloads;
  bool constant;
  long long value;
  bool null_pointer_constant;
  ExpressionFact()
      : type(none), display_type(none), category(PRValue), binding(none), constant(false), value(0),
        null_pointer_constant(false) {}
};

struct CallFact
{
  Id selected;
  Id function_type;
  Id callee_node;
  std::vector<Id> argument_nodes;
  std::vector<Id> argument_types;
  std::string special_name;
  bool builtin_abort;
  bool builtin_constant_p;
  CallFact() : selected(none), function_type(none), callee_node(none),
      builtin_abort(false), builtin_constant_p(false) {}
};

struct Environment
{
  std::unordered_map<Id, std::vector<Id> > names;
  std::vector<Id> using_directives;
};

struct SwitchState
{
  bool has_default;
  std::unordered_set<long long> labels;
  SwitchState() : has_default(false) {}
};

struct GeneratedUnionConstructor
{
  Id type;
  Id pointer_type;
  Id function_type;
  std::string name;
};

class SemanticDumper
{
public:
  explicit SemanticDumper(pa6::SemanticUnit& unit)
      : unit_(unit), ast_(unit.ast()), visibility_(unit),
        current_scope_(unit.global_scope()),
        current_function_type_(none), loop_depth_(0), switch_depth_(0),
        analysis_depth_(0)
  {
    facts_.resize(ast_.nodes.size());
    fact_ready_.assign(ast_.nodes.size(), false);
    call_facts_.reset();
    pa7::IndexNamespaceFunctionTemplates(unit_, function_template_index_);
    index_anonymous_types();
  }

  void print(std::ostream& out)
  {
    out_ = &out;
    out << "translation-unit\n";
    index_function_definitions_and_templates();
    for (Id c = ast_.nodes[0].first_child; c != none; c = ast_.nodes[c].next_sibling)
      emit_declaration(c, 1);
    for (std::size_t i = 0; i < demanded_member_definitions_.size(); ++i) {
      const Id binding = demanded_member_definitions_[i];
      const std::unordered_map<Id, Id>::const_iterator definition =
          member_definition_nodes_.find(binding);
      if (definition == member_definition_nodes_.end()) continue;
      const Id entity = unit_.binding(binding).entity;
      if (defined_functions_.contains(entity)) continue;
      emit_function(definition->second, 1);
    }
    for (std::size_t i = 0; i < demanded_function_instances_.size(); ++i)
      emit_function_instance(demanded_function_instances_[i], 1);
    for (std::size_t i = 0; i < translation_unit_constructors_.size(); ++i)
      emit_constructor_definition(translation_unit_constructors_[i], 1);
    out_ = 0;
  }

private:
  pa6::SemanticUnit& unit_;
  const Ast& ast_;
  std::ostream* out_;
  pa7::SourceVisibility visibility_;
  Id current_scope_;
  Id current_function_type_;
  std::vector<Environment> environments_;
  std::vector<SwitchState> switches_;
  unsigned loop_depth_;
  unsigned switch_depth_;
  unsigned analysis_depth_;
  std::vector<ExpressionFact> facts_;
  std::vector<bool> fact_ready_;
  pa7::NodeFactTable<CallFact> call_facts_;
  std::unordered_map<Id, Id> canonical_functions_;
  std::unordered_map<Id, std::pair<Id, std::string> > injected_union_members_;
  pa7::FlatIdSet defined_functions_;
  std::unordered_map<Id, std::unordered_map<Id, pa6::BindingKind> >
      namespace_ordinary_kinds_;
  std::vector<GeneratedUnionConstructor> generated_union_constructors_;
  std::vector<GeneratedUnionConstructor> translation_unit_constructors_;
  std::unordered_map<Id, Id> member_definition_nodes_;
  std::vector<Id> demanded_member_definitions_;
  pa7::FlatIdSet demanded_member_set_;
  std::unordered_map<Id, Id> template_declarators_;
  std::vector<pa6::Binding> instantiated_function_bindings_;
  std::vector<Id> instantiated_function_origins_;
  std::vector<Id> demanded_function_instances_;
  pa7::FlatIdSet demanded_function_instance_set_;
  pa7::FunctionTemplateInstances function_instances_;
  pa7::FunctionTemplateIndex function_template_index_;
  std::unordered_map<Id, std::string> anonymous_type_names_;

  std::string text(Id node) const
  {
    if (node == none || node >= ast_.nodes.size()) return std::string();
    const AstNode& n = ast_.nodes[node];
    if (n.composite != none) return ast_.composite_atoms[n.composite];
    if (n.atom != none && n.atom < ast_.tokens.size())
      return ast_.tokens[n.atom].text.get();
    return std::string();
  }

  std::string token_label(Id node) const
  {
    if (node == none || ast_.nodes[node].atom == none) return std::string();
    const ast_tokens::Token& token = ast_.tokens[ast_.nodes[node].atom];
    return token.tag + ":" + token.text.get();
  }

  std::vector<Id> children(Id node) const
  {
    std::vector<Id> result;
    if (node == none) return result;
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
      result.push_back(c);
    return result;
  }

  std::string indent(unsigned depth) const { return std::string(depth * 2, ' '); }

  void line(unsigned depth, const std::string& value)
  { *out_ << indent(depth) << value << '\n'; }

  std::string category_name(ValueCategory category) const
  {
    return category == LValue ? "lvalue" : category == XValue ? "xvalue" : "prvalue";
  }

  const pa6::Type& type(Id id) const { return unit_.type(id); }

  bool is_instantiated_binding(Id id) const
  { return id >= unit_.binding_count() &&
      id - unit_.binding_count() < instantiated_function_bindings_.size(); }

  const pa6::Binding& binding(Id id) const
  {
    if (id < unit_.binding_count()) return unit_.binding(id);
    if (!is_instantiated_binding(id))
      throw std::logic_error("invalid semantic binding identity");
    return instantiated_function_bindings_[id - unit_.binding_count()];
  }

  Id instantiate_template_function(Id primary,
                                    const std::vector<Id>& explicit_types,
                                    const std::vector<ExpressionFact>* arguments = 0)
  {
    std::vector<Id> argument_types;
    if (arguments) {
      for (std::size_t i = 0; i < arguments->size(); ++i)
        argument_types.push_back((*arguments)[i].type);
    }
    std::vector<Id> specialization_arguments;
    if (!pa7::ResolveFunctionTemplateArguments(
            unit_, primary, explicit_types, argument_types, arguments != 0,
            specialization_arguments)) return none;
    std::unordered_map<Id, std::unordered_map<std::vector<Id>, Id,
        pa7::TypeArgumentVectorHash> >::const_iterator template_instances =
            function_instances_.find(primary);
    if (template_instances != function_instances_.end()) {
      const std::unordered_map<std::vector<Id>, Id,
          pa7::TypeArgumentVectorHash>::const_iterator cached =
              template_instances->second.find(specialization_arguments);
      if (cached != template_instances->second.end()) return cached->second;
    }
    Id specialized = pa7::InstantiateFunctionTemplateType(
        unit_, primary, specialization_arguments);
    if (specialized == none) return none;
    specialized = canonical_function(specialized);
    std::unordered_map<std::vector<Id>, Id, pa7::TypeArgumentVectorHash>& instances =
        function_instances_[primary];
    pa6::Binding instance = unit_.binding(primary);
    instance.type = specialized;
    instance.output = false;
    instance.previous_same_name = none;
    const Id id = unit_.binding_count() + instantiated_function_bindings_.size();
    instantiated_function_bindings_.push_back(instance);
    instantiated_function_origins_.push_back(primary);
    instances[specialization_arguments] = id;
    return id;
  }

  void demand_function_instance(Id id)
  {
    if (is_instantiated_binding(id) && demanded_function_instance_set_.insert(id))
      demanded_function_instances_.push_back(id);
  }

  void index_anonymous_types()
  {
    std::size_t next = 0;
    for (Id node = 0; node < ast_.nodes.size(); ++node) {
      if (ast_.nodes[node].kind != NClassSpecifier || ast_.nodes[node].composite != none)
        continue;
      Id class_type = unit_.type_for_node(node);
      if (class_type == none || class_type >= unit_.type_count() ||
          type(class_type).kind != pa6::NamedType) continue;
      const Id entity = type(class_type).entity;
      if (entity >= unit_.entity_count() || !unit_.entity(entity).is_anonymous ||
          unit_.entity(entity).is_union || anonymous_type_names_.find(entity) !=
              anonymous_type_names_.end()) continue;
      std::ostringstream name;
      name << "__local_type" << ++next;
      anonymous_type_names_[entity] = name.str();
    }
  }

  std::string class_name_for_output(Id entity) const
  {
    if (entity < unit_.entity_count()) {
      const std::unordered_map<Id, std::string>::const_iterator found =
          anonymous_type_names_.find(entity);
      if (found != anonymous_type_names_.end()) return found->second;
      return unit_.entity(entity).name;
    }
    return "<unknown>";
  }

  std::string type_spelling(Id id) const
  {
    if (id == none || id >= unit_.type_count()) return "<invalid>";
    const pa6::Type& t = type(id);
    switch (t.kind) {
    case pa6::FundamentalType:
    case pa6::TemplateParameterType:
      return t.atom;
    case pa6::NamedType: {
      if (t.entity >= unit_.entity_count()) return "<invalid-type>";
      const pa6::Entity& entity = unit_.entity(t.entity);
      Id owner = entity.scope;
      if (owner != none && (entity.kind == pa6::ClassEntity || entity.scoped_enum))
        owner = unit_.scope(owner).parent;
      const std::string display_name = entity.is_anonymous
          ? class_name_for_output(t.entity) : entity.name;
      const std::string name = entity.kind == pa6::EnumEntity || owner == none || display_name.empty()
          ? display_name : qualified_name(owner, display_name);
      if (entity.kind == pa6::EnumEntity)
        return entity.scoped_enum ? "enum class " + name : "enum " + name;
      return entity.class_key + " " + name;
    }
    case pa6::QualifiedType: {
      const std::string qualifier = t.atom == "c" ? "const " :
          t.atom == "v" ? "volatile " : "const volatile ";
      return qualifier + type_spelling(t.base);
    }
    case pa6::PointerType: return "pointer to " + type_spelling(t.base);
    case pa6::MemberPointerType:
      return "member-pointer of " + type_spelling(unit_.entity(t.entity).type) +
          " to " + type_spelling(t.base);
    case pa6::LvalueReferenceType: return "lvalue-reference to " + type_spelling(t.base);
    case pa6::RvalueReferenceType: return "rvalue-reference to " + type_spelling(t.base);
    case pa6::ArrayType: {
      std::ostringstream bound;
      bound << t.bound;
      return "array of " + bound.str() + " " + type_spelling(t.base);
    }
    case pa6::FunctionType: {
      std::string result = "function of (";
      for (std::size_t i = 0; i < t.parameters.size(); ++i) {
        if (i) result += ", ";
        result += type_spelling(t.parameters[i]);
      }
      if (t.variadic) {
        if (!t.parameters.empty()) result += ", ";
        result += "...";
      }
      result += ")";
      if (t.member_const) result += " const";
      if (t.member_volatile) result += " volatile";
      if (t.ref_qualifier == pa6::LvalueRefQualifier) result += " &";
      else if (t.ref_qualifier == pa6::RvalueRefQualifier) result += " &&";
      return result + " returning " + type_spelling(t.base);
    }
    default: return unit_.type_spelling(id);
    }
  }

  Id strip_reference(Id id, ValueCategory* category = 0) const
  {
    if (id == none || id >= unit_.type_count()) return none;
    if (type(id).kind == pa6::LvalueReferenceType) {
      if (category) *category = LValue;
      return type(id).base;
    }
    if (type(id).kind == pa6::RvalueReferenceType) {
      if (category) *category = XValue;
      return type(id).base;
    }
    return id;
  }

  Id strip_cv(Id id) const
  {
    while (id != none && id < unit_.type_count() && type(id).kind == pa6::QualifiedType)
      id = type(id).base;
    return id;
  }

  bool is_const(Id id) const
  {
    if (id == none || id >= unit_.type_count()) return false;
    if (type(id).kind == pa6::QualifiedType)
      return type(id).atom.find('c') != std::string::npos;
    return false;
  }

  bool is_volatile(Id id) const
  {
    if (id == none || id >= unit_.type_count()) return false;
    if (type(id).kind == pa6::QualifiedType)
      return type(id).atom.find('v') != std::string::npos;
    return false;
  }

  bool same_unqualified(Id a, Id b) const
  { return strip_cv(a) == strip_cv(b); }

  bool is_enum(Id id) const
  {
    id = strip_cv(id);
    return id != none && type(id).kind == pa6::NamedType &&
        type(id).entity < unit_.entity_count() &&
        unit_.entity(type(id).entity).kind == pa6::EnumEntity;
  }

  bool is_scoped_enum(Id id) const
  {
    return is_enum(id) && unit_.entity(type(strip_cv(id)).entity).scoped_enum;
  }

  bool is_integral(Id id) const
  {
    id = strip_cv(id);
    if (id == none || id >= unit_.type_count()) return false;
    if (type(id).kind == pa6::NamedType) return is_enum(id);
    if (type(id).kind != pa6::FundamentalType) return false;
    const std::string& n = type(id).atom;
    return n == "bool" || n == "char" || n == "signed char" ||
        n == "unsigned char" || n == "short int" || n == "unsigned short int" ||
        n == "int" || n == "unsigned int" || n == "long int" ||
        n == "unsigned long int" || n == "long long int" ||
        n == "unsigned long long int" || n == "wchar_t" || n == "char16_t" ||
        n == "char32_t";
  }

  bool is_floating(Id id) const
  {
    id = strip_cv(id);
    if (id == none || id >= unit_.type_count() || type(id).kind != pa6::FundamentalType)
      return false;
    return type(id).atom == "float" || type(id).atom == "double" ||
        type(id).atom == "long double";
  }

  bool is_arithmetic(Id id) const
  { return (is_integral(id) && !is_scoped_enum(id)) || is_floating(id); }

  bool is_pointer(Id id) const
  { return id != none && type(strip_cv(id)).kind == pa6::PointerType; }

  bool is_function(Id id) const
  { return id != none && type(strip_cv(id)).kind == pa6::FunctionType; }

  bool is_array(Id id) const
  { return id != none && type(strip_cv(id)).kind == pa6::ArrayType; }

  bool is_nullptr_type(Id id) const
  {
    id = strip_cv(id);
    return id != none && type(id).kind == pa6::FundamentalType &&
        type(id).atom == "nullptr_t";
  }

  bool is_void(Id id) const
  {
    id = strip_cv(id);
    return id != none && type(id).kind == pa6::FundamentalType && type(id).atom == "void";
  }

  bool is_scalar(Id id) const
  {
    id = strip_cv(id);
    return is_arithmetic(id) || is_pointer(id) || is_nullptr_type(id);
  }

  bool is_unsigned(Id id) const
  {
    id = strip_cv(id);
    if (id == none || type(id).kind != pa6::FundamentalType) return false;
    const std::string& n = type(id).atom;
    return n.find("unsigned") == 0 || n == "bool" || n == "char16_t" ||
        n == "char32_t";
  }

  int integer_rank(Id id) const
  {
    id = strip_cv(id);
    if (id == none || type(id).kind != pa6::FundamentalType) return 0;
    const std::string& n = type(id).atom;
    if (n == "bool") return 1;
    if (n == "char" || n == "signed char" || n == "unsigned char" ||
        n == "wchar_t" || n == "char16_t" || n == "char32_t") return 2;
    if (n == "short int" || n == "unsigned short int") return 3;
    if (n == "int" || n == "unsigned int") return 4;
    if (n == "long int" || n == "unsigned long int") return 5;
    if (n == "long long int" || n == "unsigned long long int") return 6;
    return 0;
  }

  Id function_type_from(Id id) const
  {
    id = strip_reference(id);
    if (id == none || id >= unit_.type_count()) return none;
    if (type(id).kind == pa6::PointerType) id = type(id).base;
    return is_function(id) ? id : none;
  }

  Id canonical_function(Id id)
  {
    id = strip_cv(id);
    std::unordered_map<Id, Id>::const_iterator cached = canonical_functions_.find(id);
    if (cached != canonical_functions_.end()) return cached->second;
    if (!is_function(id)) return none;
    pa6::Type canonical = type(id);
    canonical.parameters.clear();
    for (std::size_t i = 0; i < type(id).parameters.size(); ++i) {
      Id parameter = type(id).parameters[i];
      if (type(parameter).kind == pa6::QualifiedType) parameter = type(parameter).base;
      if (type(parameter).kind == pa6::ArrayType)
        parameter = unit_.pointer_type(type(parameter).base);
      else if (type(parameter).kind == pa6::FunctionType)
        parameter = unit_.pointer_type(parameter);
      canonical.parameters.push_back(parameter);
    }
    const Id result = unit_.function_type(canonical);
    canonical_functions_[id] = result;
    return result;
  }

  Id canonical_type(Id id)
  {
    if (id == none) return none;
    const pa6::Type original = type(id);
    if (original.kind == pa6::QualifiedType) {
      const Id base = canonical_type(original.base);
      if (base == original.base) return id;
      return unit_.qualified_type(base, is_const(id), is_volatile(id));
    }
    if (original.kind == pa6::FunctionType) return canonical_function(id);
    if (original.kind == pa6::PointerType && is_function(original.base)) {
      const Id function = canonical_function(original.base);
      return function == original.base ? id : unit_.pointer_type(function);
    }
    if (original.kind == pa6::ArrayType) {
      const Id base = canonical_type(original.base);
      return base == original.base ? id : unit_.array_type(base, original.bound);
    }
    if (original.kind == pa6::LvalueReferenceType || original.kind == pa6::RvalueReferenceType) {
      const Id base = canonical_type(original.base);
      if (base == original.base) return id;
      return original.kind == pa6::LvalueReferenceType
          ? unit_.lvalue_reference_type(base) : unit_.rvalue_reference_type(base);
    }
    return id;
  }

  std::string qualified_name(Id scope, const std::string& leaf) const
  {
    std::vector<std::string> parts;
    for (Id s = scope; s != none && s != unit_.global_scope(); s = unit_.scope(s).parent) {
      const pa6::ScopeRecord& record = unit_.scope(s);
      if (record.kind == pa6::NamespaceScope && record.name != "<unnamed>")
        parts.push_back(record.name);
      else if (record.kind == pa6::ClassScope && !record.name.empty())
        parts.push_back(record.entity < unit_.entity_count() &&
                        unit_.entity(record.entity).is_anonymous
                            ? class_name_for_output(record.entity) : record.name);
    }
    std::reverse(parts.begin(), parts.end());
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (!result.empty()) result += "::";
      result += parts[i];
    }
    if (!result.empty()) result += "::";
    return result + leaf;
  }

  std::string binding_name(Id id) const
  {
    if (id == none || (id >= unit_.binding_count() && !is_instantiated_binding(id)))
      return "<unknown>";
    const pa6::Binding& selected = binding(id);
    Id scope = selected.scope;
    if (selected.entity != none && selected.entity < unit_.entity_count())
      scope = unit_.entity(selected.entity).scope;
    return qualified_name(scope, selected.name);
  }

  std::string node_name(Id node) const { return text(node); }

  std::vector<Id> direct_bindings(Id scope, Id name,
                                  bool namespaces_only = false,
                                  bool types_only = false) const
  {
    pa7::BindingCandidates result;
    if (scope == none || scope >= unit_.scope_count()) return std::vector<Id>();
    const pa6::ScopeRecord& record = unit_.scope(scope);
    const std::vector<Id> indexed = unit_.lookup_bindings(
        scope, name, false, namespaces_only);
    if (types_only) {
      Id latest = none;
      for (std::size_t i = 0; i < indexed.size(); ++i)
        if (visibility_.binding_visible(unit_, scope, indexed[i])) latest = indexed[i];
      if (latest == none && !namespaces_only) {
        const std::vector<Id> namespace_bindings = unit_.lookup_bindings(
            scope, name, false, true);
        for (std::size_t i = 0; i < namespace_bindings.size(); ++i)
          if (visibility_.binding_visible(unit_, scope, namespace_bindings[i]))
            latest = namespace_bindings[i];
      }
      if (latest != none) {
        result.add(unit_, latest);
      }
    } else {
      for (std::size_t i = 0; i < indexed.size(); ++i)
        if (visibility_.binding_visible(unit_, scope, indexed[i])) result.add(unit_, indexed[i]);
    }
    if (!namespaces_only && !types_only && record.kind == pa6::NamespaceScope) {
      const pa7::FunctionTemplateIndex::const_iterator indexed_scope =
          function_template_index_.find(scope);
      if (indexed_scope != function_template_index_.end()) {
        const std::unordered_map<Id, std::vector<Id> >::const_iterator functions =
          indexed_scope->second.find(name);
        if (functions != indexed_scope->second.end())
          for (std::size_t i = 0; i < functions->second.size(); ++i)
            if (visibility_.is_visible(functions->second[i]))
              result.add(unit_, functions->second[i]);
      }
    }
    return result.release();
  }

  void collect_class_members(Id entity, Id name,
                             pa7::FlatIdSet& visited,
                             std::vector<Id>& result) const
  {
    if (entity == none || entity >= unit_.entity_count() || !visited.insert(entity))
      return;
    const pa6::Entity& record = unit_.entity(entity);
    if (record.scope == none) return;
    const std::vector<Id> direct = direct_bindings(record.scope, name);
    if (!direct.empty()) {
      result.insert(result.end(), direct.begin(), direct.end());
      return;
    }
    for (std::size_t i = 0; i < record.bases.size(); ++i)
      collect_class_members(record.bases[i], name, visited, result);
  }

  bool derives_from(Id derived, Id base,
                    pa7::FlatIdSet& visited) const
  {
    if (derived == base) return true;
    if (derived == none || derived >= unit_.entity_count() || !visited.insert(derived))
      return false;
    const std::vector<Id>& bases = unit_.entity(derived).bases;
    for (std::size_t i = 0; i < bases.size(); ++i)
      if (bases[i] == base || derives_from(bases[i], base, visited)) return true;
    return false;
  }

  bool class_conversion_type(Id source, Id target, bool* added_cv = 0) const
  {
    if (source == none || target == none) return false;
    const bool source_const = is_const(source), source_volatile = is_volatile(source);
    const bool target_const = is_const(target), target_volatile = is_volatile(target);
    if ((source_const && !target_const) || (source_volatile && !target_volatile))
      return false;
    const Id source_base = strip_cv(source), target_base = strip_cv(target);
    if (type(source_base).kind != pa6::NamedType ||
        type(target_base).kind != pa6::NamedType) return false;
    const Id source_entity = type(source_base).entity;
    const Id target_entity = type(target_base).entity;
    if (source_entity == target_entity) return false;
    pa7::FlatIdSet visited;
    if (!derives_from(source_entity, target_entity, visited)) return false;
    if (added_cv)
      *added_cv = (!source_const && target_const) ||
                  (!source_volatile && target_volatile);
    return true;
  }

  void collect_directive_scope(Id scope, Id name,
                               pa7::FlatIdSet& visited,
                               pa7::BindingCandidates& result,
                               bool namespaces_only = false,
                               bool types_only = false) const
  {
    if (scope == none || !visited.insert(scope)) return;
    std::vector<Id> local = direct_bindings(scope, name, namespaces_only, types_only);
    if (!local.empty()) {
      for (std::size_t i = 0; i < local.size(); ++i) result.add(unit_, local[i]);
      return;
    }
    const std::vector<Id>& directives = visibility_.using_directives(scope);
    for (std::size_t i = 0; i < directives.size(); ++i)
        collect_directive_scope(directives[i], name, visited,
                                result, namespaces_only, types_only);
    const std::vector<Id>& inlines = visibility_.inline_namespaces(scope);
    for (std::size_t i = 0; i < inlines.size(); ++i)
        collect_directive_scope(inlines[i], name, visited,
                                result, namespaces_only, types_only);
  }

  std::vector<Id> lookup_namespace_scope(Id scope, Id name,
                                         bool namespaces_only = false,
                                         bool types_only = false) const
  {
    std::vector<Id> direct = direct_bindings(scope, name, namespaces_only, types_only);
    if (!direct.empty()) return direct;
    pa7::FlatIdSet visited;
    pa7::BindingCandidates result;
    const std::vector<Id>& directives = visibility_.using_directives(scope);
    for (std::size_t i = 0; i < directives.size(); ++i)
        collect_directive_scope(directives[i], name, visited,
                                result, namespaces_only, types_only);
    const std::vector<Id>& inlines = visibility_.inline_namespaces(scope);
    for (std::size_t i = 0; i < inlines.size(); ++i)
        collect_directive_scope(inlines[i], name, visited,
                                result, namespaces_only, types_only);
    return result.release();
  }

  std::vector<Id> lookup_name(Id name,
                              bool namespaces_only = false,
                              bool types_only = false) const
  {
    for (std::vector<Environment>::const_reverse_iterator i = environments_.rbegin();
         i != environments_.rend(); ++i) {
      const std::unordered_map<Id, std::vector<Id> >::const_iterator local =
          i->names.find(name);
      if (local != i->names.end()) {
        if (namespaces_only) break;
        return local->second;
      }
    }

    std::vector<Id> result;
    pa7::BindingCandidates deferred;
    pa7::FlatIdSet visited;
    for (Id scope = current_scope_; scope != none; scope = unit_.scope(scope).parent) {
      const pa6::ScopeRecord& record = unit_.scope(scope);
      if (record.kind == pa6::NamespaceScope) {
        result = direct_bindings(scope, name, namespaces_only, types_only);
        if (!result.empty()) return result;
        std::vector<Id> nominated = lookup_namespace_scope(scope, name,
                                                          namespaces_only, types_only);
        for (std::size_t i = 0; i < nominated.size(); ++i) deferred.add(unit_, nominated[i]);
      } else {
        if (namespaces_only) {
          result = direct_bindings(scope, name, true, false);
          if (!result.empty()) return result;
        }
        if (!types_only && !namespaces_only) {
          const std::vector<Id> declarations = direct_bindings(scope, name);
          for (std::size_t i = 0; i < declarations.size(); ++i)
            if (unit_.binding(declarations[i]).kind == pa6::EnumeratorBinding)
              deferred.add(unit_, declarations[i]);
        }
        for (std::vector<Environment>::const_reverse_iterator i = environments_.rbegin();
             i != environments_.rend(); ++i) {
          for (std::size_t d = 0; d < i->using_directives.size(); ++d)
            collect_directive_scope(i->using_directives[d], name, visited, deferred,
                                    namespaces_only, types_only);
        }
      }
    }
    return deferred.release();
  }

  Id namespace_scope(Id binding) const
  {
    if (binding == none || binding >= unit_.binding_count()) return none;
    const pa6::Binding& b = unit_.binding(binding);
    if (b.kind == pa6::NamespaceBinding && b.entity < unit_.entity_count())
      return unit_.entity(b.entity).scope;
    if ((b.kind == pa6::TypeBinding || b.kind == pa6::AliasBinding) &&
        b.type < unit_.type_count() && type(b.type).kind == pa6::NamedType)
      return unit_.entity(type(b.type).entity).scope;
    return none;
  }

  std::vector<Id> lookup_qualified(const std::vector<Id>& parts,
                                   bool absolute, bool namespaces_only = false,
                                   bool types_only = false) const
  {
    if (parts.empty()) return std::vector<Id>();
    if (parts.size() == 1) return lookup_name(parts[0], namespaces_only, types_only);
    std::vector<Id> first;
    if (absolute) {
      Id scope = unit_.global_scope();
      first = lookup_namespace_scope(scope, parts[0], true, false);
      if (first.empty()) first = lookup_namespace_scope(scope, parts[0], false, true);
    } else {
      first = lookup_name(parts[0], true, false);
      if (first.empty()) first = lookup_name(parts[0], false, true);
    }
    if (first.empty()) return std::vector<Id>();
    Id scope = namespace_scope(first[0]);
    if (scope == none) return std::vector<Id>();
    for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
      std::vector<Id> next = lookup_namespace_scope(scope, parts[i], true, false);
      if (next.empty()) next = lookup_namespace_scope(scope, parts[i], false, true);
      if (next.empty()) return std::vector<Id>();
      scope = namespace_scope(next[0]);
      if (scope == none) return std::vector<Id>();
    }
    return lookup_namespace_scope(scope, parts.back(), namespaces_only, types_only);
  }

  std::vector<Id> lookup_node(Id node, bool namespaces_only = false,
                              bool types_only = false) const
  {
    if (node == none || node >= ast_.nodes.size()) return std::vector<Id>();
    const pa6::SourceNamePath path = unit_.source_name_path(node);
    return lookup_qualified(path.components, path.absolute,
                            namespaces_only, types_only);
  }

  void push_environment()
  { environments_.push_back(Environment()); }

  void pop_environment()
  {
    if (environments_.empty()) throw std::logic_error("semantic environment underflow");
    environments_.pop_back();
  }

  void add_environment_binding(Id binding)
  {
    if (binding == none || binding >= unit_.binding_count()) return;
    visibility_.mark_binding(binding);
    if (environments_.empty()) return;
    const pa6::Binding& b = unit_.binding(binding);
    environments_.back().names[b.name_id].push_back(binding);
  }

  bool is_function_binding(Id id) const
  { return id != none && binding(id).kind == pa6::FunctionBinding; }

  bool is_primary_function_template(Id id) const
  {
    if (id == none || id >= unit_.binding_count() ||
        unit_.binding(id).kind != pa6::FunctionBinding) return false;
    const Id scope = unit_.binding(id).scope;
    return scope < unit_.scope_count() &&
        unit_.scope(scope).kind == pa6::TemplateScope;
  }

  int unqualified_rank(Id id) const
  {
    id = strip_cv(id);
    if (id == none || type(id).kind != pa6::FundamentalType) return 0;
    const std::string& n = type(id).atom;
    if (n == "long double") return 3;
    if (n == "double") return 2;
    if (n == "float") return 1;
    return 0;
  }

  Id promote_integral(Id id)
  {
    id = strip_cv(id);
    if (is_enum(id)) {
      if (is_scoped_enum(id)) return id;
      return unit_.fundamental_type("int");
    }
    if (id == none || type(id).kind != pa6::FundamentalType) return id;
    if (integer_rank(id) < integer_rank(unit_.fundamental_type("int"))) {
      if (type(id).atom == "unsigned short int" || type(id).atom == "unsigned char")
        return unit_.fundamental_type("int");
      return unit_.fundamental_type("int");
    }
    return id;
  }

  Id usual_arithmetic_type(Id a, Id b)
  {
    a = strip_cv(a); b = strip_cv(b);
    if (is_floating(a) || is_floating(b)) {
      const int rank = std::max(unqualified_rank(a), unqualified_rank(b));
      return unit_.fundamental_type(rank == 3 ? "long double" : rank == 2 ? "double" : "float");
    }
    a = promote_integral(a); b = promote_integral(b);
    if (a == b) return a;
    const int ra = integer_rank(a), rb = integer_rank(b);
    const bool ua = is_unsigned(a), ub = is_unsigned(b);
    if (ua == ub) return ra >= rb ? a : b;
    const Id u = ua ? a : b;
    const Id s = ua ? b : a;
    if (integer_rank(u) >= integer_rank(s)) return u;
    return s;
  }

  bool pointer_qualification(Id source, Id target, unsigned depth = 0,
                             bool* added = 0) const
  {
    bool local_added = false;
    if (source == none || target == none) return false;
    const bool sc = is_const(source), sv = is_volatile(source);
    const bool tc = is_const(target), tv = is_volatile(target);
    if ((sc && !tc) || (sv && !tv)) return false;
    local_added = (!sc && tc) || (!sv && tv);
    Id s = strip_cv(source), t = strip_cv(target);
    if (s == t) {
      if (added) *added = local_added;
      return true;
    }
    if (type(s).kind != pa6::PointerType || type(t).kind != pa6::PointerType)
      return false;
    bool child_added = false;
    if (!pointer_qualification(type(s).base, type(t).base, depth + 1, &child_added))
      return false;
    if (child_added && depth > 0 && !tc) return false;
    if (added) *added = local_added || child_added;
    return true;
  }

  bool qualification_compatible(Id source, Id target, bool* added = 0) const
  {
    bool local_added = false;
    if (source == none || target == none) return false;
    const bool sc = is_const(source), sv = is_volatile(source);
    const bool tc = is_const(target), tv = is_volatile(target);
    if ((sc && !tc) || (sv && !tv)) return false;
    local_added = (!sc && tc) || (!sv && tv);
    const Id s = strip_cv(source), t = strip_cv(target);
    if (s == t) {
      if (added) *added = local_added;
      return true;
    }
    if (type(s).kind == pa6::ArrayType && type(t).kind == pa6::ArrayType &&
        type(s).bound == type(t).bound) {
      bool child_added = false;
      if (!qualification_compatible(type(s).base, type(t).base, &child_added)) return false;
      if (added) *added = local_added || child_added;
      return true;
    }
    if (type(s).kind == pa6::PointerType && type(t).kind == pa6::PointerType) {
      bool pointer_added = false;
      if (!pointer_qualification(s, t, 0, &pointer_added)) return false;
      if (added) *added = local_added || pointer_added;
      return true;
    }
    return false;
  }

  Conversion arithmetic_conversion(Id source, Id target) const
  {
    source = strip_cv(source); target = strip_cv(target);
    if (source == target) return Conversion(true, 0, 0);
    if (!is_arithmetic(source) || !is_arithmetic(target)) return Conversion();
    if (is_integral(source) && is_integral(target)) {
      const Id promoted = const_cast<SemanticDumper*>(this)->promote_integral(source);
      if (promoted == target && source != promoted) return Conversion(true, 1, 0);
      return Conversion(true, 2, 0);
    }
    if (is_floating(source) && is_floating(target)) {
      if (source == unit_.fundamental_type("float") && target == unit_.fundamental_type("double"))
        return Conversion(true, 1, 0);
      return Conversion(true, 2, std::abs(unqualified_rank(source) - unqualified_rank(target)));
    }
    return Conversion(true, 2, 0);
  }

  Id array_or_function_decay(Id id)
  {
    id = strip_cv(id);
    if (id == none) return none;
    if (type(id).kind == pa6::ArrayType) return unit_.pointer_type(type(id).base);
    if (type(id).kind == pa6::FunctionType) return unit_.pointer_type(id);
    return id;
  }

  bool same_function_type(Id a, Id b)
  {
    a = canonical_function(a);
    b = canonical_function(b);
    if (a == b) return true;
    if (!is_function(a) || !is_function(b)) return false;
    const pa6::Type& x = type(a);
    const pa6::Type& y = type(b);
    if (x.base != y.base || x.variadic != y.variadic ||
        x.member_const != y.member_const || x.member_volatile != y.member_volatile ||
        x.ref_qualifier != y.ref_qualifier ||
        x.parameters.size() != y.parameters.size()) return false;
    for (std::size_t i = 0; i < x.parameters.size(); ++i) {
      Id xp = strip_cv(x.parameters[i]);
      Id yp = strip_cv(y.parameters[i]);
      if (type(xp).kind == pa6::ArrayType) xp = unit_.pointer_type(type(xp).base);
      else if (type(xp).kind == pa6::FunctionType) xp = unit_.pointer_type(xp);
      if (type(yp).kind == pa6::ArrayType) yp = unit_.pointer_type(type(yp).base);
      else if (type(yp).kind == pa6::FunctionType) yp = unit_.pointer_type(yp);
      if (xp != yp) return false;
    }
    return true;
  }

  Id resolve_function_overload(const ExpressionFact& source, Id target)
  {
    if (source.overloads.empty()) return none;
    target = strip_reference(target);
    const Id target_value = strip_cv(target);
    const bool member_target = type(target_value).kind == pa6::MemberPointerType;
    const Id target_class = member_target ? type(target_value).entity : none;
    Id target_function = member_target ? type(target_value).base : function_type_from(target_value);
    if (target_function == none && is_function(target)) target_function = target;
    if (target_function == none) return none;
    Id selected = none;
    for (std::size_t i = 0; i < source.overloads.size(); ++i) {
      const Id candidate = source.overloads[i];
      const Id candidate_type = canonical_function(binding(candidate).type);
      if (!same_function_type(candidate_type, target_function)) continue;
      const pa6::Binding& candidate_binding = binding(candidate);
      const pa6::ScopeRecord& owner = unit_.scope(candidate_binding.scope);
      if (member_target) {
        if (owner.kind != pa6::ClassScope || owner.entity != target_class) continue;
      } else if (owner.kind == pa6::ClassScope) continue;
      if (selected != none) return none;
      selected = candidate;
    }
    demand_function_instance(selected);
    return selected;
  }

  Id function_address_type(Id binding)
  {
    const Id function = canonical_function(this->binding(binding).type);
    const pa6::ScopeRecord& owner = unit_.scope(this->binding(binding).scope);
    if (owner.kind == pa6::ClassScope)
      return unit_.member_pointer_type(owner.entity, function);
    return unit_.pointer_type(function);
  }

  Conversion conversion(const ExpressionFact& source, Id target, Id node = none)
  {
    if (!source.overloads.empty()) {
      if (target != none &&
          type(target).kind == pa6::RvalueReferenceType && source.category == LValue)
        return Conversion();
      const Id selected = resolve_function_overload(source, target);
      if (selected == none) return Conversion();
      return Conversion(true, 0, 0);
    }
    if (source.type == none || target == none) return Conversion();
    const pa6::Type& target_type = type(target);
    if (target_type.kind == pa6::LvalueReferenceType ||
        target_type.kind == pa6::RvalueReferenceType) {
      const bool lref = target_type.kind == pa6::LvalueReferenceType;
      const Id referent = target_type.base;
      if (lref && source.category == LValue) {
        bool added = false;
        if (qualification_compatible(source.type, referent, &added))
          return Conversion(true, 0, added ? 1 : 0);
        if (class_conversion_type(source.type, referent, &added))
          return Conversion(true, 2, added ? 1 : 0);
      }
      if (same_unqualified(source.type, referent) &&
          ((is_const(source.type) && !is_const(referent)) ||
           (is_volatile(source.type) && !is_volatile(referent))))
        return Conversion();
      if (lref && source.category == LValue && same_unqualified(source.type, referent)) {
        return Conversion(true, 0, (!is_const(source.type) && is_const(referent)) ? 1 : 0);
      }
      if (lref && is_const(referent) && source.category != LValue &&
          same_unqualified(source.type, referent))
        return Conversion(true, 0, 1);
      if (!lref && source.category != LValue && same_unqualified(source.type, referent))
        return Conversion(true, 0, 0);
      if (lref && !is_const(referent)) return Conversion();
      if (!lref && source.category == LValue && same_unqualified(source.type, referent))
        return Conversion();
      Conversion value = conversion(source, referent, node);
      if (!value.viable) return value;
      if (lref && source.category == LValue && is_const(referent)) ++value.detail;
      if (!lref && source.category == PRValue && value.rank != 0) return value;
      return value;
    }

    Id source_type = strip_cv(source.type);
    Id target_value = strip_cv(target);
    if (source_type == target_value) return Conversion(true, 0, 0);

    const Id decayed_source = array_or_function_decay(source_type);
    if (decayed_source != source_type) {
      ExpressionFact decayed = source;
      decayed.type = decayed_source;
      decayed.category = PRValue;
      decayed.overloads.clear();
      return conversion(decayed, target, node);
    }

    if (is_nullptr_type(source_type) && is_pointer(target_value))
      return Conversion(true, 2, 0);
    if (source.null_pointer_constant && is_nullptr_type(target_value))
      return Conversion(true, 2, 0);
    if (source.null_pointer_constant && is_pointer(target_value))
      return Conversion(true, 2, 0);

    if (is_pointer(source_type) && is_pointer(target_value)) {
      const Id source_pointee = type(source_type).base;
      const Id target_pointee = type(target_value).base;
      if (is_function(source_pointee) && is_function(target_pointee) &&
          same_function_type(source_pointee, target_pointee))
        return Conversion(true, 0, 0);
      bool added = false;
      if (pointer_qualification(source_type, target_value, 0, &added))
        return Conversion(true, 0, added ? 1 : 0);
      const Id su = strip_cv(source_pointee), tu = strip_cv(target_pointee);
      if (class_conversion_type(source_pointee, target_pointee, &added))
        return Conversion(true, 2, added ? 1 : 0);
      if (is_void(tu) && (type(su).kind != pa6::FunctionType) &&
          (!is_const(source_pointee) || is_const(target_pointee)) &&
          (!is_volatile(source_pointee) || is_volatile(target_pointee)))
        return Conversion(true, 2, 0);
    }
    if (is_pointer(source_type) && is_integral(target_value) &&
        type(target_value).kind == pa6::FundamentalType && type(target_value).atom == "bool")
      return Conversion(true, 2, 10);
    if (is_integral(source_type) && is_pointer(target_value) && source.null_pointer_constant)
      return Conversion(true, 2, 0);
    return arithmetic_conversion(source_type, target_value);
  }

  bool better_conversion(const Conversion& a, const Conversion& b) const
  {
    if (a.rank != b.rank) return a.rank < b.rank;
    return a.detail < b.detail;
  }

  bool same_conversion(const Conversion& a, const Conversion& b) const
  { return a.rank == b.rank && a.detail == b.detail; }

  bool candidate_better(const std::vector<Conversion>& a,
                        const std::vector<Conversion>& b) const
  {
    bool strictly = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (better_conversion(b[i], a[i])) return false;
      if (better_conversion(a[i], b[i])) strictly = true;
    }
    return strictly;
  }

  bool same_conversion_vectors(const std::vector<Conversion>& a,
                               const std::vector<Conversion>& b) const
  {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (!same_conversion(a[i], b[i])) return false;
    return true;
  }

  Id select_overload(const std::vector<Id>& candidates,
                     const std::vector<Id>& arguments,
                     const std::vector<ExpressionFact>& argument_facts,
                     std::vector<Id>* selected_targets = 0)
  {
    struct Viable {
      Id binding;
      std::vector<Conversion> conversions;
      std::vector<Id> targets;
    };
    std::vector<Viable> viable;
    for (std::size_t ci = 0; ci < candidates.size(); ++ci) {
      Id candidate = candidates[ci];
      if (!is_function_binding(candidate)) continue;
      if (is_primary_function_template(candidate)) {
        candidate = instantiate_template_function(candidate, std::vector<Id>(),
                                                   &argument_facts);
        if (candidate == none) continue;
      }
      const Id fn = canonical_function(binding(candidate).type);
      if (!is_function(fn)) continue;
      const pa6::Type& signature = type(fn);
      const std::size_t fixed = signature.parameters.size();
      if ((!signature.variadic && arguments.size() != fixed) ||
          (signature.variadic && arguments.size() < fixed)) continue;
      Viable item; item.binding = candidate;
      bool okay = true;
      for (std::size_t i = 0; i < fixed; ++i) {
        Conversion cvt = conversion(argument_facts[i], signature.parameters[i], arguments[i]);
        if (!cvt.viable) { okay = false; break; }
        item.conversions.push_back(cvt);
        item.targets.push_back(signature.parameters[i]);
      }
      if (!okay) continue;
      for (std::size_t i = fixed; i < arguments.size(); ++i) {
        item.conversions.push_back(Conversion(true, 3, 0));
        item.targets.push_back(none);
      }
      viable.push_back(item);
    }
    if (viable.empty()) throw std::runtime_error("no viable function overload");
    std::vector<std::size_t> best;
    for (std::size_t i = 0; i < viable.size(); ++i) {
      bool dominated = false;
      for (std::size_t j = 0; j < viable.size(); ++j) {
        if (i == j) continue;
        bool better = candidate_better(viable[j].conversions, viable[i].conversions);
        if (!better && same_conversion_vectors(viable[i].conversions,
                                               viable[j].conversions) &&
            !is_instantiated_binding(viable[j].binding) &&
            is_instantiated_binding(viable[i].binding))
          better = true;
        if (better) {
          dominated = true; break;
        }
      }
      if (!dominated) best.push_back(i);
    }
    if (best.size() != 1) throw std::runtime_error("ambiguous function overload");
    const Viable& result = viable[best[0]];
    if (selected_targets) *selected_targets = result.targets;
    demand_function_instance(result.binding);
    return result.binding;
  }

  ExpressionFact invalid_fact() const { return ExpressionFact(); }

  Id literal_type(const std::string& spelling, long long* constant,
                  bool* has_constant, bool* null_pointer_constant)
  {
    *has_constant = false;
    *null_pointer_constant = false;
    *constant = 0;
    if (spelling == "true" || spelling == "false") {
      *constant = spelling == "true" ? 1 : 0;
      *has_constant = true;
      return unit_.fundamental_type("bool");
    }
    if (spelling == "nullptr") return unit_.fundamental_type("nullptr_t");
    std::size_t quote = spelling.find('\'');
    if (quote != std::string::npos) {
      if (quote + 1 < spelling.size()) {
        *constant = static_cast<unsigned char>(spelling[quote + 1]);
        *has_constant = true;
      }
      return unit_.fundamental_type("int");
    }
    if (spelling.find('"') != std::string::npos) {
      std::string body = spelling;
      long long chars = 1;
      const std::size_t raw = body.find("R\"");
      if (raw != std::string::npos) {
        const std::size_t delimiter_begin = raw + 2;
        const std::size_t open_paren = body.find('(', delimiter_begin);
        if (open_paren == std::string::npos) throw std::runtime_error("invalid raw string literal");
        const std::string delimiter = body.substr(delimiter_begin, open_paren - delimiter_begin);
        const std::string terminator = ")" + delimiter + "\"";
        const std::size_t close_paren = body.rfind(terminator);
        if (close_paren == std::string::npos || close_paren < open_paren)
          throw std::runtime_error("invalid raw string literal");
        chars += static_cast<long long>(close_paren - open_paren - 1);
      } else {
        const std::size_t open = body.find('"');
        const std::size_t close = body.rfind('"');
        if (open != std::string::npos && close > open) {
          for (std::size_t i = open + 1; i < close; ++i) {
            if (body[i] == '\\' && i + 1 < close) ++i;
            ++chars;
          }
        }
      }
      Id character = unit_.fundamental_type("char");
      if (spelling.compare(0, 2, "u\"") == 0) character = unit_.fundamental_type("char16_t");
      else if (spelling.compare(0, 2, "U\"") == 0) character = unit_.fundamental_type("char32_t");
      else if (spelling.compare(0, 2, "L\"") == 0) character = unit_.fundamental_type("wchar_t");
      character = unit_.qualified_type(character, true, false);
      return unit_.array_type(character, chars);
    }
    const bool hex_prefix = spelling.size() > 2 && spelling[0] == '0' &&
        (spelling[1] == 'x' || spelling[1] == 'X');
    const bool hex_float_marker = spelling.find_first_of(".pP") != std::string::npos;
    if (spelling.find_first_of(".eEpP") != std::string::npos &&
        (!hex_prefix || hex_float_marker)) {
      const char last = spelling.empty() ? 0 : spelling[spelling.size() - 1];
      if (last == 'f' || last == 'F') return unit_.fundamental_type("float");
      if (last == 'l' || last == 'L') return unit_.fundamental_type("long double");
      return unit_.fundamental_type("double");
    }
    std::size_t suffix = spelling.size();
    while (suffix && (spelling[suffix - 1] == 'u' || spelling[suffix - 1] == 'U' ||
           spelling[suffix - 1] == 'l' || spelling[suffix - 1] == 'L')) --suffix;
    if (suffix == 0) throw std::runtime_error("invalid integer literal");
    const std::string digits = spelling.substr(0, suffix);
    errno = 0;
    char* end = 0;
    const unsigned long long value = std::strtoull(digits.c_str(), &end, 0);
    if (errno == ERANGE || end == digits.c_str() || *end ||
        value > static_cast<unsigned long long>(LLONG_MAX))
      throw std::runtime_error("integer literal is outside the supported range");
    *constant = static_cast<long long>(value);
    *has_constant = true;
    const bool uns = spelling.find_first_of("uU") != std::string::npos;
    const bool lng = spelling.find_first_of("lL") != std::string::npos;
    Id result = unit_.fundamental_type(uns ? (lng ? "unsigned long int" : "unsigned int")
                                           : (lng ? "long int" : "int"));
    *null_pointer_constant = !uns && !lng && *constant == 0;
    return result;
  }

  ExpressionFact expression(Id node, Id expected = none)
  {
    if (node == none || node >= ast_.nodes.size())
      throw std::runtime_error("missing expression");
    if (fact_ready_[node]) {
      ExpressionFact result = facts_[node];
      if (!result.overloads.empty() && expected != none) {
        const Id selected = resolve_function_overload(result, expected);
        if (selected == none) throw std::runtime_error("overloaded function name has no target match");
        result.binding = selected;
        if (ast_.nodes[node].kind == NUnaryExpression && text(node) == "&") {
          result.type = function_address_type(selected);
          result.category = PRValue;
        } else {
          result.type = canonical_function(binding(selected).type);
          result.category = LValue;
        }
        result.overloads.clear();
      }
      return result;
    }
    if (++analysis_depth_ > ast_.nodes.size() + 8)
      throw std::runtime_error("expression nesting overflow");
    ExpressionFact result;
    const NodeKind kind = ast_.nodes[node].kind;
    const std::vector<Id> kids = children(node);
    if (kind == NLiteral || kind == NTaggedLiteral || kind == NKeywordLiteral) {
      const std::string spelling = text(node);
      bool has_constant = false, null_constant = false;
      long long value = 0;
      result.type = literal_type(spelling, &value, &has_constant, &null_constant);
      result.category = type(result.type).kind == pa6::ArrayType ? LValue : PRValue;
      result.constant = has_constant;
      result.value = value;
      result.null_pointer_constant = null_constant;
    } else if (kind == NIdExpression || kind == NIdentifier) {
      const std::vector<Id> found = lookup_node(node);
      if (found.empty()) throw std::runtime_error("unresolved identifier: " + node_name(node));
      pa7::BindingCandidates function_candidates;
      Id first_non_function = none;
      for (std::size_t i = 0; i < found.size(); ++i) {
        if (is_function_binding(found[i])) function_candidates.add(unit_, found[i]);
        else if (first_non_function == none) first_non_function = found[i];
      }
      std::vector<Id> functions = function_candidates.release();
      std::vector<Id> explicit_template_types;
      const bool explicit_template_id = pa7::ExplicitFunctionTemplateTypes(
          unit_, ast_, current_scope_, node, explicit_template_types);
      if (explicit_template_id) {
        pa7::BindingCandidates instance_candidates;
        for (std::size_t i = 0; i < functions.size(); ++i) {
          if (!is_primary_function_template(functions[i])) continue;
          const Id instance = instantiate_template_function(functions[i],
                                                             explicit_template_types);
          if (instance != none)
            instance_candidates.add(instance, binding(instance).kind,
                                    binding(instance).entity);
        }
        std::vector<Id> instances = instance_candidates.release();
        functions.swap(instances);
        first_non_function = none;
      }
      if (!functions.empty() && first_non_function == none) {
        result.overloads = functions;
        if (functions.size() == 1 || expected != none) {
          const Id selected = expected == none ? functions[0] : resolve_function_overload(result, expected);
          if (selected == none) throw std::runtime_error("ambiguous overloaded function name");
          result.binding = selected;
          result.type = canonical_function(binding(selected).type);
          result.category = LValue;
          result.overloads.clear();
          result.overloads.push_back(selected);
        }
      } else {
        const Id binding = first_non_function != none ? first_non_function : functions[0];
        result.binding = binding;
        const pa6::Binding& b = this->binding(binding);
        if (b.kind == pa6::EnumeratorBinding) {
          result.type = canonical_type(b.type);
          result.category = PRValue;
          result.constant = b.has_value;
          result.value = b.value;
        } else {
          result.type = canonical_type(b.type);
          if (type(result.type).kind == pa6::LvalueReferenceType ||
              type(result.type).kind == pa6::RvalueReferenceType)
            result.type = strip_reference(result.type, &result.category);
          else result.category = LValue;
          if (b.has_value) {
            result.constant = true;
            result.value = b.value;
            result.null_pointer_constant = is_integral(result.type) && !is_enum(result.type) &&
                result.value == 0;
          }
        }
      }
    } else if (kind == NParenthesizedExpression) {
      result = expression(kids.empty() ? none : kids[0], expected);
    } else if (kind == NCallExpression) {
      result = analyze_call(node, kids);
    } else if (kind == NUnaryExpression) {
      result = analyze_unary(node, kids, expected);
    } else if (kind == NPostfixExpression) {
      result = analyze_postfix(node, kids);
    } else if (kind == NBinaryExpression || kind == NAssignmentExpression) {
      result = analyze_binary(node, kids, kind == NAssignmentExpression);
    } else if (kind == NConditionalExpression) {
      result = analyze_conditional(node, kids);
    } else if (kind == NSubscriptExpression) {
      result = analyze_subscript(node, kids);
    } else if (kind == NSizeofExpression || kind == NTypeTraitExpression) {
      result = analyze_sizeof(node, kids);
    } else if (kind == NCastExpression) {
      result = analyze_cast(node, kids);
    } else if (kind == NMemberExpression) {
      result = analyze_member(node, kids);
    } else {
      throw std::runtime_error("unsupported PA7 expression node");
    }
    --analysis_depth_;
    if (!result.overloads.empty() && expected != none) {
      const Id selected = resolve_function_overload(result, expected);
      if (selected == none) throw std::runtime_error("overloaded function name has no target match");
      result.binding = selected;
      if (kind == NUnaryExpression && text(node) == "&") {
        result.type = function_address_type(selected);
        result.category = PRValue;
      } else {
        result.type = canonical_function(binding(selected).type);
        result.category = LValue;
      }
      result.overloads.clear();
    }
    if (expected == none) {
      facts_[node] = result;
      fact_ready_[node] = true;
    }
    return result;
  }

  ExpressionFact analyze_unary(Id node, const std::vector<Id>& kids,
                               Id expected = none)
  {
    if (kids.empty()) throw std::runtime_error("unary expression lacks an operand");
    const std::string op = text(node);
    Id operand_expected = none;
    if (op == "&" && expected != none) {
      const Id target = strip_cv(strip_reference(expected));
      if (type(target).kind == pa6::PointerType && is_function(type(target).base))
        operand_expected = target;
      else if (type(target).kind == pa6::MemberPointerType &&
               is_function(type(target).base))
        operand_expected = target;
    }
    ExpressionFact operand = expression(kids[0], operand_expected);
    ExpressionFact result;
    const Id t = strip_cv(operand.type);
    if (op == "&") {
      if (operand.type == none && !operand.overloads.empty()) {
        result.overloads = operand.overloads;
        result.category = PRValue;
        return result;
      }
      if (operand.category == PRValue || operand.type == none)
        throw std::runtime_error("address-of requires an lvalue or function");
      if (operand.binding != none &&
          unit_.scope(binding(operand.binding).scope).kind == pa6::ClassScope)
        result.type = unit_.member_pointer_type(
            unit_.scope(binding(operand.binding).scope).entity, operand.type);
      else result.type = unit_.pointer_type(operand.type);
      if (operand.binding != none &&
          binding(operand.binding).kind == pa6::FunctionBinding)
        result.binding = operand.binding;
      result.category = PRValue;
    } else if (op == "*") {
      Id pointer = array_or_function_decay(t);
      if (type(pointer).kind == pa6::PointerType) pointer = type(pointer).base;
      else throw std::runtime_error("dereference requires a pointer");
      result.type = pointer;
      result.category = is_function(pointer) ? LValue : LValue;
    } else if (op == "++" || op == "--") {
      if (operand.category != LValue || is_const(operand.type) ||
          (!is_arithmetic(t) && !is_pointer(t)))
        throw std::runtime_error("increment requires a modifiable scalar lvalue");
      result.type = operand.type;
      result.category = LValue;
    } else if (op == "!") {
      if (!is_scalar(operand.type)) throw std::runtime_error("logical negation requires a scalar");
      result.type = unit_.fundamental_type("bool");
      result.category = PRValue;
      result.constant = operand.constant;
      result.value = !operand.value;
    } else if (op == "+" || op == "-") {
      if (!is_arithmetic(t)) throw std::runtime_error("unary arithmetic requires arithmetic type");
      result.type = is_integral(t) ? promote_integral(t) : t;
      result.category = PRValue;
      result.constant = operand.constant;
      result.value = op == "-" ? -operand.value : operand.value;
    } else if (op == "~") {
      if (!is_integral(t) || is_scoped_enum(t))
        throw std::runtime_error("bitwise complement requires an integral operand");
      result.type = promote_integral(t);
      result.category = PRValue;
      result.constant = operand.constant;
      result.value = ~operand.value;
    } else throw std::runtime_error("unsupported unary operator");
    return result;
  }

  ExpressionFact analyze_postfix(Id node, const std::vector<Id>& kids)
  {
    if (kids.empty()) throw std::runtime_error("postfix expression lacks an operand");
    ExpressionFact operand = expression(kids[0]);
    const Id t = strip_cv(operand.type);
    if (operand.category != LValue || is_const(operand.type) ||
        (!is_arithmetic(t) && !is_pointer(t)))
      throw std::runtime_error("postfix increment requires a modifiable scalar lvalue");
    ExpressionFact result;
    result.type = operand.type;
    result.category = PRValue;
    return result;
  }

  ExpressionFact analyze_binary(Id node, const std::vector<Id>& kids, bool assignment)
  {
    if (kids.size() < 2) throw std::runtime_error("binary expression lacks operands");
    ExpressionFact a = expression(kids[0]);
    ExpressionFact b = expression(kids[1]);
    const Id at = array_or_function_decay(strip_cv(a.type));
    const Id bt = array_or_function_decay(strip_cv(b.type));
    const std::string op = text(node);
    ExpressionFact result;
    if (assignment || op == "=" || (op.size() > 1 && op.substr(op.size() - 1) == "=" &&
        op != "==" && op != "!=" && op != "<=" && op != ">=")) {
      if (a.category != LValue || is_const(a.type))
        throw std::runtime_error("assignment requires a modifiable lvalue");
      if (op == "=") {
        if (!conversion(b, a.type, kids[1]).viable)
          throw std::runtime_error("invalid assignment conversion");
      } else {
        const std::string baseop = op.substr(0, op.size() - 1);
        bool valid = false;
        if ((baseop == "+" || baseop == "-") && is_pointer(at) && is_integral(bt)) valid = true;
        else if (is_arithmetic(at) && is_arithmetic(bt) &&
                 !(baseop == "&" || baseop == "|" || baseop == "^" ||
                   baseop == "<<" || baseop == ">>")) valid = true;
        else if (is_integral(at) && is_integral(bt) &&
                 (baseop == "&" || baseop == "|" || baseop == "^" ||
                  baseop == "<<" || baseop == ">>")) valid = true;
        if (!valid) throw std::runtime_error("invalid compound assignment operands");
      }
      result.type = a.type;
      result.category = LValue;
      return result;
    }
    if (op == ",") {
      result = b;
      result.constant = b.constant;
      result.value = b.value;
      return result;
    }
    if (op == "&&" || op == "||") {
      if (!is_scalar(a.type) || !is_scalar(b.type))
        throw std::runtime_error("logical operator requires scalar operands");
      result.type = unit_.fundamental_type("bool");
      result.category = PRValue;
      if (a.constant && b.constant) {
        result.constant = true;
        result.value = op == "&&" ? (a.value && b.value) : (a.value || b.value);
      }
      return result;
    }
    const bool equality = op == "==" || op == "!=";
    const bool relational = op == "<" || op == ">" || op == "<=" || op == ">=";
    if (equality || relational) {
      bool valid = false;
      if (is_arithmetic(at) && is_arithmetic(bt)) valid = true;
      if (is_enum(at) && at == bt) valid = true;
      if (is_pointer(at) && is_pointer(bt)) {
        if (pointer_qualification(at, bt) || pointer_qualification(bt, at)) valid = true;
      }
      if (equality && ((is_pointer(at) && (b.null_pointer_constant || is_nullptr_type(bt))) ||
          (is_pointer(bt) && (a.null_pointer_constant || is_nullptr_type(at))))) valid = true;
      if (equality && at == bt && (is_pointer(at) || is_nullptr_type(at))) valid = true;
      if (!valid) throw std::runtime_error("invalid comparison operands");
      result.type = unit_.fundamental_type("bool");
      result.category = PRValue;
      if (a.constant && b.constant) {
        result.constant = true;
        if (op == "==") result.value = a.value == b.value;
        else if (op == "!=") result.value = a.value != b.value;
        else if (op == "<") result.value = a.value < b.value;
        else if (op == ">") result.value = a.value > b.value;
        else if (op == "<=") result.value = a.value <= b.value;
        else result.value = a.value >= b.value;
      }
      return result;
    }
    if (op == "+" && is_pointer(at) && is_integral(bt)) {
      result.type = at; result.category = PRValue; return result;
    }
    if (op == "+" && is_integral(at) && is_pointer(bt)) {
      result.type = bt; result.category = PRValue; return result;
    }
    if (op == "-" && is_pointer(at) && is_integral(bt)) {
      result.type = at; result.category = PRValue; return result;
    }
    if (op == "-" && is_pointer(at) && is_pointer(bt) &&
        (at == bt || pointer_qualification(at, bt) || pointer_qualification(bt, at))) {
      result.type = unit_.fundamental_type("long int"); result.category = PRValue; return result;
    }
    const bool bitwise = op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>";
    if (bitwise && (!is_integral(at) || !is_integral(bt) ||
                    is_scoped_enum(at) || is_scoped_enum(bt)))
      throw std::runtime_error("bitwise operator requires integral operands");
    if (!is_arithmetic(at) || !is_arithmetic(bt))
      throw std::runtime_error("arithmetic operator requires arithmetic operands");
    result.type = bitwise ? usual_arithmetic_type(promote_integral(at), promote_integral(bt))
                          : usual_arithmetic_type(at, bt);
    result.category = PRValue;
    if (a.constant && b.constant) {
      result.constant = true;
      if (op == "+") result.value = a.value + b.value;
      else if (op == "-") result.value = a.value - b.value;
      else if (op == "*") result.value = a.value * b.value;
      else if (op == "/" && b.value) result.value = a.value / b.value;
      else if (op == "%" && b.value) result.value = a.value % b.value;
      else if (op == "&") result.value = a.value & b.value;
      else if (op == "|") result.value = a.value | b.value;
      else if (op == "^") result.value = a.value ^ b.value;
      else if (op == "<<" && b.value >= 0 && b.value < 63) result.value = a.value << b.value;
      else if (op == ">>" && b.value >= 0 && b.value < 63) result.value = a.value >> b.value;
      else result.constant = false;
    }
    return result;
  }

  ExpressionFact analyze_conditional(Id, const std::vector<Id>& kids)
  {
    if (kids.size() < 3) throw std::runtime_error("conditional expression lacks an operand");
    ExpressionFact condition = expression(kids[0]);
    ExpressionFact yes = expression(kids[1]);
    ExpressionFact no = expression(kids[2]);
    if (!is_scalar(condition.type)) throw std::runtime_error("conditional test is not scalar");
    ExpressionFact result;
    if (same_unqualified(yes.type, no.type)) {
      if (yes.category == no.category && yes.category != PRValue) {
        result.type = unit_.qualified_type(strip_cv(yes.type),
            is_const(yes.type) || is_const(no.type),
            is_volatile(yes.type) || is_volatile(no.type));
        result.category = yes.category;
      } else {
        result.type = strip_cv(yes.type);
        result.category = PRValue;
      }
    } else if (is_enum(yes.type) && yes.type == no.type) {
      result.type = yes.type;
      result.category = PRValue;
    } else if (is_arithmetic(yes.type) && is_arithmetic(no.type)) {
      result.type = usual_arithmetic_type(yes.type, no.type);
      result.category = PRValue;
    } else if (is_pointer(yes.type) && is_pointer(no.type) &&
               (pointer_qualification(yes.type, no.type) ||
                pointer_qualification(no.type, yes.type))) {
      result.type = pointer_qualification(yes.type, no.type) ? no.type : yes.type;
      result.category = PRValue;
    } else if (is_pointer(yes.type) && (no.null_pointer_constant || is_nullptr_type(no.type))) {
      result.type = yes.type; result.category = PRValue;
    } else if (is_pointer(no.type) && (yes.null_pointer_constant || is_nullptr_type(yes.type))) {
      result.type = no.type; result.category = PRValue;
    } else if (yes.type == no.type) {
      result.type = yes.type; result.category = PRValue;
    } else throw std::runtime_error("conditional operands have no common type");
    const ExpressionFact& chosen = condition.constant && condition.value ? yes : no;
    if (condition.constant && chosen.constant) {
      result.constant = true; result.value = chosen.value;
      result.null_pointer_constant = chosen.null_pointer_constant;
    }
    return result;
  }

  ExpressionFact analyze_subscript(Id, const std::vector<Id>& kids)
  {
    if (kids.size() < 2) throw std::runtime_error("subscript expression lacks operands");
    ExpressionFact a = expression(kids[0]);
    ExpressionFact b = expression(kids[1]);
    Id base = strip_cv(a.type);
    if (is_integral(base) && (is_pointer(b.type) || is_array(b.type))) {
      std::swap(a, b); base = strip_cv(a.type);
    }
    Id element = none;
    if (type(base).kind == pa6::PointerType) element = type(base).base;
    else if (type(base).kind == pa6::ArrayType) element = type(base).base;
    if (element == none || !is_integral(b.type))
      throw std::runtime_error("invalid subscript operands");
    ExpressionFact result;
    result.type = element;
    result.category = LValue;
    return result;
  }

  bool type_size(Id id, long long& size) const
  {
    if (id == none || id >= unit_.type_count()) return false;
    const pa6::Type& t = type(id);
    if (t.kind == pa6::QualifiedType) return type_size(t.base, size);
    if (t.kind == pa6::PointerType || t.kind == pa6::LvalueReferenceType ||
        t.kind == pa6::RvalueReferenceType) { size = 8; return true; }
    if (t.kind == pa6::ArrayType) {
      long long element = 0;
      if (t.bound <= 0 || !type_size(t.base, element) ||
          element > LLONG_MAX / t.bound) return false;
      size = element * t.bound; return true;
    }
    if (t.kind == pa6::NamedType && t.entity < unit_.entity_count()) {
      const pa6::Entity& entity = unit_.entity(t.entity);
      if (entity.kind == pa6::EnumEntity) {
        Id underlying = entity.underlying == none ? unit_.fundamental_type("int") : entity.underlying;
        return type_size(underlying, size);
      }
      return false;
    }
    if (t.kind != pa6::FundamentalType) return false;
    const std::string& n = t.atom;
    if (n == "void" || n == "nullptr_t") return false;
    if (n == "bool" || n == "char" || n == "signed char" || n == "unsigned char") size = 1;
    else if (n == "short int" || n == "unsigned short int" || n == "char16_t") size = 2;
    else if (n == "int" || n == "unsigned int" || n == "float" || n == "wchar_t" || n == "char32_t") size = 4;
    else if (n == "long int" || n == "unsigned long int" || n == "long long int" ||
             n == "unsigned long long int" || n == "double") size = 8;
    else if (n == "long double") size = 16;
    else return false;
    return true;
  }

  ExpressionFact analyze_sizeof(Id node, const std::vector<Id>& kids)
  {
    if (kids.empty()) throw std::runtime_error("sizeof has no operand");
    Id operand_type = none;
    if (ast_.nodes[kids[0]].kind == NTypeId)
      operand_type = unit_.resolve_type_node(kids[0], current_scope_);
    else operand_type = expression(kids[0]).type;
    long long size = 0;
    if (!type_size(operand_type, size)) throw std::runtime_error("sizeof incomplete type");
    ExpressionFact result;
    result.type = unit_.fundamental_type("unsigned long int");
    result.category = PRValue;
    result.constant = true;
    result.value = size;
    (void)node;
    return result;
  }

  ExpressionFact analyze_cast(Id node, const std::vector<Id>& kids)
  {
    if (kids.size() < 2) throw std::runtime_error("cast expression is incomplete");
    const Id target = unit_.resolve_type_node(kids[0], current_scope_);
    ExpressionFact source = expression(kids[1], target);
    const pa6::Type& target_type = type(target);
    if (target_type.kind == pa6::LvalueReferenceType ||
        target_type.kind == pa6::RvalueReferenceType) {
      const Id referent = target_type.base;
      const bool compatible = qualification_compatible(source.type, referent);
      const bool direct = compatible &&
          (target_type.kind == pa6::RvalueReferenceType || source.category == LValue ||
           is_const(referent));
      const bool converted = target_type.kind == pa6::LvalueReferenceType &&
          is_const(referent) && conversion(source, referent, kids[1]).viable;
      const bool valid = direct || converted;
      if (!valid) throw std::runtime_error("invalid reference cast");
      ExpressionFact result;
      result.type = referent;
      result.display_type = target;
      result.category = target_type.kind == pa6::LvalueReferenceType ? LValue : XValue;
      (void)node;
      return result;
    }
    if (target_type.kind == pa6::MemberPointerType) {
      if (strip_cv(source.type) != strip_cv(target))
        throw std::runtime_error("invalid member-pointer cast");
      ExpressionFact result;
      result.type = target;
      result.category = PRValue;
      return result;
    }
    if (is_void(target)) {
      ExpressionFact result;
      result.type = target;
      result.category = PRValue;
      return result;
    }
    if (!is_arithmetic(target) && !is_pointer(target) && !is_enum(target) && !is_void(target))
      throw std::runtime_error("unsupported cast target");
    if (!(is_arithmetic(source.type) || is_enum(source.type) || is_nullptr_type(source.type) ||
          source.null_pointer_constant || (is_pointer(source.type) && is_pointer(target)) ||
          (is_pointer(source.type) && type(strip_cv(target)).kind == pa6::FundamentalType &&
           type(strip_cv(target)).atom == "bool")))
      throw std::runtime_error("unsupported cast source");
    if (is_pointer(source.type) && type(strip_cv(target)).kind == pa6::FundamentalType &&
        type(strip_cv(target)).atom == "bool") {
      ExpressionFact result; result.type = target; result.category = PRValue; return result;
    }
    ExpressionFact result;
    result.type = target;
    result.category = PRValue;
    if (source.constant && is_arithmetic(target)) {
      result.constant = true;
      result.value = source.value;
    }
    (void)node;
    return result;
  }

  ExpressionFact analyze_member(Id node, const std::vector<Id>& kids)
  {
    if (kids.size() < 2) throw std::runtime_error("member expression is incomplete");
    ExpressionFact object = expression(kids[0]);
    Id object_type = object.type;
    const std::string op = text(node);
    if (op == "->") {
      const Id pointer = strip_cv(object_type);
      if (!is_pointer(pointer)) throw std::runtime_error("arrow requires a pointer");
      object_type = type(pointer).base;
    }
    const Id class_type = strip_cv(object_type);
    if (type(class_type).kind != pa6::NamedType)
      throw std::runtime_error("member access requires a class object");
    const Id entity = type(class_type).entity;
    const pa6::SourceNamePath member_path = unit_.source_name_path(kids[1]);
    if (member_path.components.empty())
      throw std::runtime_error("member expression has no member name");
    const Id member_name = member_path.components.back();
    Id lookup_entity = entity;
    if (member_path.components.size() > 1) {
      std::vector<Id> qualifier(member_path.components.begin(),
                                member_path.components.end() - 1);
      lookup_entity = pa7::FindNamedBaseEntity(unit_, entity, qualifier);
      if (lookup_entity == none) throw std::runtime_error("qualified member is not a base class");
    }
    pa7::FlatIdSet visited;
    std::vector<Id> found;
    collect_class_members(lookup_entity, member_name, visited, found);
    if (found.empty()) throw std::runtime_error("unknown class member");
    ExpressionFact result;
    result.binding = found[0];
    const pa6::Binding& member = unit_.binding(found[0]);
    result.type = member.type;
    if (member.kind == pa6::FunctionBinding) {
      result.overloads = found;
      result.category = LValue;
    } else {
      if (!member.static_storage)
        result.type = unit_.qualified_type(result.type,
            is_const(object_type), is_volatile(object_type));
      result.category = object.category == XValue ? XValue : LValue;
    }
    (void)node;
    return result;
  }

  Id type_name_binding(Id node) const
  {
    const std::vector<Id> found = lookup_node(node, false, true);
    for (std::size_t i = 0; i < found.size(); ++i)
      if (binding(found[i]).kind == pa6::AliasBinding ||
          binding(found[i]).kind == pa6::TypeBinding)
        return found[i];
    return none;
  }

  bool builtin_type_name(const std::string& name) const
  {
    return name == "void" || name == "bool" || name == "char" || name == "signed char" ||
        name == "unsigned char" || name == "short" || name == "int" || name == "long" ||
        name == "float" || name == "double" || name == "nullptr_t" || name == "wchar_t" ||
        name == "char16_t" || name == "char32_t" || name == "unsigned long" ||
        name == "unsigned long int" || name == "signed long" || name == "signed long int" ||
        name == "unsigned int" || name == "signed int" || name == "unsigned short" ||
        name == "unsigned short int" || name == "signed short" || name == "signed short int" ||
        name == "long int" || name == "long long" || name == "long long int" ||
        name == "unsigned long long" || name == "unsigned long long int" ||
        name == "signed long long" || name == "signed long long int";
  }

  Id builtin_type(const std::string& name)
  {
    if (name == "short") return unit_.fundamental_type("short int");
    if (name == "long" || name == "long int" || name == "signed long" ||
        name == "signed long int") return unit_.fundamental_type("long int");
    if (name == "long long" || name == "long long int" || name == "signed long long" ||
        name == "signed long long int") return unit_.fundamental_type("long long int");
    if (name == "unsigned long" || name == "unsigned long int")
      return unit_.fundamental_type("unsigned long int");
    if (name == "unsigned long long" || name == "unsigned long long int")
      return unit_.fundamental_type("unsigned long long int");
    if (name == "unsigned int") return unit_.fundamental_type("unsigned int");
    if (name == "signed int") return unit_.fundamental_type("int");
    if (name == "unsigned short" || name == "unsigned short int")
      return unit_.fundamental_type("unsigned short int");
    if (name == "signed short" || name == "signed short int")
      return unit_.fundamental_type("short int");
    return unit_.fundamental_type(name);
  }

  ExpressionFact analyze_functional_cast(Id node, Id callee,
                                         const std::vector<Id>& args,
                                         CallFact& call)
  {
    const std::string name = node_name(callee);
    Id target = none;
    const Id decltype_node = pa7::FindDecltypeSpecifier(ast_, callee);
    if (decltype_node != none) {
      const Id operand = ast_.nodes[decltype_node].first_child;
      target = unit_.decltype_type_node(operand, current_scope_);
    } else if (builtin_type_name(name)) target = builtin_type(name);
    else {
      const Id binding = type_name_binding(callee);
      if (binding != none) target = unit_.binding(binding).type;
    }
    if (target == none) return ExpressionFact();
    if (args.size() > 1) throw std::runtime_error("functional cast takes at most one argument");
    ExpressionFact result;
    result.type = target;
    result.category = PRValue;
    if (args.empty()) {
      result.constant = true;
      result.value = 0;
      call.special_name = "<functional-cast-zero>";
    } else {
      ExpressionFact source = expression(args[0]);
      if (!conversion(source, target, args[0]).viable &&
          !(is_arithmetic(target) && is_arithmetic(source.type)) &&
          !(is_integral(target) && is_enum(source.type)) &&
          !(is_enum(target) && is_integral(source.type)))
        throw std::runtime_error("invalid functional cast");
      call.special_name = "<functional-cast>";
      call.argument_nodes = args;
      call.argument_types.push_back(target);
    }
    call.function_type = none;
    call_facts_.store(node, call);
    return result;
  }

  ExpressionFact analyze_call(Id node, const std::vector<Id>& kids)
  {
    if (kids.empty()) throw std::runtime_error("call has no callee");
    const Id callee_node = kids[0];
    const Id arg_list = kids.size() > 1 ? kids[1] : none;
    std::vector<Id> args = children(arg_list);
    CallFact call;
    call.callee_node = callee_node;
    call.argument_nodes = args;

    const std::string raw_callee = node_name(callee_node);
    if (raw_callee == "__builtin_constant_p") {
      if (args.size() != 1) throw std::runtime_error("__builtin_constant_p takes one argument");
      ExpressionFact argument = expression(args[0]);
      ExpressionFact result;
      result.type = unit_.fundamental_type("int");
      result.category = PRValue;
      result.constant = true;
      result.value = argument.constant ? 1 : 0;
      call.builtin_constant_p = true;
      call_facts_.store(node, call);
      return result;
    }
    if (raw_callee == "__builtin_abort") {
      if (!args.empty()) throw std::runtime_error("__builtin_abort takes no arguments");
      ExpressionFact result;
      result.type = unit_.fundamental_type("void");
      result.category = PRValue;
      call.builtin_abort = true;
      call.special_name = "__builtin_abort";
      call.function_type = unit_.function_type(pa6::Type());
      pa6::Type function; function.kind = pa6::FunctionType;
      function.base = result.type;
      call.function_type = unit_.function_type(function);
      call_facts_.store(node, call);
      return result;
    }

    const std::vector<Id> raw_args = args;
    if ((ast_.nodes[callee_node].kind == NIdExpression ||
         ast_.nodes[callee_node].kind == NIdentifier) &&
        (builtin_type_name(raw_callee) || type_name_binding(callee_node) != none ||
        pa7::FindDecltypeSpecifier(ast_, callee_node) != none)) {
      ExpressionFact cast = analyze_functional_cast(node, callee_node, args, call);
      if (cast.type != none) return cast;
    }

    ExpressionFact callee = expression(callee_node);
    std::vector<ExpressionFact> argument_facts;
    for (std::size_t i = 0; i < args.size(); ++i)
      argument_facts.push_back(expression(args[i]));
    call.argument_nodes = args;

    Id function = none;
    if (!callee.overloads.empty()) {
      std::vector<Id> targets;
      function = select_overload(callee.overloads, args, argument_facts, &targets);
      call.selected = function;
      call.function_type = canonical_function(binding(function).type);
      call.argument_types = targets;
    } else {
      function = canonical_function(function_type_from(callee.type));
      if (function == none) throw std::runtime_error("called expression is not a function");
      const pa6::Type& signature = type(function);
      if ((!signature.variadic && args.size() != signature.parameters.size()) ||
          (signature.variadic && args.size() < signature.parameters.size()))
        throw std::runtime_error("indirect call has incorrect argument count");
      for (std::size_t i = 0; i < args.size(); ++i) {
        if (i < signature.parameters.size()) {
          if (!conversion(argument_facts[i], signature.parameters[i], args[i]).viable)
            throw std::runtime_error("invalid indirect call argument conversion");
          call.argument_types.push_back(signature.parameters[i]);
        } else call.argument_types.push_back(none);
      }
      call.function_type = function;
    }
    call.callee_node = callee_node;
    call_facts_.store(node, call);

    const pa6::Type& signature = type(call.function_type);
    ExpressionFact result;
    result.type = signature.base;
    if (type(result.type).kind == pa6::LvalueReferenceType ||
        type(result.type).kind == pa6::RvalueReferenceType) {
      result.display_type = result.type;
      result.type = strip_reference(result.type, &result.category);
    } else result.category = PRValue;
    return result;
  }

  std::string expr_type(Id node, Id expected = none)
  {
    ExpressionFact fact = expression(node, expected);
    if (fact.type == none) throw std::runtime_error("expression has unresolved type");
    return type_spelling(fact.type);
  }

  void emit_expression(Id node, unsigned depth, Id expected = none,
                       bool preserve_expected_cv = false)
  {
    ExpressionFact fact = expression(node, expected);
    if (!fact.overloads.empty() && fact.binding == none && expected == none)
      throw std::runtime_error("overloaded function name requires a target type");
    if (fact.binding == none && !fact.overloads.empty() && fact.overloads.size() > 1)
      throw std::runtime_error("ambiguous function name");
    const NodeKind kind = ast_.nodes[node].kind;
    const std::vector<Id> kids = children(node);
    std::ostringstream header;
    if (expected != none && fact.type != none) {
      const Id target = strip_cv(strip_reference(expected));
      const Id source = strip_cv(fact.type);
      if (type(target).kind == pa6::PointerType &&
          type(source).kind == pa6::PointerType &&
          class_conversion_type(type(source).base, type(target).base)) {
        line(depth, "cast-expression prvalue " + type_spelling(target));
        emit_expression(node, depth + 1);
        return;
      }
      if ((type(expected).kind == pa6::LvalueReferenceType ||
           type(expected).kind == pa6::RvalueReferenceType) &&
          fact.category == LValue &&
          class_conversion_type(fact.type, type(expected).base)) {
        line(depth, "cast-expression lvalue " + type_spelling(type(expected).base));
        emit_expression(node, depth + 1);
        return;
      }
    }
    if (expected != none && fact.type != none &&
        (type(expected).kind == pa6::LvalueReferenceType ||
         type(expected).kind == pa6::RvalueReferenceType)) {
      const Id referent = type(expected).base;
      if (!same_unqualified(fact.type, referent) &&
          is_arithmetic(fact.type) && is_arithmetic(referent) &&
          conversion(fact, referent, node).viable) {
        line(depth, "cast-expression prvalue " + type_spelling(referent));
        emit_expression(node, depth + 1);
        return;
      }
    }
    if (kind == NLiteral || kind == NTaggedLiteral || kind == NKeywordLiteral) {
      const Id target_value = expected == none ? none : strip_reference(expected);
      const Id printed_type = fact.null_pointer_constant && target_value != none &&
          (is_pointer(target_value) || is_nullptr_type(target_value)) ? strip_cv(target_value) :
          (preserve_expected_cv && target_value != none &&
           same_unqualified(fact.type, target_value) ? target_value : fact.type);
      header << "literal " << category_name(fact.category) << ' '
             << type_spelling(printed_type) << ' '
             << (kind == NKeywordLiteral ? token_label(node) : text(node));
      line(depth, header.str());
      return;
    }
    if (kind == NIdExpression || kind == NIdentifier) {
      if (fact.binding == none && expected != none && !fact.overloads.empty()) {
        const Id selected = resolve_function_overload(fact, expected);
        if (selected == none) throw std::runtime_error("no target-compatible function overload");
        fact.binding = selected;
        fact.type = canonical_function(binding(selected).type);
      }
      if (fact.binding != none && binding(fact.binding).kind == pa6::EnumeratorBinding) {
        const pa6::Binding& b = binding(fact.binding);
        header << "literal prvalue " << type_spelling(b.type) << ' ' << b.value;
      } else {
        std::unordered_map<Id, std::pair<Id, std::string> >::const_iterator injected =
            injected_union_members_.find(fact.binding);
        if (injected != injected_union_members_.end()) {
          line(depth, "member-expression lvalue " + type_spelling(fact.type) + " " +
               node_name(node));
          line(depth + 1, "id-expression lvalue " +
               type_spelling(injected->second.first) + " " + injected->second.second);
          return;
        }
        const std::string name = node_name(node);
        Id displayed_type = fact.type;
        if (fact.binding != none &&
            binding(fact.binding).kind == pa6::FunctionBinding &&
            unit_.scope(binding(fact.binding).scope).kind == pa6::ClassScope)
          displayed_type = function_dump_type(fact.binding);
        header << "id-expression " << category_name(fact.category) << ' '
               << type_spelling(displayed_type) << ' ' << name;
      }
      line(depth, header.str());
      return;
    }
    if (kind == NParenthesizedExpression) {
      if (!kids.empty()) emit_expression(kids[0], depth, expected);
      return;
    }
    if (kind == NCallExpression) {
      const CallFact* call_record = call_facts_.find(node);
      if (!call_record) throw std::logic_error("call facts were not recorded");
      const CallFact& call = *call_record;
      if (call.special_name == "<functional-cast-zero>") {
        header << "literal prvalue " << type_spelling(fact.type) << " 0";
        line(depth, header.str());
        return;
      }
      if (call.special_name == "<functional-cast>") {
        header << "cast-expression prvalue " << type_spelling(fact.type);
        line(depth, header.str());
        if (!call.argument_nodes.empty()) emit_expression(call.argument_nodes[0], depth + 1);
        return;
      }
      if (call.builtin_constant_p) {
        header << "literal prvalue int " << fact.value;
        line(depth, header.str());
        return;
      }
      header << "call-expression " << category_name(fact.category) << ' '
             << type_spelling(fact.display_type == none ? fact.type : fact.display_type);
      line(depth, header.str());
      if (call.builtin_abort) {
        line(depth + 1, "callee __builtin_abort " + type_spelling(call.function_type));
      } else if (call.selected != none) {
        line(depth + 1, "callee " + binding_name(call.selected) + " " +
             type_spelling(call.function_type));
      } else emit_expression(call.callee_node, depth + 1);
      for (std::size_t i = 0; i < call.argument_nodes.size(); ++i) {
        const Id target = i < call.argument_types.size() ? call.argument_types[i] : none;
        emit_expression(call.argument_nodes[i], depth + 1, target);
      }
      return;
    }
    if (kind == NUnaryExpression || kind == NPostfixExpression) {
      const std::string tag = kind == NUnaryExpression ? "unary-expression " : "postfix-expression ";
      header << tag << category_name(fact.category) << ' ' << type_spelling(fact.type)
             << ' ' << token_label(node);
      line(depth, header.str());
      if (kind == NUnaryExpression && text(node) == "&")
        demand_member_definition(fact.binding);
      for (std::size_t i = 0; i < kids.size(); ++i) {
        if (kind == NUnaryExpression && text(node) == "&" && fact.binding != none)
          emit_expression(kids[i], depth + 1, fact.type);
        else emit_expression(kids[i], depth + 1);
      }
      return;
    }
    if (kind == NBinaryExpression || kind == NAssignmentExpression) {
      header << (kind == NAssignmentExpression ? "assignment-expression " : "binary-expression ")
             << category_name(fact.category) << ' ' << type_spelling(fact.type)
             << ' ' << token_label(node);
      line(depth, header.str());
      for (std::size_t i = 0; i < kids.size(); ++i) {
        if (kind == NAssignmentExpression && token_label(node) == "OP_ASS:=" &&
            i == 1 && !kids.empty())
          emit_expression(kids[i], depth + 1, expression(kids[0]).type);
        else emit_expression(kids[i], depth + 1);
      }
      return;
    }
    if (kind == NConditionalExpression) {
      header << "conditional-expression " << category_name(fact.category) << ' '
             << type_spelling(fact.type);
      line(depth, header.str());
      for (std::size_t i = 0; i < kids.size(); ++i) emit_expression(kids[i], depth + 1);
      return;
    }
    if (kind == NSubscriptExpression) {
      header << "subscript-expression " << category_name(fact.category) << ' '
             << type_spelling(fact.type);
      line(depth, header.str());
      if (kids.size() == 2) {
        const ExpressionFact left = expression(kids[0]);
        const ExpressionFact right = expression(kids[1]);
        if (is_integral(left.type) && (is_pointer(right.type) || is_array(right.type))) {
          emit_expression(kids[1], depth + 1);
          emit_expression(kids[0], depth + 1);
          return;
        }
      }
      for (std::size_t i = 0; i < kids.size(); ++i) emit_expression(kids[i], depth + 1);
      return;
    }
    if (kind == NSizeofExpression || kind == NTypeTraitExpression) {
      header << "sizeof-expression prvalue " << type_spelling(fact.type);
      line(depth, header.str());
      return;
    }
    if (kind == NCastExpression) {
      const Id target = unit_.resolve_type_node(kids[0], current_scope_);
      const Id target_value = strip_cv(target);
      if (kids.size() > 1 && ast_.nodes[kids[1]].kind == NUnaryExpression &&
          text(kids[1]) == "&" &&
          type(target_value).kind == pa6::MemberPointerType) {
        emit_expression(kids[1], depth, target);
        return;
      }
      if (target != none &&
          (type(target).kind == pa6::LvalueReferenceType ||
           type(target).kind == pa6::RvalueReferenceType) && kids.size() > 1 &&
          (ast_.nodes[kids[1]].kind == NIdExpression || ast_.nodes[kids[1]].kind == NIdentifier)) {
        header << "id-expression " << category_name(fact.category) << ' '
               << type_spelling(fact.display_type) << ' ' << node_name(kids[1]);
        line(depth, header.str());
        return;
      }
      header << "cast-expression prvalue " << type_spelling(fact.type);
      if (ast_.nodes[node].atom != none) {
        const ast_tokens::Token& token = ast_.tokens[ast_.nodes[node].atom];
        if (token.tag == "OP_LPAREN") header << " OP_LPAREN:";
        else header << ' ' << token_label(node);
      }
      line(depth, header.str());
      for (std::size_t i = 0; i < kids.size(); ++i) {
        if (ast_.nodes[kids[i]].kind == NTypeId) continue;
        if (i > 0 && type(target_value).kind == pa6::PointerType &&
            is_function(type(target_value).base) &&
            ast_.nodes[kids[i]].kind == NUnaryExpression && text(kids[i]) == "&")
          emit_expression(kids[i], depth + 1, target);
        else emit_expression(kids[i], depth + 1);
      }
      return;
    }
    if (kind == NMemberExpression) {
      std::string member_operator = token_label(node);
      if (kids.size() > 1 && ast_.nodes[node].atom != none) {
        const ast_tokens::Token& op = ast_.tokens[ast_.nodes[node].atom];
        bool absolute = false;
        const std::vector<std::string> member_parts =
            pa7::SplitNameSpelling(node_name(kids.back()), absolute);
        const std::string member_name = member_parts.empty()
            ? node_name(kids.back()) : member_parts.back();
        member_operator = op.tag + ":" + member_name;
      }
      header << "member-expression " << category_name(fact.category) << ' '
             << type_spelling(fact.type) << ' ' << member_operator;
      line(depth, header.str());
      for (std::size_t i = 0; i < kids.size(); ++i) {
        if (ast_.nodes[kids[i]].kind == NIdentifier) continue;
        emit_expression(kids[i], depth + 1);
      }
      return;
    }
    throw std::runtime_error("cannot print semantic expression");
  }

  Id initializer_expression(Id node) const
  {
    if (node == none) return none;
    const NodeKind kind = ast_.nodes[node].kind;
    if (kind != NInitializer && kind != NParenInitializer &&
        kind != NBracedInitList && kind != NDefaultArgument)
      return node;
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      const Id result = initializer_expression(c);
      if (result != none) return result;
    }
    return none;
  }

  Id initializer_content(Id node) const
  {
    if (node == none) return none;
    const NodeKind kind = ast_.nodes[node].kind;
    if (kind != NInitializer && kind != NParenInitializer && kind != NDefaultArgument)
      return node;
    return ast_.nodes[node].first_child;
  }

  void emit_initializer(Id initializer, Id target, unsigned depth,
                        bool preserve_expected_cv = false)
  {
    const Id content = initializer_content(initializer);
    if (content == none) return;
    if (ast_.nodes[content].kind == NBracedInitList) {
      const Id value_type = strip_cv(strip_reference(target));
      Id element_type = target;
      long long bound = -1;
      if (value_type != none && type(value_type).kind == pa6::ArrayType) {
        element_type = type(value_type).base;
        bound = type(value_type).bound;
      } else if (value_type != none) element_type = target;
      std::vector<Id> elements = children(content);
      if (bound >= 0 && static_cast<long long>(elements.size()) > bound)
        throw std::runtime_error("too many elements in array initializer");
      line(depth, "braced-init-list lvalue " + type_spelling(value_type));
      for (std::size_t i = 0; i < elements.size(); ++i) {
        const ExpressionFact value = expression(elements[i], element_type);
        if (!conversion(value, element_type, elements[i]).viable)
          throw std::runtime_error("invalid element in braced initializer");
        emit_expression(elements[i], depth + 1, element_type);
      }
      return;
    }
    const Id expr = initializer_expression(initializer);
    if (expr == none) return;
    const ExpressionFact value = expression(expr, target);
    if (!conversion(value, target, expr).viable)
      throw std::runtime_error("invalid copy-initializer conversion");
    emit_expression(expr, depth, target, preserve_expected_cv);
  }

  void register_ordinary_name(Id scope, Id name,
                              pa6::BindingKind kind)
  {
    std::unordered_map<Id, pa6::BindingKind>& names = namespace_ordinary_kinds_[scope];
    std::unordered_map<Id, pa6::BindingKind>::const_iterator found = names.find(name);
    if (found != names.end() && found->second != kind &&
        (found->second == pa6::FunctionBinding || found->second == pa6::VariableBinding) &&
        (kind == pa6::FunctionBinding || kind == pa6::VariableBinding))
      throw std::runtime_error("function and variable share an ordinary name");
    if (found == names.end()) names.insert(std::make_pair(name, kind));
  }

  void emit_top_simple(Id node, unsigned depth)
  {
    Id seq = ast_.nodes[node].first_child;
    Id list = seq == none ? none : ast_.nodes[seq].next_sibling;
    visibility_.mark_type_declarations(unit_, ast_, seq);
    Id anonymous_union_spec = none;
    for (Id spec = seq == none ? none : ast_.nodes[seq].first_child;
         spec != none; spec = ast_.nodes[spec].next_sibling) {
      if (ast_.nodes[spec].kind != NClassSpecifier || ast_.nodes[spec].composite != none)
        continue;
      for (Id key = ast_.nodes[spec].first_child; key != none;
           key = ast_.nodes[key].next_sibling)
        if (ast_.nodes[key].kind == NClassKey && text(key) == "union")
          anonymous_union_spec = spec;
    }
    if (list == none || ast_.nodes[list].kind != NInitDeclaratorList) {
      if (anonymous_union_spec != none)
        emit_anonymous_union_storage(node, anonymous_union_spec, depth, true);
      return;
    }
    for (Id item = ast_.nodes[list].first_child; item != none; item = ast_.nodes[item].next_sibling) {
      const Id binding = unit_.binding_for_node(item);
      if (binding == none) continue;
      visibility_.mark_binding(binding);
      const pa6::Binding& b = unit_.binding(binding);
      const std::string name = b.kind == pa6::FunctionBinding ||
          (b.kind == pa6::VariableBinding && unit_.scope(b.scope).kind == pa6::ClassScope)
          ? qualified_name(b.scope, b.name) : b.name;
      if (b.kind == pa6::FunctionBinding || b.kind == pa6::VariableBinding)
        register_ordinary_name(b.scope, b.name_id, b.kind);
      std::ostringstream out;
      if (b.kind == pa6::FunctionBinding)
      out << "function-declaration " << name << ' '
            << type_spelling(canonical_function(b.type));
      else if (b.kind == pa6::VariableBinding)
        out << "variable " << name << ' ' << type_spelling(canonical_type(b.type));
      else if (b.kind == pa6::AliasBinding)
        out << "type-alias " << b.name << ' ' << type_spelling(b.type);
      else continue;
      line(depth, out.str());
      if (b.kind == pa6::VariableBinding) {
        const Id declarator = ast_.nodes[item].first_child;
        const Id init = declarator == none ? none : ast_.nodes[declarator].next_sibling;
        if (init != none) {
          bool constexpr_object = false;
          for (Id spec = ast_.nodes[seq].first_child; spec != none;
               spec = ast_.nodes[spec].next_sibling)
            if (text(spec) == "constexpr") constexpr_object = true;
          const Id plain_type = strip_cv(b.type);
          emit_initializer(init, b.type, depth + 1,
                           constexpr_object && !is_pointer(plain_type));
        } else emit_union_constructor(b.type, b.name, depth + 1, true);
      }
    }
  }

  void expose_template_declaration(Id node)
  {
    Id declaration = ast_.nodes[node].first_child;
    declaration = declaration == none ? none : ast_.nodes[declaration].next_sibling;
    if (declaration == none) return;
    if (ast_.nodes[declaration].kind == NFunctionDefinition) {
      visibility_.mark_binding(unit_.binding_for_node(declaration));
      return;
    }
    if (ast_.nodes[declaration].kind != NSimpleDeclaration) return;
    const Id sequence = ast_.nodes[declaration].first_child;
    const Id list = sequence == none ? none : ast_.nodes[sequence].next_sibling;
    for (Id item = list == none ? none : ast_.nodes[list].first_child;
         item != none; item = ast_.nodes[item].next_sibling)
      visibility_.mark_binding(unit_.binding_for_node(item));
  }

  void emit_declaration(Id node, unsigned depth)
  {
    const NodeKind kind = ast_.nodes[node].kind;
    if (kind == NNamespaceDefinition) {
      const Id prior_scope = current_scope_;
      const Id ns = unit_.scope_for_node(node);
      visibility_.expose_namespace_definition(unit_, ns);
      current_scope_ = ns == none ? prior_scope : ns;
      std::string name = text(node);
      if (name.empty()) name = "<unnamed>";
      line(depth, "namespace-definition " + name);
      for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
        if (ast_.nodes[c].kind != NInlineMarker) emit_declaration(c, depth + 1);
      current_scope_ = prior_scope;
    } else if (kind == NSimpleDeclaration) emit_top_simple(node, depth);
    else if (kind == NAliasDeclaration) {
      const Id binding = unit_.binding_for_node(node);
      if (binding != none) {
        visibility_.mark_binding(binding);
        line(depth, "type-alias " + unit_.binding(binding).name + " " +
             type_spelling(unit_.binding(binding).type));
      }
    } else if (kind == NFunctionDefinition) emit_function(node, depth);
    else if (kind == NTemplateDeclaration) expose_template_declaration(node);
    else if (kind == NClassForwardDeclaration)
      visibility_.mark_binding(unit_.binding_for_node(node));
    else if (kind == NClassSpecifier || kind == NEnumSpecifier)
      visibility_.mark_type_declarations(unit_, ast_, node);
    else if (kind == NNamespaceAliasDefinition)
      visibility_.mark_binding(unit_.binding_for_node(node));
    else if (kind == NUsingDeclaration)
      visibility_.mark_binding(unit_.binding_for_node(node));
    else if (kind == NUsingDirective) {
      const Id target = ast_.nodes[node].first_child;
      const std::vector<Id> found = lookup_node(target, true, false);
      if (found.empty()) throw std::runtime_error("using directive target not found");
      const Id nominated = namespace_scope(found[0]);
      if (nominated == none)
        throw std::runtime_error("using directive target is not a namespace");
      visibility_.add_using_directive(current_scope_, nominated);
    }
    else if (kind == NLinkageSpecification) {
      for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
        emit_declaration(c, depth);
    }
  }

  Id function_dump_type(Id binding)
  {
    const Id function = canonical_function(unit_.binding(binding).type);
    const pa6::ScopeRecord& owner = unit_.scope(unit_.binding(binding).scope);
    if (owner.kind != pa6::ClassScope || owner.entity >= unit_.entity_count())
      return function;
    pa6::Type signature = type(function);
    const Id class_type = unit_.entity(owner.entity).type;
    const Id object_type = unit_.qualified_type(class_type,
        signature.member_const, signature.member_volatile);
    signature.parameters.insert(signature.parameters.begin(),
                                unit_.pointer_type(object_type));
    signature.member_const = false;
    signature.member_volatile = false;
    signature.ref_qualifier = pa6::NoRefQualifier;
    return unit_.function_type(signature);
  }

  void emit_constructor_definition(const GeneratedUnionConstructor& constructor,
                                   unsigned depth)
  {
    line(depth, "function-definition " + constructor.name + "::" + constructor.name + " " +
         type_spelling(constructor.function_type));
    line(depth + 1, "parameter this " + type_spelling(constructor.pointer_type));
    line(depth + 1, "compound-statement");
  }

  void index_function_definitions_and_templates()
  {
    for (Id node = 0; node < ast_.nodes.size(); ++node) {
      const NodeKind kind = ast_.nodes[node].kind;
      if (kind == NFunctionDefinition) {
        const Id binding = unit_.binding_for_node(node);
        if (binding != none && binding < unit_.binding_count() &&
            unit_.scope(unit_.binding(binding).scope).kind == pa6::ClassScope)
          member_definition_nodes_[binding] = node;
        continue;
      }
      if (kind != NTemplateDeclaration) continue;
      const Id declaration = ast_.nodes[node].first_child == none ? none
          : ast_.nodes[ast_.nodes[node].first_child].next_sibling;
      if (declaration == none) continue;
      if (ast_.nodes[declaration].kind == NFunctionDefinition) {
        const Id binding = unit_.binding_for_node(declaration);
        const Id seq = ast_.nodes[declaration].first_child;
        const Id declarator = seq == none ? none : ast_.nodes[seq].next_sibling;
        if (binding != none && declarator != none)
          template_declarators_[binding] = declarator;
      } else if (ast_.nodes[declaration].kind == NSimpleDeclaration) {
        const Id seq = ast_.nodes[declaration].first_child;
        const Id list = seq == none ? none : ast_.nodes[seq].next_sibling;
        for (Id item = list == none ? none : ast_.nodes[list].first_child;
             item != none; item = ast_.nodes[item].next_sibling) {
          const Id binding = unit_.binding_for_node(item);
          const Id declarator = ast_.nodes[item].first_child;
          if (binding != none && declarator != none)
            template_declarators_[binding] = declarator;
        }
      }
    }
  }

  void emit_function_instance(Id id, unsigned depth)
  {
    const pa6::Binding& function = binding(id);
    const Id signature_id = canonical_function(function.type);
    if (!is_function(signature_id))
      throw std::logic_error("function template instance has a non-function type");
    const pa6::Type& signature = type(signature_id);
    line(depth, "function-declaration " + binding_name(id) + " " +
         type_spelling(signature_id));
    std::vector<std::string> parameter_names;
    const std::size_t instance_index = id - unit_.binding_count();
    const std::unordered_map<Id, Id>::const_iterator found =
        template_declarators_.find(instantiated_function_origins_[instance_index]);
    if (found == template_declarators_.end())
      throw std::logic_error("function template declarator index is incomplete");
    const Id declarator = found->second;
    const Id clause = pa7::FindFirstParameterClause(ast_, declarator);
    for (Id parameter = clause == none ? none : ast_.nodes[clause].first_child;
         parameter != none; parameter = ast_.nodes[parameter].next_sibling) {
      if (ast_.nodes[parameter].kind != NParameterDeclaration) continue;
      const Id sequence = ast_.nodes[parameter].first_child;
      const Id parameter_declarator = sequence == none ? none
          : ast_.nodes[sequence].next_sibling;
      parameter_names.push_back(pa7::FindDeclaratorName(ast_, parameter_declarator));
    }
    for (std::size_t i = 0; i < signature.parameters.size(); ++i) {
      const std::string name = i < parameter_names.size() ? parameter_names[i] : "";
      line(depth + 1, "parameter " + name + " " +
           type_spelling(signature.parameters[i]));
    }
  }

  void demand_member_definition(Id binding)
  {
    if (binding == none || binding >= unit_.binding_count() ||
        unit_.binding(binding).kind != pa6::FunctionBinding ||
        unit_.scope(unit_.binding(binding).scope).kind != pa6::ClassScope)
      return;
    if (demanded_member_set_.insert(binding))
      demanded_member_definitions_.push_back(binding);
  }

  void emit_function(Id node, unsigned depth)
  {
    const Id binding = unit_.binding_for_node(node);
    if (binding == none) throw std::runtime_error("function definition has no declaration binding");
    visibility_.mark_binding(binding);
    const pa6::Binding& function = unit_.binding(binding);
    if (function.entity != none && !defined_functions_.insert(function.entity))
      throw std::runtime_error("duplicate function definition");
    register_ordinary_name(function.scope, function.name_id, pa6::FunctionBinding);
    const Id signature_id = canonical_function(function.type);
    if (!is_function(signature_id)) throw std::runtime_error("function definition has non-function type");
    Id declarator = ast_.nodes[node].first_child;
    declarator = declarator == none ? none : ast_.nodes[declarator].next_sibling;
    const std::string name = qualified_name(function.scope, function.name);
    const Id dump_signature = function_dump_type(binding);
    line(depth, "function-definition " + name + " " + type_spelling(dump_signature));
    const Id clause = pa7::FindFirstParameterClause(ast_, declarator);
    const Id old_scope = current_scope_;
    const Id old_function_type = current_function_type_;
    generated_union_constructors_.clear();
    current_scope_ = unit_.scope_for_node(node);
    current_function_type_ = signature_id;
    push_environment();
    if (unit_.scope(function.scope).kind == pa6::ClassScope &&
        !type(dump_signature).parameters.empty())
      line(depth + 1, "parameter this " + type_spelling(type(dump_signature).parameters[0]));
    std::size_t parameter_index = 0;
    if (clause != none) {
      for (Id p = ast_.nodes[clause].first_child; p != none; p = ast_.nodes[p].next_sibling) {
        if (ast_.nodes[p].kind != NParameterDeclaration) continue;
        const Id pb = unit_.binding_for_node(p);
        if (pb == none) continue;
        add_environment_binding(pb);
        const pa6::Binding& parameter = unit_.binding(pb);
        const Id parameter_type = parameter_index < type(signature_id).parameters.size()
            ? type(signature_id).parameters[parameter_index] : parameter.type;
        line(depth + 1, "parameter " +
             parameter.name + " " +
             type_spelling(parameter_type));
        ++parameter_index;
      }
    }
    Id body = ast_.nodes[declarator].next_sibling;
    while (body != none && ast_.nodes[body].kind != NCompoundStatement)
      body = ast_.nodes[body].next_sibling;
    if (body != none) emit_statement(body, depth + 1);
    else if (unit_.scope(function.scope).kind == pa6::ClassScope &&
             function.name == unit_.entity(unit_.scope(function.scope).entity).name)
      line(depth + 1, "compound-statement");
    for (std::size_t i = 0; i < generated_union_constructors_.size(); ++i) {
      emit_constructor_definition(generated_union_constructors_[i], depth);
    }
    generated_union_constructors_.clear();
    pop_environment();
    current_scope_ = old_scope;
    current_function_type_ = old_function_type;
  }

  void emit_local_declaration(Id node, unsigned depth)
  {
    if (ast_.nodes[node].kind == NAliasDeclaration) {
      const Id binding = unit_.binding_for_node(node);
      if (binding != none) {
        add_environment_binding(binding);
        line(depth, "type-alias " + unit_.binding(binding).name + " " +
             type_spelling(unit_.binding(binding).type));
      }
      return;
    }
    const Id seq = ast_.nodes[node].first_child;
    const Id list = seq == none ? none : ast_.nodes[seq].next_sibling;
    visibility_.mark_type_declarations(unit_, ast_, seq);
    Id anonymous_union_spec = none;
    for (Id spec = seq == none ? none : ast_.nodes[seq].first_child;
         spec != none; spec = ast_.nodes[spec].next_sibling) {
      if (ast_.nodes[spec].kind != NClassSpecifier || ast_.nodes[spec].composite != none)
        continue;
      bool is_union = false;
      for (Id key = ast_.nodes[spec].first_child; key != none;
           key = ast_.nodes[key].next_sibling)
        if (ast_.nodes[key].kind == NClassKey && text(key) == "union") is_union = true;
      if (is_union) { anonymous_union_spec = spec; break; }
    }
    if (list == none || ast_.nodes[list].kind != NInitDeclaratorList) {
      if (anonymous_union_spec != none) {
        emit_anonymous_union_storage(node, anonymous_union_spec, depth);
      } else line(depth, "simple-declaration");
      return;
    }
    line(depth, "simple-declaration");
    for (Id item = ast_.nodes[list].first_child; item != none; item = ast_.nodes[item].next_sibling) {
      const Id binding = unit_.binding_for_node(item);
      if (binding == none) continue;
      add_environment_binding(binding);
      const pa6::Binding& b = unit_.binding(binding);
      std::string kind;
      if (b.kind == pa6::VariableBinding) kind = "variable ";
      else if (b.kind == pa6::FunctionBinding) kind = "function-declaration ";
      else if (b.kind == pa6::AliasBinding) kind = "type-alias ";
      else continue;
      line(depth + 1, kind + b.name + " " +
           type_spelling(b.kind == pa6::FunctionBinding
                                   ? canonical_function(b.type) : canonical_type(b.type)));
      if (b.kind == pa6::VariableBinding && anonymous_union_spec != none)
        emit_union_constructor(b.type, b.name, depth + 2);
      const Id declarator = ast_.nodes[item].first_child;
      const Id init = declarator == none ? none : ast_.nodes[declarator].next_sibling;
      if (init != none && b.kind == pa6::VariableBinding)
        emit_initializer(init, b.type, depth + 2);
      else if (b.kind == pa6::VariableBinding && anonymous_union_spec == none)
        emit_union_constructor(b.type, b.name, depth + 2);
    }
  }

  void emit_anonymous_union_storage(Id declaration, Id specifier, unsigned depth,
                                    bool top_level = false)
  {
    const std::string storage = unit_.anonymous_union_storage_name(declaration);
    const Id union_type = unit_.type_for_node(specifier);
    if (!top_level) line(depth, "simple-declaration");
    if (storage.empty() || union_type == none) return;
    const unsigned member_depth = depth + (top_level ? 0 : 1);
    line(member_depth, "variable " + storage + " " + type_spelling(union_type));
    emit_union_constructor(union_type, storage, member_depth + 1, top_level);
    const pa6::Entity& entity = unit_.entity(type(union_type).entity);
    if (entity.scope != none) {
      const pa6::ScopeRecord& members = unit_.scope(entity.scope);
      for (std::size_t i = 0; i < members.bindings.size(); ++i) {
        const pa6::Binding& member = unit_.binding(members.bindings[i]);
        if (member.kind != pa6::VariableBinding) continue;
        const Id injected = unit_.lookup(current_scope_, member.name);
        if (injected != none && unit_.binding(injected).entity == member.entity) {
          add_environment_binding(injected);
          injected_union_members_[injected] = std::make_pair(union_type, storage);
        }
      }
    }
  }

  void emit_union_constructor(Id union_type, const std::string& object,
                              unsigned depth, bool top_level = false)
  {
    union_type = strip_cv(strip_reference(union_type));
    if (union_type == none || type(union_type).kind != pa6::NamedType ||
        type(union_type).entity >= unit_.entity_count()) return;
    const pa6::Entity& entity = unit_.entity(type(union_type).entity);
    if (entity.kind != pa6::ClassEntity || !entity.complete || entity.scope == none) return;
    for (std::size_t i = 0; i < unit_.scope(entity.scope).bindings.size(); ++i) {
      const pa6::Binding& member = unit_.binding(unit_.scope(entity.scope).bindings[i]);
      if (member.kind == pa6::FunctionBinding && member.name == entity.name) return;
    }
    const std::string name = class_name_for_output(type(union_type).entity);
    const Id pointer = unit_.pointer_type(union_type);
    pa6::Type signature;
    signature.kind = pa6::FunctionType;
    signature.base = unit_.fundamental_type("void");
    signature.parameters.push_back(pointer);
    const Id function = unit_.function_type(signature);
    line(depth, "constructor-action " + name + "::" + name);
    line(depth + 1, "call-expression prvalue void");
    line(depth + 2, "callee " + name + "::" + name + " " + type_spelling(function));
    line(depth + 2, "unary-expression prvalue " + type_spelling(pointer) + " OP_AMP:&");
    line(depth + 3, "id-expression lvalue " + type_spelling(union_type) + " " + object);
    std::vector<GeneratedUnionConstructor>& constructors = top_level
        ? translation_unit_constructors_ : generated_union_constructors_;
    for (std::size_t i = 0; i < constructors.size(); ++i)
      if (constructors[i].type == union_type) return;
    GeneratedUnionConstructor generated;
    generated.type = union_type;
    generated.pointer_type = pointer;
    generated.function_type = function;
    generated.name = name;
    constructors.push_back(generated);
  }

  void emit_condition(Id node, unsigned depth, bool switch_condition)
  {
    if (node == none) throw std::runtime_error("control-flow statement has no condition");
    line(depth, "condition");
    const Id child = ast_.nodes[node].first_child;
    if (child == none) throw std::runtime_error("empty condition");
    if (ast_.nodes[child].kind == NConditionDeclaration) {
      const Id binding = unit_.add_condition_binding(child, current_scope_);
      add_environment_binding(binding);
      line(depth + 1, "condition-declaration");
      const pa6::Binding& b = unit_.binding(binding);
      line(depth + 2, "variable " + b.name + " " + type_spelling(b.type));
      Id initializer = ast_.nodes[child].first_child;
      initializer = initializer == none ? none : ast_.nodes[initializer].next_sibling;
      initializer = initializer == none ? none : ast_.nodes[initializer].next_sibling;
      const Id expr = initializer_expression(initializer);
      if (expr == none) throw std::runtime_error("condition declaration has no initializer");
      const ExpressionFact value = expression(expr, b.type);
      if (!conversion(value, b.type, expr).viable)
        throw std::runtime_error("invalid condition declaration conversion");
      emit_expression(expr, depth + 3, b.type);
    } else {
      const ExpressionFact value = expression(child);
      if (switch_condition) {
        if (!is_integral(value.type) && !is_enum(value.type))
          throw std::runtime_error("switch condition is not integral or enumeration");
      } else if (!is_scalar(value.type))
        throw std::runtime_error("condition expression is not scalar");
      emit_expression(child, depth + 1);
    }
  }

  void emit_substatement(Id node, unsigned depth)
  {
    push_environment();
    emit_statement(node, depth);
    pop_environment();
  }

  void emit_statement(Id node, unsigned depth)
  {
    if (node == none) return;
    const NodeKind kind = ast_.nodes[node].kind;
    const std::vector<Id> kids = children(node);
    if (kind == NCompoundStatement) {
      const Id old_scope = current_scope_;
      const Id block_scope = unit_.scope_for_node(node);
      if (block_scope != none) current_scope_ = block_scope;
      push_environment();
      line(depth, "compound-statement");
      for (std::size_t i = 0; i < kids.size(); ++i) {
        const NodeKind child_kind = ast_.nodes[kids[i]].kind;
        if (child_kind == NSimpleDeclaration || child_kind == NAliasDeclaration)
          emit_local_declaration(kids[i], depth + 1);
        else if (child_kind == NUsingDeclaration) {
          const std::vector<Id> imported = lookup_node(ast_.nodes[kids[i]].first_child);
          for (std::size_t j = 0; j < imported.size(); ++j)
            if (unit_.binding(imported[j]).kind == pa6::FunctionBinding ||
                unit_.binding(imported[j]).kind == pa6::VariableBinding ||
                unit_.binding(imported[j]).kind == pa6::EnumeratorBinding)
              add_environment_binding(imported[j]);
          const Id mapped = unit_.binding_for_node(kids[i]);
          if (mapped != none) add_environment_binding(mapped);
        } else if (child_kind == NUsingDirective) {
          const Id target = ast_.nodes[kids[i]].first_child;
          const std::vector<Id> found = lookup_node(target, true, false);
          if (found.empty()) throw std::runtime_error("using directive target not found");
          const Id ns = namespace_scope(found[0]);
          if (ns == none) throw std::runtime_error("using directive target is not a namespace");
          environments_.back().using_directives.push_back(ns);
        } else if (child_kind == NStaticAssertDeclaration) {
          continue;
        } else emit_statement(kids[i], depth + 1);
      }
      pop_environment();
      current_scope_ = old_scope;
      return;
    }
    if (kind == NExpressionStatement) {
      if (kids.empty()) return;
      line(depth, "expression-statement");
      emit_expression(kids[0], depth + 1);
      return;
    }
    if (kind == NReturnStatement) {
      line(depth, "return-statement");
      Id value = kids.empty() ? none : kids[0];
      const Id return_type = type(current_function_type_).base;
      if (value == none) {
        if (!is_void(return_type)) throw std::runtime_error("non-void function returns no value");
      } else {
        if (is_void(return_type)) throw std::runtime_error("void function returns a value");
        const ExpressionFact source = expression(value);
        if (!conversion(source, return_type, value).viable)
          throw std::runtime_error("invalid return conversion");
        emit_expression(value, depth + 1, return_type);
      }
      return;
    }
    if (kind == NIfStatement) {
      line(depth, "if-statement");
      push_environment();
      std::size_t i = 0;
      if (!kids.empty() && ast_.nodes[kids[0]].kind == NCondition) {
        emit_condition(kids[0], depth + 1, false); i = 1;
      }
      if (i < kids.size() && ast_.nodes[kids[i]].kind == NThen) {
        line(depth + 1, "then");
        const Id branch = ast_.nodes[kids[i]].first_child;
        emit_substatement(branch, depth + 2); ++i;
      }
      if (i < kids.size() && ast_.nodes[kids[i]].kind == NElse) {
        line(depth + 1, "else");
        emit_substatement(ast_.nodes[kids[i]].first_child, depth + 2);
      }
      pop_environment();
      return;
    }
    if (kind == NWhileStatement) {
      line(depth, "while-statement");
      std::size_t i = 0;
      if (!kids.empty() && ast_.nodes[kids[0]].kind == NCondition) {
        emit_condition(kids[0], depth + 1, false); i = 1;
      }
      ++loop_depth_;
      if (i < kids.size()) emit_substatement(kids[i], depth + 1);
      --loop_depth_;
      return;
    }
    if (kind == NDoStatement) {
      line(depth, "do-statement");
      ++loop_depth_;
      if (!kids.empty()) emit_substatement(kids[0], depth + 1);
      --loop_depth_;
      if (kids.size() > 1) emit_condition(kids[1], depth + 1, false);
      return;
    }
    if (kind == NForStatement) {
      line(depth, "for-statement");
      const Id old_scope = current_scope_;
      if (!kids.empty()) {
        const Id init_scope = unit_.scope_for_node(kids[0]);
        if (init_scope != none) current_scope_ = init_scope;
      }
      push_environment();
      std::size_t i = 0;
      if (!kids.empty() && ast_.nodes[kids[0]].kind == NForInitStatement) {
        line(depth + 1, "for-init-statement");
        const Id init = ast_.nodes[kids[0]].first_child;
        if (init != none) {
          if (ast_.nodes[init].kind == NSimpleDeclaration)
            emit_local_declaration(init, depth + 2);
          else emit_expression(init, depth + 2);
        }
        i = 1;
      }
      if (i < kids.size() && ast_.nodes[kids[i]].kind == NCondition) {
        emit_condition(kids[i], depth + 1, false); ++i;
      }
      if (i < kids.size() && ast_.nodes[kids[i]].kind == NIteration) {
        line(depth + 1, "iteration");
        const Id iter = ast_.nodes[kids[i]].first_child;
        if (iter != none) emit_expression(iter, depth + 2);
        ++i;
      }
      ++loop_depth_;
      if (i < kids.size()) emit_substatement(kids[i], depth + 1);
      --loop_depth_;
      pop_environment();
      current_scope_ = old_scope;
      return;
    }
    if (kind == NSwitchStatement) {
      line(depth, "switch-statement");
      if (kids.empty()) throw std::runtime_error("switch has no condition");
      push_environment();
      emit_condition(kids[0], depth + 1, true);
      ++switch_depth_;
      switches_.push_back(SwitchState());
      if (kids.size() > 1) emit_substatement(kids[1], depth + 1);
      switches_.pop_back();
      --switch_depth_;
      pop_environment();
      return;
    }
    if (kind == NCaseStatement) {
      if (!switch_depth_ || kids.size() < 2) throw std::runtime_error("case label outside switch");
      ExpressionFact label = expression(kids[0]);
      if (!label.constant || (!is_integral(label.type) && !is_enum(label.type)))
        throw std::runtime_error("case label is not an integral constant expression");
      if (!switches_.back().labels.insert(label.value).second)
        throw std::runtime_error("duplicate case label");
      line(depth, "case-statement");
      emit_expression(kids[0], depth + 1);
      emit_statement(kids[1], depth + 1);
      return;
    }
    if (kind == NDefaultStatement) {
      if (!switch_depth_ || kids.empty()) throw std::runtime_error("default label outside switch");
      if (switches_.back().has_default) throw std::runtime_error("duplicate default label");
      switches_.back().has_default = true;
      line(depth, "default-statement");
      emit_statement(kids[0], depth + 1);
      return;
    }
    if (kind == NBreakStatement) {
      if (!loop_depth_ && !switch_depth_) throw std::runtime_error("break outside loop or switch");
      line(depth, "break-statement"); return;
    }
    if (kind == NContinueStatement) {
      if (!loop_depth_) throw std::runtime_error("continue outside loop");
      line(depth, "continue-statement"); return;
    }
    if (kind == NSimpleDeclaration || kind == NAliasDeclaration) {
      emit_local_declaration(node, depth); return;
    }
    if (kind == NUsingDirective) {
      const std::vector<Id> found = lookup_node(kids.empty() ? none : kids[0], true, false);
      if (found.empty()) throw std::runtime_error("using directive target not found");
      const Id ns = namespace_scope(found[0]);
      if (ns == none) throw std::runtime_error("using directive target is not a namespace");
      if (environments_.empty()) push_environment();
      environments_.back().using_directives.push_back(ns);
      return;
    }
    if (kind == NUsingDeclaration) {
      const std::vector<Id> found = lookup_node(kids.empty() ? none : kids[0]);
      if (found.empty()) throw std::runtime_error("using declaration target not found");
      for (std::size_t i = 0; i < found.size(); ++i) add_environment_binding(found[i]);
      const Id binding = unit_.binding_for_node(node);
      if (binding != none) add_environment_binding(binding);
      return;
    }
    if (kind == NNamespaceAliasDefinition) {
      const Id binding = unit_.binding_for_node(node);
      if (binding != none) add_environment_binding(binding);
      return;
    }
    if (kind == NEnumSpecifier) {
      visibility_.mark_type_declarations(unit_, ast_, node);
      line(depth, "simple-declaration");
      return;
    }
    if (kind == NClassSpecifier && ast_.nodes[node].composite == none) {
      visibility_.mark_type_declarations(unit_, ast_, node);
      Id union_key = none;
      for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
        if (ast_.nodes[c].kind == NClassKey && text(c) == "union") union_key = c;
      if (union_key != none)
        emit_anonymous_union_storage(node, node, depth);
      return;
    }
    if (kind == NEnumSpecifier) { line(depth, "simple-declaration"); return; }
    if (kind == NStaticAssertDeclaration || kind == NEmptyDeclaration ||
        kind == NClassSpecifier ||
        kind == NClassForwardDeclaration) return;
    throw std::runtime_error("unsupported PA7 statement node");
  }
};

}  // namespace

void EmitSemantics(const std::vector<std::string>& inputs, const std::string& output)
{
  if (inputs.empty() || output.empty())
    throw std::runtime_error("invalid --emit-semantics invocation");
  std::ofstream file(output.c_str(), std::ios::out | std::ios::trunc);
  if (!file) throw std::runtime_error("cannot open output file: " + output);
  file << inputs.size() << " translation units\n";
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    pa6::SemanticUnit unit = pa6::AnalyzeTranslationUnit(inputs[i]);
    file << "start translation unit " << i + 1 << "\n";
    SemanticDumper(unit).print(file);
    file << "end translation unit\n";
  }
}

}  // namespace cppgm
