#pragma once

#include "preprocess/preprocessor.h"

#include <cstddef>
#include <ostream>
#include <string>

namespace cppgm {
namespace ast_tokens {

class TokenText
{
public:
  TokenText() : value_(&empty_text()) {}
  TokenText(const std::string& value) : value_(&value) {}

  operator const std::string&() const { return *value_; }
  const std::string& get() const { return *value_; }

  friend bool operator==(TokenText left, const std::string& right)
  { return left.get() == right; }
  friend bool operator==(const std::string& left, TokenText right)
  { return left == right.get(); }
  friend bool operator!=(TokenText left, const std::string& right)
  { return !(left == right); }
  friend bool operator!=(const std::string& left, TokenText right)
  { return !(left == right); }
  friend bool operator==(TokenText left, const char* right)
  { return left.get() == right; }
  friend bool operator==(const char* left, TokenText right)
  { return right == left; }
  friend bool operator!=(TokenText left, const char* right)
  { return !(left == right); }
  friend bool operator!=(const char* left, TokenText right)
  { return !(right == left); }
  friend std::ostream& operator<<(std::ostream& out, TokenText value)
  { return out << value.get(); }
  friend std::string operator+(const std::string& left, TokenText right)
  { return left + right.get(); }
  friend std::string operator+(const char* left, TokenText right)
  { return std::string(left) + right.get(); }
  friend std::string operator+(TokenText left, const std::string& right)
  { return left.get() + right; }
  friend std::string operator+(TokenText left, const char* right)
  { return left.get() + right; }

private:
  static const std::string& empty_text()
  {
    static const std::string value;
    return value;
  }
  const std::string* value_;
};

enum TokenClass { IdentifierToken, LiteralToken, PunctuatorToken, KeywordToken };

struct Token
{
  Token()
      : identifier_id(static_cast<std::size_t>(-1)), category(IdentifierToken),
        line(0), column(0), source_file_id(0) {}

  TokenText text;
  TokenText tag;
  std::size_t identifier_id;
  TokenClass category;
  std::size_t line;
  std::size_t column;
  std::size_t source_file_id;
};

Token MakeToken(const PreprocessingToken& token,
                const std::string* stable_spelling);
bool IsKeyword(const std::string& spelling);

}  // namespace ast_tokens
}  // namespace cppgm
