// MesiWorkspaceDlg.h - IDD_MESI_WORKSPACE_DLG: master view of the subject
// cohort, processing controls, AI model configuration and result review.
//
// The dialog is a thin presenter over mesi::MESIManager.  Manager events
// arrive on worker threads and are marshalled to the GUI thread with
// PostMessage(WM_MESI_EVENT); nothing in the dialog touches a control from a
// background thread.
#pragma once

#include "../resource.h"
#include "../Mesi/MESIManager.h"

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>
#include <memory>
#include <string>
#include <vector>

class CMesiWorkspaceDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CMesiWorkspaceDlg)

public:
	enum { IDD = IDD_MESI_WORKSPACE_DLG };
	static constexpr UINT WM_MESI_EVENT = WM_APP + 0x120;   // lParam = mesi::MesiEvent* (owned)

	// The manager is shared with the owner so jobs survive closing the dialog.
	explicit CMesiWorkspaceDlg(std::shared_ptr<mesi::MESIManager> manager, CWnd* pParent = nullptr);
	~CMesiWorkspaceDlg() override;

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	void OnOK() override {}            // Enter must not close the workspace
	void OnCancel() override;

	afx_msg void OnDestroy();
	afx_msg LRESULT OnMesiEvent(WPARAM wParam, LPARAM lParam);
	afx_msg void OnLvnItemChangedSubjects(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMDblClkSubjects(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBnClickedApplyFilter();
	afx_msg void OnBnClickedAddSubject();
	afx_msg void OnBnClickedEditSubject();
	afx_msg void OnBnClickedRemoveSubject();
	afx_msg void OnBnClickedRunSegmentation();
	afx_msg void OnBnClickedComputeFusion();
	afx_msg void OnBnClickedRunPipeline();
	afx_msg void OnBnClickedRunAll();
	afx_msg void OnBnClickedCancelJobs();
	afx_msg void OnBnClickedExportResults();
	afx_msg void OnBnClickedSaveWorkspace();
	afx_msg void OnBnClickedLoadWorkspace();
	afx_msg void OnBnClickedBrowseModel();
	afx_msg void OnBnClickedApplyAiConfig();
	afx_msg void OnBnClickedVerifyResult();
	DECLARE_MESSAGE_MAP()

private:
	enum SubjectColumn { colId = 0, colAge, colCohort, colModality, colState, colProgress, colResults, colModified, colCount };
	enum ResultColumn { rcolId = 0, rcolMethod, rcolModality, rcolClusters, rcolDipoles, rcolPeak, rcolVerification, rcolCreated, rcolCount };
	enum CohortFilter { cfAll = 0, cfPediatric, cfAdult };
	enum ModalityFilter { mfAll = 0, mfMegOnly, mfEegOnly, mfCombined, mfHasMeg, mfHasEeg };

	// Presentation --------------------------------------------------------
	void InitSubjectList();
	void InitResultList();
	void InitFilterCombos();
	void InitRuntimeCombo();
	void RefreshSubjectList(const std::string& keepSelection = {});
	void RefreshSubjectRow(int row, const mesi::SubjectRecord& s);
	void RefreshResultList();
	void RefreshStatistics();
	void UpdateButtonStates();
	void UpdateProgress(const mesi::MesiEvent& ev);
	void AppendLog(const CString& line);
	void LoadAiConfigToUi();
	bool ApplyAiConfigFromUi(bool quiet);
	std::vector<mesi::SubjectRecord> FilteredSubjects() const;

	std::string SelectedSubjectId() const;
	std::string SelectedResultId() const;
	int RowOfSubject(const std::string& subjectId) const;
	void ReportError(const CString& context, const std::string& utf8Error);

	enum class Action { Segmentation, Fusion, Pipeline };
	void RunForSelected(Action action);

	void InstallEventSink();
	void RemoveEventSink();
	void DrainPostedEvents();

	// State ----------------------------------------------------------------
	std::shared_ptr<mesi::MESIManager> m_manager;
	std::vector<std::string> m_rowIds;        // subject list row -> subject id
	std::vector<std::string> m_resultRowIds;  // result list row  -> result id
	bool m_closing = false;

	// Controls / DDX --------------------------------------------------------
	CListCtrl    m_subjectList;
	CListCtrl    m_resultList;
	CComboBox    m_cbCohortFilter;
	CComboBox    m_cbModalityFilter;
	CComboBox    m_cbRuntime;
	CProgressCtrl m_progress;
	CEdit        m_log;

	CString m_sAgeMin, m_sAgeMax;         // empty = unbounded
	int     m_nCohortFilter = cfAll;
	int     m_nModalityFilter = mfAll;
	CString m_sModelName, m_sModelVersion, m_sModelPath, m_sPrompt;
	int     m_nRuntimeSel = 0;
	double  m_dConfidence = 0.5;
	double  m_dIou = 0.7;
};
