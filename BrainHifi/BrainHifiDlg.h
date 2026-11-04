// BrainHifiDlg.h - main dialog of BrainHifi.
//
// Owns the shared mesi::MESIManager (subject cohort, AI segmentation, MESI
// fusion, results) and opens the MESI workspace on demand.  The legacy
// "segment image files with SAM 3" button is kept but now runs on a worker
// thread through the same engine adapter, so the GUI never blocks on HTTP.
#pragma once

#include "Mesi/MESIManager.h"
#include "MesiUi/MetaSam3SegmentationEngine.h"
#include "../BrainHifiViewDll/CFaceBrainHifiViewx.h"

#include <afxcmn.h>
#include <memory>

class CBrainHifiDlgAutoProxy;

class CBrainHifiDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CBrainHifiDlg);
	friend class CBrainHifiDlgAutoProxy;

public:
	explicit CBrainHifiDlg(CWnd* pParent = nullptr);
	~CBrainHifiDlg() override;

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_BRAINHIFI_DIALOG };
#endif
	// lParam = CString* (owned) with a line for the message box; wParam = percent
	static constexpr UINT WM_SAM3_PROGRESS = WM_APP + 0x130;

	std::shared_ptr<mesi::MESIManager> mesiManager() const noexcept { return m_pMesiManager; }

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	void OnOK() override;
	void OnCancel() override;
	DECLARE_MESSAGE_MAP()

	BOOL CanExit();
	void CreateMesiManager();
	void AppendMessage(const CString& line);

	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg LRESULT OnSam3Progress(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedBntContact();        // "Segment Images (SAM 3)"
	afx_msg void OnBnClickedBntMsiViewer();
	afx_msg void OnBnClickedBntMesiWorkspace();

public:
	// Automation proxy: lifetime is owned by COM, so this stays a back pointer.
	CBrainHifiDlgAutoProxy* m_pAutoProxy = nullptr;

private:
	HICON m_hIcon = nullptr;
	CString m_sPrompt4Sam;
	CString m_sImageAnalysisMessage;
	CProgressCtrl m_progressSam;

	std::shared_ptr<mesi::MESIManager> m_pMesiManager;
	std::unique_ptr<mesi::MesiJobRunner> m_pImageJobs;          // batch image segmentation
	std::unique_ptr<CFaceBrainHifiViewx> m_pFaceBrainHifiView;  // MSI viewer facade
};
