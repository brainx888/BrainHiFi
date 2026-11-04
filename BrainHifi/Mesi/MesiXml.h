// MesiXml.h - minimal XML element tree with writer and parser.  Supports the
// subset MESI writes: declaration, elements, attributes, text, comments and
// the five predefined entities.  No DTD / namespaces / CDATA processing.
#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mesi {

class XmlParseError : public std::runtime_error
{
public:
	XmlParseError(const std::string& msg, std::size_t offset)
		: std::runtime_error(msg + " at offset " + std::to_string(offset)) {}
};

struct XmlNode
{
	std::string name;
	std::vector<std::pair<std::string, std::string>> attributes;
	std::string text;                 // concatenated character data (trimmed)
	std::vector<XmlNode> children;

	XmlNode() = default;
	explicit XmlNode(std::string n) : name(std::move(n)) {}

	XmlNode& attr(std::string key, std::string value);
	XmlNode& attr(std::string key, double value);
	XmlNode& attr(std::string key, long long value);
	XmlNode& attr(std::string key, bool value);
	XmlNode& child(XmlNode node);
	XmlNode& childText(std::string name, std::string value);

	std::optional<std::string> attribute(std::string_view key) const;
	double      attributeDouble(std::string_view key, double def = 0.0) const;
	long long   attributeInt(std::string_view key, long long def = 0) const;
	bool        attributeBool(std::string_view key, bool def = false) const;

	const XmlNode* firstChild(std::string_view childName) const;
	std::vector<const XmlNode*> childrenNamed(std::string_view childName) const;
	std::string childText(std::string_view childName, const std::string& def = {}) const;

	std::string dump(bool withDeclaration = true, int indent = 2) const;
	static XmlNode parse(std::string_view text);     // throws XmlParseError
};

std::string xmlEscape(std::string_view s);

} // namespace mesi
