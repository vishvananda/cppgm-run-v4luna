// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include "preprocess/control_expression.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <vector>

#include "postprocess/posttoken.h"
#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/tokens/PPTokenizer.h"

namespace
{

enum TokenKind
{
	TOKEN_VALUE,
	TOKEN_TRUE,
	TOKEN_FALSE,
	TOKEN_IDENTIFIER,
	TOKEN_DEFINED,
	TOKEN_OPERATOR,
	TOKEN_INVALID
};

enum Operator
{
	OP_NONE,
	OP_LPAREN,
	OP_RPAREN,
	OP_PLUS,
	OP_MINUS,
	OP_LNOT,
	OP_COMPL,
	OP_STAR,
	OP_DIV,
	OP_MOD,
	OP_LSHIFT,
	OP_RSHIFT,
	OP_LT,
	OP_GT,
	OP_LE,
	OP_GE,
	OP_EQ,
	OP_NE,
	OP_AMP,
	OP_XOR,
	OP_BOR,
	OP_LAND,
	OP_LOR,
	OP_QMARK,
	OP_COLON
};

struct ExpressionToken
{
	TokenKind kind;
	Operator op;
	uint64_t value;
	bool is_unsigned;
	bool mock_defined;
};

struct Value
{
	uint64_t bits;
	bool is_unsigned;
};

Value MakeValue(uint64_t bits, bool is_unsigned)
{
	Value result = {bits, is_unsigned};
	return result;
}

uint64_t SignedBits(int64_t value)
{
	uint64_t result;
	std::memcpy(&result, &value, sizeof(result));
	return result;
}

int64_t AsSigned(uint64_t bits)
{
	int64_t result;
	std::memcpy(&result, &bits, sizeof(result));
	return result;
}

bool IsTrue(Value value)
{
	return value.bits != 0;
}

bool IsUnsignedOperatorKeyword(const std::string& spelling, Operator& op)
{
	if (spelling == "not") op = OP_LNOT;
	else if (spelling == "compl") op = OP_COMPL;
	else if (spelling == "bitand") op = OP_AMP;
	else if (spelling == "xor") op = OP_XOR;
	else if (spelling == "bitor") op = OP_BOR;
	else if (spelling == "and") op = OP_LAND;
	else if (spelling == "or") op = OP_LOR;
	else if (spelling == "not_eq") op = OP_NE;
	else return false;
	return true;
}

bool OperatorForPunctuator(const std::string& spelling, Operator& op)
{
	if (spelling == "(") op = OP_LPAREN;
	else if (spelling == ")") op = OP_RPAREN;
	else if (spelling == "+") op = OP_PLUS;
	else if (spelling == "-") op = OP_MINUS;
	else if (spelling == "!") op = OP_LNOT;
	else if (spelling == "~") op = OP_COMPL;
	else if (spelling == "*") op = OP_STAR;
	else if (spelling == "/") op = OP_DIV;
	else if (spelling == "%") op = OP_MOD;
	else if (spelling == "<<") op = OP_LSHIFT;
	else if (spelling == ">>") op = OP_RSHIFT;
	else if (spelling == "<") op = OP_LT;
	else if (spelling == ">") op = OP_GT;
	else if (spelling == "<=") op = OP_LE;
	else if (spelling == ">=") op = OP_GE;
	else if (spelling == "==") op = OP_EQ;
	else if (spelling == "!=") op = OP_NE;
	else if (spelling == "&") op = OP_AMP;
	else if (spelling == "^") op = OP_XOR;
	else if (spelling == "|") op = OP_BOR;
	else if (spelling == "&&") op = OP_LAND;
	else if (spelling == "||") op = OP_LOR;
	else if (spelling == "?") op = OP_QMARK;
	else if (spelling == ":") op = OP_COLON;
	else return false;
	return true;
}

bool MockIsDefined(const std::string& identifier)
{
	return !identifier.empty() &&
		(static_cast<unsigned char>(identifier[0]) & 1u) != 0;
}

struct ExpressionLine : IPPTokenStream
{
	ExpressionLine(std::ostream& output) : output_(output), tokens_() {}

	void emit_whitespace_sequence() {}

	void emit_new_line()
	{
		finishLine();
	}

	void emit_header_name(const std::string&)
	{
		appendInvalid();
	}

	void emit_identifier(const std::string& spelling)
	{
		Operator op = OP_NONE;
		if (IsUnsignedOperatorKeyword(spelling, op))
		{
			appendOperator(op);
			return;
		}
		if (spelling == "true")
		{
			appendIdentifierValue(TOKEN_TRUE, spelling);
			return;
		}
		if (spelling == "false")
		{
			appendIdentifierValue(TOKEN_FALSE, spelling);
			return;
		}
		appendIdentifierValue(spelling == "defined" ? TOKEN_DEFINED :
			TOKEN_IDENTIFIER, spelling);
	}

	void emit_pp_number(const std::string& spelling)
	{
		PPIntegralLiteral literal;
		if (!ParsePPIntegralLiteral(spelling, literal))
		{
			appendInvalid();
			return;
		}
		appendValue(literal.value, literal.is_unsigned);
	}

	void emit_character_literal(const std::string& spelling)
	{
		addCharacter(spelling, std::vector<size_t>());
	}

	void emit_character_literal(const std::string& spelling,
		const std::vector<size_t>& ucn_backslash_offsets)
	{
		addCharacter(spelling, ucn_backslash_offsets);
	}

	void emit_user_defined_character_literal(const std::string&)
	{
		appendInvalid();
	}

	void emit_user_defined_character_literal(const std::string&,
		const std::vector<size_t>&)
	{
		appendInvalid();
	}

	void emit_string_literal(const std::string&)
	{
		appendInvalid();
	}

	void emit_string_literal(const std::string&,
		const std::vector<size_t>&)
	{
		appendInvalid();
	}

	void emit_user_defined_string_literal(const std::string&)
	{
		appendInvalid();
	}

	void emit_user_defined_string_literal(const std::string&,
		const std::vector<size_t>&)
	{
		appendInvalid();
	}

	void emit_preprocessing_op_or_punc(const std::string& spelling)
	{
		Operator op = OP_NONE;
		if (OperatorForPunctuator(spelling, op) ||
			IsUnsignedOperatorKeyword(spelling, op))
			appendOperator(op);
		else appendInvalid();
	}

	void emit_non_whitespace_char(const std::string&)
	{
		appendInvalid();
	}

	void emit_eof()
	{
		finishLine();
		output_ << "eof\n";
	}

private:
	std::ostream& output_;
	std::vector<ExpressionToken> tokens_;

	void appendValue(uint64_t bits, bool is_unsigned)
	{
		ExpressionToken token = {};
		token.kind = TOKEN_VALUE;
		token.value = bits;
		token.is_unsigned = is_unsigned;
		tokens_.push_back(token);
	}

	void appendIdentifierValue(TokenKind kind, const std::string& spelling)
	{
		ExpressionToken token = {};
		token.kind = kind;
		token.mock_defined = MockIsDefined(spelling);
		tokens_.push_back(token);
	}

	void appendOperator(Operator op)
	{
		ExpressionToken token = {};
		token.kind = TOKEN_OPERATOR;
		token.op = op;
		tokens_.push_back(token);
	}

	void appendInvalid()
	{
		ExpressionToken token = {};
		token.kind = TOKEN_INVALID;
		tokens_.push_back(token);
	}

	void addCharacter(const std::string& spelling,
		const std::vector<size_t>& ucn_backslash_offsets)
	{
		PPIntegralLiteral literal;
		if (!ParsePPCharacterLiteral(spelling, ucn_backslash_offsets, literal))
		{
			appendInvalid();
			return;
		}
		appendValue(literal.value, literal.is_unsigned);
	}

	void finishLine();
};

class ExpressionParser
{
public:
	explicit ExpressionParser(const std::vector<ExpressionToken>& tokens)
		: tokens_(tokens), position_(0), valid_(true) {}

	bool parse(Value& result)
	{
		result = parseConditional(true);
		return valid_ && position_ == tokens_.size();
	}

private:
	const std::vector<ExpressionToken>& tokens_;
	size_t position_;
	bool valid_;

	bool at(Operator op) const
	{
		return position_ < tokens_.size() &&
			tokens_[position_].kind == TOKEN_OPERATOR &&
			tokens_[position_].op == op;
	}

	bool match(Operator op)
	{
		if (!at(op)) return false;
		++position_;
		return true;
	}

	void expect(Operator op)
	{
		if (!match(op)) valid_ = false;
	}

	Value parsePrimary(bool evaluate)
	{
		if (position_ >= tokens_.size())
		{
			valid_ = false;
			return MakeValue(0, false);
		}
		const ExpressionToken token = tokens_[position_];
		if (token.kind == TOKEN_VALUE)
		{
			++position_;
			return MakeValue(evaluate ? token.value : 0, token.is_unsigned);
		}
		if (token.kind == TOKEN_TRUE || token.kind == TOKEN_FALSE)
		{
			++position_;
			return MakeValue(evaluate && token.kind == TOKEN_TRUE ? 1 : 0,
				false);
		}
		if (token.kind == TOKEN_IDENTIFIER)
		{
			++position_;
			return MakeValue(0, false);
		}
		if (token.kind == TOKEN_DEFINED)
		{
			++position_;
			const bool parenthesized = match(OP_LPAREN);
			if (position_ >= tokens_.size() ||
				(tokens_[position_].kind != TOKEN_IDENTIFIER &&
				 tokens_[position_].kind != TOKEN_DEFINED &&
				 tokens_[position_].kind != TOKEN_TRUE &&
				 tokens_[position_].kind != TOKEN_FALSE))
			{
				valid_ = false;
				return MakeValue(0, false);
			}
			const bool result = tokens_[position_++].mock_defined;
			if (parenthesized) expect(OP_RPAREN);
			return MakeValue(evaluate && result ? 1 : 0, false);
		}
		if (match(OP_LPAREN))
		{
			Value result = parseConditional(evaluate);
			expect(OP_RPAREN);
			return result;
		}
		valid_ = false;
		return MakeValue(0, false);
	}

	Value parseUnary(bool evaluate)
	{
		if (position_ < tokens_.size() &&
			tokens_[position_].kind == TOKEN_OPERATOR)
		{
			const Operator op = tokens_[position_].op;
			if (op == OP_PLUS || op == OP_MINUS || op == OP_LNOT ||
				op == OP_COMPL)
			{
				++position_;
				const Value operand = parseUnary(evaluate);
				if (!valid_) return MakeValue(0, false);
				if (!evaluate) return MakeValue(0,
					op == OP_LNOT ? false : operand.is_unsigned);
				if (op == OP_PLUS) return operand;
				if (op == OP_LNOT)
					return MakeValue(IsTrue(operand) ? 0 : 1, false);
				if (op == OP_COMPL)
					return MakeValue(~operand.bits, operand.is_unsigned);
				if (operand.is_unsigned)
					return MakeValue(uint64_t(0) - operand.bits, true);
				const int64_t value = AsSigned(operand.bits);
				if (value == std::numeric_limits<int64_t>::min())
				{
					valid_ = false;
					return MakeValue(0, false);
				}
				return MakeValue(SignedBits(-value), false);
			}
		}
		return parsePrimary(evaluate);
	}

	Value applyBinary(Operator op, Value left, Value right, bool evaluate)
	{
		const bool is_shift = op == OP_LSHIFT || op == OP_RSHIFT;
		const bool is_comparison = op == OP_EQ || op == OP_NE ||
			op == OP_LT || op == OP_GT || op == OP_LE || op == OP_GE;
		const bool result_unsigned = is_shift ? left.is_unsigned :
			(!is_comparison && (left.is_unsigned || right.is_unsigned));
		if (!evaluate) return MakeValue(0, result_unsigned);

		if (is_shift)
		{
			uint64_t count;
			if (right.is_unsigned)
			{
				if (right.bits >= 64)
				{
					valid_ = false;
					return MakeValue(0, result_unsigned);
				}
				count = right.bits;
			}
			else
			{
				const int64_t signed_count = AsSigned(right.bits);
				if (signed_count < 0 || signed_count >= 64)
				{
					valid_ = false;
					return MakeValue(0, result_unsigned);
				}
				count = static_cast<uint64_t>(signed_count);
			}
			if (op == OP_RSHIFT)
			{
				uint64_t shifted = left.bits >> count;
				if (!left.is_unsigned && AsSigned(left.bits) < 0 && count != 0)
					shifted |= (~uint64_t(0)) << (64 - count);
				return MakeValue(shifted, left.is_unsigned);
			}
			if (left.is_unsigned)
				return MakeValue(left.bits << count, true);
			const int64_t signed_left = AsSigned(left.bits);
		if (signed_left < 0 || left.bits >
			(std::numeric_limits<uint64_t>::max() >> count))
			{
				valid_ = false;
				return MakeValue(0, false);
			}
			return MakeValue(left.bits << count, false);
		}

		const bool common_unsigned = left.is_unsigned || right.is_unsigned;
		const uint64_t a_bits = left.bits;
		const uint64_t b_bits = right.bits;
		if (is_comparison)
		{
			bool result = false;
			if (op == OP_EQ) result = a_bits == b_bits;
			else if (op == OP_NE) result = a_bits != b_bits;
			else if (common_unsigned)
			{
				if (op == OP_LT) result = a_bits < b_bits;
				else if (op == OP_GT) result = a_bits > b_bits;
				else if (op == OP_LE) result = a_bits <= b_bits;
				else result = a_bits >= b_bits;
			}
			else
			{
				const int64_t a = AsSigned(a_bits);
				const int64_t b = AsSigned(b_bits);
				if (op == OP_LT) result = a < b;
				else if (op == OP_GT) result = a > b;
				else if (op == OP_LE) result = a <= b;
				else result = a >= b;
			}
			return MakeValue(result ? 1 : 0, false);
		}

		if (op == OP_AMP || op == OP_XOR || op == OP_BOR)
		{
			const uint64_t result = op == OP_AMP ? (a_bits & b_bits) :
				op == OP_XOR ? (a_bits ^ b_bits) : (a_bits | b_bits);
			return MakeValue(result, common_unsigned);
		}

		if (common_unsigned)
		{
			uint64_t result = 0;
			if (op == OP_STAR) result = a_bits * b_bits;
			else if (op == OP_DIV || op == OP_MOD)
			{
				if (b_bits == 0)
				{
					valid_ = false;
					return MakeValue(0, true);
				}
				result = op == OP_DIV ? a_bits / b_bits : a_bits % b_bits;
			}
			else if (op == OP_PLUS) result = a_bits + b_bits;
			else if (op == OP_MINUS) result = a_bits - b_bits;
			return MakeValue(result, true);
		}

		const int64_t a = AsSigned(a_bits);
		const int64_t b = AsSigned(b_bits);
		int64_t result = 0;
		if (op == OP_PLUS)
		{
			if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
				(b < 0 && a < std::numeric_limits<int64_t>::min() - b))
			{
				valid_ = false;
				return MakeValue(0, false);
			}
			result = a + b;
		}
		else if (op == OP_MINUS)
		{
			if ((b < 0 && a > std::numeric_limits<int64_t>::max() + b) ||
				(b > 0 && a < std::numeric_limits<int64_t>::min() + b))
			{
				valid_ = false;
				return MakeValue(0, false);
			}
			result = a - b;
		}
		else if (op == OP_STAR)
		{
			if (a != 0 && b != 0)
			{
				const bool negative = (a < 0) != (b < 0);
				const uint64_t abs_a = a < 0 ? uint64_t(0) - a_bits : a_bits;
				const uint64_t abs_b = b < 0 ? uint64_t(0) - b_bits : b_bits;
				const uint64_t limit = negative
					? (uint64_t(1) << 63)
					: static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
				if (abs_a > limit / abs_b)
				{
					valid_ = false;
					return MakeValue(0, false);
				}
				const uint64_t magnitude = abs_a * abs_b;
				if (negative)
					return MakeValue(uint64_t(0) - magnitude, false);
				return MakeValue(magnitude, false);
			}
			return MakeValue(0, false);
		}
		else if (op == OP_DIV || op == OP_MOD)
		{
			if (b == 0 || (a == std::numeric_limits<int64_t>::min() && b == -1))
			{
				valid_ = false;
				return MakeValue(0, false);
			}
			result = op == OP_DIV ? a / b : a % b;
		}
		return MakeValue(SignedBits(result), false);
	}

#define PARSE_BINARY_LEVEL(name, lower, op1, op2, op3, op4) \
	Value name(bool evaluate) \
	{ \
		Value left = lower(evaluate); \
		while (valid_ && position_ < tokens_.size() && \
			tokens_[position_].kind == TOKEN_OPERATOR) \
		{ \
			const Operator op = tokens_[position_].op; \
			if (op != op1 && op != op2 && op != op3 && op != op4) break; \
			++position_; \
			const Value right = lower(evaluate); \
			left = applyBinary(op, left, right, evaluate); \
		} \
		return left; \
	}

	PARSE_BINARY_LEVEL(parseMultiplicative, parseUnary,
		OP_STAR, OP_DIV, OP_MOD, OP_NONE)
	PARSE_BINARY_LEVEL(parseAdditive, parseMultiplicative,
		OP_PLUS, OP_MINUS, OP_NONE, OP_NONE)
	PARSE_BINARY_LEVEL(parseShift, parseAdditive,
		OP_LSHIFT, OP_RSHIFT, OP_NONE, OP_NONE)
	PARSE_BINARY_LEVEL(parseRelational, parseShift,
		OP_LT, OP_GT, OP_LE, OP_GE)
	PARSE_BINARY_LEVEL(parseEquality, parseRelational,
		OP_EQ, OP_NE, OP_NONE, OP_NONE)
	PARSE_BINARY_LEVEL(parseAnd, parseEquality,
		OP_AMP, OP_NONE, OP_NONE, OP_NONE)
	PARSE_BINARY_LEVEL(parseExclusiveOr, parseAnd,
		OP_XOR, OP_NONE, OP_NONE, OP_NONE)
	PARSE_BINARY_LEVEL(parseInclusiveOr, parseExclusiveOr,
		OP_BOR, OP_NONE, OP_NONE, OP_NONE)

#undef PARSE_BINARY_LEVEL

	Value parseLogicalAnd(bool evaluate)
	{
		Value left = parseInclusiveOr(evaluate);
		while (valid_ && match(OP_LAND))
		{
			const bool evaluate_right = evaluate && IsTrue(left);
			const Value right = parseInclusiveOr(evaluate_right);
			left = MakeValue(evaluate && IsTrue(left) && IsTrue(right) ? 1 : 0,
				false);
		}
		return left;
	}

	Value parseLogicalOr(bool evaluate)
	{
		Value left = parseLogicalAnd(evaluate);
		while (valid_ && match(OP_LOR))
		{
			const bool evaluate_right = evaluate && !IsTrue(left);
			const Value right = parseLogicalAnd(evaluate_right);
			left = MakeValue(evaluate && (IsTrue(left) || IsTrue(right)) ? 1 : 0,
				false);
		}
		return left;
	}

	Value parseConditional(bool evaluate)
	{
		Value condition = parseLogicalOr(evaluate);
		if (!valid_ || !match(OP_QMARK)) return condition;
		const bool choose_true = evaluate && IsTrue(condition);
		const Value when_true = parseConditional(choose_true);
		expect(OP_COLON);
		const Value when_false = parseConditional(evaluate && !IsTrue(condition));
		const bool result_unsigned = when_true.is_unsigned ||
			when_false.is_unsigned;
		Value result = evaluate && IsTrue(condition) ? when_true : when_false;
		result.is_unsigned = result_unsigned;
		return result;
	}
};

void ExpressionLine::finishLine()
{
	if (tokens_.empty()) return;
	ExpressionParser parser(tokens_);
	Value result = MakeValue(0, false);
	if (!parser.parse(result))
		output_ << "error\n";
	else if (result.is_unsigned)
		output_ << result.bits << "u\n";
	else
		output_ << AsSigned(result.bits) << "\n";
	tokens_.clear();
}

} // namespace

void EvaluateControlExpressions(const std::string& source, std::ostream& output)
{
	ExpressionLine line(output);
	TokenizePreprocessingSource(source, line);
}

void EvaluateControlExpressionTokens(
	const std::vector<PreprocessingToken>& tokens, std::ostream& output)
{
	ExpressionLine line(output);
	for (std::size_t i = 0; i < tokens.size(); ++i)
	{
		const PreprocessingToken& token = tokens[i];
		switch (token.kind)
		{
		case PP_TOKEN_WHITESPACE:
			line.emit_whitespace_sequence();
			break;
		case PP_TOKEN_NEWLINE:
			line.emit_new_line();
			break;
		case PP_TOKEN_HEADER_NAME:
			line.emit_header_name(token.spelling);
			break;
		case PP_TOKEN_IDENTIFIER:
			if (token.identifier_spelling)
				line.emit_identifier(*token.identifier_spelling);
			else
				line.emit_non_whitespace_char(token.spelling);
			break;
		case PP_TOKEN_NUMBER:
			line.emit_pp_number(token.spelling);
			break;
		case PP_TOKEN_CHARACTER:
			line.emit_character_literal(token.spelling,
				token.ucn_backslash_offsets);
			break;
		case PP_TOKEN_USER_CHARACTER:
			line.emit_user_defined_character_literal(token.spelling,
				token.ucn_backslash_offsets);
			break;
		case PP_TOKEN_STRING:
			line.emit_string_literal(token.spelling,
				token.ucn_backslash_offsets);
			break;
		case PP_TOKEN_USER_STRING:
			line.emit_user_defined_string_literal(token.spelling,
				token.ucn_backslash_offsets);
			break;
		case PP_TOKEN_PUNCTUATOR:
			line.emit_preprocessing_op_or_punc(token.spelling);
			break;
		case PP_TOKEN_OTHER:
			line.emit_non_whitespace_char(token.spelling);
			break;
		case PP_TOKEN_EOF:
			break;
		}
	}
	line.emit_new_line();
}
