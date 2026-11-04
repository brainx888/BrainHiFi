// dllmain.cpp : MFC extension DLL entry point of BrainHifiViewDll.
#include "pch.h"
#include <afxdllx.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

static AFX_EXTENSION_MODULE BrainHifiViewDllDLL = { false, nullptr };

extern "C" int APIENTRY
DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(lpReserved);
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		TRACE0("BrainHifiViewDll.DLL Initializing!\n");
		if (!AfxInitExtensionModule(BrainHifiViewDllDLL, hInstance))
			return 0;
		new CDynLinkLibrary(BrainHifiViewDllDLL);
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		TRACE0("BrainHifiViewDll.DLL Terminating!\n");
		AfxTermExtensionModule(BrainHifiViewDllDLL);
	}
	return 1;
}
