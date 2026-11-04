#include "CTtsReader.h"
#include "pch.h"

bool CTtsReader::Init(HWND hwndNotify)
{
    if (m_pVoice) return true;

    // If your app already calls AfxOleInit() this is harmless (returns S_FALSE / RPC_E_CHANGED_MODE)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_bComInit = SUCCEEDED(hr);

    hr = m_pVoice.CoCreateInstance(CLSID_SpVoice);
    if (FAILED(hr)) return false;

    // Get a window message when speech ends
    m_pVoice->SetNotifyWindowMessage(hwndNotify, WM_TTS_EVENT, 0, 0);
    const ULONGLONG ev = SPFEI(SPEI_END_INPUT_STREAM);
    m_pVoice->SetInterest(ev, ev);
    return true;
}

void CTtsReader::Shutdown()
{
    if (m_pVoice)
    {
        m_pVoice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        m_pVoice.Release();
    }
    if (m_bComInit) { CoUninitialize(); m_bComInit = false; }
    m_bSpeaking = false;
}

bool CTtsReader::Speak(const CString& sText)
{
    if (!m_pVoice) return false;
    CString s = PrepareText(sText);
    if (s.IsEmpty()) return false;

    m_pVoice->Resume();   // in case it was paused
    // SPF_IS_NOT_XML: results may contain '<' or '&' (e.g. "p<0.05") which SAPI would treat as markup
    HRESULT hr = m_pVoice->Speak(CT2CW(s),
        SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
    m_bSpeaking = SUCCEEDED(hr);
    return m_bSpeaking;
}

void CTtsReader::Stop()
{
    if (!m_pVoice) return;
    m_pVoice->Resume();
    m_pVoice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
    m_bSpeaking = false;
}

void CTtsReader::Pause() { if (m_pVoice) m_pVoice->Pause(); }
void CTtsReader::Resume() { if (m_pVoice) m_pVoice->Resume(); }

void CTtsReader::SetRate(int rate)
{
    if (m_pVoice) m_pVoice->SetRate(max(-10, min(rate, 10)));
}

void CTtsReader::SetVolume(int vol)
{
    if (m_pVoice) m_pVoice->SetVolume((USHORT)max(0, min(vol, 100)));
}

bool CTtsReader::SetVoiceByName(LPCWSTR pszContains)
{
    if (!m_pVoice) return false;

    CComPtr<ISpObjectTokenCategory> pCat;
    if (FAILED(pCat.CoCreateInstance(CLSID_SpObjectTokenCategory))) return false;
    if (FAILED(pCat->SetId(SPCAT_VOICES, FALSE))) return false;

    CComPtr<IEnumSpObjectTokens> pEnum;
    if (FAILED(pCat->EnumTokens(nullptr, nullptr, &pEnum))) return false;

    ULONG n = 0; pEnum->GetCount(&n);
    for (ULONG i = 0; i < n; ++i)
    {
        CComPtr<ISpObjectToken> pTok;
        if (FAILED(pEnum->Item(i, &pTok))) continue;

        LPWSTR pszDesc = nullptr;
        if (SUCCEEDED(pTok->GetStringValue(nullptr, &pszDesc)) && pszDesc)
        {
            bool match = (wcsstr(pszDesc, pszContains) != nullptr);
            CoTaskMemFree(pszDesc);
            if (match) return SUCCEEDED(m_pVoice->SetVoice(pTok));
        }
    }
    return false;
}

bool CTtsReader::HandleEvent()
{
    if (!m_pVoice) return false;
    bool ended = false;
    SPEVENT ev = {};
    ULONG fetched = 0;
    while (SUCCEEDED(m_pVoice->GetEvents(1, &ev, &fetched)) && fetched == 1)
    {
        if (ev.eEventId == SPEI_END_INPUT_STREAM) ended = true;
    }
    if (ended) m_bSpeaking = false;
    return ended;
}

// Make the report sound natural: collapse blank lines, turn line breaks into pauses
CString CTtsReader::PrepareText(const CString& sRaw)
{
    CString s = sRaw;
    s.Replace(_T("\r\n"), _T("\n"));
    s.Replace(_T("\t"), _T(" "));
    s.Replace(_T("\n\n"), _T("\n"));
    s.Replace(_T("\n"), _T(". "));       // sentence break = audible pause
    s.Replace(_T("_"), _T(" "));
    s.Replace(_T(". . "), _T(". "));
    s.Trim();
    return s;
}