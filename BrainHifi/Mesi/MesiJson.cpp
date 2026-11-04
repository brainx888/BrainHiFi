#include "MesiJson.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <Windows.h>

namespace mesi {

namespace {

const JsonValue& nullValue()
{
	static const JsonValue kNull;
	return kNull;
}

void appendUtf8(std::string& out, std::uint32_t cp)
{
	if (cp < 0x80) out.push_back(static_cast<char>(cp));
	else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

class Parser
{
public:
	explicit Parser(std::string_view text) : m_text(text) {}

	JsonValue parseDocument()
	{
		skipWs();
		JsonValue v = parseValue(0);
		skipWs();
		if (m_pos != m_text.size()) fail("trailing characters");
		return v;
	}

private:
	static constexpr int kMaxDepth = 256;

	[[noreturn]] void fail(const char* msg) const { throw JsonParseError(msg, m_pos); }

	bool eof() const noexcept { return m_pos >= m_text.size(); }
	char peek() const noexcept { return eof() ? '\0' : m_text[m_pos]; }
	char next() { if (eof()) fail("unexpected end of input"); return m_text[m_pos++]; }

	void skipWs()
	{
		while (!eof()) {
			const char c = m_text[m_pos];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++m_pos;
			else break;
		}
	}

	void expectLiteral(const char* lit)
	{
		const std::size_t n = std::strlen(lit);
		if (m_text.substr(m_pos, n) != lit) fail("invalid literal");
		m_pos += n;
	}

	JsonValue parseValue(int depth)
	{
		if (depth > kMaxDepth) fail("nesting too deep");
		switch (peek()) {
		case '{': return parseObject(depth);
		case '[': return parseArray(depth);
		case '"': return JsonValue(parseString());
		case 't': expectLiteral("true");  return JsonValue(true);
		case 'f': expectLiteral("false"); return JsonValue(false);
		case 'n': expectLiteral("null");  return JsonValue(nullptr);
		default:  return parseNumber();
		}
	}

	JsonValue parseObject(int depth)
	{
		next(); // '{'
		JsonValue::Object members;
		skipWs();
		if (peek() == '}') { ++m_pos; return JsonValue(std::move(members)); }
		for (;;) {
			skipWs();
			if (peek() != '"') fail("expected string key");
			std::string key = parseString();
			skipWs();
			if (next() != ':') fail("expected ':'");
			skipWs();
			members.emplace_back(std::move(key), parseValue(depth + 1));
			skipWs();
			const char c = next();
			if (c == ',') continue;
			if (c == '}') break;
			fail("expected ',' or '}'");
		}
		return JsonValue(std::move(members));
	}

	JsonValue parseArray(int depth)
	{
		next(); // '['
		JsonValue::Array items;
		skipWs();
		if (peek() == ']') { ++m_pos; return JsonValue(std::move(items)); }
		for (;;) {
			skipWs();
			items.push_back(parseValue(depth + 1));
			skipWs();
			const char c = next();
			if (c == ',') continue;
			if (c == ']') break;
			fail("expected ',' or ']'");
		}
		return JsonValue(std::move(items));
	}

	std::uint32_t parseHex4()
	{
		std::uint32_t v = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = next();
			v <<= 4;
			if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
			else fail("invalid \\u escape");
		}
		return v;
	}

	std::string parseString()
	{
		next(); // '"'
		std::string out;
		for (;;) {
			const char c = next();
			if (c == '"') break;
			if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
			if (c != '\\') { out.push_back(c); continue; }
			const char e = next();
			switch (e) {
			case '"':  out.push_back('"');  break;
			case '\\': out.push_back('\\'); break;
			case '/':  out.push_back('/');  break;
			case 'b':  out.push_back('\b'); break;
			case 'f':  out.push_back('\f'); break;
			case 'n':  out.push_back('\n'); break;
			case 'r':  out.push_back('\r'); break;
			case 't':  out.push_back('\t'); break;
			case 'u': {
				std::uint32_t cp = parseHex4();
				if (cp >= 0xD800 && cp <= 0xDBFF) {              // surrogate pair
					if (next() != '\\' || next() != 'u') fail("missing low surrogate");
					const std::uint32_t lo = parseHex4();
					if (lo < 0xDC00 || lo > 0xDFFF) fail("invalid low surrogate");
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				appendUtf8(out, cp);
				break;
			}
			default: fail("invalid escape");
			}
		}
		return out;
	}

	JsonValue parseNumber()
	{
		const std::size_t start = m_pos;
		if (peek() == '-') ++m_pos;
		if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid number");
		while (std::isdigit(static_cast<unsigned char>(peek()))) ++m_pos;
		if (peek() == '.') { ++m_pos; while (std::isdigit(static_cast<unsigned char>(peek()))) ++m_pos; }
		if (peek() == 'e' || peek() == 'E') {
			++m_pos;
			if (peek() == '+' || peek() == '-') ++m_pos;
			if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid exponent");
			while (std::isdigit(static_cast<unsigned char>(peek()))) ++m_pos;
		}
		const std::string token(m_text.substr(start, m_pos - start));
		char* end = nullptr;
		const double v = std::strtod(token.c_str(), &end);
		if (end == token.c_str()) fail("invalid number");
		return JsonValue(v);
	}

	std::string_view m_text;
	std::size_t m_pos = 0;
};

} // namespace

// ---------------------------------------------------------------------------

const JsonValue::Array& JsonValue::asArray() const
{
	if (!isArray()) throw std::logic_error("JsonValue is not an array");
	return m_array;
}
JsonValue::Array& JsonValue::asArray()
{
	if (!isArray()) throw std::logic_error("JsonValue is not an array");
	return m_array;
}
const JsonValue::Object& JsonValue::asObject() const
{
	if (!isObject()) throw std::logic_error("JsonValue is not an object");
	return m_object;
}
JsonValue::Object& JsonValue::asObject()
{
	if (!isObject()) throw std::logic_error("JsonValue is not an object");
	return m_object;
}

bool JsonValue::has(std::string_view key) const noexcept
{
	if (!isObject()) return false;
	for (const auto& m : m_object)
		if (m.first == key) return true;
	return false;
}

const JsonValue& JsonValue::operator[](std::string_view key) const
{
	if (isObject())
		for (const auto& m : m_object)
			if (m.first == key) return m.second;
	return nullValue();
}

JsonValue& JsonValue::operator[](std::string_view key)
{
	if (!isObject()) throw std::logic_error("JsonValue is not an object");
	for (auto& m : m_object)
		if (m.first == key) return m.second;
	m_object.emplace_back(std::string(key), JsonValue());
	return m_object.back().second;
}

JsonValue& JsonValue::set(std::string key, JsonValue value)
{
	(*this)[key] = std::move(value);
	return *this;
}

std::size_t JsonValue::size() const noexcept
{
	if (isArray()) return m_array.size();
	if (isObject()) return m_object.size();
	return 0;
}

const JsonValue& JsonValue::at(std::size_t i) const
{
	if (!isArray() || i >= m_array.size()) return nullValue();
	return m_array[i];
}

JsonValue& JsonValue::push(JsonValue v)
{
	if (!isArray()) throw std::logic_error("JsonValue is not an array");
	m_array.push_back(std::move(v));
	return *this;
}

bool JsonValue::operator==(const JsonValue& o) const noexcept
{
	if (m_type != o.m_type) return false;
	switch (m_type) {
	case Type::Null:   return true;
	case Type::Bool:   return m_bool == o.m_bool;
	case Type::Number: return m_number == o.m_number;
	case Type::String: return m_string == o.m_string;
	case Type::Array:  return m_array == o.m_array;
	case Type::Object: return m_object == o.m_object;
	}
	return false;
}

void JsonValue::appendEscaped(std::string& out, std::string_view s)
{
	out.push_back('"');
	for (const char c : s) {
		switch (c) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b";  break;
		case '\f': out += "\\f";  break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof buf, "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
				out += buf;
			} else {
				out.push_back(c);   // UTF-8 passes through untouched
			}
		}
	}
	out.push_back('"');
}

void JsonValue::dumpTo(std::string& out, int indent, int depth) const
{
	const auto newline = [&](int d) {
		if (indent <= 0) return;
		out.push_back('\n');
		out.append(static_cast<std::size_t>(d * indent), ' ');
	};

	switch (m_type) {
	case Type::Null:   out += "null"; break;
	case Type::Bool:   out += m_bool ? "true" : "false"; break;
	case Type::Number: {
		if (!std::isfinite(m_number)) { out += "null"; break; }   // JSON has no NaN/Inf
		if (m_number == std::floor(m_number) && std::fabs(m_number) < 1e15) {
			char buf[32];
			std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(m_number));
			out += buf;
		} else {
			char buf[40];
			std::snprintf(buf, sizeof buf, "%.17g", m_number);
			out += buf;
		}
		break;
	}
	case Type::String: appendEscaped(out, m_string); break;
	case Type::Array: {
		if (m_array.empty()) { out += "[]"; break; }
		out.push_back('[');
		for (std::size_t i = 0; i < m_array.size(); ++i) {
			if (i) out.push_back(',');
			newline(depth + 1);
			m_array[i].dumpTo(out, indent, depth + 1);
		}
		newline(depth);
		out.push_back(']');
		break;
	}
	case Type::Object: {
		if (m_object.empty()) { out += "{}"; break; }
		out.push_back('{');
		for (std::size_t i = 0; i < m_object.size(); ++i) {
			if (i) out.push_back(',');
			newline(depth + 1);
			appendEscaped(out, m_object[i].first);
			out += indent > 0 ? ": " : ":";
			m_object[i].second.dumpTo(out, indent, depth + 1);
		}
		newline(depth);
		out.push_back('}');
		break;
	}
	}
}

std::string JsonValue::dump(int indent) const
{
	std::string out;
	out.reserve(256);
	dumpTo(out, indent, 0);
	return out;
}

JsonValue JsonValue::parse(std::string_view text)
{
	// Tolerate a UTF-8 BOM written by some editors.
	if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
		static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
		text.remove_prefix(3);
	return Parser(text).parseDocument();
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#include <windows.h>
namespace {
std::wstring utf8ToWide(const std::string& s)
{
	if (s.empty()) return {};
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(static_cast<std::size_t>(n), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
	return w;
}
} // namespace
#endif

bool writeTextFile(const std::string& utf8Path, std::string_view text)
{
	// Write to a sibling temp file first and rename, so a crash never leaves a
	// half-written workspace file behind.
	const std::string tmpPath = utf8Path + ".tmp";
	{
#if defined(_WIN32)
		std::ofstream f(utf8ToWide(tmpPath), std::ios::binary | std::ios::trunc);
#else
		std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
#endif
		if (!f) return false;
		f.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!f) return false;
	}
#if defined(_WIN32)
	return ::MoveFileExW(utf8ToWide(tmpPath).c_str(), utf8ToWide(utf8Path).c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return std::rename(tmpPath.c_str(), utf8Path.c_str()) == 0;
#endif
}

std::optional<std::string> readTextFile(const std::string& utf8Path)
{
#if defined(_WIN32)
	std::ifstream f(utf8ToWide(utf8Path), std::ios::binary);
#else
	std::ifstream f(utf8Path, std::ios::binary);
#endif
	if (!f) return std::nullopt;
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

} // namespace mesi
