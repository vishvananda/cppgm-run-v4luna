#include "semantic/pa6.h"

#include "parser/ast_parser.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cppgm {
namespace {

typedef std::size_t Id;
const Id none = static_cast<Id>(-1);

enum TypeKind {
  InvalidType, FundamentalType, NamedType, TemplateParameterType,
  QualifiedType, PointerType, LvalueReferenceType, RvalueReferenceType,
  ArrayType, FunctionType
};

struct Type
{
  TypeKind kind;
  std::string atom;
  Id base;
  Id entity;
  long long bound;
  bool variadic;
  std::vector<Id> parameters;
  Type() : kind(InvalidType), base(none), entity(none), bound(0), variadic(false) {}
};

struct TypeKey
{
  TypeKind kind;
  std::string atom;
  Id base;
  Id entity;
  long long bound;
  bool variadic;
  std::vector<Id> parameters;
  bool operator==(const TypeKey& r) const
  {
    return kind == r.kind && atom == r.atom && base == r.base && entity == r.entity &&
        bound == r.bound && variadic == r.variadic && parameters == r.parameters;
  }
};

struct TypeKeyHash
{
  std::size_t operator()(const TypeKey& k) const
  {
    std::size_t h = static_cast<std::size_t>(k.kind) + 0x9e3779b9U;
    const std::size_t prime = static_cast<std::size_t>(1099511628211ULL);
    h = (h ^ std::hash<std::string>()(k.atom)) * prime;
    h = (h ^ std::hash<Id>()(k.base)) * prime;
    h = (h ^ std::hash<Id>()(k.entity)) * prime;
    h = (h ^ std::hash<long long>()(k.bound)) * prime;
    h = (h ^ std::hash<bool>()(k.variadic)) * prime;
    for (std::size_t i = 0; i < k.parameters.size(); ++i)
      h = (h ^ std::hash<Id>()(k.parameters[i])) * prime;
    return h;
  }
};

enum ScopeKind { NamespaceScope, TemplateScope, ClassScope, EnumScope,
                 FunctionScope, BlockScope };
enum EntityKind { NamespaceEntity, ClassEntity, EnumEntity, AliasNamespaceEntity };
enum BindingKind { TypeBinding, AliasBinding, EnumeratorBinding,
                   FunctionBinding, VariableBinding, ParameterBinding,
                   NamespaceBinding, TemplateNameBinding };

struct Scope
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
  std::unordered_map<Id, std::vector<Id> > index;
  Scope(ScopeKind k = BlockScope, const std::string& n = std::string(),
        Id p = none, Id e = none)
      : kind(k), name(n), parent(p), entity(e), inline_namespace(false) {}
};

struct Entity
{
  EntityKind kind;
  std::string name;
  std::string key;
  Id scope;
  Id type;
  bool complete;
  bool defined;
  bool scoped_enum;
  bool is_union;
  std::string class_key;
  Id underlying;
  Entity() : kind(ClassEntity), scope(none), type(none), complete(false), defined(false),
      scoped_enum(false), is_union(false), underlying(none) {}
};

struct Binding
{
  BindingKind kind;
  Id name_id;
  std::string name;
  Id type;
  Id entity;
  Id scope;
  bool output;
  bool constexpr_object;
  bool static_storage;
  bool namespace_alias;
  std::string display_override;
  std::string type_override;
  bool has_value;
  long long value;
  Binding() : kind(VariableBinding), name_id(none), type(none), entity(none),
      scope(none), output(true), constexpr_object(false), static_storage(false),
      namespace_alias(false), has_value(false), value(0) {}
};

struct ConstResult
{
  bool valid;
  long long value;
  Id type;
  bool lvalue;
  ConstResult(bool v = false, long long n = 0, Id t = none, bool l = false)
      : valid(v), value(n), type(t), lvalue(l) {}
};

class Analyzer
{
public:
  explicit Analyzer(Ast& ast) : ast_(ast), global_(0), current_(0), unnamed_namespace_(none)
  {
    scopes_.push_back(Scope(NamespaceScope, "<global>"));
    current_ = global_;
    fundamental("void"); fundamental("bool"); fundamental("char");
    fundamental("signed char"); fundamental("unsigned char");
    fundamental("short int"); fundamental("unsigned short int");
    fundamental("int"); fundamental("unsigned int");
    fundamental("long int"); fundamental("unsigned long int");
    fundamental("long long int"); fundamental("unsigned long long int");
    fundamental("wchar_t"); fundamental("char16_t"); fundamental("char32_t");
    fundamental("float"); fundamental("double"); fundamental("long double");
    fundamental("nullptr_t");
  }

  void analyze()
  {
    if (ast_.nodes.empty() || ast_.nodes[0].kind != NTranslationUnit)
      throw std::runtime_error("invalid translation unit AST");
    for (Id c = ast_.nodes[0].first_child; c != none; c = ast_.nodes[c].next_sibling)
      process(c, global_);
    finish_array_completions();
  }

  void print(std::ostream& out) const
  {
    out << "translation-unit\n";
    print_scope(global_, 1, out);
  }

private:
  Ast& ast_;
  std::vector<Type> types_;
  std::unordered_map<TypeKey, Id, TypeKeyHash> type_ids_;
  std::vector<Scope> scopes_;
  std::vector<Entity> entities_;
  std::vector<Binding> bindings_;
  std::vector<std::pair<Id, Id> > incomplete_arrays_;
  Id global_;
  Id current_;
  Id unnamed_namespace_;
  std::unordered_map<std::string, Id> named_namespace_entities_;
  std::unordered_map<std::string, Id> class_entities_;
  std::unordered_map<std::string, Id> enum_entities_;
  std::unordered_map<Id, std::vector<Id> > predeclared_bindings_;
  std::unordered_map<Id, Id> class_node_types_;
  std::unordered_map<Id, Id> enum_node_types_;
  std::unordered_map<Id, Id> class_tag_bindings_;
  std::unordered_map<Id, Id> enum_tag_bindings_;
  std::unordered_map<Id, Id> qualified_enum_output_bindings_;
  std::unordered_map<Id, Id> qualified_enum_output_scopes_;
  std::unordered_set<Id> class_members_analyzed_;
  std::unordered_map<Id, std::string> pending_anonymous_names_;
  std::unordered_map<Id, std::string> class_key_by_declaration_;
  std::unordered_map<Id, std::string> enum_key_by_declaration_;
  std::unordered_map<Id, Id> unnamed_namespace_by_parent_;
  std::unordered_set<Id> anonymous_union_nodes_;

  std::vector<Id> children(Id node) const
  {
    std::vector<Id> result;
    if (node == none) return result;
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
      result.push_back(c);
    return result;
  }

  bool contains_parameter_pack_outside_nested_clause(Id node) const
  {
    if (node == none || ast_.nodes[node].kind == NParameterClause) return false;
    if (ast_.nodes[node].kind == NParameterPack) return true;
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
      if (contains_parameter_pack_outside_nested_clause(c)) return true;
    return false;
  }

  std::vector<Id> auxiliary_children(Id node) const
  {
    std::vector<Id> result;
    if (node == none) return result;
    for (Id c = ast_.nodes[node].first_aux_child; c != none; c = ast_.nodes[c].next_aux_sibling)
      result.push_back(c);
    return result;
  }

  std::string text(Id node) const
  {
    if (node == none) return std::string();
    const AstNode& n = ast_.nodes[node];
    if (n.composite != none) return ast_.composite_atoms[n.composite];
    if (n.atom != none && n.atom < ast_.tokens.size()) return ast_.tokens[n.atom].text.get();
    return std::string();
  }

  Id name_id(const std::string& name)
  {
    Id result = ast_.find_name(name);
    return result == none ? ast_.intern_name(name) : result;
  }

  Id intern(Type type)
  {
    TypeKey key;
    key.kind = type.kind; key.atom = type.atom; key.base = type.base;
    key.entity = type.entity; key.bound = type.bound; key.variadic = type.variadic;
    key.parameters = type.parameters;
    std::unordered_map<TypeKey, Id, TypeKeyHash>::const_iterator old = type_ids_.find(key);
    if (old != type_ids_.end()) return old->second;
    const Id id = types_.size();
    types_.push_back(type);
    type_ids_.insert(std::make_pair(key, id));
    return id;
  }

  Id fundamental(const std::string& name)
  {
    Type t; t.kind = FundamentalType; t.atom = name; return intern(t);
  }

  Id named_type(Id entity)
  {
    Type t; t.kind = NamedType; t.entity = entity; return intern(t);
  }

  Id template_parameter_type(const std::string& spelling)
  {
    Type t; t.kind = TemplateParameterType; t.atom = spelling; return intern(t);
  }

  Id unary_type(TypeKind kind, Id base)
  {
    Type t; t.kind = kind; t.base = base; return intern(t);
  }

  Id qualified(Id type, bool is_const, bool is_volatile)
  {
    if (!is_const && !is_volatile) return type;
    if (types_[type].kind == LvalueReferenceType || types_[type].kind == RvalueReferenceType)
      return type;
    if (types_[type].kind == ArrayType) {
      Type array = types_[type];
      array.base = qualified(array.base, is_const, is_volatile);
      return intern(array);
    }
    bool c = is_const, v = is_volatile;
    if (types_[type].kind == QualifiedType) {
      const std::string q = types_[type].atom;
      c = c || q.find('c') != std::string::npos;
      v = v || q.find('v') != std::string::npos;
      type = types_[type].base;
    }
    Type t; t.kind = QualifiedType; t.base = type;
    t.atom = c ? (v ? "cv" : "c") : "v";
    return intern(t);
  }

  std::string type_spelling(Id id) const
  {
    if (id == none || id >= types_.size()) return "<invalid>";
    const Type& t = types_[id];
    switch (t.kind) {
    case FundamentalType: return t.atom;
    case TemplateParameterType: return t.atom;
    case NamedType: {
      if (t.entity >= entities_.size()) return "<invalid-type>";
      const Entity& e = entities_[t.entity];
      if (e.kind == EnumEntity)
        return e.scoped_enum ? "enum class " + e.name : "enum " + e.name;
      return e.class_key + " " + e.name;
    }
    case QualifiedType: {
      std::string q = t.atom == "c" ? "const " : t.atom == "v" ? "volatile " : "const volatile ";
      return q + type_spelling(t.base);
    }
    case PointerType: return "pointer to " + type_spelling(t.base);
    case LvalueReferenceType: return "lvalue-reference to " + type_spelling(t.base);
    case RvalueReferenceType: return "rvalue-reference to " + type_spelling(t.base);
    case ArrayType:
      return std::string("array of ") + (t.bound == 0 ? "0" : number(t.bound)) +
          " " + type_spelling(t.base);
    case FunctionType: {
      std::string out = "function of (";
      for (std::size_t i = 0; i < t.parameters.size(); ++i) {
        if (i) out += ", ";
        out += type_spelling(t.parameters[i]);
      }
      if (t.variadic) {
        if (!t.parameters.empty()) out += ", ";
        out += "...";
      }
      out += ") returning " + type_spelling(t.base);
      return out;
    }
    default: return "<invalid>";
    }
  }

  static std::string number(long long n)
  {
    std::ostringstream out; out << n; return out.str();
  }

  Id new_scope(ScopeKind kind, const std::string& name, Id parent, Id entity = none)
  {
    const Id id = scopes_.size();
    scopes_.push_back(Scope(kind, name, parent, entity));
    if (parent != none) scopes_[parent].children.push_back(id);
    return id;
  }

  Id new_entity(Entity entity)
  {
    const Id id = entities_.size(); entities_.push_back(entity); return id;
  }

  Id add_binding(Id scope, BindingKind kind, const std::string& name,
                 Id type, Id entity = none, bool output = true)
  {
    Binding binding; binding.kind = kind; binding.name = name;
    binding.name_id = name_id(name); binding.type = type; binding.entity = entity;
    binding.scope = scope; binding.output = output;
    const Id id = bindings_.size(); bindings_.push_back(binding);
    scopes_[scope].bindings.push_back(id);
    scopes_[scope].index[binding.name_id].push_back(id);
    if (output) scopes_[scope].output_order.push_back(id);
    return id;
  }

  void set_binding_output(Id id, bool output)
  {
    if (id >= bindings_.size() || !output || bindings_[id].output) return;
    bindings_[id].output = true;
    scopes_[bindings_[id].scope].output_order.push_back(id);
  }

  Id add_or_find_binding(Id scope, BindingKind kind, const std::string& name,
                         Id type, Id entity, bool output)
  {
    Id n = name_id(name);
    std::unordered_map<Id, std::vector<Id> >::const_iterator i = scopes_[scope].index.find(n);
    if (i != scopes_[scope].index.end()) {
      for (std::size_t x = 0; x < i->second.size(); ++x) {
        Binding& b = bindings_[i->second[x]];
        if (b.kind == kind && (entity == none || b.entity == entity)) {
          if (b.type == none) b.type = type;
          if (output) set_binding_output(i->second[x], true);
          return i->second[x];
        }
      }
    }
    return add_binding(scope, kind, name, type, entity, output);
  }

  void reject_namespace_name_conflict(Id scope, const std::string& name) const
  {
    const Id old = local_lookup(scope, name, false, false);
    if (old != none && bindings_[old].kind == NamespaceBinding)
      throw std::runtime_error("declaration conflicts with a namespace name");
  }

  bool is_type_binding(BindingKind kind) const
  { return kind == TypeBinding || kind == AliasBinding || kind == TemplateNameBinding; }

  Id local_lookup(Id scope, const std::string& name, bool types_only = false,
                  bool namespaces_only = false) const
  {
    if (scope == none) return none;
    const Id n = ast_.find_name(name);
    if (n == none) return none;
    std::unordered_map<Id, std::vector<Id> >::const_iterator it = scopes_[scope].index.find(n);
    if (it == scopes_[scope].index.end()) return none;
    for (std::vector<Id>::const_reverse_iterator b = it->second.rbegin(); b != it->second.rend(); ++b) {
      const Binding& binding = bindings_[*b];
      if (namespaces_only) {
        if (binding.kind == NamespaceBinding) return *b;
        continue;
      }
      if (types_only) {
        if (is_type_binding(binding.kind)) return *b;
        // An ordinary declaration hides a type of the same spelling.
        return none;
      }
      return *b;
    }
    return none;
  }

  void lookup_directives(Id scope, const std::string& name, bool types_only,
                         bool namespaces_only, std::unordered_set<Id>& visited,
                         std::vector<Id>& result) const
  {
    if (scope == none || !visited.insert(scope).second) return;
    const Id local = local_lookup(scope, name, types_only, namespaces_only);
    if (local != none) { result.push_back(local); return; }
    const Scope& s = scopes_[scope];
    for (std::size_t i = 0; i < s.using_directives.size(); ++i)
      lookup_directives(s.using_directives[i], name, types_only, namespaces_only, visited, result);
    if (s.kind == NamespaceScope) {
      for (std::size_t i = 0; i < s.children.size(); ++i) {
        const Id child = s.children[i];
        if (scopes_[child].inline_namespace)
          lookup_directives(child, name, types_only, namespaces_only, visited, result);
      }
    }
  }

  Id lookup(Id scope, const std::string& name, bool types_only = false,
            bool namespaces_only = false) const
  {
    std::unordered_set<Id> visited;
    for (Id s = scope; s != none; s = scopes_[s].parent) {
      const Id local = local_lookup(s, name, types_only, namespaces_only);
      if (local != none) return local;
      std::vector<Id> found;
      for (std::size_t i = 0; i < scopes_[s].using_directives.size(); ++i)
        lookup_directives(scopes_[s].using_directives[i], name, types_only,
                          namespaces_only, visited, found);
      for (std::size_t i = 0; i < scopes_[s].children.size(); ++i) {
        const Id child = scopes_[s].children[i];
        if (scopes_[child].inline_namespace)
          lookup_directives(child, name, types_only, namespaces_only, visited, found);
      }
      if (found.size() == 1) return found[0];
      if (found.size() > 1) throw std::runtime_error("ambiguous name lookup");
    }
    return none;
  }

  std::vector<std::string> split_qualified(const std::string& spelling) const
  {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < spelling.size()) {
      if (spelling.compare(start, 2, "::") == 0) { start += 2; continue; }
      std::size_t end = spelling.find("::", start);
      if (end == std::string::npos) end = spelling.size();
      std::string part = spelling.substr(start, end - start);
      const std::size_t angle = part.find('<');
      if (angle != std::string::npos) part.erase(angle);
      if (!part.empty()) result.push_back(part);
      start = end == spelling.size() ? end : end + 2;
    }
    return result;
  }

  bool is_absolute_name(const std::string& spelling) const
  { return spelling.compare(0, 2, "::") == 0; }

  Id namespace_scope_for_binding(Id binding) const
  {
    if (binding == none) return none;
    const Binding& b = bindings_[binding];
    if (b.kind == NamespaceBinding && b.entity < entities_.size()) return entities_[b.entity].scope;
    if (b.kind == TypeBinding || b.kind == AliasBinding || b.kind == TemplateNameBinding) {
      if (b.type < types_.size() && types_[b.type].kind == NamedType) {
        const Id e = types_[b.type].entity;
        if (e < entities_.size()) return entities_[e].scope;
      }
    }
    return none;
  }

  Id qualified_component(Id scope, const std::string& name, bool types_only,
                         bool namespaces_only)
  {
    Id local = local_lookup(scope, name, types_only, namespaces_only);
    if (local != none) return local;
    std::unordered_set<Id> visited;
    std::vector<Id> found;
    const Scope& s = scopes_[scope];
    for (std::size_t i = 0; i < s.using_directives.size(); ++i)
      lookup_directives(s.using_directives[i], name, types_only, namespaces_only, visited, found);
    for (std::size_t i = 0; i < s.children.size(); ++i)
      if (scopes_[s.children[i]].inline_namespace)
        lookup_directives(s.children[i], name, types_only, namespaces_only, visited, found);
    if (found.size() == 1) return found[0];
    if (found.size() > 1) throw std::runtime_error("ambiguous qualified name");
    return none;
  }

  Id resolve_name_binding(Id from, const std::string& spelling,
                          bool types_only = false, bool namespaces_only = false)
  {
    std::vector<std::string> parts = split_qualified(spelling);
    if (parts.empty()) return none;
    Id binding = none;
    Id scope = is_absolute_name(spelling) ? global_ : from;
    binding = lookup(scope, parts[0], parts.size() == 1 && types_only, namespaces_only);
    if (binding == none) return none;
    for (std::size_t i = 1; i < parts.size(); ++i) {
      scope = namespace_scope_for_binding(binding);
      if (scope == none) return none;
      const bool final_component = i + 1 == parts.size();
      binding = qualified_component(scope, parts[i], final_component && types_only,
                                    final_component && namespaces_only);
      if (binding == none) {
        std::unordered_set<Id> visited;
        std::vector<Id> found;
        for (std::size_t d = 0; d < scopes_[scope].using_directives.size(); ++d)
          lookup_directives(scopes_[scope].using_directives[d], parts[i],
                            final_component && types_only,
                            final_component && namespaces_only, visited, found);
        if (found.size() == 1) binding = found[0];
        else if (found.size() > 1) throw std::runtime_error("ambiguous qualified lookup");
      }
      if (binding == none) return none;
    }
    return binding;
  }

  Id resolve_type(Id scope, const std::string& name, bool elaborated_class = false)
  {
    Id b = resolve_name_binding(scope, name, true, false);
    if (b == none && elaborated_class) {
      Entity e; e.kind = ClassEntity; e.name = name; e.key = scope_key(scope, name);
      e.class_key = "class"; e.complete = false;
      Id eid = new_entity(e); e.type = named_type(eid); entities_[eid].type = e.type;
      entities_[eid].scope = none;
      add_binding(scope, TypeBinding, name, e.type, eid);
      return e.type;
    }
    if (b == none) throw std::runtime_error("unknown type name: " + name);
    if (!is_type_binding(bindings_[b].kind)) throw std::runtime_error("name does not denote a type");
    return bindings_[b].type;
  }

  std::string scope_key(Id scope, const std::string& name) const
  {
    std::vector<std::string> names;
    for (Id s = scope; s != none; s = scopes_[s].parent) {
      if (!scopes_[s].name.empty() && scopes_[s].name != "<global>") names.push_back(scopes_[s].name);
    }
    std::string key;
    for (std::vector<std::string>::reverse_iterator i = names.rbegin(); i != names.rend(); ++i) {
      if (!key.empty()) key += "::";
      key += *i;
    }
    if (!key.empty()) key += "::";
    key += name;
    return key;
  }

  Id type_from_decl_spec(Id seq, Id scope, bool allow_declaration = true)
  {
    bool is_const = false, is_volatile = false;
    bool is_unsigned = false, is_signed = false;
    int longs = 0, shorts = 0;
    std::string base;
    const std::vector<Id> specs = children(seq);
    for (std::size_t i = 0; i < specs.size(); ++i) {
      const Id node = specs[i];
      const NodeKind kind = ast_.nodes[node].kind;
      if (kind == NClassSpecifier) { base = "@" + number(process_class(node, scope, allow_declaration, allow_declaration)); continue; }
      if (kind == NClassForwardDeclaration) {
        std::string name = text(node);
        base = "@" + number(process_elaborated_class(node, scope, name)); continue;
      }
      if (kind == NEnumSpecifier) { base = "@" + number(process_enum(node, scope, allow_declaration, allow_declaration)); continue; }
      if (kind == NDeclSpecifier) {
        if (ast_.nodes[node].composite != none && text(node).find("decltype(") == 0) {
          const std::vector<Id> expr = children(node);
          if (expr.empty()) throw std::runtime_error("missing decltype expression");
          base = "@" + number(decltype_type(expr[0], scope)); continue;
        }
        const std::string s = text(node);
        if (s == "const") { is_const = true; continue; }
        if (s == "volatile") { is_volatile = true; continue; }
        if (s == "signed") { is_signed = true; continue; }
        if (s == "unsigned") { is_unsigned = true; continue; }
        if (s == "short") { ++shorts; continue; }
        if (s == "long") { ++longs; continue; }
        if (s == "int" || s == "char" || s == "float" || s == "double" ||
            s == "bool" || s == "void" || s == "wchar_t" || s == "char16_t" ||
            s == "char32_t" || s == "nullptr_t") {
          if (!base.empty()) throw std::runtime_error("multiple declaration types");
          base = s; continue;
        }
        if (s == "typedef" || s == "extern" || s == "static" || s == "thread_local" ||
            s == "inline" || s == "virtual" || s == "constexpr" || s == "friend" ||
            s == "register" || s == "mutable" || s == "explicit") continue;
        if (!base.empty()) throw std::runtime_error("multiple declaration types");
        base = "@" + number(resolve_type(scope, s));
        continue;
      }
      if (kind == NDecltypeSpecifier) {
        const std::vector<Id> expr = children(node);
        if (expr.empty()) throw std::runtime_error("missing decltype expression");
        base = "@" + number(decltype_type(expr[0], scope)); continue;
      }
    }
    if (base.empty() && (is_signed || is_unsigned || longs || shorts)) base = "int";
    if (base.empty()) throw std::runtime_error("declaration has no type");
    Id result;
    if (base[0] == '@') {
      char* end = 0; const unsigned long value = std::strtoul(base.c_str() + 1, &end, 10);
      if (end == base.c_str() + 1 || *end) throw std::runtime_error("invalid type identity");
      result = static_cast<Id>(value);
    } else {
      std::string f = base;
      if (base == "int") {
        if (shorts) f = is_unsigned ? "unsigned short int" : "short int";
        else if (longs >= 2) f = is_unsigned ? "unsigned long long int" : "long long int";
        else if (longs == 1) f = is_unsigned ? "unsigned long int" : "long int";
        else if (is_unsigned) f = "unsigned int";
      } else if (base == "char" && is_unsigned) f = "unsigned char";
      else if (base == "char" && is_signed) f = "signed char";
      else if (base == "double" && longs) f = "long double";
      result = fundamental(f);
    }
    return qualified(result, is_const, is_volatile);
  }

  Id process_elaborated_class(Id node, Id scope, const std::string& name)
  {
    if (name.empty()) throw std::runtime_error("unnamed elaborated class type");
    const std::vector<std::string> parts = split_qualified(name);
    const std::string leaf = parts.empty() ? name : parts.back();
    Id search_scope = is_absolute_name(name) ? global_ : scope;
    if (parts.size() > 1) {
      const std::string prefix = name.substr(0, name.rfind("::"));
      const Id qualifier = resolve_name_binding(search_scope, prefix, false, false);
      search_scope = namespace_scope_for_binding(qualifier);
      if (search_scope == none) throw std::runtime_error("elaborated class qualifier is not a scope");
    }
    const Id id = ast_.find_name(leaf);
    for (Id s = search_scope; s != none; s = scopes_[s].parent) {
      if (id == none) break;
      std::unordered_map<Id, std::vector<Id> >::const_iterator entries = scopes_[s].index.find(id);
      if (entries == scopes_[s].index.end()) continue;
      for (std::vector<Id>::const_reverse_iterator b = entries->second.rbegin();
           b != entries->second.rend(); ++b) {
        const Binding& binding = bindings_[*b];
        if (!is_type_binding(binding.kind) || binding.type >= types_.size() ||
            types_[binding.type].kind != NamedType) continue;
        const Id entity = types_[binding.type].entity;
        if (entity < entities_.size() && entities_[entity].kind == ClassEntity)
          return binding.type;
      }
    }
    if (parts.size() > 1) throw std::runtime_error("unknown elaborated class type");
    const std::string key = scope_key(scope, leaf);
    Id eid;
    std::unordered_map<std::string, Id>::iterator old = class_entities_.find(key);
    if (old != class_entities_.end()) eid = old->second;
    else {
      Entity e; e.kind = ClassEntity; e.name = leaf; e.key = key; e.class_key = "class";
      eid = new_entity(e); entities_[eid].type = named_type(eid);
      class_entities_[key] = eid;
      add_binding(scope, TypeBinding, leaf, entities_[eid].type, eid);
    }
    (void)node;
    return entities_[eid].type;
  }

  std::string fundamental_sequence(const std::vector<std::string>& words,
                                   bool& is_const, bool& is_volatile) const
  {
    bool uns = false, sign = false; int lng = 0, sh = 0;
    std::string base;
    for (std::size_t i = 0; i < words.size(); ++i) {
      const std::string& s = words[i];
      if (s == "const") is_const = true; else if (s == "volatile") is_volatile = true;
      else if (s == "unsigned") uns = true; else if (s == "signed") sign = true;
      else if (s == "long") ++lng; else if (s == "short") ++sh;
      else if (s == "int" || s == "char" || s == "double" || s == "float" ||
          s == "bool" || s == "void" || s == "wchar_t" || s == "char16_t" || s == "char32_t") base = s;
    }
    if (base.empty() || base == "int") {
      if (sh) base = uns ? "unsigned short int" : "short int";
      else if (lng > 1) base = uns ? "unsigned long long int" : "long long int";
      else if (lng) base = uns ? "unsigned long int" : "long int";
      else base = uns ? "unsigned int" : "int";
    } else if (base == "char" && uns) base = "unsigned char";
    else if (base == "char" && sign) base = "signed char";
    else if (base == "double" && lng) base = "long double";
    return base;
  }

  bool has_specifier(Id seq, const std::string& spelling) const
  {
    for (Id c = ast_.nodes[seq].first_child; c != none; c = ast_.nodes[c].next_sibling)
      if (ast_.nodes[c].kind == NDeclSpecifier && text(c) == spelling) return true;
    return false;
  }

  std::string declared_name(Id declarator) const
  {
    if (declarator == none) return std::string();
    if (ast_.nodes[declarator].kind == NIdentifier) return text(declarator);
    for (Id c = ast_.nodes[declarator].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      if (ast_.nodes[c].kind == NIdentifier) return text(c);
      std::string nested = declared_name(c);
      if (!nested.empty()) return nested;
    }
    return std::string();
  }

  Id declared_identifier_node(Id declarator) const
  {
    if (declarator == none) return none;
    if (ast_.nodes[declarator].kind == NIdentifier) return declarator;
    for (Id c = ast_.nodes[declarator].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      if (ast_.nodes[c].kind == NIdentifier) return c;
      Id nested = declared_identifier_node(c);
      if (nested != none) return nested;
    }
    return none;
  }

  struct DeclaratorAction
  {
    NodeKind kind;
    Id node;
    bool is_const;
    bool is_volatile;
    DeclaratorAction(NodeKind k, Id n, bool c = false, bool v = false)
        : kind(k), node(n), is_const(c), is_volatile(v) {}
  };

  void declarator_path(Id node, std::vector<DeclaratorAction>& actions, Id scope)
  {
    if (node == none) return;
    std::vector<DeclaratorAction> ptrs;
    Id last_pointer = none;
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      const NodeKind kind = ast_.nodes[c].kind;
      if (kind == NNestedDeclarator || kind == NDeclarator || kind == NAbstractDeclarator)
        declarator_path(c, actions, scope);
      else if (kind == NArraySuffix || kind == NParameterClause)
        { actions.push_back(DeclaratorAction(kind, c)); last_pointer = none; }
      else if (kind == NPtrOperator) {
        ptrs.push_back(DeclaratorAction(kind, c)); last_pointer = ptrs.size() - 1;
      } else if (kind == NCvQualifier && last_pointer != none) {
        if (text(c) == "const") ptrs[last_pointer].is_const = true;
        if (text(c) == "volatile") ptrs[last_pointer].is_volatile = true;
      }
    }
    for (std::vector<DeclaratorAction>::reverse_iterator p = ptrs.rbegin(); p != ptrs.rend(); ++p)
      actions.push_back(*p);
  }

  Id build_declarator(Id declarator, Id base, Id scope)
  {
    std::vector<DeclaratorAction> path;
    declarator_path(declarator, path, scope);
    for (std::vector<DeclaratorAction>::reverse_iterator i = path.rbegin(); i != path.rend(); ++i) {
      if (i->kind == NPtrOperator) {
        const std::string op = text(i->node);
        if (op == "*") {
          if (types_[base].kind == LvalueReferenceType || types_[base].kind == RvalueReferenceType)
            throw std::runtime_error("pointer to reference type is invalid");
          base = qualified(unary_type(PointerType, base), i->is_const, i->is_volatile);
        }
        else if (op == "&") {
          if (types_[base].kind == LvalueReferenceType || types_[base].kind == RvalueReferenceType)
            base = unary_type(LvalueReferenceType, types_[base].base);
          else base = unary_type(LvalueReferenceType, base);
        } else if (op == "&&") {
          if (types_[base].kind == LvalueReferenceType) base = unary_type(LvalueReferenceType, types_[base].base);
          else if (types_[base].kind == RvalueReferenceType) base = unary_type(RvalueReferenceType, types_[base].base);
          else base = unary_type(RvalueReferenceType, base);
        }
      } else if (i->kind == NArraySuffix) {
        long long bound = 0;
        const Id expr = ast_.nodes[i->node].first_child;
        if (expr != none) {
          ConstResult result = eval(expr, scope);
          if (!result.valid || result.value <= 0) throw std::runtime_error("array bound is not a positive constant");
          bound = result.value;
        }
        if (types_[base].kind == LvalueReferenceType || types_[base].kind == RvalueReferenceType ||
            types_[base].kind == FunctionType)
          throw std::runtime_error("array element type is invalid");
        Type t; t.kind = ArrayType; t.base = base; t.bound = bound; base = intern(t);
      } else if (i->kind == NParameterClause) {
        if (types_[base].kind == FunctionType || types_[base].kind == ArrayType)
          throw std::runtime_error("function return type is invalid");
        Id function = build_function_type(i->node, base, scope);
        base = function;
      }
    }
    return base;
  }

  Id build_function_type(Id clause, Id result_type, Id scope)
  {
    Type fn; fn.kind = FunctionType; fn.base = result_type;
    bool variadic = false;
    for (Id p = ast_.nodes[clause].first_child; p != none; p = ast_.nodes[p].next_sibling) {
      if (ast_.nodes[p].kind == NParameterPack) { variadic = true; continue; }
      if (ast_.nodes[p].kind != NParameterDeclaration) continue;
      if (contains_parameter_pack_outside_nested_clause(p)) variadic = true;
      Id seq = ast_.nodes[p].first_child;
      Id declarator = seq == none ? none : ast_.nodes[seq].next_sibling;
      Id pt = type_from_decl_spec(seq, scope);
      if (declarator != none && (ast_.nodes[declarator].kind == NDeclarator ||
          ast_.nodes[declarator].kind == NAbstractDeclarator))
        pt = build_declarator(declarator, pt, scope);
      fn.parameters.push_back(pt);
    }
    if (fn.parameters.size() == 1 && types_[fn.parameters[0]].kind == FundamentalType &&
        types_[fn.parameters[0]].atom == "void" && !variadic)
      fn.parameters.clear();
    fn.variadic = variadic;
    return intern(fn);
  }

  Id type_id(Id node, Id scope)
  {
    Id seq = ast_.nodes[node].first_child;
    if (seq == none) throw std::runtime_error("empty type-id");
    Id base = type_from_type_specifier_seq(seq, scope);
    Id declarator = ast_.nodes[seq].next_sibling;
    if (declarator != none) base = build_declarator(declarator, base, scope);
    return base;
  }

  Id type_from_type_specifier_seq(Id seq, Id scope)
  {
    bool c = false, v = false;
    std::vector<std::string> words;
    Id named = none;
    for (Id p = ast_.nodes[seq].first_child; p != none; p = ast_.nodes[p].next_sibling) {
      const NodeKind k = ast_.nodes[p].kind;
      const std::string s = text(p);
      if (k == NCvQualifier) { if (s == "const") c = true; else if (s == "volatile") v = true; }
      else if (k == NTypeName) named = resolve_type(scope, s);
      else if (k == NTypeSpecifier) words.push_back(s);
      else if (k == NClassForwardDeclaration) named = process_elaborated_class(p, scope, s);
      else if (k == NDecltypeSpecifier) {
        const std::vector<Id> e = children(p);
        if (e.empty()) throw std::runtime_error("missing decltype expression");
        named = decltype_type(e[0], scope);
      }
    }
    Id result = named;
    if (result == none) {
      bool wc = false, wv = false;
      const std::string f = fundamental_sequence(words, wc, wv);
      c = c || wc; v = v || wv; result = fundamental(f);
    }
    return qualified(result, c, v);
  }

  std::string type_id_name(Id node) const
  {
    if (node == none) return std::string();
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      if (ast_.nodes[c].kind == NTypeName || ast_.nodes[c].kind == NClassForwardDeclaration)
        return text(c);
    }
    return std::string();
  }

  Id class_entity_in_scope(Id scope, const std::string& name) const
  {
    Id b = local_lookup(scope, name, true, false);
    if (b == none || bindings_[b].type >= types_.size() ||
        types_[bindings_[b].type].kind != NamedType) return none;
    const Id e = types_[bindings_[b].type].entity;
    return e < entities_.size() && entities_[e].kind == ClassEntity ? e : none;
  }

  bool unnamed_union(Id node) const
  {
    if (node == none || ast_.nodes[node].kind != NClassSpecifier ||
        ast_.nodes[node].composite != none) return false;
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
      if (ast_.nodes[c].kind == NClassKey) return text(c) == "union";
    return false;
  }

  std::pair<std::size_t, std::size_t> anonymous_union_location(Id node) const
  {
    if (ast_.nodes[node].source_end_token_index != none &&
        ast_.nodes[node].source_file_id != none)
      return std::make_pair(ast_.nodes[node].source_file_id + 1,
                            ast_.nodes[node].source_end_token_index);
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
      if (ast_.nodes[c].kind == NClassKey)
        return std::make_pair(ast_.nodes[c].line, ast_.nodes[c].column);
    return std::make_pair(ast_.nodes[node].line, ast_.nodes[node].column);
  }

  Id process_class(Id node, Id scope, bool emit, bool process_members)
  {
    std::unordered_map<Id, Id>::const_iterator known_node = class_node_types_.find(node);
    if (known_node != class_node_types_.end()) {
      const Id type = known_node->second;
      const Id eid = types_[type].entity;
      const std::unordered_map<Id, Id>::const_iterator tag = class_tag_bindings_.find(node);
      if (emit && tag != class_tag_bindings_.end()) set_binding_output(tag->second, true);
      if (process_members && entities_[eid].complete && class_members_analyzed_.insert(eid).second)
        analyze_class_members(node, entities_[eid].scope);
      return type;
    }

    if (scopes_[scope].kind == NamespaceScope && unnamed_union(node) &&
        anonymous_union_nodes_.find(node) == anonymous_union_nodes_.end())
      throw std::runtime_error("namespace anonymous union requires static");

    std::string name = ast_.nodes[node].composite == none ? std::string() : text(node);
    std::unordered_map<Id, std::string>::const_iterator pending = pending_anonymous_names_.find(node);
    if (name.empty() && pending != pending_anonymous_names_.end()) name = pending->second;
    std::string key = name.empty() ? std::string("<anonymous>") : scope_key(scope, name);
    Id eid = none;
    if (!name.empty()) {
      std::unordered_map<std::string, Id>::iterator found = class_entities_.find(key);
      if (found != class_entities_.end()) eid = found->second;
      else eid = class_entity_in_scope(scope, name);
    }
    if (eid == none) {
      Entity e; e.kind = ClassEntity; e.name = name; e.key = key;
      e.class_key = "class";
      const std::vector<Id> cs = children(node);
      for (std::size_t i = 0; i < cs.size(); ++i)
      if (ast_.nodes[cs[i]].kind == NClassKey) {
          e.class_key = text(cs[i]); e.is_union = e.class_key == "union"; break;
        }
      eid = new_entity(e);
      entities_[eid].type = named_type(eid);
      if (!name.empty()) class_entities_[key] = eid;
    }
    if (name.empty()) {
      entities_[eid].key = key + "@" + number(ast_.nodes[node].line) + ":" + number(ast_.nodes[node].column);
    }
    Id type = entities_[eid].type;
    class_node_types_[node] = type;
    if (name.empty() && entities_[eid].is_union && scopes_[scope].kind == NamespaceScope) {
      const std::pair<std::size_t, std::size_t> location = anonymous_union_location(node);
      name = "__anonymous_union_type__" + number(location.first) + "_" + number(location.second);
      entities_[eid].name = name;
      entities_[eid].key = scope_key(scope, name);
      anonymous_union_nodes_.insert(node);
    }
    if (!name.empty() && anonymous_union_nodes_.find(node) == anonymous_union_nodes_.end()) {
      Id b = add_binding(scope, TypeBinding, name, type, eid, emit);
      class_tag_bindings_[node] = b;
      std::unordered_map<Id, std::string>::const_iterator keydecl = class_key_by_declaration_.find(node);
      if (keydecl != class_key_by_declaration_.end()) bindings_[b].display_override = keydecl->second;
      else {
        const std::vector<Id> parts = children(node);
        for (std::size_t i = 0; i < parts.size(); ++i)
          if (ast_.nodes[parts[i]].kind == NClassKey) bindings_[b].display_override = text(parts[i]);
      }
    }

    bool definition = true;
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
      if (ast_.nodes[c].kind == NBaseClause) definition = true;
    if (definition && !entities_[eid].complete) {
      entities_[eid].complete = true;
      entities_[eid].defined = true;
      entities_[eid].scope = new_scope(ClassScope, name, scope, eid);
    }
    if (entities_[eid].scope != none && !name.empty()) scopes_[entities_[eid].scope].name = name;
    if (definition && process_members && class_members_analyzed_.insert(eid).second)
      analyze_class_members(node, entities_[eid].scope);
    else if (definition && !process_members)
      predeclare_class_member_types(node, entities_[eid].scope);
    return type;
  }

  Id process_class_forward(Id node, Id scope, bool emit)
  {
    std::unordered_map<Id, Id>::const_iterator known = class_node_types_.find(node);
    if (known != class_node_types_.end()) {
      std::unordered_map<Id, Id>::const_iterator tag = class_tag_bindings_.find(node);
      if (emit && tag != class_tag_bindings_.end()) set_binding_output(tag->second, true);
      return known->second;
    }
    const std::string name = text(node);
    if (name.empty()) throw std::runtime_error("unnamed class forward declaration");
    const std::string key = scope_key(scope, name);
    Id eid = class_entity_in_scope(scope, name);
    if (eid == none) {
      std::unordered_map<std::string, Id>::iterator i = class_entities_.find(key);
      if (i != class_entities_.end()) eid = i->second;
    }
    if (eid == none) {
      Entity e; e.kind = ClassEntity; e.name = name; e.key = key;
      const std::vector<Id> cs = children(node);
      for (std::size_t i = 0; i < cs.size(); ++i)
        if (ast_.nodes[cs[i]].kind == NClassKey) { e.class_key = text(cs[i]); break; }
      eid = new_entity(e); entities_[eid].type = named_type(eid); class_entities_[key] = eid;
    }
    Id binding = add_binding(scope, TypeBinding, name, entities_[eid].type, eid, emit);
    class_tag_bindings_[node] = binding;
    const std::vector<Id> cs = children(node);
    for (std::size_t i = 0; i < cs.size(); ++i)
      if (ast_.nodes[cs[i]].kind == NClassKey) bindings_[binding].display_override = text(cs[i]);
    class_node_types_[node] = entities_[eid].type;
    return entities_[eid].type;
  }

  void predeclare_class_member_types(Id class_node, Id class_scope)
  {
    for (Id c = ast_.nodes[class_node].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      const NodeKind k = ast_.nodes[c].kind;
      if (k == NClassSpecifier) process_class(c, class_scope, false, false);
      else if (k == NClassForwardDeclaration) process_class_forward(c, class_scope, false);
      else if (k == NEnumSpecifier) process_enum(c, class_scope, false, false);
      else if (k == NAliasDeclaration) process_alias_declaration(c, class_scope, false);
      else if (k == NSimpleDeclaration) {
        Id seq = ast_.nodes[c].first_child;
        if (seq != none && ast_.nodes[seq].kind == NDeclSpecifierSeq && has_specifier(seq, "typedef"))
          process_simple_declaration(c, class_scope, false, true);
        else if (seq != none) {
          for (Id spec = ast_.nodes[seq].first_child; spec != none; spec = ast_.nodes[spec].next_sibling) {
            if (ast_.nodes[spec].kind == NClassSpecifier) process_class(spec, class_scope, false, false);
            if (ast_.nodes[spec].kind == NEnumSpecifier) process_enum(spec, class_scope, false, false);
          }
        }
      }
    }
  }

  void analyze_class_members(Id class_node, Id class_scope)
  {
    predeclare_class_member_types(class_node, class_scope);
    for (Id c = ast_.nodes[class_node].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      const NodeKind k = ast_.nodes[c].kind;
      if (k == NClassKey || k == NBaseClause || k == NAccessSpecifier || k == NEmptyDeclaration) continue;
      process(c, class_scope);
    }
  }

  Id process_enum(Id node, Id scope, bool emit, bool process_values)
  {
    std::unordered_map<Id, Id>::const_iterator known = enum_node_types_.find(node);
    if (known != enum_node_types_.end()) {
      Id t = known->second, e = types_[t].entity;
      std::unordered_map<Id, Id>::const_iterator tag = enum_tag_bindings_.find(node);
      if (emit && tag != enum_tag_bindings_.end()) set_binding_output(tag->second, true);
      if (process_values && !entities_[e].defined) {
        add_enum_values(node, scope, e);
        const std::string raw = ast_.nodes[node].composite == none ? std::string() : text(node);
        if (entities_[e].scoped_enum && raw.find("::") != std::string::npos)
          add_qualified_enum_output(raw, e);
      }
      return t;
    }
    std::string name = ast_.nodes[node].composite == none ? std::string() : text(node);
    std::unordered_map<Id, std::string>::const_iterator pending = pending_anonymous_names_.find(node);
    if (name.empty() && pending != pending_anonymous_names_.end()) name = pending->second;
    std::string raw_name = name;
    if (name.find("::") != std::string::npos) {
      name = name.substr(name.rfind("::") + 2);
      scope = qualified_declaration_scope(scope, raw_name, name);
    }
    bool scoped = false;
    bool has_underlying = false;
    Id underlying = fundamental("int");
    std::vector<Id> members = children(node);
    bool definition = false;
    for (std::size_t i = 0; i < members.size(); ++i) {
      if (ast_.nodes[members[i]].kind == NEnumKey) {
        const std::string key = text(members[i]); scoped = key == "class" || key == "struct";
      } else if (ast_.nodes[members[i]].kind == NTypeSpecifierSeq) {
        has_underlying = true;
        underlying = type_from_type_specifier_seq(members[i], scope);
      } else if (ast_.nodes[members[i]].kind == NTypeId) {
        has_underlying = true;
        underlying = type_id(members[i], scope);
      } else if (ast_.nodes[members[i]].kind == NEnumerator) {
        definition = true;
      }
    }
    if (!name.empty() && !scoped && !definition && !has_underlying &&
        raw_name.find("::") == std::string::npos) {
      const Id visible = resolve_name_binding(scope, name, true, false);
      if (visible != none && is_type_binding(bindings_[visible].kind) &&
          bindings_[visible].type < types_.size() &&
          types_[bindings_[visible].type].kind == NamedType &&
          entities_[types_[bindings_[visible].type].entity].kind == EnumEntity) {
        enum_node_types_[node] = bindings_[visible].type;
        return bindings_[visible].type;
      }
    }
    std::string key = name.empty() ? scope_key(scope, "<anonymous-enum>") : scope_key(scope, name);
    Id eid = none;
    if (!name.empty()) {
      std::unordered_map<std::string, Id>::iterator old = enum_entities_.find(key);
      if (old != enum_entities_.end()) eid = old->second;
      if (eid == none) {
        Id b = local_lookup(scope, name, true, false);
        if (b != none && bindings_[b].type < types_.size() && types_[bindings_[b].type].kind == NamedType &&
            entities_[types_[bindings_[b].type].entity].kind == EnumEntity)
          eid = types_[bindings_[b].type].entity;
      }
    }
    if (eid == none) {
      Entity e; e.kind = EnumEntity; e.name = name; e.key = key;
      e.scoped_enum = scoped; e.underlying = underlying;
      eid = new_entity(e); entities_[eid].type = named_type(eid);
      if (!name.empty()) enum_entities_[key] = eid;
    }
    Entity& e = entities_[eid];
    if (e.scoped_enum != scoped && (e.defined || scoped))
      throw std::runtime_error("incompatible enum declaration");
    if (has_underlying && e.complete && e.underlying != underlying)
      throw std::runtime_error("incompatible enum underlying type");
    if (!name.empty()) {
      Id b = local_lookup(scope, name, true, false);
      if ((b == none || bindings_[b].entity != eid) && raw_name.find("::") == std::string::npos) {
        Id tag = add_binding(scope, TypeBinding, name, e.type, eid, emit);
        enum_tag_bindings_[node] = tag;
      } else if (b != none && bindings_[b].entity == eid && emit) {
        set_binding_output(b, true); enum_tag_bindings_[node] = b;
      }
    }
    if (!name.empty() && !scoped && !definition && !has_underlying && !e.complete)
      throw std::runtime_error("opaque unscoped enum requires an underlying type");
    if (scoped && e.scope == none) e.scope = new_scope(EnumScope, name, scope, eid);
    enum_node_types_[node] = e.type;
    if ((scoped || has_underlying) && !definition) e.complete = true;
    if (definition && process_values) {
      const bool qualified_scoped_definition = scoped && raw_name.find("::") != std::string::npos;
      add_enum_values(node, scope, eid, !qualified_scoped_definition);
      if (qualified_scoped_definition) add_qualified_enum_output(raw_name, eid);
    }
    return e.type;
  }

  Id process_enum_forward(Id node, Id scope, bool emit)
  { return process_enum(node, scope, emit, false); }

  void add_enum_values(Id node, Id surrounding_scope, Id eid, bool output = true)
  {
    Entity& e = entities_[eid];
    if (e.defined) return;
    const std::vector<Id> members = children(node);
    bool has_enumerator = false;
    for (std::size_t i = 0; i < members.size(); ++i)
      if (ast_.nodes[members[i]].kind == NEnumerator) has_enumerator = true;
    if (!has_enumerator) return;
    e.complete = true;
    e.defined = true;
    const Id binding_scope = e.scoped_enum ? e.scope : surrounding_scope;
    long long next = 0;
    for (std::size_t i = 0; i < members.size(); ++i) {
      const Id item = members[i];
      if (ast_.nodes[item].kind != NEnumerator) continue;
      const Id init = ast_.nodes[item].first_child;
      if (init != none) {
        const ConstResult value = eval(init, binding_scope);
        if (!value.valid) throw std::runtime_error("invalid enumerator constant expression");
        next = value.value;
      }
      const Id b = add_binding(binding_scope, EnumeratorBinding, text(item), e.type, eid, output);
      bindings_[b].has_value = true;
      bindings_[b].value = next;
      if (next == LLONG_MAX) throw std::runtime_error("enumerator value overflow");
      ++next;
    }
  }

  void add_qualified_enum_output(const std::string& raw_name, Id eid)
  {
    if (qualified_enum_output_bindings_.find(eid) != qualified_enum_output_bindings_.end()) return;
    std::string qualified_name = raw_name;
    while (qualified_name.compare(0, 2, "::") == 0) qualified_name.erase(0, 2);
    const Id binding = add_binding(global_, TypeBinding, qualified_name, entities_[eid].type, eid, true);
    bindings_[binding].type_override = "enum class " + qualified_name;
    qualified_enum_output_bindings_[eid] = binding;
    const Id view = new_scope(EnumScope, qualified_name, global_, eid);
    qualified_enum_output_scopes_[eid] = view;
    const Id semantic_scope = entities_[eid].scope;
    if (semantic_scope == none) return;
    const std::vector<Id> values = scopes_[semantic_scope].bindings;
    for (std::size_t i = 0; i < values.size(); ++i) {
      const Binding& source = bindings_[values[i]];
      if (source.kind != EnumeratorBinding) continue;
      const Id copy = add_binding(view, EnumeratorBinding, source.name, source.type,
                                  source.entity, true);
      bindings_[copy].has_value = source.has_value;
      bindings_[copy].value = source.value;
      bindings_[copy].type_override = "enum class " + qualified_name;
    }
  }

  Id qualified_declaration_scope(Id starting, const std::string& qualified_name,
                                 std::string& leaf)
  {
    std::vector<std::string> parts = split_qualified(qualified_name);
    if (parts.empty()) { leaf = qualified_name; return starting; }
    leaf = parts.back();
    if (parts.size() == 1) return starting;
    Id scope = is_absolute_name(qualified_name) ? global_ : starting;
    Id b = lookup(scope, parts[0], false, true);
    if (b == none) b = lookup(scope, parts[0], true, false);
    if (b == none) throw std::runtime_error("unknown qualified declaration scope");
    for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
      scope = namespace_scope_for_binding(b);
      if (scope == none) throw std::runtime_error("qualified name does not name a scope");
      b = local_lookup(scope, parts[i], false, true);
      if (b == none) b = local_lookup(scope, parts[i], true, false);
      if (b == none) throw std::runtime_error("unknown qualified declaration scope");
    }
    scope = namespace_scope_for_binding(b);
    if (scope == none) throw std::runtime_error("qualified name does not name a scope");
    bool encloses = false;
    for (Id s = scope; s != none; s = scopes_[s].parent)
      if (s == starting) { encloses = true; break; }
    if (!encloses) throw std::runtime_error("qualified definition is outside an enclosing scope");
    return scope;
  }

  Id init_expression(Id init) const
  {
    if (init == none) return none;
    const NodeKind k = ast_.nodes[init].kind;
    if (k != NInitializer && k != NParenInitializer && k != NBracedInitList && k != NDefaultArgument)
      return init;
    for (Id c = ast_.nodes[init].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      Id result = init_expression(c); if (result != none) return result;
    }
    return none;
  }

  Id reidentify_anonymous(Id type, Id scope, const std::string& name, bool output)
  {
    if (type >= types_.size() || types_[type].kind != NamedType) return type;
    const Id eid = types_[type].entity;
    if (eid >= entities_.size() || !entities_[eid].name.empty()) return type;
    entities_[eid].name = name;
    entities_[eid].key = scope_key(scope, name);
    if (entities_[eid].scope != none) scopes_[entities_[eid].scope].name = name;
    BindingKind kind = TypeBinding;
    add_or_find_binding(scope, kind, name, type, eid, output);
    if (entities_[eid].kind == EnumEntity) enum_entities_[entities_[eid].key] = eid;
    else if (entities_[eid].kind == ClassEntity) class_entities_[entities_[eid].key] = eid;
    return type;
  }

  Id process_simple_declaration(Id node, Id scope, bool emit, bool predeclare)
  {
    Id seq = ast_.nodes[node].first_child;
    if (seq == none || ast_.nodes[seq].kind != NDeclSpecifierSeq) return none;
    Id list = ast_.nodes[seq].next_sibling;
    Id first_item = list == none || ast_.nodes[list].kind != NInitDeclaratorList ? none : ast_.nodes[list].first_child;
    Id first_declarator = first_item == none ? none : ast_.nodes[first_item].first_child;
    const std::string first_name = declared_name(first_declarator);
    for (Id spec = ast_.nodes[seq].first_child; spec != none; spec = ast_.nodes[spec].next_sibling) {
      if (ast_.nodes[spec].kind == NClassSpecifier) {
        const std::string n = ast_.nodes[spec].composite == none ? std::string() : text(spec);
        const std::vector<Id> cs = children(spec);
        for (std::size_t i = 0; i < cs.size(); ++i) {
          if (ast_.nodes[cs[i]].kind == NClassKey) {
            class_key_by_declaration_[spec] = text(cs[i]);
            if (text(cs[i]) == "union" && n.empty() && first_name.empty() &&
                scopes_[scope].kind == NamespaceScope) anonymous_union_nodes_.insert(spec);
          }
        }
        if (n.empty() && !first_name.empty()) pending_anonymous_names_[spec] = first_name;
      } else if (ast_.nodes[spec].kind == NEnumSpecifier) {
        enum_key_by_declaration_[spec] = std::string();
        if (ast_.nodes[spec].composite == none && !first_name.empty()) pending_anonymous_names_[spec] = first_name;
      }
    }
    const bool is_typedef = has_specifier(seq, "typedef");
    const bool is_constexpr = has_specifier(seq, "constexpr");
    const bool is_extern = has_specifier(seq, "extern");
    Id base = type_from_decl_spec(seq, scope, emit);
    bool anonymous_union = false;
    for (Id spec = ast_.nodes[seq].first_child; spec != none; spec = ast_.nodes[spec].next_sibling)
      if (ast_.nodes[spec].kind == NClassSpecifier && anonymous_union_nodes_.find(spec) != anonymous_union_nodes_.end())
        anonymous_union = true;
    if (anonymous_union && !has_specifier(seq, "static"))
      throw std::runtime_error("namespace anonymous union requires static");
    if (list == none || ast_.nodes[list].kind != NInitDeclaratorList) {
      if (anonymous_union && !predeclare) inject_anonymous_union(base, scope);
      return base;
    }
    for (Id item = ast_.nodes[list].first_child; item != none; item = ast_.nodes[item].next_sibling) {
      Id declarator = ast_.nodes[item].first_child;
      if (declarator == none || ast_.nodes[declarator].kind != NDeclarator) continue;
      const std::string raw_name = declared_name(declarator);
      if (raw_name.empty()) continue;
      std::string name = raw_name;
      Id target_scope = qualified_declaration_scope(scope, raw_name, name);
      reject_namespace_name_conflict(target_scope, name);
      Id type = build_declarator(declarator, base, target_scope);
      if (is_constexpr && types_[type].kind != FunctionType)
        type = qualified(type, true, false);
      const Id init = ast_.nodes[declarator].next_sibling;
      const bool has_initializer = init != none && ast_.nodes[init].kind == NInitializer;

      if (types_[type].kind == NamedType && entities_[types_[type].entity].name.empty())
        reidentify_anonymous(type, target_scope, name, emit);
      if (is_typedef) {
        Id b = add_or_find_binding(target_scope, AliasBinding, name, type, none, emit);
        if (predeclare) bindings_[b].output = false;
        continue;
      }

      if (types_[type].kind == FundamentalType && types_[type].atom == "void")
        throw std::runtime_error("object has incomplete void type");
      if ((types_[type].kind == LvalueReferenceType || types_[type].kind == RvalueReferenceType) &&
          !has_initializer && !is_extern && scopes_[target_scope].kind != ClassScope)
        throw std::runtime_error("reference object is not initialized");
      if (types_[type].kind == ArrayType && types_[type].bound == 0)
        incomplete_arrays_.push_back(std::make_pair(target_scope, name_id(name)));
      BindingKind kind = types_[type].kind == FunctionType ? FunctionBinding : VariableBinding;
      Id b = add_binding(target_scope, kind, name, type, none, emit);
      bindings_[b].constexpr_object = is_constexpr;
      bindings_[b].static_storage = has_specifier(seq, "static");
      if (has_initializer && (is_constexpr || is_const_type(type) ||
          types_[type].kind == LvalueReferenceType || types_[type].kind == RvalueReferenceType)) {
        Id expr = init_expression(init);
        if (expr != none) {
          ConstResult value = eval(expr, target_scope);
          if (value.valid && (is_constexpr || is_const_type(type) ||
              types_[type].kind == LvalueReferenceType || types_[type].kind == RvalueReferenceType)) {
            bindings_[b].has_value = true; bindings_[b].value = value.value;
          }
        }
      }
      if (predeclare) bindings_[b].output = false;
    }
    return base;
  }

  void inject_anonymous_union(Id type, Id scope)
  {
    if (type >= types_.size() || types_[type].kind != NamedType) return;
    const Id e = types_[type].entity;
    if (e >= entities_.size() || entities_[e].kind != ClassEntity || entities_[e].scope == none) return;
    const Scope& members = scopes_[entities_[e].scope];
    for (std::size_t i = 0; i < members.bindings.size(); ++i) {
      const Binding source = bindings_[members.bindings[i]];
      if (source.kind != VariableBinding) continue;
      Id b = add_binding(scope, VariableBinding, source.name, source.type, source.entity, true);
      bindings_[b].has_value = source.has_value; bindings_[b].value = source.value;
      bindings_[b].constexpr_object = source.constexpr_object;
    }
  }

  bool is_const_type(Id type) const
  {
    if (type >= types_.size()) return false;
    if (types_[type].kind == QualifiedType)
      return types_[type].atom.find('c') != std::string::npos;
    if (types_[type].kind == ArrayType) return is_const_type(types_[type].base);
    return false;
  }

  Id closest_parameter_clause(Id declarator, Id scope)
  {
    std::vector<DeclaratorAction> path;
    declarator_path(declarator, path, scope);
    for (std::size_t i = 0; i < path.size(); ++i)
      if (path[i].kind == NParameterClause) return path[i].node;
    return none;
  }

  void add_function_parameters(Id clause, Id fn_scope)
  {
    if (clause == none) return;
    for (Id p = ast_.nodes[clause].first_child; p != none; p = ast_.nodes[p].next_sibling) {
      if (ast_.nodes[p].kind != NParameterDeclaration) continue;
      Id seq = ast_.nodes[p].first_child;
      Id declarator = seq == none ? none : ast_.nodes[seq].next_sibling;
      Id type = type_from_decl_spec(seq, fn_scope);
      std::string name;
      if (declarator != none && (ast_.nodes[declarator].kind == NDeclarator ||
          ast_.nodes[declarator].kind == NAbstractDeclarator)) {
        name = declared_name(declarator);
        type = build_declarator(declarator, type, fn_scope);
      }
      add_binding(fn_scope, ParameterBinding, name, type, none, true);
    }
  }

  void process_function_definition(Id node, Id scope)
  {
    Id seq = ast_.nodes[node].first_child;
    Id declarator = seq == none ? none : ast_.nodes[seq].next_sibling;
    if (seq == none || declarator == none) return;
    const Id base = type_from_decl_spec(seq, scope);
    const std::string raw_name = declared_name(declarator);
    std::string name = raw_name;
    const Id target_scope = qualified_declaration_scope(scope, raw_name, name);
    const Id type = build_declarator(declarator, base, target_scope);
    if (types_[type].kind != FunctionType) throw std::runtime_error("function definition declarator is not a function");
    reject_namespace_name_conflict(target_scope, name);
    add_binding(target_scope, FunctionBinding, name, type, none, true);
    const Id fn_scope = new_scope(FunctionScope, name, target_scope);
    const Id clause = closest_parameter_clause(declarator, target_scope);
    add_function_parameters(clause, fn_scope);
    Id body = ast_.nodes[declarator].next_sibling;
    while (body != none && ast_.nodes[body].kind != NCompoundStatement)
      body = ast_.nodes[body].next_sibling;
    if (body != none) process_compound(body, fn_scope);
  }

  void process_compound(Id node, Id parent)
  {
    const Id block = new_scope(BlockScope, std::string(), parent);
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      const NodeKind k = ast_.nodes[c].kind;
      if (k == NSimpleDeclaration || k == NFunctionDefinition || k == NNamespaceDefinition ||
          k == NUsingDirective || k == NUsingDeclaration || k == NAliasDeclaration ||
          k == NStaticAssertDeclaration || k == NClassSpecifier || k == NClassForwardDeclaration ||
          k == NEnumSpecifier || k == NTemplateDeclaration) process(c, block);
      else if (k == NCompoundStatement) process_compound(c, block);
      else process_statement_scopes(c, block);
    }
  }

  void process_statement_scopes(Id node, Id parent)
  {
    if (node == none) return;
    const NodeKind k = ast_.nodes[node].kind;
    if (k == NCompoundStatement) { process_compound(node, parent); return; }
    if (k == NCallExpression) process_constructor_call(node, parent);
    if (k == NSimpleDeclaration || k == NStaticAssertDeclaration || k == NUsingDeclaration ||
        k == NAliasDeclaration) { process(node, parent); return; }
    for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
      process_statement_scopes(c, parent);
  }

  void process_constructor_call(Id node, Id scope)
  {
    const Id callee = ast_.nodes[node].first_child;
    if (callee == none || (ast_.nodes[callee].kind != NIdExpression &&
        ast_.nodes[callee].kind != NIdentifier)) return;
    const Id binding = resolve_name_binding(scope, text(callee), true, false);
    if (binding == none || !is_type_binding(bindings_[binding].kind) ||
        bindings_[binding].type >= types_.size() ||
        types_[bindings_[binding].type].kind != NamedType) return;
    const Id entity = types_[bindings_[binding].type].entity;
    if (entity >= entities_.size() || entities_[entity].kind != ClassEntity ||
        entities_[entity].scope == none) return;
    const Id class_scope = entities_[entity].scope;
    for (std::size_t i = 0; i < scopes_[class_scope].bindings.size(); ++i) {
      const Binding& member = bindings_[scopes_[class_scope].bindings[i]];
      if (member.kind == FunctionBinding && member.name == entities_[entity].name) return;
    }
    for (std::size_t i = 0; i < scopes_[class_scope].children.size(); ++i) {
      const Scope& child = scopes_[scopes_[class_scope].children[i]];
      if (child.kind == FunctionScope && child.name == entities_[entity].name) return;
    }
    new_scope(FunctionScope, entities_[entity].name, class_scope);
  }

  void process_namespace(Id node, Id parent)
  {
    std::string name = text(node);
    const std::vector<Id> body = children(node);
    bool is_inline = false;
    for (std::size_t i = 0; i < body.size(); ++i)
      if (ast_.nodes[body[i]].kind == NInlineMarker) is_inline = true;
    Id ns_scope = none, eid = none;
    if (name.empty()) {
      std::unordered_map<Id, Id>::iterator prior = unnamed_namespace_by_parent_.find(parent);
      if (prior == unnamed_namespace_by_parent_.end()) {
        Entity e; e.kind = NamespaceEntity; e.name = "<unnamed>"; e.key = "<anonymous-namespace>";
        eid = new_entity(e); ns_scope = new_scope(NamespaceScope, "<unnamed>", parent, eid);
        entities_[eid].scope = ns_scope; unnamed_namespace_ = ns_scope;
        unnamed_namespace_by_parent_[parent] = ns_scope;
        scopes_[parent].using_directives.push_back(ns_scope);
      } else { ns_scope = prior->second; eid = scopes_[ns_scope].entity; }
    } else {
      Id any = local_lookup(parent, name, false, false);
      if (any != none) {
        if (bindings_[any].kind != NamespaceBinding || bindings_[any].namespace_alias ||
            bindings_[any].entity >= entities_.size() ||
            entities_[bindings_[any].entity].kind != NamespaceEntity)
          throw std::runtime_error("namespace definition conflicts with a prior binding or alias");
        eid = bindings_[any].entity; ns_scope = entities_[eid].scope;
      } else {
        const std::string key = scope_key(parent, name);
        std::unordered_map<std::string, Id>::iterator prior = named_namespace_entities_.find(key);
        if (prior != named_namespace_entities_.end()) eid = prior->second;
        else {
          Entity e; e.kind = NamespaceEntity; e.name = name; e.key = key;
          eid = new_entity(e); named_namespace_entities_[key] = eid;
        }
        if (entities_[eid].scope == none) {
          ns_scope = new_scope(NamespaceScope, name, parent, eid); entities_[eid].scope = ns_scope;
        } else ns_scope = entities_[eid].scope;
        add_or_find_binding(parent, NamespaceBinding, name, none, eid, false);
      }
    }
    scopes_[ns_scope].inline_namespace = scopes_[ns_scope].inline_namespace || is_inline;
    for (std::size_t i = 0; i < body.size(); ++i)
      if (ast_.nodes[body[i]].kind != NInlineMarker) process(body[i], ns_scope);
  }

  void process_namespace_alias(Id node, Id scope)
  {
    const std::string alias = text(node);
    Id target = ast_.nodes[node].first_child;
    if (target == none) throw std::runtime_error("namespace alias has no target");
    const Id b = resolve_name_binding(scope, text(target), false, true);
    if (b == none) throw std::runtime_error("namespace alias target is not a namespace");
    const Id eid = bindings_[b].entity;
    Id old = local_lookup(scope, alias, false, false);
    if (old != none) {
      if (bindings_[old].kind != NamespaceBinding || bindings_[old].entity != eid)
        throw std::runtime_error("incompatible namespace alias redeclaration");
      return;
    }
    add_binding(scope, NamespaceBinding, alias, none, eid, false);
    bindings_.back().namespace_alias = true;
  }

  void process_using_directive(Id node, Id scope)
  {
    Id target = ast_.nodes[node].first_child;
    if (target == none) throw std::runtime_error("using directive without target");
    Id b = resolve_name_binding(scope, text(target), false, true);
    if (b == none) throw std::runtime_error("using directive target is not a namespace");
    Id nominated = entities_[bindings_[b].entity].scope;
    if (std::find(scopes_[scope].using_directives.begin(), scopes_[scope].using_directives.end(), nominated) ==
        scopes_[scope].using_directives.end()) scopes_[scope].using_directives.push_back(nominated);
  }

  void process_using_declaration(Id node, Id scope)
  {
    Id target_node = ast_.nodes[node].first_child;
    if (target_node == none) throw std::runtime_error("using declaration without target");
    const std::string target = text(target_node);
    if (target.find('<') != std::string::npos) throw std::runtime_error("using declaration template-id is unsupported");
    Id b = resolve_name_binding(scope, target, false, false);
    if (b == none) throw std::runtime_error("using declaration target not found");
    const Binding source = bindings_[b];
    const std::size_t sep = target.rfind("::");
    const std::string name = sep == std::string::npos ? target : target.substr(sep + 2);
    if (is_type_binding(source.kind)) {
      add_binding(scope, source.kind, name, source.type, source.entity, true);
    } else if (source.kind == EnumeratorBinding) {
      Id imported = add_binding(scope, EnumeratorBinding, name, source.type, source.entity, true);
      bindings_[imported].has_value = source.has_value; bindings_[imported].value = source.value;
    } else if (source.kind == FunctionBinding) {
      add_binding(scope, FunctionBinding, name, source.type, source.entity, true);
    } else if (source.kind == VariableBinding || source.kind == ParameterBinding) {
      Id imported = add_binding(scope, VariableBinding, name, source.type, source.entity, true);
      bindings_[imported].has_value = source.has_value; bindings_[imported].value = source.value;
      bindings_[imported].constexpr_object = source.constexpr_object;
    } else throw std::runtime_error("unsupported using declaration target");
  }

  void process_alias_declaration(Id node, Id scope, bool emit)
  {
    const std::string name = text(node);
    Id type_node = ast_.nodes[node].first_child;
    if (type_node == none) throw std::runtime_error("alias declaration has no type");
    const Id type = type_id(type_node, scope);
    add_or_find_binding(scope, AliasBinding, name, type, none, emit);
  }

  void process_template(Id node, Id parent)
  {
    Id clause = ast_.nodes[node].first_child;
    Id declaration = clause == none ? none : ast_.nodes[clause].next_sibling;
    const Id templ_scope = new_scope(TemplateScope, std::string(), parent);
    if (clause != none) {
      Id list = ast_.nodes[clause].first_child;
      for (Id p = list == none ? none : ast_.nodes[list].first_child; p != none; p = ast_.nodes[p].next_sibling) {
        const std::string name = template_parameter_name(p);
        if (name.empty()) continue;
        const NodeKind k = ast_.nodes[p].kind;
        std::string spelling;
        BindingKind binding_kind = TypeBinding;
        if (k == NTypeParameter) {
          if (!children(p).empty() && ast_.nodes[ast_.nodes[p].first_child].kind == NTemplateTemplateParameter)
            spelling = "template-parameter " + name;
          else spelling = "typename " + name;
        } else if (k == NNonTypeTemplateParameter) {
          Id seq = ast_.nodes[p].first_child;
          Id type = seq == none ? fundamental("int") : type_from_decl_spec(seq, templ_scope);
          binding_kind = VariableBinding;
          add_binding(templ_scope, binding_kind, name, type, none, true);
          continue;
        }
        const Id t = template_parameter_type(spelling);
        add_binding(templ_scope, binding_kind, name, t, none, true);
      }
    }
    if (declaration != none) process(declaration, templ_scope);
  }

  std::string template_parameter_name(Id node) const
  {
    if (node == none) return std::string();
    if (ast_.nodes[node].kind == NTypeParameter || ast_.nodes[node].kind == NNonTypeTemplateParameter) {
      for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
        if (ast_.nodes[c].kind == NIdentifier) return text(c);
      return std::string();
    }
    return std::string();
  }

  void process_static_assert(Id node, Id scope)
  {
    Id expr = ast_.nodes[node].first_child;
    if (expr == none) throw std::runtime_error("empty static_assert");
    ConstResult value = eval(expr, scope);
    if (!value.valid || value.value == 0) throw std::runtime_error("static_assert failed or is not constant");
  }

  static bool integral_fundamental(const std::string& name)
  {
    return name == "bool" || name == "char" || name == "signed char" ||
        name == "unsigned char" || name == "short int" || name == "unsigned short int" ||
        name == "int" || name == "unsigned int" || name == "long int" ||
        name == "unsigned long int" || name == "long long int" ||
        name == "unsigned long long int" || name == "wchar_t" ||
        name == "char16_t" || name == "char32_t";
  }

  bool is_integral(Id type) const
  {
    if (type >= types_.size()) return false;
    const Type& t = types_[type];
    if (t.kind == QualifiedType) return is_integral(t.base);
    if (t.kind == FundamentalType) return integral_fundamental(t.atom);
    return t.kind == NamedType && entities_[t.entity].kind == EnumEntity;
  }

  bool is_scoped_enum(Id type) const
  {
    if (type >= types_.size()) return false;
    const Type& t = types_[type];
    if (t.kind == QualifiedType) return is_scoped_enum(t.base);
    return t.kind == NamedType && entities_[t.entity].kind == EnumEntity &&
        entities_[t.entity].scoped_enum;
  }

  Id strip_qualified(Id type) const
  { return type < types_.size() && types_[type].kind == QualifiedType ? types_[type].base : type; }

  ConstResult literal_value(Id node)
  {
    std::string s = text(node);
    const Id int_type = fundamental("int");
    if (s == "true") return ConstResult(true, 1, fundamental("bool"));
    if (s == "false") return ConstResult(true, 0, fundamental("bool"));
    if (s == "nullptr") return ConstResult(false, 0, fundamental("nullptr_t"));
    std::size_t quote = s.find('\'');
    if (quote != std::string::npos && s.size() > quote + 1) {
      std::size_t end_quote = s.find('\'', quote + 1);
      if (end_quote == std::string::npos || end_quote == quote + 1) return ConstResult();
      unsigned char value = 0;
      if (s[quote + 1] == '\\') {
        if (quote + 2 >= end_quote) return ConstResult();
        const char esc = s[quote + 2];
        switch (esc) {
        case 'n': value = '\n'; break; case 'r': value = '\r'; break;
        case 't': value = '\t'; break; case '0': value = 0; break;
        case '\\': value = '\\'; break; case '\'': value = '\''; break;
        case '"': value = '"'; break;
        default: value = static_cast<unsigned char>(esc); break;
        }
      } else value = static_cast<unsigned char>(s[quote + 1]);
      return ConstResult(true, value, fundamental("int"));
    }
    std::size_t suffix = s.size();
    while (suffix > 0 && (s[suffix - 1] == 'u' || s[suffix - 1] == 'U' ||
           s[suffix - 1] == 'l' || s[suffix - 1] == 'L')) --suffix;
    if (suffix == 0) return ConstResult();
    const std::string digits = s.substr(0, suffix);
    errno = 0;
    char* end = 0;
    const unsigned long long value = std::strtoull(digits.c_str(), &end, 0);
    if (errno == ERANGE || end == digits.c_str() || *end || value > static_cast<unsigned long long>(LLONG_MAX))
      return ConstResult();
    Id type = int_type;
    if (s.find_first_of("lL") != std::string::npos) type = fundamental("long int");
    if (s.find_first_of("uU") != std::string::npos) type = fundamental("unsigned int");
    return ConstResult(true, static_cast<long long>(value), type);
  }

  bool checked_binary(const std::string& op, long long a, long long b, long long& result) const
  {
    if (op == "+") { __int128 n = static_cast<__int128>(a) + b; if (n > LLONG_MAX || n < LLONG_MIN) return false; result = static_cast<long long>(n); return true; }
    if (op == "-") { __int128 n = static_cast<__int128>(a) - b; if (n > LLONG_MAX || n < LLONG_MIN) return false; result = static_cast<long long>(n); return true; }
    if (op == "*") { __int128 n = static_cast<__int128>(a) * b; if (n > LLONG_MAX || n < LLONG_MIN) return false; result = static_cast<long long>(n); return true; }
    if (op == "/") { if (!b || (a == LLONG_MIN && b == -1)) return false; result = a / b; return true; }
    if (op == "%") { if (!b || (a == LLONG_MIN && b == -1)) return false; result = a % b; return true; }
    if (op == "<<") { if (b < 0 || b >= 63) return false; __int128 n = static_cast<__int128>(a) << b; if (n > LLONG_MAX || n < LLONG_MIN) return false; result = static_cast<long long>(n); return true; }
    if (op == ">>") { if (b < 0 || b >= 63) return false; result = a >> b; return true; }
    if (op == "&") { result = a & b; return true; }
    if (op == "|") { result = a | b; return true; }
    if (op == "^") { result = a ^ b; return true; }
    if (op == "==") { result = a == b; return true; }
    if (op == "!=") { result = a != b; return true; }
    if (op == "<") { result = a < b; return true; }
    if (op == ">") { result = a > b; return true; }
    if (op == "<=") { result = a <= b; return true; }
    if (op == ">=") { result = a >= b; return true; }
    if (op == "&&") { result = a && b; return true; }
    if (op == "||") { result = a || b; return true; }
    return false;
  }

  ConstResult eval(Id node, Id scope)
  {
    if (node == none) return ConstResult();
    const NodeKind k = ast_.nodes[node].kind;
    if (k == NLiteral || k == NTaggedLiteral || k == NKeywordLiteral)
      return literal_value(node);
    if (k == NIdExpression || k == NIdentifier) {
      const std::string name = text(node);
      const Id b = resolve_name_binding(scope, name, false, false);
      if (b == none) return ConstResult();
      const Binding& binding = bindings_[b];
      if (binding.kind == EnumeratorBinding && binding.has_value)
        return ConstResult(true, binding.value, binding.type, false);
      if ((binding.kind == VariableBinding || binding.kind == ParameterBinding) && binding.has_value) {
        Id expression_type = binding.type;
        bool lvalue = true;
        if (types_[expression_type].kind == LvalueReferenceType || types_[expression_type].kind == RvalueReferenceType)
          expression_type = types_[expression_type].base;
        return ConstResult(true, binding.value, expression_type, lvalue);
      }
      Id expression_type = binding.type;
      if (expression_type < types_.size() &&
          (types_[expression_type].kind == LvalueReferenceType || types_[expression_type].kind == RvalueReferenceType))
        expression_type = types_[expression_type].base;
      return ConstResult(false, 0, expression_type, binding.kind == VariableBinding || binding.kind == ParameterBinding);
    }
    if (k == NParenthesizedExpression) {
      ConstResult r = eval(ast_.nodes[node].first_child, scope); return r;
    }
    if (k == NUnaryExpression) {
      Id operand = ast_.nodes[node].first_child;
      ConstResult a = eval(operand, scope);
      const std::string op = text(node);
      if (!a.valid) return a;
      long long value = a.value;
      if (op == "+") return a;
      if (op == "-") { if (value == LLONG_MIN) return ConstResult(); value = -value; }
      else if (op == "!") value = !value;
      else if (op == "~") value = ~value;
      else return ConstResult(false, 0, a.type);
      return ConstResult(true, value, op == "!" ? fundamental("int") : a.type);
    }
    if (k == NBinaryExpression) {
      Id left_node = ast_.nodes[node].first_child;
      Id right_node = left_node == none ? none : ast_.nodes[left_node].next_sibling;
      ConstResult a = eval(left_node, scope);
      const std::string op = text(node);
      if (op == "&&" && a.valid && a.value == 0)
        return ConstResult(true, 0, fundamental("int"));
      if (op == "||" && a.valid && a.value != 0)
        return ConstResult(true, 1, fundamental("int"));
      ConstResult b = eval(right_node, scope);
      if (!a.valid || !b.valid) return ConstResult();
      if ((is_scoped_enum(a.type) && strip_qualified(a.type) != strip_qualified(b.type)) ||
          (is_scoped_enum(b.type) && strip_qualified(a.type) != strip_qualified(b.type)))
        return ConstResult();
      if ((is_scoped_enum(a.type) || is_scoped_enum(b.type)) &&
          op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=")
        return ConstResult();
      long long value = 0;
      if (!checked_binary(op, a.value, b.value, value)) return ConstResult();
      const bool comparison = op == "==" || op == "!=" || op == "<" || op == ">" ||
          op == "<=" || op == ">=" || op == "&&" || op == "||";
      return ConstResult(true, value, comparison ? fundamental("int") : a.type);
    }
    if (k == NConditionalExpression) {
      Id cond = ast_.nodes[node].first_child;
      Id yes = cond == none ? none : ast_.nodes[cond].next_sibling;
      Id no = yes == none ? none : ast_.nodes[yes].next_sibling;
      ConstResult c = eval(cond, scope);
      if (!c.valid) return ConstResult();
      return eval(c.value ? yes : no, scope);
    }
    if (k == NSizeofExpression || k == NTypeTraitExpression) {
      Id operand = ast_.nodes[node].first_child;
      if (operand == none) return ConstResult();
      Id type = none;
      if (ast_.nodes[operand].kind == NTypeId) type = type_id(operand, scope);
      else {
        ConstResult value = eval(operand, scope);
        type = value.type;
      }
      bool alignment = k == NTypeTraitExpression && text(node) == "alignof";
      long long size = 0, align = 0;
      if (type == none || !type_layout(type, size, align)) return ConstResult();
      return ConstResult(true, alignment ? align : size, fundamental("unsigned long int"));
    }
    if (k == NCastExpression) {
      Id target = ast_.nodes[node].first_child;
      Id value_node = target == none ? none : ast_.nodes[target].next_sibling;
      if (target == none || value_node == none) return ConstResult();
      Id type = type_id(target, scope);
      ConstResult value = eval(value_node, scope);
      if (!value.valid || !is_integral(type)) return ConstResult();
      return ConstResult(true, value.value, type);
    }
    return ConstResult();
  }

  Id decltype_type(Id expression, Id scope)
  {
    if (expression == none) throw std::runtime_error("empty decltype operand");
    if (ast_.nodes[expression].kind == NIdExpression || ast_.nodes[expression].kind == NIdentifier) {
      Id b = resolve_name_binding(scope, text(expression), false, false);
      if (b == none) throw std::runtime_error("unknown decltype name");
      return bindings_[b].type;
    }
    ConstResult value = eval(expression, scope);
    if (value.type == none) throw std::runtime_error("unsupported decltype expression");
    if (value.lvalue) return unary_type(LvalueReferenceType, value.type);
    return value.type;
  }

  bool type_layout(Id type, long long& size, long long& alignment) const
  {
    if (type >= types_.size()) return false;
    const Type& t = types_[type];
    if (t.kind == QualifiedType) return type_layout(t.base, size, alignment);
    if (t.kind == PointerType || t.kind == LvalueReferenceType || t.kind == RvalueReferenceType) {
      size = alignment = 8; return true;
    }
    if (t.kind == ArrayType) {
      long long elem_size = 0, elem_align = 0;
      if (!t.bound || !type_layout(t.base, elem_size, elem_align) ||
          elem_size > LLONG_MAX / t.bound) return false;
      size = elem_size * t.bound; alignment = elem_align; return true;
    }
    if (t.kind == FundamentalType) {
      const std::string& n = t.atom;
      if (n == "void" || n == "nullptr_t") return false;
      if (n == "bool" || n == "char" || n == "signed char" || n == "unsigned char" || n == "char8_t") size = 1;
      else if (n == "short int" || n == "unsigned short int" || n == "char16_t") size = 2;
      else if (n == "int" || n == "unsigned int" || n == "float" || n == "wchar_t" || n == "char32_t") size = 4;
      else if (n == "long int" || n == "unsigned long int" || n == "long long int" ||
               n == "unsigned long long int" || n == "double") size = 8;
      else if (n == "long double") size = 16;
      else return false;
      alignment = size > 8 ? 16 : size; return true;
    }
    if (t.kind == NamedType) {
      const Entity& e = entities_[t.entity];
      if (e.kind == EnumEntity) return type_layout(e.underlying, size, alignment);
      if (!e.complete || e.scope == none) return false;
      long long offset = 0, max_size = 0, max_align = 1;
      const Scope& s = scopes_[e.scope];
      for (std::size_t i = 0; i < s.bindings.size(); ++i) {
        const Binding& b = bindings_[s.bindings[i]];
        if (b.kind != VariableBinding || b.static_storage) continue;
        long long field_size = 0, field_align = 0;
        if (!type_layout(b.type, field_size, field_align)) return false;
        if (field_align > max_align) max_align = field_align;
        if (e.is_union) { if (field_size > max_size) max_size = field_size; }
        else {
          const long long rem = offset % field_align;
          if (rem) offset += field_align - rem;
          if (offset > LLONG_MAX - field_size) return false;
          offset += field_size;
        }
      }
      size = e.is_union ? max_size : offset;
      if (size == 0) size = 1;
      if (size % max_align) size += max_align - size % max_align;
      alignment = max_align; return true;
    }
    return false;
  }

  void finish_array_completions()
  {
    for (std::size_t s = 0; s < scopes_.size(); ++s) {
      Scope& scope = scopes_[s];
      for (std::size_t i = 0; i < scope.bindings.size(); ++i) {
        Binding& candidate = bindings_[scope.bindings[i]];
        if (candidate.kind != VariableBinding || candidate.type >= types_.size() ||
            types_[candidate.type].kind != ArrayType) continue;
        Id complete = candidate.type;
        for (std::size_t j = 0; j < scope.bindings.size(); ++j) {
          const Binding& other = bindings_[scope.bindings[j]];
          if (other.name_id != candidate.name_id || other.type >= types_.size() ||
              types_[other.type].kind != ArrayType) continue;
          complete = merge_array_type(complete, other.type);
        }
        if (complete != none) {
          for (std::size_t j = 0; j < scope.bindings.size(); ++j)
            if (bindings_[scope.bindings[j]].name_id == candidate.name_id &&
                bindings_[scope.bindings[j]].kind == VariableBinding &&
                types_[bindings_[scope.bindings[j]].type].kind == ArrayType)
              bindings_[scope.bindings[j]].type = complete;
        }
      }
    }
  }

  Id merge_array_type(Id old_type, Id new_type)
  {
    if (old_type >= types_.size() || new_type >= types_.size() ||
        types_[old_type].kind != ArrayType || types_[new_type].kind != ArrayType) return none;
    const Type old = types_[old_type], next = types_[new_type];
    if (old.base != next.base && types_[old.base].kind != ArrayType) return none;
    Id elem = old.base;
    if (types_[old.base].kind == ArrayType && types_[next.base].kind == ArrayType) {
      Id merged = merge_array_type(old.base, next.base);
      if (merged != none) elem = merged;
    } else if (old.base != next.base) return none;
    if (old.bound && next.bound && old.bound != next.bound) return none;
    Type result = old; result.bound = old.bound ? old.bound : next.bound; result.base = elem;
    return intern(result);
  }

  void process_special_member(Id node, Id scope)
  {
    const std::string name = text(node);
    Id declarator = ast_.nodes[node].first_child;
    if (declarator == none) return;
    const Id clause = closest_parameter_clause(declarator, scope);
    Type fn; fn.kind = FunctionType; fn.base = fundamental("void");
    for (Id p = clause == none ? none : ast_.nodes[clause].first_child; p != none; p = ast_.nodes[p].next_sibling) {
      if (ast_.nodes[p].kind == NParameterPack) { fn.variadic = true; continue; }
      if (ast_.nodes[p].kind != NParameterDeclaration) continue;
      Id seq = ast_.nodes[p].first_child;
      Id d = seq == none ? none : ast_.nodes[seq].next_sibling;
      Id t = type_from_decl_spec(seq, scope);
      if (d != none && ast_.nodes[d].kind == NDeclarator) t = build_declarator(d, t, scope);
      fn.parameters.push_back(t);
    }
    if (fn.parameters.size() == 1 && types_[fn.parameters[0]].kind == FundamentalType &&
        types_[fn.parameters[0]].atom == "void") fn.parameters.clear();
    const Id type = intern(fn);
    add_binding(scope, FunctionBinding, name, type, none, true);
    const Id fscope = new_scope(FunctionScope, name, scope);
    add_function_parameters(clause, fscope);
    Id body = ast_.nodes[node].first_child;
    if (body == declarator) body = ast_.nodes[body].next_sibling;
    while (body != none && ast_.nodes[body].kind != NCompoundStatement)
      body = ast_.nodes[body].next_sibling;
    if (body != none) process_compound(body, fscope);
  }

  void process(Id node, Id scope)
  {
    if (node == none) return;
    const NodeKind k = ast_.nodes[node].kind;
    switch (k) {
    case NNamespaceDefinition: process_namespace(node, scope); break;
    case NNamespaceAliasDefinition: process_namespace_alias(node, scope); break;
    case NSimpleDeclaration: process_simple_declaration(node, scope, true, false); break;
    case NFunctionDefinition: process_function_definition(node, scope); break;
    case NClassSpecifier: process_class(node, scope, true, true); break;
    case NClassForwardDeclaration: process_class_forward(node, scope, true); break;
    case NEnumSpecifier: process_enum(node, scope, true, true); break;
    case NAliasDeclaration: process_alias_declaration(node, scope, true); break;
    case NUsingDirective: process_using_directive(node, scope); break;
    case NUsingDeclaration: process_using_declaration(node, scope); break;
    case NStaticAssertDeclaration: process_static_assert(node, scope); break;
    case NTemplateDeclaration: process_template(node, scope); break;
    case NCompoundStatement: process_compound(node, scope); break;
    case NLinkageSpecification:
    case NExplicitInstantiation:
      for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling) process(c, scope);
      break;
    case NSpecialMemberDeclaration:
    case NSpecialMemberDefinition:
      process_special_member(node, scope); break;
    default:
      for (Id c = ast_.nodes[node].first_child; c != none; c = ast_.nodes[c].next_sibling)
        if (ast_.nodes[c].kind == NSimpleDeclaration || ast_.nodes[c].kind == NStaticAssertDeclaration)
          process(c, scope);
      break;
    }
  }

  void print_scope(Id id, std::size_t depth, std::ostream& out) const
  {
    const Scope& s = scopes_[id];
    const std::string indent(depth * 2, ' ');
    out << indent << "scope ";
    switch (s.kind) {
    case NamespaceScope: out << "namespace " << s.name; break;
    case TemplateScope: out << "template-parameters"; break;
    case ClassScope: out << "class " << s.name; break;
    case EnumScope: out << "enum " << s.name; break;
    case FunctionScope: out << "function " << s.name; break;
    case BlockScope: out << "block"; break;
    }
    out << '\n';
    for (std::size_t i = 0; i < s.output_order.size(); ++i) {
      const Binding& b = bindings_[s.output_order[i]];
      if (!b.output) continue;
      out << indent << "  ";
      switch (b.kind) {
      case TypeBinding: out << "type "; break;
      case AliasBinding: out << "type-alias "; break;
      case EnumeratorBinding: out << "enumerator "; break;
      case FunctionBinding: out << "function "; break;
      case VariableBinding: out << "variable "; break;
      case ParameterBinding: out << "parameter "; break;
      case NamespaceBinding: case TemplateNameBinding: continue;
      }
      out << b.name << ' ';
      if (!b.type_override.empty()) out << b.type_override;
      else if (!b.display_override.empty()) out << b.display_override << ' ' << entities_[b.entity].name;
      else out << type_spelling(b.type);
      if (b.kind == EnumeratorBinding && b.has_value) out << ' ' << b.value;
      out << '\n';
    }
    for (std::size_t i = 0; i < s.children.size(); ++i)
      print_scope(s.children[i], depth + 1, out);
  }
};

}  // namespace

void EmitTypes(const std::vector<std::string>& inputs, const std::string& output)
{
  if (inputs.empty() || output.empty()) throw std::runtime_error("invalid --emit-types invocation");
  std::ofstream file(output.c_str(), std::ios::out | std::ios::trunc);
  if (!file) throw std::runtime_error("cannot open output file: " + output);
  std::ostream& out = file;
  out << inputs.size() << " translation units\n";
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    Ast ast = ParseTranslationUnit(inputs[i]);
    Analyzer analyzer(ast);
    analyzer.analyze();
    out << "start translation unit " << i + 1 << "\n";
    analyzer.print(out);
    out << "end translation unit\n";
  }
}

}  // namespace cppgm
