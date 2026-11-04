#pragma once
#include <afxwin.h>     // CString, CRect (MFC)
#include <string>
#include <vector>

// One detected/segmented object returned by SAM 3.
struct CSam3Instance
{
    float score = 0.f;
    float box[4] = { 0, 0, 0, 0 };      // x0, y0, x1, y1 in image pixels
    std::vector<BYTE> mask;             // width*height, 0 or 1, row-major

    CRect BoxRect() const
    {
        return CRect((int)box[0], (int)box[1], (int)box[2], (int)box[3]);
    }
};

// Thin client for Meta SAM 3 hosted on fal.ai (https://fal.run/fal-ai/sam-3/image).
// No third-party dependencies: WinHTTP for transport, WIC for image/mask
// decoding. Both are Windows system components, so nothing extra to install.
class   AFX_EXT_CLASS CMetaSAM3Client
{
public:
    explicit CMetaSAM3Client(LPCTSTR host = _T("fal.run"), WORD port = 443,bool useHttps = true);

protected:

    CString m_host;
    WORD    m_port;
    bool    m_https;
    CString m_apiKey;
    int     m_maxMasks = 32;
    int     m_resolveMs = 5000, m_connectMs = 10000, m_sendMs = 120000, m_recvMs = 300000;
public:
    CString m_lastError;
	std::vector<CString> m_vsPrompt;  // last mask PNG bytes (for debugging)

public:
    void SetServer(LPCTSTR host, WORD port, bool useHttps = true);
    void SetApiKey(LPCTSTR key);                 // or set the FAL_KEY env var
    void SetMaxMasks(int n);                     // 1..32, default 32
    void SetTimeouts(int resolveMs, int connectMs, int sendMs, int receiveMs);

    // Blocking calls: run them on a worker thread, never on the UI thread.
    // Segment uploads a local image file as a data URI; SegmentUrl passes a
    // public image URL through (still downloaded once for its dimensions).
    // Both return true on success; out may be empty when nothing matches.
    bool Segment(const CString& imagePath, const CString& prompt, std::vector<CSam3Instance>& out, int& width, int& height);
    bool SegmentUrl(const CString& imageUrl, const CString& prompt, std::vector<CSam3Instance>& out, int& width, int& height);

    CString LastErrorText() const { return m_lastError; }

private:
    bool SegmentImpl(const std::string& imageUrlUtf8, const CString& prompt, int width, int height, std::vector<CSam3Instance>& out);
    bool ReadFileBytes(const CString& path, std::vector<BYTE>& data);
    bool HttpPostJson(const CStringW& path, const std::wstring& headers, const std::string& body, std::string& response, DWORD& httpStatus);
    bool HttpGetBytes(const CStringW& url, std::vector<BYTE>& bytes);
    void SetError(LPCTSTR fmt, ...);
};
