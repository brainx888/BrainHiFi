#include "pch.h"
#include "BhHttps.h"
#include <vector>

namespace {

struct CBhHInternet
{
	HINTERNET h = nullptr;
	explicit CBhHInternet(HINTERNET hIn) : h(hIn) {}
	~CBhHInternet() { if (h) WinHttpCloseHandle(h); }
	CBhHInternet(const CBhHInternet&) = delete;
	CBhHInternet& operator=(const CBhHInternet&) = delete;
	operator HINTERNET() const { return h; }
};

bool fail(SBhHttpsResult& res) { res.nWinError = GetLastError(); return false; }

}

bool BhHttpsSend(const SBhHttpsCall& call, SBhHttpsResult& res)
{
	res = SBhHttpsResult();
	CBhHInternet hSession(WinHttpOpen(L"BrainHifi-CppClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!hSession) return fail(res);
	if (!WinHttpSetTimeouts(hSession, call.nResolveMs, call.nConnectMs, call.nSendMs, call.nReceiveMs)) return fail(res);
	CBhHInternet hConnect(WinHttpConnect(hSession, call.sHost.c_str(), call.nPort, 0));
	if (!hConnect) return fail(res);
	CBhHInternet hRequest(WinHttpOpenRequest(hConnect, call.sVerb.c_str(), call.sPath.c_str(), nullptr, WINHTTP_NO_REFERER,
	                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if (!hRequest) return fail(res);
	if (!WinHttpSendRequest(hRequest, call.sHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : call.sHeaders.c_str(),
	                        call.sHeaders.empty() ? 0 : (DWORD)-1L,
	                        call.sBody.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)call.sBody.data(),
	                        (DWORD)call.sBody.size(), (DWORD)call.sBody.size(), 0)
	    || !WinHttpReceiveResponse(hRequest, nullptr))
		return fail(res);
	DWORD nSize = sizeof(res.nStatus);
	if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
	                         &res.nStatus, &nSize, WINHTTP_NO_HEADER_INDEX))
		return fail(res);
	std::vector<char> buf;
	for (;;) {
		DWORD nAvail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &nAvail)) return fail(res);
		if (nAvail == 0) break;
		buf.resize(nAvail);
		DWORD nRead = 0;
		if (!WinHttpReadData(hRequest, buf.data(), nAvail, &nRead)) return fail(res);
		if (nRead == 0) break;
		res.sBody.append(buf.data(), nRead);
	}
	return true;
}

std::wstring BhWinHttpErrorText(DWORD n)
{
	const wchar_t* sz = L"";
	switch (n) {
	case ERROR_WINHTTP_TIMEOUT: sz = L" (timeout)"; break;
	case ERROR_WINHTTP_NAME_NOT_RESOLVED: sz = L" (host name not found)"; break;
	case ERROR_WINHTTP_CANNOT_CONNECT: sz = L" (cannot connect)"; break;
	case ERROR_WINHTTP_CONNECTION_ERROR: sz = L" (connection reset)"; break;
	case ERROR_WINHTTP_SECURE_FAILURE: sz = L" (TLS/certificate error)"; break;
	}
	return std::to_wstring(n) + sz;
}
