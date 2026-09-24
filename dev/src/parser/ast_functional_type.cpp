#include "parser/ast_parser.h"

namespace cppgm {

bool IsMultiwordFunctionalTypeAhead(const std::vector<ast_tokens::Token>& tokens,
                                   std::size_t position, std::string& spelling,
                                   std::size_t& word_count)
{
  static const char* words[] = {"signed", "unsigned", "long", "short", "int", "char"};
  std::size_t count = 0;
  std::string value;
  while (count < 3) {
    if (position + count >= tokens.size() ||
        tokens[position + count].category != ast_tokens::KeywordToken) break;
    const std::string& token = tokens[position + count].text.get();
    bool found = false;
    for (std::size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
      if (token == words[i]) { found = true; break; }
    if (!found) break;
    if (count) value += ' ';
    value += token;
    ++count;
  }
  if (count < 2 || position + count >= tokens.size() ||
      tokens[position + count].text != "(") return false;
  spelling.swap(value);
  word_count = count;
  return true;
}

}  // namespace cppgm
