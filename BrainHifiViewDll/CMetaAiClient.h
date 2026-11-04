#pragma once
/*
 * CMetaAiClient - WinHTTP client for the Meta AI API (https://api.meta.ai).
 *   Chat()       POST /v1/responses   text or text + images (muse-wonder-1.0, muse-spark-1.3, ...)
 *   Segment()    POST /v1/responses   sam-3.1: noun-phrase prompt + image -> boxes/masks
 *   ListModels() GET  /v1/models
 * The key comes from META_AI_API_KEY or %APPDATA%\BrainHifi\MetaAi.key (DPAPI, written by
 * tools\StoreMetaAiKey.ps1) unless SetApiKey() is called.  It is never logged.
 * Calls block: run them on a worker thread (CBhMsiAiAnalyst does).
 */
#include "core/BhMetaAiProtocol.h"
#include <string>
#include <vector>

class CMetaAiClient
{
public:
	static constexpr LPCWSTR KEY_ENV = L"META_AI_API_KEY";
	static constexpr LPCWSTR KEY_FILE = L"MetaAi.key";

	explicit CMetaAiClient(LPCWSTR szHost = L"api.meta.ai");
	~CMetaAiClient();

	void SetApiKey(const CStringW& sKey);
	bool HasApiKey();
	void SetReasoningEffort(const std::string& s) { m_sEffort = s; }   // "low" | "medium" | "high" | "" (server default)
	void SetMaxOutputTokens(int n) { m_nMaxOutputTokens = n; }        // 0 = server default
	void SetTimeouts(int nConnectMs, int nSendMs, int nReceiveMs);

	bool Chat(const SBhLlamaRequest& req, SBhLlamaReply& reply);
	bool Segment(const std::string& sPrompt, const SBhLlamaImage& im, std::vector<SBhSam3Box>& vBoxes,
	             std::string* pRawText = nullptr);
	bool ListModels(std::vector<std::string>& vIds);

	std::string m_sSamModel = "sam-3.1";
	CString m_lastError;
	CStringW m_sKeySource;

private:
	bool ensureKey();
	bool send(LPCWSTR szVerb, LPCWSTR szPath, const std::string& sBody, DWORD& nStatus, std::string& sResponse);

	CStringW m_sHost, m_sKey;
	std::string m_sEffort = "high";
	int m_nMaxOutputTokens = 0;
	int m_nConnectMs = 15000, m_nSendMs = 60000, m_nReceiveMs = 300000;
};

/// Improved version of the hand-written test call: blocking (use a worker thread), key from
/// META_AI_API_KEY / DPAPI store, returns the model text or "Error: ..." (never the key).
CString CallMetaApiFromCpp(const CString& sPrompt, LPCSTR szModel = "muse-wonder-1.0", LPCSTR szEffort = "high");
