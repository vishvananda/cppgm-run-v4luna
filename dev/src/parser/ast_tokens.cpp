#include "parser/ast_tokens.h"

#include <stdexcept>
#include <unordered_map>

namespace cppgm {
namespace ast_tokens {
namespace {

const std::unordered_map<std::string, std::string>& keyword_tags()
{
  static const std::unordered_map<std::string, std::string> tags = {
    {"alignas", "KW_ALIGNAS"}, {"alignof", "KW_ALIGNOF"}, {"asm", "KW_ASM"},
    {"auto", "KW_AUTO"}, {"bool", "KW_BOOL"}, {"break", "KW_BREAK"},
    {"case", "KW_CASE"}, {"catch", "KW_CATCH"}, {"char", "KW_CHAR"},
    {"char16_t", "KW_CHAR16_T"}, {"char32_t", "KW_CHAR32_T"}, {"class", "KW_CLASS"},
    {"const", "KW_CONST"}, {"constexpr", "KW_CONSTEXPR"}, {"const_cast", "KW_CONST_CAST"},
    {"continue", "KW_CONTINUE"}, {"decltype", "KW_DECLTYPE"}, {"default", "KW_DEFAULT"},
    {"delete", "KW_DELETE"}, {"do", "KW_DO"}, {"double", "KW_DOUBLE"},
    {"dynamic_cast", "KW_DYNAMIC_CAST"}, {"else", "KW_ELSE"}, {"enum", "KW_ENUM"},
    {"explicit", "KW_EXPLICIT"}, {"export", "KW_EXPORT"}, {"extern", "KW_EXTERN"},
    {"false", "KW_FALSE"}, {"float", "KW_FLOAT"}, {"for", "KW_FOR"},
    {"friend", "KW_FRIEND"}, {"goto", "KW_GOTO"}, {"if", "KW_IF"},
    {"inline", "KW_INLINE"}, {"int", "KW_INT"}, {"long", "KW_LONG"},
    {"mutable", "KW_MUTABLE"}, {"namespace", "KW_NAMESPACE"}, {"new", "KW_NEW"},
    {"noexcept", "KW_NOEXCEPT"}, {"nullptr", "KW_NULLPTR"}, {"operator", "KW_OPERATOR"},
    {"private", "KW_PRIVATE"}, {"protected", "KW_PROTECTED"}, {"public", "KW_PUBLIC"},
    {"register", "KW_REGISTER"}, {"reinterpret_cast", "KW_REINTERPET_CAST"},
    {"return", "KW_RETURN"}, {"short", "KW_SHORT"}, {"signed", "KW_SIGNED"},
    {"sizeof", "KW_SIZEOF"}, {"static", "KW_STATIC"}, {"static_assert", "KW_STATIC_ASSERT"},
    {"static_cast", "KW_STATIC_CAST"}, {"struct", "KW_STRUCT"}, {"switch", "KW_SWITCH"},
    {"template", "KW_TEMPLATE"}, {"this", "KW_THIS"}, {"thread_local", "KW_THREAD_LOCAL"},
    {"throw", "KW_THROW"}, {"true", "KW_TRUE"}, {"try", "KW_TRY"},
    {"typedef", "KW_TYPEDEF"}, {"typeid", "KW_TYPEID"}, {"typename", "KW_TYPENAME"},
    {"union", "KW_UNION"}, {"unsigned", "KW_UNSIGNED"}, {"using", "KW_USING"},
    {"virtual", "KW_VIRTUAL"}, {"void", "KW_VOID"}, {"volatile", "KW_VOLATILE"},
    {"wchar_t", "KW_WCHAR_T"}, {"while", "KW_WHILE"}
  };
  return tags;
}

const std::unordered_map<std::string, std::string>& punctuator_tags()
{
  static const std::unordered_map<std::string, std::string> tags = {
    {"{", "OP_LBRACE"}, {"<%", "OP_LBRACE"}, {"}", "OP_RBRACE"}, {"%>", "OP_RBRACE"},
    {"[", "OP_LSQUARE"}, {"<:", "OP_LSQUARE"}, {"]", "OP_RSQUARE"}, {":>", "OP_RSQUARE"},
    {"(", "OP_LPAREN"}, {")", "OP_RPAREN"}, {"|", "OP_BOR"}, {"bitor", "OP_BOR"},
    {"^", "OP_XOR"}, {"xor", "OP_XOR"}, {"~", "OP_COMPL"}, {"compl", "OP_COMPL"},
    {"&", "OP_AMP"}, {"bitand", "OP_AMP"}, {"!", "OP_LNOT"}, {"not", "OP_LNOT"},
    {";", "OP_SEMICOLON"}, {":", "OP_COLON"}, {"...", "OP_DOTS"}, {"?", "OP_QMARK"},
    {"::", "OP_COLON2"}, {".", "OP_DOT"}, {".*", "OP_DOTSTAR"}, {"+", "OP_PLUS"},
    {"-", "OP_MINUS"}, {"*", "OP_STAR"}, {"/", "OP_DIV"}, {"%", "OP_MOD"},
    {"=", "OP_ASS"}, {"<", "OP_LT"}, {">", "OP_GT"}, {"+=", "OP_PLUSASS"},
    {"-=", "OP_MINUSASS"}, {"*=", "OP_STARASS"}, {"/=", "OP_DIVASS"}, {"%=", "OP_MODASS"},
    {"^=", "OP_XORASS"}, {"xor_eq", "OP_XORASS"}, {"&=", "OP_BANDASS"},
    {"and_eq", "OP_BANDASS"}, {"|=", "OP_BORASS"}, {"or_eq", "OP_BORASS"},
    {"<<", "OP_LSHIFT"}, {">>", "OP_RSHIFT"}, {">>=", "OP_RSHIFTASS"}, {"<<=", "OP_LSHIFTASS"},
    {"==", "OP_EQ"}, {"!=", "OP_NE"}, {"not_eq", "OP_NE"}, {"<=", "OP_LE"}, {">=", "OP_GE"},
    {"&&", "OP_LAND"}, {"and", "OP_LAND"}, {"||", "OP_LOR"}, {"or", "OP_LOR"},
    {"++", "OP_INC"}, {"--", "OP_DEC"}, {",", "OP_COMMA"}, {"->*", "OP_ARROWSTAR"},
    {"->", "OP_ARROW"}
  };
  return tags;
}

const std::string& identifier_tag()
{
  static const std::string value = "TT_IDENTIFIER";
  return value;
}

const std::string& literal_tag()
{
  static const std::string value = "TT_LITERAL";
  return value;
}

}  // namespace

bool IsKeyword(const std::string& spelling)
{
  return keyword_tags().find(spelling) != keyword_tags().end();
}

Token MakeToken(const PreprocessingToken& pp, const std::string* stable_spelling)
{
  if (!stable_spelling) throw std::runtime_error("token spelling has no owner");
  Token token;
  token.text = TokenText(*stable_spelling);
  token.identifier_id = pp.identifier_id;
  token.line = pp.line;
  token.column = pp.column;
  token.source_file_id = pp.source_file_id;
  const std::string& spelling = *stable_spelling;
  if (pp.kind == PP_TOKEN_IDENTIFIER) {
    const std::unordered_map<std::string, std::string>::const_iterator keyword =
        keyword_tags().find(spelling);
    const std::unordered_map<std::string, std::string>::const_iterator punct =
        punctuator_tags().find(spelling);
    if (punct != punctuator_tags().end()) {
      token.category = PunctuatorToken;
      token.tag = punct->second;
    } else if (keyword != keyword_tags().end()) {
      token.category = KeywordToken;
      token.tag = keyword->second;
    } else {
      token.category = IdentifierToken;
      token.tag = identifier_tag();
    }
  } else if (pp.kind == PP_TOKEN_PUNCTUATOR) {
    const std::unordered_map<std::string, std::string>::const_iterator keyword =
        keyword_tags().find(spelling);
    if (keyword != keyword_tags().end()) {
      token.category = KeywordToken;
      token.tag = keyword->second;
    } else {
      token.category = PunctuatorToken;
      const std::unordered_map<std::string, std::string>::const_iterator found =
          punctuator_tags().find(spelling);
      if (found == punctuator_tags().end())
        throw std::runtime_error("invalid C++ punctuator");
      token.tag = found->second;
    }
  } else if (pp.kind == PP_TOKEN_NUMBER || pp.kind == PP_TOKEN_CHARACTER ||
             pp.kind == PP_TOKEN_USER_CHARACTER || pp.kind == PP_TOKEN_STRING ||
             pp.kind == PP_TOKEN_USER_STRING) {
    token.category = LiteralToken;
    token.tag = literal_tag();
  } else {
    throw std::runtime_error("invalid token in C++ source");
  }
  return token;
}

}  // namespace ast_tokens
}  // namespace cppgm
