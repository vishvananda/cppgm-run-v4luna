// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <memory>
#include <cstring>
#include <cstdint>
#include <climits>
#include <map>
#include <vector>

using namespace std;

#include "postprocess/posttoken.h"
#include "preprocess/tokens/PPTokenizer.h"

// See 3.9.1: Fundamental Types
enum EFundamentalType
{
	// 3.9.1.2
	FT_SIGNED_CHAR,
	FT_SHORT_INT,
	FT_INT,
	FT_LONG_INT,
	FT_LONG_LONG_INT,

	// 3.9.1.3
	FT_UNSIGNED_CHAR,
	FT_UNSIGNED_SHORT_INT,
	FT_UNSIGNED_INT,
	FT_UNSIGNED_LONG_INT,
	FT_UNSIGNED_LONG_LONG_INT,

	// 3.9.1.1 / 3.9.1.5
	FT_WCHAR_T,
	FT_CHAR,
	FT_CHAR16_T,
	FT_CHAR32_T,

	// 3.9.1.6
	FT_BOOL,

	// 3.9.1.8
	FT_FLOAT,
	FT_DOUBLE,
	FT_LONG_DOUBLE,

	// 3.9.1.9
	FT_VOID,

	// 3.9.1.10
	FT_NULLPTR_T
};

// FundamentalTypeOf: convert fundamental type T to EFundamentalType
// for example: `FundamentalTypeOf<long int>()` will return `FT_LONG_INT`
template<typename T> constexpr EFundamentalType FundamentalTypeOf();
template<> constexpr EFundamentalType FundamentalTypeOf<signed char>() { return FT_SIGNED_CHAR; }
template<> constexpr EFundamentalType FundamentalTypeOf<short int>() { return FT_SHORT_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<int>() { return FT_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<long int>() { return FT_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<long long int>() { return FT_LONG_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned char>() { return FT_UNSIGNED_CHAR; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned short int>() { return FT_UNSIGNED_SHORT_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned int>() { return FT_UNSIGNED_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned long int>() { return FT_UNSIGNED_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned long long int>() { return FT_UNSIGNED_LONG_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<wchar_t>() { return FT_WCHAR_T; }
template<> constexpr EFundamentalType FundamentalTypeOf<char>() { return FT_CHAR; }
template<> constexpr EFundamentalType FundamentalTypeOf<char16_t>() { return FT_CHAR16_T; }
template<> constexpr EFundamentalType FundamentalTypeOf<char32_t>() { return FT_CHAR32_T; }
template<> constexpr EFundamentalType FundamentalTypeOf<bool>() { return FT_BOOL; }
template<> constexpr EFundamentalType FundamentalTypeOf<float>() { return FT_FLOAT; }
template<> constexpr EFundamentalType FundamentalTypeOf<double>() { return FT_DOUBLE; }
template<> constexpr EFundamentalType FundamentalTypeOf<long double>() { return FT_LONG_DOUBLE; }
template<> constexpr EFundamentalType FundamentalTypeOf<void>() { return FT_VOID; }
template<> constexpr EFundamentalType FundamentalTypeOf<nullptr_t>() { return FT_NULLPTR_T; }

// convert EFundamentalType to a source code
const map<EFundamentalType, string> FundamentalTypeToStringMap
{
	{FT_SIGNED_CHAR, "signed char"},
	{FT_SHORT_INT, "short int"},
	{FT_INT, "int"},
	{FT_LONG_INT, "long int"},
	{FT_LONG_LONG_INT, "long long int"},
	{FT_UNSIGNED_CHAR, "unsigned char"},
	{FT_UNSIGNED_SHORT_INT, "unsigned short int"},
	{FT_UNSIGNED_INT, "unsigned int"},
	{FT_UNSIGNED_LONG_INT, "unsigned long int"},
	{FT_UNSIGNED_LONG_LONG_INT, "unsigned long long int"},
	{FT_WCHAR_T, "wchar_t"},
	{FT_CHAR, "char"},
	{FT_CHAR16_T, "char16_t"},
	{FT_CHAR32_T, "char32_t"},
	{FT_BOOL, "bool"},
	{FT_FLOAT, "float"},
	{FT_DOUBLE, "double"},
	{FT_LONG_DOUBLE, "long double"},
	{FT_VOID, "void"},
	{FT_NULLPTR_T, "nullptr_t"}
};

// token type enum for `simples`
enum ETokenType
{
	// keywords
	KW_ALIGNAS,
	KW_ALIGNOF,
	KW_ASM,
	KW_AUTO,
	KW_BOOL,
	KW_BREAK,
	KW_CASE,
	KW_CATCH,
	KW_CHAR,
	KW_CHAR16_T,
	KW_CHAR32_T,
	KW_CLASS,
	KW_CONST,
	KW_CONSTEXPR,
	KW_CONST_CAST,
	KW_CONTINUE,
	KW_DECLTYPE,
	KW_DEFAULT,
	KW_DELETE,
	KW_DO,
	KW_DOUBLE,
	KW_DYNAMIC_CAST,
	KW_ELSE,
	KW_ENUM,
	KW_EXPLICIT,
	KW_EXPORT,
	KW_EXTERN,
	KW_FALSE,
	KW_FLOAT,
	KW_FOR,
	KW_FRIEND,
	KW_GOTO,
	KW_IF,
	KW_INLINE,
	KW_INT,
	KW_LONG,
	KW_MUTABLE,
	KW_NAMESPACE,
	KW_NEW,
	KW_NOEXCEPT,
	KW_NULLPTR,
	KW_OPERATOR,
	KW_PRIVATE,
	KW_PROTECTED,
	KW_PUBLIC,
	KW_REGISTER,
	KW_REINTERPET_CAST,
	KW_RETURN,
	KW_SHORT,
	KW_SIGNED,
	KW_SIZEOF,
	KW_STATIC,
	KW_STATIC_ASSERT,
	KW_STATIC_CAST,
	KW_STRUCT,
	KW_SWITCH,
	KW_TEMPLATE,
	KW_THIS,
	KW_THREAD_LOCAL,
	KW_THROW,
	KW_TRUE,
	KW_TRY,
	KW_TYPEDEF,
	KW_TYPEID,
	KW_TYPENAME,
	KW_UNION,
	KW_UNSIGNED,
	KW_USING,
	KW_VIRTUAL,
	KW_VOID,
	KW_VOLATILE,
	KW_WCHAR_T,
	KW_WHILE,

	// operators/punctuation
	OP_LBRACE,
	OP_RBRACE,
	OP_LSQUARE,
	OP_RSQUARE,
	OP_LPAREN,
	OP_RPAREN,
	OP_BOR,
	OP_XOR,
	OP_COMPL,
	OP_AMP,
	OP_LNOT,
	OP_SEMICOLON,
	OP_COLON,
	OP_DOTS,
	OP_QMARK,
	OP_COLON2,
	OP_DOT,
	OP_DOTSTAR,
	OP_PLUS,
	OP_MINUS,
	OP_STAR,
	OP_DIV,
	OP_MOD,
	OP_ASS,
	OP_LT,
	OP_GT,
	OP_PLUSASS,
	OP_MINUSASS,
	OP_STARASS,
	OP_DIVASS,
	OP_MODASS,
	OP_XORASS,
	OP_BANDASS,
	OP_BORASS,
	OP_LSHIFT,
	OP_RSHIFT,
	OP_RSHIFTASS,
	OP_LSHIFTASS,
	OP_EQ,
	OP_NE,
	OP_LE,
	OP_GE,
	OP_LAND,
	OP_LOR,
	OP_INC,
	OP_DEC,
	OP_COMMA,
	OP_ARROWSTAR,
	OP_ARROW,
};

// StringToETokenTypeMap map of `simple` `preprocessing-tokens` to ETokenType
const unordered_map<string, ETokenType> StringToTokenTypeMap =
{
	// keywords
	{"alignas", KW_ALIGNAS},
	{"alignof", KW_ALIGNOF},
	{"asm", KW_ASM},
	{"auto", KW_AUTO},
	{"bool", KW_BOOL},
	{"break", KW_BREAK},
	{"case", KW_CASE},
	{"catch", KW_CATCH},
	{"char", KW_CHAR},
	{"char16_t", KW_CHAR16_T},
	{"char32_t", KW_CHAR32_T},
	{"class", KW_CLASS},
	{"const", KW_CONST},
	{"constexpr", KW_CONSTEXPR},
	{"const_cast", KW_CONST_CAST},
	{"continue", KW_CONTINUE},
	{"decltype", KW_DECLTYPE},
	{"default", KW_DEFAULT},
	{"delete", KW_DELETE},
	{"do", KW_DO},
	{"double", KW_DOUBLE},
	{"dynamic_cast", KW_DYNAMIC_CAST},
	{"else", KW_ELSE},
	{"enum", KW_ENUM},
	{"explicit", KW_EXPLICIT},
	{"export", KW_EXPORT},
	{"extern", KW_EXTERN},
	{"false", KW_FALSE},
	{"float", KW_FLOAT},
	{"for", KW_FOR},
	{"friend", KW_FRIEND},
	{"goto", KW_GOTO},
	{"if", KW_IF},
	{"inline", KW_INLINE},
	{"int", KW_INT},
	{"long", KW_LONG},
	{"mutable", KW_MUTABLE},
	{"namespace", KW_NAMESPACE},
	{"new", KW_NEW},
	{"noexcept", KW_NOEXCEPT},
	{"nullptr", KW_NULLPTR},
	{"operator", KW_OPERATOR},
	{"private", KW_PRIVATE},
	{"protected", KW_PROTECTED},
	{"public", KW_PUBLIC},
	{"register", KW_REGISTER},
	{"reinterpret_cast", KW_REINTERPET_CAST},
	{"return", KW_RETURN},
	{"short", KW_SHORT},
	{"signed", KW_SIGNED},
	{"sizeof", KW_SIZEOF},
	{"static", KW_STATIC},
	{"static_assert", KW_STATIC_ASSERT},
	{"static_cast", KW_STATIC_CAST},
	{"struct", KW_STRUCT},
	{"switch", KW_SWITCH},
	{"template", KW_TEMPLATE},
	{"this", KW_THIS},
	{"thread_local", KW_THREAD_LOCAL},
	{"throw", KW_THROW},
	{"true", KW_TRUE},
	{"try", KW_TRY},
	{"typedef", KW_TYPEDEF},
	{"typeid", KW_TYPEID},
	{"typename", KW_TYPENAME},
	{"union", KW_UNION},
	{"unsigned", KW_UNSIGNED},
	{"using", KW_USING},
	{"virtual", KW_VIRTUAL},
	{"void", KW_VOID},
	{"volatile", KW_VOLATILE},
	{"wchar_t", KW_WCHAR_T},
	{"while", KW_WHILE},

	// operators/punctuation
	{"{", OP_LBRACE},
	{"<%", OP_LBRACE},
	{"}", OP_RBRACE},
	{"%>", OP_RBRACE},
	{"[", OP_LSQUARE},
	{"<:", OP_LSQUARE},
	{"]", OP_RSQUARE},
	{":>", OP_RSQUARE},
	{"(", OP_LPAREN},
	{")", OP_RPAREN},
	{"|", OP_BOR},
	{"bitor", OP_BOR},
	{"^", OP_XOR},
	{"xor", OP_XOR},
	{"~", OP_COMPL},
	{"compl", OP_COMPL},
	{"&", OP_AMP},
	{"bitand", OP_AMP},
	{"!", OP_LNOT},
	{"not", OP_LNOT},
	{";", OP_SEMICOLON},
	{":", OP_COLON},
	{"...", OP_DOTS},
	{"?", OP_QMARK},
	{"::", OP_COLON2},
	{".", OP_DOT},
	{".*", OP_DOTSTAR},
	{"+", OP_PLUS},
	{"-", OP_MINUS},
	{"*", OP_STAR},
	{"/", OP_DIV},
	{"%", OP_MOD},
	{"=", OP_ASS},
	{"<", OP_LT},
	{">", OP_GT},
	{"+=", OP_PLUSASS},
	{"-=", OP_MINUSASS},
	{"*=", OP_STARASS},
	{"/=", OP_DIVASS},
	{"%=", OP_MODASS},
	{"^=", OP_XORASS},
	{"xor_eq", OP_XORASS},
	{"&=", OP_BANDASS},
	{"and_eq", OP_BANDASS},
	{"|=", OP_BORASS},
	{"or_eq", OP_BORASS},
	{"<<", OP_LSHIFT},
	{">>", OP_RSHIFT},
	{">>=", OP_RSHIFTASS},
	{"<<=", OP_LSHIFTASS},
	{"==", OP_EQ},
	{"!=", OP_NE},
	{"not_eq", OP_NE},
	{"<=", OP_LE},
	{">=", OP_GE},
	{"&&", OP_LAND},
	{"and", OP_LAND},
	{"||", OP_LOR},
	{"or", OP_LOR},
	{"++", OP_INC},
	{"--", OP_DEC},
	{",", OP_COMMA},
	{"->*", OP_ARROWSTAR},
	{"->", OP_ARROW}
};

// map of enum to string
const map<ETokenType, string> TokenTypeToStringMap =
{
	{KW_ALIGNAS, "KW_ALIGNAS"},
	{KW_ALIGNOF, "KW_ALIGNOF"},
	{KW_ASM, "KW_ASM"},
	{KW_AUTO, "KW_AUTO"},
	{KW_BOOL, "KW_BOOL"},
	{KW_BREAK, "KW_BREAK"},
	{KW_CASE, "KW_CASE"},
	{KW_CATCH, "KW_CATCH"},
	{KW_CHAR, "KW_CHAR"},
	{KW_CHAR16_T, "KW_CHAR16_T"},
	{KW_CHAR32_T, "KW_CHAR32_T"},
	{KW_CLASS, "KW_CLASS"},
	{KW_CONST, "KW_CONST"},
	{KW_CONSTEXPR, "KW_CONSTEXPR"},
	{KW_CONST_CAST, "KW_CONST_CAST"},
	{KW_CONTINUE, "KW_CONTINUE"},
	{KW_DECLTYPE, "KW_DECLTYPE"},
	{KW_DEFAULT, "KW_DEFAULT"},
	{KW_DELETE, "KW_DELETE"},
	{KW_DO, "KW_DO"},
	{KW_DOUBLE, "KW_DOUBLE"},
	{KW_DYNAMIC_CAST, "KW_DYNAMIC_CAST"},
	{KW_ELSE, "KW_ELSE"},
	{KW_ENUM, "KW_ENUM"},
	{KW_EXPLICIT, "KW_EXPLICIT"},
	{KW_EXPORT, "KW_EXPORT"},
	{KW_EXTERN, "KW_EXTERN"},
	{KW_FALSE, "KW_FALSE"},
	{KW_FLOAT, "KW_FLOAT"},
	{KW_FOR, "KW_FOR"},
	{KW_FRIEND, "KW_FRIEND"},
	{KW_GOTO, "KW_GOTO"},
	{KW_IF, "KW_IF"},
	{KW_INLINE, "KW_INLINE"},
	{KW_INT, "KW_INT"},
	{KW_LONG, "KW_LONG"},
	{KW_MUTABLE, "KW_MUTABLE"},
	{KW_NAMESPACE, "KW_NAMESPACE"},
	{KW_NEW, "KW_NEW"},
	{KW_NOEXCEPT, "KW_NOEXCEPT"},
	{KW_NULLPTR, "KW_NULLPTR"},
	{KW_OPERATOR, "KW_OPERATOR"},
	{KW_PRIVATE, "KW_PRIVATE"},
	{KW_PROTECTED, "KW_PROTECTED"},
	{KW_PUBLIC, "KW_PUBLIC"},
	{KW_REGISTER, "KW_REGISTER"},
	{KW_REINTERPET_CAST, "KW_REINTERPET_CAST"},
	{KW_RETURN, "KW_RETURN"},
	{KW_SHORT, "KW_SHORT"},
	{KW_SIGNED, "KW_SIGNED"},
	{KW_SIZEOF, "KW_SIZEOF"},
	{KW_STATIC, "KW_STATIC"},
	{KW_STATIC_ASSERT, "KW_STATIC_ASSERT"},
	{KW_STATIC_CAST, "KW_STATIC_CAST"},
	{KW_STRUCT, "KW_STRUCT"},
	{KW_SWITCH, "KW_SWITCH"},
	{KW_TEMPLATE, "KW_TEMPLATE"},
	{KW_THIS, "KW_THIS"},
	{KW_THREAD_LOCAL, "KW_THREAD_LOCAL"},
	{KW_THROW, "KW_THROW"},
	{KW_TRUE, "KW_TRUE"},
	{KW_TRY, "KW_TRY"},
	{KW_TYPEDEF, "KW_TYPEDEF"},
	{KW_TYPEID, "KW_TYPEID"},
	{KW_TYPENAME, "KW_TYPENAME"},
	{KW_UNION, "KW_UNION"},
	{KW_UNSIGNED, "KW_UNSIGNED"},
	{KW_USING, "KW_USING"},
	{KW_VIRTUAL, "KW_VIRTUAL"},
	{KW_VOID, "KW_VOID"},
	{KW_VOLATILE, "KW_VOLATILE"},
	{KW_WCHAR_T, "KW_WCHAR_T"},
	{KW_WHILE, "KW_WHILE"},
	{OP_LBRACE, "OP_LBRACE"},
	{OP_RBRACE, "OP_RBRACE"},
	{OP_LSQUARE, "OP_LSQUARE"},
	{OP_RSQUARE, "OP_RSQUARE"},
	{OP_LPAREN, "OP_LPAREN"},
	{OP_RPAREN, "OP_RPAREN"},
	{OP_BOR, "OP_BOR"},
	{OP_XOR, "OP_XOR"},
	{OP_COMPL, "OP_COMPL"},
	{OP_AMP, "OP_AMP"},
	{OP_LNOT, "OP_LNOT"},
	{OP_SEMICOLON, "OP_SEMICOLON"},
	{OP_COLON, "OP_COLON"},
	{OP_DOTS, "OP_DOTS"},
	{OP_QMARK, "OP_QMARK"},
	{OP_COLON2, "OP_COLON2"},
	{OP_DOT, "OP_DOT"},
	{OP_DOTSTAR, "OP_DOTSTAR"},
	{OP_PLUS, "OP_PLUS"},
	{OP_MINUS, "OP_MINUS"},
	{OP_STAR, "OP_STAR"},
	{OP_DIV, "OP_DIV"},
	{OP_MOD, "OP_MOD"},
	{OP_ASS, "OP_ASS"},
	{OP_LT, "OP_LT"},
	{OP_GT, "OP_GT"},
	{OP_PLUSASS, "OP_PLUSASS"},
	{OP_MINUSASS, "OP_MINUSASS"},
	{OP_STARASS, "OP_STARASS"},
	{OP_DIVASS, "OP_DIVASS"},
	{OP_MODASS, "OP_MODASS"},
	{OP_XORASS, "OP_XORASS"},
	{OP_BANDASS, "OP_BANDASS"},
	{OP_BORASS, "OP_BORASS"},
	{OP_LSHIFT, "OP_LSHIFT"},
	{OP_RSHIFT, "OP_RSHIFT"},
	{OP_RSHIFTASS, "OP_RSHIFTASS"},
	{OP_LSHIFTASS, "OP_LSHIFTASS"},
	{OP_EQ, "OP_EQ"},
	{OP_NE, "OP_NE"},
	{OP_LE, "OP_LE"},
	{OP_GE, "OP_GE"},
	{OP_LAND, "OP_LAND"},
	{OP_LOR, "OP_LOR"},
	{OP_INC, "OP_INC"},
	{OP_DEC, "OP_DEC"},
	{OP_COMMA, "OP_COMMA"},
	{OP_ARROWSTAR, "OP_ARROWSTAR"},
	{OP_ARROW, "OP_ARROW"}
};

// convert integer [0,15] to hexadecimal digit
char ValueToHexChar(int c)
{
	switch (c)
	{
	case 0: return '0';
	case 1: return '1';
	case 2: return '2';
	case 3: return '3';
	case 4: return '4';
	case 5: return '5';
	case 6: return '6';
	case 7: return '7';
	case 8: return '8';
	case 9: return '9';
	case 10: return 'A';
	case 11: return 'B';
	case 12: return 'C';
	case 13: return 'D';
	case 14: return 'E';
	case 15: return 'F';
	default: throw logic_error("ValueToHexChar of nonhex value");
	}
}

// hex dump memory range
string HexDump(const void* pdata, size_t nbytes)
{
	unsigned char* p = (unsigned char*) pdata;

	string s(nbytes*2, '?');

	for (size_t i = 0; i < nbytes; i++)
	{
		s[2*i+0] = ValueToHexChar((p[i] & 0xF0) >> 4);
		s[2*i+1] = ValueToHexChar((p[i] & 0x0F) >> 0);
	}

	return s;
}

// DebugPostTokenOutputStream: helper class to produce PA2 output format
struct DebugPostTokenOutputStream
{
	explicit DebugPostTokenOutputStream(ostream& output)
		: output(output), valid(true) {}
	ostream& output;
	bool valid;

	// output: invalid <source>
	void emit_invalid(const string& source)
	{
		valid = false;
		output << "invalid " << source << '\n';
	}

	// output: simple <source> <token_type>
	void emit_simple(const string& source, ETokenType token_type)
	{
		output << "simple " << source << " " << TokenTypeToStringMap.at(token_type) << '\n';
	}

	// output: identifier <source>
	void emit_identifier(const string& source)
	{
		output << "identifier " << source << '\n';
	}

	// output: literal <source> <type> <hexdump(data,nbytes)>
	void emit_literal(const string& source, EFundamentalType type, const void* data, size_t nbytes)
	{
		output << "literal " << source << " " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << '\n';
	}

	// output: literal <source> array of <num_elements> <type> <hexdump(data,nbytes)>
	void emit_literal_array(const string& source, size_t num_elements, EFundamentalType type, const void* data, size_t nbytes)
	{
		output << "literal " << source << " array of " << num_elements << " " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << '\n';
	}

	// output: user-defined-literal <source> <ud_suffix> character <type> <hexdump(data,nbytes)>
	void emit_user_defined_literal_character(const string& source, const string& ud_suffix, EFundamentalType type, const void* data, size_t nbytes)
	{
		output << "user-defined-literal " << source << " " << ud_suffix << " character " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << '\n';
	}

	// output: user-defined-literal <source> <ud_suffix> string array of <num_elements> <type> <hexdump(data, nbytes)>
	void emit_user_defined_literal_string_array(const string& source, const string& ud_suffix, size_t num_elements, EFundamentalType type, const void* data, size_t nbytes)
	{
		output << "user-defined-literal " << source << " " << ud_suffix << " string array of " << num_elements << " " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << '\n';
	}

	// output: user-defined-literal <source> <ud_suffix> <prefix>
	void emit_user_defined_literal_integer(const string& source, const string& ud_suffix, const string& prefix)
	{
		output << "user-defined-literal " << source << " " << ud_suffix << " integer " << prefix << '\n';
	}

	// output: user-defined-literal <source> <ud_suffix> <prefix>
	void emit_user_defined_literal_floating(const string& source, const string& ud_suffix, const string& prefix)
	{
		output << "user-defined-literal " << source << " " << ud_suffix << " floating " << prefix << '\n';
	}

	// output : eof
	void emit_eof()
	{
		output << "eof\n";
		output.flush();
	}
};


// use these 3 functions to scan `floating-literals` (see PA2)
// for example PA2Decode_float("12.34") returns "12.34" as a `float` type
float PA2Decode_float(const string& s)
{
	istringstream iss(s);
	float x;
	iss >> x;
	return x;
}

double PA2Decode_double(const string& s)
{
	istringstream iss(s);
	double x;
	iss >> x;
	return x;
}

long double PA2Decode_long_double(const string& s)
{
	istringstream iss(s);
	long double x;
	iss >> x;
	return x;
}

namespace
{

struct Utf8Point
{
	uint32_t value;
	size_t width;
};

bool DecodeUtf8Point(const string& text, size_t offset, Utf8Point& point)
{
	if (offset >= text.size()) return false;
	const unsigned char first = static_cast<unsigned char>(text[offset]);
	if (first < 0x80)
	{
		point.value = first;
		point.width = 1;
		return true;
	}
	uint32_t value;
	uint32_t minimum;
	if (first >= 0xC2 && first <= 0xDF)
	{
		value = first & 0x1F;
		minimum = 0x80;
		point.width = 2;
	}
	else if (first >= 0xE0 && first <= 0xEF)
	{
		value = first & 0x0F;
		minimum = 0x800;
		point.width = 3;
	}
	else if (first >= 0xF0 && first <= 0xF4)
	{
		value = first & 0x07;
		minimum = 0x10000;
		point.width = 4;
	}
	else
		return false;
	if (point.width > text.size() - offset) return false;
	for (size_t i = 1; i < point.width; ++i)
	{
		const unsigned char next = static_cast<unsigned char>(text[offset + i]);
		if ((next & 0xC0) != 0x80) return false;
		value = (value << 6) | (next & 0x3F);
	}
	if (value < minimum || value > 0x10FFFF ||
		(value >= 0xD800 && value <= 0xDFFF))
		return false;
	point.value = value;
	return true;
}

bool IsUnicodeScalar(uint32_t value)
{
	return value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF);
}

void AppendUtf8(vector<unsigned char>& bytes, uint32_t value)
{
	if (value <= 0x7F)
		bytes.push_back(static_cast<unsigned char>(value));
	else if (value <= 0x7FF)
	{
		bytes.push_back(static_cast<unsigned char>(0xC0 | (value >> 6)));
		bytes.push_back(static_cast<unsigned char>(0x80 | (value & 0x3F)));
	}
	else if (value <= 0xFFFF)
	{
		bytes.push_back(static_cast<unsigned char>(0xE0 | (value >> 12)));
		bytes.push_back(static_cast<unsigned char>(0x80 | ((value >> 6) & 0x3F)));
		bytes.push_back(static_cast<unsigned char>(0x80 | (value & 0x3F)));
	}
	else
	{
		bytes.push_back(static_cast<unsigned char>(0xF0 | (value >> 18)));
		bytes.push_back(static_cast<unsigned char>(0x80 | ((value >> 12) & 0x3F)));
		bytes.push_back(static_cast<unsigned char>(0x80 | ((value >> 6) & 0x3F)));
		bytes.push_back(static_cast<unsigned char>(0x80 | (value & 0x3F)));
	}
}

void AppendLittleEndian(vector<unsigned char>& bytes, uint64_t value,
	size_t width)
{
	for (size_t i = 0; i < width; ++i)
		bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
}

template<typename T>
vector<unsigned char> ObjectBytes(const T& value)
{
	vector<unsigned char> bytes(sizeof(T));
	memcpy(&bytes[0], &value, sizeof(T));
	return bytes;
}

vector<unsigned char> LongDoubleBytes(const long double& value)
{
	vector<unsigned char> bytes(sizeof(long double), 0);
	// The x86-64 System V ABI stores an 80-bit extended value in a 16-byte
	// slot. Its six padding bytes are not part of the value representation.
	const size_t value_bytes = sizeof(long double) == 16 ? 10 : sizeof(long double);
	memcpy(&bytes[0], &value, value_bytes);
	return bytes;
}

bool IsDecimalDigit(char c)
{
	return c >= '0' && c <= '9';
}

int DigitValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

struct IntegerCore
{
	unsigned base;
	uint64_t value;
	bool fits_uint64;
};

bool ParseIntegerCore(const string& text, IntegerCore& result)
{
	if (text.empty()) return false;
	size_t position = 0;
	if (text.size() >= 2 && text[0] == '0' &&
		(text[1] == 'x' || text[1] == 'X'))
	{
		result.base = 16;
		position = 2;
	}
	else if (text[0] == '0')
	{
		result.base = 8;
	}
	else
		result.base = 10;
	if (position == text.size()) return false;
	result.value = 0;
	result.fits_uint64 = true;
	const size_t first_digit = position;
	for (; position < text.size(); ++position)
	{
		const int digit = DigitValue(text[position]);
		if (digit < 0 || static_cast<unsigned>(digit) >= result.base)
			return false;
		if (result.fits_uint64)
		{
			if (result.value > (numeric_limits<uint64_t>::max() -
				static_cast<unsigned>(digit)) / result.base)
				result.fits_uint64 = false;
			else
				result.value = result.value * result.base +
					static_cast<unsigned>(digit);
		}
	}
	return position > first_digit;
}

bool ParseIntegerSuffix(const string& suffix, bool& is_unsigned,
	unsigned& long_count)
{
	is_unsigned = false;
	long_count = 0;
	if (suffix.empty()) return true;
	size_t position = 0;
	if (suffix[position] == 'u' || suffix[position] == 'U')
	{
		is_unsigned = true;
		++position;
	}
	if (position < suffix.size() &&
		(suffix[position] == 'l' || suffix[position] == 'L'))
	{
		const char first = suffix[position++];
		long_count = 1;
		if (position < suffix.size() && suffix[position] == first)
		{
			++position;
			long_count = 2;
		}
	}
	if (!is_unsigned && position < suffix.size() &&
		(suffix[position] == 'u' || suffix[position] == 'U'))
	{
		is_unsigned = true;
		++position;
	}
	return position == suffix.size();
}

uint64_t IntegerTypeMaximum(EFundamentalType type)
{
	switch (type)
	{
	case FT_INT: return 0x7FFFFFFFULL;
	case FT_UNSIGNED_INT: return 0xFFFFFFFFULL;
	case FT_LONG_INT:
	case FT_LONG_LONG_INT: return 0x7FFFFFFFFFFFFFFFULL;
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT: return 0xFFFFFFFFFFFFFFFFULL;
	default: return 0;
	}
}

size_t IntegerTypeWidth(EFundamentalType type)
{
	switch (type)
	{
	case FT_INT:
	case FT_UNSIGNED_INT: return 4;
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT: return 8;
	default: return 0;
	}
}

bool SelectIntegerType(const IntegerCore& core, bool is_unsigned,
	unsigned long_count, EFundamentalType& type)
{
	if (!core.fits_uint64) return false;
	EFundamentalType candidates[6];
	size_t count = 0;
	if (core.base == 10)
	{
		if (is_unsigned)
		{
			if (long_count == 0)
			{
				candidates[count++] = FT_UNSIGNED_INT;
				candidates[count++] = FT_UNSIGNED_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
			}
			else if (long_count == 1)
			{
				candidates[count++] = FT_UNSIGNED_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
			}
			else
				candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
		}
		else if (long_count == 0)
		{
			candidates[count++] = FT_INT;
			candidates[count++] = FT_LONG_INT;
			candidates[count++] = FT_LONG_LONG_INT;
		}
		else if (long_count == 1)
		{
			candidates[count++] = FT_LONG_INT;
			candidates[count++] = FT_LONG_LONG_INT;
		}
		else
			candidates[count++] = FT_LONG_LONG_INT;
	}
	else
	{
		if (long_count == 0)
		{
			if (is_unsigned)
			{
				candidates[count++] = FT_UNSIGNED_INT;
				candidates[count++] = FT_UNSIGNED_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
			}
			else
			{
				candidates[count++] = FT_INT;
				candidates[count++] = FT_UNSIGNED_INT;
				candidates[count++] = FT_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_INT;
				candidates[count++] = FT_LONG_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
			}
		}
		else if (long_count == 1)
		{
			if (is_unsigned)
			{
				candidates[count++] = FT_UNSIGNED_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
			}
			else
			{
				candidates[count++] = FT_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_INT;
				candidates[count++] = FT_LONG_LONG_INT;
				candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
			}
		}
		else if (is_unsigned)
			candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
		else
		{
			candidates[count++] = FT_LONG_LONG_INT;
			candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
		}
	}
	for (size_t i = 0; i < count; ++i)
	{
		if (core.value <= IntegerTypeMaximum(candidates[i]))
		{
			type = candidates[i];
			return true;
		}
	}
	return false;
}

bool ParseFloatingCore(const string& text, char& suffix)
{
	suffix = 0;
	if (text.empty()) return false;
	size_t end = text.size();
	const char last = text[end - 1];
	if (last == 'f' || last == 'F' || last == 'l' || last == 'L')
	{
		suffix = last;
		--end;
	}
	if (end == 0) return false;
	size_t position = 0;
	bool has_point = false;
	bool has_exponent = false;
	if (text[position] == '.')
	{
		has_point = true;
		++position;
		const size_t digits = position;
		while (position < end && IsDecimalDigit(text[position])) ++position;
		if (position == digits) return false;
	}
	else
	{
		const size_t digits = position;
		while (position < end && IsDecimalDigit(text[position])) ++position;
		if (position == digits) return false;
		if (position < end && text[position] == '.')
		{
			has_point = true;
			++position;
			while (position < end && IsDecimalDigit(text[position])) ++position;
		}
	}
	if (position < end && (text[position] == 'e' || text[position] == 'E'))
	{
		has_exponent = true;
		++position;
		if (position < end && (text[position] == '+' || text[position] == '-'))
			++position;
		const size_t digits = position;
		while (position < end && IsDecimalDigit(text[position])) ++position;
		if (position == digits) return false;
	}
	return position == end && (has_point || has_exponent);
}

bool IsValidUdSuffix(const string& suffix)
{
	return !suffix.empty() && suffix[0] == '_' &&
		IsValidIdentifierName(suffix);
}

void EmitInvalid(DebugPostTokenOutputStream& output, const string& source)
{
	output.emit_invalid(source);
}

void EmitInteger(DebugPostTokenOutputStream& output, const string& source,
	const IntegerCore& core, EFundamentalType type)
{
	vector<unsigned char> bytes;
	AppendLittleEndian(bytes, core.value, IntegerTypeWidth(type));
	output.emit_literal(source, type, &bytes[0], bytes.size());
}

void EmitFloating(DebugPostTokenOutputStream& output, const string& source,
	const string& spelling, char suffix)
{
	const string number = suffix == 0 ? spelling : spelling.substr(0, spelling.size() - 1);
	if (suffix == 'f' || suffix == 'F')
	{
		const float value = PA2Decode_float(number);
		const vector<unsigned char> bytes = ObjectBytes(value);
		output.emit_literal(source, FT_FLOAT, &bytes[0], bytes.size());
	}
	else if (suffix == 'l' || suffix == 'L')
	{
		const long double value = PA2Decode_long_double(number);
		const vector<unsigned char> bytes = LongDoubleBytes(value);
		output.emit_literal(source, FT_LONG_DOUBLE, &bytes[0], bytes.size());
	}
	else
	{
		const double value = PA2Decode_double(number);
		const vector<unsigned char> bytes = ObjectBytes(value);
		output.emit_literal(source, FT_DOUBLE, &bytes[0], bytes.size());
	}
}

void PostTokenizeNumber(DebugPostTokenOutputStream& output, const string& source)
{
	const size_t underscore = source.find('_');
	if (underscore != string::npos)
	{
		const string prefix = source.substr(0, underscore);
		const string ud_suffix = source.substr(underscore);
		if (IsValidUdSuffix(ud_suffix))
		{
			IntegerCore integer;
			if (ParseIntegerCore(prefix, integer))
			{
				output.emit_user_defined_literal_integer(source, ud_suffix, prefix);
				return;
			}
			char float_suffix = 0;
			if (ParseFloatingCore(prefix, float_suffix) && float_suffix == 0)
			{
				output.emit_user_defined_literal_floating(source, ud_suffix, prefix);
				return;
			}
		}
	}

	char float_suffix = 0;
	if (ParseFloatingCore(source, float_suffix))
	{
		EmitFloating(output, source, source, float_suffix);
		return;
	}

	string integer_spelling = source;
	size_t suffix_begin = integer_spelling.size();
	while (suffix_begin > 0 &&
		(integer_spelling[suffix_begin - 1] == 'u' ||
		 integer_spelling[suffix_begin - 1] == 'U' ||
		 integer_spelling[suffix_begin - 1] == 'l' ||
		 integer_spelling[suffix_begin - 1] == 'L'))
		--suffix_begin;
	const string core_spelling = integer_spelling.substr(0, suffix_begin);
	const string suffix = integer_spelling.substr(suffix_begin);
	IntegerCore core;
	bool is_unsigned = false;
	unsigned long_count = 0;
	EFundamentalType type;
	if (ParseIntegerCore(core_spelling, core) &&
		ParseIntegerSuffix(suffix, is_unsigned, long_count) &&
		SelectIntegerType(core, is_unsigned, long_count, type))
	{
		EmitInteger(output, source, core, type);
		return;
	}
	EmitInvalid(output, source);
}

} // namespace

namespace
{

bool IsUnsignedIntegralType(EFundamentalType type)
{
	switch (type)
	{
	case FT_UNSIGNED_CHAR:
	case FT_UNSIGNED_SHORT_INT:
	case FT_UNSIGNED_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
	case FT_CHAR16_T:
	case FT_CHAR32_T:
		return true;
	default:
		return false;
	}
}

} // namespace

bool ParsePPIntegralLiteral(const string& spelling, PPIntegralLiteral& result)
{
	if (spelling.find('_') != string::npos)
		return false;
	char floating_suffix = 0;
	if (ParseFloatingCore(spelling, floating_suffix))
		return false;

	size_t suffix_begin = spelling.size();
	while (suffix_begin > 0 &&
		(spelling[suffix_begin - 1] == 'u' ||
		 spelling[suffix_begin - 1] == 'U' ||
		 spelling[suffix_begin - 1] == 'l' ||
		 spelling[suffix_begin - 1] == 'L'))
		--suffix_begin;
	const string core_spelling = spelling.substr(0, suffix_begin);
	const string suffix = spelling.substr(suffix_begin);
	IntegerCore core;
	bool is_unsigned = false;
	unsigned long_count = 0;
	EFundamentalType type;
	if (!ParseIntegerCore(core_spelling, core) ||
		!ParseIntegerSuffix(suffix, is_unsigned, long_count) ||
		!SelectIntegerType(core, is_unsigned, long_count, type))
		return false;
	result.value = core.value;
	result.is_unsigned = IsUnsignedIntegralType(type);
	return true;
}

namespace
{

struct LiteralView
{
	string encoding;
	string suffix;
	size_t content_begin;
	size_t content_end;
	size_t suffix_begin;
	bool raw;
};

struct DecodedAtom
{
	uint32_t value;
	bool numeric_escape;
};

bool SplitLiteral(const string& source, char quote, LiteralView& view);
bool DecodeLiteralContent(const string& source, const LiteralView& view,
	const vector<size_t>& ucn_backslash_offsets, vector<DecodedAtom>& atoms);
size_t EncodingWidth(const string& encoding);
EFundamentalType StringElementType(const string& encoding);
bool AppendStringAtom(vector<unsigned char>& bytes, const DecodedAtom& atom,
	const string& encoding, size_t width);
void PostTokenizeCharacter(DebugPostTokenOutputStream& output,
	const string& source, bool user_defined,
	const vector<size_t>& ucn_backslash_offsets);

struct StringPart
{
	string source;
	bool user_defined;
	vector<size_t> ucn_backslash_offsets;
};

size_t EncodingWidth(const string& encoding)
{
	return encoding == "u" ? 2 : (encoding == "" || encoding == "u8" ? 1 : 4);
}

EFundamentalType StringElementType(const string& encoding)
{
	if (encoding == "u") return FT_CHAR16_T;
	if (encoding == "U") return FT_CHAR32_T;
	if (encoding == "L") return FT_WCHAR_T;
	return FT_CHAR;
}

bool AppendStringAtom(vector<unsigned char>& bytes, const DecodedAtom& atom,
	const string& encoding, size_t width)
{
	if (atom.numeric_escape)
	{
		const uint64_t maximum = width == 1 ? 0xFFULL :
			(width == 2 ? 0xFFFFULL : 0xFFFFFFFFULL);
		if (atom.value > maximum) return false;
		AppendLittleEndian(bytes, atom.value, width);
		return true;
	}
	if (!IsUnicodeScalar(atom.value)) return false;
	if (encoding.empty() || encoding == "u8")
		AppendUtf8(bytes, atom.value);
	else if (encoding == "u")
	{
		if (atom.value <= 0xFFFF)
			AppendLittleEndian(bytes, atom.value, 2);
		else
		{
			const uint32_t adjusted = atom.value - 0x10000;
			AppendLittleEndian(bytes, 0xD800 + (adjusted >> 10), 2);
			AppendLittleEndian(bytes, 0xDC00 + (adjusted & 0x3FF), 2);
		}
	}
	else
		AppendLittleEndian(bytes, atom.value, 4);
	return true;
}

class PostTokenConsumer : public IPPTokenStream
{
public:
	explicit PostTokenConsumer(ostream& output)
		: output_(output), operator_literal_pending_(false) {}

	void emit_whitespace_sequence() {}
	void emit_new_line() {}

	void emit_header_name(const string& data)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		output_.emit_invalid(data);
	}

	void emit_identifier(const string& data)
	{
		FlushStringRun();
		const unordered_map<string, ETokenType>::const_iterator found =
			StringToTokenTypeMap.find(data);
		if (found != StringToTokenTypeMap.end())
			output_.emit_simple(data, found->second);
		else
			output_.emit_identifier(data);
		operator_literal_pending_ = data == "operator";
	}

	void emit_pp_number(const string& data)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		PostTokenizeNumber(output_, data);
	}

	void emit_character_literal(const string& data)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		PostTokenizeCharacter(output_, data, false, vector<size_t>());
	}

	void emit_character_literal(const string& data,
		const vector<size_t>& ucn_backslash_offsets)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		PostTokenizeCharacter(output_, data, false, ucn_backslash_offsets);
	}

	void emit_user_defined_character_literal(const string& data)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		PostTokenizeCharacter(output_, data, true, vector<size_t>());
	}

	void emit_user_defined_character_literal(const string& data,
		const vector<size_t>& ucn_backslash_offsets)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		PostTokenizeCharacter(output_, data, true, ucn_backslash_offsets);
	}

	void emit_string_literal(const string& data)
	{
		AddStringToken(data, false, vector<size_t>());
	}

	void emit_string_literal(const string& data,
		const vector<size_t>& ucn_backslash_offsets)
	{
		AddStringToken(data, false, ucn_backslash_offsets);
	}

	void emit_user_defined_string_literal(const string& data)
	{
		emit_user_defined_string_literal(data, vector<size_t>());
	}

	void emit_user_defined_string_literal(const string& data,
		const vector<size_t>& ucn_backslash_offsets)
	{
		if (operator_literal_pending_)
		{
			FlushStringRun();
			LiteralView view = {};
			if (!SplitLiteral(data, '"', view) || view.suffix.empty() ||
				view.suffix_begin != 2 || data.compare(0, 2, "\"\"") != 0)
			{
				operator_literal_pending_ = false;
				AddStringToken(data, true, ucn_backslash_offsets);
				return;
			}
			const string literal_source = data.substr(0, view.suffix_begin);
			vector<size_t> literal_ucn_offsets;
			for (size_t i = 0; i < ucn_backslash_offsets.size(); ++i)
				if (ucn_backslash_offsets[i] < view.suffix_begin)
					literal_ucn_offsets.push_back(ucn_backslash_offsets[i]);
			AddStringToken(literal_source, false, literal_ucn_offsets);
			FlushStringRun();
			output_.emit_identifier(view.suffix);
			operator_literal_pending_ = false;
			return;
		}
		AddStringToken(data, true, ucn_backslash_offsets);
	}

	void emit_preprocessing_op_or_punc(const string& data)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		if (data == "#" || data == "##" || data == "%:" || data == "%:%:")
		{
			output_.emit_invalid(data);
			return;
		}
		const unordered_map<string, ETokenType>::const_iterator found =
			StringToTokenTypeMap.find(data);
		if (found == StringToTokenTypeMap.end())
			output_.emit_invalid(data);
		else
			output_.emit_simple(data, found->second);
	}

	void emit_non_whitespace_char(const string& data)
	{
		FlushStringRun();
		operator_literal_pending_ = false;
		output_.emit_invalid(data);
	}

	void emit_eof()
	{
		FlushStringRun();
		output_.emit_eof();
	}

	bool isValid() const { return output_.valid; }

private:
	DebugPostTokenOutputStream output_;
	vector<StringPart> string_run_;
	bool operator_literal_pending_;

	void AddStringToken(const string& source, bool user_defined,
		const vector<size_t>& ucn_backslash_offsets)
	{
		operator_literal_pending_ = false;
		StringPart part = {source, user_defined, ucn_backslash_offsets};
		string_run_.push_back(part);
	}

	void FlushStringRun()
	{
		if (string_run_.empty()) return;
		string source;
		vector<LiteralView> views;
		views.reserve(string_run_.size());
		string encoding;
		string ud_suffix;
		bool invalid = false;
		for (size_t i = 0; i < string_run_.size(); ++i)
		{
			if (i != 0) source += ' ';
			source += string_run_[i].source;
			LiteralView view = {};
			const bool split = SplitLiteral(string_run_[i].source, '"', view);
			views.push_back(view);
			if (!split || (string_run_[i].user_defined
				? !IsValidUdSuffix(view.suffix)
				: !view.suffix.empty()))
			{
				invalid = true;
				continue;
			}
			if (!view.encoding.empty())
			{
				if (encoding.empty()) encoding = view.encoding;
				else if (encoding != view.encoding) invalid = true;
			}
			if (!view.suffix.empty())
			{
				if (ud_suffix.empty()) ud_suffix = view.suffix;
				else if (ud_suffix != view.suffix) invalid = true;
			}
		}
		if (!invalid)
		{
			const size_t width = EncodingWidth(encoding);
			vector<unsigned char> bytes;
			for (size_t i = 0; i < string_run_.size() && !invalid; ++i)
			{
				vector<DecodedAtom> atoms;
				if (!DecodeLiteralContent(string_run_[i].source, views[i],
					string_run_[i].ucn_backslash_offsets, atoms))
				{
					invalid = true;
					break;
				}
				for (size_t j = 0; j < atoms.size(); ++j)
					if (!AppendStringAtom(bytes, atoms[j], encoding, width))
					{
						invalid = true;
						break;
					}
			}
			if (!invalid)
			{
				AppendLittleEndian(bytes, 0, width);
				const EFundamentalType type = StringElementType(encoding);
				const size_t elements = bytes.size() / width;
				if (ud_suffix.empty())
					output_.emit_literal_array(source, elements, type,
						&bytes[0], bytes.size());
				else
					output_.emit_user_defined_literal_string_array(source,
						ud_suffix, elements, type, &bytes[0], bytes.size());
			}
		}
		if (invalid) output_.emit_invalid(source);
		string_run_.clear();
	}
};

} // namespace

void PostTokenizeSource(const string& source)
{
	PostTokenConsumer output(cout);
	TokenizePreprocessingSource(source, output);
}

bool PostTokenizePreprocessingTokens(
	const vector<PreprocessingToken>& tokens, ostream& output, bool emit_eof)
{
	PostTokenStreamWriter writer(output);
	for (size_t i = 0; i < tokens.size(); ++i)
		if (!writer.emit(tokens[i])) return false;
	return writer.finish(emit_eof);
}

class PostTokenStreamWriter::Impl
{
public:
	explicit Impl(ostream& output) : consumer(output) {}
	PostTokenConsumer consumer;
};

PostTokenStreamWriter::PostTokenStreamWriter(ostream& output)
	: impl_(new Impl(output))
{}

PostTokenStreamWriter::~PostTokenStreamWriter()
{
	delete impl_;
}

bool PostTokenStreamWriter::emit(const PreprocessingToken& token)
{
	switch (token.kind)
	{
	case PP_TOKEN_WHITESPACE:
		impl_->consumer.emit_whitespace_sequence();
		break;
	case PP_TOKEN_NEWLINE:
		impl_->consumer.emit_new_line();
		break;
	case PP_TOKEN_HEADER_NAME:
		impl_->consumer.emit_header_name(token.spelling);
		break;
	case PP_TOKEN_IDENTIFIER:
		if (token.identifier_spelling)
			impl_->consumer.emit_identifier(*token.identifier_spelling);
		else
			impl_->consumer.emit_non_whitespace_char(token.spelling);
		break;
	case PP_TOKEN_NUMBER:
		impl_->consumer.emit_pp_number(token.spelling);
		break;
	case PP_TOKEN_CHARACTER:
		impl_->consumer.emit_character_literal(token.spelling,
			token.ucn_backslash_offsets);
		break;
	case PP_TOKEN_USER_CHARACTER:
		impl_->consumer.emit_user_defined_character_literal(token.spelling,
			token.ucn_backslash_offsets);
		break;
	case PP_TOKEN_STRING:
		impl_->consumer.emit_string_literal(token.spelling,
			token.ucn_backslash_offsets);
		break;
	case PP_TOKEN_USER_STRING:
		impl_->consumer.emit_user_defined_string_literal(token.spelling,
			token.ucn_backslash_offsets);
		break;
	case PP_TOKEN_PUNCTUATOR:
		impl_->consumer.emit_preprocessing_op_or_punc(token.spelling);
		break;
	case PP_TOKEN_OTHER:
		impl_->consumer.emit_non_whitespace_char(token.spelling);
		break;
	case PP_TOKEN_EOF:
		break;
	}
	return impl_->consumer.isValid();
}

bool PostTokenStreamWriter::finish(bool emit_eof)
{
	if (emit_eof) impl_->consumer.emit_eof();
	return impl_->consumer.isValid();
}

namespace
{

bool SplitLiteral(const string& source, char quote, LiteralView& view)
{
	const size_t opening = source.find(quote);
	if (opening == string::npos) return false;
	view.raw = quote == '"' && opening > 0 && source[opening - 1] == 'R';
	view.encoding = source.substr(0, opening);
	if (view.raw) view.encoding.erase(view.encoding.size() - 1);
	if (quote == '"')
	{
		if (view.encoding != "" && view.encoding != "u8" &&
			view.encoding != "u" && view.encoding != "U" &&
			view.encoding != "L")
			return false;
	}
	else if (view.encoding != "" && view.encoding != "u" &&
		view.encoding != "U" && view.encoding != "L")
		return false;

	if (view.raw)
	{
		const size_t open_paren = source.find('(', opening + 1);
		if (open_paren == string::npos) return false;
		const string delimiter = source.substr(opening + 1,
			open_paren - opening - 1);
		const string terminator = ")" + delimiter + "\"";
		const size_t close = source.rfind(terminator);
		if (close == string::npos || close < open_paren + 1) return false;
		view.content_begin = open_paren + 1;
		view.content_end = close;
		view.suffix_begin = close + terminator.size();
	}
	else
	{
		const size_t close = source.rfind(quote);
		if (close == string::npos || close < opening + 1) return false;
		view.content_begin = opening + 1;
		view.content_end = close;
		view.suffix_begin = close + 1;
	}
	if (view.suffix_begin > source.size()) return false;
	view.suffix = source.substr(view.suffix_begin);
	return true;
}

bool DecodeEscapeValue(const string& source, size_t& position, size_t end,
	DecodedAtom& atom)
{
	if (position >= end || source[position] != '\\') return false;
	++position;
	if (position >= end) return false;
	const char escape = source[position++];
	atom.numeric_escape = false;
	switch (escape)
	{
	case '\'': atom.value = '\''; return true;
	case '"': atom.value = '"'; return true;
	case '?': atom.value = '?'; return true;
	case '\\': atom.value = '\\'; return true;
	case 'a': atom.value = 7; return true;
	case 'b': atom.value = 8; return true;
	case 'f': atom.value = 12; return true;
	case 'n': atom.value = 10; return true;
	case 'r': atom.value = 13; return true;
	case 't': atom.value = 9; return true;
	case 'v': atom.value = 11; return true;
	default: break;
	}
	if (escape >= '0' && escape <= '7')
	{
		uint64_t value = static_cast<unsigned>(escape - '0');
		unsigned count = 1;
		while (count < 3 && position < end &&
			source[position] >= '0' && source[position] <= '7')
		{
			value = value * 8 + static_cast<unsigned>(source[position] - '0');
			++position;
			++count;
		}
		atom.value = static_cast<uint32_t>(value);
		atom.numeric_escape = true;
		return true;
	}
	if (escape == 'x')
	{
		if (position >= end || DigitValue(source[position]) < 0 ||
			DigitValue(source[position]) >= 16)
			return false;
		uint64_t value = 0;
		while (position < end)
		{
			const int digit = DigitValue(source[position]);
			if (digit < 0 || digit >= 16) break;
			if (value > (numeric_limits<uint32_t>::max() -
				static_cast<unsigned>(digit)) / 16)
				return false;
			value = value * 16 + static_cast<unsigned>(digit);
			++position;
		}
		atom.value = static_cast<uint32_t>(value);
		atom.numeric_escape = true;
		return true;
	}
	return false;
}

bool DecodeLiteralContent(const string& source, const LiteralView& view,
	const vector<size_t>& ucn_backslash_offsets, vector<DecodedAtom>& atoms)
{
	size_t position = view.content_begin;
	size_t ucn_index = 0;
	while (ucn_index < ucn_backslash_offsets.size() &&
		ucn_backslash_offsets[ucn_index] < position)
		++ucn_index;
	while (position < view.content_end)
	{
		while (ucn_index < ucn_backslash_offsets.size() &&
			ucn_backslash_offsets[ucn_index] < position)
			++ucn_index;
		const bool ucn_backslash = ucn_index < ucn_backslash_offsets.size() &&
			ucn_backslash_offsets[ucn_index] == position;
		if (!view.raw && source[position] == '\\' && !ucn_backslash)
		{
			DecodedAtom atom;
			if (!DecodeEscapeValue(source, position, view.content_end, atom))
				return false;
			atoms.push_back(atom);
			continue;
		}
		if (ucn_backslash) ++ucn_index;
		Utf8Point point;
		if (!DecodeUtf8Point(source, position, point) ||
			point.width > view.content_end - position)
			return false;
		DecodedAtom atom = {point.value, false};
		atoms.push_back(atom);
		position += point.width;
	}
	return true;
}

void PostTokenizeCharacter(DebugPostTokenOutputStream& output,
	const string& source, bool user_defined,
	const vector<size_t>& ucn_backslash_offsets)
{
	LiteralView view;
	vector<DecodedAtom> atoms;
	if (!SplitLiteral(source, '\'', view) || view.raw ||
		!DecodeLiteralContent(source, view, ucn_backslash_offsets, atoms) ||
		atoms.size() != 1 ||
		!IsUnicodeScalar(atoms[0].value) ||
		(user_defined ? !IsValidUdSuffix(view.suffix) : !view.suffix.empty()))
	{
		EmitInvalid(output, source);
		return;
	}

	EFundamentalType type;
	const uint32_t value = atoms[0].value;
	if (view.encoding.empty())
		type = value <= 127 ? FT_CHAR : FT_INT;
	else if (view.encoding == "u")
	{
		if (value > 0xFFFF) { EmitInvalid(output, source); return; }
		type = FT_CHAR16_T;
	}
	else if (view.encoding == "U")
		type = FT_CHAR32_T;
	else if (view.encoding == "L")
		type = FT_WCHAR_T;
	else
	{
		EmitInvalid(output, source);
		return;
	}

	vector<unsigned char> bytes;
	if (type == FT_CHAR)
		AppendLittleEndian(bytes, value, 1);
	else if (type == FT_CHAR16_T)
		AppendLittleEndian(bytes, value, 2);
	else
		AppendLittleEndian(bytes, value, 4);
	if (user_defined)
		output.emit_user_defined_literal_character(source, view.suffix, type,
			&bytes[0], bytes.size());
	else
		output.emit_literal(source, type, &bytes[0], bytes.size());
}

} // namespace

bool ParsePPCharacterLiteral(const string& spelling,
	const vector<size_t>& ucn_backslash_offsets, PPIntegralLiteral& result)
{
	LiteralView view;
	vector<DecodedAtom> atoms;
	if (!SplitLiteral(spelling, '\'', view) || view.raw ||
		!view.suffix.empty() ||
		!DecodeLiteralContent(spelling, view, ucn_backslash_offsets, atoms) ||
		atoms.size() != 1 || !IsUnicodeScalar(atoms[0].value))
		return false;

	const uint32_t value = atoms[0].value;
	EFundamentalType type;
	if (view.encoding.empty())
		type = value <= 127 ? FT_CHAR : FT_INT;
	else if (view.encoding == "u")
	{
		if (value > 0xFFFF) return false;
		type = FT_CHAR16_T;
	}
	else if (view.encoding == "U")
		type = FT_CHAR32_T;
	else if (view.encoding == "L")
		type = FT_WCHAR_T;
	else
		return false;

	result.value = value;
	result.is_unsigned = IsUnsignedIntegralType(type);
	return true;
}
