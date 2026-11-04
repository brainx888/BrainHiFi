#include "pch.h"
#include "CMetaAiClient.h"
#include "CBhSecureKeyStore.h"
#include "BhHttps.h"

namespace {

CString toCString(const std::string& sUtf8) { return CString(CA2W(sUtf8.c_str(), CP_UTF8)); }

void wipe(std::wstring& s)
{
	if (!s.empty()) SecureZeroMemory(&s[0], s.size() * sizeof(wchar_t));
	s.clear();
}

void traceLong(LPCTSTR szTag, const std::string& s)
{
	const CString w = toCString(s);
	for (int i = 0; i < w.GetLength(); i += 400) TRACE(_T("%s%s\n"), szTag, (LPCTSTR)w.Mid(i, 400));
}

}

CMetaAiClient::CMetaAiClient(LPCWSTR szHost) : m_sHost(szHost) {}

CMetaAiClient::~CMetaAiClient()
{
	if (!m_sKey.IsEmpty()) SecureZeroMemory(m_sKey.GetBuffer(), m_sKey.GetLength() * sizeof(wchar_t));
	m_sKey.ReleaseBuffer(0);
}
void CMetaAiClient::SetApiKey(const CStringW& sKey)
{
	m_sKey = CBhSecureKeyStore::normalizeLlamaKey(sKey);
	m_sKey.Trim();
	m_sKeySource = L"SetApiKey";
}

bool CMetaAiClient::ensureKey()
{
	if (!m_sKey.IsEmpty()) return true;

	m_sKey = CBhSecureKeyStore::load(
		KEY_ENV,
		KEY_FILE,
		&m_sKeySource);

	// Compatibility with older project configurations that used
	// MODEL_API_KEY instead of META_AI_API_KEY.
	if (m_sKey.IsEmpty()) {
		CStringW sLegacySource;
		m_sKey = CBhSecureKeyStore::load(
			L"MODEL_API_KEY",
			nullptr,
			&sLegacySource);

		if (!m_sKey.IsEmpty())
			m_sKeySource = L"MODEL_API_KEY environment";
	}

	m_sKey = CBhSecureKeyStore::normalizeLlamaKey(m_sKey);
	m_sKey.Trim();

	if (m_sKey.IsEmpty()) {
		m_lastError =
			_T("No Meta AI API key. Set META_AI_API_KEY, set the legacy ")
			_T("MODEL_API_KEY, or run tools\\StoreMetaAiKey.ps1.");
		return false;
	}

	return true;
}
//void CMetaAiClient::SetApiKey(const CStringW& sKey) { m_sKey = sKey; m_sKey.Trim(); m_sKeySource = L"SetApiKey"; }

//bool CMetaAiClient::ensureKey()
//{
//	if (m_sKey.IsEmpty()) m_sKey = CBhSecureKeyStore::load(KEY_ENV, KEY_FILE, &m_sKeySource);
//	if (m_sKey.IsEmpty()) {
//		m_lastError = _T("No Meta AI API key: set META_AI_API_KEY or run tools\\StoreMetaAiKey.ps1");
//		return false;
//	}
//	return true;
//}

bool CMetaAiClient::HasApiKey() { return ensureKey(); }

void CMetaAiClient::SetTimeouts(int nConnectMs, int nSendMs, int nReceiveMs)
{
	m_nConnectMs = nConnectMs; m_nSendMs = nSendMs; m_nReceiveMs = nReceiveMs;
}

bool CMetaAiClient::send(LPCWSTR szVerb, LPCWSTR szPath, const std::string& sBody, DWORD& nStatus, std::string& sResponse)
{
	nStatus = 0;
	sResponse.clear();
	if (!ensureKey()) return false;
	SBhHttpsCall c;
	c.sHost = (LPCWSTR)m_sHost;
	c.sVerb = szVerb;
	c.sPath = szPath;
	c.sBody = sBody;
	c.nConnectMs = m_nConnectMs; c.nSendMs = m_nSendMs; c.nReceiveMs = m_nReceiveMs;
	c.sHeaders = L"Content-Type: application/json\r\nAccept: application/json\r\nAuthorization: Bearer ";
	c.sHeaders += (LPCWSTR)m_sKey;
	c.sHeaders += L"\r\n";
	SBhHttpsResult r;
	const bool bSent = BhHttpsSend(c, r);
	wipe(c.sHeaders);
	if (!bSent) {
		m_lastError.Format(_T("Network error %s talking to %s%s"), BhWinHttpErrorText(r.nWinError).c_str(),
		                   (LPCTSTR)m_sHost, szPath);
		TRACE(_T("CMetaAiClient: %s\n"), (LPCTSTR)m_lastError);
		return false;
	}
	nStatus = r.nStatus;
	sResponse.swap(r.sBody);
	TRACE(_T("CMetaAiClient: %s %s -> HTTP %u, %u bytes\n"), szVerb, szPath, nStatus, (unsigned)sResponse.size());
	if (nStatus < 200 || nStatus >= 300) traceLong(_T("  body: "), CBhLlamaProtocol::redactKeys(sResponse.substr(0, 2000)));
	return true;
}

bool CMetaAiClient::Chat(const SBhLlamaRequest& req, SBhLlamaReply& reply)
{
	DWORD nStatus = 0;
	std::string sResp;
	const int nMax = CBhMetaAiProtocol::isSamModel(req.sModel) ? 0 : m_nMaxOutputTokens;
	if (!send(L"POST", L"/v1/responses", CBhMetaAiProtocol::buildResponsesRequest(req, m_sEffort, nMax), nStatus, sResp)) {
		reply = SBhLlamaReply();
		reply.sError = std::string(CT2A(m_lastError, CP_UTF8));
		return false;
	}
	reply = CBhMetaAiProtocol::parseResponsesReply((int)nStatus, sResp);
	if (reply.bOk && reply.sText.empty() && !CBhMetaAiProtocol::isSamModel(req.sModel)) {
		reply.bOk = false;
		reply.sError = "empty reply (status " + reply.sStopReason + ")";
	}
	if (!reply.bOk) m_lastError = _T("Meta AI: ") + toCString(reply.sError);
	return reply.bOk;
}

bool CMetaAiClient::Segment(const std::string& sPrompt, const SBhLlamaImage& im, std::vector<SBhSam3Box>& vBoxes,
                            std::string* pRawText)
{
	vBoxes.clear();
	SBhLlamaRequest q;
	q.sModel = m_sSamModel;
	q.sUser = sPrompt;
	q.vImages.push_back(im);
	SBhLlamaReply r;
	if (!Chat(q, r)) return false;
	if (pRawText) *pRawText = r.sText;
	CBhMetaAiProtocol::parseSam3(r.sText, vBoxes);
	return true;
}

bool CMetaAiClient::ListModels(std::vector<std::string>& vIds)
{
	vIds.clear();
	DWORD nStatus = 0;
	std::string sResp, sErr;
	if (!send(L"GET", L"/v1/models", std::string(), nStatus, sResp)) return false;
	if (nStatus < 200 || nStatus >= 300) {
		m_lastError = _T("Meta AI: ") + toCString(CBhMetaAiProtocol::parseResponsesReply((int)nStatus, sResp).sError);
		return false;
	}
	if (!CBhLlamaProtocol::parseModelList(sResp, vIds, &sErr)) {
		m_lastError = _T("Meta AI: model list: ") + toCString(sErr);
		return false;
	}
	return true;
}

CString CallMetaApiFromCpp(const CString& sPrompt, LPCSTR szModel, LPCSTR szEffort)
{
	CMetaAiClient client;
	client.SetReasoningEffort(szEffort ? szEffort : "");
	SBhLlamaRequest q;
	q.sModel = szModel;
	q.sUser = std::string(CT2A(sPrompt, CP_UTF8));
	SBhLlamaReply r;
	if (!client.Chat(q, r)) {
		TRACE(_T("CallMetaApiFromCpp failed: %s\n"), (LPCTSTR)client.m_lastError);
		return _T("Error: ") + client.m_lastError;
	}
	TRACE(_T("CallMetaApiFromCpp: %d input / %d output tokens, status %s\n"), r.nPromptTokens, r.nCompletionTokens,
	      (LPCTSTR)toCString(r.sStopReason));
	return toCString(r.sText);
}
