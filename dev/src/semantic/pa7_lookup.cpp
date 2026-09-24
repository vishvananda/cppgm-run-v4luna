#include "semantic/pa7_lookup.h"

#include <algorithm>
#include <utility>

namespace cppgm {
namespace pa7 {
namespace {

const pa6::Id invalid_id = pa6::InvalidId;

std::size_t slot_for(pa6::Id value, std::size_t capacity)
{ return std::hash<pa6::Id>()(value) & (capacity - 1); }

void insert_slot(std::vector<pa6::Id>& slots, pa6::Id value)
{
  std::size_t slot = slot_for(value, slots.size());
  while (slots[slot] != invalid_id) slot = (slot + 1) & (slots.size() - 1);
  slots[slot] = value;
}

bool base_name_matches(const pa6::SemanticUnit& unit, pa6::Id entity,
                       const std::vector<pa6::Id>& qualifier)
{
  if (entity >= unit.entity_count() || qualifier.empty()) return false;
  const pa6::Entity& base = unit.entity(entity);
  if (qualifier.size() == 1) return base.name_id == qualifier[0];
  std::vector<pa6::Id> path;
  pa6::Id scope = base.scope == pa6::InvalidId ? pa6::InvalidId
      : unit.scope(base.scope).parent;
  while (scope != pa6::InvalidId && scope != unit.global_scope()) {
    const pa6::ScopeRecord& record = unit.scope(scope);
    if (((record.kind == pa6::NamespaceScope && record.name != "<unnamed>") ||
         record.kind == pa6::ClassScope) && record.entity < unit.entity_count())
      path.push_back(unit.entity(record.entity).name_id);
    scope = record.parent;
  }
  std::reverse(path.begin(), path.end());
  path.push_back(base.name_id);
  return path == qualifier;
}

}  // namespace

FlatIdSet::FlatIdSet() : size_(0) {}

void FlatIdSet::grow()
{
  const std::size_t capacity = slots_.empty() ? 8 : slots_.size() * 2;
  std::vector<pa6::Id> grown(capacity, invalid_id);
  for (std::size_t i = 0; i < slots_.size(); ++i)
    if (slots_[i] != invalid_id) insert_slot(grown, slots_[i]);
  slots_.swap(grown);
}

bool FlatIdSet::contains(pa6::Id value) const
{
  if (value == invalid_id || slots_.empty()) return false;
  std::size_t slot = slot_for(value, slots_.size());
  while (slots_[slot] != invalid_id) {
    if (slots_[slot] == value) return true;
    slot = (slot + 1) & (slots_.size() - 1);
  }
  return false;
}

bool FlatIdSet::insert(pa6::Id value)
{
  if (value == invalid_id || contains(value)) return false;
  if (slots_.empty() || (size_ + 1) * 10 >= slots_.size() * 7) grow();
  insert_slot(slots_, value);
  ++size_;
  return true;
}

BindingCandidates::BindingCandidates()
    : indexed_(false), has_null_function_entity_(false)
{}

void SourceVisibility::Edges::add(pa6::Id target)
{
  if (targets.insert(target)) ordered.push_back(target);
}

SourceVisibility::SourceVisibility(const pa6::SemanticUnit& unit)
    : unit_(unit), visible_bindings_(unit.binding_count(), false)
{}

void SourceVisibility::mark_binding(pa6::Id binding)
{
  if (binding == pa6::InvalidId || binding >= unit_.binding_count()) return;
  if (binding >= visible_bindings_.size())
    visible_bindings_.resize(unit_.binding_count(), false);
  visible_bindings_[binding] = true;
}

bool SourceVisibility::is_visible(pa6::Id binding) const
{ return binding < visible_bindings_.size() && visible_bindings_[binding]; }

bool SourceVisibility::binding_visible(const pa6::SemanticUnit& unit,
                                       pa6::Id scope, pa6::Id binding) const
{
  if (scope >= unit.scope_count()) return false;
  const pa6::ScopeKind kind = unit.scope(scope).kind;
  return kind == pa6::ClassScope || is_visible(binding);
}

void SourceVisibility::mark_type_declarations(
    const pa6::SemanticUnit& unit, const Ast& ast, pa6::Id node)
{
  if (node == pa6::InvalidId || node >= ast.nodes.size()) return;
  const NodeKind kind = ast.nodes[node].kind;
  if (kind == NClassSpecifier || kind == NClassForwardDeclaration) {
    mark_binding(unit.binding_for_node(node));
    return;
  }
  if (kind == NEnumSpecifier) {
    mark_binding(unit.binding_for_node(node));
    for (pa6::Id child = ast.nodes[node].first_child;
         child != pa6::InvalidId; child = ast.nodes[child].next_sibling)
      if (ast.nodes[child].kind == NEnumerator)
        mark_binding(unit.binding_for_node(child));
    return;
  }
  for (pa6::Id child = ast.nodes[node].first_child;
       child != pa6::InvalidId; child = ast.nodes[child].next_sibling)
    mark_type_declarations(unit, ast, child);
}

void SourceVisibility::expose_namespace_definition(
    const pa6::SemanticUnit& unit, pa6::Id scope)
{
  if (scope == pa6::InvalidId || scope >= unit.scope_count()) return;
  const pa6::ScopeRecord& record = unit.scope(scope);
  const pa6::Id parent = record.parent;
  if (parent == pa6::InvalidId || parent >= unit.scope_count()) return;
  if (record.name == "<unnamed>") {
    add_using_directive(parent, scope);
  } else if (record.entity < unit.entity_count()) {
    const pa6::Id name = unit.entity(record.entity).name_id;
    const std::vector<pa6::Id> declarations = unit.lookup_bindings(
        parent, name, false, true);
    for (std::size_t i = 0; i < declarations.size(); ++i)
      if (unit.binding(declarations[i]).entity == record.entity)
        mark_binding(declarations[i]);
  }
  if (record.inline_namespace) add_inline_namespace(parent, scope);
}

void SourceVisibility::add_using_directive(pa6::Id scope, pa6::Id target)
{ using_directives_[scope].add(target); }

void SourceVisibility::add_inline_namespace(pa6::Id scope, pa6::Id target)
{ inline_namespaces_[scope].add(target); }

const std::vector<pa6::Id>& SourceVisibility::using_directives(pa6::Id scope) const
{
  static const std::vector<pa6::Id> empty;
  const std::unordered_map<pa6::Id, Edges>::const_iterator found =
      using_directives_.find(scope);
  return found == using_directives_.end() ? empty : found->second.ordered;
}

const std::vector<pa6::Id>& SourceVisibility::inline_namespaces(pa6::Id scope) const
{
  static const std::vector<pa6::Id> empty;
  const std::unordered_map<pa6::Id, Edges>::const_iterator found =
      inline_namespaces_.find(scope);
  return found == inline_namespaces_.end() ? empty : found->second.ordered;
}

void BindingCandidates::add(const pa6::SemanticUnit& unit, pa6::Id binding)
{
  if (binding == pa6::InvalidId) return;
  const pa6::Binding& candidate = unit.binding(binding);
  add(binding, candidate.kind, candidate.entity);
}

void BindingCandidates::add(pa6::Id binding, pa6::BindingKind kind,
                            pa6::Id entity)
{
  if (binding == pa6::InvalidId) return;
  const Key candidate = { binding, entity, kind };
  if (!indexed_ && values_.size() < 4) {
    for (std::size_t i = 0; i < values_.size(); ++i) {
      const Key& old = keys_[i];
      if (old.binding == binding ||
          (old.kind == pa6::FunctionBinding &&
           candidate.kind == pa6::FunctionBinding && old.entity == candidate.entity))
        return;
    }
    values_.push_back(binding);
    keys_.push_back(candidate);
    return;
  }
  if (!indexed_) {
    for (std::size_t i = 0; i < keys_.size(); ++i) {
      bindings_.insert(keys_[i].binding);
      if (keys_[i].kind == pa6::FunctionBinding) {
        if (keys_[i].entity == pa6::InvalidId)
          has_null_function_entity_ = true;
        else function_entities_.insert(keys_[i].entity);
      }
    }
    indexed_ = true;
    std::vector<Key>().swap(keys_);
  }
  if (bindings_.contains(binding)) return;
  if (candidate.kind == pa6::FunctionBinding &&
      (candidate.entity == pa6::InvalidId
          ? has_null_function_entity_
          : function_entities_.contains(candidate.entity))) return;
  bindings_.insert(binding);
  if (candidate.kind == pa6::FunctionBinding) {
    if (candidate.entity == pa6::InvalidId) has_null_function_entity_ = true;
    else function_entities_.insert(candidate.entity);
  }
  values_.push_back(binding);
}

std::vector<pa6::Id> BindingCandidates::release()
{ return std::move(values_); }

pa6::Id FindNamedBaseEntity(const pa6::SemanticUnit& unit, pa6::Id entity,
                            const std::vector<pa6::Id>& qualifier)
{
  FlatIdSet visited;
  std::vector<pa6::Id> pending(1, entity);
  while (!pending.empty()) {
    const pa6::Id current = pending.back();
    pending.pop_back();
    if (current >= unit.entity_count() || !visited.insert(current)) continue;
    const std::vector<pa6::Id>& bases = unit.entity(current).bases;
    for (std::size_t i = 0; i < bases.size(); ++i) {
      const pa6::Id base = bases[i];
      if (base_name_matches(unit, base, qualifier)) return base;
      pending.push_back(base);
    }
  }
  return pa6::InvalidId;
}

}  // namespace pa7
}  // namespace cppgm
