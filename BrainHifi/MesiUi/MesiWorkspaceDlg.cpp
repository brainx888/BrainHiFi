// MesiWorkspaceDlg.cpp
#include "..\pch.h"
#include "..\framework.h"
#include "MesiWorkspaceDlg.h"
#include "SubjectDetailDlg.h"
#include "MesiStringUtil.h"

#include <afxdlgs.h>
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace mesi;
using namespace mesiui;

namespace {

constexpr LPCTSTR kExportFilter = _T("JSON results (*.json)|*.json|CSV table (*.csv)|*.csv|XML results (*.xml)|*.xml||");
constexpr LPCTSTR kModelFilter  = _T("Model files (*.onnx;*.engine;*.plan;*.pt;*.pth)|*.onnx;*.engine;*.plan;*.pt;*.pth|All files (*.*)|*.*||");

const RuntimeType kRuntimeOrder[] = {
	RuntimeType::Remote, RuntimeType::ONNX_CPU, RuntimeType::ONNX_CUDA,
	RuntimeType::ONNX_DirectML, RuntimeType::TensorRT, RuntimeType::CPU,
};

CString StateText(const SubjectRecord& s)
{
	CString t = FromUtf8(toString(s.state));
	if (s.state == ProcessingState::Failed && !s.lastError.empty())
		t += _T(": ") + FromUtf8(s.lastError);
	return t;
}

} // namespace

IMPLEMENT_DYNAMIC(CMesiWorkspaceDlg, CDialogEx)

CMesiWorkspaceDlg::CMesiWorkspaceDlg(std::shared_ptr<MESIManager> manager, CWnd* pParent)
	: CDialogEx(IDD_MESI_WORKSPACE_DLG, pParent)
	, m_manager(std::move(manager))
{
	ASSERT(m_manager);
}

CMesiWorkspaceDlg::~CMesiWorkspaceDlg()
{
	RemoveEventSink();
}

void CMesiWorkspaceDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MESI_SUBJECT_LIST, m_subjectList);
	DDX_Control(pDX, IDC_MESI_RESULT_LIST, m_resultList);
	DDX_Control(pDX, IDC_MESI_FILTER_COHORT, m_cbCohortFilter);
	DDX_Control(pDX, IDC_MESI_FILTER_MODALITY, m_cbModalityFilter);
	DDX_Control(pDX, IDC_MESI_AI_RUNTIME, m_cbRuntime);
	DDX_Control(pDX, IDC_MESI_PROGRESS, m_progress);
	DDX_Control(pDX, IDC_MESI_LOG, m_log);

	DDX_Text(pDX, IDC_MESI_FILTER_AGE_MIN, m_sAgeMin);
	DDX_Text(pDX, IDC_MESI_FILTER_AGE_MAX, m_sAgeMax);
	DDX_CBIndex(pDX, IDC_MESI_FILTER_COHORT, m_nCohortFilter);
	DDX_CBIndex(pDX, IDC_MESI_FILTER_MODALITY, m_nModalityFilter);

	DDX_Text(pDX, IDC_MESI_AI_MODEL_NAME, m_sModelName);
	DDX_Text(pDX, IDC_MESI_AI_MODEL_VERSION, m_sModelVersion);
	DDX_CBIndex(pDX, IDC_MESI_AI_RUNTIME, m_nRuntimeSel);
	DDX_Text(pDX, IDC_MESI_AI_MODEL_PATH, m_sModelPath);
	DDX_Text(pDX, IDC_MESI_AI_PROMPT, m_sPrompt);
	DDX_Text(pDX, IDC_MESI_AI_CONFIDENCE, m_dConfidence);
	DDV_MinMaxDouble(pDX, m_dConfidence, 0.0, 1.0);
	DDX_Text(pDX, IDC_MESI_AI_IOU, m_dIou);
	DDV_MinMaxDouble(pDX, m_dIou, 0.0, 1.0);
}

BEGIN_MESSAGE_MAP(CMesiWorkspaceDlg, CDialogEx)
	ON_WM_DESTROY()
	ON_MESSAGE(WM_MESI_EVENT, &CMesiWorkspaceDlg::OnMesiEvent)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_MESI_SUBJECT_LIST, &CMesiWorkspaceDlg::OnLvnItemChangedSubjects)
	ON_NOTIFY(NM_DBLCLK, IDC_MESI_SUBJECT_LIST, &CMesiWorkspaceDlg::OnNMDblClkSubjects)
	ON_BN_CLICKED(IDC_MESI_BTN_APPLY_FILTER, &CMesiWorkspaceDlg::OnBnClickedApplyFilter)
	ON_CBN_SELCHANGE(IDC_MESI_FILTER_COHORT, &CMesiWorkspaceDlg::OnBnClickedApplyFilter)
	ON_CBN_SELCHANGE(IDC_MESI_FILTER_MODALITY, &CMesiWorkspaceDlg::OnBnClickedApplyFilter)
	ON_BN_CLICKED(IDC_MESI_BTN_ADD_SUBJECT, &CMesiWorkspaceDlg::OnBnClickedAddSubject)
	ON_BN_CLICKED(IDC_MESI_BTN_EDIT_SUBJECT, &CMesiWorkspaceDlg::OnBnClickedEditSubject)
	ON_BN_CLICKED(IDC_MESI_BTN_REMOVE_SUBJECT, &CMesiWorkspaceDlg::OnBnClickedRemoveSubject)
	ON_BN_CLICKED(IDC_MESI_BTN_RUN_SEGMENTATION, &CMesiWorkspaceDlg::OnBnClickedRunSegmentation)
	ON_BN_CLICKED(IDC_MESI_BTN_COMPUTE_FUSION, &CMesiWorkspaceDlg::OnBnClickedComputeFusion)
	ON_BN_CLICKED(IDC_MESI_BTN_RUN_PIPELINE, &CMesiWorkspaceDlg::OnBnClickedRunPipeline)
	ON_BN_CLICKED(IDC_MESI_BTN_RUN_ALL, &CMesiWorkspaceDlg::OnBnClickedRunAll)
	ON_BN_CLICKED(IDC_MESI_BTN_CANCEL_JOBS, &CMesiWorkspaceDlg::OnBnClickedCancelJobs)
	ON_BN_CLICKED(IDC_MESI_BTN_EXPORT_RESULTS, &CMesiWorkspaceDlg::OnBnClickedExportResults)
	ON_BN_CLICKED(IDC_MESI_BTN_SAVE_WORKSPACE, &CMesiWorkspaceDlg::OnBnClickedSaveWorkspace)
	ON_BN_CLICKED(IDC_MESI_BTN_LOAD_WORKSPACE, &CMesiWorkspaceDlg::OnBnClickedLoadWorkspace)
	ON_BN_CLICKED(IDC_MESI_AI_BROWSE_MODEL, &CMesiWorkspaceDlg::OnBnClickedBrowseModel)
	ON_BN_CLICKED(IDC_MESI_AI_APPLY, &CMesiWorkspaceDlg::OnBnClickedApplyAiConfig)
	ON_BN_CLICKED(IDC_MESI_BTN_VERIFY_RESULT, &CMesiWorkspaceDlg::OnBnClickedVerifyResult)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

BOOL CMesiWorkspaceDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CString title;
	if (title.LoadString(IDS_MESI_WORKSPACE_TITLE)) SetWindowText(title);

	InitSubjectList();
	InitResultList();
	InitFilterCombos();
	InitRuntimeCombo();
	m_progress.SetRange(0, 100);
	m_log.SetLimitText(0);

	LoadAiConfigToUi();
	RefreshSubjectList();
	RefreshResultList();
	UpdateButtonStates();
	InstallEventSink();

	const auto seg = m_manager->segmentationEngine();
	const auto fus = m_manager->fusionEngine();
	CString line;
	line.Format(_T("Engines: segmentation=%s, fusion=%s, workers=%u"),
		seg ? FromUtf8(seg->name()).GetString() : _T("<none>"),
		fus ? FromUtf8(fus->name()).GetString() : _T("<none>"),
		static_cast<unsigned>(m_manager->options().workerThreads));
	AppendLog(line);
	return TRUE;
}

void CMesiWorkspaceDlg::InitSubjectList()
{
	m_subjectList.SetExtendedStyle(m_subjectList.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
	struct Col { LPCTSTR text; int width; int fmt; };
	const Col cols[colCount] = {
		{ _T("Subject ID"), 110, LVCFMT_LEFT },  { _T("Age"), 40, LVCFMT_RIGHT },
		{ _T("Cohort"), 60, LVCFMT_LEFT },       { _T("Modalities"), 70, LVCFMT_LEFT },
		{ _T("State"), 150, LVCFMT_LEFT },       { _T("Progress"), 55, LVCFMT_RIGHT },
		{ _T("Results"), 50, LVCFMT_RIGHT },     { _T("Modified (UTC)"), 120, LVCFMT_LEFT },
	};
	for (int i = 0; i < colCount; ++i)
		m_subjectList.InsertColumn(i, cols[i].text, cols[i].fmt, cols[i].width);
}

void CMesiWorkspaceDlg::InitResultList()
{
	m_resultList.SetExtendedStyle(m_resultList.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
	struct Col { LPCTSTR text; int width; int fmt; };
	const Col cols[rcolCount] = {
		{ _T("Result ID"), 120, LVCFMT_LEFT },   { _T("Method"), 90, LVCFMT_LEFT },
		{ _T("Modality"), 65, LVCFMT_LEFT },     { _T("Clusters"), 55, LVCFMT_RIGHT },
		{ _T("Dipoles"), 50, LVCFMT_RIGHT },     { _T("Peak (mm) / value"), 160, LVCFMT_LEFT },
		{ _T("Verification"), 110, LVCFMT_LEFT }, { _T("Created (UTC)"), 120, LVCFMT_LEFT },
	};
	for (int i = 0; i < rcolCount; ++i)
		m_resultList.InsertColumn(i, cols[i].text, cols[i].fmt, cols[i].width);
}

void CMesiWorkspaceDlg::InitFilterCombos()
{
	m_cbCohortFilter.ResetContent();
	for (LPCTSTR s : { _T("All ages"), _T("Pediatric (<18)"), _T("Adult (18+)") })
		m_cbCohortFilter.AddString(s);
	m_cbCohortFilter.SetCurSel(cfAll);

	m_cbModalityFilter.ResetContent();
	for (LPCTSTR s : { _T("All modalities"), _T("MEG only"), _T("EEG only"), _T("Combined MEG+EEG"), _T("Has MEG"), _T("Has EEG") })
		m_cbModalityFilter.AddString(s);
	m_cbModalityFilter.SetCurSel(mfAll);
}

void CMesiWorkspaceDlg::InitRuntimeCombo()
{
	m_cbRuntime.ResetContent();
	for (RuntimeType rt : kRuntimeOrder)
		m_cbRuntime.AddString(FromUtf8(toString(rt)));
}

// ---------------------------------------------------------------------------
// Event marshalling (worker thread -> GUI thread)
// ---------------------------------------------------------------------------

void CMesiWorkspaceDlg::InstallEventSink()
{
	const HWND hwnd = GetSafeHwnd();
	m_manager->setEventSink([hwnd](const MesiEvent& ev) {
		auto copy = std::make_unique<MesiEvent>(ev);
		if (::PostMessage(hwnd, WM_MESI_EVENT, 0, reinterpret_cast<LPARAM>(copy.get())))
			copy.release();                // ownership moves to the message queue
	});
}

void CMesiWorkspaceDlg::RemoveEventSink()
{
	if (m_manager) m_manager->setEventSink(nullptr);
}

void CMesiWorkspaceDlg::DrainPostedEvents()
{
	// Events already posted but not yet dispatched would leak after the
	// window is gone; pull them out of the queue and free them.
	MSG msg;
	while (::PeekMessage(&msg, GetSafeHwnd(), WM_MESI_EVENT, WM_MESI_EVENT, PM_REMOVE))
		delete reinterpret_cast<MesiEvent*>(msg.lParam);
}

LRESULT CMesiWorkspaceDlg::OnMesiEvent(WPARAM, LPARAM lParam)
{
	std::unique_ptr<MesiEvent> ev(reinterpret_cast<MesiEvent*>(lParam));
	if (!ev || m_closing) return 0;

	const CString subject = FromUtf8(ev->subjectId);
	switch (ev->type) {
	case MesiEvent::Type::SubjectsChanged:
		RefreshSubjectList(SelectedSubjectId());
		break;
	case MesiEvent::Type::JobQueued:
		AppendLog(_T("[") + subject + _T("] queued ") + FromUtf8(ev->kind));
		if (const int row = RowOfSubject(ev->subjectId); row >= 0)
			if (const auto s = m_manager->cohort().find(ev->subjectId)) RefreshSubjectRow(row, *s);
		break;
	case MesiEvent::Type::JobStarted:
		AppendLog(_T("[") + subject + _T("] started ") + FromUtf8(ev->kind));
		break;
	case MesiEvent::Type::JobProgress:
		if (const int row = RowOfSubject(ev->subjectId); row >= 0) {
			CString pct; pct.Format(_T("%d%%"), ev->percent);
			m_subjectList.SetItemText(row, colProgress, pct);
			if (!ev->message.empty()) m_subjectList.SetItemText(row, colState, FromUtf8(ev->message));
		}
		break;
	case MesiEvent::Type::JobCompleted:
		AppendLog(_T("[") + subject + _T("] completed ") + FromUtf8(ev->kind) + (ev->message.empty() ? CString() : _T(" - ") + FromUtf8(ev->message)));
		RefreshSubjectList(SelectedSubjectId());
		break;
	case MesiEvent::Type::JobFailed:
		AppendLog(_T("[") + subject + _T("] FAILED ") + FromUtf8(ev->kind) + _T(": ") + FromUtf8(ev->message));
		RefreshSubjectList(SelectedSubjectId());
		break;
	case MesiEvent::Type::JobCancelled:
		AppendLog(_T("[") + subject + _T("] cancelled ") + FromUtf8(ev->kind));
		RefreshSubjectList(SelectedSubjectId());
		break;
	case MesiEvent::Type::ResultsChanged:
		RefreshResultList();
		RefreshSubjectList(SelectedSubjectId());
		break;
	case MesiEvent::Type::ConfigChanged:
		LoadAiConfigToUi();
		break;
	}
	UpdateProgress(*ev);
	UpdateButtonStates();
	return 0;
}

void CMesiWorkspaceDlg::UpdateProgress(const MesiEvent& ev)
{
	const std::vector<JobInfo> active = m_manager->activeJobs();
	CString status;
	if (active.empty()) {
		m_progress.SetPos(0);
		status = _T("Idle");
	}
	else {
		if (ev.isJobEvent()) m_progress.SetPos(ev.percent);
		status.Format(_T("%u job(s) active - %s %s %d%% %s"), static_cast<unsigned>(active.size()),
			FromUtf8(ev.subjectId).GetString(), FromUtf8(ev.kind).GetString(), ev.percent, FromUtf8(ev.message).GetString());
	}
	SetDlgItemText(IDC_MESI_STATUS, status);
}

void CMesiWorkspaceDlg::AppendLog(const CString& line)
{
	if (!m_log.GetSafeHwnd()) return;
	SYSTEMTIME st; ::GetLocalTime(&st);
	CString stamped;
	stamped.Format(_T("%02d:%02d:%02d  %s\r\n"), st.wHour, st.wMinute, st.wSecond, line.GetString());
	const int len = m_log.GetWindowTextLength();
	m_log.SetSel(len, len);
	m_log.ReplaceSel(stamped);        // O(line) append, no full-text DDX round trip
}

// ---------------------------------------------------------------------------
// Subject list
// ---------------------------------------------------------------------------

std::vector<SubjectRecord> CMesiWorkspaceDlg::FilteredSubjects() const
{
	double ageMin = SubjectRecord::kMinAgeYears, ageMax = SubjectRecord::kMaxAgeYears;
	double v = 0.0;
	if (ParseDouble(m_sAgeMin, v)) ageMin = v;
	if (ParseDouble(m_sAgeMax, v)) ageMax = v;
	const int cohortSel = m_nCohortFilter;
	const int modSel = m_nModalityFilter;

	return m_manager->cohort().filter([&](const SubjectRecord& s) {
		if (s.ageYears < (std::min)(ageMin, ageMax) || s.ageYears > (std::max)(ageMin, ageMax)) return false;
		if (cohortSel == cfPediatric && !s.isPediatric()) return false;
		if (cohortSel == cfAdult && !s.isAdult()) return false;
		const Modality m = s.modality.modalities;
		switch (modSel) {
		case mfMegOnly:  return m == Modality::MEG;
		case mfEegOnly:  return m == Modality::EEG;
		case mfCombined: return m == Modality::Combined_MEG_EEG;
		case mfHasMeg:   return hasModality(m, Modality::MEG);
		case mfHasEeg:   return hasModality(m, Modality::EEG);
		default:         return true;
		}
	});
}

void CMesiWorkspaceDlg::RefreshSubjectRow(int row, const SubjectRecord& s)
{
	m_subjectList.SetItemText(row, colId, FromUtf8(s.subjectId));
	m_subjectList.SetItemText(row, colAge, FormatDouble(s.ageYears, 1));
	m_subjectList.SetItemText(row, colCohort, FromUtf8(toString(s.cohort)));
	m_subjectList.SetItemText(row, colModality, FromUtf8(s.modality.label()));
	m_subjectList.SetItemText(row, colState, StateText(s));
	CString progress;
	if (const auto jobs = m_manager->activeJobs(); !jobs.empty()) {
		for (const JobInfo& j : jobs)
			if (j.subjectId == s.subjectId) { progress.Format(_T("%d%%"), j.percent); break; }
	}
	else if (s.state == ProcessingState::Completed || s.state == ProcessingState::Segmented) {
		progress = _T("100%");
	}
	m_subjectList.SetItemText(row, colProgress, progress);
	CString nResults;
	nResults.Format(_T("%u"), static_cast<unsigned>(m_manager->resultsForSubject(s.subjectId).size()));
	m_subjectList.SetItemText(row, colResults, nResults);
	m_subjectList.SetItemText(row, colModified, FromUtf8(s.modifiedUtc));
}

void CMesiWorkspaceDlg::RefreshSubjectList(const std::string& keepSelection)
{
	const std::vector<SubjectRecord> subjects = FilteredSubjects();
	m_subjectList.SetRedraw(FALSE);
	m_subjectList.DeleteAllItems();
	m_rowIds.clear();
	m_rowIds.reserve(subjects.size());
	int selectRow = -1;
	for (const SubjectRecord& s : subjects) {
		const int row = m_subjectList.InsertItem(m_subjectList.GetItemCount(), FromUtf8(s.subjectId));
		RefreshSubjectRow(row, s);
		m_rowIds.push_back(s.subjectId);
		if (s.subjectId == keepSelection) selectRow = row;
	}
	if (selectRow >= 0) {
		m_subjectList.SetItemState(selectRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_subjectList.EnsureVisible(selectRow, FALSE);
	}
	m_subjectList.SetRedraw(TRUE);
	m_subjectList.Invalidate();
	RefreshStatistics();
}

void CMesiWorkspaceDlg::RefreshStatistics()
{
	const SubjectCohortManager::Statistics st = m_manager->cohort().statistics();
	CString text;
	text.Format(_T("%u subjects shown of %u  |  pediatric %u, adult %u  |  MEG %u, EEG %u, MEG+EEG %u  |  completed %u, failed %u, busy %u  |  age %.1f-%.1f (mean %.1f)"),
		static_cast<unsigned>(m_rowIds.size()), static_cast<unsigned>(st.total),
		static_cast<unsigned>(st.pediatric), static_cast<unsigned>(st.adult),
		static_cast<unsigned>(st.meg), static_cast<unsigned>(st.eeg), static_cast<unsigned>(st.combined),
		static_cast<unsigned>(st.completed), static_cast<unsigned>(st.failed), static_cast<unsigned>(st.busy),
		st.minAge, st.maxAge, st.meanAge);
	SetDlgItemText(IDC_MESI_STATS, text);
}

std::string CMesiWorkspaceDlg::SelectedSubjectId() const
{
	const int row = m_subjectList.GetNextItem(-1, LVNI_SELECTED);
	return (row >= 0 && static_cast<std::size_t>(row) < m_rowIds.size()) ? m_rowIds[static_cast<std::size_t>(row)] : std::string{};
}

std::string CMesiWorkspaceDlg::SelectedResultId() const
{
	const int row = m_resultList.GetNextItem(-1, LVNI_SELECTED);
	return (row >= 0 && static_cast<std::size_t>(row) < m_resultRowIds.size()) ? m_resultRowIds[static_cast<std::size_t>(row)] : std::string{};
}

int CMesiWorkspaceDlg::RowOfSubject(const std::string& subjectId) const
{
	const auto it = std::find(m_rowIds.begin(), m_rowIds.end(), subjectId);
	return it == m_rowIds.end() ? -1 : static_cast<int>(it - m_rowIds.begin());
}

void CMesiWorkspaceDlg::OnLvnItemChangedSubjects(NMHDR* pNMHDR, LRESULT* pResult)
{
	const auto* nm = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_SELECTED)) {
		RefreshResultList();
		UpdateButtonStates();
	}
	*pResult = 0;
}

void CMesiWorkspaceDlg::OnNMDblClkSubjects(NMHDR*, LRESULT* pResult)
{
	if (!SelectedSubjectId().empty()) OnBnClickedEditSubject();
	*pResult = 0;
}

void CMesiWorkspaceDlg::OnBnClickedApplyFilter()
{
	UpdateData(TRUE);
	RefreshSubjectList(SelectedSubjectId());
	UpdateButtonStates();
}

// ---------------------------------------------------------------------------
// Result list
// ---------------------------------------------------------------------------

void CMesiWorkspaceDlg::RefreshResultList()
{
	const std::string subjectId = SelectedSubjectId();
	const std::vector<MESIResultRecord> results = subjectId.empty() ? m_manager->results() : m_manager->resultsForSubject(subjectId);
	m_resultList.SetRedraw(FALSE);
	m_resultList.DeleteAllItems();
	m_resultRowIds.clear();
	for (const MESIResultRecord& r : results) {
		const int row = m_resultList.InsertItem(m_resultList.GetItemCount(), FromUtf8(r.resultId));
		m_resultList.SetItemText(row, rcolMethod, FromUtf8(r.method));
		m_resultList.SetItemText(row, rcolModality, FromUtf8(toString(r.modality)));
		CString n; n.Format(_T("%u"), static_cast<unsigned>(r.clusters.size()));
		m_resultList.SetItemText(row, rcolClusters, n);
		n.Format(_T("%u"), static_cast<unsigned>(r.dipoles.size()));
		m_resultList.SetItemText(row, rcolDipoles, n);
		if (const SourceCluster* c = r.strongestCluster()) {
			CString peak;
			peak.Format(_T("(%.1f, %.1f, %.1f)  %.3g"), c->peakMm.x, c->peakMm.y, c->peakMm.z, c->peakValue);
			m_resultList.SetItemText(row, rcolPeak, peak);
		}
		CString ver = FromUtf8(toString(r.verification));
		if (!r.verifiedBy.empty()) ver += _T(" (") + FromUtf8(r.verifiedBy) + _T(")");
		m_resultList.SetItemText(row, rcolVerification, ver);
		m_resultList.SetItemText(row, rcolCreated, FromUtf8(r.createdUtc));
		m_resultRowIds.push_back(r.resultId);
	}
	m_resultList.SetRedraw(TRUE);
	m_resultList.Invalidate();
}

void CMesiWorkspaceDlg::OnBnClickedVerifyResult()
{
	const std::string resultId = SelectedResultId();
	if (resultId.empty()) return;
	const auto r = m_manager->result(resultId);
	if (!r) return;
	const VerificationStatus next = r->isVerified() ? VerificationStatus::Rejected : VerificationStatus::ExpertVerified;
	CString question;
	question.Format(_T("Mark result %s as %s?"), FromUtf8(resultId).GetString(), FromUtf8(toString(next)).GetString());
	if (AfxMessageBox(question, MB_ICONQUESTION | MB_YESNO) != IDYES) return;
	TCHAR user[256] = {};
	DWORD len = _countof(user);
	::GetUserName(user, &len);
	if (!m_manager->setResultVerification(resultId, next, ToUtf8(CString(user))))
		AfxMessageBox(_T("The result no longer exists."), MB_ICONEXCLAMATION);
}

// ---------------------------------------------------------------------------
// Subject editing
// ---------------------------------------------------------------------------

void CMesiWorkspaceDlg::ReportError(const CString& context, const std::string& utf8Error)
{
	const CString text = context + _T("\r\n\r\n") + FromUtf8(utf8Error);
	AppendLog(context + _T(": ") + FromUtf8(utf8Error));
	AfxMessageBox(text, MB_ICONEXCLAMATION | MB_OK);
}

void CMesiWorkspaceDlg::OnBnClickedAddSubject()
{
	SubjectRecord fresh;
	fresh.modality.modalities = Modality::Combined_MEG_EEG;
	CSubjectDetailDlg dlg(fresh, true, this);
	if (dlg.DoModal() != IDOK) return;
	std::string err;
	if (!m_manager->cohort().add(dlg.record(), &err)) { ReportError(_T("Cannot add subject"), err); return; }
	RefreshSubjectList(dlg.record().subjectId);
	UpdateButtonStates();
}

void CMesiWorkspaceDlg::OnBnClickedEditSubject()
{
	const std::string id = SelectedSubjectId();
	std::optional<SubjectRecord> current;
	if (!id.empty()) current = m_manager->cohort().find(id);
	if (!current) { AfxMessageBox(IDS_MESI_NO_SELECTION, MB_ICONINFORMATION); return; }
	if (m_manager->isSubjectBusy(id)) { AfxMessageBox(IDS_MESI_JOBS_RUNNING, MB_ICONINFORMATION); return; }
	CSubjectDetailDlg dlg(*current, false, this);
	if (dlg.DoModal() != IDOK) return;
	std::string err;
	if (!m_manager->cohort().update(dlg.record(), &err)) { ReportError(_T("Cannot update subject"), err); return; }
	RefreshSubjectList(id);
}

void CMesiWorkspaceDlg::OnBnClickedRemoveSubject()
{
	const std::string id = SelectedSubjectId();
	if (id.empty()) { AfxMessageBox(IDS_MESI_NO_SELECTION, MB_ICONINFORMATION); return; }
	if (m_manager->isSubjectBusy(id)) { AfxMessageBox(IDS_MESI_JOBS_RUNNING, MB_ICONINFORMATION); return; }
	CString fmt, question;
	fmt.LoadString(IDS_MESI_CONFIRM_REMOVE);
	question.Format(fmt, FromUtf8(id).GetString());
	if (AfxMessageBox(question, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
	m_manager->removeResultsForSubject(id);
	m_manager->cohort().remove(id);
	RefreshSubjectList();
	RefreshResultList();
	UpdateButtonStates();
}

// ---------------------------------------------------------------------------
// Processing
// ---------------------------------------------------------------------------

void CMesiWorkspaceDlg::RunForSelected(Action action)
{
	const std::string id = SelectedSubjectId();
	if (id.empty()) { AfxMessageBox(IDS_MESI_NO_SELECTION, MB_ICONINFORMATION); return; }
	if (!ApplyAiConfigFromUi(true)) return;
	std::string err;
	JobId job = kInvalidJobId;
	LPCTSTR what = _T("");
	switch (action) {
	case Action::Segmentation: job = m_manager->runAISegmentationAsync(id, &err); what = _T("AI segmentation"); break;
	case Action::Fusion:       job = m_manager->computeMESIFusionAsync(id, &err); what = _T("MESI fusion"); break;
	case Action::Pipeline:     job = m_manager->runFullPipelineAsync(id, &err);   what = _T("full pipeline"); break;
	}
	if (job == kInvalidJobId) ReportError(CString(_T("Cannot start ")) + what, err);
	UpdateButtonStates();
}

void CMesiWorkspaceDlg::OnBnClickedRunSegmentation() { RunForSelected(Action::Segmentation); }
void CMesiWorkspaceDlg::OnBnClickedComputeFusion()   { RunForSelected(Action::Fusion); }
void CMesiWorkspaceDlg::OnBnClickedRunPipeline()     { RunForSelected(Action::Pipeline); }

void CMesiWorkspaceDlg::OnBnClickedRunAll()
{
	if (!ApplyAiConfigFromUi(true)) return;
	std::vector<std::string> ids;
	ids.reserve(m_rowIds.size());
	for (const std::string& id : m_rowIds)
		if (!m_manager->isSubjectBusy(id)) ids.push_back(id);
	if (ids.empty()) { AfxMessageBox(_T("No idle subjects in the current view."), MB_ICONINFORMATION); return; }
	CString question;
	question.Format(_T("Queue the full segmentation + fusion pipeline for %u subject(s) shown?"), static_cast<unsigned>(ids.size()));
	if (AfxMessageBox(question, MB_ICONQUESTION | MB_YESNO) != IDYES) return;
	std::vector<std::string> errors;
	const std::vector<JobId> queued = m_manager->runFullPipelineForAll(ids, &errors);
	CString line;
	line.Format(_T("Queued %u job(s), %u rejected"), static_cast<unsigned>(queued.size()), static_cast<unsigned>(errors.size()));
	AppendLog(line);
	for (const std::string& e : errors) AppendLog(_T("  ") + FromUtf8(e));
	UpdateButtonStates();
}

void CMesiWorkspaceDlg::OnBnClickedCancelJobs()
{
	const std::string id = SelectedSubjectId();
	bool cancelledSome = false;
	if (!id.empty() && m_manager->isSubjectBusy(id)) {
		for (const JobInfo& j : m_manager->activeJobs())
			if (j.subjectId == id) cancelledSome |= m_manager->cancelJob(j.id);
	}
	if (!cancelledSome) {
		if (m_manager->activeJobs().empty()) return;
		if (AfxMessageBox(_T("Cancel all queued and running jobs?"), MB_ICONQUESTION | MB_YESNO) != IDYES) return;
		m_manager->cancelAllJobs();
	}
	AppendLog(_T("Cancellation requested"));
	UpdateButtonStates();
}

void CMesiWorkspaceDlg::UpdateButtonStates()
{
	const std::string id = SelectedSubjectId();
	const bool hasSel = !id.empty();
	const bool busy = hasSel && m_manager->isSubjectBusy(id);
	const bool anyJobs = !m_manager->activeJobs().empty();
	std::optional<SubjectRecord> subject;
	if (hasSel) subject = m_manager->cohort().find(id);
	const bool segmented = subject && (subject->state == ProcessingState::Segmented || subject->state == ProcessingState::Completed);
	const bool haveSeg = static_cast<bool>(m_manager->segmentationEngine());
	const bool haveFus = static_cast<bool>(m_manager->fusionEngine());

	auto enable = [this](UINT ctrl, bool on) { if (CWnd* w = GetDlgItem(ctrl)) w->EnableWindow(on ? TRUE : FALSE); };
	enable(IDC_MESI_BTN_EDIT_SUBJECT, hasSel && !busy);
	enable(IDC_MESI_BTN_REMOVE_SUBJECT, hasSel && !busy);
	enable(IDC_MESI_BTN_RUN_SEGMENTATION, hasSel && !busy && haveSeg);
	enable(IDC_MESI_BTN_COMPUTE_FUSION, hasSel && !busy && haveFus && segmented);
	enable(IDC_MESI_BTN_RUN_PIPELINE, hasSel && !busy && haveSeg && haveFus);
	enable(IDC_MESI_BTN_RUN_ALL, !m_rowIds.empty() && haveSeg && haveFus);
	enable(IDC_MESI_BTN_CANCEL_JOBS, anyJobs);
	enable(IDC_MESI_BTN_LOAD_WORKSPACE, !anyJobs);
	enable(IDC_MESI_BTN_EXPORT_RESULTS, !m_manager->results().empty());
	enable(IDC_MESI_BTN_VERIFY_RESULT, !SelectedResultId().empty());
}

// ---------------------------------------------------------------------------
// AI configuration
// ---------------------------------------------------------------------------

void CMesiWorkspaceDlg::LoadAiConfigToUi()
{
	const AISegmentationConfig cfg = m_manager->aiConfig();
	m_sModelName = FromUtf8(cfg.modelName);
	m_sModelVersion = FromUtf8(cfg.modelVersion);
	m_sModelPath = FromUtf8(cfg.runtime == RuntimeType::Remote ? cfg.endpoint : cfg.modelPath);
	m_sPrompt = FromUtf8(cfg.promptText);
	m_dConfidence = cfg.confidenceThreshold;
	m_dIou = cfg.iouThreshold;
	m_nRuntimeSel = 0;
	for (int i = 0; i < static_cast<int>(_countof(kRuntimeOrder)); ++i)
		if (kRuntimeOrder[i] == cfg.runtime) m_nRuntimeSel = i;
	UpdateData(FALSE);
}

bool CMesiWorkspaceDlg::ApplyAiConfigFromUi(bool quiet)
{
	if (!UpdateData(TRUE)) return false;
	AISegmentationConfig cfg = m_manager->aiConfig();
	cfg.modelName = ToUtf8(m_sModelName.Trim());
	cfg.modelVersion = ToUtf8(m_sModelVersion.Trim());
	cfg.runtime = kRuntimeOrder[(std::max)(0, (std::min)(m_nRuntimeSel, static_cast<int>(_countof(kRuntimeOrder)) - 1))];
	if (cfg.runtime == RuntimeType::Remote) cfg.endpoint = ToUtf8(m_sModelPath.Trim());
	else cfg.modelPath = ToUtf8(m_sModelPath.Trim());
	cfg.promptText = ToUtf8(m_sPrompt.Trim());
	cfg.confidenceThreshold = m_dConfidence;
	cfg.iouThreshold = m_dIou;

	if (cfg == m_manager->aiConfig()) return true;
	const std::vector<std::string> errors = m_manager->setAiConfig(cfg);
	if (errors.empty()) {
		if (!quiet) AppendLog(_T("AI configuration applied: ") + FromUtf8(cfg.displayName()));
		return true;
	}
	CString text(_T("The AI segmentation configuration is invalid:\r\n"));
	for (const std::string& e : errors) text += _T("  - ") + FromUtf8(e) + _T("\r\n");
	AfxMessageBox(text, MB_ICONEXCLAMATION);
	return false;
}

void CMesiWorkspaceDlg::OnBnClickedApplyAiConfig() { ApplyAiConfigFromUi(false); }

void CMesiWorkspaceDlg::OnBnClickedBrowseModel()
{
	UpdateData(TRUE);
	if (kRuntimeOrder[(std::max)(0, m_nRuntimeSel)] == RuntimeType::Remote) {
		AfxMessageBox(_T("The Remote runtime uses an endpoint host name; type it into the path field."), MB_ICONINFORMATION);
		return;
	}
	CFileDialog dlg(TRUE, _T("onnx"), m_sModelPath.IsEmpty() ? nullptr : m_sModelPath.GetString(),
		OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY, kModelFilter, this);
	if (dlg.DoModal() == IDOK) {
		m_sModelPath = dlg.GetPathName();
		UpdateData(FALSE);
	}
}

// ---------------------------------------------------------------------------
// Export / workspace persistence
// ---------------------------------------------------------------------------

void CMesiWorkspaceDlg::OnBnClickedExportResults()
{
	if (m_manager->results().empty()) { AfxMessageBox(_T("There are no results to export yet."), MB_ICONINFORMATION); return; }
	std::string filter;
	const std::string selected = SelectedSubjectId();
	if (!selected.empty() && !m_manager->resultsForSubject(selected).empty()) {
		CString q;
		q.Format(_T("Export only the results of subject %s?\r\n\r\nYes = selected subject, No = whole cohort."), FromUtf8(selected).GetString());
		const int answer = AfxMessageBox(q, MB_ICONQUESTION | MB_YESNOCANCEL);
		if (answer == IDCANCEL) return;
		if (answer == IDYES) filter = selected;
	}
	CFileDialog dlg(FALSE, _T("json"), filter.empty() ? _T("mesi_results") : FromUtf8(filter + "_results").GetString(),
		OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, kExportFilter, this);
	if (dlg.DoModal() != IDOK) return;

	MESIManager::ExportFormat format = MESIManager::ExportFormat::Json;
	CString ext = dlg.GetFileExt(); ext.MakeLower();
	if (ext == _T("csv")) format = MESIManager::ExportFormat::Csv;
	else if (ext == _T("xml")) format = MESIManager::ExportFormat::Xml;
	else if (ext != _T("json")) {
		switch (dlg.m_ofn.nFilterIndex) {   // 1-based filter index when the user typed no extension
		case 2: format = MESIManager::ExportFormat::Csv; break;
		case 3: format = MESIManager::ExportFormat::Xml; break;
		default: break;
		}
	}
	CString path = dlg.GetPathName();
	if (ext.IsEmpty()) path += CString(_T(".")) + CString(MESIManager::exportExtension(format));

	std::string err;
	if (!m_manager->exportResults(ToUtf8(path), format, filter, &err)) { ReportError(_T("Export failed"), err); return; }
	AppendLog(_T("Exported results to ") + path);
}

void CMesiWorkspaceDlg::OnBnClickedSaveWorkspace()
{
	CFolderPickerDialog dlg(nullptr, OFN_PATHMUSTEXIST, this);
	dlg.m_ofn.lpstrTitle = _T("Choose the workspace folder (subjects.json, results.json, ai_config.json)");
	if (dlg.DoModal() != IDOK) return;
	std::string err;
	if (!m_manager->saveWorkspace(ToUtf8(dlg.GetPathName()), &err)) { ReportError(_T("Save workspace failed"), err); return; }
	AppendLog(_T("Workspace saved to ") + dlg.GetPathName());
}

void CMesiWorkspaceDlg::OnBnClickedLoadWorkspace()
{
	if (!m_manager->activeJobs().empty()) { AfxMessageBox(IDS_MESI_JOBS_RUNNING, MB_ICONINFORMATION); return; }
	if (m_manager->cohort().size() > 0 &&
		AfxMessageBox(_T("Loading replaces the current cohort and results. Continue?"), MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
		return;
	CFolderPickerDialog dlg(nullptr, OFN_PATHMUSTEXIST, this);
	dlg.m_ofn.lpstrTitle = _T("Choose a workspace folder");
	if (dlg.DoModal() != IDOK) return;
	std::string err;
	const bool ok = m_manager->loadWorkspace(ToUtf8(dlg.GetPathName()), &err);
	if (!ok) ReportError(_T("Load workspace failed"), err);
	else if (!err.empty()) AppendLog(_T("Loaded with warnings: ") + FromUtf8(err));
	else AppendLog(_T("Workspace loaded from ") + dlg.GetPathName());
	LoadAiConfigToUi();
	RefreshSubjectList();
	RefreshResultList();
	UpdateButtonStates();
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void CMesiWorkspaceDlg::OnCancel()
{
	if (!m_manager->activeJobs().empty()) {
		const int answer = AfxMessageBox(
			_T("Jobs are still running. They continue in the background if you close the workspace.\r\n\r\n")
			_T("Yes = close and keep running, No = cancel jobs and close, Cancel = stay."),
			MB_ICONQUESTION | MB_YESNOCANCEL);
		if (answer == IDCANCEL) return;
		if (answer == IDNO) m_manager->cancelAllJobs();
	}
	m_closing = true;
	CDialogEx::OnCancel();
}

void CMesiWorkspaceDlg::OnDestroy()
{
	m_closing = true;
	RemoveEventSink();
	DrainPostedEvents();
	CDialogEx::OnDestroy();
}
