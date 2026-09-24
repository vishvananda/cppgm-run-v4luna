#pragma once

#include "preprocess/preprocessor.h"

#include <cstddef>
#include <string>

namespace cppgm {
namespace ast_tokens {

enum TokenClass { IdentifierToken, LiteralToken, PunctuatorToken, KeywordToken };

struct Token
{
  std::string text;
  std::string tag;
  TokenClass category;
  std::size_t line;
  std::size_t column;
};

Token MakeToken(const PreprocessingToken& token);
bool IsKeyword(const std::string& spelling);

}  // namespace ast_tokens
}  // namespace cppgm
