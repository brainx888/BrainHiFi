#include "CStaticBusySpinnerx.h"
#include "pch.h"
#include <cmath>

using namespace Gdiplus;

ULONG_PTR CStaticBusySpinnerx::s_gdiplusToken = 0;
int       CStaticBusySpinnerx::s_refCount = 0;

BEGIN_MESSAGE_MAP(CStaticBusySpinnerx, CStatic)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_TIMER()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

CStaticBusySpinnerx::CStaticBusySpinnerx()
{
    if (s_refCount++ == 0)
    {
        GdiplusStartupInput in;
        GdiplusStartup(&s_gdiplusToken, &in, nullptr);
    }
}

CStaticBusySpinnerx::~CStaticBusySpinnerx()
{
    if (--s_refCount == 0)
        GdiplusShutdown(s_gdiplusToken);
}

void CStaticBusySpinnerx::Start()
{
    if (m_bRunning || !GetSafeHwnd()) return;
    m_bRunning = true;
    m_nTimerId = SetTimer(1, m_nIntervalMs, nullptr);
    ShowWindow(SW_SHOW);
    Invalidate(FALSE);
}

void CStaticBusySpinnerx::Stop()
{
    if (!m_bRunning) return;
    m_bRunning = false;
    if (m_nTimerId) { KillTimer(m_nTimerId); m_nTimerId = 0; }
    if (m_bHideWhenStopped) ShowWindow(SW_HIDE);
    else Invalidate(FALSE);
}

void CStaticBusySpinnerx::OnDestroy()
{
    Stop();
    CStatic::OnDestroy();
}

void CStaticBusySpinnerx::OnTimer(UINT_PTR id)
{
    if (id == 1)
    {
        m_nStep = (m_nStep + 1) % m_nSpokes;
        // repaint including the parent area behind us (for transparency)
        Invalidate(FALSE);
    }
    CStatic::OnTimer(id);
}

// Paint the parent's background into our DC so the control looks transparent
void CStaticBusySpinnerx::DrawBackground(CDC& dc, const CRect& rc)
{
    CWnd* pParent = GetParent();
    if (!pParent) { dc.FillSolidRect(rc, ::GetSysColor(COLOR_BTNFACE)); return; }

    CPoint pt(0, 0);
    MapWindowPoints(pParent, &pt, 1);
    CPoint old = dc.OffsetWindowOrg(pt.x, pt.y);
    pParent->SendMessage(WM_ERASEBKGND, (WPARAM)dc.GetSafeHdc(), 0);
    dc.SetWindowOrg(old);
}

void CStaticBusySpinnerx::OnPaint()
{
    CPaintDC dcPaint(this);
    CRect rc; GetClientRect(&rc);

    // double buffer
    CDC dcMem; dcMem.CreateCompatibleDC(&dcPaint);
    CBitmap bmp; bmp.CreateCompatibleBitmap(&dcPaint, rc.Width(), rc.Height());
    CBitmap* pOld = dcMem.SelectObject(&bmp);

    DrawBackground(dcMem, rc);

    if (m_bRunning)
    {
        Graphics g(dcMem.GetSafeHdc());
        g.SetSmoothingMode(SmoothingModeAntiAlias);

        const float cx = rc.Width() / 2.0f;
        const float cy = rc.Height() / 2.0f;
        const float outer = min(cx, cy) - 1.0f;
        const float inner = outer * 0.55f;
        const float penW = max(2.0f, outer * 0.16f);
        const float pi = 3.14159265f;

        for (int i = 0; i < m_nSpokes; ++i)
        {
            // spoke i is brightest when it equals m_nStep, tail fades behind it
            int age = (m_nStep - i + m_nSpokes) % m_nSpokes;
            BYTE alpha = (BYTE)(255 - (age * 230 / m_nSpokes));

            Pen pen(Color(alpha, GetRValue(m_clr), GetGValue(m_clr), GetBValue(m_clr)), penW);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);

            float a = 2.0f * pi * i / m_nSpokes - pi / 2.0f;
            g.DrawLine(&pen,
                cx + inner * cosf(a), cy + inner * sinf(a),
                cx + outer * cosf(a), cy + outer * sinf(a));
        }
    }

    dcPaint.BitBlt(0, 0, rc.Width(), rc.Height(), &dcMem, 0, 0, SRCCOPY);
    dcMem.SelectObject(pOld);
}