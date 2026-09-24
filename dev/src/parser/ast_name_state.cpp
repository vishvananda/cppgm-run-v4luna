#include "parser/ast_name_state.h"

#include <utility>

namespace cppgm {

ParserNameState::UndoEntry::UndoEntry()
    : kind(UndoScopeName), scope(0), existed(false), owner_existed(false),
      old_kind(UnknownName), old_size(0)
{}

ParserNameState::ParserNameState() : transaction_depth_(0) {}

std::size_t ParserNameState::begin_transaction()
{
  ++transaction_depth_;
  return undo_log_.size();
}

void ParserNameState::commit_transaction()
{
  --transaction_depth_;
  if (transaction_depth_ == 0) undo_log_.clear();
}

void ParserNameState::rollback_transaction(std::size_t mark)
{
  while (undo_log_.size() > mark) {
    const UndoEntry& entry = undo_log_.back();
    if (entry.kind == UndoScopeName) {
      if (entry.scope < scopes.size()) {
        if (entry.existed) scopes[entry.scope][entry.key] = entry.old_kind;
        else scopes[entry.scope].erase(entry.key);
      }
    } else if (entry.kind == UndoQualifiedName) {
      restore_qualified_name(entry.key, entry.existed, entry.old_kind);
    } else if (entry.kind == UndoNamespaceAlias) {
      if (entry.existed) namespace_aliases[entry.key] = entry.old_string;
      else namespace_aliases.erase(entry.key);
    } else if (entry.kind == UndoClassMember) {
      std::unordered_map<std::string, std::unordered_map<std::string, NameKind> >::iterator group =
          class_members.find(entry.owner);
      if (group != class_members.end()) {
        if (entry.existed) group->second[entry.key] = entry.old_kind;
        else group->second.erase(entry.key);
        if (!entry.owner_existed && group->second.empty()) class_members.erase(group);
      }
    } else if (entry.kind == UndoClassBases) {
      if (entry.owner_existed) class_bases[entry.owner].resize(entry.old_size);
      else class_bases.erase(entry.owner);
    } else if (entry.kind == UndoTypeAlias) {
      if (entry.existed) type_alias_targets[entry.key] = entry.old_string;
      else type_alias_targets.erase(entry.key);
    }
    undo_log_.pop_back();
  }
  --transaction_depth_;
  if (transaction_depth_ == 0) undo_log_.clear();
}

void ParserNameState::record_undo(UndoEntry&& entry)
{ if (transaction_depth_) undo_log_.push_back(std::move(entry)); }

void ParserNameState::set_scope_name(std::size_t scope, const std::string& key, NameKind value)
{
  std::unordered_map<std::string, NameKind>& names = scopes[scope];
  std::unordered_map<std::string, NameKind>::const_iterator old = names.find(key);
  if (old != names.end() && old->second == value) return;
  UndoEntry entry; entry.kind = UndoScopeName; entry.scope = scope; entry.key = key;
  entry.existed = old != names.end();
  if (entry.existed) entry.old_kind = old->second;
  record_undo(std::move(entry));
  names[key] = value;
}

void ParserNameState::set_qualified_name(const std::string& key, NameKind value)
{
  std::unordered_map<std::string, NameKind>::const_iterator old = qualified_names.find(key);
  if (old != qualified_names.end() && old->second == value) return;
  UndoEntry entry; entry.kind = UndoQualifiedName; entry.key = key;
  entry.existed = old != qualified_names.end();
  if (entry.existed) entry.old_kind = old->second;
  record_undo(std::move(entry));
  restore_qualified_name(key, true, value);
}

void ParserNameState::restore_qualified_name(const std::string& key, bool exists, NameKind value)
{
  const std::size_t separator = key.rfind("::");
  if (exists) qualified_names[key] = value;
  else qualified_names.erase(key);
  if (separator == std::string::npos) return;
  const std::string owner = key.substr(0, separator);
  const std::string member = key.substr(separator + 2);
  if (exists) {
    qualified_namespace_members[owner][member] = value;
  } else {
    std::unordered_map<std::string, std::unordered_map<std::string, NameKind> >::iterator group =
        qualified_namespace_members.find(owner);
    if (group != qualified_namespace_members.end()) {
      group->second.erase(member);
      if (group->second.empty()) qualified_namespace_members.erase(group);
    }
  }
}

void ParserNameState::set_namespace_alias(const std::string& key, const std::string& value)
{
  std::unordered_map<std::string, std::string>::const_iterator old = namespace_aliases.find(key);
  if (old != namespace_aliases.end() && old->second == value) return;
  UndoEntry entry; entry.kind = UndoNamespaceAlias; entry.key = key;
  entry.existed = old != namespace_aliases.end();
  if (entry.existed) entry.old_string = old->second;
  record_undo(std::move(entry));
  namespace_aliases[key] = value;
}

void ParserNameState::set_class_member(const std::string& owner, const std::string& key, NameKind value)
{
  std::unordered_map<std::string, std::unordered_map<std::string, NameKind> >::const_iterator group =
      class_members.find(owner);
  std::unordered_map<std::string, NameKind>::const_iterator old;
  if (group != class_members.end()) old = group->second.find(key);
  if (group != class_members.end() && old != group->second.end() && old->second == value) return;
  UndoEntry entry; entry.kind = UndoClassMember; entry.owner = owner; entry.key = key;
  entry.owner_existed = group != class_members.end();
  entry.existed = entry.owner_existed && old != group->second.end();
  if (entry.existed) entry.old_kind = old->second;
  record_undo(std::move(entry));
  class_members[owner][key] = value;
}

void ParserNameState::set_type_alias(const std::string& key, const std::string& value)
{
  std::unordered_map<std::string, std::string>::const_iterator old = type_alias_targets.find(key);
  if (old != type_alias_targets.end() && old->second == value) return;
  UndoEntry entry; entry.kind = UndoTypeAlias; entry.key = key;
  entry.existed = old != type_alias_targets.end();
  if (entry.existed) entry.old_string = old->second;
  record_undo(std::move(entry));
  type_alias_targets[key] = value;
}

void ParserNameState::append_class_base(const std::string& owner, const std::string& base)
{
  std::unordered_map<std::string, std::vector<std::string> >::const_iterator old =
      class_bases.find(owner);
  UndoEntry entry; entry.kind = UndoClassBases; entry.owner = owner;
  entry.owner_existed = old != class_bases.end();
  if (entry.owner_existed) entry.old_size = old->second.size();
  record_undo(std::move(entry));
  class_bases[owner].push_back(base);
}

}  // namespace cppgm
