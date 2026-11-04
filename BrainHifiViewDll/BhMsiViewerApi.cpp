#include "pch.h"
#include "BhMsiViewerApi.h"
#include "CDlgBhMsiViewer.h"

extern "C" BH_API INT_PTR BhShowMsiViewer(HWND hParent, LPCTSTR szMriPath, LPCTSTR szSrcPath)
{
	CWnd* pParent = hParent ? CWnd::FromHandle(hParent) : AfxGetMainWnd();
	CDlgBhMsiViewer dlg(pParent);
	if (szMriPath) dlg.m_sMriPath = szMriPath;
	if (szSrcPath) dlg.m_sSrcPath = szSrcPath;
	return dlg.DoModal();
}
