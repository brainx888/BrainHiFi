// SubjectDetailDlg.cpp
#include "..\pch.h"
#include "..\framework.h"
#include "SubjectDetailDlg.h"
#include "MesiStringUtil.h"

#include <afxdlgs.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace mesi;
using namespace mesiui;

namespace {

constexpr LPCTSTR kMriFilter   = _T("MRI volumes (*.nii;*.nii.gz;*.mgz;*.img;*.dcm)|*.nii;*.nii.gz;*.mgz;*.img;*.dcm|All files (*.*)|*.*||");
constexpr LPCTSTR kMegFilter   = _T("MEG data (*.fif;*.ds;*.sqd;*.con;*.meg4;*.bin)|*.fif;*.ds;*.sqd;*.con;*.meg4;*.bin|All files (*.*)|*.*||");
constexpr LPCTSTR kEegFilter   = _T("EEG data (*.edf;*.bdf;*.vhdr;*.set;*.cnt)|*.edf;*.bdf;*.vhdr;*.set;*.cnt|All files (*.*)|*.*||");
constexpr LPCTSTR kXfmFilter   = _T("Transforms (*.txt;*.mat;*.xfm;*.trans;*.fif)|*.txt;*.mat;*.xfm;*.trans;*.fif|All files (*.*)|*.*||");
constexpr LPCTSTR kHeadFilter  = _T("Head models (*.fif;*.bem;*.mat;*.json)|*.fif;*.bem;*.mat;*.json|All files (*.*)|*.*||");
constexpr LPCTSTR kAtlasFilter = _T("Atlas / labels (*.nii;*.nii.gz;*.annot;*.txt)|*.nii;*.nii.gz;*.annot;*.txt|All files (*.*)|*.*||");

} // namespace

IMPLEMENT_DYNAMIC(CSubjectDetailDlg, CDialogEx)

CSubjectDetailDlg::CSubjectDetailDlg(const SubjectRecord& record, bool isNew, CWnd* pParent)
	: CDialogEx(IDD_SUBJECT_DETAIL_DLG, pParent)
	, m_record(record)
	, m_isNew(isNew)
{
	m_sSubjectId = FromUtf8(record.subjectId);
	m_dAge       = record.ageYears;
	m_nCohortSel = record.cohort == CohortGroup::Pediatric ? kCohortPediatric
	             : record.cohort == CohortGroup::Adult     ? kCohortAdult
	             : kCohortAuto;
	m_sSex       = FromUtf8(record.sex);
	m_sDiagnosis = FromUtf8(record.diagnosis);
	m_sNotes     = FromUtf8(record.clinicalNotes);
	m_sNotes.Replace(_T("\r\n"), _T("\n"));
	m_sNotes.Replace(_T("\n"), _T("\r\n"));
	m_sT1Path    = FromUtf8(record.t1MriPath);
	m_sT2Path    = FromUtf8(record.t2MriPath);
	m_sAtlasPath = FromUtf8(record.atlasPath);

	const ModalityConfig& mc = record.modality;
	m_bMeg        = mc.hasMEG() ? TRUE : FALSE;
	m_bEeg        = mc.hasEEG() ? TRUE : FALSE;
	m_sMegPath    = FromUtf8(mc.megDataPath);
	m_sEegPath    = FromUtf8(mc.eegDataPath);
	m_dMegRateHz  = mc.megSamplingRateHz;
	m_dEegRateHz  = mc.eegSamplingRateHz;
	m_sCoregMri   = FromUtf8(mc.coregisteredMriPath);
	m_sTransform  = FromUtf8(mc.coregistrationTransformPath);
	m_sHeadModel  = FromUtf8(mc.headModelPath);
}

void CSubjectDetailDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SUBJ_COHORT, m_cbCohort);
	DDX_Control(pDX, IDC_SUBJ_SEX, m_cbSex);

	DDX_Text(pDX, IDC_SUBJ_ID, m_sSubjectId);
	DDV_MaxChars(pDX, m_sSubjectId, static_cast<int>(SubjectRecord::kMaxIdLength));
	DDX_Text(pDX, IDC_SUBJ_AGE, m_dAge);
	DDV_MinMaxDouble(pDX, m_dAge, SubjectRecord::kMinAgeYears, SubjectRecord::kMaxAgeYears);
	DDX_CBIndex(pDX, IDC_SUBJ_COHORT, m_nCohortSel);
	DDX_CBString(pDX, IDC_SUBJ_SEX, m_sSex);
	DDX_Text(pDX, IDC_SUBJ_DIAGNOSIS, m_sDiagnosis);
	DDX_Text(pDX, IDC_SUBJ_NOTES, m_sNotes);

	DDX_Text(pDX, IDC_SUBJ_T1_PATH, m_sT1Path);
	DDX_Text(pDX, IDC_SUBJ_T2_PATH, m_sT2Path);
	DDX_Text(pDX, IDC_SUBJ_ATLAS_PATH, m_sAtlasPath);

	DDX_Check(pDX, IDC_SUBJ_CHK_MEG, m_bMeg);
	DDX_Check(pDX, IDC_SUBJ_CHK_EEG, m_bEeg);
	DDX_Text(pDX, IDC_SUBJ_MEG_PATH, m_sMegPath);
	DDX_Text(pDX, IDC_SUBJ_EEG_PATH, m_sEegPath);
	DDX_Text(pDX, IDC_SUBJ_MEG_RATE, m_dMegRateHz);
	DDV_MinMaxDouble(pDX, m_dMegRateHz, 0.0, 100000.0);
	DDX_Text(pDX, IDC_SUBJ_EEG_RATE, m_dEegRateHz);
	DDV_MinMaxDouble(pDX, m_dEegRateHz, 0.0, 100000.0);
	DDX_Text(pDX, IDC_SUBJ_COREG_MRI, m_sCoregMri);
	DDX_Text(pDX, IDC_SUBJ_TRANSFORM, m_sTransform);
	DDX_Text(pDX, IDC_SUBJ_HEADMODEL, m_sHeadModel);
}

BEGIN_MESSAGE_MAP(CSubjectDetailDlg, CDialogEx)
	ON_EN_CHANGE(IDC_SUBJ_AGE, &CSubjectDetailDlg::OnEnChangeAge)
	ON_CBN_SELCHANGE(IDC_SUBJ_COHORT, &CSubjectDetailDlg::OnCbnSelChangeCohort)
	ON_BN_CLICKED(IDC_SUBJ_CHK_MEG, &CSubjectDetailDlg::OnBnClickedModalityToggle)
	ON_BN_CLICKED(IDC_SUBJ_CHK_EEG, &CSubjectDetailDlg::OnBnClickedModalityToggle)
	ON_BN_CLICKED(IDC_SUBJ_T1_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseT1)
	ON_BN_CLICKED(IDC_SUBJ_T2_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseT2)
	ON_BN_CLICKED(IDC_SUBJ_ATLAS_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseAtlas)
	ON_BN_CLICKED(IDC_SUBJ_MEG_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseMeg)
	ON_BN_CLICKED(IDC_SUBJ_EEG_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseEeg)
	ON_BN_CLICKED(IDC_SUBJ_COREG_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseCoreg)
	ON_BN_CLICKED(IDC_SUBJ_TRANSFORM_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseTransform)
	ON_BN_CLICKED(IDC_SUBJ_HEADMODEL_BROWSE, &CSubjectDetailDlg::OnBnClickedBrowseHeadModel)
END_MESSAGE_MAP()

BOOL CSubjectDetailDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	m_cbCohort.ResetContent();
	m_cbCohort.AddString(_T("Auto (from age)"));
	m_cbCohort.AddString(FromUtf8(toString(CohortGroup::Pediatric)));
	m_cbCohort.AddString(FromUtf8(toString(CohortGroup::Adult)));
	m_cbCohort.SetCurSel(m_nCohortSel);

	m_cbSex.ResetContent();
	for (LPCTSTR s : { _T(""), _T("F"), _T("M"), _T("Other") })
		m_cbSex.AddString(s);
	if (m_cbSex.SelectString(-1, m_sSex) == CB_ERR)
		m_cbSex.SetWindowText(m_sSex);

	SetWindowText(m_isNew ? CString(_T("Add Subject")) : CString(_T("Edit Subject - ")) + m_sSubjectId);
	if (!m_isNew)
		if (CEdit* id = static_cast<CEdit*>(GetDlgItem(IDC_SUBJ_ID))) id->SetReadOnly(TRUE);

	CString state;
	state.Format(_T("State: %s"), FromUtf8(toString(m_record.state)).GetString());
	if (!m_record.lastError.empty())
		state += _T("  -  ") + FromUtf8(m_record.lastError);
	SetDlgItemText(IDC_SUBJ_STATE, state);

	UpdateDerivedLabels();
	UpdateModalityControls();
	if (m_isNew) GotoDlgCtrl(GetDlgItem(IDC_SUBJ_ID));
	return FALSE;   // keep focus set above
}

void CSubjectDetailDlg::UpdateDerivedLabels()
{
	CString ageText;
	GetDlgItemText(IDC_SUBJ_AGE, ageText);
	double age = 0.0;
	CString summary;
	if (ParseDouble(ageText, age) && age >= SubjectRecord::kMinAgeYears && age <= SubjectRecord::kMaxAgeYears) {
		const int sel = m_cbCohort.GetCurSel();
		const CohortGroup derived = cohortFromAge(age);
		const CohortGroup chosen = sel == kCohortPediatric ? CohortGroup::Pediatric
		                         : sel == kCohortAdult     ? CohortGroup::Adult : derived;
		summary.Format(_T("Cohort from age: %s%s"), FromUtf8(toString(derived)).GetString(),
			chosen != derived ? _T("  (manual override differs - will be rejected)") : _T(""));
	}
	else {
		summary.Format(_T("Age must be %.0f-%.0f years"), SubjectRecord::kMinAgeYears, SubjectRecord::kMaxAgeYears);
	}
	SetDlgItemText(IDC_SUBJ_MODALITY_SUMMARY, summary);
}

void CSubjectDetailDlg::UpdateModalityControls()
{
	const bool meg = IsDlgButtonChecked(IDC_SUBJ_CHK_MEG) == BST_CHECKED;
	const bool eeg = IsDlgButtonChecked(IDC_SUBJ_CHK_EEG) == BST_CHECKED;
	for (UINT id : { IDC_SUBJ_MEG_PATH, IDC_SUBJ_MEG_BROWSE, IDC_SUBJ_MEG_RATE })
		if (CWnd* w = GetDlgItem(id)) w->EnableWindow(meg);
	for (UINT id : { IDC_SUBJ_EEG_PATH, IDC_SUBJ_EEG_BROWSE, IDC_SUBJ_EEG_RATE })
		if (CWnd* w = GetDlgItem(id)) w->EnableWindow(eeg);
}

void CSubjectDetailDlg::OnEnChangeAge()           { UpdateDerivedLabels(); }
void CSubjectDetailDlg::OnCbnSelChangeCohort()    { UpdateDerivedLabels(); }
void CSubjectDetailDlg::OnBnClickedModalityToggle() { UpdateModalityControls(); }

void CSubjectDetailDlg::BrowseInto(UINT editId, LPCTSTR filter, LPCTSTR defaultExt)
{
	CString current;
	GetDlgItemText(editId, current);
	CFileDialog dlg(TRUE, defaultExt, current.IsEmpty() ? nullptr : current.GetString(),
		OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY, filter, this);
	if (dlg.DoModal() == IDOK)
		SetDlgItemText(editId, dlg.GetPathName());
}

void CSubjectDetailDlg::OnBnClickedBrowseT1()        { BrowseInto(IDC_SUBJ_T1_PATH, kMriFilter, _T("nii")); }
void CSubjectDetailDlg::OnBnClickedBrowseT2()        { BrowseInto(IDC_SUBJ_T2_PATH, kMriFilter, _T("nii")); }
void CSubjectDetailDlg::OnBnClickedBrowseAtlas()     { BrowseInto(IDC_SUBJ_ATLAS_PATH, kAtlasFilter, _T("nii")); }
void CSubjectDetailDlg::OnBnClickedBrowseMeg()       { BrowseInto(IDC_SUBJ_MEG_PATH, kMegFilter, _T("fif")); }
void CSubjectDetailDlg::OnBnClickedBrowseEeg()       { BrowseInto(IDC_SUBJ_EEG_PATH, kEegFilter, _T("edf")); }
void CSubjectDetailDlg::OnBnClickedBrowseCoreg()     { BrowseInto(IDC_SUBJ_COREG_MRI, kMriFilter, _T("nii")); }
void CSubjectDetailDlg::OnBnClickedBrowseTransform() { BrowseInto(IDC_SUBJ_TRANSFORM, kXfmFilter, _T("txt")); }
void CSubjectDetailDlg::OnBnClickedBrowseHeadModel() { BrowseInto(IDC_SUBJ_HEADMODEL, kHeadFilter, _T("fif")); }

bool CSubjectDetailDlg::CollectRecord(SubjectRecord& out, CString& errorText)
{
	out = m_record;                           // keeps state / timestamps / lastError
	out.subjectId = ToUtf8(m_sSubjectId.Trim());
	out.ageYears  = m_dAge;
	switch (m_nCohortSel) {
	case kCohortPediatric: out.cohort = CohortGroup::Pediatric; break;
	case kCohortAdult:     out.cohort = CohortGroup::Adult;     break;
	default:               out.deriveCohortFromAge();           break;
	}
	out.sex           = ToUtf8(m_sSex.Trim());
	out.diagnosis     = ToUtf8(m_sDiagnosis.Trim());
	CString notes(m_sNotes);
	notes.Replace(_T("\r\n"), _T("\n"));
	out.clinicalNotes = ToUtf8(notes);
	out.t1MriPath     = ToUtf8(m_sT1Path.Trim());
	out.t2MriPath     = ToUtf8(m_sT2Path.Trim());
	out.atlasPath     = ToUtf8(m_sAtlasPath.Trim());

	ModalityConfig& mc = out.modality;
	mc.modalities = Modality::None;
	if (m_bMeg) mc.modalities |= Modality::MEG;
	if (m_bEeg) mc.modalities |= Modality::EEG;
	mc.megDataPath = m_bMeg ? ToUtf8(m_sMegPath.Trim()) : std::string{};
	mc.eegDataPath = m_bEeg ? ToUtf8(m_sEegPath.Trim()) : std::string{};
	mc.megSamplingRateHz = m_bMeg ? m_dMegRateHz : 0.0;
	mc.eegSamplingRateHz = m_bEeg ? m_dEegRateHz : 0.0;
	mc.coregisteredMriPath = ToUtf8(m_sCoregMri.Trim());
	mc.coregistrationTransformPath = ToUtf8(m_sTransform.Trim());
	mc.headModelPath = ToUtf8(m_sHeadModel.Trim());
	out.touch();

	const std::vector<std::string> errors = out.validate();
	if (errors.empty()) return true;
	errorText = _T("The subject cannot be saved:\r\n");
	for (const std::string& e : errors)
		errorText += _T("  - ") + FromUtf8(e) + _T("\r\n");
	return false;
}

void CSubjectDetailDlg::OnOK()
{
	if (!UpdateData(TRUE))       // DDV failures already showed their message
		return;
	SubjectRecord candidate;
	CString error;
	if (!CollectRecord(candidate, error)) {
		AfxMessageBox(error, MB_ICONEXCLAMATION | MB_OK);
		return;
	}
	// Modality paths are only required for processing, not for saving the
	// record - but warn so the user is not surprised later.
	const std::vector<std::string> modalityErrors = candidate.modality.validate();
	if (!modalityErrors.empty()) {
		CString warn(_T("The modality configuration is incomplete; processing will be refused until it is fixed:\r\n"));
		for (const std::string& e : modalityErrors) warn += _T("  - ") + FromUtf8(e) + _T("\r\n");
		warn += _T("\r\nSave anyway?");
		if (AfxMessageBox(warn, MB_ICONQUESTION | MB_YESNO) != IDYES)
			return;
	}
	m_record = std::move(candidate);
	CDialogEx::OnOK();
}
