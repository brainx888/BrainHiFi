#include "pch.h"
#include "CBhSecureKeyStore.h"
#include <wincrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

const char g_szEntropy[] = "BrainHifi.MetaLlama.v1";

DATA_BLOB entropyBlob()
{
	DATA_BLOB b;
	b.pbData = (BYTE*)g_szEntropy;
	b.cbData = (DWORD)(sizeof(g_szEntropy) - 1);
	return b;
}

CStringW fromUtf8(const std::vector<BYTE>& v)
{
	if (v.empty()) return CStringW();
	const int n = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)v.data(), (int)v.size(), nullptr, 0);
	CStringW s;
	if (n > 0) {
		MultiByteToWideChar(CP_UTF8, 0, (LPCCH)v.data(), (int)v.size(), s.GetBuffer(n), n);
		s.ReleaseBuffer(n);
	}
	return s;
}

}

CStringW CBhSecureKeyStore::filePath(LPCWSTR szFileName)
{
	PWSTR pszAppData = nullptr;
	CStringW s;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &pszAppData))) {
		s = pszAppData;
		s += L"\\BrainHifi\\";
		s += szFileName;
	}
	CoTaskMemFree(pszAppData);
	return s;
}

CStringW CBhSecureKeyStore::load(LPCWSTR szEnvVar, LPCWSTR szFileName, CStringW* pSource)
{
	if (pSource) pSource->Empty();
	if (szEnvVar && *szEnvVar) {
		wchar_t* pEnv = nullptr;
		size_t n = 0;
		if (_wdupenv_s(&pEnv, &n, szEnvVar) == 0 && pEnv) {
			CStringW s(pEnv);
			SecureZeroMemory(pEnv, n * sizeof(wchar_t));
			free(pEnv);
			s.Trim();
			if (!s.IsEmpty()) { if (pSource) *pSource = L"environment"; return s; }
		}
	}
	const CStringW sPath = filePath(szFileName);
	if (sPath.IsEmpty()) return CStringW();
	HANDLE h = CreateFileW(sPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return CStringW();
	std::vector<BYTE> vEnc;
	LARGE_INTEGER nSize{};
	if (GetFileSizeEx(h, &nSize) && nSize.QuadPart > 0 && nSize.QuadPart < 65536) {
		vEnc.resize((size_t)nSize.QuadPart);
		DWORD nRead = 0;
		if (!ReadFile(h, vEnc.data(), (DWORD)vEnc.size(), &nRead, nullptr) || nRead != vEnc.size()) vEnc.clear();
	}
	CloseHandle(h);
	if (vEnc.empty()) return CStringW();

	DATA_BLOB in{ (DWORD)vEnc.size(), vEnc.data() }, out{}, ent = entropyBlob();
	CStringW s;
	if (CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		std::vector<BYTE> vPlain(out.pbData, out.pbData + out.cbData);
		SecureZeroMemory(out.pbData, out.cbData);
		LocalFree(out.pbData);
		s = fromUtf8(vPlain);
		SecureZeroMemory(vPlain.data(), vPlain.size());
		s.Trim();
		if (!s.IsEmpty() && pSource) *pSource = L"DPAPI store";
	}
	return s;
}

bool CBhSecureKeyStore::save(LPCWSTR szFileName, const CStringW& sKey, CStringW* pError)
{
	auto fail = [&](LPCWSTR sz) { if (pError) pError->Format(L"%s (error %lu)", sz, GetLastError()); return false; };
	if (sKey.IsEmpty()) return fail(L"empty key");
	const CStringW sPath = filePath(szFileName);
	if (sPath.IsEmpty()) return fail(L"no AppData folder");
	const CStringW sDir = sPath.Left(sPath.ReverseFind(L'\\'));
	SHCreateDirectoryExW(nullptr, sDir, nullptr);

	const int n = WideCharToMultiByte(CP_UTF8, 0, sKey, sKey.GetLength(), nullptr, 0, nullptr, nullptr);
	std::vector<BYTE> vPlain((size_t)(std::max)(n, 0));
	WideCharToMultiByte(CP_UTF8, 0, sKey, sKey.GetLength(), (LPSTR)vPlain.data(), n, nullptr, nullptr);
	DATA_BLOB in{ (DWORD)vPlain.size(), vPlain.data() }, out{}, ent = entropyBlob();
	const BOOL bEnc = CryptProtectData(&in, L"BrainHifi API key", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
	SecureZeroMemory(vPlain.data(), vPlain.size());
	if (!bEnc) return fail(L"CryptProtectData failed");

	HANDLE h = CreateFileW(sPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	bool bOk = false;
	if (h != INVALID_HANDLE_VALUE) {
		DWORD nWritten = 0;
		bOk = WriteFile(h, out.pbData, out.cbData, &nWritten, nullptr) && nWritten == out.cbData;
		CloseHandle(h);
	}
	LocalFree(out.pbData);
	return bOk ? true : fail(L"cannot write key file");
}

bool CBhSecureKeyStore::remove(LPCWSTR szFileName)
{
	const CStringW sPath = filePath(szFileName);
	return !sPath.IsEmpty() && DeleteFileW(sPath);
}

CStringW CBhSecureKeyStore::normalizeLlamaKey(const CStringW& sKey)
{
	CStringW s = sKey;
	s.Trim();
	if (s.Left(4) == L"LLM_") {
		s.SetAt(3, L'|');
		const int i = s.Find(L'_', 4);
		if (i > 4) s.SetAt(i, L'|');
	}
	return s;
}
