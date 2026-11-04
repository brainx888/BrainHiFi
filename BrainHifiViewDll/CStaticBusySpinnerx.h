#pragma once
#pragma once
#include <afxwin.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// Usage: place a Static control (IDC_SPINNER) in the dialog, then
//        m_spinner.SubclassDlgItem(IDC_SPINNER, this);
class CStaticBusySpinnerx : public CStatic
{
public:
    CStaticBusySpinnerx();
    virtual ~CStaticBusySpinnerx();

    void Start();                       // show + animate
    void Stop();                        // stop + hide (if m_bHideWhenStopped)
    bool IsRunning() const { return m_bRunning; }

    void SetColor(COLORREF clr) { m_clr = clr; Invalidate(FALSE); }
    void SetSpokes(int n) { m_nSpokes = max(6, min(n, 24)); }
    void SetIntervalMs(UINT ms) { m_nIntervalMs = ms; if (m_bRunning) { Stop(); Start(); } }
    void HideWhenStopped(bool b) { m_bHideWhenStopped = b; }

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }   // we paint everything
    afx_msg void OnTimer(UINT_PTR id);
    afx_msg void OnDestroy();
    DECLARE_MESSAGE_MAP()

private:
    void DrawBackground(CDC& dc, const CRect& rc);

    static ULONG_PTR s_gdiplusToken;
    static int       s_refCount;

    bool     m_bRunning = false;
    bool     m_bHideWhenStopped = true;
    int      m_nStep = 0;
    int      m_nSpokes = 12;
    UINT     m_nIntervalMs = 80;
    COLORREF m_clr = RGB(0, 120, 215);
    UINT_PTR m_nTimerId = 0;
};
