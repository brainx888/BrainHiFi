#include "pch.h"
#include "CMetaSAM3Client.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <climits>
#include <wrl/client.h>
#include <shlwapi.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // ---------- RAII for HINTERNET ----------
    struct WinHttpHandle
    {
        HINTERNET h = nullptr;
        WinHttpHandle() = default;
        explicit WinHttpHandle(HINTERNET x) : h(x) {}
        ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;
        explicit operator bool() const { return h != nullptr; }
    };

    // ---------- RAII for COM on this thread (WIC needs it) ----------
    struct ComInit
    {
        HRESULT hr;
        ComInit() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
        ComInit(const ComInit&) = delete;
        ComInit& operator=(const ComInit&) = delete;
    };

    // ---------- minimal JSON DOM ----------
    struct J
    {
        enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
        double n = 0;
        std::string s;
        std::vector<J> a;
        std::vector<std::pair<std::string, J>> o;

        const J* find(const char* k) const
        {
            for (const auto& kv : o) if (kv.first == k) return &kv.second;
            return nullptr;
        }
        const J& req(const char* k) const
        {
            const J* v = find(k);
            if (!v) throw std::runtime_error(std::string("missing JSON field: ") + k);
            return *v;
        }
    };

    class JParser
    {
    public:
        explicit JParser(const std::string& s) : d(s) {}
        J parse() { J v = value(); return v; }

    private:
        const std::string& d;
        size_t p = 0;

        void ws() { while (p < d.size() && isspace((unsigned char)d[p])) ++p; }

        J value()
        {
            ws();
            J v;
            const char c = d.at(p);
            if (c == '{')
            {
                v.t = J::Obj; ++p; ws();
                if (d.at(p) == '}') { ++p; return v; }
                for (;;)
                {
                    ws();
                    std::string k = str();
                    ws();
                    if (d.at(p++) != ':') throw std::runtime_error("JSON: expected ':'");
                    v.o.emplace_back(std::move(k), value());
                    ws();
                    const char s = d.at(p++);
                    if (s == ',') continue;
                    if (s == '}') break;
                    throw std::runtime_error("JSON: expected ',' or '}'");
                }
            }
            else if (c == '[')
            {
                v.t = J::Arr; ++p; ws();
                if (d.at(p) == ']') { ++p; return v; }
                for (;;)
                {
                    v.a.push_back(value());
                    ws();
                    const char s = d.at(p++);
                    if (s == ',') continue;
                    if (s == ']') break;
                    throw std::runtime_error("JSON: expected ',' or ']'");
                }
            }
            else if (c == '"') { v.t = J::Str; v.s = str(); }
            else if (!d.compare(p, 4, "true")) { v.t = J::Bool; v.n = 1; p += 4; }
            else if (!d.compare(p, 5, "false")) { v.t = J::Bool; p += 5; }
            else if (!d.compare(p, 4, "null")) { p += 4; }
            else
            {
                char* e = nullptr;
                v.t = J::Num;
                v.n = strtod(d.c_str() + p, &e);
                if (e == d.c_str() + p) throw std::runtime_error("JSON: bad number");
                p = (size_t)(e - d.c_str());
            }
            return v;
        }

        std::string str()
        {
            if (d.at(p) != '"') throw std::runtime_error("JSON: expected string");
            std::string r; ++p;
            while (d.at(p) != '"')
            {
                if (d[p] == '\\')
                {
                    ++p;
                    const char e = d.at(p);
                    if (e == 'u') { p += 4; r += '?'; }      // not needed for our payload
                    else if (e == 'n') r += '\n';
                    else if (e == 't') r += '\t';
                    else r += e;
                    ++p;
                }
                else r += d[p++];
            }
            ++p;
            return r;
        }
    };

    // ---------- helpers ----------
    bool Base64Encode(const std::vector<BYTE>& in, std::string& out)
    {
        const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
        DWORD n = 0;
        if (!CryptBinaryToStringA(in.data(), (DWORD)in.size(), flags, nullptr, &n)) return false;
        out.assign(n, '\0');
        if (!CryptBinaryToStringA(in.data(), (DWORD)in.size(), flags, &out[0], &n)) return false;
        out.resize(n);
        while (!out.empty() && out.back() == '\0') out.pop_back();
        return true;
    }

    std::string JsonEscape(const std::string& s)
    {
        std::string r;
        r.reserve(s.size() + 8);
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); r += b; }
                else r += (char)c;
            }
        }
        return r;
    }

    DWORD ProxyAccessType(const CStringW& host)
    {
        CStringW h(host); h.MakeLower();
        if (h == L"localhost" || h == L"127.0.0.1" || h == L"::1")
            return WINHTTP_ACCESS_TYPE_NO_PROXY;
        return WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    }

    bool ReadAllBytes(HINTERNET req, std::vector<BYTE>& out, size_t cap = (256u << 20))
    {
        out.clear();
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) return false;
            if (avail == 0) break;
            if (out.size() + avail > cap) { ::SetLastError(ERROR_INSUFFICIENT_BUFFER); return false; }
            const size_t old = out.size();
            out.resize(old + avail);
            DWORD got = 0;
            if (!WinHttpReadData(req, &out[old], avail, &got)) { out.resize(old); return false; }
            out.resize(old + got);
            if (got == 0) break;
        }
        return true;
    }

    const char* SniffMime(const std::vector<BYTE>& b)
    {
        if (b.size() >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return "image/jpeg";
        if (b.size() >= 8 && !memcmp(b.data(), "\x89PNG\r\n\x1A\n", 8)) return "image/png";
        if (b.size() >= 12 && !memcmp(b.data(), "RIFF", 4) && !memcmp(b.data() + 8, "WEBP", 4))
            return "image/webp";
        if (b.size() >= 6 && (!memcmp(b.data(), "GIF87a", 6) || !memcmp(b.data(), "GIF89a", 6)))
            return "image/gif";
        if (b.size() >= 2 && b[0] == 'B' && b[1] == 'M') return "image/bmp";
        return nullptr;
    }

    // Helper to create an IStream from a memory buffer for WIC decoding
    HRESULT CreateStreamFromBytes(const std::vector<BYTE>& bytes, ComPtr<IStream>& stream)
    {
        if (bytes.empty() || bytes.size() > UINT_MAX) return E_INVALIDARG;
        IStream* pStream = SHCreateMemStream(bytes.data(), (UINT)bytes.size());
        if (!pStream) return E_OUTOFMEMORY;
        stream.Attach(pStream);
        return S_OK;
    }

    HRESULT WicGetSize(const std::vector<BYTE>& bytes, UINT& w, UINT& h)
    {
        w = h = 0;
        if (bytes.empty() || bytes.size() > UINT_MAX) return E_INVALIDARG;

        ComPtr<IWICImagingFactory> f;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&f));
        if (FAILED(hr)) return hr;

        ComPtr<IStream> stream;
        hr = CreateStreamFromBytes(bytes, stream);
        if (FAILED(hr)) return hr;

        ComPtr<IWICBitmapDecoder> dec;
        hr = f->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec);
        if (FAILED(hr)) return hr;

        ComPtr<IWICBitmapFrameDecode> fr;
        hr = dec->GetFrame(0, &fr);
        if (FAILED(hr)) return hr;

        return fr->GetSize(&w, &h);
    }

    HRESULT WicDecodeToGray8(const std::vector<BYTE>& bytes, UINT& w, UINT& h,
        std::vector<BYTE>& gray)
    {
        w = h = 0;
        gray.clear();
        if (bytes.empty() || bytes.size() > UINT_MAX) return E_INVALIDARG;

        ComPtr<IWICImagingFactory> f;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&f));
        if (FAILED(hr)) return hr;

        ComPtr<IStream> stream;
        hr = CreateStreamFromBytes(bytes, stream);
        if (FAILED(hr)) return hr;

        ComPtr<IWICBitmapDecoder> dec;
        hr = f->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec);
        if (FAILED(hr)) return hr;

        ComPtr<IWICBitmapFrameDecode> fr;
        hr = dec->GetFrame(0, &fr);
        if (FAILED(hr)) return hr;

        ComPtr<IWICFormatConverter> cv;
        hr = f->CreateFormatConverter(&cv);
        if (FAILED(hr)) return hr;

        hr = cv->Initialize(fr.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return hr;

        hr = cv->GetSize(&w, &h);
        if (FAILED(hr)) return hr;
        if (w == 0 || h == 0 || (ULONGLONG)w * h > (256ull << 20)) return E_FAIL;

        gray.resize((size_t)w * h);
        return cv->CopyPixels(nullptr, w, (UINT)gray.size(), gray.data());
    }

    HRESULT WicScaleGray8(const std::vector<BYTE>& src, UINT sw, UINT sh,
        UINT dw, UINT dh, std::vector<BYTE>& dst)
    {
        dst.clear();
        if (src.size() < (size_t)sw * sh || sw == 0 || sh == 0 || dw == 0 || dh == 0)
            return E_INVALIDARG;
        if (src.size() > UINT_MAX || (ULONGLONG)dw * dh > (256ull << 20))
            return E_INVALIDARG;

        ComPtr<IWICImagingFactory> f;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&f));
        if (FAILED(hr)) return hr;

        ComPtr<IWICBitmap> bmp;
        hr = f->CreateBitmapFromMemory(sw, sh, GUID_WICPixelFormat8bppGray, sw,
            (UINT)src.size(), const_cast<BYTE*>(src.data()), &bmp);
        if (FAILED(hr)) return hr;

        ComPtr<IWICBitmapScaler> sc;
        hr = f->CreateBitmapScaler(&sc);
        if (FAILED(hr)) return hr;

        hr = sc->Initialize(bmp.Get(), dw, dh, WICBitmapInterpolationModeNearestNeighbor);
        if (FAILED(hr)) return hr;

        dst.resize((size_t)dw * dh);
        return sc->CopyPixels(nullptr, dw, (UINT)dst.size(), dst.data());
    }

    CString NormalizeFalApiKey(const CString& value)
    {
        CString key(value);
        key.Trim();

        if (key.Left(8).CompareNoCase(_T("FAL_KEY=")) == 0)
        {
            key = key.Mid(8);
            key.Trim();
        }

        if (key.GetLength() >= 2)
        {
            const TCHAR first = key[0];
            const TCHAR last = key[key.GetLength() - 1];

            if ((first == _T('"') && last == _T('"')) ||
                (first == _T('\'') && last == _T('\'')))
            {
                key = key.Mid(1, key.GetLength() - 2);
                key.Trim();
            }
        }

        if (key.Left(14).CompareNoCase(_T("Authorization:")) == 0)
        {
            key = key.Mid(14);
            key.Trim();
        }

        if (key.Left(4).CompareNoCase(_T("Key ")) == 0)
        {
            key = key.Mid(4);
            key.Trim();
        }
        else if (key.Left(7).CompareNoCase(_T("Bearer ")) == 0)
        {
            key = key.Mid(7);
            key.Trim();
        }

        return key;
    }
} // namespace

// ============================================================================
CMetaSAM3Client::CMetaSAM3Client(LPCTSTR host, WORD port, bool useHttps)
    : m_host(host), m_port(port), m_https(useHttps)
{
	m_vsPrompt.push_back(_T("the bright colored activation clusters overlaid on the brain"));
	m_vsPrompt.push_back(_T("all bright activation blobs"));
	m_vsPrompt.push_back(_T("the largest activation cluster"));
	m_vsPrompt.push_back(_T("the red and yellow highlighted regions"));
}

void CMetaSAM3Client::SetServer(LPCTSTR host, WORD port, bool useHttps)
{
    m_host = host; m_port = port; m_https = useHttps;
}

void CMetaSAM3Client::SetApiKey(LPCTSTR key)
{
    m_apiKey = key;
}

void CMetaSAM3Client::SetMaxMasks(int n)
{
    if (n < 1) n = 1;
    if (n > 32) n = 32;
    m_maxMasks = n;
}

void CMetaSAM3Client::SetTimeouts(int resolveMs, int connectMs, int sendMs, int receiveMs)
{
    m_resolveMs = resolveMs; m_connectMs = connectMs;
    m_sendMs = sendMs;        m_recvMs = receiveMs;
}

void CMetaSAM3Client::SetError(LPCTSTR fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    m_lastError.FormatV(fmt, args);
    va_end(args);
    TRACE(_T("[SAM3] ERROR: %s\n"), (LPCTSTR)m_lastError);
}

bool CMetaSAM3Client::ReadFileBytes(const CString& path, std::vector<BYTE>& data)
{
    CFile f;
    CFileException ex;
    if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary, &ex))
    {
        TCHAR msg[256] = {};
        ex.GetErrorMessage(msg, _countof(msg));
        SetError(_T("Cannot open image '%s': %s"), (LPCTSTR)path, msg);
        return false;
    }

    const ULONGLONG len = f.GetLength();
    if (len == 0 || len > (256ull << 20))
    {
        SetError(_T("Image size invalid (%llu bytes): %s"), len, (LPCTSTR)path);
        return false;
    }

    try
    {
        data.resize((size_t)len);
        if (f.Read(data.data(), (UINT)len) != (UINT)len)
        {
            SetError(_T("Short read on '%s'"), (LPCTSTR)path);
            return false;
        }
    }
    catch (CFileException* e)
    {
        TCHAR msg[256] = {};
        e->GetErrorMessage(msg, _countof(msg));
        e->Delete();
        SetError(_T("Read error on '%s': %s"), (LPCTSTR)path, msg);
        return false;
    }
    return true;
}

bool CMetaSAM3Client::HttpPostJson(
    const CStringW& path,
    const std::wstring& headers,
    const std::string& body,
    std::string& response,
    DWORD& httpStatus)
{
    response.clear();
    httpStatus = 0;

    if (body.size() > MAXDWORD)
    {
        SetError(_T("JSON request body is too large"));
        return false;
    }

    WinHttpHandle ses(WinHttpOpen(
        L"MIDEE-SAM3/1.0",
        ProxyAccessType(m_host),
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));

    if (!ses)
    {
        SetError(_T("WinHttpOpen failed (%lu)"), ::GetLastError());
        return false;
    }

    const CStringW whost(m_host);
    WinHttpHandle con(WinHttpConnect(ses.h, whost, m_port, 0));
    if (!con)
    {
        SetError(
            _T("WinHttpConnect to %s:%u failed (%lu)"),
            (LPCTSTR)m_host,
            (unsigned)m_port,
            ::GetLastError());
        return false;
    }

    WinHttpHandle req(WinHttpOpenRequest(
        con.h,
        L"POST",
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        m_https ? WINHTTP_FLAG_SECURE : 0));

    if (!req)
    {
        SetError(_T("WinHttpOpenRequest failed (%lu)"), ::GetLastError());
        return false;
    }

    if (!WinHttpSetTimeouts(
        req.h,
        m_resolveMs,
        m_connectMs,
        m_sendMs,
        m_recvMs))
    {
        SetError(_T("WinHttpSetTimeouts failed (%lu)"), ::GetLastError());
        return false;
    }

    const LPCWSTR requestHeaders =
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();

    const DWORD requestHeadersLength =
        headers.empty() ? 0 : static_cast<DWORD>(-1L);

    const DWORD bodyLength = static_cast<DWORD>(body.size());
    LPVOID bodyData = body.empty()
        ? WINHTTP_NO_REQUEST_DATA
        : const_cast<char*>(body.data());

    if (!WinHttpSendRequest(
        req.h,
        requestHeaders,
        requestHeadersLength,
        bodyData,
        bodyLength,
        bodyLength,
        0))
    {
        const DWORD error = ::GetLastError();

        SetError(
            error == ERROR_WINHTTP_CANNOT_CONNECT
            ? _T("Cannot connect to %s:%u (%lu)")
            : _T("WinHttpSendRequest failed for %s:%u (%lu)"),
            (LPCTSTR)m_host,
            (unsigned)m_port,
            error);
        return false;
    }

    if (!WinHttpReceiveResponse(req.h, nullptr))
    {
        const DWORD error = ::GetLastError();
        SetError(
            error == ERROR_WINHTTP_TIMEOUT
            ? _T("Server timed out (%lu)")
            : _T("WinHttpReceiveResponse failed (%lu)"),
            error);
        return false;
    }

    DWORD statusSize = sizeof(httpStatus);
    if (!WinHttpQueryHeaders(
        req.h,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &httpStatus,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX))
    {
        SetError(_T("Cannot read HTTP status (%lu)"), ::GetLastError());
        return false;
    }

    std::vector<BYTE> raw;
    if (!ReadAllBytes(req.h, raw))
    {
        SetError(_T("WinHttpReadData failed (%lu)"), ::GetLastError());
        return false;
    }

    response.assign(raw.begin(), raw.end());
    return true;
}
//bool CMetaSAM3Client::HttpPostJson(const CStringW& path, const std::wstring& headers, const std::string& body, std::string& response, DWORD& httpStatus)
//{
//    response.clear();
//    httpStatus = 0;
//
//    WinHttpHandle ses(WinHttpOpen(L"MIDEE-SAM3/1.0", ProxyAccessType(m_host),
//        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
//    if (!ses) { SetError(_T("WinHttpOpen failed (%lu)"), ::GetLastError()); return false; }
//
//    const CStringW whost(m_host);
//    WinHttpHandle con(WinHttpConnect(ses.h, whost, m_port, 0));
//    if (!con)
//    {
//        SetError(_T("WinHttpConnect to %s:%u failed (%lu)"),
//            (LPCTSTR)m_host, (unsigned)m_port, ::GetLastError());
//        return false;
//    }
//
//    WinHttpHandle req(WinHttpOpenRequest(con.h, L"POST", path, nullptr, WINHTTP_NO_REFERER,
//        WINHTTP_DEFAULT_ACCEPT_TYPES, m_https ? WINHTTP_FLAG_SECURE : 0));
//    if (!req) { SetError(_T("WinHttpOpenRequest failed (%lu)"), ::GetLastError()); return false; }
//
//    WinHttpSetTimeouts(req.h, m_resolveMs, m_connectMs, m_sendMs, m_recvMs);
//
//    LPCWSTR hdrs = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
//    const DWORD hdrLen = headers.empty() ? 0 : (DWORD)-1L;
//    if (!WinHttpSendRequest(req.h, hdrs, hdrLen, WINHTTP_NO_REQUEST_DATA, 0,
//        (DWORD)body.size(), 0))
//    {
//        const DWORD e = ::GetLastError();
//        SetError(e == ERROR_WINHTTP_CANNOT_CONNECT
//            ? _T("Cannot connect to %s:%u (%lu)")
//            : _T("WinHttpSendRequest failed for %s:%u (%lu)"),
//            (LPCTSTR)m_host, (unsigned)m_port, e);
//        return false;
//    }
//
//    const char* p = body.data();
//    size_t left = body.size();
//    while (left)
//    {
//        const DWORD chunk = (DWORD)std::min<size_t>(left, 1u << 20);
//        DWORD written = 0;
//        if (!WinHttpWriteData(req.h, p, chunk, &written) || written == 0)
//        {
//            SetError(_T("WinHttpWriteData failed (%lu)"), ::GetLastError());
//            return false;
//        }
//        p += written; left -= written;
//    }
//
//    if (!WinHttpReceiveResponse(req.h, nullptr))
//    {
//        const DWORD e = ::GetLastError();
//        SetError(e == ERROR_WINHTTP_TIMEOUT ? _T("Server timed out (%lu)")
//            : _T("WinHttpReceiveResponse failed (%lu)"), e);
//        return false;
//    }
//
//    DWORD sz = sizeof(httpStatus);
//    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
//        WINHTTP_HEADER_NAME_BY_INDEX, &httpStatus, &sz, WINHTTP_NO_HEADER_INDEX);
//
//    std::vector<BYTE> raw;
//    if (!ReadAllBytes(req.h, raw))
//    {
//        SetError(_T("WinHttpReadData failed (%lu)"), ::GetLastError());
//        return false;
//    }
//    response.assign(raw.begin(), raw.end());
//    return true;
//}

bool CMetaSAM3Client::HttpGetBytes(const CStringW& url, std::vector<BYTE>& bytes)
{
    bytes.clear();

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url, 0, 0, &uc))
    {
        SetError(_T("Bad URL: %s"), (LPCTSTR)CString(url));
        return false;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)
    {
        SetError(_T("Unsupported URL scheme"));
        return false;
    }
    const CStringW host(uc.lpszHostName, (int)uc.dwHostNameLength);
    CStringW path(uc.lpszUrlPath, (int)uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        path += CStringW(uc.lpszExtraInfo, (int)uc.dwExtraInfoLength);
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    WinHttpHandle ses(WinHttpOpen(L"MIDEE-SAM3/1.0", ProxyAccessType(host),
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!ses) { SetError(_T("WinHttpOpen failed (%lu)"), ::GetLastError()); return false; }

    WinHttpHandle con(WinHttpConnect(ses.h, host, uc.nPort, 0));
    if (!con)
    {
        SetError(_T("WinHttpConnect to %s failed (%lu)"),
            (LPCTSTR)CString(host), ::GetLastError());
        return false;
    }

    WinHttpHandle req(WinHttpOpenRequest(con.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0));
    if (!req) { SetError(_T("WinHttpOpenRequest failed (%lu)"), ::GetLastError()); return false; }

    WinHttpSetTimeouts(req.h, m_resolveMs, m_connectMs, m_sendMs, m_recvMs);

    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        SetError(_T("Download request failed (%lu)"), ::GetLastError());
        return false;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr))
    {
        SetError(_T("Download failed (%lu)"), ::GetLastError());
        return false;
    }

    DWORD status = 0;
    DWORD sz = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);

    if (!ReadAllBytes(req.h, bytes))
    {
        SetError(_T("Download read failed (%lu)"), ::GetLastError());
        return false;
    }
    if (status != 200)
    {
        SetError(_T("Download returned HTTP %lu"), status);
        return false;
    }
    if (bytes.empty())
    {
        SetError(_T("Download returned an empty body"));
        return false;
    }
    return true;
}

bool CMetaSAM3Client::SegmentImpl(const std::string& imageUrlUtf8, const CString& prompt,
    int width, int height, std::vector<CSam3Instance>& out)
{
    out.clear();

    if (width <= 0 || height <= 0 ||
        static_cast<ULONGLONG>(width) * static_cast<ULONGLONG>(height) >
        (64ull << 20))
    {
        SetError(_T("Invalid image dimensions: %d x %d"), width, height);
        return false;
    }

    if (prompt.IsEmpty())
    {
        SetError(_T("The segmentation prompt cannot be empty"));
        return false;
    }

    const ULONGLONG t0 = ::GetTickCount64();
    const size_t total = static_cast<size_t>(width) *
        static_cast<size_t>(height);


    try
    {
        CString key(m_apiKey);

        if (key.IsEmpty())
        {
            size_t size = 0;
            wchar_t* environmentValue = nullptr;

            if (_wdupenv_s(
                &environmentValue,
                &size,
                L"META_AI_API_KEY") == 0 &&
                //L"FAL_KEY") == 0 &&
                environmentValue != nullptr)
            {
                key = environmentValue;
                free(environmentValue);
            }
        }

        key = NormalizeFalApiKey(key);
        if (key.Left(4).CompareNoCase(_T("LLM_")) == 0 ||
            key.Left(4).CompareNoCase(_T("LLM|")) == 0)
        {
            SetError(
                _T("FAL_KEY contains a Meta AI key. fal.ai requires a ")
                _T("separate key issued by fal.ai. Store the Meta key in ")
                _T("META_AI_API_KEY and store the fal.ai key in FAL_KEY."));
            return false;
        }

        if (key.IsEmpty())
        {
            SetError(
                _T("FAL API key is not set; call SetApiKey() or set FAL_KEY"));
            return false;
        }

        TRACE(
            _T("[SAM3] Loaded FAL_KEY length: %d\n"),
            key.GetLength());

        const CStringA utf8(CT2A(prompt, CP_UTF8));
        std::string body("{\"image_url\":\"");
        body += JsonEscape(imageUrlUtf8);
        body += "\",\"prompt\":\"";
        body += JsonEscape(std::string(utf8.GetString(), utf8.GetLength()));
        body += "\",\"apply_mask\":false,\"return_multiple_masks\":true";

        char mm[64] = {};
        sprintf_s(mm, ",\"max_masks\":%d", m_maxMasks);
        body += mm;
        body += ",\"include_scores\":true,\"include_boxes\":true,\"output_format\":\"png\"}";

        // Ensure m_host is set to "fal.run" for fal.ai endpoints
        TRACE(_T("[SAM3] POST %s://%s%s  prompt=\"%s\"\n"),
            m_https ? _T("https") : _T("http"), (LPCTSTR)m_host, _T("/fal-ai/sam-3/image"), (LPCTSTR)prompt);

        const CStringW wideKey(key);

        std::wstring headers;
        headers.reserve(
            static_cast<size_t>(wideKey.GetLength()) + 96);
        headers += L"Authorization: Key ";
        headers.append(
            wideKey.GetString(),
            static_cast<size_t>(wideKey.GetLength()));
        headers += L"\r\n";
        headers += L"Content-Type: application/json\r\n";
        headers += L"Accept: application/json\r\n";

        std::string resp;
        DWORD status = 0;

        if (!HttpPostJson(
            L"/fal-ai/sam-3/image",
            headers,
            body,
            resp,
            status))
        {
            return false;
        }

        if (status == 401 || status == 403)
        {
            const std::string details = resp.substr(0, 500);

            SetError(
                _T("fal.ai returned HTTP %lu: %hs. Verify that FAL_KEY is ")
                _T("a fal.ai key and that the running Visual Studio process ")
                _T("has the current environment value."),
                status,
                details.empty() ? "no response details" : details.c_str());
            return false;
        }

        if (status < 200 || status >= 300)
        {
            const std::string details = resp.substr(0, 500);
            SetError(
                _T("fal.ai returned HTTP %lu: %hs"),
                status,
                details.empty() ? "no response details" : details.c_str());
            return false;
        }

        JParser jp(resp);
        const J root = jp.parse();
        const J* masks = root.find("masks");
        if (!masks || masks->t != J::Arr)
            throw std::runtime_error("missing JSON field: masks");
        const J* scores = root.find("scores");
        const J* boxes = root.find("boxes");

        auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };

        for (size_t i = 0; i < masks->a.size(); ++i)
        {
            const J& m = masks->a[i];
            if (m.t != J::Obj) throw std::runtime_error("mask entry is not an object");
            const std::string& murl = m.req("url").s;
            if (murl.empty()) throw std::runtime_error("mask URL is empty");

            CSam3Instance inst;

            if (scores && scores->t == J::Arr && i < scores->a.size() &&
                scores->a[i].t == J::Num)
                inst.score = (float)scores->a[i].n;

            bool haveBox = false;
            if (boxes && boxes->t == J::Arr && i < boxes->a.size() &&
                boxes->a[i].t == J::Arr && boxes->a[i].a.size() == 4)
            {
                float c[4];
                bool nums = true;
                for (int k = 0; k < 4; ++k)
                {
                    if (boxes->a[i].a[k].t != J::Num) { nums = false; break; }
                    c[k] = (float)boxes->a[i].a[k].n;
                }
                if (nums)
                {
                    inst.box[0] = clampf((c[0] - c[2] / 2) * width, 0.f, (float)width);
                    inst.box[1] = clampf((c[1] - c[3] / 2) * height, 0.f, (float)height);
                    inst.box[2] = clampf((c[0] + c[2] / 2) * width, 0.f, (float)width);
                    inst.box[3] = clampf((c[1] + c[3] / 2) * height, 0.f, (float)height);
                    haveBox = true;
                }
            }
            if (!haveBox)
            {
                inst.box[0] = 0.f; inst.box[1] = 0.f;
                inst.box[2] = (float)width; inst.box[3] = (float)height;
            }

            std::vector<BYTE> png;
            if (!HttpGetBytes(CStringW(murl.c_str()), png))
            {
                CString t;
                t.Format(_T("mask %Iu: %s"), i, (LPCTSTR)m_lastError);
                m_lastError = t;
                return false;
            }

            UINT mw = 0, mh = 0;
            std::vector<BYTE> gray;
            HRESULT hr = WicDecodeToGray8(png, mw, mh, gray);
            if (FAILED(hr))
            {
                SetError(_T("mask %Iu: PNG decode failed (0x%08X)"), i, hr);
                return false;
            }
            if (mw != (UINT)width || mh != (UINT)height)
            {
                std::vector<BYTE> scaled;
                hr = WicScaleGray8(gray, mw, mh, (UINT)width, (UINT)height, scaled);
                if (FAILED(hr))
                {
                    SetError(_T("mask %Iu: rescale to %dx%d failed (0x%08X)"),
                        i, width, height, hr);
                    return false;
                }
                gray.swap(scaled);
            }
            if (gray.size() != total)
            {
                SetError(_T("mask %Iu: unexpected raster size"), i);
                return false;
            }

            inst.mask.resize(total);
            for (size_t k = 0; k < total; ++k)
                inst.mask[k] = (gray[k] > 127) ? 1 : 0;

            out.push_back(std::move(inst));
        }

        TRACE(_T("[SAM3] OK: %dx%d, %Iu instance(s), %llu ms\n"),
            width, height, out.size(), ::GetTickCount64() - t0);
        for (size_t i = 0; i < out.size(); ++i)
            TRACE(_T("[SAM3]    #%Iu score=%.3f box=[%.0f %.0f %.0f %.0f]\n"), i, out[i].score,
                out[i].box[0], out[i].box[1], out[i].box[2], out[i].box[3]);
        return true;
    }
    catch (const std::exception& e)
    {
        SetError(_T("Response parse error: %hs"), e.what());
        return false;
    }
}

bool CMetaSAM3Client::Segment(const CString& imagePath, const CString& prompt,
    std::vector<CSam3Instance>& out, int& width, int& height)
{
    out.clear();
    width = height = 0;
    m_lastError.Empty();
    ComInit com;

    std::vector<BYTE> img;
    if (!ReadFileBytes(imagePath, img)) return false;

    const char* mime = SniffMime(img);
    if (!mime)
    {
        SetError(_T("Unsupported image type (need JPEG/PNG/WebP/GIF/BMP): %s"),
            (LPCTSTR)imagePath);
        return false;
    }

    UINT w = 0, h = 0;
    HRESULT hr = WicGetSize(img, w, h);
    if (FAILED(hr) || w == 0 || h == 0)
    {
        SetError(_T("Cannot decode image '%s' (0x%08X)"), (LPCTSTR)imagePath, hr);
        return false;
    }
    if ((ULONGLONG)w * h > (64ull << 20))
    {
        SetError(_T("Image too large (%u x %u)"), w, h);
        return false;
    }
    width = (int)w; height = (int)h;

    std::string b64;
    if (!Base64Encode(img, b64)) { SetError(_T("Base64 encoding failed")); return false; }

    std::string dataUri("data:");
    dataUri += mime;
    dataUri += ";base64,";
    dataUri += b64;

    return SegmentImpl(dataUri, prompt, width, height, out);
}

bool CMetaSAM3Client::SegmentUrl(const CString& imageUrl, const CString& prompt,
    std::vector<CSam3Instance>& out, int& width, int& height)
{
    out.clear();
    width = height = 0;
    m_lastError.Empty();
    ComInit com;

    std::vector<BYTE> img;
    if (!HttpGetBytes(CStringW(imageUrl), img)) return false;

    UINT w = 0, h = 0;
    HRESULT hr = WicGetSize(img, w, h);
    if (FAILED(hr) || w == 0 || h == 0)
    {
        SetError(_T("Cannot decode image URL (0x%08X)"), hr);
        return false;
    }
    if ((ULONGLONG)w * h > (64ull << 20))
    {
        SetError(_T("Image too large (%u x %u)"), w, h);
        return false;
    }
    width = (int)w; height = (int)h;

    const CStringA urlA(CT2A(imageUrl, CP_UTF8));
    return SegmentImpl(std::string(urlA.GetString(), urlA.GetLength()),
        prompt, width, height, out);
}
