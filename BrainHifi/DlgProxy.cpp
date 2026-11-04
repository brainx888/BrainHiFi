
// DlgProxy.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "BrainHifi.h"
#include "DlgProxy.h"
#include "BrainHifiDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CBrainHifiDlgAutoProxy

IMPLEMENT_DYNCREATE(CBrainHifiDlgAutoProxy, CCmdTarget)

CBrainHifiDlgAutoProxy::CBrainHifiDlgAutoProxy()
{
	EnableAutomation();

	// To keep the application running as long as an automation
	//	object is active, the constructor calls AfxOleLockApp.
	AfxOleLockApp();

	// Get access to the dialog through the application's
	//  main window pointer.  Set the proxy's internal pointer
	//  to point to the dialog, and set the dialog's back pointer to
	//  this proxy.
	ASSERT_VALID(AfxGetApp()->m_pMainWnd);
	if (AfxGetApp()->m_pMainWnd)
	{
		ASSERT_KINDOF(CBrainHifiDlg, AfxGetApp()->m_pMainWnd);
		if (AfxGetApp()->m_pMainWnd->IsKindOf(RUNTIME_CLASS(CBrainHifiDlg)))
		{
			m_pDialog = reinterpret_cast<CBrainHifiDlg*>(AfxGetApp()->m_pMainWnd);
			m_pDialog->m_pAutoProxy = this;
		}
	}
}

CBrainHifiDlgAutoProxy::~CBrainHifiDlgAutoProxy()
{
	// To terminate the application when all objects created with
	// 	with automation, the destructor calls AfxOleUnlockApp.
	//  Among other things, this will destroy the main dialog
	if (m_pDialog != nullptr)
		m_pDialog->m_pAutoProxy = nullptr;
	AfxOleUnlockApp();
}

void CBrainHifiDlgAutoProxy::OnFinalRelease()
{
	// When the last reference for an automation object is released
	// OnFinalRelease is called.  The base class will automatically
	// deletes the object.  Add additional cleanup required for your
	// object before calling the base class.

	CCmdTarget::OnFinalRelease();
}

BEGIN_MESSAGE_MAP(CBrainHifiDlgAutoProxy, CCmdTarget)
END_MESSAGE_MAP()

BEGIN_DISPATCH_MAP(CBrainHifiDlgAutoProxy, CCmdTarget)
END_DISPATCH_MAP()

// Note: we add support for IID_IBrainHifi to support typesafe binding
//  from VBA.  This IID must match the GUID that is attached to the
//  dispinterface in the .IDL file.

// {4b608cf6-a6f0-4cea-94dc-bcd0b09208fd}
static const IID IID_IBrainHifi =
{0x4b608cf6,0xa6f0,0x4cea,{0x94,0xdc,0xbc,0xd0,0xb0,0x92,0x08,0xfd}};

BEGIN_INTERFACE_MAP(CBrainHifiDlgAutoProxy, CCmdTarget)
	INTERFACE_PART(CBrainHifiDlgAutoProxy, IID_IBrainHifi, Dispatch)
END_INTERFACE_MAP()

// The IMPLEMENT_OLECREATE2 macro is defined in pch.h of this project
// {d97ef40b-c637-4b43-a6bf-3dc889b3781d}
IMPLEMENT_OLECREATE2(CBrainHifiDlgAutoProxy, "BrainHifi.Application", 0xd97ef40b,0xc637,0x4b43,0xa6,0xbf,0x3d,0xc8,0x89,0xb3,0x78,0x1d)


// CBrainHifiDlgAutoProxy message handlers
