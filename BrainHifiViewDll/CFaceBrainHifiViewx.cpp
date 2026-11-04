// CFaceBrainHifiViewx.cpp : implementation file
//

#include "pch.h"
#include "CFaceBrainHifiViewx.h"
#include "CDlgBhMsiViewer.h"


// CFaceBrainHifiViewx

CFaceBrainHifiViewx::CFaceBrainHifiViewx()
{
}

CFaceBrainHifiViewx::~CFaceBrainHifiViewx()
{
}


// CFaceBrainHifiViewx member functions

int CFaceBrainHifiViewx::showDlgBhMsiView(CWnd* pView)
{
	if (!m_pDlgBhMsiViewer)
	{
		m_pDlgBhMsiViewer = new CDlgBhMsiViewer(pView);
		if (!m_pDlgBhMsiViewer)
		{
			return -1;
		}
		if (!m_pDlgBhMsiViewer->Create(IDD_BH_MSI_VIEWER, pView))
		{
			delete m_pDlgBhMsiViewer;
			m_pDlgBhMsiViewer = nullptr;
			return -2;
		}

	}
	m_pDlgBhMsiViewer->m_pParent = pView;
	m_pDlgBhMsiViewer->ShowWindow(SW_SHOW);
	return 1;
}
