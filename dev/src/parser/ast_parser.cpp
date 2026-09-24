#include "parser/ast_parser.h"
#include "parser/ast_name_state.h"
#include "parser/ast_tokens.h"

#include "postprocess/posttoken.h"
#include "preprocess/preprocessor.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

bool GetPreprocessorFileId(const std::string& path,
                           std::pair<unsigned long long, unsigned long long>& fileid)
{
  struct stat info;
  if (stat(path.c_str(), &info) != 0) return false;
  fileid = std::make_pair(static_cast<unsigned long long>(info.st_dev),
                          static_cast<unsigned long long>(info.st_ino));
  return true;
}

namespace cppgm {

namespace {

using ast_tokens::IdentifierToken;
using ast_tokens::IsKeyword;
using ast_tokens::KeywordToken;
using ast_tokens::LiteralToken;
using ast_tokens::MakeToken;
using ast_tokens::PunctuatorToken;
using ast_tokens::Token;
using ast_tokens::TokenText;

const std::size_t none = static_cast<std::size_t>(-1);


class Parser
{
  struct ParsedName
  {
    ParsedName() : first_syntax(none), last_syntax(none), last_component(none), first_token(none),
        source_token(none), prefix_syntax(none), operator_tokens_begin(none),
        operator_tokens_end(none), global_scope(false) {}
    std::string spelling;
    std::size_t first_syntax;
    std::size_t last_syntax;
    std::size_t last_component;
    std::size_t first_token;
    std::size_t source_token;
    std::size_t prefix_syntax;
    std::size_t operator_tokens_begin;
    std::size_t operator_tokens_end;
    bool global_scope;
  };

public:
  Parser(Ast& ast, PreprocessedTokenCursor& cursor)
      : ast_(ast), tokens_(ast.tokens), cursor_(cursor), pos_(0),
        pending_gt_(0), template_non_type_default_(0),
        template_expression_nesting_(0), input_finished_(false)
  {
    names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
  }

  Ast Parse()
  {
    const std::size_t root = node(NTranslationUnit);
    while (!at_end()) {
      const std::size_t before = pos_;
      ast_.append(root, parse_declaration(false));
      if (pos_ == before) fail("parser made no progress");
    }
    ast_.compact_tokens();
    return std::move(ast_);
  }

private:
  const Token& token(std::size_t n) const
  {
    if (has_token(n)) return tokens_[n];
    static const std::string end_text("<eof>");
    static const std::string end_tag("ST_EOF");
    static const Token end = []() {
      Token value;
      value.text = TokenText(end_text);
      value.tag = TokenText(end_tag);
      value.category = PunctuatorToken;
      return value;
    }();
    return end;
  }
  bool has_token(std::size_t n) const
  {
    while (tokens_.size() <= n && !input_finished_) {
      PreprocessingToken pp;
      if (!cursor_.next(pp)) {
        input_finished_ = true;
        break;
      }
      if (pp.kind == PP_TOKEN_WHITESPACE || pp.kind == PP_TOKEN_NEWLINE ||
          pp.kind == PP_TOKEN_EOF || pp.kind == PP_TOKEN_HEADER_NAME)
        continue;
      const std::string* spelling = pp.identifier_spelling;
      if (pp.kind == PP_TOKEN_IDENTIFIER &&
          (spelling == 0 || pp.identifier_id == static_cast<std::size_t>(-1)))
        throw std::runtime_error("preprocessor emitted an uninterned identifier");
      if (pp.kind == PP_TOKEN_IDENTIFIER)
        ast_.register_identifier(pp.identifier_id, *spelling);
      if (!spelling) spelling = ast_.intern_spelling(pp.spelling);
      tokens_.push_back(MakeToken(pp, spelling));
    }
    return n < tokens_.size();
  }
  const Token& peek(std::size_t n = 0) const { return token(pos_ + n); }
  bool at_end() const { return pending_gt_ == 0 && !has_token(pos_); }
  bool is(const std::string& s, std::size_t n = 0) const
  { return n == 0 && s == ">" && pending_gt_ ? true : peek(n).text == s; }
  bool is_tag(const std::string& s, std::size_t n = 0) const { return peek(n).tag == s; }
  std::size_t take() { if (at_end()) fail("unexpected end of input"); return pos_++; }
  bool consume(const std::string& s)
  {
    if (s == ">" && pending_gt_) { --pending_gt_; return true; }
    if (!is(s)) return false;
    ++pos_; return true;
  }
  void expect(const std::string& s) { if (!consume(s)) fail("expected '" + s + "'"); }
  void fail(const std::string& what) const
  {
    const Token& t = peek();
    throw std::runtime_error(what + " at " + std::to_string(t.line) + ":" +
        std::to_string(t.column) + " near '" + t.text + "'");
  }
  std::size_t node(NodeKind kind, std::size_t atom = none)
  {
    const std::size_t id = ast_.add(kind, atom);
    if (atom != none && atom < tokens_.size()) set_node_location_from_token(id, atom);
    else if (has_token(pos_)) set_node_location_from_token(id, pos_);
    return id;
  }
  std::size_t atom_node(NodeKind kind, std::size_t atom)
  { return node(kind, atom); }
  void set_node_location_from_token(std::size_t id, std::size_t token_index)
  {
    if (token_index >= tokens_.size()) return;
    const Token& source = tokens_[token_index];
    ast_.set_location(id, source.source_file_id, source.line, source.column);
  }
  std::size_t composite_node(NodeKind kind, const std::string& text,
                             std::size_t source_token = none)
  {
    ast_.composite_atoms.push_back(text);
    const std::size_t id = ast_.add(kind, none, ast_.composite_atoms.size() - 1);
    if (source_token != none) set_node_location_from_token(id, source_token);
    else if (has_token(pos_)) set_node_location_from_token(id, pos_);
    return id;
  }
  void ensure_name_syntax(ParsedName& name)
  {
    if (name.first_syntax != none) return;
    const std::size_t source_token = name.source_token != none ? name.source_token : name.first_token;
    if (name.global_scope) {
      const std::size_t global = node(NGlobalScope);
      if (source_token != none) set_node_location_from_token(global, source_token);
      append_name_syntax(name, global);
    }
    if (name.prefix_syntax != none) append_name_syntax(name, name.prefix_syntax);
    if (name.first_token != none) {
      name.last_component = node(NNameComponent, name.first_token);
      set_node_location_from_token(name.last_component, name.first_token);
      append_operator_name_tokens(name, name.last_component);
      append_name_syntax(name, name.last_component);
    } else if (name.first_token == none && name.prefix_syntax == none &&
               !name.global_scope && !name.spelling.empty()) {
      name.last_component = composite_node(NNameComponent, name.spelling, source_token);
      append_name_syntax(name, name.last_component);
    }
  }
  void append_name_syntax(ParsedName& name, std::size_t id)
  {
    if (name.first_syntax == none) name.first_syntax = name.last_syntax = id;
    else {
      ast_.nodes[name.last_syntax].next_aux_sibling = id;
      name.last_syntax = id;
    }
  }
  void append_operator_name_tokens(const ParsedName& name, std::size_t component)
  {
    if (name.operator_tokens_begin == none || name.operator_tokens_end == none ||
        ast_.nodes[component].atom != name.operator_tokens_begin) return;
    for (std::size_t i = name.operator_tokens_begin + 1;
         i < name.operator_tokens_end; ++i)
      ast_.append(component, node(NNameComponentToken, i));
  }
  void add_name_component(ParsedName& name, std::size_t token_index)
  {
    ensure_name_syntax(name);
    const std::size_t component = node(NNameComponent, token_index);
    set_node_location_from_token(component, token_index);
    if (name.operator_tokens_begin == token_index)
      append_operator_name_tokens(name, component);
    append_name_syntax(name, component);
    name.last_component = component;
  }
  std::size_t composite_node(NodeKind kind, ParsedName name)
  {
    if ((kind == NIdentifier || kind == NIdExpression) &&
        name.first_token != none && name.first_token == name.source_token &&
        !name.global_scope && name.prefix_syntax == none &&
        name.spelling == tokens_[name.first_token].text)
      return atom_node(kind, name.first_token);
    ensure_name_syntax(name);
    const std::size_t source_token = name.source_token != none ? name.source_token : name.first_token;
    const std::size_t id = composite_node(kind, name.spelling, source_token);
    if (name.first_syntax != none) {
      ast_.nodes[id].first_aux_child = name.first_syntax;
      ast_.nodes[id].last_aux_child = name.last_syntax;
    }
    return id;
  }
  void set_composite_name(std::size_t id, ParsedName& name)
  {
    ensure_name_syntax(name);
    ast_.composite_atoms.push_back(name.spelling);
    ast_.nodes[id].composite = ast_.composite_atoms.size() - 1;
    if (name.first_syntax != none) {
      ast_.nodes[id].first_aux_child = name.first_syntax;
      ast_.nodes[id].last_aux_child = name.last_syntax;
    }
    const std::size_t source_token = name.source_token != none ? name.source_token : name.first_token;
    if (source_token != none) set_node_location_from_token(id, source_token);
  }
  bool is_identifier(std::size_t n = 0) const { return peek(n).category == IdentifierToken; }
  bool is_literal(std::size_t n = 0) const { return peek(n).category == LiteralToken; }
  bool is_keyword(const std::string& s, std::size_t n = 0) const
  { return peek(n).category == KeywordToken && is(s, n); }
  bool is_cv() const { return is_keyword("const") || is_keyword("volatile"); }
  bool is_builtin_type() const
  {
    static const char* words[] = {"bool", "char", "char16_t", "char32_t", "double", "float",
      "int", "long", "short", "signed", "unsigned", "void", "wchar_t", "auto"};
    for (std::size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
      if (is_keyword(words[i])) return true;
    return false;
  }
  bool is_decl_modifier() const
  {
    static const char* words[] = {"const", "volatile", "typedef", "extern", "static", "inline",
      "virtual", "constexpr", "thread_local", "register", "friend", "explicit", "mutable"};
    for (std::size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
      if (is_keyword(words[i])) return true;
    return false;
  }
  NameKind lookup_name(const std::string& name) const
  {
    std::string normalized = name;
    while (normalized.compare(0, 2, "::") == 0) normalized.erase(0, 2);
    std::unordered_map<std::string, NameKind>::const_iterator qualified = names_.qualified_names.find(normalized);
    if (qualified != names_.qualified_names.end()) return qualified->second;
    std::size_t separator = normalized.find("::");
    if (separator != std::string::npos) {
      const std::string prefix = normalized.substr(0, separator);
      std::unordered_map<std::string, std::string>::const_iterator alias = names_.namespace_aliases.find(prefix);
      if (alias != names_.namespace_aliases.end()) {
        const std::string resolved = alias->second + normalized.substr(separator);
        qualified = names_.qualified_names.find(resolved);
        if (qualified != names_.qualified_names.end()) return qualified->second;
      }
    }
    const std::size_t id = ast_.find_name(normalized);
    if (id == none) return UnknownName;
    for (std::size_t i = names_.scopes.size(); i != 0; --i) {
      std::unordered_map<NameId, NameKind>::const_iterator it = names_.scopes[i - 1].find(id);
      if (it != names_.scopes[i - 1].end()) return it->second;
    }
    return UnknownName;
  }
  NameKind lookup_name(const Token& token) const
  {
    if (token.identifier_id == none || token.category != IdentifierToken)
      return UnknownName;
    for (std::size_t i = names_.scopes.size(); i != 0; --i) {
      std::unordered_map<NameId, NameKind>::const_iterator found =
          names_.scopes[i - 1].find(token.identifier_id);
      if (found != names_.scopes[i - 1].end()) return found->second;
    }
    return UnknownName;
  }
  void bind_name(const std::string& name, NameKind kind)
  { bind_name_in_scope(names_.scopes.size() - 1, name, kind); }
  void bind_name(const Token& token, NameKind kind)
  { bind_name_in_scope(names_.scopes.size() - 1, token, kind); }
  void bind_name_in_scope(std::size_t scope, const std::string& name, NameKind kind)
  {
    if (name.empty()) return;
    bind_name_in_scope(scope, ast_.intern_name(name), name, kind);
  }
  void bind_name_in_scope(std::size_t scope, const Token& token, NameKind kind)
  {
    const NameId id = token.identifier_id == none ? ast_.intern_name(token.text) : token.identifier_id;
    bind_name_in_scope(scope, id, token.text, kind);
  }
  void bind_name_in_scope(std::size_t scope, NameId id,
                          const std::string& name, NameKind kind)
  {
    if (name.empty()) return;
    names_.set_scope_name(scope, id, kind);
    if (!class_scope_indices_.empty() && class_scope_indices_.back() == scope) {
      const std::string key = class_lookup_key(class_context_names_.back());
      names_.set_class_member(key, id, kind);
    }
    if (namespace_path_.empty()) return;
    std::string full, visible;
    for (std::size_t i = 0; i < namespace_path_.size(); ++i) {
      if (!full.empty()) full += "::";
      full += namespace_path_[i];
      if (i >= namespace_inline_.size() || !namespace_inline_[i]) {
        if (!visible.empty()) visible += "::";
        visible += namespace_path_[i];
      }
    }
    if (!full.empty()) names_.set_qualified_name(full + "::" + name, kind);
    if (!visible.empty() && visible != full) names_.set_qualified_name(visible + "::" + name, kind);
  }
  std::string class_lookup_key(const std::string& name) const
  {
    std::string key = strip_template_arguments(name);
    while (key.compare(0, 2, "::") == 0) key.erase(0, 2);
    return key;
  }
  std::string strip_template_arguments(const std::string& name) const
  {
    std::string result;
    int depth = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
      if (name[i] == '<') ++depth;
      else if (name[i] == '>' && depth) --depth;
      else if (!depth) result += name[i];
    }
    return result;
  }
  std::string qualified_class_name(const std::string& name) const
  {
    std::string qualified;
    if (!class_context_names_.empty()) qualified = class_context_names_.back();
    else {
      for (std::size_t i = 0; i < namespace_path_.size(); ++i) {
        if (!qualified.empty()) qualified += "::";
        qualified += namespace_path_[i];
      }
    }
    if (!qualified.empty()) qualified += "::";
    qualified += name;
    return qualified;
  }
  void remember_class_bases(const std::string& class_name, std::size_t clause)
  {
    const std::string owner = class_lookup_key(class_name);
    for (std::size_t base = ast_.nodes[clause].first_child; base != none;
         base = ast_.nodes[base].next_sibling) {
      for (std::size_t child = ast_.nodes[base].first_child; child != none;
           child = ast_.nodes[child].next_sibling) {
        if (ast_.nodes[child].kind == NBaseName && ast_.nodes[child].composite != none)
          names_.append_class_base(owner, ast_.composite_atoms[ast_.nodes[child].composite]);
      }
    }
  }
  void import_class_members_recursive(const std::string& class_name,
      std::unordered_set<std::string>& visited)
  {
    const std::string key = class_lookup_key(class_name);
    if (!visited.insert(key).second) return;
    std::unordered_map<std::string, std::unordered_map<NameId, NameKind> >::const_iterator members =
        names_.class_members.find(key);
    if (members != names_.class_members.end()) {
      for (std::unordered_map<NameId, NameKind>::const_iterator member = members->second.begin();
           member != members->second.end(); ++member)
        if (names_.scopes.back().find(member->first) == names_.scopes.back().end())
          names_.set_scope_name(names_.scopes.size() - 1, member->first, member->second);
    }
    std::unordered_map<std::string, std::vector<std::string> >::const_iterator bases = names_.class_bases.find(key);
    if (bases != names_.class_bases.end())
      for (std::size_t i = 0; i < bases->second.size(); ++i)
        import_class_members_recursive(bases->second[i], visited);
  }
  void import_class_members_for_declarator(std::size_t declarator)
  {
    const std::string name = first_declaration_name(declarator);
    const std::size_t separator = name.rfind("::");
    if (separator == std::string::npos) return;
    std::unordered_set<std::string> visited;
    import_class_members_recursive(name.substr(0, separator), visited);
  }
  void import_namespace(const std::string& raw_name)
  {
    std::string name = raw_name;
    while (name.compare(0, 2, "::") == 0) name.erase(0, 2);
    std::unordered_map<std::string, std::string>::const_iterator alias = names_.namespace_aliases.find(name);
    if (alias != names_.namespace_aliases.end()) name = alias->second;
    std::unordered_map<std::string, std::unordered_map<std::string, NameKind> >::const_iterator members =
        names_.qualified_namespace_members.find(name);
    if (members == names_.qualified_namespace_members.end()) return;
    for (std::unordered_map<std::string, NameKind>::const_iterator it = members->second.begin();
         it != members->second.end(); ++it)
      names_.set_scope_name(names_.scopes.size() - 1, ast_.intern_name(it->first), it->second);
  }
  bool lexical_type_hint(const std::string& name) const
  {
    if (name.empty()) return false;
    return name.find('T') != std::string::npos || name.find('C') != std::string::npos ||
        name.find('Y') != std::string::npos || name.find('E') != std::string::npos;
  }
  bool is_type_name() const
  {
    if (is("::")) return qualified_type_name_ahead();
    if (!is_identifier()) return false;
    NameKind known = lookup_name(peek());
    if (known == ValueName) {
      return names_.namespace_aliases.find(peek().text) != names_.namespace_aliases.end() &&
          qualified_type_name_ahead();
    }
    if (known == TemplateFunctionNameKind) return false;
    return known == TypeNameKind || known == TemplateNameKind ||
        (known == NamespaceNameKind && qualified_type_name_ahead()) ||
        lexical_type_hint(peek().text) || qualified_type_name_ahead();
  }
  bool qualified_type_name_ahead() const
  { return qualified_type_name_ahead(0); }
  bool qualified_type_name_ahead(std::size_t offset) const
  {
    std::size_t i = pos_ + offset;
    std::string name;
    bool global = false;
    if (has_token(i) && tokens_[i].text == "::") { global = true; name = "::"; ++i; }
    if (!has_token(i) || tokens_[i].category != IdentifierToken) return false;
    const std::string first = tokens_[i].text;
    if (!global) {
      const NameKind first_kind = lookup_name(tokens_[i]);
      if ((first_kind == ValueName && names_.namespace_aliases.find(first) == names_.namespace_aliases.end()) ||
          first_kind == TemplateFunctionNameKind) return false;
    }
    name += tokens_[i++].text;
    if (has_token(i) && tokens_[i].text == "<") {
      int angles = 0, parens = 0, brackets = 0, braces = 0;
      do {
        const std::string& text = tokens_[i].text;
        if (text == "(") ++parens;
        else if (text == ")" && parens) --parens;
        else if (text == "[") ++brackets;
        else if (text == "]" && brackets) --brackets;
        else if (text == "{") ++braces;
        else if (text == "}" && braces) --braces;
        else if (!parens && !brackets && !braces && text == "<") ++angles;
        else if (!parens && !brackets && !braces && text == ">" && angles) --angles;
        else if (!parens && !brackets && !braces && text == ">>" && angles)
          angles = angles >= 2 ? angles - 2 : 0;
        name += text; ++i;
      } while (has_token(i) && angles > 0);
    }
    bool qualified = false;
    while (has_token(i + 1) && tokens_[i].text == "::" &&
           tokens_[i + 1].category == IdentifierToken) {
      qualified = true; name += "::"; name += tokens_[i + 1].text; i += 2;
      if (has_token(i) && tokens_[i].text == "<") {
        int angles = 0;
        do {
          if (tokens_[i].text == "<") ++angles;
          else if (tokens_[i].text == ">") --angles;
          else if (tokens_[i].text == ">>" && angles >= 2) angles -= 2;
          name += tokens_[i].text; ++i;
        } while (has_token(i) && angles > 0);
      }
    }
    if (!qualified) return false;
    NameKind kind = lookup_name(name);
    if (kind == TypeNameKind || kind == TemplateNameKind) return true;
    const std::string normalized = strip_template_arguments(name);
    kind = lookup_name(normalized);
    if (kind == TypeNameKind || kind == TemplateNameKind) return true;
    const std::size_t separator = normalized.rfind("::");
    if (separator == std::string::npos) return false;
    std::string prefix = normalized.substr(0, separator);
    const std::string leaf = normalized.substr(separator + 2);
    const NameKind prefix_kind = lookup_name(prefix);
    if (prefix_kind == ValueName || prefix_kind == TemplateFunctionNameKind) return false;
    if (prefix_kind == TypeNameKind) {
      std::unordered_map<std::string, std::string>::const_iterator alias = names_.type_alias_targets.find(prefix);
      if (alias != names_.type_alias_targets.end()) prefix = alias->second;
    }
    std::unordered_map<std::string, std::unordered_map<NameId, NameKind> >::const_iterator members =
        names_.class_members.find(class_lookup_key(prefix));
    const NameId leaf_id = ast_.find_name(leaf);
    if (leaf_id == none) return false;
    if (members != names_.class_members.end()) {
      std::unordered_map<NameId, NameKind>::const_iterator member = members->second.find(leaf_id);
      if (member != members->second.end() &&
          (member->second == TypeNameKind || member->second == TemplateNameKind)) return true;
    }
    const std::string root = normalized.substr(0, normalized.find("::"));
    const NameKind root_kind = lookup_name(root);
    return names_.has_qualified_type_leaf(leaf, root_kind == NamespaceNameKind ||
        names_.namespace_aliases.find(root) != names_.namespace_aliases.end());
  }
  bool template_id_followed_by_scope() const
  {
    if (!is_identifier() || !is("<", 1)) return false;
    std::size_t i = pos_ + 1;
    int angles = 0, parens = 0, brackets = 0, braces = 0;
    do {
      const std::string& text = tokens_[i].text;
      if (text == "(") ++parens;
      else if (text == ")" && parens) --parens;
      else if (text == "[") ++brackets;
      else if (text == "]" && brackets) --brackets;
      else if (text == "{") ++braces;
      else if (text == "}" && braces) --braces;
      else if (!parens && !brackets && !braces && text == "<") ++angles;
      else if (!parens && !brackets && !braces && text == ">" && angles) --angles;
      else if (!parens && !brackets && !braces && text == ">>" && angles)
        angles = angles >= 2 ? angles - 2 : 0;
      ++i;
    } while (has_token(i) && angles > 0);
    return angles == 0 && has_token(i + 1) && tokens_[i].text == "::" &&
        tokens_[i + 1].category == IdentifierToken;
  }
  bool is_type_token(std::size_t offset) const
  {
    if (peek(offset).category != IdentifierToken) return false;
    NameKind known = lookup_name(peek(offset));
    return known == TypeNameKind || known == TemplateNameKind ||
        (known != ValueName && lexical_type_hint(peek(offset).text));
  }
  bool looks_parameter_clause() const
  {
    if (!is("(")) return false;
    const Token& first = peek(1);
    if (first.text == ")" || first.text == "...") return true;
    if (first.text == "class" || first.text == "struct" || first.text == "union" ||
        first.text == "enum" || first.text == "typename" || first.text == "decltype") return true;
    static const char* type_words[] = {"bool", "char", "char16_t", "char32_t", "double", "float",
      "int", "long", "short", "signed", "unsigned", "void", "wchar_t", "auto", "const", "volatile"};
    for (std::size_t i = 0; i < sizeof(type_words) / sizeof(type_words[0]); ++i)
      if (first.text == type_words[i]) return true;
    if (!is_type_token(1) && !qualified_type_name_ahead(1)) return false;
    int parens = 0, brackets = 0, braces = 0, angles = 0;
    for (std::size_t i = pos_ + 1; has_token(i); ++i) {
      const std::string& text = tokens_[i].text;
      if (text == ")") { if (!parens && !brackets && !braces) return true; if (parens) --parens; }
      else if (text == "(") ++parens;
      else if (text == "]") { if (brackets) --brackets; }
      else if (text == "[") ++brackets;
      else if (text == "}") { if (braces) --braces; }
      else if (text == "{") ++braces;
      else if (!parens && !brackets && !braces && text == "<") ++angles;
      else if (!parens && !brackets && !braces && text == ">" && angles) --angles;
      else if (!parens && !brackets && !braces && text == ">>" && angles >= 2) angles -= 2;
      else if (!parens && !brackets && !braces && !angles && text == ",") {
        const std::size_t next = i + 1;
        if (!has_token(next)) return false;
        const Token& token = tokens_[next];
        static const char* starters[] = {"bool", "char", "char16_t", "char32_t", "class",
          "const", "decltype", "double", "enum", "float", "int", "long", "short",
          "signed", "struct", "typename", "union", "unsigned", "void", "volatile", "wchar_t"};
        bool starts = token.text == "...";
        for (std::size_t j = 0; j < sizeof(starters) / sizeof(starters[0]); ++j)
          if (token.text == starters[j]) starts = true;
        if (token.category == IdentifierToken) {
          const NameKind kind = lookup_name(token);
          starts = starts || kind == TypeNameKind || kind == TemplateNameKind ||
              (kind != ValueName && lexical_type_hint(token.text)) ||
              qualified_type_name_ahead(next - pos_);
        }
        if (!starts) return false;
      }
    }
    return false;
  }
  bool starts_decl_specifier() const
  { return is_builtin_type() || is_decl_modifier() || is_type_name() ||
      is_keyword("typename") || is_keyword("enum") || is_keyword("class") ||
      is_keyword("struct") || is_keyword("union") || is_keyword("decltype"); }
  bool skip_attribute_specifier()
  {
    bool skipped = false;
    for (;;) {
      if (is("[") && is("[", 1)) {
        take(); take();
        int depth = 2;
        while (depth && !at_end()) {
          if (consume("[")) ++depth;
          else if (consume("]")) --depth;
          else take();
        }
        if (depth) fail("unterminated attribute specifier");
        skipped = true;
      } else if (is_identifier() && (peek().text == "__attribute__" ||
                 peek().text == "__attribute" || peek().text == "__declspec")) {
        take();
        if (consume("(")) {
          int depth = 1;
          while (depth && !at_end()) {
            if (consume("(")) ++depth;
            else if (consume(")")) --depth;
            else take();
          }
          if (depth) fail("unterminated vendor attribute");
        }
        skipped = true;
      } else {
        break;
      }
    }
    return skipped;
  }
  void skip_alignas_specifier()
  {
    if (!is_keyword("alignas")) return;
    take();
    expect("(");
    int depth = 1;
    while (depth && !at_end()) {
      if (consume("(")) ++depth;
      else if (consume(")")) --depth;
      else take();
    }
    if (depth) fail("unterminated alignas specifier");
  }
  std::size_t parse_decl_specifier_seq(bool require_type)
  {
    std::size_t seq = node(NDeclSpecifierSeq);
    bool saw_type = false;
    while (!at_end()) {
      if (skip_attribute_specifier()) continue;
      if (!saw_type && (is_keyword("class") || is_keyword("struct") || is_keyword("union"))) {
        ast_.append(seq, parse_class_specifier(true));
        saw_type = true;
        continue;
      }
      if (!saw_type && is_keyword("enum")) {
        ast_.append(seq, parse_enum_specifier(true));
        saw_type = true;
        continue;
      }
      if (!saw_type && is_keyword("decltype")) {
        ast_.append(seq, parse_decltype_specifier(NDeclSpecifier));
        saw_type = true;
        continue;
      }
      if (!saw_type && is_keyword("typename")) {
        take();
        ast_.append(seq, composite_node(NDeclSpecifier, joined_name()));
        saw_type = true;
        continue;
      }
      if (is_builtin_type()) {
        std::size_t t = take();
        ast_.append(seq, atom_node(NDeclSpecifier, t));
        saw_type = true;
        continue;
      }
      if (is_decl_modifier()) {
        std::size_t t = take();
        ast_.append(seq, atom_node(NDeclSpecifier, t));
        continue;
      }
      if (!saw_type && qualified_type_name_ahead()) {
        ast_.append(seq, composite_node(NDeclSpecifier, joined_name()));
        saw_type = true;
        continue;
      }
      if (!saw_type && is_type_name()) {
        std::size_t t = take();
        ParsedName name;
        name.spelling = tokens_[t].text;
        name.first_token = name.source_token = t;
        if (is("<") && template_id_candidate(name.spelling)) {
          take(); read_template_suffix(name);
          ast_.append(seq, composite_node(NDeclSpecifier, name));
        } else {
          ast_.append(seq, atom_node(NDeclSpecifier, t));
        }
        saw_type = true;
        continue;
      }
      break;
    }
    if (require_type && !saw_type) fail("expected declaration type");
    return seq;
  }
  std::size_t parse_decltype_specifier(NodeKind kind)
  {
    std::size_t keyword = take();
    expect("(");
    const std::size_t expression_begin = pos_;
    ++template_expression_nesting_;
    std::size_t expression = parse_expression();
    --template_expression_nesting_;
    const std::size_t expression_end = pos_;
    expect(")");
    std::string spelling = "decltype(";
    for (std::size_t i = expression_begin; i < expression_end; ++i) {
      if (i != expression_begin && is_word_token(tokens_[i - 1]) && is_word_token(tokens_[i]))
        spelling += ' ';
      spelling += tokens_[i].text;
    }
    spelling += ")";
    std::size_t result = composite_node(kind, spelling, keyword);
    ast_.append(result, expression);
    return result;
  }
  bool is_word_token(const Token& token) const
  { return token.category == IdentifierToken || token.category == KeywordToken || token.category == LiteralToken; }
  ParsedName read_decltype_qualified_name()
  {
    const std::size_t keyword = pos_;
    const std::size_t prefix = parse_decltype_specifier(NDecltypeSpecifier);
    ParsedName name;
    name.spelling = ast_.composite_atoms[ast_.nodes[prefix].composite];
    name.source_token = keyword;
    name.prefix_syntax = prefix;
    while (consume("::")) {
      name.spelling += "::";
      if (is_keyword("template")) { name.spelling += "template"; take(); }
      if (!is_identifier() && peek().category != KeywordToken) fail("expected qualified name component");
      const std::size_t component = take();
      name.spelling += tokens_[component].text;
      add_name_component(name, component);
      if (is("<")) { take(); read_template_suffix(name); }
    }
    return name;
  }
  ParsedName joined_name()
  {
    if (is_keyword("decltype")) return read_decltype_qualified_name();
    ParsedName name;
    if (consume("::")) {
      name.global_scope = true;
      name.source_token = pos_ - 1;
      name.spelling = "::";
    }
    if (!is_identifier()) fail("expected identifier");
    const std::size_t first = take();
    name.first_token = first;
    if (name.source_token == none) name.source_token = first;
    name.spelling += tokens_[first].text;
    if (is("<") && template_id_candidate(name.spelling)) { take(); read_template_suffix(name); }
    while (consume("::")) {
      name.spelling += "::";
      const bool explicit_template = is_keyword("template");
      if (explicit_template) { name.spelling += "template "; take(); }
      if (!is_identifier()) fail("expected qualified name component");
      const std::size_t component = take();
      name.spelling += tokens_[component].text;
      add_name_component(name, component);
      if (is("<") && (explicit_template || template_id_candidate(name.spelling))) {
        take(); read_template_suffix(name);
      }
    }
    return name;
  }
  bool template_id_candidate(const std::string& base) const
  {
    if (!is("<")) return false;
    NameKind kind = lookup_name(base);
    if (kind == ValueName) return false;
    const std::size_t scope = base.rfind("::");
    const std::string leaf = scope == std::string::npos ? base : base.substr(scope + 2);
    if (kind == UnknownName) kind = lookup_name(leaf);
    if (kind == TypeNameKind || kind == TemplateNameKind ||
        kind == TemplateFunctionNameKind || lexical_type_hint(base)) return true;
    if (scope != std::string::npos) {
      const std::string owner = base.substr(0, scope);
      std::unordered_map<std::string, std::unordered_map<NameId, NameKind> >::const_iterator members =
          names_.class_members.find(class_lookup_key(owner));
      if (members != names_.class_members.end()) {
        const NameId leaf_id = ast_.find_name(leaf);
        if (leaf_id == none) return false;
        std::unordered_map<NameId, NameKind>::const_iterator member = members->second.find(leaf_id);
        if (member != members->second.end() &&
            (member->second == TemplateNameKind || member->second == TemplateFunctionNameKind))
          return true;
      }
    }
    int depth = 0, parens = 0, brackets = 0;
    bool type_argument = false;
    for (std::size_t i = pos_; has_token(i); ++i) {
      const std::string& s = tokens_[i].text;
      if (s == "(") ++parens; else if (s == ")" && parens) --parens;
      else if (s == "[") ++brackets; else if (s == "]" && brackets) --brackets;
      if (!parens && !brackets) {
        if (s == "<") ++depth;
        else if (s == ">") { if (--depth == 0) {
          const std::string& after = has_token(i + 1) ? tokens_[i + 1].text.get() : std::string();
          return type_argument && (after == "(" || after == "::" || after == ")" ||
              after == ";" || after == "," || (has_token(i + 1) && tokens_[i + 1].category == IdentifierToken));
        }} else if (s == ">>" && depth >= 2) {
          depth -= 2;
          if (depth == 0) {
            const std::string& after = has_token(i + 1) ? tokens_[i + 1].text.get() : std::string();
            return type_argument && (after == "(" || after == "::" || after == ")" || after == ";" || after == ",");
          }
        }
      }
      if (IsKeyword(s) || lexical_type_hint(s)) type_argument = true;
      if (s == ";") return false;
    }
    return false;
  }

  std::size_t parse_declaration(bool in_class)
  {
    skip_attribute_specifier();
    if (consume(";")) return node(NEmptyDeclaration);
    if (in_class && !class_context_names_.empty() && is_keyword("explicit") &&
        is_identifier(1) && peek(1).text == class_name_leaf(class_context_names_.back()) &&
        is("(", 2)) {
      const std::size_t specifier = take();
      return parse_special_member(class_context_names_.back(), specifier);
    }
    if (is_keyword("friend") && friend_class_declaration_ahead())
      return parse_friend_class_declaration();
    if (is_keyword("extern") && is_literal(1)) return parse_linkage_specification();
    if (is_keyword("extern") && is_keyword("template", 1)) {
      take(); take();
      return parse_explicit_instantiation();
    }
    if (is_keyword("namespace") || (is_keyword("inline") && is_keyword("namespace", 1))) return parse_namespace();
    if (is_keyword("using")) return parse_using();
    if (is_keyword("static_assert")) return parse_static_assert();
    if (is_keyword("template")) {
      if (is("<", 1)) return parse_template_declaration(in_class);
      take(); return parse_explicit_instantiation();
    }
    if (qualified_special_member_ahead()) return parse_qualified_special_member_definition();
    if (is_keyword("inline")) {
      const std::size_t saved = pos_;
      std::size_t specifier = take();
      skip_attribute_specifier();
      if (qualified_special_member_ahead())
        return parse_qualified_special_member_definition(specifier);
      pos_ = saved;
    }
    if ((is_keyword("class") || is_keyword("struct") || is_keyword("union")) &&
        class_definition_or_forward_ahead()) return parse_class();
    if (is_keyword("enum") && enum_declaration_ahead()) return parse_enum();
    if (!starts_decl_specifier()) fail("expected declaration");
    return parse_simple_or_function();
  }
  bool friend_class_declaration_ahead() const
  {
    if (!is_keyword("friend") ||
        !(is_keyword("class", 1) || is_keyword("struct", 1) || is_keyword("union", 1)))
      return false;
    std::size_t i = pos_ + 2;
    if (has_token(i) && tokens_[i].category == IdentifierToken) ++i;
    return has_token(i) && tokens_[i].text == ";";
  }
  std::size_t parse_friend_class_declaration()
  {
    std::size_t friend_token = take();
    std::size_t specifiers = node(NDeclSpecifierSeq);
    ast_.append(specifiers, atom_node(NDeclSpecifier, friend_token));
    ast_.append(specifiers, parse_class_specifier(false));
    std::size_t declaration = node(NSimpleDeclaration);
    ast_.append(declaration, specifiers);
    return declaration;
  }
  std::size_t parse_linkage_specification()
  {
    take();
    if (!is_literal()) fail("expected linkage string literal");
    std::string language = tokens_[take()].text;
    if (language.size() >= 2 && language[0] == '"' && language[language.size() - 1] == '"')
      language = language.substr(1, language.size() - 2);
    std::size_t linkage = composite_node(NLinkageSpecification, language);
    if (consume("{")) {
      while (!consume("}")) {
        if (at_end()) fail("unterminated linkage specification");
        ast_.append(linkage, parse_declaration(false));
      }
    } else {
      ast_.append(linkage, parse_declaration(false));
    }
    return linkage;
  }
  std::size_t parse_explicit_instantiation()
  {
    std::size_t declaration = parse_simple_or_function();
    std::size_t result = node(NExplicitInstantiation);
    ast_.append(result, declaration);
    return result;
  }
  std::size_t parse_namespace()
  {
    bool inline_namespace = consume("inline");
    take();
    if (is_identifier() && is("=", 1)) {
      std::size_t n = take(); expect("=");
      std::size_t alias = node(NNamespaceAliasDefinition, n);
      ParsedName target = joined_name();
      ast_.append(alias, composite_node(NTarget, target));
      bind_name(tokens_[n], NamespaceNameKind);
      names_.set_namespace_alias(tokens_[n].text, target.spelling);
      expect(";");
      return alias;
    }
    std::size_t name = none;
    if (is_identifier()) name = take();
    if (name != none) bind_name(tokens_[name], NamespaceNameKind);
    expect("{");
    if (name != none) {
      namespace_path_.push_back(tokens_[name].text);
      namespace_inline_.push_back(inline_namespace);
    }
    const bool owns_name_scope = name != none;
    if (owns_name_scope) names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
    if (!namespace_path_.empty()) {
      std::string current_namespace;
      for (std::size_t i = 0; i < namespace_path_.size(); ++i) {
        if (!current_namespace.empty()) current_namespace += "::";
        current_namespace += namespace_path_[i];
      }
      import_namespace(current_namespace);
    }
    std::size_t ns = node(NNamespaceDefinition, name);
    if (inline_namespace) ast_.append(ns, node(NInlineMarker));
    while (!is("}")) {
      if (at_end()) fail("unterminated namespace");
      ast_.append(ns, parse_declaration(false));
    }
    expect("}");
    if (owns_name_scope) names_.scopes.pop_back();
    if (name != none) { namespace_path_.pop_back(); namespace_inline_.pop_back(); }
    return ns;
  }
  std::size_t parse_using()
  {
    take();
    bool directive = consume("namespace");
    if (!directive && is_identifier() && is("=", 1)) {
      std::size_t name = take(); expect("=");
      std::size_t alias = node(NAliasDeclaration, name);
      std::size_t target = parse_type_id();
      ast_.append(alias, target);
      bind_name(tokens_[name], TypeNameKind);
      const std::string target_name = type_id_display(target);
      if (!target_name.empty()) names_.set_type_alias(tokens_[name].text, target_name);
      expect(";");
      return alias;
    }
    ParsedName target = joined_name();
    expect(";");
    if (directive) {
      import_namespace(target.spelling);
      std::size_t n = node(NUsingDirective);
      ast_.append(n, composite_node(NTarget, target));
      return n;
    }
    NameKind imported = lookup_name(target.spelling);
    const std::size_t separator = target.spelling.rfind("::");
    if (imported != UnknownName)
      bind_name(separator == std::string::npos ? target.spelling : target.spelling.substr(separator + 2), imported);
    std::size_t n = node(NUsingDeclaration);
    ast_.append(n, composite_node(NTarget, target));
    return n;
  }
  std::size_t parse_type_id(bool allow_function_abstract = true)
  {
    std::size_t type = node(NTypeId), seq = node(NTypeSpecifierSeq);
    bool any = false;
    while (is_cv()) {
      ast_.append(seq, atom_node(NCvQualifier, take()));
      any = true;
    }
    if (is_keyword("class") || is_keyword("struct") || is_keyword("union")) {
      std::size_t key = take();
      skip_attribute_specifier();
      ParsedName name;
      if (is_identifier() || is("::")) name = joined_name();
      std::size_t elaborated = node(NClassForwardDeclaration, key);
      if (!name.spelling.empty()) set_composite_name(elaborated, name);
      ast_.append(elaborated, atom_node(NClassKey, key));
      ast_.append(seq, elaborated);
      any = true;
    } else if (is_keyword("enum")) {
      ast_.append(seq, parse_enum_specifier(true));
      any = true;
    } else if (is_keyword("decltype")) {
      ast_.append(seq, parse_decltype_specifier(NDecltypeSpecifier));
      any = true;
    } else if (is_keyword("typename")) {
      take();
      ast_.append(seq, composite_node(NTypeName, joined_name()));
      any = true;
    } else if (is_builtin_type()) {
      do { ast_.append(seq, atom_node(NTypeSpecifier, take())); }
      while (is_builtin_type());
      any = true;
    } else if (is_identifier() || is("::")) {
      ParsedName name = joined_name();
      if (is("<")) { take(); read_template_suffix(name); }
      ast_.append(seq, composite_node(NTypeName, name)); any = true;
    }
    if (!any) fail("expected type-id");
    ast_.append(type, seq);
    if (is("*") || is("&") || is("&&") || is("[") ||
        (allow_function_abstract && is("(")) || member_pointer_operator_ahead()) {
      std::size_t abstract = node(NAbstractDeclarator);
      while (member_pointer_operator_ahead())
        ast_.append(abstract, composite_node(NPtrOperator, parse_member_pointer_operator()));
      while (is("*") || is("&") || is("&&")) {
        ast_.append(abstract, atom_node(NPtrOperator, take()));
      }
      if (allow_function_abstract && is("(") && !looks_parameter_clause()) {
        expect("(");
        std::size_t nested = node(NNestedDeclarator);
        ast_.append(nested, parse_declarator(true));
        expect(")");
        ast_.append(abstract, nested);
      }
      for (;;) {
        if (is("(") && looks_parameter_clause()) {
          ast_.append(abstract, parse_parameter_clause());
        } else if (consume("[")) {
          std::size_t array = node(NArraySuffix);
          if (!is("]")) {
            ++template_expression_nesting_;
            ast_.append(array, parse_expression());
            --template_expression_nesting_;
          }
          expect("]"); ast_.append(abstract, array);
        } else break;
      }
      ast_.append(type, abstract);
    }
    return type;
  }
  bool type_starts_at(std::size_t offset) const
  {
    static const char* words[] = {"bool", "char", "char16_t", "char32_t", "double", "float",
      "int", "long", "short", "signed", "unsigned", "void", "wchar_t", "const", "volatile"};
    for (std::size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
      if (peek(offset).text == words[i]) return true;
    if (peek(offset).text == "class" || peek(offset).text == "struct" ||
        peek(offset).text == "union" || peek(offset).text == "enum" ||
        peek(offset).text == "typename" || peek(offset).text == "decltype") return true;
    return is_type_token(offset);
  }
  bool is_template_close() const
  {
    return pending_gt_ != 0 || is(">") || is(">>");
  }
  bool consume_template_close()
  {
    if (pending_gt_) {
      --pending_gt_;
      return true;
    }
    if (is(">")) { ++pos_; return true; }
    if (is(">>")) { ++pos_; ++pending_gt_; return true; }
    return false;
  }
  bool template_argument_terminator() const
  {
    if (template_argument_nesting_.empty() ||
        template_expression_nesting_ != template_argument_nesting_.back()) return false;
    if (is(">")) return true;
    return is(">>");
  }
  bool template_argument_starts_type() const
  {
    if (is("::")) return qualified_type_name_ahead();
    if (!type_starts_at(0)) return false;
    if (is_identifier() && is("::", 1) && !qualified_type_name_ahead()) return false;
    if (is_identifier() && template_id_followed_by_scope() &&
        !qualified_type_name_ahead()) return false;
    return true;
  }
  void read_template_suffix(ParsedName& name)
  {
    const std::size_t open = pos_ - 1;
    ensure_name_syntax(name);
    const std::size_t template_id = node(NTemplateIdSyntax);
    set_node_location_from_token(template_id, open);
    ast_.append(name.last_component, template_id);
    const std::size_t arguments = node(NTemplateArgumentList);
    ast_.append(template_id, arguments);

    if (!is_template_close()) {
      for (;;) {
        const bool type_argument = template_argument_starts_type();
        const std::size_t argument = node(type_argument ?
            NTypeTemplateArgument : NExpressionTemplateArgument);
        template_argument_nesting_.push_back(template_expression_nesting_);
        std::size_t value = type_argument ? parse_type_id() : parse_assignment();
        if (consume("...")) {
          const std::size_t expansion = node(type_argument ? NPackExpansion : NPackExpansionExpression);
          ast_.append(expansion, value);
          value = expansion;
        }
        template_argument_nesting_.pop_back();
        ast_.append(argument, value);
        ast_.append(arguments, argument);
        if (!consume(",")) break;
      }
    }
    if (!consume_template_close()) fail("unterminated template-id");

    std::string suffix;
    bool have_previous = false;
    bool previous_word = false;
    for (std::size_t i = open; i < pos_; ++i) {
      std::string text = tokens_[i].text;
      if (i + 1 == pos_ && pending_gt_) {
        std::size_t trim = pending_gt_;
        while (trim && !text.empty() && text[text.size() - 1] == '>') {
          text.erase(text.size() - 1);
          --trim;
        }
      }
      const bool current_word = is_word_token(tokens_[i]);
      if (!text.empty()) {
        if (have_previous && previous_word && current_word) suffix += ' ';
        suffix += text;
      }
      have_previous = have_previous || !text.empty();
      previous_word = current_word;
    }
    name.spelling += suffix;
  }
  std::size_t parse_template_declaration(bool in_class = false)
  {
    take(); expect("<");
    names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
    std::size_t clause = node(NTemplateParameterClause);
    if (!consume(">")) {
      std::size_t list = node(NTemplateParameterList);
      do { ast_.append(list, parse_template_parameter()); } while (consume(","));
      expect(">"); ast_.append(clause, list);
    }
    std::size_t declaration = parse_declaration(in_class);
    std::size_t templ = node(NTemplateDeclaration);
    ast_.append(templ, clause); ast_.append(templ, declaration);
    std::string declared = first_declaration_name(declaration);
    std::size_t angle = declared.find('<');
    if (angle != std::string::npos) declared.erase(angle);
    std::size_t scope_separator = declared.rfind("::");
    if (scope_separator != std::string::npos) declared.erase(0, scope_separator + 2);
    if (!declared.empty() && names_.scopes.size() > 1) {
      const NodeKind declaration_kind = ast_.nodes[declaration].kind;
      const NameKind name_kind = (declaration_kind == NClassSpecifier ||
          declaration_kind == NClassForwardDeclaration || declaration_kind == NEnumSpecifier ||
          declaration_kind == NAliasDeclaration) ? TemplateNameKind : TemplateFunctionNameKind;
      bind_name_in_scope(names_.scopes.size() - 2, declared, name_kind);
    }
    names_.scopes.pop_back();
    return templ;
  }
  std::string first_declaration_name(std::size_t root) const
  {
    const AstNode& n = ast_.nodes[root];
    if ((n.kind == NAliasDeclaration || n.kind == NNamespaceAliasDefinition) && n.atom != none)
      return tokens_[n.atom].text;
    if ((n.kind == NClassSpecifier || n.kind == NClassForwardDeclaration || n.kind == NEnumSpecifier) &&
        n.composite != none)
      return ast_.composite_atoms[n.composite];
    if (n.kind == NIdentifier) {
      if (n.composite != none) return ast_.composite_atoms[n.composite];
      if (n.atom != none) return tokens_[n.atom].text;
    }
    for (std::size_t c = n.first_child; c != none; c = ast_.nodes[c].next_sibling) {
      std::string name = first_declaration_name(c);
      if (!name.empty()) return name;
    }
    return std::string();
  }
  std::size_t parse_template_parameter()
  {
    if (is_keyword("template")) {
      take(); expect("<");
      std::size_t clause = node(NTemplateParameterClause);
      if (!consume(">")) {
        std::size_t list = node(NTemplateParameterList);
        do { ast_.append(list, parse_template_parameter()); } while (consume(","));
        expect(">"); ast_.append(clause, list);
      }
      if (!is_keyword("class") && !is_keyword("typename")) fail("expected template parameter key");
      std::size_t key = take(), p = node(NTypeParameter);
      ast_.append(p, node(NTemplateTemplateParameter));
      ast_.append(p, clause);
      ast_.append(p, atom_node(NParameterKey, key));
      if (is_identifier()) {
        std::size_t id = take(); ast_.append(p, atom_node(NIdentifier, id));
        bind_name(tokens_[id], TemplateNameKind);
      }
      if (consume("=")) {
        std::size_t d = node(NDefaultTemplateArgument);
        ast_.append(d, parse_type_id()); ast_.append(p, d);
      }
      return p;
    }
    const bool typename_parameter = is_keyword("typename") &&
        !(is_identifier(1) && (is("<", 2) || is("::", 2)));
    if (is_keyword("class") || typename_parameter) {
      std::size_t key = take();
      std::size_t p = node(NTypeParameter);
      ast_.append(p, atom_node(NParameterKey, key));
      if (consume("...")) ast_.append(p, node(NParameterPack, pos_ - 1));
      std::string name;
      if (is_identifier()) {
        std::size_t id = take(); name = tokens_[id].text; ast_.append(p, atom_node(NIdentifier, id));
        bind_name(name, TypeNameKind);
      }
      if (consume("=")) {
        std::size_t d = node(NDefaultTemplateArgument); ast_.append(d, parse_type_id()); ast_.append(p, d);
      }
      return p;
    }
    std::size_t p = node(NNonTypeTemplateParameter);
    std::size_t seq = parse_decl_specifier_seq(true); ast_.append(p, seq);
    if (consume("...")) ast_.append(p, node(NParameterPack, pos_ - 1));
    bool has_parameter_name = false;
    if (!is("=") && !is(",") && !is(">")) {
      std::size_t d = parse_declarator(false); ast_.append(p, d);
      std::size_t id = find_identifier_node(d);
      if (id != none) { bind_name(tokens_[id], ValueName); has_parameter_name = true; }
    }
    if (consume("=")) {
      std::size_t d = node(NDefaultTemplateArgument);
      ++template_non_type_default_;
      const std::size_t value = parse_assignment();
      --template_non_type_default_;
      const std::size_t type_specifier = ast_.nodes[seq].first_child;
      const bool unnamed_int_default = !has_parameter_name && type_specifier != none &&
          ast_.nodes[type_specifier].kind == NDeclSpecifier &&
          ast_.nodes[type_specifier].atom != none &&
          tokens_[ast_.nodes[type_specifier].atom].text == "int";
      if (unnamed_int_default && ast_.nodes[value].kind == NLiteral)
        ast_.nodes[value].kind = NTaggedLiteral;
      ast_.append(d, value);
      ast_.append(p, d);
    }
    return p;
  }
  ParsedName read_class_name()
  {
    if (!is_identifier()) fail("expected class name");
    ParsedName name;
    name.first_token = name.source_token = take();
    name.spelling = tokens_[name.first_token].text;
    if (is("<")) { take(); read_template_suffix(name); }
    return name;
  }
  std::size_t parse_class()
  {
    std::size_t n = parse_class_specifier(false);
    if (ast_.nodes[n].kind != NClassForwardDeclaration) expect(";");
    return n;
  }
  std::size_t parse_class_specifier(bool embedded)
  {
    std::size_t key = take();
    skip_alignas_specifier();
    skip_attribute_specifier();
    ParsedName name;
    if (is_identifier()) name = read_class_name();
    const std::string registry_name = name.spelling.empty() ? std::string() : qualified_class_name(name.spelling);
    if (!embedded && is(";")) {
      take();
      if (!name.spelling.empty()) bind_name(name.spelling, TypeNameKind);
      std::size_t n = node(NClassForwardDeclaration, key);
      if (!name.spelling.empty()) set_composite_name(n, name);
      std::size_t keynode = atom_node(NClassKey, key); ast_.append(n, keynode);
      return n;
    }
    std::size_t n = node(NClassSpecifier, key);
    if (!name.spelling.empty()) set_composite_name(n, name);
    ast_.append(n, atom_node(NClassKey, key));
    if (!name.spelling.empty()) bind_name(name.spelling, TypeNameKind);
    if (consume(":")) {
      const std::size_t bases = parse_base_clause();
      ast_.append(n, bases);
      if (!registry_name.empty()) remember_class_bases(registry_name, bases);
    }
    if (!is("{")) {
      if (embedded) {
        ast_.nodes[n].kind = NClassForwardDeclaration;
        return n;
      }
      fail("expected class body");
    }
    expect("{");
    names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
    class_context_names_.push_back(registry_name);
    class_scope_indices_.push_back(names_.scopes.size() - 1);
    std::unordered_map<std::string, std::vector<std::string> >::const_iterator inherited =
        names_.class_bases.find(class_lookup_key(registry_name));
    if (inherited != names_.class_bases.end()) {
      std::unordered_set<std::string> visited;
      for (std::size_t i = 0; i < inherited->second.size(); ++i)
        import_class_members_recursive(inherited->second[i], visited);
    }
    predeclare_class_types();
    while (!consume("}")) {
      if (at_end()) fail("unterminated class definition");
      if (skip_attribute_specifier()) continue;
      if (consume(";")) { ast_.append(n, node(NEmptyDeclaration)); continue; }
      if ((is_keyword("public") || is_keyword("protected") || is_keyword("private")) && is(":", 1)) {
        ast_.append(n, atom_node(NAccessSpecifier, take())); expect(":"); continue;
      }
      if (is_identifier() && peek().text == class_name_leaf(name.spelling) && is("(", 1)) {
        ast_.append(n, parse_special_member(name.spelling)); continue;
      }
      if (is_keyword("virtual") && (is("~", 1) || is_keyword("operator", 1) ||
          (is_identifier(1) && peek(1).text == class_name_leaf(name.spelling)))) {
        std::size_t specifier = take();
        skip_attribute_specifier();
        ast_.append(n, parse_special_member(name.spelling, specifier)); continue;
      }
      if (is("~") || is_keyword("operator")) {
        ast_.append(n, parse_special_member(name.spelling)); continue;
      }
      if (is_keyword("template")) {
        ast_.append(n, parse_template_declaration(true)); continue;
      }
      if (is_keyword("using")) {
        ast_.append(n, parse_using()); continue;
      }
      if (is_keyword("static_assert")) {
        ast_.append(n, parse_static_assert()); continue;
      }
      if (friend_class_declaration_ahead()) {
        ast_.append(n, parse_friend_class_declaration()); continue;
      }
      if ((is_keyword("class") || is_keyword("struct") || is_keyword("union")) &&
          class_definition_or_forward_ahead()) {
        ast_.append(n, parse_class()); continue;
      }
      if (is_keyword("enum") && enum_declaration_ahead()) {
        ast_.append(n, parse_enum()); continue;
      }
      if (!starts_decl_specifier()) fail("expected class member declaration");
      std::size_t spec = parse_decl_specifier_seq(true);
      if (is(":") || has_colon_before_comma_or_semicolon())
        ast_.append(n, parse_bit_field(spec));
      else
        ast_.append(n, parse_simple_after_spec(spec));
    }
    if (name.spelling.empty() && tokens_[key].text == "union" && is(";"))
      ast_.nodes[n].source_end_token_index = pos_;
    names_.scopes.pop_back();
    class_context_names_.pop_back();
    class_scope_indices_.pop_back();
    return n;
  }
  std::string class_name_leaf(const std::string& name) const
  {
    std::string leaf = name;
    std::size_t angle = leaf.find('<');
    if (angle != std::string::npos) leaf.erase(angle);
    std::size_t scope = leaf.rfind("::");
    if (scope != std::string::npos) leaf.erase(0, scope + 2);
    return leaf;
  }
  void predeclare_class_types()
  {
    int braces = 0;
    for (std::size_t i = pos_; has_token(i);) {
      const std::string& text = tokens_[i].text;
      if (braces == 0 && text == "}") break;
      if (braces == 0 && text == "template" && has_token(i + 1) && tokens_[i + 1].text == "<") {
        i += 2;
        int angles = 1;
        while (has_token(i) && angles) {
          if (tokens_[i].text == "<") ++angles;
          else if (tokens_[i].text == ">") --angles;
          else if (tokens_[i].text == ">>" && angles >= 2) angles -= 2;
          ++i;
        }
        continue;
      }
      if (braces == 0 && (text == "class" || text == "struct" || text == "union" || text == "enum")) {
        std::size_t name = i + 1;
        if (text == "enum" && has_token(name) &&
            (tokens_[name].text == "class" || tokens_[name].text == "struct")) ++name;
        if (text != "enum" && has_token(name) && tokens_[name].text == "alignas") {
          ++name;
          if (has_token(name) && tokens_[name].text == "(") {
            int parens = 0;
            do {
              if (tokens_[name].text == "(") ++parens;
              else if (tokens_[name].text == ")") --parens;
              ++name;
            } while (has_token(name) && parens);
          }
        }
        if (has_token(name) && tokens_[name].category == IdentifierToken)
          bind_name(tokens_[name], TypeNameKind);
      }
      if (braces == 0 && text == "using" && has_token(i + 2) &&
          tokens_[i + 1].category == IdentifierToken && tokens_[i + 2].text == "=")
        bind_name(tokens_[i + 1], TypeNameKind);
      if (braces == 0 && text == "typedef") {
        std::vector<std::size_t> names;
        std::size_t end;
        ScanTypedefDeclarationNames(tokens_, i + 1, names, end,
            [this](std::size_t token) { return has_token(token); });
        for (std::size_t n = 0; n < names.size(); ++n)
          bind_name(tokens_[names[n]], TypeNameKind);
        if (end > i) i = end;
        continue;
      }
      if (text == "{") ++braces;
      else if (text == "}" && braces) --braces;
      ++i;
    }
  }
  bool class_definition_or_forward_ahead() const
  {
    std::size_t i = pos_ + 1;
    if (has_token(i) && (tokens_[i].text == "alignas")) {
      ++i;
      if (!has_token(i) || tokens_[i].text != "(") return false;
      int parens = 0;
      do {
        if (tokens_[i].text == "(") ++parens;
        else if (tokens_[i].text == ")") --parens;
        ++i;
      } while (has_token(i) && parens);
    }
    if (has_token(i + 1) && tokens_[i].text == "[" && tokens_[i + 1].text == "[") {
      i += 2; int brackets = 2;
      while (has_token(i) && brackets) {
        if (tokens_[i].text == "[") ++brackets;
        else if (tokens_[i].text == "]") --brackets;
        ++i;
      }
    }
    if (has_token(i) && tokens_[i].category == IdentifierToken) ++i;
    if (has_token(i) && tokens_[i].text == "<") {
      int angles = 0, parens = 0, brackets = 0, braces = 0;
      do {
        const std::string& text = tokens_[i].text;
        if (text == "(") ++parens;
        else if (text == ")" && parens) --parens;
        else if (text == "[") ++brackets;
        else if (text == "]" && brackets) --brackets;
        else if (text == "{") ++braces;
        else if (text == "}" && braces) --braces;
        else if (!parens && !brackets && !braces && text == "<") ++angles;
        else if (!parens && !brackets && !braces && text == ">" && angles) --angles;
        else if (!parens && !brackets && !braces && text == ">>" && angles)
          angles = angles >= 2 ? angles - 2 : 0;
        ++i;
      } while (has_token(i) && angles > 0);
    }
    if (has_token(i) && tokens_[i].text == ":") {
      while (has_token(i) && tokens_[i].text != "{" && tokens_[i].text != ";") ++i;
    }
    if (!has_token(i)) return false;
    if (tokens_[i].text == ";") return true;
    if (tokens_[i].text != "{") return false;
    int braces = 1; ++i;
    while (has_token(i) && braces) {
      if (tokens_[i].text == "{") ++braces;
      else if (tokens_[i].text == "}") --braces;
      ++i;
    }
    return braces == 0 && has_token(i) && tokens_[i].text == ";";
  }
  bool enum_declaration_ahead() const
  {
    std::size_t i = pos_ + 1;
    if (has_token(i) && (tokens_[i].text == "class" || tokens_[i].text == "struct")) ++i;
    if (has_token(i) && tokens_[i].text == "::") ++i;
    if (has_token(i) && tokens_[i].category == IdentifierToken) {
      ++i;
      while (has_token(i + 1) && tokens_[i].text == "::" &&
             tokens_[i + 1].category == IdentifierToken) i += 2;
    }
    if (has_token(i) && tokens_[i].text == ":") {
      while (has_token(i) && tokens_[i].text != "{" && tokens_[i].text != ";") ++i;
    }
    if (!has_token(i)) return false;
    if (tokens_[i].text == ";") return true;
    if (tokens_[i].text != "{") return false;
    int braces = 1; ++i;
    while (has_token(i) && braces) {
      if (tokens_[i].text == "{") ++braces;
      else if (tokens_[i].text == "}") --braces;
      ++i;
    }
    return braces == 0 && has_token(i) && tokens_[i].text == ";";
  }
  bool has_colon_before_comma_or_semicolon() const
  {
    int parens = 0, brackets = 0, braces = 0, conditionals = 0;
    for (std::size_t i = pos_; has_token(i); ++i) {
      const std::string& s = tokens_[i].text;
      if (parens == 0 && brackets == 0 && braces == 0 && (s == "{" || s == "}")) return false;
      if (s == "(" ) ++parens; else if (s == ")") { if (parens) --parens; }
      else if (s == "[") ++brackets; else if (s == "]") { if (brackets) --brackets; }
      else if (s == "{") ++braces; else if (s == "}") { if (braces) --braces; else return false; }
      if (parens || brackets || braces) continue;
      if (s == "?") ++conditionals;
      else if (s == ":") { if (conditionals) --conditionals; else return true; }
      else if (s == "=" || s == ";") return false;
    }
    return false;
  }
  std::size_t parse_base_clause()
  {
    std::size_t clause = node(NBaseClause);
    do {
      std::size_t base = node(NBaseSpecifier);
      if (is_keyword("virtual")) ast_.append(base, atom_node(NVirtualBase, take()));
      if (is_keyword("public") || is_keyword("protected") || is_keyword("private"))
        ast_.append(base, atom_node(NAccessSpecifier, take()));
      if (is_keyword("virtual")) ast_.append(base, atom_node(NVirtualBase, take()));
      std::size_t name = composite_node(NBaseName, joined_name());
      ast_.append(base, name);
      if (consume("...")) ast_.append(base, atom_node(NPackExpansion, pos_ - 1));
      ast_.append(clause, base);
    } while (consume(","));
    return clause;
  }
  std::size_t parse_bit_field(std::size_t spec)
  {
    std::size_t field = node(NBitFieldDeclaration); ast_.append(field, spec);
    for (;;) {
      std::size_t item = node(NBitFieldDeclarator);
      if (!is(":")) ast_.append(item, parse_declarator(false));
      expect(":"); ast_.append(item, parse_assignment()); ast_.append(field, item);
      if (!consume(",")) break;
    }
    expect(";"); return field;
  }
  std::size_t parse_special_member(const std::string& class_name, std::size_t leading_specifier = none)
  {
    const std::size_t member_source = pos_;
    std::string name;
    ParsedName operator_name;
    bool has_operator_name = false;
    if (consume("~")) {
      name = "~";
      if (!is_identifier()) fail("expected destructor name");
      name += tokens_[take()].text;
    } else if (is_keyword("operator")) {
      operator_name.operator_tokens_begin = pos_;
      operator_name.spelling = parse_operator_name();
      operator_name.operator_tokens_end = pos_;
      operator_name.first_token = operator_name.source_token =
          operator_name.operator_tokens_begin;
      if (operator_name.spelling.compare(0, 9, "operator ") == 0)
        operator_name.spelling.erase(8, 1);
      name = operator_name.spelling;
      has_operator_name = true;
    } else {
      if (!is_identifier()) fail("expected special member name");
      name = tokens_[take()].text;
    }
    std::size_t member = composite_node(NSpecialMemberDefinition, name, member_source);
    if (leading_specifier != none) {
      std::size_t specs = node(NMemberSpecifiers);
      ast_.append(specs, atom_node(NSpecifier, leading_specifier));
      ast_.append(member, specs);
    }
    std::size_t decl = node(NDeclarator);
    ast_.append(decl, has_operator_name ?
        composite_node(NIdentifier, operator_name) :
        composite_node(NIdentifier, name, member_source));
    ast_.append(decl, parse_parameter_clause());
    ast_.append(member, decl);
    while (is_cv() || is("&") || is("&&") || is_keyword("noexcept") || is_keyword("throw") ||
           (is_identifier() && (is("override", 0) || is("final", 0)))) {
      if (is_cv()) ast_.append(decl, atom_node(NCvQualifier, take()));
      else if (is("&") || is("&&")) ast_.append(decl, atom_node(NRefQualifier, take()));
      else if (is_keyword("noexcept")) {
        ast_.append(decl, parse_noexcept_qualifier());
      } else if (is_keyword("throw")) {
        ast_.append(decl, parse_throw_specification());
      } else take();
    }
    if (consume(";")) { ast_.nodes[member].kind = NSpecialMemberDeclaration; return member; }
    if (is_keyword("try")) {
      ast_.append(member, parse_function_try_block(true));
      return member;
    }
    if (consume(":")) ast_.append(member, parse_ctor_initializer());
    ast_.append(member, parse_compound_statement());
    (void)class_name;
    return member;
  }
  bool qualified_special_member_ahead(std::size_t offset = 0) const
  {
    if (peek(offset).category != IdentifierToken) return false;
    std::string first = peek(offset).text;
    std::size_t i = pos_ + offset + 1;
    if (has_token(i) && tokens_[i].text == "<") {
      int angles = 0;
      do {
        if (tokens_[i].text == "<") ++angles;
        else if (tokens_[i].text == ">") --angles;
        else if (tokens_[i].text == ">>" && angles >= 2) angles -= 2;
        ++i;
      } while (has_token(i) && angles > 0);
    }
    std::size_t angle = first.find('<');
    if (angle != std::string::npos) first.erase(angle);
    while (has_token(i + 1) && tokens_[i].text == "::") {
      ++i;
      if (tokens_[i].text == "~") return true;
      if (tokens_[i].text == "operator") return conversion_type_starts_at(i + 1);
      if (tokens_[i].category == IdentifierToken && tokens_[i].text == first &&
          has_token(i + 1) && tokens_[i + 1].text == "(") return true;
      if (tokens_[i].category != IdentifierToken) return false;
      ++i;
      if (has_token(i) && tokens_[i].text == "<") {
        int angles = 0;
        do {
          if (tokens_[i].text == "<") ++angles;
          else if (tokens_[i].text == ">") --angles;
          else if (tokens_[i].text == ">>" && angles >= 2) angles -= 2;
          ++i;
        } while (has_token(i) && angles > 0);
      }
    }
    return false;
  }
  bool conversion_type_starts_at(std::size_t i) const
  {
    if (!has_token(i)) return false;
    const std::string& text = tokens_[i].text;
    if (text == "new" || text == "delete" || text == "[" || text == "(") return false;
    if (tokens_[i].category == IdentifierToken || text == "::") return true;
    static const char* types[] = {"bool", "char", "char16_t", "char32_t", "const", "decltype",
      "double", "float", "int", "long", "short", "signed", "struct", "class", "typename",
      "union", "unsigned", "void", "volatile", "wchar_t"};
    for (std::size_t j = 0; j < sizeof(types) / sizeof(types[0]); ++j)
      if (text == types[j]) return true;
    return false;
  }
  std::size_t parse_qualified_special_member_definition(std::size_t leading_specifier = none)
  {
    std::string name = "";
    std::size_t declarator_id = parse_declarator_id();
    if (ast_.nodes[declarator_id].composite != none)
      name = ast_.composite_atoms[ast_.nodes[declarator_id].composite];
    std::size_t member = composite_node(NSpecialMemberDefinition, name);
    if (leading_specifier != none) {
      std::size_t specifiers = node(NMemberSpecifiers);
      ast_.append(specifiers, atom_node(NSpecifier, leading_specifier));
      ast_.append(member, specifiers);
    }
    std::size_t declarator = node(NDeclarator);
    ast_.append(declarator, declarator_id);
    ast_.append(declarator, parse_parameter_clause());
    while (is_cv() || is("&") || is("&&") || is_keyword("noexcept") || is_keyword("throw")) {
      if (is_cv()) ast_.append(declarator, atom_node(NCvQualifier, take()));
      else if (is("&") || is("&&")) ast_.append(declarator, atom_node(NRefQualifier, take()));
      else if (is_keyword("throw")) ast_.append(declarator, parse_throw_specification());
      else {
        ast_.append(declarator, parse_noexcept_qualifier());
      }
    }
    ast_.append(member, declarator);
    if (is_keyword("try")) ast_.append(member, parse_function_try_block(true));
    else if (consume(":")) {
      ast_.append(member, parse_ctor_initializer());
      ast_.append(member, parse_compound_statement());
    } else ast_.append(member, parse_compound_statement());
    return member;
  }
  std::size_t parse_ctor_initializer()
  {
    std::size_t init = node(NCtorInitializer);
    do {
      std::size_t item = node(NMemInitializer);
      ParsedName name = joined_name();
      ast_.append(item, composite_node(NMemInitializerId, name));
      if (consume("(")) {
        std::size_t args = node(NParenArgumentList);
        if (!consume(")")) {
          do { ast_.append(args, parse_initializer_clause()); } while (consume(","));
          expect(")");
        }
        ast_.append(item, args);
      } else if (is("{")) {
        ast_.append(item, parse_braced_init());
      } else fail("expected member initializer");
      if (consume("...")) ast_.append(item, atom_node(NPackExpansion, pos_ - 1));
      ast_.append(init, item);
    } while (consume(","));
    return init;
  }
  std::size_t parse_throw_specification()
  {
    take(); expect("(");
    std::string text = "throw(";
    int depth = 0;
    bool previous_word = false, have_previous = false;
    while (!at_end() && !(is(")") && depth == 0)) {
      const Token current = peek();
      if (current.text == "(") ++depth;
      else if (current.text == ")") --depth;
      if (have_previous && previous_word && is_word_token(current)) text += ' ';
      text += current.text;
      previous_word = is_word_token(current); have_previous = true;
      take();
    }
    expect(")"); text += ')';
    return composite_node(NFunctionQualifier, text);
  }
  std::size_t parse_noexcept_qualifier()
  {
    take();
    std::string text = "noexcept";
    std::size_t expression = none;
    if (consume("(")) {
      const std::size_t begin = pos_;
      expression = parse_expression();
      const std::size_t end = pos_;
      expect(")");
      text += '(';
      bool previous_word = false;
      for (std::size_t i = begin; i < end; ++i) {
        if (i != begin && previous_word && is_word_token(tokens_[i])) text += ' ';
        text += tokens_[i].text;
        previous_word = is_word_token(tokens_[i]);
      }
      text += ')';
    }
    const std::size_t result = composite_node(NFunctionQualifier, text);
    if (expression != none) ast_.append(result, expression);
    return result;
  }
  std::size_t parse_function_try_block(bool constructor)
  {
    expect("try");
    std::size_t result = node(NFunctionTryBlock);
    if (constructor && consume(":")) ast_.append(result, parse_ctor_initializer());
    ast_.append(result, parse_compound_statement());
    if (!is_keyword("catch")) fail("expected function try handler");
    do {
      take(); expect("(");
      std::size_t handler = node(NHandler), exception = node(NExceptionDeclaration);
      if (consume("...")) ast_.append(exception, node(NEllipsis, pos_ - 1));
      else {
        ast_.append(exception, parse_decl_specifier_seq(true));
        if (!is(")")) ast_.append(exception, parse_declarator(true));
      }
      expect(")"); ast_.append(handler, exception);
      ast_.append(handler, parse_compound_statement()); ast_.append(result, handler);
    } while (is_keyword("catch"));
    return result;
  }
  std::size_t parse_enum()
  {
    std::size_t n = parse_enum_specifier(false);
    expect(";");
    return n;
  }
  std::size_t parse_enum_specifier(bool embedded)
  {
    take();
    std::size_t key = none;
    if (is_keyword("class") || is_keyword("struct")) key = take();
    ParsedName parsed_name;
    std::string name;
    if (is_identifier() || is("::")) {
      parsed_name = joined_name();
      name = parsed_name.spelling;
    }
    std::size_t n = node(NEnumSpecifier);
    if (!name.empty()) set_composite_name(n, parsed_name);
    if (key != none) ast_.append(n, atom_node(NEnumKey, key));
    if (!name.empty() && name.find("::") == std::string::npos) bind_name(name, TypeNameKind);
    if (consume(":")) ast_.append(n, parse_type_id());
    if (!is("{")) {
      (void)embedded;
      return n;
    }
    expect("{");
    while (!consume("}")) {
      if (!is_identifier()) fail("expected enumerator");
      std::size_t id = take(), e = node(NEnumerator, id);
      if (key == none) bind_name(tokens_[id], ValueName);
      if (consume("=")) ast_.append(e, parse_assignment());
      ast_.append(n, e);
      if (!consume(",")) { expect("}"); break; }
    }
    return n;
  }
  std::size_t parse_static_assert()
  {
    take(); expect("(");
    std::size_t n = node(NStaticAssertDeclaration);
    ast_.append(n, parse_assignment());
    if (consume(",")) {
      if (!is_literal()) fail("expected static_assert message literal");
      ast_.append(n, node(NMessage, take()));
    }
    expect(")"); expect(";");
    return n;
  }

  std::size_t parse_simple_or_function()
  {
    std::size_t spec = parse_decl_specifier_seq(true);
    return parse_simple_after_spec(spec);
  }
  std::size_t parse_simple_after_spec(std::size_t spec)
  {
    names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
    bool is_typedef = contains_decl_specifier(spec, "typedef");
    const std::string typedef_target = is_typedef ? declared_type_name(spec) : std::string();
    std::vector<std::size_t> init_nodes;
    std::size_t decl = none;
    if (!is(";")) {
      decl = parse_declarator(false);
      if ((is("{") || is_keyword("try")) && contains_node_kind(decl, NParameterClause)) {
        std::size_t f = node(NFunctionDefinition);
        ast_.append(f, spec); ast_.append(f, decl);
        import_class_members_for_declarator(decl);
        std::size_t function_name = find_identifier_node(decl);
        if (function_name != none) bind_name(tokens_[function_name], ValueName);
        if (is_keyword("try")) ast_.append(f, parse_function_try_block(false));
        else ast_.append(f, parse_compound_statement());
        names_.scopes.pop_back();
        return f;
      }
      init_nodes.push_back(parse_init_declarator_tail(decl));
      while (consume(",")) init_nodes.push_back(parse_init_declarator_tail(parse_declarator(false)));
    }
    expect(";");
    for (std::size_t i = 0; i < init_nodes.size(); ++i) {
      std::size_t declarator = ast_.nodes[init_nodes[i]].first_child;
      std::size_t identifier = find_identifier_node(declarator);
      if (identifier != none)
        bind_name_in_scope(names_.scopes.size() - 2, tokens_[identifier],
                           is_typedef ? TypeNameKind : ValueName);
      if (is_typedef && identifier != none && !typedef_target.empty())
        names_.set_type_alias(tokens_[identifier].text, typedef_target);
    }
    names_.scopes.pop_back();
    std::size_t simple = node(NSimpleDeclaration);
    ast_.append(simple, spec);
    if (!init_nodes.empty()) {
      std::size_t list = node(NInitDeclaratorList);
      for (std::size_t i = 0; i < init_nodes.size(); ++i) ast_.append(list, init_nodes[i]);
      ast_.append(simple, list);
    }
    return simple;
  }
  bool contains_decl_specifier(std::size_t seq, const std::string& text) const
  {
    for (std::size_t c = ast_.nodes[seq].first_child; c != none; c = ast_.nodes[c].next_sibling)
      if (ast_.nodes[c].atom != none && tokens_[ast_.nodes[c].atom].text == text) return true;
    return false;
  }
  std::string declared_type_name(std::size_t seq) const
  {
    for (std::size_t child = ast_.nodes[seq].first_child; child != none;
         child = ast_.nodes[child].next_sibling) {
      if (ast_.nodes[child].kind == NDeclSpecifier && ast_.nodes[child].atom != none) {
        const Token& token = tokens_[ast_.nodes[child].atom];
        if (token.text != "typedef" && token.text != "const" && token.text != "volatile")
          return token.text;
      }
      if (ast_.nodes[child].composite != none && ast_.nodes[child].kind == NDeclSpecifier)
        return ast_.composite_atoms[ast_.nodes[child].composite];
      if ((ast_.nodes[child].kind == NClassSpecifier || ast_.nodes[child].kind == NEnumSpecifier) &&
          ast_.nodes[child].composite != none)
        return ast_.composite_atoms[ast_.nodes[child].composite];
    }
    return std::string();
  }
  bool contains_node_kind(std::size_t root, NodeKind kind) const
  {
    if (root == none) return false;
    if (ast_.nodes[root].kind == kind) return true;
    for (std::size_t c = ast_.nodes[root].first_child; c != none; c = ast_.nodes[c].next_sibling)
      if (contains_node_kind(c, kind)) return true;
    return false;
  }
  std::size_t parse_init_declarator_tail(std::size_t declarator)
  {
    std::size_t init = node(NInitDeclarator);
    ast_.append(init, declarator);
    if (consume("=")) {
      std::size_t initializer = node(NInitializer);
      if (is_keyword("default") || is_keyword("delete"))
        ast_.append(initializer, node(NSpecialInitializer, take()));
      else
        ast_.append(initializer, parse_initializer_clause());
      ast_.append(init, initializer);
    } else if (is("{")) {
      std::size_t initializer = node(NInitializer);
      ast_.append(initializer, parse_braced_init());
      ast_.append(init, initializer);
    } else if (is("(")) {
      std::size_t initializer = node(NInitializer);
      ast_.append(initializer, parse_paren_initializer());
      ast_.append(init, initializer);
    } else if (is_keyword("default") || is_keyword("delete")) {
      ast_.append(init, node(NSpecialInitializer, take()));
    }
    return init;
  }
  bool member_pointer_operator_ahead() const
  {
    std::size_t i = pos_;
    if (has_token(i) && tokens_[i].text == "::") ++i;
    if (!has_token(i) || tokens_[i].category != IdentifierToken) return false;
    ++i;
    for (;;) {
      if (has_token(i) && tokens_[i].text == "<") {
        int depth = 0;
        do {
          if (tokens_[i].text == "<") ++depth;
          else if (tokens_[i].text == ">") --depth;
          else if (tokens_[i].text == ">>" && depth >= 2) depth -= 2;
          ++i;
        } while (has_token(i) && depth > 0);
      }
      if (!has_token(i) || tokens_[i].text != "::") return false;
      if (has_token(i + 1) && tokens_[i + 1].text == "*") return true;
      if (!has_token(i + 1) || tokens_[i + 1].category != IdentifierToken) return false;
      i += 2;
    }
  }
  ParsedName parse_member_pointer_operator()
  {
    ParsedName name;
    if (consume("::")) {
      name.global_scope = true;
      name.source_token = pos_ - 1;
      name.spelling = "::";
    }
    if (!is_identifier()) fail("expected member pointer class name");
    const std::size_t first = take();
    name.first_token = first;
    if (name.source_token == none) name.source_token = first;
    name.spelling += tokens_[first].text;
    if (is("<") && template_id_candidate(name.spelling)) { take(); read_template_suffix(name); }
    while (consume("::")) {
      name.spelling += "::";
      if (is("*")) break;
      if (!is_identifier()) fail("expected member pointer qualifier");
      const std::size_t component = take();
      name.spelling += tokens_[component].text;
      add_name_component(name, component);
      if (is("<") && template_id_candidate(name.spelling)) { take(); read_template_suffix(name); }
    }
    expect("*");
    name.spelling += "*";
    return name;
  }
  std::size_t parse_declarator(bool abstract_ok)
  {
    std::size_t d = node(NDeclarator);
    while (member_pointer_operator_ahead())
      ast_.append(d, composite_node(NPtrOperator, parse_member_pointer_operator()));
    while (is("*") || is("&") || is("&&")) {
      std::size_t op = take();
      ast_.append(d, atom_node(NPtrOperator, op));
      while (is_cv()) ast_.append(d, atom_node(NCvQualifier, take()));
    }
    if (is("(") && !(abstract_ok && looks_parameter_clause())) {
      take();
      std::size_t nested = node(NNestedDeclarator);
      ast_.append(nested, parse_declarator(abstract_ok));
      expect(")");
      ast_.append(d, nested);
    } else if (consume("...")) {
      ast_.append(d, node(NParameterPack, pos_ - 1));
      if (is_identifier()) ast_.append(d, atom_node(NIdentifier, take()));
      else if (!abstract_ok) fail("expected declarator name after pack");
    } else if (is_keyword("operator")) {
      ParsedName name;
      name.operator_tokens_begin = pos_;
      name.spelling = parse_operator_name();
      name.operator_tokens_end = pos_;
      name.first_token = name.source_token = name.operator_tokens_begin;
      ast_.append(d, composite_node(NIdentifier, name));
    } else if (is_identifier() || is("::")) {
      ast_.append(d, parse_declarator_id());
    } else if (!abstract_ok) {
      fail("expected declarator name");
    }
    for (;;) {
      if (skip_attribute_specifier()) {
        continue;
      } else if (is("(") && looks_parameter_clause()) {
        ast_.append(d, parse_parameter_clause());
      } else if (consume("[")) {
        std::size_t array = node(NArraySuffix);
        if (!is("]")) {
          ++template_expression_nesting_;
          ast_.append(array, parse_expression());
          --template_expression_nesting_;
        }
        expect("]");
        ast_.append(d, array);
      } else if (is_cv()) {
        ast_.append(d, atom_node(NCvQualifier, take()));
      } else if (is("&") || is("&&")) {
        ast_.append(d, atom_node(NRefQualifier, take()));
      } else if (is_keyword("noexcept")) {
        ast_.append(d, parse_noexcept_qualifier());
      } else if (is_keyword("throw")) {
        ast_.append(d, parse_throw_specification());
      } else if (is("override") || is("final")) {
        std::size_t v = take();
        ast_.append(d, atom_node(NVirtSpecifier, v));
      } else if (consume("...")) {
        ast_.append(d, node(NParameterPack, pos_ - 1));
      } else if (is("->") && contains_node_kind(d, NParameterClause)) {
        take();
        std::size_t trailing = node(NTrailingReturnType);
        std::size_t type = parse_type_id();
        std::string display = type_id_display(type);
        if (!display.empty()) {
          ast_.composite_atoms.push_back(display);
          ast_.nodes[trailing].composite = ast_.composite_atoms.size() - 1;
        }
        ast_.append(trailing, type); ast_.append(d, trailing);
      } else break;
    }
    return d;
  }
  std::size_t parse_declarator_id()
  {
    ParsedName name;
    bool qualified = consume("::");
    if (qualified) {
      name.global_scope = true;
      name.source_token = pos_ - 1;
      name.spelling = "::";
    }
    if (!is_identifier()) fail("expected declarator identifier");
    std::size_t first = take();
    name.first_token = first;
    if (name.source_token == none) name.source_token = first;
    name.spelling += tokens_[first].text;
    if (is("<") && template_id_candidate(name.spelling)) { take(); read_template_suffix(name); qualified = true; }
    while (consume("::")) {
      qualified = true; name.spelling += "::";
      if (is_keyword("template")) { name.spelling += "template "; take(); }
      if (is_keyword("operator")) {
        const std::size_t operator_token = pos_;
        const std::string operator_name = parse_operator_name();
        name.spelling += operator_name;
        name.operator_tokens_begin = operator_token;
        name.operator_tokens_end = pos_;
        add_name_component(name, operator_token);
      } else if (consume("~")) {
        name.spelling += "~";
        if (!is_identifier()) fail("expected destructor identifier");
        const std::size_t component = take();
        name.spelling += tokens_[component].text;
        add_name_component(name, component);
      } else {
        if (!is_identifier()) fail("expected qualified declarator identifier");
        const std::size_t component = take();
        name.spelling += tokens_[component].text;
        add_name_component(name, component);
        if (is("<") && template_id_candidate(name.spelling)) { take(); read_template_suffix(name); }
      }
    }
    if (qualified) return composite_node(NIdentifier, name);
    if (name.spelling != tokens_[first].text)
      return composite_node(NIdentifier, name);
    return atom_node(NIdentifier, first);
  }
  std::string parse_operator_name()
  {
    expect("operator");
    std::string name = "operator";
    if (consume("[")) { expect("]"); return name + "[]"; }
    if (consume("(")) { expect(")"); return name + "()"; }
    if (is_keyword("new") || is_keyword("delete")) {
      name += peek().text; take();
      if (consume("[")) { expect("]"); name += "[]"; }
      return name;
    }
    if (is_literal()) {
      name += peek().text; take();
      if (is_identifier()) { name += peek().text; take(); }
      return name;
    }
    if (is_identifier() || is("::") || is_builtin_type() || is_cv() ||
        is_keyword("typename") || is_keyword("decltype") || is_keyword("class") ||
        is_keyword("struct") || is_keyword("union"))
      return parse_conversion_operator_name();
    if (is_identifier() || peek().category == KeywordToken) {
      name += ' '; name += peek().text; take();
      return name;
    }
    if (peek().category == PunctuatorToken) {
      name += peek().text; take();
      return name;
    }
    fail("expected overloaded operator name");
    return name;
  }
  std::string parse_conversion_operator_name()
  {
    std::string name = "operator ";
    int angles = 0, parens = 0;
    bool previous_word = false, consumed = false;
    std::string previous;
    while (!at_end()) {
      const Token& current = peek();
      if (consumed && angles == 0 && parens == 0) {
        if (current.text == "(" && previous != "decltype") break;
        bool continuation = current.text == "::" || current.text == "<" ||
            current.text == "*" || current.text == "&" || current.text == "&&" ||
            current.text == "const" || current.text == "volatile" ||
            previous == "::" || previous == "typename" || previous == "const" ||
            previous == "volatile";
        static const char* builtins[] = {"bool", "char", "char16_t", "char32_t", "double",
          "float", "int", "long", "short", "signed", "unsigned", "void", "wchar_t"};
        bool current_builtin = false, previous_builtin = false;
        for (std::size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
          if (current.text == builtins[i]) current_builtin = true;
          if (previous == builtins[i]) previous_builtin = true;
        }
        continuation = continuation || (current_builtin && previous_builtin);
        if (!continuation) break;
      }
      if (current.text == "(") ++parens;
      else if (current.text == ")" && parens) --parens;
      else if (!parens && current.text == "<") ++angles;
      else if (!parens && current.text == ">" && angles) --angles;
      else if (!parens && current.text == ">>" && angles)
        angles = angles >= 2 ? angles - 2 : 0;
      if (previous_word && is_word_token(current)) name += ' ';
      name += current.text;
      previous_word = is_word_token(current);
      previous = current.text;
      take(); consumed = true;
      if (!angles && !parens && previous == "decltype") continue;
    }
    if (!consumed) fail("expected conversion type-id");
    return name;
  }
  std::string type_id_display(std::size_t type) const
  {
    const std::size_t seq = ast_.nodes[type].first_child;
    if (seq == none || ast_.nodes[seq].first_child == none) return std::string();
    const std::size_t spec = ast_.nodes[seq].first_child;
    if (ast_.nodes[spec].kind == NTypeName && ast_.nodes[spec].composite != none)
      return ast_.composite_atoms[ast_.nodes[spec].composite];
    if (ast_.nodes[spec].kind == NTypeSpecifier && ast_.nodes[spec].atom != none)
      return tokens_[ast_.nodes[spec].atom].text;
    return std::string();
  }
  std::size_t parse_parameter_clause()
  {
    expect("(");
    std::size_t clause = node(NParameterClause);
    if (consume(")")) return clause;
    if (consume("...")) { ast_.append(clause, node(NParameterPack, pos_ - 1)); expect(")"); return clause; }
    for (;;) {
      skip_attribute_specifier();
      if (!starts_decl_specifier()) fail("expected parameter declaration");
      std::size_t p = node(NParameterDeclaration);
      std::size_t seq = parse_decl_specifier_seq(true);
      ast_.append(p, seq);
      if (!is(",") && !is(")") && !is("=")) {
        const std::size_t declarator = parse_declarator(true);
        if (ast_.nodes[declarator].first_child != none &&
            ast_.nodes[declarator].first_child == ast_.nodes[declarator].last_child &&
            ast_.nodes[ast_.nodes[declarator].first_child].kind == NParameterClause) {
          const std::size_t clause = ast_.nodes[declarator].first_child;
          const std::size_t first_parameter = ast_.nodes[clause].first_child;
          if (first_parameter == none ||
              (ast_.nodes[first_parameter].kind == NParameterPack &&
               ast_.nodes[first_parameter].next_sibling == none))
            ast_.nodes[declarator].kind = NAbstractDeclarator;
        }
        ast_.append(p, declarator);
      }
      if (consume("=")) {
        std::size_t def = node(NDefaultArgument), init = node(NInitializer);
        ast_.append(init, parse_initializer_clause()); ast_.append(def, init); ast_.append(p, def);
      }
      ast_.append(clause, p);
      // Parameter names take effect only after their own declarator is parsed.
      const std::size_t id = find_declared_identifier(p);
      if (id != none) bind_name(tokens_[id], ValueName);
      if (consume(")")) break;
      expect(",");
      if (consume("...")) { ast_.append(clause, node(NParameterPack, pos_ - 1)); expect(")"); break; }
    }
    return clause;
  }
  std::size_t find_declared_identifier(std::size_t root) const
  {
    for (std::size_t c = ast_.nodes[root].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      if (ast_.nodes[c].kind == NDeclarator) return find_identifier_node(c);
    }
    return none;
  }
  std::size_t find_identifier_node(std::size_t root) const
  {
    if (ast_.nodes[root].kind == NIdentifier) return ast_.nodes[root].atom;
    for (std::size_t c = ast_.nodes[root].first_child; c != none; c = ast_.nodes[c].next_sibling) {
      std::size_t found = find_identifier_node(c);
      if (found != none) return found;
    }
    return none;
  }
  std::size_t parse_initializer_clause()
  {
    if (is("{")) return parse_braced_init();
    std::size_t expression = parse_assignment();
    if (consume("...")) {
      std::size_t expansion = node(NPackExpansionExpression);
      ast_.append(expansion, expression);
      return expansion;
    }
    return expression;
  }
  std::size_t parse_braced_init()
  {
    expect("{");
    std::size_t n = node(NBracedInitList);
    if (!consume("}")) {
      ++template_expression_nesting_;
      do { ast_.append(n, parse_initializer_clause()); } while (consume(",") && !is("}"));
      expect("}");
      --template_expression_nesting_;
    }
    return n;
  }
  std::size_t parse_paren_initializer()
  {
    expect("(");
    std::size_t n = node(NParenInitializer);
    if (!consume(")")) {
      ++template_expression_nesting_;
      do { ast_.append(n, parse_initializer_clause()); } while (consume(","));
      expect(")");
      --template_expression_nesting_;
    }
    return n;
  }

  std::size_t parse_compound_statement()
  {
    expect("{");
    std::size_t block = node(NCompoundStatement);
    names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
    while (!consume("}")) {
      if (at_end()) fail("unterminated compound statement");
      std::size_t before = pos_;
      std::size_t item = parse_block_item();
      ast_.append(block, item);
      if (pos_ == before) fail("parser made no progress in compound statement");
    }
    names_.scopes.pop_back();
    return block;
  }
  std::size_t parse_block_item()
  {
    if (!starts_decl_specifier()) return parse_statement();
    const std::size_t saved_pos = pos_, saved_nodes = ast_.nodes.size();
    const std::size_t saved_atoms = ast_.composite_atoms.size();
    const unsigned saved_pending_gt = pending_gt_;
    const unsigned saved_template_default = template_non_type_default_;
    const std::size_t saved_scopes = names_.scopes.size();
    const std::size_t saved_namespace_path = namespace_path_.size();
    const std::size_t saved_namespace_inline = namespace_inline_.size();
    const std::size_t saved_class_context = class_context_names_.size();
    const std::size_t saved_class_scopes = class_scope_indices_.size();
    const std::size_t undo_mark = names_.begin_transaction();
    try {
      const std::size_t declaration = parse_declaration(false);
      names_.commit_transaction();
      return declaration;
    } catch (const std::exception&) {
      names_.rollback_transaction(undo_mark);
      pos_ = saved_pos; pending_gt_ = saved_pending_gt;
      template_non_type_default_ = saved_template_default;
      ast_.nodes.resize(saved_nodes); ast_.composite_atoms.resize(saved_atoms);
      names_.scopes.resize(saved_scopes);
      namespace_path_.resize(saved_namespace_path);
      namespace_inline_.resize(saved_namespace_inline);
      class_context_names_.resize(saved_class_context);
      class_scope_indices_.resize(saved_class_scopes);
      return parse_statement();
    }
  }
  std::size_t parse_statement()
  {
    if (is("{")) return parse_compound_statement();
    if (is_keyword("if")) return parse_if();
    if (is_keyword("while")) return parse_while();
    if (is_keyword("for")) return parse_for();
    if (is_keyword("switch")) return parse_switch();
    if (is_keyword("do")) return parse_do();
    if (is_keyword("try")) return parse_try_block();
    if (is_keyword("case")) {
      take(); std::size_t n = node(NCaseStatement); ast_.append(n, parse_expression());
      expect(":"); ast_.append(n, parse_statement()); return n;
    }
    if (is_keyword("default")) {
      take(); expect(":"); std::size_t n = node(NDefaultStatement);
      ast_.append(n, parse_statement()); return n;
    }
    if (is_identifier() && is(":", 1)) {
      std::size_t label = take(); expect(":"); std::size_t n = node(NLabeledStatement, label);
      ast_.append(n, parse_statement()); return n;
    }
    if (is_keyword("return")) {
      take(); std::size_t n = node(NReturnStatement);
      if (!is(";")) ast_.append(n, parse_expression());
      expect(";"); return n;
    }
    if (is_keyword("break")) { take(); expect(";"); return node(NBreakStatement); }
    if (is_keyword("continue")) { take(); expect(";"); return node(NContinueStatement); }
    if (is_keyword("goto")) {
      take(); if (!is_identifier()) fail("expected goto label");
      std::size_t n = node(NGotoStatement, take()); expect(";"); return n;
    }
    if (is_keyword("throw")) {
      take(); std::size_t n = node(NThrowStatement);
      if (!is(";")) ast_.append(n, parse_assignment());
      expect(";"); return n;
    }
    if (is(";") ) { take(); return node(NExpressionStatement); }
    std::size_t n = node(NExpressionStatement);
    ast_.append(n, parse_expression());
    expect(";");
    return n;
  }
  std::size_t parse_if()
  {
    take(); expect("(");
    std::size_t n = node(NIfStatement), condition = node(NCondition);
    names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
    if (starts_decl_specifier() && condition_declaration_ahead())
      ast_.append(condition, parse_condition_declaration());
    else
      ast_.append(condition, parse_expression());
    ast_.append(n, condition);
    expect(")");
    std::size_t then_node = node(NThen); ast_.append(then_node, parse_statement()); ast_.append(n, then_node);
    if (consume("else")) {
      std::size_t else_node = node(NElse); ast_.append(else_node, parse_statement()); ast_.append(n, else_node);
    }
    names_.scopes.pop_back();
    return n;
  }
  bool condition_declaration_ahead() const
  {
    if (is_builtin_type() || is_decl_modifier()) return true;
    if (template_id_followed_by_scope() && !qualified_type_name_ahead()) return false;
    if (!is_type_name()) return false;
    if (is("=", 1) || is(")", 1) || is("?", 1) || is("||", 1) || is("&&", 1) ||
        is("(", 1) || is("::", 1)) return false;
    return true;
  }
  std::size_t parse_condition_declaration()
  {
    std::size_t condition = node(NConditionDeclaration);
    ast_.append(condition, parse_decl_specifier_seq(true));
    std::size_t declarator = parse_declarator(false);
    ast_.append(condition, declarator);
    std::size_t identifier = find_identifier_node(declarator);
    if (identifier != none) bind_name(tokens_[identifier], ValueName);
    if (consume("=")) {
      std::size_t initializer = node(NInitializer);
      ast_.append(initializer, parse_initializer_clause());
      ast_.append(condition, initializer);
    } else if (is("(")) {
      std::size_t initializer = node(NInitializer);
      ast_.append(initializer, parse_paren_initializer());
      ast_.append(condition, initializer);
    } else if (is("{")) {
      std::size_t initializer = node(NInitializer);
      ast_.append(initializer, parse_braced_init());
      ast_.append(condition, initializer);
    }
    if (ast_.nodes[condition].last_child == ast_.nodes[condition].first_child ||
        ast_.nodes[condition].last_child == declarator)
      fail("condition declaration requires an initializer");
    return condition;
  }
  std::size_t parse_while()
  {
    take(); expect("(");
    std::size_t n = node(NWhileStatement), condition = node(NCondition);
    ast_.append(condition, parse_expression()); ast_.append(n, condition);
    expect(")"); ast_.append(n, parse_statement()); return n;
  }
  bool range_for_ahead() const
  {
    int parens = 0, brackets = 0, braces = 0, questions = 0;
    for (std::size_t i = pos_; has_token(i); ++i) {
      const std::string& s = tokens_[i].text;
      if (s == "(" ) ++parens; else if (s == ")") { if (parens) --parens; else return false; }
      else if (s == "[") ++brackets; else if (s == "]") { if (brackets) --brackets; }
      else if (s == "{") ++braces; else if (s == "}") { if (braces) --braces; }
      if (parens || brackets || braces) continue;
      if (s == "?") ++questions;
      else if (s == ":") { if (questions) --questions; else return true; }
      else if (s == ";") return false;
    }
    return false;
  }
  std::size_t parse_for()
  {
    take(); expect("(");
    if (range_for_ahead()) {
      names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
      std::size_t n = node(NRangeForStatement), range = node(NRangeDeclaration);
      std::size_t spec = parse_decl_specifier_seq(true); ast_.append(range, spec);
      std::size_t d = parse_declarator(false); ast_.append(range, d);
      std::size_t id = find_identifier_node(d);
      if (id != none) bind_name(tokens_[id], ValueName);
      ast_.append(n, range); expect(":");
      std::size_t init = node(NRangeInitializer); ast_.append(init, parse_expression()); ast_.append(n, init);
      expect(")"); ast_.append(n, parse_statement());
      names_.scopes.pop_back(); return n;
    }
    std::size_t n = node(NForStatement);
    std::size_t init = node(NForInitStatement);
    if (starts_decl_specifier()) ast_.append(init, parse_simple_declaration_in_for());
    else { if (!is(";")) ast_.append(init, parse_expression()); expect(";"); }
    ast_.append(n, init);
    if (!is(";")) { std::size_t c = node(NCondition); ast_.append(c, parse_expression()); ast_.append(n, c); }
    expect(";");
    if (!is(")")) { std::size_t it = node(NIteration); ast_.append(it, parse_expression()); ast_.append(n, it); }
    expect(")"); ast_.append(n, parse_statement()); return n;
  }
  std::size_t parse_switch()
  {
    take(); expect("("); std::size_t n = node(NSwitchStatement), c = node(NCondition);
    ast_.append(c, parse_expression()); ast_.append(n, c); expect(")");
    ast_.append(n, parse_statement()); return n;
  }
  std::size_t parse_do()
  {
    take(); std::size_t n = node(NDoStatement); ast_.append(n, parse_statement());
    if (!is_keyword("while")) fail("expected while after do statement");
    take(); expect("("); std::size_t c = node(NCondition);
    ast_.append(c, parse_expression()); ast_.append(n, c); expect(")"); expect(";"); return n;
  }
  std::size_t parse_try_block()
  {
    take(); std::size_t n = node(NTryBlock); ast_.append(n, parse_compound_statement());
    if (!is_keyword("catch")) fail("expected catch handler");
    do {
      take(); expect("("); std::size_t h = node(NHandler), e = node(NExceptionDeclaration);
      if (consume("...")) ast_.append(e, node(NEllipsis, pos_ - 1));
      else {
        std::size_t spec = parse_decl_specifier_seq(true); ast_.append(e, spec);
        if (!is(")")) ast_.append(e, parse_declarator(true));
      }
      expect(")"); ast_.append(h, e); ast_.append(h, parse_compound_statement()); ast_.append(n, h);
    } while (is_keyword("catch"));
    return n;
  }
  std::size_t parse_simple_declaration_in_for()
  {
    std::size_t spec = parse_decl_specifier_seq(true), d = parse_declarator(false);
    std::size_t init = node(NSimpleDeclaration); ast_.append(init, spec);
    std::size_t list = node(NInitDeclaratorList); ast_.append(list, parse_init_declarator_tail(d));
    while (consume(",")) ast_.append(list, parse_init_declarator_tail(parse_declarator(false)));
    ast_.append(init, list); expect(";"); return init;
  }

  std::size_t parse_expression()
  {
    std::size_t lhs = parse_assignment();
    while (is(",")) { std::size_t op = take(), rhs = parse_assignment(); lhs = binary_node(op, lhs, rhs); }
    return lhs;
  }
  std::size_t parse_assignment()
  {
    std::size_t lhs = parse_conditional();
    if (is_assignment_operator(peek().text)) {
      std::size_t op = take(); std::size_t rhs = parse_assignment();
      std::size_t n = atom_node(NAssignmentExpression, op);
      ast_.append(n, lhs); ast_.append(n, rhs); return n;
    }
    return lhs;
  }
  static bool is_assignment_operator(const std::string& s)
  {
    return s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%=" ||
      s == "^=" || s == "&=" || s == "|=" || s == "<<=" || s == ">>=" ||
      s == "and_eq" || s == "or_eq" || s == "xor_eq";
  }
  std::size_t parse_conditional()
  {
    std::size_t condition = parse_binary(4);
    if (!consume("?")) return condition;
    std::size_t n = node(NConditionalExpression);
    ast_.append(n, condition); ast_.append(n, parse_expression()); expect(":");
    ast_.append(n, parse_assignment()); return n;
  }
  int precedence(const Token& t) const
  {
    const std::string& s = t.text;
    if (s == "||" || s == "or") return 4;
    if (s == "&&" || s == "and") return 5;
    if (s == "|") return 6;
    if (s == "^") return 7;
    if (s == "&") return 8;
    if (s == "==" || s == "!=" || s == "not_eq") return 9;
    if (s == "<" || s == ">" || s == "<=" || s == ">=") return 10;
    if (s == "<<" || s == ">>") return 11;
    if (s == "+" || s == "-") return 12;
    if (s == "*" || s == "/" || s == "%") return 13;
    if (s == ".*" || s == "->*") return 14;
    return 0;
  }
  std::size_t binary_node(std::size_t op, std::size_t left, std::size_t right)
  {
    std::size_t n = atom_node(NBinaryExpression, op);
    ast_.append(n, left); ast_.append(n, right); return n;
  }
  std::size_t parse_binary(int min_prec)
  {
    std::size_t lhs = parse_unary();
    for (;;) {
      if (template_argument_terminator()) break;
      if (template_non_type_default_ && is(">") &&
          (is(",", 1) || is(">", 1) || is(";", 1) || is_keyword("int", 1) ||
           is_keyword("long", 1) || is_keyword("void", 1) || is_keyword("class", 1) ||
           is_keyword("struct", 1) || is_keyword("typename", 1) ||
           is_keyword("explicit", 1) || is_keyword("static", 1) ||
           (is_identifier(1) && (peek(1).text == "__attribute__" || peek(1).text == "__declspec")))) break;
      int prec = precedence(peek());
      if (prec < min_prec) break;
      std::size_t op = take();
      std::size_t rhs = parse_binary(prec + 1);
      lhs = binary_node(op, lhs, rhs);
    }
    return lhs;
  }
  std::size_t parse_unary()
  {
    std::size_t cast = try_parse_c_style_cast();
    if (cast != none) return cast;
    static const char* ops[] = {"++", "--", "*", "&", "+", "-", "!", "~", "not", "compl", "bitand"};
    for (std::size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
      if (is(ops[i])) { std::size_t op = take(), n = node(NUnaryExpression, op); ast_.append(n, parse_unary()); return n; }
    }
    if (is_keyword("sizeof")) {
      take(); std::size_t n = node(NSizeofExpression);
      if (consume("...")) {
        expect("("); if (!is_identifier()) fail("expected sizeof pack name");
        std::size_t pack = composite_node(NSizeofPackExpression, tokens_[take()].text);
        expect(")"); return pack;
      }
      if (consume("(")) {
        if (type_starts_at(0) && !empty_functional_cast_ahead()) ast_.append(n, parse_type_id());
        else {
          ++template_expression_nesting_;
          ast_.append(n, parse_expression());
          --template_expression_nesting_;
        }
        expect(")");
      } else ast_.append(n, parse_unary());
      return n;
    }
    if (is_keyword("alignof") || is_keyword("typeid") || is_keyword("noexcept"))
      return parse_postfix_suffix(parse_type_trait());
    if (is_keyword("static_cast") || is_keyword("dynamic_cast") || is_keyword("const_cast") ||
        is_keyword("reinterpret_cast")) return parse_keyword_cast();
    if (is_keyword("new") || (is("::") && is_keyword("new", 1))) return parse_new_expression();
    if (is_keyword("delete") || (is("::") && is_keyword("delete", 1))) return parse_delete_expression();
    if (is_builtin_type() && is("(", 1)) {
      // Fundamental type function-style casts are represented as a type name
      // call by the PA5 syntax view.
      std::size_t type = take();
      return postfix_from_base(atom_node(NIdExpression, type));
    }
    return parse_postfix();
  }
  std::size_t try_parse_c_style_cast()
  {
    if (!is("(") || !type_starts_at(1)) return none;
    const std::size_t saved_pos = pos_;
    const std::size_t saved_nodes = ast_.nodes.size();
    const std::size_t saved_atoms = ast_.composite_atoms.size();
    const unsigned saved_pending_gt = pending_gt_;
    const unsigned saved_expression_nesting = template_expression_nesting_;
    const std::size_t saved_argument_nesting = template_argument_nesting_.size();
    try {
      std::size_t left = take();
      std::size_t type = parse_type_id();
      if (!consume(")")) throw std::runtime_error("not a C-style cast");
      std::size_t n = atom_node(NCastExpression, left);
      ast_.append(n, type); ast_.append(n, parse_unary()); return n;
    } catch (const std::exception&) {
      pos_ = saved_pos;
      ast_.nodes.resize(saved_nodes);
      ast_.composite_atoms.resize(saved_atoms);
      pending_gt_ = saved_pending_gt;
      template_expression_nesting_ = saved_expression_nesting;
      template_argument_nesting_.resize(saved_argument_nesting);
      return none;
    }
  }
  std::size_t parse_keyword_cast()
  {
    std::size_t keyword = take(); expect("<"); std::size_t type = parse_type_id(); expect(">");
    expect("("); ++template_expression_nesting_;
    std::size_t expr = parse_expression();
    --template_expression_nesting_; expect(")");
    std::size_t n = atom_node(NCastExpression, keyword); ast_.append(n, type); ast_.append(n, expr); return n;
  }
  std::size_t parse_type_trait()
  {
    std::size_t keyword = take();
    std::size_t n = atom_node(NTypeTraitExpression, keyword);
    expect("(");
    if (tokens_[keyword].text == "typeid" && (!type_starts_at(0) || empty_functional_cast_ahead()))
    {
      ++template_expression_nesting_;
      ast_.append(n, parse_expression());
      --template_expression_nesting_;
    }
    else if (tokens_[keyword].text == "noexcept")
    {
      ++template_expression_nesting_;
      ast_.append(n, parse_expression());
      --template_expression_nesting_;
    }
    else
      ast_.append(n, parse_type_id());
    expect(")"); return n;
  }
  bool empty_functional_cast_ahead() const
  { return is("(", 1) && is(")", 2); }
  std::size_t parse_new_expression()
  {
    std::size_t n = node(NNewExpression);
    if (consume("::")) ast_.append(n, node(NGlobalScope));
    expect("new");
    std::size_t type = none;
    if (is("(") && parenthesized_new_type_ahead()) {
      expect("(");
      type = parse_type_id(true);
      expect(")");
    } else {
      if (is("(")) {
      expect("(");
      const std::size_t begin = pos_;
      std::size_t placement = node(NPlacement), args = node(NParenArgumentList);
      std::string spelling = "(";
      if (!consume(")")) {
        do { ast_.append(args, parse_assignment()); } while (consume(","));
        const std::size_t end = pos_;
        for (std::size_t i = begin; i < end; ++i) {
          if (i != begin && is_word_token(tokens_[i - 1]) && is_word_token(tokens_[i])) spelling += ' ';
          spelling += tokens_[i].text;
        }
        expect(")"); spelling += ')';
      } else {
        spelling += ')';
      }
      ast_.composite_atoms.push_back(spelling);
      ast_.nodes[placement].composite = ast_.composite_atoms.size() - 1;
      ast_.append(placement, args);
      ast_.append(n, placement);
      }
      type = parse_type_id(false);
    }
    ast_.append(n, type);
    if (is("(") || is("{")) {
      std::size_t init = node(NInitializer);
      if (is("(")) ast_.append(init, parse_paren_initializer());
      else ast_.append(init, parse_braced_init());
      ast_.append(n, init);
    }
    return n;
  }
  bool parenthesized_new_type_ahead() const
  {
    if (!is("(") || !type_starts_at(1)) return false;
    int depth = 1;
    std::size_t i = pos_ + 1;
    while (has_token(i) && depth) {
      if (tokens_[i].text == "(") ++depth;
      else if (tokens_[i].text == ")") --depth;
      ++i;
    }
    if (depth || !has_token(i)) return false;
    const std::string& after = tokens_[i].text;
    return after == "(" || after == "{" || after == ";" || after == ")" || after == ",";
  }
  std::size_t parse_delete_expression()
  {
    std::size_t n = node(NDeleteExpression);
    if (consume("::")) ast_.append(n, node(NGlobalScope));
    expect("delete");
    if (consume("[")) { expect("]"); ast_.append(n, node(NArrayDelete)); }
    ast_.append(n, parse_unary()); return n;
  }
  std::size_t postfix_from_base(std::size_t base)
  {
    expect("("); std::size_t call = node(NCallExpression); ast_.append(call, base);
    std::size_t args = node(NParenArgumentList);
    if (!consume(")")) {
      do { ast_.append(args, parse_assignment()); } while (consume(","));
      expect(")");
    }
    ast_.append(call, args); return call;
  }
  std::size_t parse_postfix()
  {
    return parse_postfix_suffix(parse_primary());
  }
  bool qualified_member_id_ahead() const
  {
    if (!is_identifier()) return false;
    std::size_t i = pos_ + 1;
    if (has_token(i) && tokens_[i].text == "::") return true;
    if (!has_token(i) || tokens_[i].text != "<") return false;
    int angles = 0, parens = 0, brackets = 0, braces = 0;
    do {
      const std::string& text = tokens_[i].text;
      if (text == "(") ++parens;
      else if (text == ")" && parens) --parens;
      else if (text == "[") ++brackets;
      else if (text == "]" && brackets) --brackets;
      else if (text == "{") ++braces;
      else if (text == "}" && braces) --braces;
      else if (!parens && !brackets && !braces && text == "<") ++angles;
      else if (!parens && !brackets && !braces && text == ">" && angles) --angles;
      else if (!parens && !brackets && !braces && text == ">>" && angles)
        angles = angles >= 2 ? angles - 2 : 0;
      ++i;
    } while (has_token(i) && angles > 0);
    return angles == 0 && has_token(i) && tokens_[i].text == "::";
  }
  std::size_t parse_postfix_suffix(std::size_t base)
  {
    for (;;) {
      if (consume("(")) {
        std::size_t call = node(NCallExpression); ast_.append(call, base);
        bool type_call = ast_.nodes[base].kind == NIdExpression && ast_.nodes[base].atom != none &&
            ast_.tokens[ast_.nodes[base].atom].category == KeywordToken;
        std::size_t args = node(type_call ? NParenArgumentList : NArgumentList);
        if (!consume(")")) {
          ++template_expression_nesting_;
          do { ast_.append(args, parse_assignment()); } while (consume(","));
          expect(")");
          --template_expression_nesting_;
        }
        ast_.append(call, args); base = call; continue;
      }
    if (is("{")) {
      std::size_t call = node(NCallExpression); ast_.append(call, base);
      ast_.append(call, parse_braced_init()); base = call; continue;
      }
      if (consume("[")) {
        std::size_t sub = node(NSubscriptExpression); ast_.append(sub, base);
        ++template_expression_nesting_;
        ast_.append(sub, parse_expression()); expect("]");
        --template_expression_nesting_;
        base = sub; continue;
      }
      if (is(".") || is("->")) {
        std::size_t op = take();
        std::size_t member = atom_node(NMemberExpression, op); ast_.append(member, base);
        ParsedName name;
        const bool explicit_template = is_keyword("template");
        if (explicit_template) { name.spelling = "template "; take(); }
        if (is("~")) {
          take();
          if (!is_identifier()) fail("expected destructor name");
          const std::size_t component = take();
          name.spelling += "~" + tokens_[component].text;
          name.first_token = name.source_token = component;
          add_name_component(name, component);
        } else if (is_keyword("operator")) {
          const std::size_t operator_token = pos_;
          name.spelling += parse_operator_name();
          name.operator_tokens_begin = operator_token;
          name.operator_tokens_end = pos_;
          name.first_token = name.source_token = operator_token;
          add_name_component(name, operator_token);
          if (explicit_template && is("<")) { take(); read_template_suffix(name); }
        } else {
          if (!is_identifier()) fail("expected member name");
          if (explicit_template) {
            const std::size_t component = take();
            name.spelling += tokens_[component].text;
            name.first_token = name.source_token = component;
            add_name_component(name, component);
            if (is("<")) { take(); read_template_suffix(name); }
          } else if (qualified_member_id_ahead()) {
            name = joined_name();
          } else {
            const std::size_t component = take();
            name.spelling = tokens_[component].text;
            name.first_token = name.source_token = component;
          }
        }
        if (name.spelling.empty()) {
          const std::size_t operator_token = pos_;
          name.spelling = parse_operator_name();
          name.operator_tokens_begin = operator_token;
          name.operator_tokens_end = pos_;
          name.first_token = name.source_token = operator_token;
          add_name_component(name, operator_token);
        }
        ast_.append(member, composite_node(NIdentifier, name));
        base = member; continue;
      }
      if (is("++") || is("--")) {
        std::size_t op = take(), post = atom_node(NPostfixExpression, op);
        ast_.append(post, base); base = post; continue;
      }
      break;
    }
    return base;
  }
  std::size_t parse_primary()
  {
    if (is_literal()) return atom_node(NLiteral, take());
    if (is_keyword("true") || is_keyword("false") || is_keyword("nullptr"))
      return atom_node(NKeywordLiteral, take());
    if (is_keyword("this")) return atom_node(NKeywordLiteral, take());
    if (is_keyword("operator")) {
      const std::size_t operator_token = pos_;
      ParsedName name;
      name.operator_tokens_begin = operator_token;
      name.spelling = parse_operator_name();
      name.operator_tokens_end = pos_;
      name.first_token = name.source_token = operator_token;
      const std::size_t space = name.spelling.find("operator ");
      if (space == 0) name.spelling.erase(8, 1);
      if (is("<")) { take(); read_template_suffix(name); }
      return composite_node(NIdExpression, name);
    }
    if (is_keyword("typename")) {
      take();
      return composite_node(NIdExpression, joined_name());
    }
    if (is_keyword("decltype"))
      return composite_node(NIdExpression, read_decltype_qualified_name());
    if (is("[")) return parse_lambda_expression();
    if (is_builtin_type() && is("(", 1)) return atom_node(NIdExpression, take());
    if (is_identifier() || is("::")) {
      ParsedName name = joined_name();
      if (is("<") && template_id_candidate(name.spelling)) {
        take(); read_template_suffix(name);
      }
      return composite_node(NIdExpression, name);
    }
    if (consume("(")) {
      std::size_t n = node(NParenthesizedExpression);
      ++template_expression_nesting_;
      ast_.append(n, parse_expression()); expect(")");
      --template_expression_nesting_;
      return n;
    }
    if (is("{")) return parse_braced_init();
    fail("expected expression");
    return none;
  }
  std::size_t parse_lambda_expression()
  {
    std::string capture = "["; expect("[");
    while (!consume("]")) {
      if (at_end()) fail("unterminated lambda capture");
      capture += peek().text; take();
    }
    capture += "]";
    std::size_t lambda = node(NLambdaExpression);
    ast_.append(lambda, composite_node(NLambdaIntroducer, capture));
    names_.scopes.push_back(std::unordered_map<NameId, NameKind>());
    if (is("(") || is_keyword("mutable") || is_keyword("noexcept") || is("->")) {
      std::size_t declarator = node(NLambdaDeclarator);
      if (is("(")) ast_.append(declarator, parse_parameter_clause());
      if (is_keyword("mutable")) ast_.append(declarator, atom_node(NLambdaSpecifier, take()));
      if (is_keyword("noexcept")) {
        std::size_t noex = node(NNoexceptSpecification); take();
        if (consume("(")) { ast_.append(noex, parse_expression()); expect(")"); }
        ast_.append(declarator, noex);
      }
      if (consume("->")) {
        std::size_t trailing = node(NTrailingReturnType);
        ast_.append(trailing, parse_type_id()); ast_.append(declarator, trailing);
      }
      ast_.append(lambda, declarator);
    }
    ast_.append(lambda, parse_compound_statement());
    names_.scopes.pop_back();
    return lambda;
  }

  Ast& ast_;
  std::vector<Token>& tokens_;
  PreprocessedTokenCursor& cursor_;
  std::size_t pos_;
  unsigned pending_gt_;
  unsigned template_non_type_default_;
  unsigned template_expression_nesting_;
  std::vector<unsigned> template_argument_nesting_;
  mutable bool input_finished_;
  ParserNameState names_;
  std::vector<std::string> namespace_path_;
  std::vector<bool> namespace_inline_;
  std::vector<std::string> class_context_names_;
  std::vector<std::size_t> class_scope_indices_;
};


std::string ReadFile(const std::string& path)
{
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) throw std::runtime_error("unable to open source file: " + path);
  std::string source;
  char buffer[64 * 1024];
  for (;;) {
    in.read(buffer, sizeof(buffer));
    std::streamsize count = in.gcount();
    if (count > 0) source.append(buffer, static_cast<std::size_t>(count));
    if (in.bad()) throw std::runtime_error("failed to read source file: " + path);
    if (in.eof()) break;
    if (!in) throw std::runtime_error("failed to read source file: " + path);
  }
  return source;
}

std::pair<std::string, std::string> BuildDateAndTime()
{
  std::time_t now = std::time(NULL);
  const std::tm* local = std::localtime(&now);
  if (!local) throw std::runtime_error("unable to read build date and time");
  const char* text = std::asctime(local);
  if (!text || std::string(text).size() < 25)
    throw std::runtime_error("unable to format build date and time");
  return std::make_pair(std::string(text + 4, 6) + " " + std::string(text + 20, 4),
                        std::string(text + 11, 8));
}

}  // namespace

Ast ParseTranslationUnitSource(const std::string& source, const std::string& path)
{
  Ast ast;
  const std::pair<std::string, std::string> time = BuildDateAndTime();
  // The cursor must join before ast releases metadata referenced by its worker.
  PreprocessedTokenCursor cursor(source, path, ast.preprocessing_metadata(),
                                 time.first, time.second);
  Parser parser(ast, cursor);
  return parser.Parse();
}

Ast ParseTranslationUnit(const std::string& path)
{
  const std::string source = ReadFile(path);
  return ParseTranslationUnitSource(source, path);
}


void EmitAst(const std::vector<std::string>& inputs, const std::string& output)
{
  std::ofstream out(output.c_str(), std::ios::out | std::ios::trunc);
  if (!out) throw std::runtime_error("unable to open output file: " + output);
  out << inputs.size() << " translation units\n";
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    Ast ast = ParseTranslationUnit(inputs[i]);
    out << "start translation unit " << (i + 1) << '\n';
    PrintAst(ast, out);
    out << "end translation unit\n";
    if (!out) throw std::runtime_error("failed to write AST output");
  }
}

}  // namespace cppgm
