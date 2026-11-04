// BrainHifiDlg.cpp : implementation file
#include "pch.h"
#include "framework.h"
#include "BrainHifi.h"
#include "BrainHifiDlg.h"
#include "DlgProxy.h"
#include "CDlgAbout.h"
#include "MesiUi/MesiWorkspaceDlg.h"
#include "MesiUi/MesiStringUtil.h"
#include "afxdialogex.h"
#include "../BrainHifiViewDll/CBhMsiAiAnalyst.h"
#include <chrono>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace mesi;
using namespace mesiui;

IMPLEMENT_DYNAMIC(CBrainHifiDlg, CDialogEx);

CBrainHifiDlg::CBrainHifiDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_BRAINHIFI_DIALOG, pParent)
	, m_sPrompt4Sam(_T("the bright colored activation clusters overlaid on the brain"))
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CBrainHifiDlg::~CBrainHifiDlg()
{
	// If there is an automation proxy for this dialog, tell it the dialog is gone.
	if (m_pAutoProxy != nullptr)
		m_pAutoProxy->m_pDialog = nullptr;
	// unique_ptr / shared_ptr members release the viewer, job runner (joins
	// its worker) and manager in reverse declaration order.
}

void CBrainHifiDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PROGRESS_SAM3, m_progressSam);
	DDX_Text(pDX, IDC_EDIT_PROMPT_SAM3, m_sPrompt4Sam);
	DDV_MaxChars(pDX, m_sPrompt4Sam, 2000);
	DDX_Text(pDX, IDC_EDIT_SEGMENT_MESSAGE, m_sImageAnalysisMessage);
}

BEGIN_MESSAGE_MAP(CBrainHifiDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_MESSAGE(WM_SAM3_PROGRESS, &CBrainHifiDlg::OnSam3Progress)
	ON_BN_CLICKED(IDC_BNT_CONTACT, &CBrainHifiDlg::OnBnClickedBntContact)
	ON_BN_CLICKED(IDC_BNT_MSI_VIEWER, &CBrainHifiDlg::OnBnClickedBntMsiViewer)
	ON_BN_CLICKED(IDC_BNT_MESI_WORKSPACE, &CBrainHifiDlg::OnBnClickedBntMesiWorkspace)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// Set-up
// ---------------------------------------------------------------------------

BOOL CBrainHifiDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Add "About..." menu item to system menu.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);
	if (CMenu* pSysMenu = GetSystemMenu(FALSE)) {
		CString strAboutMenu;
		if (strAboutMenu.LoadString(IDS_ABOUTBOX) && !strAboutMenu.IsEmpty()) {
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);
	m_progressSam.SetRange(0, 100);

	CreateMesiManager();

	OnBnClickedBntMsiViewer();

	return TRUE;
}

void CBrainHifiDlg::CreateMesiManager()
{
	MESIManager::Options opt;
	opt.workerThreads = 1;                       // one remote / GPU session at a time

	TCHAR exePath[MAX_PATH] = {};
	::GetModuleFileName(nullptr, exePath, MAX_PATH);
	CString outDir(exePath);
	const int slash = outDir.ReverseFind(_T('\\'));
	if (slash >= 0) outDir = outDir.Left(slash);
	outDir += _T("\\MesiOutput");
	::CreateDirectory(outDir, nullptr);
	opt.outputDirectory = ToUtf8(outDir);

	m_pMesiManager = std::make_shared<MESIManager>(opt);

	// Real remote SAM 3 when a key is available, otherwise the deterministic
	// simulator so the workflow can be exercised offline.
	if (!CMetaSam3SegmentationEngine::ApiKeyFromEnvironment().IsEmpty())
		m_pMesiManager->setSegmentationEngine(std::make_shared<CMetaSam3SegmentationEngine>());
	else {
		m_pMesiManager->setSegmentationEngine(std::make_shared<SimulatedSegmentationEngine>(20));
		CString warn;
		warn.LoadString(IDS_MESI_API_KEY_MISSING);
		AppendMessage(warn);
	}
	m_pMesiManager->setFusionEngine(std::make_shared<SimulatedFusionEngine>(20));

	AISegmentationConfig cfg = m_pMesiManager->aiConfig();
	cfg.promptText = ToUtf8(m_sPrompt4Sam);
	m_pMesiManager->setAiConfig(cfg);

	m_pImageJobs = std::make_unique<MesiJobRunner>(1);
}

// ---------------------------------------------------------------------------
// Standard dialog plumbing
// ---------------------------------------------------------------------------

void CBrainHifiDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX) {
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else {
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

void CBrainHifiDlg::OnPaint()
{
	if (IsIconic()) {
		CPaintDC dc(this);
		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
		const int cxIcon = GetSystemMetrics(SM_CXICON);
		const int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		const int x = (rect.Width() - cxIcon + 1) / 2;
		const int y = (rect.Height() - cyIcon + 1) / 2;
		dc.DrawIcon(x, y, m_hIcon);
	}
	else {
		CDialogEx::OnPaint();
	}
}

HCURSOR CBrainHifiDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CBrainHifiDlg::OnClose()  { if (CanExit()) CDialogEx::OnClose(); }
void CBrainHifiDlg::OnOK()     { if (CanExit()) CDialogEx::OnOK(); }
void CBrainHifiDlg::OnCancel() { if (CanExit()) CDialogEx::OnCancel(); }

BOOL CBrainHifiDlg::CanExit()
{
	// If the proxy object is still around, the automation controller still
	// holds the application: hide instead of closing.
	if (m_pAutoProxy != nullptr) {
		ShowWindow(SW_HIDE);
		return FALSE;
	}
	const bool imageJobs = m_pImageJobs && m_pImageJobs->activeCount() > 0;
	const bool mesiJobs = m_pMesiManager && !m_pMesiManager->activeJobs().empty();
	if (imageJobs || mesiJobs) {
		if (AfxMessageBox(_T("Background processing is still running. Cancel it and exit?"), MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
			return FALSE;
		if (m_pImageJobs) m_pImageJobs->cancelAll();
		if (m_pMesiManager) m_pMesiManager->cancelAllJobs();
	}
	return TRUE;
}

void CBrainHifiDlg::OnDestroy()
{
	// Stop callbacks before the window handle disappears, then drain any
	// progress messages that were already posted.
	if (m_pImageJobs) { m_pImageJobs->setObserver(nullptr); m_pImageJobs->cancelAll(); m_pImageJobs->waitIdleFor(std::chrono::seconds(5)); }
	if (m_pMesiManager) m_pMesiManager->setEventSink(nullptr);
	MSG msg;
	while (::PeekMessage(&msg, GetSafeHwnd(), WM_SAM3_PROGRESS, WM_SAM3_PROGRESS, PM_REMOVE))
		delete reinterpret_cast<CString*>(msg.lParam);
	CDialogEx::OnDestroy();
}

void CBrainHifiDlg::AppendMessage(const CString& line)
{
	if (!m_sImageAnalysisMessage.IsEmpty() && m_sImageAnalysisMessage.Right(2) != _T("\r\n"))
		m_sImageAnalysisMessage += _T("\r\n");
	m_sImageAnalysisMessage += line + _T("\r\n");
	if (GetSafeHwnd()) {
		SetDlgItemText(IDC_EDIT_SEGMENT_MESSAGE, m_sImageAnalysisMessage);
		if (CEdit* e = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_SEGMENT_MESSAGE)))
			e->LineScroll(e->GetLineCount());
	}
}

LRESULT CBrainHifiDlg::OnSam3Progress(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<CString> line(reinterpret_cast<CString*>(lParam));
	const int percent = static_cast<int>(wParam);
	if (percent >= 0) m_progressSam.SetPos((std::min)(percent, 100));
	if (line && !line->IsEmpty()) AppendMessage(*line);
	if (percent >= 100 && GetDlgItem(IDC_BNT_CONTACT)) GetDlgItem(IDC_BNT_CONTACT)->EnableWindow(TRUE);
	return 0;
}


void CBrainHifiDlg::OnBnClickedBntContact()
{
	if (!UpdateData(TRUE)) return;

	if (!m_pImageJobs) {
		AfxMessageBox(_T("The image job runner is not initialized."), MB_ICONEXCLAMATION);
		return;
	}

	if (m_pImageJobs->activeCount() > 0) {
		AfxMessageBox(_T("A segmentation batch is already running."), MB_ICONINFORMATION);
		return;
	}

	const DWORD kMaxFiles = 256;
	const DWORD kMaxPathLen = MAX_PATH + 1;
	std::vector<TCHAR> fileBuffer(kMaxFiles * kMaxPathLen, 0);

	CFileDialog dlg(
		TRUE,
		_T("png"),
		nullptr,
		OFN_FILEMUSTEXIST |
		OFN_PATHMUSTEXIST |
		OFN_ALLOWMULTISELECT |
		OFN_HIDEREADONLY |
		OFN_EXPLORER,
		_T("Image Files (*.png; *.jpg; *.jpeg; *.bmp; *.gif; *.tif; *.tiff)|")
		_T("*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|")
		_T("All Files (*.*)|*.*||"),
		this);

	dlg.m_ofn.lpstrFile = fileBuffer.data();
	dlg.m_ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());

	if (dlg.DoModal() != IDOK) return;

	SBhMsiAiJob job;
	job.eTask = EBhMsiAiTask::ImageSegmentation;
	job.bUseSam = true;
	job.sInstruction = ToUtf8(m_sPrompt4Sam);

	POSITION pos = dlg.GetStartPosition();
	while (pos != nullptr) {
		job.vImageFiles.push_back(dlg.GetNextPathName(pos));
	}

	if (job.vImageFiles.empty()) return;

	const HWND hwnd = GetSafeHwnd();
	auto post = [hwnd](int percent, const CString& text) {
		auto line = std::make_unique<CString>(text);
		if (::PostMessage(hwnd, WM_SAM3_PROGRESS,
			static_cast<WPARAM>(percent),
			reinterpret_cast<LPARAM>(line.get()))) {
			line.release();
		}
		};

	if (CWnd* button = GetDlgItem(IDC_BNT_CONTACT))
		button->EnableWindow(FALSE);

	m_progressSam.SetPos(10);

	CString start;
	start.Format(_T("Segmenting %u image(s) with Meta SAM..."),
		static_cast<unsigned>(job.vImageFiles.size()));
	AppendMessage(start);

	try {
		m_pImageJobs->submit("sam3-batch", "images",
			[job = std::move(job), post](const CancellationToken& cancel, const ProgressFn&) {
				int success = 0;
				int failed = 0;

				try {
					SBhMsiAiJob imageJob = job;
					imageJob.vImageFiles.clear();
					imageJob.vImageFiles.reserve(1);

					for (std::size_t i = 0; i < job.vImageFiles.size(); ++i) {
						cancel.throwIfCancelled();

						imageJob.vImageFiles.clear();
						imageJob.vImageFiles.push_back(job.vImageFiles[i]);

						const SBhMsiAiResult result = CBhMsiAiAnalyst::runFile(imageJob);

						CString line;
						if (result.bOk) {
							++success;
							line.Format(_T("OK  %s"), job.vImageFiles[i].GetString());
						}
						else {
							++failed;
							line.Format(_T("FAILED  %s"), job.vImageFiles[i].GetString());
						}

						if (!result.sText.IsEmpty())
							line += _T("\r\n") + result.sText;

						if (!result.sError.IsEmpty())
							line += _T("\r\n") + result.sError;

						post(static_cast<int>((i + 1) * 99 / job.vImageFiles.size()), line);
						cancel.throwIfCancelled();
					}

					CString summary;
					summary.Format(
						_T("Batch processing complete.  Success: %d   Failed: %d"),
						success, failed);
					post(100, summary);
				}
				catch (const CancellationToken::OperationCancelled&) {
					post(100, _T("Batch cancelled."));
				}
				catch (...) {
					post(100, _T("Batch stopped because of an unexpected processing error."));
				}
			});
	}
	catch (...) {
		if (CWnd* button = GetDlgItem(IDC_BNT_CONTACT))
			button->EnableWindow(TRUE);

		AppendMessage(_T("Unable to submit the segmentation batch."));
	}
}

void CBrainHifiDlg::OnBnClickedBntMsiViewer()
{
	if (!m_pFaceBrainHifiView) {
		try {
			m_pFaceBrainHifiView = std::make_unique<CFaceBrainHifiViewx>();
		}
		catch (...) {
			AfxMessageBox(_T("Failed to create the BrainHifi MSI viewer."), MB_ICONERROR);
			return;
		}
	}
	m_pFaceBrainHifiView->showDlgBhMsiView(this);
}

void CBrainHifiDlg::OnBnClickedBntMesiWorkspace()
{
	if (!m_pMesiManager) CreateMesiManager();
	if (UpdateData(TRUE)) {
		AISegmentationConfig cfg = m_pMesiManager->aiConfig();
		cfg.promptText = ToUtf8(m_sPrompt4Sam);
		m_pMesiManager->setAiConfig(cfg);
	}
	CMesiWorkspaceDlg workspace(m_pMesiManager, this);
	workspace.DoModal();
	// The manager (and any running jobs) outlive the modal workspace.
	m_sPrompt4Sam = FromUtf8(m_pMesiManager->aiConfig().promptText);
	UpdateData(FALSE);
}
