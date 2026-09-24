#pragma once

#include "semantic/pa6.h"

#include <functional>
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace cppgm {
namespace pa7 {

class FlatIdSet
{
public:
  FlatIdSet();
  bool contains(pa6::Id value) const;
  bool insert(pa6::Id value);

private:
  void grow();
  std::vector<pa6::Id> slots_;
  std::size_t size_;
};

template <typename Fact>
class NodeFactTable
{
public:
  NodeFactTable() : size_(0) {}

  void reset()
  {
    std::vector<Entry>().swap(entries_);
    facts_.clear();
    size_ = 0;
  }

  void store(pa6::Id node, const Fact& fact)
  {
    if (node == pa6::InvalidId)
      throw std::invalid_argument("invalid AST node for fact");
    if (entries_.empty()) grow();
    std::size_t slot = slot_for(node);
    while (entries_[slot].node != pa6::InvalidId) {
      if (entries_[slot].node == node) {
        facts_[entries_[slot].fact] = fact;
        return;
      }
      slot = (slot + 1) & (entries_.size() - 1);
    }
    if ((size_ + 1) * 10 >= entries_.size() * 7) {
      grow();
      slot = slot_for(node);
      while (entries_[slot].node != pa6::InvalidId)
        slot = (slot + 1) & (entries_.size() - 1);
    }
    entries_[slot].node = node;
    entries_[slot].fact = facts_.size();
    ++size_;
    facts_.push_back(fact);
  }

  const Fact* find(pa6::Id node) const
  {
    if (node == pa6::InvalidId || entries_.empty()) return 0;
    std::size_t slot = slot_for(node);
    while (entries_[slot].node != pa6::InvalidId) {
      if (entries_[slot].node == node)
        return &facts_[entries_[slot].fact];
      slot = (slot + 1) & (entries_.size() - 1);
    }
    return 0;
  }

private:
  struct Entry
  {
    pa6::Id node;
    pa6::Id fact;
    Entry() : node(pa6::InvalidId), fact(pa6::InvalidId) {}
  };

  std::size_t slot_for(pa6::Id node) const
  { return std::hash<pa6::Id>()(node) & (entries_.size() - 1); }

  void grow()
  {
    const std::size_t capacity = entries_.empty() ? 8 : entries_.size() * 2;
    std::vector<Entry> grown(capacity);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].node == pa6::InvalidId) continue;
      std::size_t slot = std::hash<pa6::Id>()(entries_[i].node) & (capacity - 1);
      while (grown[slot].node != pa6::InvalidId)
        slot = (slot + 1) & (capacity - 1);
      grown[slot] = entries_[i];
    }
    entries_.swap(grown);
  }

  std::vector<Entry> entries_;
  std::vector<Fact> facts_;
  std::size_t size_;
};

class BindingCandidates
{
public:
  BindingCandidates();
  void add(const pa6::SemanticUnit& unit, pa6::Id binding);
  void add(pa6::Id binding, pa6::BindingKind kind, pa6::Id entity);
  std::vector<pa6::Id> release();

private:
  struct Key
  {
    pa6::Id binding;
    pa6::Id entity;
    pa6::BindingKind kind;
  };
  std::vector<pa6::Id> values_;
  std::vector<Key> keys_;
  FlatIdSet bindings_;
  FlatIdSet function_entities_;
  bool indexed_;
  bool has_null_function_entity_;
};

class SourceVisibility
{
public:
  explicit SourceVisibility(const pa6::SemanticUnit& unit);
  void mark_binding(pa6::Id binding);
  bool is_visible(pa6::Id binding) const;
  bool binding_visible(const pa6::SemanticUnit& unit, pa6::Id scope,
                       pa6::Id binding) const;
  void mark_type_declarations(const pa6::SemanticUnit& unit,
                              const Ast& ast, pa6::Id node);
  void expose_namespace_definition(const pa6::SemanticUnit& unit,
                                   pa6::Id scope);
  void add_using_directive(pa6::Id scope, pa6::Id target);
  void add_inline_namespace(pa6::Id scope, pa6::Id target);
  const std::vector<pa6::Id>& using_directives(pa6::Id scope) const;
  const std::vector<pa6::Id>& inline_namespaces(pa6::Id scope) const;

private:
  struct Edges
  {
    std::vector<pa6::Id> ordered;
    FlatIdSet targets;
    void add(pa6::Id target);
  };
  const pa6::SemanticUnit& unit_;
  std::vector<bool> visible_bindings_;
  std::unordered_map<pa6::Id, Edges> using_directives_;
  std::unordered_map<pa6::Id, Edges> inline_namespaces_;
};

pa6::Id FindNamedBaseEntity(const pa6::SemanticUnit& unit,
                            pa6::Id entity,
                            const std::vector<pa6::Id>& qualifier);

}  // namespace pa7
}  // namespace cppgm
