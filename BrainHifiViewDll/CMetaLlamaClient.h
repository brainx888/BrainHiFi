#pragma once
/*
 * CMetaLlamaClient - WinHTTP client for the Meta Llama API
 * (https://api.llama.com/v1/chat/completions, Bearer key).
 *
 * The key comes from CBhSecureKeyStore (META_LLAMA_API_KEY or the DPAPI
 * store) unless SetApiKey() is called; it is never written to logs or
 * error messages.  Calls are blocking: run them on a worker thread.
 */
#include "core/BhLlamaProtocol.h"
#include <string>
#include <vector>

class CMetaLlamaClient
{
public:
	CMetaLlamaClient(LPCWSTR szHost = L"api.llama.com", WORD nPort = 443, bool bHttps = true);
	~CMetaLlamaClient();

	void SetApiKey(const CStringW& sKey);      // optional override
	bool HasApiKey();
	void SetChatPath(LPCWSTR szPath) { m_sChatPath = szPath; }   // "/v1/chat/completions" or "/compat/v1/chat/completions"
	void SetTimeouts(int nResolveMs, int nConnectMs, int nSendMs, int nReceiveMs);

	bool Chat(const SBhLlamaRequest& req, SBhLlamaReply& reply);
	/// Models enabled for this API app (an empty list means none is enabled yet).
	bool ListModels(std::vector<std::string>& vIds);

	CString m_lastError;
	CStringW m_sKeySource;                     // "environment", "DPAPI store", "SetApiKey"

private:
	bool request(LPCWSTR szVerb, LPCWSTR szPath, const std::string& sBody, std::string& sResponse, DWORD& nStatus);
	bool ensureKey();

	CStringW m_sHost, m_sChatPath = L"/v1/chat/completions";
	WORD m_nPort;
	bool m_bHttps;
	CStringW m_sKey;
	int m_nResolveMs = 10000, m_nConnectMs = 15000, m_nSendMs = 60000, m_nReceiveMs = 180000;
};
