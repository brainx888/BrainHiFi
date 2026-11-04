
// DlgProxy.h: header file
//

#pragma once

class CBrainHifiDlg;


// CBrainHifiDlgAutoProxy command target

class CBrainHifiDlgAutoProxy : public CCmdTarget
{
	DECLARE_DYNCREATE(CBrainHifiDlgAutoProxy)

	CBrainHifiDlgAutoProxy();           // protected constructor used by dynamic creation

// Attributes
public:
	CBrainHifiDlg* m_pDialog;

// Operations
public:

// Overrides
	public:
	virtual void OnFinalRelease();

// Implementation
protected:
	virtual ~CBrainHifiDlgAutoProxy();

	// Generated message map functions

	DECLARE_MESSAGE_MAP()
	DECLARE_OLECREATE(CBrainHifiDlgAutoProxy)

	// Generated OLE dispatch map functions

	DECLARE_DISPATCH_MAP()
	DECLARE_INTERFACE_MAP()
};

