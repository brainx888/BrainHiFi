#include "BhMetaAiProtocol.h"
#include <cstdio>
#include <cstdlib>

namespace {

bool startsWithNoCase(const std::string& s, const char* szPrefix)
{
	for (size_t i = 0; szPrefix[i]; i++) {
		if (i >= s.size()) return false;
		char a = s[i], b = szPrefix[i];
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (a != b) return false;
	}
	return true;
}

// "x1=28;y1=38;w=128" -> value of key, or nDefault
int keyInt(const std::string& sFields, const char* szKey, int nDefault)
{
	const std::string k = std::string(szKey) + "=";
	size_t p = 0;
	while (p < sFields.size()) {
		const size_t e = sFields.find(';', p);
		const std::string f = sFields.substr(p, e == std::string::npos ? std::string::npos : e - p);
		if (f.compare(0, k.size(), k) == 0) return std::atoi(f.c_str() + k.size());
		if (e == std::string::npos) break;
		p = e + 1;
	}
	return nDefault;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// End of a "<|mask;...|>" block.  Mask data may itself contain "|>", so accept only a "|>"
// followed by the end, a line break, or ",<digits><|box".
size_t maskEnd(const std::string& s, size_t p)
{
	for (size_t e = s.find("|>", p); e != std::string::npos; e = s.find("|>", e + 1)) {
		const size_t a = e + 2;
		if (a >= s.size() || s[a] == '\n' || s[a] == '\r') return e;
		if (s[a] == ',') {
			size_t d = a + 1;
			while (d < s.size() && isDigit(s[d])) d++;
			if (d > a + 1 && s.compare(d, 6, "<|box;") == 0) return e;
		}
	}
	return std::string::npos;
}

}

bool CBhMetaAiProtocol::isSamModel(const std::string& sModel) { return startsWithNoCase(sModel, "sam"); }

std::string CBhMetaAiProtocol::buildResponsesRequest(const SBhLlamaRequest& req, const std::string& sEffort, int nMaxOutputTokens)
{
	const bool bSam = isSamModel(req.sModel);
	std::string s = "{\"model\":" + SBhJson::escape(req.sModel);
	if (!bSam && !req.sSystem.empty()) s += ",\"instructions\":" + SBhJson::escape(req.sSystem);
	if (!bSam && !sEffort.empty()) s += ",\"reasoning\":{\"effort\":" + SBhJson::escape(sEffort) + "}";
	if (!bSam && nMaxOutputTokens > 0) s += ",\"max_output_tokens\":" + std::to_string(nMaxOutputTokens);
	s += ",\"input\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":" + SBhJson::escape(req.sUser) + "}";
	for (const SBhLlamaImage& im : req.vImages) {
		if (!bSam && !im.sCaption.empty())
			s += ",{\"type\":\"input_text\",\"text\":" + SBhJson::escape(im.sCaption) + "}";
		s += ",{\"type\":\"input_image\",\"image_url\":" + SBhJson::escape("data:" + im.sMime + ";base64," + im.sBase64) + "}";
	}
	s += "]}]}";
	return s;
}

SBhLlamaReply CBhMetaAiProtocol::parseResponsesReply(int nHttpStatus, const std::string& sBody)
{
	SBhLlamaReply r;
	SBhJson j;
	std::string sErr;
	const bool bJson = SBhJson::parse(sBody, j, &sErr);
	const SBhJson* pErr = bJson ? j.find("error") : nullptr;
	std::string sApiErr;
	if (pErr && pErr->eType == SBhJson::Object) {
		const SBhJson* m = pErr->find("message");
		const SBhJson* c = pErr->find("code");
		sApiErr = (m ? m->str() : std::string()) + (c && !c->str().empty() ? " (" + c->str() + ")" : std::string());
	}
	else if (bJson && j.find("detail")) sApiErr = j.find("detail")->str();
	if (nHttpStatus < 200 || nHttpStatus >= 300) {
		r.sError = "HTTP " + std::to_string(nHttpStatus) + ": " + (sApiErr.empty() ? sBody.substr(0, 300) : sApiErr);
		if (nHttpStatus == 401) r.sError += " - check the Meta AI API key";
		r.sError = CBhLlamaProtocol::redactKeys(r.sError);
		return r;
	}
	if (!bJson) { r.sError = "unreadable response: " + sErr; return r; }
	if (!sApiErr.empty()) { r.sError = CBhLlamaProtocol::redactKeys(sApiErr); return r; }
	const std::string sStatus = j.find("status") ? j.find("status")->str() : std::string("completed");
	if (const SBhJson* out = j.find("output"))
		for (const SBhJson& o : out->a) {
			if (!o.find("type") || o.find("type")->str() != "message") continue;
			if (const SBhJson* c = o.find("content"))
				for (const SBhJson& part : c->a)
					if (part.find("type") && part.find("type")->str() == "output_text" && part.find("text"))
						r.sText += part.find("text")->str();
		}
	if (const SBhJson* u = j.find("usage")) {
		if (const SBhJson* v = u->find("input_tokens")) r.nPromptTokens = (int)v->num(-1);
		if (const SBhJson* v = u->find("output_tokens")) r.nCompletionTokens = (int)v->num(-1);
	}
	r.sStopReason = sStatus;
	if (sStatus == "incomplete") {
		const SBhJson* d = j.find("incomplete_details");
		const SBhJson* why = d ? d->find("reason") : nullptr;
		r.sStopReason += why ? " (" + why->str() + ")" : std::string();
	}
	else if (sStatus != "completed") { r.sError = "response status: " + sStatus; return r; }
	r.bOk = true;
	return r;
}

bool CBhMetaAiProtocol::parseSam3(const std::string& s, std::vector<SBhSam3Box>& v)
{
	v.clear();
	size_t p = 0;
	while ((p = s.find("<|box;", p)) != std::string::npos) {
		SBhSam3Box b;
		size_t q = p;
		while (q > 0 && isDigit(s[q - 1])) q--;
		if (q < p) b.iIndex = std::atoi(s.substr(q, p - q).c_str());
		const size_t e = s.find("|>", p);
		if (e == std::string::npos) break;
		const std::string f = s.substr(p + 6, e - p - 6);
		b.x1 = keyInt(f, "x1", 0); b.y1 = keyInt(f, "y1", 0);
		b.x2 = keyInt(f, "x2", 0); b.y2 = keyInt(f, "y2", 0);
		b.nImgW = keyInt(f, "w", 0); b.nImgH = keyInt(f, "h", 0);
		p = e + 2;
		if (s.compare(p, 7, "<|mask;") == 0) {
			const size_t me = maskEnd(s, p + 7);
			const std::string m = s.substr(p + 7, me == std::string::npos ? std::string::npos : me - p - 7);
			b.nMaskX = keyInt(m, "x", 0);
			b.nMaskY = keyInt(m, "y", 0);
			const size_t d = m.find("data=");
			if (d != std::string::npos) {
				const size_t c1 = m.find(',', d), c2 = c1 == std::string::npos ? c1 : m.find(',', c1 + 1);
				if (c2 != std::string::npos) {
					b.nMaskW = std::atoi(m.c_str() + d + 5);
					b.nMaskH = std::atoi(m.c_str() + c1 + 1);
					b.sMaskData = m.substr(c2 + 1);
				}
			}
			p = me == std::string::npos ? s.size() : me + 2;
		}
		if (b.x2 >= b.x1 && b.y2 >= b.y1) v.push_back(b);
	}
	return !v.empty();
}

std::string CBhMetaAiProtocol::formatSam3(const std::vector<SBhSam3Box>& v, const std::string& sPrompt)
{
	char buf[256];
	std::snprintf(buf, sizeof(buf), "SAM 3.1 found %d region(s) for \"%s\"%s\n", (int)v.size(), sPrompt.c_str(),
	              v.empty() ? "" : " (x right, y down, pixels; centre as fraction of image width/height):");
	std::string s = buf;
	for (const SBhSam3Box& b : v) {
		std::snprintf(buf, sizeof(buf), "  #%d  box x %d-%d, y %d-%d (%dx%d px of %dx%d), centre %.2f, %.2f%s\n",
		              b.iIndex + 1, b.x1, b.x2, b.y1, b.y2, b.width(), b.height(), b.nImgW, b.nImgH,
		              b.centreFracX(), b.centreFracY(), b.nMaskW > 0 ? ", mask returned" : "");
		s += buf;
	}
	return s;
}
