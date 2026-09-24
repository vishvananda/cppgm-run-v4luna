#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppgm {

enum NameKind { UnknownName, TypeNameKind, ValueName, TemplateNameKind,
  TemplateFunctionNameKind, NamespaceNameKind };

class ParserNameState
{
public:
  ParserNameState();
  std::size_t begin_transaction();
  void commit_transaction();
  void rollback_transaction(std::size_t mark);
  void set_scope_name(std::size_t scope, const std::string& key, NameKind value);
  void set_qualified_name(const std::string& key, NameKind value);
  void set_namespace_alias(const std::string& key, const std::string& value);
  void set_class_member(const std::string& owner, const std::string& key, NameKind value);
  void set_type_alias(const std::string& key, const std::string& value);
  void append_class_base(const std::string& owner, const std::string& base);

  std::vector<std::unordered_map<std::string, NameKind> > scopes;
  std::unordered_map<std::string, NameKind> qualified_names;
  std::unordered_map<std::string, std::unordered_map<std::string, NameKind> > qualified_namespace_members;
  std::unordered_map<std::string, std::string> namespace_aliases;
  std::unordered_map<std::string, std::unordered_map<std::string, NameKind> > class_members;
  std::unordered_map<std::string, std::vector<std::string> > class_bases;
  std::unordered_map<std::string, std::string> type_alias_targets;

private:
  enum UndoKind { UndoScopeName, UndoQualifiedName, UndoNamespaceAlias,
    UndoClassMember, UndoClassBases, UndoTypeAlias };
  struct UndoEntry
  {
    UndoEntry();
    UndoKind kind;
    std::size_t scope;
    bool existed;
    bool owner_existed;
    NameKind old_kind;
    std::size_t old_size;
    std::string owner;
    std::string key;
    std::string old_string;
  };
  void record_undo(UndoEntry&& entry);
  void restore_qualified_name(const std::string& key, bool exists, NameKind value);
  unsigned transaction_depth_;
  std::vector<UndoEntry> undo_log_;
};

}  // namespace cppgm
