#pragma once
/*
 * CBhSecureKeyStore - runtime API-key lookup, never compiled into the binary.
 *
 * load() order:
 *   1. environment variable (e.g. META_LLAMA_API_KEY)
 *   2. %APPDATA%\BrainHifi\<file> encrypted with Windows DPAPI for the
 *      current Windows user (CryptProtectData + fixed entropy), written by
 *      save() or by tools\StoreMetaLlamaKey.ps1
 * Keys are never logged; temporary buffers are wiped with SecureZeroMemory.
 */
#include <afxstr.h>

class CBhSecureKeyStore
{
public:
	static constexpr LPCWSTR LLAMA_ENV = L"META_LLAMA_API_KEY";
	static constexpr LPCWSTR LLAMA_FILE = L"MetaLlama.key";

	/// Empty string if not found. pSource receives "environment" / "DPAPI store" (never the key).
	static CStringW load(LPCWSTR szEnvVar, LPCWSTR szFileName, CStringW* pSource = nullptr);
	static bool save(LPCWSTR szFileName, const CStringW& sKey, CStringW* pError = nullptr);
	static bool remove(LPCWSTR szFileName);
	static CStringW filePath(LPCWSTR szFileName);

	static CStringW loadLlamaKey(CStringW* pSource = nullptr) { return normalizeLlamaKey(load(LLAMA_ENV, LLAMA_FILE, pSource)); }
	static bool saveLlamaKey(const CStringW& sKey, CStringW* pError = nullptr) { return save(LLAMA_FILE, normalizeLlamaKey(sKey), pError); }

	/// Llama API keys are "LLM|<app id>|<secret>"; a copy written as
	/// "LLM_<app id>_<secret>" is converted (only the first two '_').
	static CStringW normalizeLlamaKey(const CStringW& sKey);
};
