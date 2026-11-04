// CMetaLlamaAPI.cpp : implementation file
//

#include "pch.h"
#include "CMetaLlamaAPI.h"


//#include "pch.h"
//#include "MetaVision.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <gdiplus.h>
#include <vector>
#include <string>


#include <windows.h>
#include <format>
#include <memory>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "gdiplus.lib")

static CString g_error;
static void SetError(LPCWSTR s) { g_error = s; }

static std::string W2Utf8(LPCWSTR w) {
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, nullptr, nullptr);
    return s;
}
static std::wstring Utf82W(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}
static std::string JsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 100);
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

// Convert any image (bmp/png/jpg) to PNG bytes using GDI+ (no OpenCV)
static BOOL LoadAsPng(LPCWSTR path, std::vector<BYTE>& out) {



    ULONG_PTR token;
    Gdiplus::GdiplusStartupInput in;
    //Gdiplus::GdiplusStartup(&token, &in, nullptr);
    if (Gdiplus::GdiplusStartup(&token, &in, nullptr) != Gdiplus::Ok)
        return FALSE;

    BOOL ok = FALSE;
    try{ 
     Gdiplus::Bitmap bmp(path);
   if (bmp.GetLastStatus() == Gdiplus::Ok) {
        // Find PNG encoder
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        std::vector<BYTE> buf(size);
        auto* codecs = (Gdiplus::ImageCodecInfo*)buf.data();
        Gdiplus::GetImageEncoders(num, size, codecs);
        CLSID pngClsid = {};
        for (UINT i = 0; i < num; i++)
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0) pngClsid = codecs[i].Clsid;

        IStream* stream = nullptr;
        CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        if (bmp.Save(stream, &pngClsid) == Gdiplus::Ok) {
            HGLOBAL hg = nullptr; GetHGlobalFromStream(stream, &hg);
            SIZE_T len = GlobalSize(hg); BYTE* p = (BYTE*)GlobalLock(hg);
            out.assign(p, p + len); GlobalUnlock(hg); ok = TRUE;
        }
        if (stream) stream->Release();
    }
	}
	catch (...) { 
        ok = FALSE;
    }

    Gdiplus::GdiplusShutdown(token);
    return ok;
}

static std::string Base64(const std::vector<BYTE>& data) {
    DWORD len = 0;
    CryptBinaryToStringA(data.data(), (DWORD)data.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
    std::string s(len, 0);
    CryptBinaryToStringA(data.data(), (DWORD)data.size(),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &s[0], &len);
    s.resize(len > 0 ? len - 1 : 0); // remove null
    return s;
}

// Simple parser: find "content":"..."
static std::wstring ExtractContent(const std::string& json) {
    size_t p = json.find("\"content\"");
    if (p == std::string::npos) return Utf82W(json); // return full error json
    p = json.find('"', json.find(':', p) + 1);
    if (p == std::string::npos) return L"";

    std::string out;
    for (size_t i = p + 1; i < json.size(); i++) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            i++; if (json[i] == 'n') out += '\n'; else if (json[i] == 't') out += '\t'; else out += json[i];
        }
        else if (json[i] == '"') break;
        else out += json[i];
    }
    return Utf82W(out);
}

static std::string HttpsPost(LPCWSTR host, LPCWSTR path, const std::string& json, LPCWSTR apiKey) {
    std::string response;
    HINTERNET hSession = WinHttpOpen(L"MFC-Meta/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) { SetError(L"WinHttpOpen failed"); return ""; }
    WinHttpSetTimeouts(hSession, 30000, 30000, 120000, 120000);

    HINTERNET hConn = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hReq = hConn ? WinHttpOpenRequest(hConn, L"POST", path, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE) : nullptr;

    if (hReq) {
        CString hdr; hdr.Format(L"Authorization: Bearer %s\r\nContent-Type: application/json\r\n", apiKey);
        WinHttpAddRequestHeaders(hReq, hdr, -1, WINHTTP_ADDREQ_FLAG_ADD);
        BOOL ok = WinHttpSendRequest(hReq, nullptr, 0, (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0)
            && WinHttpReceiveResponse(hReq, nullptr);
        if (ok) {
            DWORD avail = 0;
            do {
                WinHttpQueryDataAvailable(hReq, &avail);
                if (!avail) break;
                std::vector<char> b(avail);
                DWORD read = 0; WinHttpReadData(hReq, b.data(), avail, &read);
                response.append(b.data(), read);
            } while (avail > 0);
        }
        else SetError(L"HTTPS request failed. Check internet/API key.");
    }
    if (hReq) WinHttpCloseHandle(hReq);
    if (hConn) WinHttpCloseHandle(hConn);
    if (hSession) WinHttpCloseHandle(hSession);
    return response;
}

extern "C" {

     BOOL  Meta_AnalyzeImage(LPCWSTR imagePath, LPCWSTR prompt, LPCWSTR apiKey, LPWSTR result, int resultChars) {
        std::vector<BYTE> png;
        if (!LoadAsPng(imagePath, png)) { SetError(L"Cannot load image. Use PNG/JPG."); return FALSE; }

        std::string b64 = Base64(png);
        std::string pUtf8 = JsonEscape(W2Utf8(prompt));

        // OpenAI-compatible JSON for Meta Llama API
        std::string json = "{\"model\":\"Llama-4-Maverick-17B-128E-Instruct-FP8\","
            "\"messages\":[{\"role\":\"user\",\"content\":["
            "{\"type\":\"text\",\"text\":\"" + pUtf8 + "\"},"
            "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64," + b64 + "\"}}"
            "]}]}";

        std::string resp = HttpsPost(L"api.llama.com", L"/v1/chat/completions", json, apiKey);
        if (resp.empty()) return FALSE;

        std::wstring answer = ExtractContent(resp);
        wcsncpy_s(result, resultChars, answer.c_str(), _TRUNCATE);
        return TRUE;
    }

     void  Meta_GetLastError(LPWSTR buffer, int bufferChars) {
        wcsncpy_s(buffer, bufferChars, g_error, _TRUNCATE);
    }

} // extern "C"

CString CallMetaApiFromCpp1(const std::wstring apiKey)
{
    // 1. API endpoint configuration derived from your PowerShell Uri
    constexpr std::wstring_view server = L"api.meta.ai";
    constexpr std::wstring_view path = L"/v1/responses";

    // 2. Authorization Token and JSON Payload matching your working curl/PowerShell request
    // (In production, you can replace this hardcoded string with GetWinEnvVar(L"MODEL_API_KEY"))
   

    std::string jsonPayload = R"({
      "model": "muse-wonder-1.0",
      "reasoning": {
        "effort": "high"
      },
      "input": "Plan the upgrade of a Flask app from SQLAlchemy 1.4 to 2.0: list the breaking changes to check and the order to make them in."
    })";

    // 3. Initialize WinHTTP session
    HINTERNET hSession = WinHttpOpen(
        L"BrainHifi-CppClient/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0
    );
    if (!hSession) return CString(L"Error: WinHttpOpen failed.");

    // 4. Connect to server over HTTPS (Port 443)
    HINTERNET hConnect = WinHttpConnect(hSession, server.data(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return CString(L"Error: WinHttpConnect failed.");
    }

    // 5. Open POST request with WINHTTP_FLAG_SECURE for TLS encryption
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", path.data(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return CString(L"Error: WinHttpOpenRequest failed.");
    }

    // 6. Format headers (Content-Type and Bearer Token)
    std::wstring headers = std::format(
        L"Content-Type: application/json\r\nAuthorization: Bearer {}\r\n",
        apiKey
    );

    // 7. Send request with the JSON payload body
    BOOL bResults = WinHttpSendRequest(
        hRequest,
        headers.c_str(), static_cast<DWORD>(-1L),
        const_cast<char*>(jsonPayload.data()), static_cast<DWORD>(jsonPayload.size()),
        static_cast<DWORD>(jsonPayload.size()), 0
    );

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    std::string responseBody;
    if (bResults) {
        // Query HTTP Status Code
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &dwStatusCode, &dwSize,
            WINHTTP_NO_HEADER_INDEX
        );

        // Read incoming response data chunks
        DWORD dwDownloaded = 0;
        do {
            DWORD dwSizeAvailable = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSizeAvailable) || dwSizeAvailable == 0) break;

            auto buffer = std::make_unique<char[]>(dwSizeAvailable + 1);
            std::memset(buffer.get(), 0, dwSizeAvailable + 1);

            if (WinHttpReadData(hRequest, buffer.get(), dwSizeAvailable, &dwDownloaded)) {
                responseBody.append(buffer.get(), dwDownloaded);
            }
        } while (dwDownloaded > 0);

        if (dwStatusCode != 200) {
            responseBody = std::format("HTTP Error Status {}:\n{}", dwStatusCode, responseBody);
        }
    }
    else {
        responseBody = "Error: WinHttpReceiveResponse failed.";
    }

    // 8. Cleanup handles
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

	CString s = CString(responseBody.c_str());
    return s;
}



namespace MetaApi
{
    constexpr const wchar_t* kServer = L"api.meta.ai";
    constexpr const wchar_t* kPath = L"/v1/responses";
    constexpr const wchar_t* kModel = L"muse-wonder-1.0";

    // ---- RAII wrapper for HINTERNET -------------------------------------
    class WinHttpHandle
    {
    public:
        WinHttpHandle() = default;
        explicit WinHttpHandle(HINTERNET h) : m_h(h) {}
        ~WinHttpHandle() { if (m_h) WinHttpCloseHandle(m_h); }
        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;
        explicit operator bool() const { return m_h != nullptr; }
        operator HINTERNET() const { return m_h; }
    private:
        HINTERNET m_h = nullptr;
    };

    // ---- helpers ---------------------------------------------------------
    inline CString Fail(const wchar_t* what)
    {
        CString s;
        s.Format(L"Error: %s (Win32 error %lu)", what, ::GetLastError());
        return s;
    }

    inline std::string Api_WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
            nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
            &out[0], n, nullptr, nullptr);
        return out;
    }

    inline CString Api_Utf8ToCString(const std::string& s)
    {
        CString out;
        if (s.empty()) return out;
        int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        wchar_t* buf = out.GetBuffer(n);
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), buf, n);
        out.ReleaseBuffer(n);
        return out;
    }

    inline std::string Api_JsonEscape(const std::string& s)
    {
        std::string o;
        o.reserve(s.size() + 16);
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
                else { o += static_cast<char>(c); }
            }
        }
        return o;
    }

    inline std::wstring Api_GetEnvVar(const wchar_t* name)
    {
        DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
        if (n == 0) return std::wstring();
        std::wstring v(n, L'\0');
        DWORD got = ::GetEnvironmentVariableW(name, &v[0], n);
        v.resize(got);
        return v;
    }
    inline std::string Api_BuildPayloadMini(const wchar_t* model,
        const wchar_t* effort,
        const CString& prompt,
        int maxOutputTokens)
    {
        std::string p;
        p += "{\"model\":\"";
        p += Api_JsonEscape(Api_WideToUtf8(model));
        p += "\",\"reasoning\":{\"effort\":\"";
        p += Api_JsonEscape(Api_WideToUtf8(effort));
        p += "\"},\"max_output_tokens\":";
        p += std::to_string(maxOutputTokens);
        p += ",\"input\":\"";
        p += Api_JsonEscape(Api_WideToUtf8(std::wstring(prompt.GetString())));
        p += "\"}";
        return p;
    }

    // Build the request body step by step (no long '+' chain)
    inline std::string Api_BuildPayload(const wchar_t* model,
        const wchar_t* effort,
        const CString& prompt)
    {
        std::string p;
        p += "{\"model\":\"";
        p += Api_JsonEscape(Api_WideToUtf8(model));
        p += "\",\"reasoning\":{\"effort\":\"";
        p += Api_JsonEscape(Api_WideToUtf8(effort));
        p += "\"},\"input\":\"";
        p += Api_JsonEscape(Api_WideToUtf8(std::wstring(prompt.GetString())));
        p += "\"}";
        return p;
    }
}

// Returns the raw JSON response (UTF-16), or a string starting with "Error:" / "HTTP Error".
CString CallMetaApiFromCpp(const CString& prompt, const wchar_t* effort, int maxOutputTokens)
{
    using namespace MetaApi;

    // 1. API key from environment (never hardcode)
    const std::wstring apiKey = L"LLM_1711108750729050_weOrEdaYerP86snI99O1jY529bo"; //MetaApi::Api_GetEnvVar(L"MODEL_API_KEY");
    if (apiKey.empty())
        return CString(L"Error: environment variable MODEL_API_KEY is not set.");

    // 2. Build JSON payload with proper escaping
    const std::string payload = MetaApi::Api_BuildPayload(MetaApi::kModel, effort, prompt);

    // 3. Session
    WinHttpHandle hSession(WinHttpOpen(
        L"MIDEE-CppClient/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,   // Win 8.1+; use DEFAULT_PROXY for older
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) return Fail(L"WinHttpOpen failed");

    // resolve, connect, send, receive (ms). High-effort reasoning can be slow.
    WinHttpSetTimeouts(hSession, 10'000, 15'000, 30'000, 300'000);

    // 4. Connect
    WinHttpHandle hConnect(WinHttpConnect(hSession, kServer, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConnect) return Fail(L"WinHttpConnect failed");

    // 5. Request
    WinHttpHandle hRequest(WinHttpOpenRequest(
        hConnect, L"POST", kPath, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!hRequest) return Fail(L"WinHttpOpenRequest failed");

    // 6. Headers
    const std::wstring headers =
        L"Content-Type: application/json\r\n"
        L"Accept: application/json\r\n"
        L"Authorization: Bearer " + apiKey + L"\r\n";

    // 7. Send
    const DWORD bodyLen = static_cast<DWORD>(payload.size());
    if (!WinHttpSendRequest(hRequest,
        headers.c_str(), (DWORD)headers.size(),
        const_cast<char*>(payload.data()), bodyLen, bodyLen, 0))
        return Fail(L"WinHttpSendRequest failed");

    if (!WinHttpReceiveResponse(hRequest, nullptr))
        return Fail(L"WinHttpReceiveResponse failed");

    // 8. Status code
    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        return Fail(L"WinHttpQueryHeaders failed");

    // 9. Read body
    std::string body;
    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail))
            return Fail(L"WinHttpQueryDataAvailable failed");
        if (avail == 0) break;

        const size_t old = body.size();
        body.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, body.data() + old, avail, &read))
            return Fail(L"WinHttpReadData failed");
        body.resize(old + read);
    }

    // 10. Convert UTF-8 -> UTF-16
    CString result = MetaApi::Api_Utf8ToCString(body);
    if (status != 200)
    {
        CString err;
        err.Format(L"HTTP Error %lu:\r\n", status);
        return err + result;
    }
    return result;
}

CMetaLlamaAPI::CMetaLlamaAPI()
{
}

CMetaLlamaAPI::~CMetaLlamaAPI()
{
}


// CMetaLlamaAPI member functions
