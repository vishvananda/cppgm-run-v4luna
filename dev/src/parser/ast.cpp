#include "parser/ast_parser.h"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cppgm {

struct Ast::SpellingSet
{
  std::unordered_set<std::string> values;
  std::unordered_map<std::string, std::size_t> name_ids;
  std::size_t next_composite_name;
  SpellingSet() : next_composite_name(0) {}
};

Ast::Ast()
    : spellings_(new SpellingSet()),
      metadata_(new PreprocessingMetadata())
{}

Ast::~Ast() {}

Ast::Ast(Ast&& other) : tokens(std::move(other.tokens)),
    nodes(std::move(other.nodes)), composite_atoms(std::move(other.composite_atoms)),
    spellings_(std::move(other.spellings_)), metadata_(std::move(other.metadata_))
{}

Ast& Ast::operator=(Ast&& other)
{
  if (this != &other) {
    tokens = std::move(other.tokens);
    nodes = std::move(other.nodes);
    composite_atoms = std::move(other.composite_atoms);
    spellings_ = std::move(other.spellings_);
    metadata_ = std::move(other.metadata_);
  }
  return *this;
}

std::size_t Ast::add(NodeKind kind, std::size_t atom, std::size_t composite)
{
  AstNode node;
  node.kind = kind;
  node.atom = atom;
  node.composite = composite;
  node.first_child = node.last_child = node.next_sibling = static_cast<std::size_t>(-1);
  node.first_aux_child = node.last_aux_child = node.next_aux_sibling =
      static_cast<std::size_t>(-1);
  node.source_file_id = node.line = node.column = static_cast<std::size_t>(-1);
  node.source_end_token_index = static_cast<std::size_t>(-1);
  nodes.push_back(node);
  return nodes.size() - 1;
}

void Ast::append(std::size_t parent, std::size_t child)
{
  const std::size_t none = static_cast<std::size_t>(-1);
  if (parent == none || child == none) return;
  if (nodes[parent].first_child == none) nodes[parent].first_child = child;
  else nodes[nodes[parent].last_child].next_sibling = child;
  nodes[parent].last_child = child;
}

void Ast::append_aux(std::size_t parent, std::size_t child)
{
  const std::size_t none = static_cast<std::size_t>(-1);
  if (parent == none || child == none) return;
  if (nodes[parent].first_aux_child == none) nodes[parent].first_aux_child = child;
  else nodes[nodes[parent].last_aux_child].next_aux_sibling = child;
  nodes[parent].last_aux_child = child;
}

void Ast::set_location(std::size_t id, std::size_t source_file_id,
                       std::size_t line, std::size_t column)
{
  nodes[id].source_file_id = source_file_id;
  nodes[id].line = line;
  nodes[id].column = column;
}

void Ast::compact_tokens()
{
  const std::size_t none = static_cast<std::size_t>(-1);
  std::vector<std::size_t> remap(tokens.size(), none);
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const std::size_t old = nodes[i].atom;
    if (old == none) continue;
    if (old >= tokens.size()) throw std::logic_error("AST token index out of range");
    remap[old] = 0;
  }
  std::size_t retained = 0;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (remap[i] == none) continue;
    remap[i] = retained;
    if (retained != i) tokens[retained] = std::move(tokens[i]);
    ++retained;
  }
  for (std::size_t i = 0; i < nodes.size(); ++i)
    if (nodes[i].atom != none) nodes[i].atom = remap[nodes[i].atom];
  tokens.resize(retained);
}

const std::string* Ast::intern_spelling(const std::string& spelling)
{
  const std::pair<std::unordered_set<std::string>::const_iterator, bool> result =
      spellings_->values.insert(spelling);
  return &*result.first;
}

std::size_t Ast::register_identifier(std::size_t identifier_id,
                                     const std::string& spelling)
{
  const std::pair<std::unordered_map<std::string, std::size_t>::iterator, bool> result =
      spellings_->name_ids.insert(std::make_pair(spelling, identifier_id));
  if (!result.second && result.first->second != identifier_id)
    throw std::logic_error("preprocessor identifier identity mismatch");
  return result.first->second;
}

std::size_t Ast::intern_name(const std::string& spelling)
{
  const std::unordered_map<std::string, std::size_t>::const_iterator old =
      spellings_->name_ids.find(spelling);
  if (old != spellings_->name_ids.end()) return old->second;
  const std::size_t high_bit = std::size_t(1) << (sizeof(std::size_t) * 8 - 1);
  if (spellings_->next_composite_name >= high_bit - 1)
    throw std::overflow_error("AST name identity space exhausted");
  const std::size_t id = high_bit | spellings_->next_composite_name++;
  spellings_->name_ids.insert(std::make_pair(spelling, id));
  return id;
}

std::size_t Ast::find_name(const std::string& spelling) const
{
  const std::unordered_map<std::string, std::size_t>::const_iterator found =
      spellings_->name_ids.find(spelling);
  return found == spellings_->name_ids.end() ? static_cast<std::size_t>(-1) : found->second;
}

PreprocessingMetadata& Ast::preprocessing_metadata() { return *metadata_; }
const PreprocessingMetadata& Ast::preprocessing_metadata() const { return *metadata_; }

const std::string& Ast::source_file(std::size_t id) const
{
  if (id >= metadata_->source_files.size())
    throw std::out_of_range("invalid AST source file id");
  return metadata_->source_files[id];
}


}  // namespace cppgm
