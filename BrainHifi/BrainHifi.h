
// BrainHifi.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


// CBrainHifiApp:
// See BrainHifi.cpp for the implementation of this class
//

class CBrainHifiApp : public CWinApp
{
public:
	CBrainHifiApp();

// Overrides
public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();

// Implementation

	DECLARE_MESSAGE_MAP()
};

extern CBrainHifiApp theApp;
