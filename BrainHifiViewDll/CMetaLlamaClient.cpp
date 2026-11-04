#include "pch.h"
#include "CMetaLlamaClient.h"
#include "CBhSecureKeyStore.h"
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace {

struct CHInternet
{
	HINTERNET h = nullptr;
	explicit CHInternet(HINTERNET hIn = nullptr) : h(hIn) {}
	~CHInternet() { if (h) WinHttpCloseHandle(h); }
	CHInternet(const CHInternet&) = delete;
	CHInternet& operator=(const CHInternet&) = delete;
	operator HINTERNET() const { return h; }
};

CString toCString(const std::string& sUtf8)
{
	return CString(CA2W(sUtf8.c_str(), CP_UTF8));
}

}

CMetaLlamaClient::CMetaLlamaClient(LPCWSTR szHost, WORD nPort, bool bHttps)
	: m_sHost(szHost), m_nPort(nPort), m_bHttps(bHttps)
{
}

CMetaLlamaClient::~CMetaLlamaClient()
{
	if (!m_sKey.IsEmpty()) SecureZeroMemory(m_sKey.GetBuffer(), m_sKey.GetLength() * sizeof(wchar_t));
	m_sKey.ReleaseBuffer(0);
}

void CMetaLlamaClient::SetApiKey(const CStringW& sKey)
{
	m_sKey = CBhSecureKeyStore::normalizeLlamaKey(sKey);
	m_sKeySource = L"SetApiKey";
}

bool CMetaLlamaClient::HasApiKey() { return ensureKey(); }

void CMetaLlamaClient::SetTimeouts(int nResolveMs, int nConnectMs, int nSendMs, int nReceiveMs)
{
	m_nResolveMs = nResolveMs; m_nConnectMs = nConnectMs; m_nSendMs = nSendMs; m_nReceiveMs = nReceiveMs;
}

bool CMetaLlamaClient::ensureKey()
{
	if (m_sKey.IsEmpty()) m_sKey = CBhSecureKeyStore::loadLlamaKey(&m_sKeySource);
	if (m_sKey.IsEmpty()) {
		m_lastError = _T("No Meta Llama API key. Set the META_LLAMA_API_KEY environment variable or run ")
		              _T("tools\\StoreMetaLlamaKey.ps1 once to store it encrypted (DPAPI) for this Windows user.");
		return false;
	}
	return true;
}

bool CMetaLlamaClient::request(LPCWSTR szVerb, LPCWSTR szPath, const std::string& sBody,
                               std::string& sResponse, DWORD& nStatus)
{
	sResponse.clear();
	nStatus = 0;
	if (!ensureKey()) return false;
	CHInternet hSession(WinHttpOpen(L"BrainHifi-MetaLlama/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!hSession) { m_lastError.Format(_T("WinHttpOpen failed (%lu)"), GetLastError()); return false; }
	WinHttpSetTimeouts(hSession, m_nResolveMs, m_nConnectMs, m_nSendMs, m_nReceiveMs);
	CHInternet hConnect(WinHttpConnect(hSession, m_sHost, m_nPort, 0));
	if (!hConnect) { m_lastError.Format(_T("WinHttpConnect failed (%lu)"), GetLastError()); return false; }
	CHInternet hRequest(WinHttpOpenRequest(hConnect, szVerb, szPath, nullptr, WINHTTP_NO_REFERER,
	                                       WINHTTP_DEFAULT_ACCEPT_TYPES, m_bHttps ? WINHTTP_FLAG_SECURE : 0));
	if (!hRequest) { m_lastError.Format(_T("WinHttpOpenRequest failed (%lu)"), GetLastError()); return false; }

	CStringW sHeaders = L"Content-Type: application/json\r\nAccept: application/json\r\nAuthorization: Bearer ";
	sHeaders += m_sKey;
	sHeaders += L"\r\n";
	const BOOL bAdded = WinHttpAddRequestHeaders(hRequest, sHeaders, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
	SecureZeroMemory(sHeaders.GetBuffer(), sHeaders.GetLength() * sizeof(wchar_t));
	sHeaders.ReleaseBuffer(0);
	if (!bAdded) { m_lastError = _T("WinHttpAddRequestHeaders failed"); return false; }

	LPVOID pBody = sBody.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)sBody.data();
	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, pBody, (DWORD)sBody.size(), (DWORD)sBody.size(), 0) ||
	    !WinHttpReceiveResponse(hRequest, nullptr)) {
		m_lastError.Format(_T("Network error %lu talking to %s"), GetLastError(), (LPCWSTR)m_sHost);
		return false;
	}
	DWORD nSize = sizeof(nStatus);
	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
	                    &nStatus, &nSize, WINHTTP_NO_HEADER_INDEX);
	for (;;) {
		DWORD nAvail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &nAvail) || nAvail == 0) break;
		const size_t nOld = sResponse.size();
		sResponse.resize(nOld + nAvail);
		DWORD nRead = 0;
		if (!WinHttpReadData(hRequest, &sResponse[nOld], nAvail, &nRead)) { sResponse.resize(nOld); break; }
		sResponse.resize(nOld + nRead);
	}
	return true;
}

bool CMetaLlamaClient::Chat(const SBhLlamaRequest& req, SBhLlamaReply& reply)
{
	reply = SBhLlamaReply();
	m_lastError.Empty();
	std::string sResp;
	DWORD nStatus = 0;
	SBhLlamaRequest q = req;
	q.bCompat = m_sChatPath.Find(L"/compat/") >= 0;
	if (!request(L"POST", m_sChatPath, CBhLlamaProtocol::buildChatRequest(q), sResp, nStatus)) {
		reply.sError = CW2A(m_lastError, CP_UTF8);
		return false;
	}
	reply = CBhLlamaProtocol::parseChatResponse((int)nStatus, sResp);
	if (!reply.bOk) m_lastError = toCString(reply.sError);
	return reply.bOk;
}

bool CMetaLlamaClient::ListModels(std::vector<std::string>& vIds)
{
	vIds.clear();
	m_lastError.Empty();
	std::string sResp;
	DWORD nStatus = 0;
	if (!request(L"GET", L"/v1/models", std::string(), sResp, nStatus)) return false;
	if (nStatus != 200) {
		m_lastError = toCString(CBhLlamaProtocol::parseChatResponse((int)nStatus, sResp).sError);
		return false;
	}
	std::string sErr;
	if (!CBhLlamaProtocol::parseModelList(sResp, vIds, &sErr)) { m_lastError = toCString(sErr); return false; }
	return true;
}
