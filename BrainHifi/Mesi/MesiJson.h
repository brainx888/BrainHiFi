// MesiJson.h - small, dependency-free JSON value / parser / writer used for
// MESI persistence.  Objects keep insertion order so written files are stable
// and diff-friendly.  Not intended as a general-purpose JSON library.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mesi {

class JsonParseError : public std::runtime_error
{
public:
	JsonParseError(const std::string& msg, std::size_t offset)
		: std::runtime_error(msg + " at offset " + std::to_string(offset)), m_offset(offset) {}
	std::size_t offset() const noexcept { return m_offset; }
private:
	std::size_t m_offset;
};

class JsonValue
{
public:
	enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

	using Array  = std::vector<JsonValue>;
	using Member = std::pair<std::string, JsonValue>;
	using Object = std::vector<Member>;

	JsonValue() = default;
	JsonValue(std::nullptr_t) {}
	JsonValue(bool b) : m_type(Type::Bool), m_bool(b) {}
	JsonValue(int v) : m_type(Type::Number), m_number(v) {}
	JsonValue(unsigned v) : m_type(Type::Number), m_number(v) {}
	JsonValue(long long v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
	JsonValue(unsigned long long v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
	JsonValue(double v) : m_type(Type::Number), m_number(v) {}
	JsonValue(const char* s) : m_type(Type::String), m_string(s ? s : "") {}
	JsonValue(std::string s) : m_type(Type::String), m_string(std::move(s)) {}
	JsonValue(std::string_view s) : m_type(Type::String), m_string(s) {}
	JsonValue(Array a) : m_type(Type::Array), m_array(std::move(a)) {}
	JsonValue(Object o) : m_type(Type::Object), m_object(std::move(o)) {}

	static JsonValue object() { return JsonValue(Object{}); }
	static JsonValue array()  { return JsonValue(Array{}); }

	Type type() const noexcept { return m_type; }
	bool isNull()   const noexcept { return m_type == Type::Null; }
	bool isBool()   const noexcept { return m_type == Type::Bool; }
	bool isNumber() const noexcept { return m_type == Type::Number; }
	bool isString() const noexcept { return m_type == Type::String; }
	bool isArray()  const noexcept { return m_type == Type::Array; }
	bool isObject() const noexcept { return m_type == Type::Object; }

	// Typed accessors with defaults (never throw).
	bool        asBool(bool def = false) const noexcept { return isBool() ? m_bool : def; }
	double      asDouble(double def = 0.0) const noexcept { return isNumber() ? m_number : def; }
	int         asInt(int def = 0) const noexcept { return isNumber() ? static_cast<int>(m_number) : def; }
	long long   asInt64(long long def = 0) const noexcept { return isNumber() ? static_cast<long long>(m_number) : def; }
	std::string asString(const std::string& def = {}) const { return isString() ? m_string : def; }

	const Array&  asArray()  const;   // throws std::logic_error if not array
	Array&        asArray();
	const Object& asObject() const;   // throws std::logic_error if not object
	Object&       asObject();

	// Object helpers --------------------------------------------------------
	bool has(std::string_view key) const noexcept;
	const JsonValue& operator[](std::string_view key) const;  // Null singleton when absent
	JsonValue&       operator[](std::string_view key);        // inserts Null when absent (object only)
	JsonValue& set(std::string key, JsonValue value);         // fluent

	// Array helpers ---------------------------------------------------------
	std::size_t size() const noexcept;
	const JsonValue& at(std::size_t i) const;
	JsonValue& push(JsonValue v);

	// Serialization ---------------------------------------------------------
	std::string dump(int indent = 2) const;
	static JsonValue parse(std::string_view text);  // throws JsonParseError

	bool operator==(const JsonValue& o) const noexcept;
	bool operator!=(const JsonValue& o) const noexcept { return !(*this == o); }

private:
	void dumpTo(std::string& out, int indent, int depth) const;
	static void appendEscaped(std::string& out, std::string_view s);

	Type        m_type = Type::Null;
	bool        m_bool = false;
	double      m_number = 0.0;
	std::string m_string;
	Array       m_array;
	Object      m_object;
};

// File helpers: UTF-8 text in / out.  Return false on I/O failure.
bool writeTextFile(const std::string& utf8Path, std::string_view text);
std::optional<std::string> readTextFile(const std::string& utf8Path);

} // namespace mesi
