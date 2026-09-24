#include "preprocess/tokens/PPTokenizer.h"

#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

const int EndOfFile = -1;

struct CodePoint
{
	int value;
	size_t byte_begin;
	size_t byte_end;
	size_t line;
	size_t column;
	size_t end_line;
	size_t end_column;
	bool synthetic;

	CodePoint()
		: value(EndOfFile), byte_begin(0), byte_end(0), line(1), column(1),
		  end_line(1), end_column(1), synthetic(false)
	{}
};

struct Range
{
	uint32_t first;
	uint32_t last;
};

const Range AnnexE1[] =
{
	{0x00A8,0x00A8}, {0x00AA,0x00AA}, {0x00AD,0x00AD}, {0x00AF,0x00AF},
	{0x00B2,0x00B5}, {0x00B7,0x00BA}, {0x00BC,0x00BE}, {0x00C0,0x00D6},
	{0x00D8,0x00F6}, {0x00F8,0x00FF}, {0x0100,0x167F}, {0x1681,0x180D},
	{0x180F,0x1FFF}, {0x200B,0x200D}, {0x202A,0x202E}, {0x203F,0x2040},
	{0x2054,0x2054}, {0x2060,0x206F}, {0x2070,0x218F}, {0x2460,0x24FF},
	{0x2776,0x2793}, {0x2C00,0x2DFF}, {0x2E80,0x2FFF}, {0x3004,0x3007},
	{0x3021,0x302F}, {0x3031,0x303F}, {0x3040,0xD7FF}, {0xF900,0xFD3D},
	{0xFD40,0xFDCF}, {0xFDF0,0xFE44}, {0xFE47,0xFFFD},
	{0x10000,0x1FFFD}, {0x20000,0x2FFFD}, {0x30000,0x3FFFD},
	{0x40000,0x4FFFD}, {0x50000,0x5FFFD}, {0x60000,0x6FFFD},
	{0x70000,0x7FFFD}, {0x80000,0x8FFFD}, {0x90000,0x9FFFD},
	{0xA0000,0xAFFFD}, {0xB0000,0xBFFFD}, {0xC0000,0xCFFFD},
	{0xD0000,0xDFFFD}, {0xE0000,0xEFFFD}
};

const Range AnnexE2[] =
{
	{0x0300,0x036F}, {0x1DC0,0x1DFF}, {0x20D0,0x20FF}, {0xFE20,0xFE2F}
};

bool InRanges(uint32_t value, const Range* ranges, size_t count)
{
	size_t first = 0;
	size_t last = count;
	while (first < last)
	{
		size_t middle = first + (last - first) / 2;
		if (value < ranges[middle].first)
			last = middle;
		else if (value > ranges[middle].last)
			first = middle + 1;
		else
			return true;
	}
	return false;
}

bool IsHexDigit(int value)
{
	return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
		(value >= 'A' && value <= 'F');
}

uint32_t HexValue(int value)
{
	if (value >= '0' && value <= '9') return static_cast<uint32_t>(value - '0');
	if (value >= 'a' && value <= 'f') return static_cast<uint32_t>(value - 'a' + 10);
	return static_cast<uint32_t>(value - 'A' + 10);
}

uint32_t DecodeUtf8(const std::string& text, size_t offset, size_t& width)
{
	if (offset >= text.size())
		throw std::runtime_error("unexpected end of UTF-8 input");
	const unsigned char first = static_cast<unsigned char>(text[offset]);
	if (first < 0x80)
	{
		width = 1;
		return first;
	}

	uint32_t value;
	uint32_t minimum;
	if (first >= 0xC2 && first <= 0xDF)
	{
		value = first & 0x1F;
		minimum = 0x80;
		width = 2;
	}
	else if (first >= 0xE0 && first <= 0xEF)
	{
		value = first & 0x0F;
		minimum = 0x800;
		width = 3;
	}
	else if (first >= 0xF0 && first <= 0xF4)
	{
		value = first & 0x07;
		minimum = 0x10000;
		width = 4;
	}
	else
		throw std::runtime_error("invalid UTF-8 leading byte");

	if (width > text.size() - offset)
		throw std::runtime_error("incomplete UTF-8 sequence");
	for (size_t i = 1; i < width; ++i)
	{
		const unsigned char next = static_cast<unsigned char>(text[offset + i]);
		if ((next & 0xC0) != 0x80)
			throw std::runtime_error("invalid UTF-8 continuation byte");
		value = (value << 6) | (next & 0x3F);
	}
	if (value < minimum || value > 0x10FFFF ||
		(value >= 0xD800 && value <= 0xDFFF))
		throw std::runtime_error("invalid UTF-8 code point");
	return value;
}

void AppendUtf8(std::string& output, uint32_t value)
{
	if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
		throw std::runtime_error("invalid universal character value");
	if (value <= 0x7F)
		output.push_back(static_cast<char>(value));
	else if (value <= 0x7FF)
	{
		output.push_back(static_cast<char>(0xC0 | (value >> 6)));
		output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
	}
	else if (value <= 0xFFFF)
	{
		output.push_back(static_cast<char>(0xE0 | (value >> 12)));
		output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
	}
	else
	{
		output.push_back(static_cast<char>(0xF0 | (value >> 18)));
		output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
	}
}

bool IsBasicSourceCharacter(uint32_t value)
{
	if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
		(value >= '0' && value <= '9'))
		return true;
	if (value == ' ' || value == '\t' || value == '\v' || value == '\f' ||
		value == '\n')
		return true;
	static const std::string punctuation = "_{}[]#()<>%:;.?*+-/^&|~=!,\\\"'";
	return value <= 0x7F && punctuation.find(static_cast<char>(value)) !=
		std::string::npos;
}

bool IsControl(uint32_t value)
{
	return value <= 0x1F || (value >= 0x7F && value <= 0x9F);
}

bool IsAsciiNondigit(int value)
{
	return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
		value == '_';
}

bool IsDigit(int value)
{
	return value >= '0' && value <= '9';
}

bool IsIdentifierStart(uint32_t value)
{
	if (IsAsciiNondigit(static_cast<int>(value))) return true;
	return InRanges(value, AnnexE1, sizeof(AnnexE1) / sizeof(AnnexE1[0])) &&
		!InRanges(value, AnnexE2, sizeof(AnnexE2) / sizeof(AnnexE2[0]));
}

bool IsIdentifierBody(uint32_t value)
{
	return IsIdentifierStart(value) || IsDigit(static_cast<int>(value)) ||
		InRanges(value, AnnexE1, sizeof(AnnexE1) / sizeof(AnnexE1[0]));
}

bool IsWhitespace(int value)
{
	return value == ' ' || value == '\t' || value == '\v' || value == '\f';
}

bool IsOctalDigit(int value)
{
	return value >= '0' && value <= '7';
}

bool IsSimpleEscape(int value)
{
	static const std::string escapes = "'\"?\\abfnrtv";
	return value >= 0 && escapes.find(static_cast<char>(value)) !=
		std::string::npos;
}

int TrigraphReplacement(int value)
{
	switch (value)
	{
	case '=': return '#';
	case '/': return '\\';
	case '\'': return '^';
	case '(': return '[';
	case ')': return ']';
	case '!': return '|';
	case '<': return '{';
	case '>': return '}';
	case '-': return '~';
	default: return EndOfFile;
	}
}

bool NeedsFinalNewline(const std::string& source)
{
	if (source.empty()) return false;
	if (source[source.size() - 1] != '\n') return true;
	if (source.size() >= 2 && source[source.size() - 2] == '\\') return true;
	return source.size() >= 4 &&
		source.compare(source.size() - 4, 4, "?" "?/\n") == 0;
}

class SourceCursor
{
public:
	explicit SourceCursor(const std::string& source)
		: source_(source), raw_byte_(HasUtf8Bom(source) ? 3 : 0), raw_line_(1),
		  raw_column_(1), raw_done_(false), phase1_done_(false), logical_done_(false),
		  synthetic_needed_(NeedsFinalNewline(source)), synthetic_emitted_(false)
	{}

	const CodePoint& peek(size_t offset = 0)
	{
		while (logical_.size() <= offset && !logical_done_)
		{
			CodePoint point;
			if (!readLogical(point))
				logical_done_ = true;
			else
				logical_.push_back(point);
		}
		return offset < logical_.size() ? logical_[offset] : EofPoint();
	}

	CodePoint consume()
	{
		const CodePoint point = peek();
		if (point.value == EndOfFile)
			return point;
		logical_.pop_front();
		return point;
	}

	void resetAfterRaw(size_t byte, size_t line, size_t column)
	{
		raw_byte_ = byte;
		raw_line_ = line;
		raw_column_ = column;
		raw_.clear();
		phase1_.clear();
		logical_.clear();
		raw_done_ = false;
		phase1_done_ = false;
		logical_done_ = false;
		synthetic_emitted_ = false;
	}

	size_t eofLine() const { return synthetic_emitted_ ? raw_line_ + 1 : raw_line_; }
	size_t eofColumn() const { return synthetic_emitted_ ? 1 : raw_column_; }

private:
	const std::string& source_;
	size_t raw_byte_;
	size_t raw_line_;
	size_t raw_column_;
	bool raw_done_;
	bool phase1_done_;
	bool logical_done_;
	bool synthetic_needed_;
	bool synthetic_emitted_;
	std::deque<CodePoint> raw_;
	std::deque<CodePoint> phase1_;
	std::deque<CodePoint> logical_;

	static bool HasUtf8Bom(const std::string& source)
	{
		return source.size() >= 3 &&
			static_cast<unsigned char>(source[0]) == 0xEF &&
			static_cast<unsigned char>(source[1]) == 0xBB &&
			static_cast<unsigned char>(source[2]) == 0xBF;
	}

	static const CodePoint& EofPoint()
	{
		static const CodePoint point;
		return point;
	}

	bool readRaw(CodePoint& point)
	{
		if (raw_byte_ >= source_.size())
		{
			raw_done_ = true;
			return false;
		}
		size_t width = 0;
		const uint32_t value = DecodeUtf8(source_, raw_byte_, width);
		point.value = static_cast<int>(value);
		point.byte_begin = raw_byte_;
		point.byte_end = raw_byte_ + width;
		point.line = raw_line_;
		point.column = raw_column_;
		point.synthetic = false;
		raw_byte_ += width;
		if (value == '\n')
		{
			++raw_line_;
			raw_column_ = 1;
		}
		else
			++raw_column_;
		point.end_line = raw_line_;
		point.end_column = raw_column_;
		return true;
	}

	bool fillRaw(size_t offset)
	{
		while (raw_.size() <= offset && !raw_done_)
		{
			CodePoint point;
			if (readRaw(point)) raw_.push_back(point);
		}
		return offset < raw_.size();
	}

	const CodePoint& peekRaw(size_t offset)
	{
		return fillRaw(offset) ? raw_[offset] : EofPoint();
	}

	CodePoint popRaw()
	{
		CodePoint point = raw_.front();
		raw_.pop_front();
		return point;
	}

	bool readPhase1(CodePoint& point)
	{
		if (!fillRaw(0))
		{
			if (!synthetic_needed_ || synthetic_emitted_) return false;
			point.value = '\n';
			point.byte_begin = source_.size();
			point.byte_end = source_.size();
			point.line = raw_line_;
			point.column = raw_column_;
			point.end_line = raw_line_ + 1;
			point.end_column = 1;
			point.synthetic = true;
			synthetic_emitted_ = true;
			return true;
		}
		const CodePoint first = peekRaw(0);
		if (first.value == '?' && peekRaw(1).value == '?' &&
			peekRaw(2).value != EndOfFile)
		{
			const int replacement = TrigraphReplacement(peekRaw(2).value);
			if (replacement != EndOfFile)
			{
				popRaw();
				popRaw();
				const CodePoint third = popRaw();
				point = first;
				point.value = replacement;
				point.byte_end = third.byte_end;
				point.end_line = third.end_line;
				point.end_column = third.end_column;
				return true;
			}
		}
		point = popRaw();
		return true;
	}

	bool fillPhase1(size_t offset)
	{
		while (phase1_.size() <= offset && !phase1_done_)
		{
			CodePoint point;
			if (readPhase1(point)) phase1_.push_back(point);
			else phase1_done_ = true;
		}
		return offset < phase1_.size();
	}

	CodePoint popPhase1()
	{
		CodePoint point = phase1_.front();
		phase1_.pop_front();
		return point;
	}

	bool readLogical(CodePoint& point)
	{
		for (;;)
		{
			if (!fillPhase1(0)) return false;

			const CodePoint first = phase1_.front();
			if (first.value == '\\' && fillPhase1(1) &&
				phase1_[1].value == '\n')
			{
				popPhase1();
				popPhase1();
				continue;
			}
			point = popPhase1();
			return true;
		}
	}
};

struct LexChar
{
	uint32_t value;
	size_t source_units;
};

enum TokenKind
{
	HeaderName,
	Identifier,
	PPNumber,
	CharacterLiteral,
	UserDefinedCharacterLiteral,
	StringLiteral,
	UserDefinedStringLiteral,
	Punctuator,
	NonWhitespace
};

enum DirectiveState
{
	AtDirectiveStart,
	AfterDirectiveMarker,
	ExpectHeaderName,
	NotDirective
};

struct LiteralPrefix
{
	bool valid;
	bool raw;
	bool character;
	std::string spelling;
};

class PPTokenizer
{
public:
	PPTokenizer(const std::string& source, IPPTokenStream& output)
		: source_(source), cursor_(source), output_(output),
		  directive_state_(AtDirectiveStart)
	{}

	void run()
	{
		for (;;)
		{
			const CodePoint point = cursor_.peek();
			if (point.value == EndOfFile)
				break;
			if (point.value == '\n')
			{
				emitNewLine(cursor_.consume());
				continue;
			}
			if (IsWhitespace(point.value) || startsComment())
			{
				scanWhitespace();
				continue;
			}
			if (directive_state_ == ExpectHeaderName &&
				(point.value == '<' || point.value == '"'))
			{
				scanHeaderName();
				continue;
			}

			const LiteralPrefix prefix = findLiteralPrefix();
			if (prefix.valid)
			{
				if (prefix.raw) scanRawLiteral(prefix);
				else scanQuotedLiteral(prefix);
				continue;
			}

			const LexChar first = peekLex(0, false);
			if (IsIdentifierStart(first.value))
				scanIdentifier();
			else if (IsDigit(static_cast<int>(first.value)) ||
				(first.value == '.' && IsDigit(cursor_.peek(1).value)))
				scanPPNumber();
			else if (point.value == '\n')
				emitNewLine(cursor_.consume());
			else
				scanPunctuatorOrOther();
		}

		output_.set_source_location(cursor_.eofLine(), cursor_.eofColumn());
		output_.emit_eof();
	}

private:
	const std::string& source_;
	SourceCursor cursor_;
	IPPTokenStream& output_;
	DirectiveState directive_state_;

	static uint32_t DecodeRawAt(const std::string& source, size_t byte,
		size_t& width)
	{
		return DecodeUtf8(source, byte, width);
	}

	bool startsComment()
	{
		return cursor_.peek().value == '/' &&
			(cursor_.peek(1).value == '/' || cursor_.peek(1).value == '*');
	}

	void setLocation(const CodePoint& point)
	{
		output_.set_source_location(point.line, point.column);
	}

	void emitNewLine(const CodePoint& point)
	{
		setLocation(point);
		output_.emit_new_line();
		directive_state_ = AtDirectiveStart;
	}

	void emitWhitespace(const CodePoint& point)
	{
		setLocation(point);
		output_.emit_whitespace_sequence();
	}

	void emitToken(TokenKind kind, const std::string& data,
		const CodePoint& point)
	{
		setLocation(point);
		switch (kind)
		{
		case HeaderName: output_.emit_header_name(data); break;
		case Identifier: output_.emit_identifier(data); break;
		case PPNumber: output_.emit_pp_number(data); break;
		case CharacterLiteral: output_.emit_character_literal(data); break;
		case UserDefinedCharacterLiteral:
			output_.emit_user_defined_character_literal(data); break;
		case StringLiteral: output_.emit_string_literal(data); break;
		case UserDefinedStringLiteral:
			output_.emit_user_defined_string_literal(data); break;
		case Punctuator: output_.emit_preprocessing_op_or_punc(data); break;
		case NonWhitespace: output_.emit_non_whitespace_char(data); break;
		}
		observeToken(kind, data);
	}

	void observeToken(TokenKind kind, const std::string& data)
	{
		if (directive_state_ == AtDirectiveStart)
		{
			if (kind == Punctuator && (data == "#" || data == "%:"))
				directive_state_ = AfterDirectiveMarker;
			else
				directive_state_ = NotDirective;
		}
		else if (directive_state_ == AfterDirectiveMarker)
		{
			if (kind == Identifier && data == "include")
				directive_state_ = ExpectHeaderName;
			else
				directive_state_ = NotDirective;
		}
		else if (directive_state_ == ExpectHeaderName)
			directive_state_ = NotDirective;
	}

	void scanWhitespace()
	{
		bool has_whitespace = false;
		CodePoint whitespace_start;
		for (;;)
		{
			const CodePoint point = cursor_.peek();
			if (IsWhitespace(point.value))
			{
				if (!has_whitespace) whitespace_start = point;
				has_whitespace = true;
				cursor_.consume();
				continue;
			}
			if (!startsComment()) break;
			if (!has_whitespace) whitespace_start = point;
			has_whitespace = true;
			const bool line_comment = cursor_.peek(1).value == '/';
			cursor_.consume();
			cursor_.consume();
			if (line_comment)
			{
				while (cursor_.peek().value != EndOfFile &&
					cursor_.peek().value != '\n')
					cursor_.consume();
				break;
			}
			scanBlockComment(has_whitespace, whitespace_start);
		}
		if (has_whitespace) emitWhitespace(whitespace_start);
	}

	void scanBlockComment(bool& has_whitespace, CodePoint& whitespace_start)
	{
		for (;;)
		{
			const CodePoint point = cursor_.peek();
			if (point.value == EndOfFile)
				throw std::runtime_error("unterminated block comment");
			if (point.value == '*' && cursor_.peek(1).value == '/')
			{
				cursor_.consume();
				cursor_.consume();
				return;
			}
			if (point.value == '\n')
			{
				if (has_whitespace) emitWhitespace(whitespace_start);
				const CodePoint newline = cursor_.consume();
				setLocation(newline);
				has_whitespace = output_.emit_comment_new_line();
				directive_state_ = AtDirectiveStart;
				whitespace_start = cursor_.peek();
				continue;
			}
			cursor_.consume();
		}
	}

	bool matchAscii(size_t offset, const std::string& spelling)
	{
		for (size_t i = 0; i < spelling.size(); ++i)
			if (cursor_.peek(offset + i).value !=
				static_cast<unsigned char>(spelling[i]))
				return false;
		return true;
	}

	LiteralPrefix makePrefix(const std::string& spelling, bool raw,
		bool character)
	{
		LiteralPrefix result;
		result.valid = true;
		result.raw = raw;
		result.character = character;
		result.spelling = spelling;
		return result;
	}

	LiteralPrefix findLiteralPrefix()
	{
		static const char* raw_prefixes[] = {"u8R\"", "uR\"", "UR\"", "LR\"", "R\""};
		for (size_t i = 0; i < sizeof(raw_prefixes) / sizeof(raw_prefixes[0]); ++i)
			if (matchAscii(0, raw_prefixes[i]))
				return makePrefix(raw_prefixes[i], true, false);

		static const char* string_prefixes[] = {"u8\"", "u\"", "U\"", "L\"", "\""};
		for (size_t i = 0; i < sizeof(string_prefixes) / sizeof(string_prefixes[0]); ++i)
			if (matchAscii(0, string_prefixes[i]))
				return makePrefix(string_prefixes[i], false, false);

		static const char* character_prefixes[] = {"u'", "U'", "L'", "'"};
		for (size_t i = 0; i < sizeof(character_prefixes) /
			sizeof(character_prefixes[0]); ++i)
			if (matchAscii(0, character_prefixes[i]))
				return makePrefix(character_prefixes[i], false, true);

		LiteralPrefix none;
		none.valid = false;
		none.raw = false;
		none.character = false;
		return none;
	}

	void consumePrefix(const LiteralPrefix& prefix, std::string& data,
		CodePoint& start, CodePoint& last)
	{
		start = cursor_.peek();
		for (size_t i = 0; i < prefix.spelling.size(); ++i)
		{
			last = cursor_.consume();
			AppendUtf8(data, static_cast<uint32_t>(last.value));
		}
	}

	void scanRawLiteral(const LiteralPrefix& prefix)
	{
		std::string data;
		CodePoint start;
		CodePoint opening_quote;
		consumePrefix(prefix, data, start, opening_quote);
		const size_t raw_start = opening_quote.byte_end;
		size_t position = raw_start;
		size_t line = opening_quote.end_line;
		size_t column = opening_quote.end_column;
		std::string delimiter;
		bool opened = false;
		size_t delimiter_length = 0;
		while (position < source_.size())
		{
			size_t width = 0;
			const uint32_t value = DecodeRawAt(source_, position, width);
			if (value == '(')
			{
				position += width;
				advanceLocation(value, line, column);
				opened = true;
				break;
			}
			if (value == ' ' || value == '\t' || value == '\v' || value == '\f' ||
				value == '\n' || value == '(' || value == ')' || value == '\\')
				throw std::runtime_error("invalid raw string delimiter");
			if (delimiter_length == 16)
				throw std::runtime_error("raw string delimiter is too long");
			delimiter.append(source_, position, width);
			++delimiter_length;
			position += width;
			advanceLocation(value, line, column);
		}
		if (!opened)
			throw std::runtime_error("unterminated raw string literal");

		const size_t body_start = position;
		const std::string terminator = ")" + delimiter + "\"";
		const size_t terminator_start = source_.find(terminator, body_start);
		if (terminator_start == std::string::npos)
			throw std::runtime_error("unterminated raw string literal");
		const size_t raw_end = terminator_start + terminator.size();
		while (position < raw_end)
		{
			size_t width = 0;
			const uint32_t value = DecodeRawAt(source_, position, width);
			position += width;
			advanceLocation(value, line, column);
		}
		data.append(source_, raw_start, raw_end - raw_start);
		cursor_.resetAfterRaw(raw_end, line, column);
		TokenKind kind = StringLiteral;
		appendUserDefinedSuffix(data, kind);
		emitToken(kind, data, start);
	}

	static void advanceLocation(uint32_t value, size_t& line, size_t& column)
	{
		if (value == '\n')
		{
			++line;
			column = 1;
		}
		else
			++column;
	}

	void scanQuotedLiteral(const LiteralPrefix& prefix)
	{
		std::string data;
		std::vector<size_t> ucn_backslash_offsets;
		CodePoint start;
		CodePoint last;
		consumePrefix(prefix, data, start, last);
		const int quote = prefix.character ? '\'' : '"';
		for (;;)
		{
			const CodePoint point = cursor_.peek();
			if (point.value == EndOfFile || point.value == '\n')
				throw std::runtime_error("unterminated quoted literal");
			if (point.value == quote)
			{
				AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
				break;
			}
			if (point.value == '\\')
			{
				const LexChar universal = decodeLexCharAt(0, true);
				if (universal.source_units > 1)
				{
					consumeUnits(universal.source_units);
					if (universal.value == '\\')
						ucn_backslash_offsets.push_back(data.size());
					AppendUtf8(data, universal.value);
					continue;
			}
			scanEscapeSequence(data);
			continue;
		}
		AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
		}
		// Preserve an empty character preprocessing token so PA2 can report its
		// failed conversion as one invalid token and continue the stream.
		TokenKind kind = prefix.character ? CharacterLiteral : StringLiteral;
		appendUserDefinedSuffix(data, kind);
		emitQuotedToken(kind, data, ucn_backslash_offsets, start);
	}

	void emitQuotedToken(TokenKind kind, const std::string& data,
		const std::vector<size_t>& ucn_backslash_offsets,
		const CodePoint& point)
	{
		setLocation(point);
		switch (kind)
		{
		case CharacterLiteral:
			output_.emit_character_literal(data, ucn_backslash_offsets);
			break;
		case UserDefinedCharacterLiteral:
			output_.emit_user_defined_character_literal(data,
				ucn_backslash_offsets);
			break;
		case StringLiteral:
			output_.emit_string_literal(data, ucn_backslash_offsets);
			break;
		case UserDefinedStringLiteral:
			output_.emit_user_defined_string_literal(data,
				ucn_backslash_offsets);
			break;
		default:
			throw std::logic_error("non-literal passed to emitQuotedToken");
		}
		observeToken(kind, data);
	}

	void scanEscapeSequence(std::string& data)
	{
		AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
		const int next = cursor_.peek().value;
		if (next == EndOfFile || next == '\n')
			throw std::runtime_error("unterminated quoted literal");
		if (IsSimpleEscape(next))
		{
			AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
			return;
		}
		if (IsOctalDigit(next))
		{
			AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
			for (size_t count = 1; count < 3 && IsOctalDigit(cursor_.peek().value); ++count)
				AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
			return;
		}
		if (next == 'x')
		{
			AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
			if (!IsHexDigit(cursor_.peek().value))
				throw std::runtime_error("hex escape has no digits");
			while (IsHexDigit(cursor_.peek().value))
				AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
			return;
		}
		throw std::runtime_error("invalid escape sequence");
	}

	void appendUserDefinedSuffix(std::string& data, TokenKind& kind)
	{
		const LexChar first = peekLex(0, false);
		if (!IsIdentifierStart(first.value)) return;
		while (IsIdentifierBody(peekLex(0, false).value))
		{
			const LexChar next = takeLex(false);
			AppendUtf8(data, next.value);
		}
		kind = kind == CharacterLiteral || kind == UserDefinedCharacterLiteral
			? UserDefinedCharacterLiteral : UserDefinedStringLiteral;
	}

	LexChar decodeLexCharAt(size_t offset, bool in_literal)
	{
		const int current = cursor_.peek(offset).value;
		if (current != '\\')
		{
			LexChar direct = {static_cast<uint32_t>(current), 1};
			return direct;
		}
		const int prefix = cursor_.peek(offset + 1).value;
		if (prefix != 'u' && prefix != 'U')
		{
			LexChar direct = {'\\', 1};
			return direct;
		}
		const size_t digits = prefix == 'u' ? 4 : 8;
		uint32_t value = 0;
		for (size_t i = 0; i < digits; ++i)
		{
			const int digit = cursor_.peek(offset + 2 + i).value;
			if (!IsHexDigit(digit))
			{
				LexChar direct = {'\\', 1};
				return direct;
			}
			value = (value << 4) | HexValue(digit);
		}
		if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
			throw std::runtime_error("invalid universal character value");
		if (!in_literal && (IsControl(value) || IsBasicSourceCharacter(value)))
			throw std::runtime_error("invalid universal character value");
		LexChar decoded = {value, digits + 2};
		return decoded;
	}

	LexChar peekLex(size_t offset, bool in_literal)
	{
		size_t source_offset = 0;
		for (size_t i = 0; i < offset; ++i)
		{
			const LexChar skipped = decodeLexCharAt(source_offset, in_literal);
			source_offset += skipped.source_units;
		}
		return decodeLexCharAt(source_offset, in_literal);
	}

	LexChar takeLex(bool in_literal)
	{
		const LexChar result = decodeLexCharAt(0, in_literal);
		consumeUnits(result.source_units);
		return result;
	}

	void consumeUnits(size_t count)
	{
		for (size_t i = 0; i < count; ++i)
			cursor_.consume();
	}

	void scanIdentifier()
	{
		const CodePoint start = cursor_.peek();
		std::string data;
		while (IsIdentifierBody(peekLex(0, false).value))
		{
			const LexChar next = takeLex(false);
			AppendUtf8(data, next.value);
		}
		if (IsIdentifierLikeOperator(data))
			emitToken(Punctuator, data, start);
		else
			emitToken(Identifier, data, start);
	}

	static bool IsIdentifierLikeOperator(const std::string& data)
	{
		static const char* words[] =
		{
			"new", "delete", "and", "and_eq", "bitand", "bitor", "compl",
			"not", "not_eq", "or", "or_eq", "xor", "xor_eq"
		};
		for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
			if (data == words[i]) return true;
		return false;
	}

	void scanPPNumber()
	{
		const CodePoint start = cursor_.peek();
		std::string data;
		int previous = EndOfFile;
		for (;;)
		{
			const LexChar next = peekLex(0, false);
			if (IsDigit(static_cast<int>(next.value)) || next.value == '.' ||
				IsAsciiNondigit(static_cast<int>(next.value)) ||
				InRanges(next.value, AnnexE1,
					sizeof(AnnexE1) / sizeof(AnnexE1[0])))
			{
				const LexChar consumed = takeLex(false);
				AppendUtf8(data, consumed.value);
				previous = static_cast<int>(consumed.value);
				continue;
			}
			if ((next.value == '+' || next.value == '-') &&
				(previous == 'e' || previous == 'E'))
			{
				const LexChar consumed = takeLex(false);
				AppendUtf8(data, consumed.value);
				previous = static_cast<int>(consumed.value);
				continue;
			}
			break;
		}
		emitToken(PPNumber, data, start);
	}

	void scanHeaderName()
	{
		const CodePoint start = cursor_.peek();
		const int opening = cursor_.consume().value;
		const int closing = opening == '<' ? '>' : '"';
		std::string data;
		AppendUtf8(data, static_cast<uint32_t>(opening));
		bool has_character = false;
		for (;;)
		{
			const int value = cursor_.peek().value;
			if (value == EndOfFile || value == '\n')
				throw std::runtime_error("unterminated header name");
			if (value == closing)
			{
				AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
				break;
			}
			AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
			has_character = true;
		}
		if (!has_character) throw std::runtime_error("empty header name");
		emitToken(HeaderName, data, start);
	}

	static bool IsPunctuatorStart(int value)
	{
		static const std::string starts = "{}[]#();:?.,+-*/%^&|~!=<>%";
		return value >= 0 && starts.find(static_cast<char>(value)) !=
			std::string::npos;
	}

	bool matchPunctuator(const std::string& spelling)
	{
		return matchAscii(0, spelling);
	}

	void scanPunctuatorOrOther()
	{
		const CodePoint start = cursor_.peek();
		if (cursor_.peek(0).value == '<' && cursor_.peek(1).value == ':' &&
			cursor_.peek(2).value == ':' && cursor_.peek(3).value != ':' &&
			cursor_.peek(3).value != '>')
		{
			cursor_.consume();
			emitToken(Punctuator, "<", start);
			return;
		}

		static const char* punctuators[] =
		{
			"%:%:", "->*", ">>=", "<<=", "...", "##", "<:", ":>", "<%",
			"%>", "%:", "::", ".*", "+=", "-=", "*=", "/=", "%=", "^=",
			"&=", "|=", "<<", ">>", "<=", ">=", "&&", "==", "!=", "||",
			"++", "--", "->", "{", "}", "[", "]", "#", "(", ")", ";",
			":", "?", ".", "+", "-", "*", "/", "%", "^", "&", "|", "~",
			"!", "=", "<", ">", ","
		};
		for (size_t i = 0; i < sizeof(punctuators) / sizeof(punctuators[0]); ++i)
		{
			if (!matchPunctuator(punctuators[i])) continue;
			std::string data;
			for (size_t j = 0; j < std::string(punctuators[i]).size(); ++j)
				AppendUtf8(data, static_cast<uint32_t>(cursor_.consume().value));
			emitToken(Punctuator, data, start);
			return;
		}

		if (!IsPunctuatorStart(start.value) && start.value != '\'' &&
			start.value != '"')
		{
			const LexChar value = takeLex(false);
			std::string data;
			AppendUtf8(data, value.value);
			emitToken(NonWhitespace, data, start);
			return;
		}
		if (start.value == '\'' || start.value == '"')
			throw std::runtime_error("unterminated quoted literal");
		const LexChar value = takeLex(false);
		std::string data;
		AppendUtf8(data, value.value);
		emitToken(NonWhitespace, data, start);
	}
};

} // namespace

void TokenizePreprocessingSource(const std::string& source,
	IPPTokenStream& output)
{
	PPTokenizer tokenizer(source, output);
	tokenizer.run();
}

bool IsValidIdentifierName(const std::string& spelling)
{
	if (spelling.empty())
		return false;
	size_t offset = 0;
	bool first = true;
	try
	{
		while (offset < spelling.size())
		{
			size_t width = 0;
			const uint32_t value = DecodeUtf8(spelling, offset, width);
			if (first ? !IsIdentifierStart(value) : !IsIdentifierBody(value))
				return false;
		offset += width;
			first = false;
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}
