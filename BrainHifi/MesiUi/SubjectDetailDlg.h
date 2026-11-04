// SubjectDetailDlg.h - IDD_SUBJECT_DETAIL_DLG: create / edit one SubjectRecord
// (identity, cohort, clinical notes, anatomical paths, modality configuration).
// The dialog works on a copy and only hands back a record that passes
// SubjectRecord::validate().
#pragma once

#include "../resource.h"
#include "../Mesi/SubjectRecord.h"

#include <afxdialogex.h>
#include <afxwin.h>

class CSubjectDetailDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CSubjectDetailDlg)

public:
	enum { IDD = IDD_SUBJECT_DETAIL_DLG };

	// `isNew` = false keeps the subject ID read-only (it is the cohort key).
	CSubjectDetailDlg(const mesi::SubjectRecord& record, bool isNew, CWnd* pParent = nullptr);
	~CSubjectDetailDlg() override = default;

	const mesi::SubjectRecord& record() const noexcept { return m_record; }

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	void OnOK() override;

	afx_msg void OnEnChangeAge();
	afx_msg void OnCbnSelChangeCohort();
	afx_msg void OnBnClickedModalityToggle();
	afx_msg void OnBnClickedBrowseT1();
	afx_msg void OnBnClickedBrowseT2();
	afx_msg void OnBnClickedBrowseAtlas();
	afx_msg void OnBnClickedBrowseMeg();
	afx_msg void OnBnClickedBrowseEeg();
	afx_msg void OnBnClickedBrowseCoreg();
	afx_msg void OnBnClickedBrowseTransform();
	afx_msg void OnBnClickedBrowseHeadModel();
	DECLARE_MESSAGE_MAP()

private:
	enum CohortSel { kCohortAuto = 0, kCohortPediatric = 1, kCohortAdult = 2 };

	void BrowseInto(UINT editId, LPCTSTR filter, LPCTSTR defaultExt);
	void UpdateDerivedLabels();
	void UpdateModalityControls();
	bool CollectRecord(mesi::SubjectRecord& out, CString& errorText);

	mesi::SubjectRecord m_record;
	bool m_isNew;

	// DDX members --------------------------------------------------------
	CString m_sSubjectId;
	double  m_dAge = 0.0;
	int     m_nCohortSel = kCohortAuto;
	CString m_sSex;
	CString m_sDiagnosis;
	CString m_sNotes;
	CString m_sT1Path, m_sT2Path, m_sAtlasPath;
	BOOL    m_bMeg = FALSE, m_bEeg = FALSE;
	CString m_sMegPath, m_sEegPath;
	double  m_dMegRateHz = 0.0, m_dEegRateHz = 0.0;
	CString m_sCoregMri, m_sTransform, m_sHeadModel;

	CComboBox m_cbCohort;
	CComboBox m_cbSex;
};
