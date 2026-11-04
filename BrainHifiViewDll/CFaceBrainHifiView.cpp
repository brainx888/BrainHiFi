// CFaceBrainHifiView.cpp : implementation file
//

#include "pch.h"
#include "CFaceBrainHifiView.h"


// CFaceBrainHifiView

CFaceBrainHifiView::CFaceBrainHifiView()
{
}

CFaceBrainHifiView::~CFaceBrainHifiView()
{
	if(m_pDlgBhMsiViewer)
	{
		delete m_pDlgBhMsiViewer;
		m_pDlgBhMsiViewer = nullptr;
	}
}


// CFaceBrainHifiView member functions
