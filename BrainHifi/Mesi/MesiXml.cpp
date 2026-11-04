#include "MesiXml.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace mesi {

namespace {

std::string trim(std::string_view s)
{
	std::size_t b = 0, e = s.size();
	while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
	return std::string(s.substr(b, e - b));
}

std::string formatDouble(double v)
{
	char buf[40];
	std::snprintf(buf, sizeof buf, "%.17g", v);
	return buf;
}

class XmlParser
{
public:
	explicit XmlParser(std::string_view t) : m_text(t) {}

	XmlNode parseDocument()
	{
		skipProlog();
		if (peek() != '<') fail("expected root element");
		XmlNode root = parseElement(0);
		skipMisc();
		if (!eof()) fail("trailing content after root element");
		return root;
	}

private:
	static constexpr int kMaxDepth = 256;

	[[noreturn]] void fail(const char* msg) const { throw XmlParseError(msg, m_pos); }
	bool eof() const noexcept { return m_pos >= m_text.size(); }
	char peek(std::size_t ahead = 0) const noexcept { return m_pos + ahead < m_text.size() ? m_text[m_pos + ahead] : '\0'; }
	bool startsWith(std::string_view s) const noexcept { return m_text.substr(m_pos, s.size()) == s; }
	void skipWs() { while (!eof() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos; }

	void skipUntil(std::string_view terminator)
	{
		const std::size_t p = m_text.find(terminator, m_pos);
		if (p == std::string_view::npos) fail("unterminated construct");
		m_pos = p + terminator.size();
	}

	void skipMisc()
	{
		for (;;) {
			skipWs();
			if (startsWith("<!--")) skipUntil("-->");
			else if (startsWith("<?")) skipUntil("?>");
			else break;
		}
	}

	void skipProlog()
	{
		if (m_text.size() >= 3 && static_cast<unsigned char>(m_text[0]) == 0xEF &&
			static_cast<unsigned char>(m_text[1]) == 0xBB && static_cast<unsigned char>(m_text[2]) == 0xBF)
			m_pos = 3;
		for (;;) {
			skipMisc();
			if (startsWith("<!DOCTYPE")) skipUntil(">");
			else break;
		}
	}

	static bool isNameChar(char c) noexcept
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == ':';
	}

	std::string parseName()
	{
		const std::size_t start = m_pos;
		while (!eof() && isNameChar(m_text[m_pos])) ++m_pos;
		if (start == m_pos) fail("expected name");
		return std::string(m_text.substr(start, m_pos - start));
	}

	std::string decodeEntities(std::string_view raw)
	{
		std::string out;
		out.reserve(raw.size());
		for (std::size_t i = 0; i < raw.size(); ++i) {
			if (raw[i] != '&') { out.push_back(raw[i]); continue; }
			const std::size_t semi = raw.find(';', i);
			if (semi == std::string_view::npos) fail("unterminated entity");
			const std::string_view ent = raw.substr(i + 1, semi - i - 1);
			if (ent == "lt") out.push_back('<');
			else if (ent == "gt") out.push_back('>');
			else if (ent == "amp") out.push_back('&');
			else if (ent == "quot") out.push_back('"');
			else if (ent == "apos") out.push_back('\'');
			else if (!ent.empty() && ent[0] == '#') {
				const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
				const std::string digits(ent.substr(hex ? 2 : 1));
				const unsigned long cp = std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10);
				appendUtf8(out, static_cast<unsigned>(cp));
			} else fail("unknown entity");
			i = semi;
		}
		return out;
	}

	static void appendUtf8(std::string& out, unsigned cp)
	{
		if (cp < 0x80) out.push_back(static_cast<char>(cp));
		else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
		else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
		else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
	}

	XmlNode parseElement(int depth)
	{
		if (depth > kMaxDepth) fail("nesting too deep");
		++m_pos; // '<'
		XmlNode node(parseName());
		for (;;) {
			skipWs();
			if (startsWith("/>")) { m_pos += 2; return node; }
			if (peek() == '>') { ++m_pos; break; }
			std::string key = parseName();
			skipWs();
			if (peek() != '=') fail("expected '='");
			++m_pos;
			skipWs();
			const char q = peek();
			if (q != '"' && q != '\'') fail("expected quoted attribute value");
			++m_pos;
			const std::size_t end = m_text.find(q, m_pos);
			if (end == std::string_view::npos) fail("unterminated attribute value");
			node.attributes.emplace_back(std::move(key), decodeEntities(m_text.substr(m_pos, end - m_pos)));
			m_pos = end + 1;
		}
		// content
		std::string text;
		for (;;) {
			if (eof()) fail("unterminated element");
			if (startsWith("</")) {
				m_pos += 2;
				const std::string closing = parseName();
				if (closing != node.name) fail("mismatched closing tag");
				skipWs();
				if (peek() != '>') fail("expected '>'");
				++m_pos;
				break;
			}
			if (startsWith("<!--")) { skipUntil("-->"); continue; }
			if (startsWith("<![CDATA[")) {
				m_pos += 9;
				const std::size_t end = m_text.find("]]>", m_pos);
				if (end == std::string_view::npos) fail("unterminated CDATA");
				text.append(m_text.substr(m_pos, end - m_pos));
				m_pos = end + 3;
				continue;
			}
			if (startsWith("<?")) { skipUntil("?>"); continue; }
			if (peek() == '<') { node.children.push_back(parseElement(depth + 1)); continue; }
			const std::size_t next = m_text.find('<', m_pos);
			const std::size_t end = next == std::string_view::npos ? m_text.size() : next;
			text += decodeEntities(m_text.substr(m_pos, end - m_pos));
			m_pos = end;
		}
		node.text = trim(text);
		return node;
	}

	std::string_view m_text;
	std::size_t m_pos = 0;
};

void dumpNode(const XmlNode& n, std::string& out, int indent, int depth)
{
	const std::string pad(static_cast<std::size_t>(indent > 0 ? depth * indent : 0), ' ');
	out += pad;
	out.push_back('<');
	out += n.name;
	for (const auto& [k, v] : n.attributes) {
		out.push_back(' ');
		out += k;
		out += "=\"";
		out += xmlEscape(v);
		out.push_back('"');
	}
	if (n.children.empty() && n.text.empty()) {
		out += "/>";
		if (indent > 0) out.push_back('\n');
		return;
	}
	out.push_back('>');
	if (n.children.empty()) {
		out += xmlEscape(n.text);
	} else {
		if (indent > 0) out.push_back('\n');
		if (!n.text.empty()) {
			out += pad;
			out.append(static_cast<std::size_t>(indent), ' ');
			out += xmlEscape(n.text);
			if (indent > 0) out.push_back('\n');
		}
		for (const auto& c : n.children) dumpNode(c, out, indent, depth + 1);
		out += pad;
	}
	out += "</";
	out += n.name;
	out.push_back('>');
	if (indent > 0) out.push_back('\n');
}

} // namespace

std::string xmlEscape(std::string_view s)
{
	std::string out;
	out.reserve(s.size());
	for (const char c : s) {
		switch (c) {
		case '<':  out += "&lt;";   break;
		case '>':  out += "&gt;";   break;
		case '&':  out += "&amp;";  break;
		case '"':  out += "&quot;"; break;
		case '\'': out += "&apos;"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\r' && c != '\t') {
				char buf[12];
				std::snprintf(buf, sizeof buf, "&#x%02X;", static_cast<unsigned>(static_cast<unsigned char>(c)));
				out += buf;
			} else out.push_back(c);
		}
	}
	return out;
}

XmlNode& XmlNode::attr(std::string key, std::string value) { attributes.emplace_back(std::move(key), std::move(value)); return *this; }
XmlNode& XmlNode::attr(std::string key, double value)      { return attr(std::move(key), formatDouble(value)); }
XmlNode& XmlNode::attr(std::string key, long long value)   { return attr(std::move(key), std::to_string(value)); }
XmlNode& XmlNode::attr(std::string key, bool value)        { return attr(std::move(key), std::string(value ? "true" : "false")); }
XmlNode& XmlNode::child(XmlNode node) { children.push_back(std::move(node)); return *this; }
XmlNode& XmlNode::childText(std::string n, std::string value)
{
	XmlNode c(std::move(n));
	c.text = std::move(value);
	children.push_back(std::move(c));
	return *this;
}

std::optional<std::string> XmlNode::attribute(std::string_view key) const
{
	for (const auto& [k, v] : attributes)
		if (k == key) return v;
	return std::nullopt;
}
double XmlNode::attributeDouble(std::string_view key, double def) const
{
	const auto a = attribute(key);
	if (!a) return def;
	char* end = nullptr;
	const double v = std::strtod(a->c_str(), &end);
	return end == a->c_str() ? def : v;
}
long long XmlNode::attributeInt(std::string_view key, long long def) const
{
	const auto a = attribute(key);
	if (!a) return def;
	char* end = nullptr;
	const long long v = std::strtoll(a->c_str(), &end, 10);
	return end == a->c_str() ? def : v;
}
bool XmlNode::attributeBool(std::string_view key, bool def) const
{
	const auto a = attribute(key);
	if (!a) return def;
	return *a == "true" || *a == "1";
}

const XmlNode* XmlNode::firstChild(std::string_view childName) const
{
	for (const auto& c : children)
		if (c.name == childName) return &c;
	return nullptr;
}
std::vector<const XmlNode*> XmlNode::childrenNamed(std::string_view childName) const
{
	std::vector<const XmlNode*> out;
	for (const auto& c : children)
		if (c.name == childName) out.push_back(&c);
	return out;
}
std::string XmlNode::childText(std::string_view childName, const std::string& def) const
{
	const XmlNode* c = firstChild(childName);
	return c ? c->text : def;
}

std::string XmlNode::dump(bool withDeclaration, int indent) const
{
	std::string out;
	if (withDeclaration) out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	dumpNode(*this, out, indent, 0);
	return out;
}

XmlNode XmlNode::parse(std::string_view text) { return XmlParser(text).parseDocument(); }

} // namespace mesi
