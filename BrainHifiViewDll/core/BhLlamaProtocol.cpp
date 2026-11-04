#include "BhLlamaProtocol.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>

// ---------------------------------------------------------------- SBhJson
const SBhJson* SBhJson::find(const std::string& sKey) const
{
	if (eType != Object) return nullptr;
	for (const auto& kv : o) if (kv.first == sKey) return &kv.second;
	return nullptr;
}
const SBhJson* SBhJson::at(size_t i) const { return eType == Array && i < a.size() ? &a[i] : nullptr; }
std::string SBhJson::str(const std::string& sDefault) const
{
	if (eType == String) return s;
	if (eType == Number) { char buf[64]; std::snprintf(buf, sizeof(buf), "%.10g", d); return buf; }
	if (eType == Bool) return b ? "true" : "false";
	return sDefault;
}
double SBhJson::num(double dDefault) const
{
	if (eType == Number) return d;
	if (eType == String) { char* pEnd = nullptr; double v = std::strtod(s.c_str(), &pEnd); if (pEnd && pEnd != s.c_str()) return v; }
	return dDefault;
}

namespace {

struct CParser
{
	const char* p; const char* e; int nDepth = 0;
	void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p; }
	bool lit(const char* sz) { size_t n = std::strlen(sz); if ((size_t)(e - p) < n || std::strncmp(p, sz, n)) return false; p += n; return true; }
	static void utf8(unsigned cp, std::string& s)
	{
		if (cp < 0x80) s += (char)cp;
		else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
		else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
	}
	bool hex4(unsigned& v)
	{
		if (e - p < 4) return false;
		v = 0;
		for (int i = 0; i < 4; i++) {
			const char c = *p++;
			v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return false;
		}
		return true;
	}
	bool string(std::string& s)
	{
		if (p >= e || *p != '"') return false;
		++p;
		while (p < e && *p != '"') {
			char c = *p++;
			if (c != '\\') { s += c; continue; }
			if (p >= e) return false;
			c = *p++;
			switch (c) {
			case '"': s += '"'; break; case '\\': s += '\\'; break; case '/': s += '/'; break;
			case 'b': s += '\b'; break; case 'f': s += '\f'; break; case 'n': s += '\n'; break;
			case 'r': s += '\r'; break; case 't': s += '\t'; break;
			case 'u': {
				unsigned cp;
				if (!hex4(cp)) return false;
				if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
					p += 2; unsigned lo;
					if (!hex4(lo)) return false;
					if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); else { utf8(0xFFFD, s); cp = lo; }
				}
				utf8(cp, s);
				break;
			}
			default: return false;
			}
		}
		if (p >= e) return false;
		++p;
		return true;
	}
	bool value(SBhJson& v)
	{
		if (++nDepth > 200) return false;
		ws();
		bool bOk = false;
		if (p >= e) bOk = false;
		else if (*p == '{') {
			v.eType = SBhJson::Object; ++p; ws();
			if (p < e && *p == '}') { ++p; bOk = true; }
			else for (;;) {
				ws(); std::string k;
				if (!string(k)) break;
				ws(); if (p >= e || *p != ':') break; ++p;
				v.o.emplace_back(k, SBhJson());
				if (!value(v.o.back().second)) break;
				ws();
				if (p < e && *p == ',') { ++p; continue; }
				if (p < e && *p == '}') { ++p; bOk = true; }
				break;
			}
		}
		else if (*p == '[') {
			v.eType = SBhJson::Array; ++p; ws();
			if (p < e && *p == ']') { ++p; bOk = true; }
			else for (;;) {
				v.a.emplace_back();
				if (!value(v.a.back())) break;
				ws();
				if (p < e && *p == ',') { ++p; continue; }
				if (p < e && *p == ']') { ++p; bOk = true; }
				break;
			}
		}
		else if (*p == '"') { v.eType = SBhJson::String; bOk = string(v.s); }
		else if (lit("true")) { v.eType = SBhJson::Bool; v.b = true; bOk = true; }
		else if (lit("false")) { v.eType = SBhJson::Bool; bOk = true; }
		else if (lit("null")) { v.eType = SBhJson::Null; bOk = true; }
		else {
			std::string sNum(p, (size_t)(e - p) < 64 ? (size_t)(e - p) : 64);
			char* pEnd = nullptr;
			v.d = std::strtod(sNum.c_str(), &pEnd);
			if (pEnd && pEnd != sNum.c_str()) { v.eType = SBhJson::Number; p += pEnd - sNum.c_str(); bOk = true; }
		}
		--nDepth;
		return bOk;
	}
};

}

bool SBhJson::parse(const std::string& sText, SBhJson& out, std::string* pError)
{
	out = SBhJson();
	CParser ps{ sText.data(), sText.data() + sText.size() };
	bool bOk = ps.value(out);
	if (bOk) { ps.ws(); bOk = ps.p == ps.e; }
	if (!bOk) { if (pError) *pError = "invalid JSON at offset " + std::to_string(ps.p - sText.data()); out = SBhJson(); }
	return bOk;
}

std::string SBhJson::escape(const std::string& s)
{
	std::string r = "\"";
	for (unsigned char c : s) {
		switch (c) {
		case '"': r += "\\\""; break; case '\\': r += "\\\\"; break;
		case '\n': r += "\\n"; break; case '\r': r += "\\r"; break; case '\t': r += "\\t"; break;
		case '\b': r += "\\b"; break; case '\f': r += "\\f"; break;
		default:
			if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); r += buf; }
			else r += (char)c;
		}
	}
	return r + "\"";
}

// ------------------------------------------------------- CBhLlamaProtocol
std::string CBhLlamaProtocol::base64(const unsigned char* p, size_t n)
{
	static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string r;
	r.reserve((n + 2) / 3 * 4);
	for (size_t i = 0; i < n; i += 3) {
		const unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
		r += t[(v >> 18) & 63]; r += t[(v >> 12) & 63];
		r += i + 1 < n ? t[(v >> 6) & 63] : '=';
		r += i + 2 < n ? t[v & 63] : '=';
	}
	return r;
}

std::string CBhLlamaProtocol::redactKeys(const std::string& s)
{
	std::string r;
	for (size_t i = 0; i < s.size();) {
		if (s.compare(i, 4, "LLM|") == 0 || s.compare(i, 4, "LLM_") == 0) {
			size_t j = i + 4;
			while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '|' || s[j] == '_' || s[j] == '-')) ++j;
			if (j - i >= 16) { r += "LLM_***"; i = j; continue; }
		}
		r += s[i++];
	}
	return r;
}

std::string CBhLlamaProtocol::buildChatRequest(const SBhLlamaRequest& q)
{
	std::string j = "{\"model\":" + SBhJson::escape(q.sModel) + ",\"messages\":[";
	if (!q.sSystem.empty()) j += "{\"role\":\"system\",\"content\":" + SBhJson::escape(q.sSystem) + "},";
	j += "{\"role\":\"user\",\"content\":";
	if (q.vImages.empty()) j += SBhJson::escape(q.sUser);
	else {
		j += "[{\"type\":\"text\",\"text\":" + SBhJson::escape(q.sUser) + "}";
		for (const SBhLlamaImage& im : q.vImages) {
			if (!im.sCaption.empty()) j += ",{\"type\":\"text\",\"text\":" + SBhJson::escape(im.sCaption) + "}";
			j += ",{\"type\":\"image_url\",\"image_url\":{\"url\":" + SBhJson::escape("data:" + im.sMime + ";base64," + im.sBase64) + "}}";
		}
		j += "]";
	}
	char buf[128];
	std::snprintf(buf, sizeof(buf), "}],\"%s\":%d,\"temperature\":%.3g}",
	              q.bCompat ? "max_tokens" : "max_completion_tokens", q.nMaxTokens, q.dTemperature);
	return j + buf;
}

namespace {

std::string contentText(const SBhJson* c)
{
	if (!c) return std::string();
	if (c->eType == SBhJson::String) return c->s;
	if (c->eType == SBhJson::Object) { const SBhJson* t = c->find("text"); return t ? t->str() : std::string(); }
	if (c->eType == SBhJson::Array) {
		std::string s;
		for (const SBhJson& part : c->a) s += contentText(&part);
		return s;
	}
	return std::string();
}

std::string errorText(const SBhJson& j)
{
	if (const SBhJson* e = j.find("error")) {
		if (e->eType == SBhJson::Object) { const SBhJson* m = e->find("message"); if (m) return m->str(); }
		return e->str();
	}
	for (const char* k : { "detail", "message", "title" })
		if (const SBhJson* m = j.find(k)) { std::string s = m->str(); if (!s.empty()) return s; }
	return std::string();
}

}

SBhLlamaReply CBhLlamaProtocol::parseChatResponse(int nHttpStatus, const std::string& sBody)
{
	SBhLlamaReply r;
	SBhJson j;
	std::string sErr;
	const bool bJson = SBhJson::parse(sBody, j, &sErr);
	if (nHttpStatus < 200 || nHttpStatus >= 300) {
		std::string sMsg = bJson ? errorText(j) : sBody.substr(0, 300);
		r.sError = "HTTP " + std::to_string(nHttpStatus) + (sMsg.empty() ? std::string() : ": " + sMsg);
		if (nHttpStatus == 401) r.sError += " (check the Meta Llama API key)";
		else if (nHttpStatus == 403 || nHttpStatus == 400) r.sError += " (check that the model is enabled for your Llama API app)";
		r.sError = redactKeys(r.sError);
		return r;
	}
	if (!bJson) { r.sError = "unreadable response: " + sErr; return r; }
	if (const SBhJson* cm = j.find("completion_message")) {            // native Llama API
		r.sText = contentText(cm->find("content"));
		if (const SBhJson* sr = cm->find("stop_reason")) r.sStopReason = sr->str();
		if (const SBhJson* m = j.find("metrics"))
			for (const SBhJson& x : m->a) {
				const std::string n = x.find("metric") ? x.find("metric")->str() : std::string();
				const int v = x.find("value") ? (int)x.find("value")->num(-1) : -1;
				if (n == "num_prompt_tokens") r.nPromptTokens = v;
				else if (n == "num_completion_tokens") r.nCompletionTokens = v;
			}
	}
	else if (const SBhJson* ch = j.find("choices")) {                  // OpenAI-compatible
		const SBhJson* c0 = ch->at(0);
		if (c0) {
			if (const SBhJson* m = c0->find("message")) r.sText = contentText(m->find("content"));
			if (const SBhJson* fr = c0->find("finish_reason")) r.sStopReason = fr->str();
		}
		if (const SBhJson* u = j.find("usage")) {
			if (const SBhJson* v = u->find("prompt_tokens")) r.nPromptTokens = (int)v->num(-1);
			if (const SBhJson* v = u->find("completion_tokens")) r.nCompletionTokens = (int)v->num(-1);
		}
	}
	else { r.sError = errorText(j); if (r.sError.empty()) r.sError = "response has no completion_message/choices"; return r; }
	r.bOk = !r.sText.empty();
	if (!r.bOk) r.sError = "empty completion";
	return r;
}

bool CBhLlamaProtocol::parseModelList(const std::string& sBody, std::vector<std::string>& vIds, std::string* pError)
{
	vIds.clear();
	SBhJson j;
	if (!SBhJson::parse(sBody, j, pError)) return false;
	const SBhJson* d = j.find("data");
	if (!d || d->eType != SBhJson::Array) { if (pError) *pError = "no data array"; return false; }
	for (const SBhJson& m : d->a) if (const SBhJson* id = m.find("id")) vIds.push_back(id->str());
	return true;
}

std::string CBhLlamaProtocol::systemPrompt()
{
	return
		"You are a neuroimaging research assistant for magnetic source imaging (MSI): MEG source estimates "
		"overlaid on the subject's anatomical MRI. Coordinates are subject head-frame millimetres from the "
		"BrainHifi viewer, NOT MNI/Talairach: +x points to the subject's left, +y superior, +z posterior. "
		"'Relative to MRI centre' subtracts the centre of the MRI volume; 'box fraction' is the 0..1 position "
		"inside the MRI field of view along right->left, inferior->superior, anterior->posterior. "
		"Reason from these to the most likely anatomical structures, state your uncertainty, and do not "
		"assume co-registration to a template unless told. Results are for research and education only; "
		"never give a clinical diagnosis or treatment advice or medical consultation.";
}

std::string CBhLlamaProtocol::buildSegmentationPrompt(const SBhMsiSegmentResult& seg, const std::string& sContext,
                                                      const std::vector<std::string>& vViewCaptions)
{
	std::string s = "Task: interpret the segmented MSI.\n";
	if (!sContext.empty()) s += "Study context: " + sContext + "\n";
	s += "\nLocal segmentation of the source map (clusters above threshold):\n";
	s += CBhMsiPeakAnalyzer::formatReport(seg);
	s += "\nPeak points:\n" + CBhMsiPeakAnalyzer::formatPoints(CBhMsiPeakAnalyzer::peakPoints(seg));
	if (!vViewCaptions.empty()) {
		s += "\nAttached images are rendered MSI slices (grey MRI, colour = source strength, crosshair on the peak):\n";
		for (const std::string& c : vViewCaptions) s += "- " + c + "\n";
	}
	s += "\nPlease reply in plain text with:\n"
	     "1. A short overall summary of the activation pattern (focal vs. distributed, laterality, polarity).\n"
	     "2. For each cluster: likely anatomical location (hemisphere, lobe, gyrus/sulcus), and whether the "
	     "images agree with the coordinates.\n"
	     "3. Possible functional meaning and caveats (MEG depth sensitivity, spatial spread, artefact sources "
	     "such as eyes, muscles or the edge of the source grid).\n";
	return s;
}

std::string CBhLlamaProtocol::buildCoordinatePrompt(const std::vector<SBhMsiPoint>& vPoints, const std::string& sContext)
{
	std::string s = "Task: explain the anatomical location and meaning of these MSI points.\n";
	if (!sContext.empty()) s += "Study context: " + sContext + "\n";
	s += "\nPoints:\n" + CBhMsiPeakAnalyzer::formatPoints(vPoints);
	s += "\nReturn ONLY a JSON array, one object per point in the same order, with string fields: "
	     "\"label\", \"hemisphere\", \"lobe\", \"region\" (gyrus/sulcus or structure), \"brodmann\", "
	     "\"network\" (functional network), \"function\" (typical function of the region), "
	     "\"interpretation\" (what activity of this sign and strength may mean here, with caveats), "
	     "\"confidence\" (low/medium/high). No markdown.";
	return s;
}

bool CBhLlamaProtocol::parseExplanations(const std::string& sText, std::vector<SBhMsiPointExplanation>& v)
{
	v.clear();
	const size_t b = sText.find('['), e = sText.rfind(']');
	if (b == std::string::npos || e == std::string::npos || e < b) return false;
	SBhJson j;
	if (!SBhJson::parse(sText.substr(b, e - b + 1), j) || j.eType != SBhJson::Array) return false;
	for (const SBhJson& o : j.a) {
		if (o.eType != SBhJson::Object) continue;
		auto g = [&](const char* k) { const SBhJson* x = o.find(k); return x ? x->str() : std::string(); };
		SBhMsiPointExplanation x;
		x.sLabel = g("label"); x.sHemisphere = g("hemisphere"); x.sLobe = g("lobe"); x.sRegion = g("region");
		x.sBrodmann = g("brodmann"); x.sNetwork = g("network"); x.sFunction = g("function");
		x.sInterpretation = g("interpretation"); x.sConfidence = g("confidence");
		v.push_back(x);
	}
	return !v.empty();
}

std::string CBhLlamaProtocol::formatExplanations(const std::vector<SBhMsiPointExplanation>& v)
{
	std::string s;
	for (size_t i = 0; i < v.size(); i++) {
		const SBhMsiPointExplanation& x = v[i];
		s += std::to_string(i + 1) + ". " + x.sLabel + ": " + x.sHemisphere + " " + x.sLobe + ", " + x.sRegion;
		if (!x.sBrodmann.empty()) s += " (BA " + x.sBrodmann + ")";
		s += " [confidence: " + x.sConfidence + "]\n";
		if (!x.sNetwork.empty()) s += "   Network: " + x.sNetwork + "\n";
		if (!x.sFunction.empty()) s += "   Function: " + x.sFunction + "\n";
		if (!x.sInterpretation.empty()) s += "   Meaning: " + x.sInterpretation + "\n";
	}
	return s;
}

namespace {

std::string lowerAscii(std::string s)
{
	for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
	return s;
}

}

std::string CBhLlamaProtocol::buildImageSegmentationPrompt(const std::string& sInstruction, const std::string& sContext,
                                                           const std::string& sImageCaption)
{
	std::string s =
		"Task: segment and describe the attached magnetic source imaging (MSI) picture: greyscale anatomical "
		"MRI with the MEG source estimate overlaid in colour (brighter / warmer = stronger source).\n";
	if (!sImageCaption.empty()) s += "Image: " + sImageCaption + "\n";
	if (!sInstruction.empty()) s += "Instruction from the user: " + sInstruction + "\n";
	if (!sContext.empty()) s += "Study context: " + sContext + "\n";
	s += "\nPlease reply in plain text with:\n"
	     "1. The view you see (axial, coronal or sagittal), the orientation labels if visible, and image quality.\n"
	     "2. Each distinct colour-coded activation region (one numbered segment each): approximate centre and "
	     "extent as fractions of image width/height (0,0 = top-left), colour / relative strength, and the most "
	     "likely anatomical location (hemisphere, lobe, gyrus/sulcus).\n"
	     "3. Which segment holds the strongest peak.\n"
	     "4. Possible functional meaning and caveats: no coordinates come with this image, so locations are "
	     "estimated from the visible anatomy only; also MEG depth sensitivity, spatial spread and artefacts.\n";
	return s;
}

std::string CBhLlamaProtocol::imageMimeFromPath(const std::string& sPath)
{
	const size_t nDot = sPath.rfind('.');
	if (nDot == std::string::npos) return std::string();
	const std::string e = lowerAscii(sPath.substr(nDot + 1));
	if (e == "png") return "image/png";
	if (e == "jpg" || e == "jpeg" || e == "jpe") return "image/jpeg";
	if (e == "gif") return "image/gif";
	if (e == "bmp" || e == "dib") return "image/bmp";
	if (e == "webp") return "image/webp";
	if (e == "tif" || e == "tiff") return "image/tiff";
	return std::string();
}

std::string CBhLlamaProtocol::chooseModel(const std::vector<std::string>& vEnabled, const std::string& sPreferred,
                                          bool bNeedVision)
{
	const std::string sPref = lowerAscii(sPreferred);
	for (const std::string& m : vEnabled) if (lowerAscii(m) == sPref) return m;
	for (const char* szFamily : { "muse-wonder", "muse-spark", "llama-4" })
		for (const std::string& m : vEnabled) {
			const std::string l = lowerAscii(m);
			if (l.find(szFamily) != std::string::npos && l.find("contributor") == std::string::npos) return m;
		}
	if (!bNeedVision && !vEnabled.empty()) return vEnabled.front();
	return std::string();
}

bool CBhLlamaProtocol::isModelError(const std::string& sError)
{
	const std::string e = lowerAscii(sError);
	return e.find("invalid model") != std::string::npos || e.find("not available for your application") != std::string::npos
	    || e.find("model_not_found") != std::string::npos || e.find("model not found") != std::string::npos
	    || e.find("model was not found") != std::string::npos
	    || e.find("unknown model") != std::string::npos;
}
