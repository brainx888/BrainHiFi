#pragma once

#include <afxwin.h>
#include <sapi.h>
#include <atlbase.h>                 // CComPtr
#pragma comment(lib, "sapi.lib")

#define WM_TTS_EVENT (WM_APP + 102)  // posted to owner window on speech events

class CTtsReader
{
public:
    CTtsReader() = default;
    ~CTtsReader() { Shutdown(); }

    // hwndNotify receives WM_TTS_EVENT when speech finishes
    bool Init(HWND hwndNotify);
    void Shutdown();

    bool Speak(const CString& sText);   // async, interrupts current speech
    void Stop();
    void Pause();
    void Resume();
    bool IsSpeaking() const { return m_bSpeaking; }

    void SetRate(int rate);             // -10 (slow) .. +10 (fast)
    void SetVolume(int vol);            // 0 .. 100
    bool SetVoiceByName(LPCWSTR pszContains);   // e.g. L"Zira", L"David", L"Huihui"

    // call from the WM_TTS_EVENT handler; returns true if speech has ended
    bool HandleEvent();

    static CString PrepareText(const CString& sRaw);

private:
    CComPtr<ISpVoice> m_pVoice;
    bool m_bSpeaking = false;
    bool m_bComInit = false;
};
