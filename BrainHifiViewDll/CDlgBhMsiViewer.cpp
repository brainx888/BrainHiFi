#include "pch.h"
#include "CDlgBhMsiViewer.h"
#include "cmetallamaapi.h"
#include "CMetaAiClient.h"
#include <algorithm>
#include <cmath>
#include <gdiplus.h>

#include <windows.h>
#include <winhttp.h>
#include <format>
#include <memory>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {
const UINT g_uPaneId[CDlgBhMsiViewer::BH_VIEW_COUNT] = { IDC_BH_PANE_AXIAL, IDC_BH_PANE_CORONAL, IDC_BH_PANE_SAGITTAL };
const LPCTSTR g_szViewName[CDlgBhMsiViewer::BH_VIEW_COUNT] = { _T("Axial"), _T("Coronal"), _T("Sagittal") };
const LPCTSTR g_szFileFilter = _T("All files (*.*)|*.*||");

inline EBhView toView(int iView) { return (EBhView)iView; }

// Serialize the render/view parameters field-by-field (SBhRenderParam has
// no serialize of its own).  Plain data only - no CObject, no schema.
void serializeParam(CArchive& ar, SBhRenderParam& p)
{
	if (ar.IsStoring()) {
		ar << p.dGrayLow << p.dGrayHigh;
		ar << (int)(p.bShowSource ? 1 : 0) << p.nTime << p.nItem;
		ar << p.dSrcMaxAbs << p.dThreshold << p.dOpacity;
		ar << (int)(p.bShowNegative ? 1 : 0) << (int)(p.bTrilinear ? 1 : 0);
		ar << (int)(p.bFlipLR ? 1 : 0) << (int)(p.bCrosshair ? 1 : 0);
		ar << p.nCursor[0] << p.nCursor[1] << p.nCursor[2];
	}
	else {
		int nFlag = 0;
		ar >> p.dGrayLow >> p.dGrayHigh;
		ar >> nFlag; p.bShowSource = (nFlag != 0);
		ar >> p.nTime >> p.nItem;
		ar >> p.dSrcMaxAbs >> p.dThreshold >> p.dOpacity;
		ar >> nFlag; p.bShowNegative = (nFlag != 0);
		ar >> nFlag; p.bTrilinear = (nFlag != 0);
		ar >> nFlag; p.bFlipLR = (nFlag != 0);
		ar >> nFlag; p.bCrosshair = (nFlag != 0);
		ar >> p.nCursor[0] >> p.nCursor[1] >> p.nCursor[2];
	}
}

// Lazy one-time GDI+ startup.  Shutdown is intentionally skipped: static
// destruction in a DLL runs under the loader lock, where GdiplusShutdown is
// not allowed; the OS reclaims the token at process exit.
ULONG_PTR bhGdiPlusToken()
{
	static const ULONG_PTR s_token = [] {
		ULONG_PTR token = 0;
		Gdiplus::GdiplusStartupInput input;
		if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) token = 0;
		return token;
		}();
	return s_token;
}

// Pick the GDI+ encoder from the file extension (default: PNG).
const CLSID* bhEncoderForPath(LPCTSTR szPath, CLSID& clsid)
{
	CString sMime = _T("image/png");
	if (const LPCTSTR szExt = _tcsrchr(szPath, _T('.'))) {
		if (_tcsicmp(szExt, _T(".jpg")) == 0 || _tcsicmp(szExt, _T(".jpeg")) == 0) sMime = _T("image/jpeg");
		else if (_tcsicmp(szExt, _T(".bmp")) == 0) sMime = _T("image/bmp");
		else if (_tcsicmp(szExt, _T(".tif")) == 0 || _tcsicmp(szExt, _T(".tiff")) == 0) sMime = _T("image/tiff");
	}
	UINT nNum = 0, nSize = 0;
	Gdiplus::GetImageEncodersSize(&nNum, &nSize);
	if (!nSize) return nullptr;
	std::vector<BYTE> vBuf(nSize);
	Gdiplus::ImageCodecInfo* pInfo = (Gdiplus::ImageCodecInfo*)vBuf.data();
	Gdiplus::GetImageEncoders(nNum, nSize, pInfo);
	for (UINT i = 0; i < nNum; i++) {
		if (CString(pInfo[i].MimeType).CompareNoCase(sMime) == 0) {
			clsid = pInfo[i].Clsid;
			return &clsid;
		}
	}
	return nullptr;
}

// Write a top-down BGRA (0xAARRGGBB) buffer as a 24-bit RGB image file.
bool bhSaveBgra(LPCTSTR szPath, const uint32_t* pBgra, int nCol, int nRow)
{
	if (!pBgra || nCol <= 0 || nRow <= 0 || bhGdiPlusToken() == 0) return false;
	CLSID clsid;
	if (!bhEncoderForPath(szPath, clsid)) return false;
	Gdiplus::Bitmap bmp32(nCol, nRow, nCol * (int)sizeof(uint32_t),
		PixelFormat32bppARGB, (BYTE*)pBgra);
	// SAM expects 3-channel RGB - drop the alpha channel.
	Gdiplus::Bitmap* pBmp24 = bmp32.Clone(0, 0, nCol, nRow, PixelFormat24bppRGB);
	if (!pBmp24) return false;
	const Gdiplus::Status st = pBmp24->Save(szPath, &clsid, nullptr);
	delete pBmp24;
	return st == Gdiplus::Ok;
}
}

IMPLEMENT_DYNAMIC(CDlgBhMsiViewer, CDialogEx)

CDlgBhMsiViewer::CDlgBhMsiViewer(CWnd* pParent)
	: CDialogEx(IDD_BH_MSI_VIEWER, pParent)
{
	m_param.dThreshold = 0.5;
	m_param.dOpacity = 0.6;
}
CDlgBhMsiViewer::~CDlgBhMsiViewer()
{
	if (m_aiFuture.valid()) m_aiFuture.wait();
	if (m_modalFuture.valid()) m_modalFuture.wait();
#ifdef BH_ENABLE_FAL_SAM
	if (m_falFuture.valid()) m_falFuture.wait();
	if (m_pSam3Client) {
		delete m_pSam3Client;
		m_pSam3Client = nullptr;
	}
#endif
}

void CDlgBhMsiViewer::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BH_SLD_AXIAL, m_sldAxial);
	DDX_Control(pDX, IDC_BH_SLD_CORONAL, m_sldCoronal);
	DDX_Control(pDX, IDC_BH_SLD_SAGITTAL, m_sldSagittal);
	DDX_Control(pDX, IDC_BH_SLD_TIME, m_sldTime);
	DDX_Control(pDX, IDC_BH_SLD_THRESH, m_sldThreshold);
	DDX_Control(pDX, IDC_BH_SLD_OPACITY, m_sldOpacity);
	DDX_Control(pDX, IDC_BH_SLD_GRAY_LOW, m_sldGrayLow);
	DDX_Control(pDX, IDC_BH_SLD_GRAY_HIGH, m_sldGrayHigh);
	DDX_Control(pDX, IDC_BH_CMB_ITEM, m_cmbItem);
	DDX_Control(pDX, IDC_BTN_PLAY_STOP, m_btnSrcImgPlay);
	DDX_Text(pDX, IDC_EDIT_TOTAL_SRCIMG_SLICES, m_src.m_nTime);// m_src.m_nTime);
	DDX_Text(pDX, IDC_EDIT_CURRENT_VOLUMSNSR_ID, m_src.m_nTimeCurrent);// m_nCrntSamCrntTimSlc);
	DDX_Text(pDX, IDC_BH_EDIT_SRC_PATH, m_sSrcPath);
	DDX_Text(pDX, IDC_EDIT_META_MODAL_LINK_PROMPT, m_sPrompt);
	DDX_Text(pDX, IDC_EDIT_META_MODAL_LINK_RESULT, m_sAiAnalysisResult);
}

BEGIN_MESSAGE_MAP(CDlgBhMsiViewer, CDialogEx)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_HSCROLL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_BN_CLICKED(IDC_BH_BTN_LOAD_MRI, &CDlgBhMsiViewer::OnBnClickedLoadMri)
	ON_BN_CLICKED(IDC_BH_BTN_LOAD_SRC, &CDlgBhMsiViewer::OnBnClickedLoadSrc)
	ON_BN_CLICKED(IDC_BH_BTN_GOTO_PEAK, &CDlgBhMsiViewer::OnBnClickedGotoPeak)
	ON_BN_CLICKED(IDC_BH_BTN_CENTER, &CDlgBhMsiViewer::OnBnClickedCenter)
	ON_BN_CLICKED(IDC_BH_CHK_SHOW_SRC, &CDlgBhMsiViewer::OnBnClickedOption)
	ON_BN_CLICKED(IDC_BH_CHK_NEGATIVE, &CDlgBhMsiViewer::OnBnClickedOption)
	ON_BN_CLICKED(IDC_BH_CHK_TRILINEAR, &CDlgBhMsiViewer::OnBnClickedOption)
	ON_BN_CLICKED(IDC_BH_CHK_CROSSHAIR, &CDlgBhMsiViewer::OnBnClickedOption)
	ON_BN_CLICKED(IDC_BH_CHK_FLIP_LR, &CDlgBhMsiViewer::OnBnClickedOption)
	ON_CBN_SELCHANGE(IDC_BH_CMB_ITEM, &CDlgBhMsiViewer::OnCbnSelchangeItem)
	ON_BN_CLICKED(IDC_BTN_PLAY_STOP, &CDlgBhMsiViewer::OnBnClickedBtnPlayStop)
	ON_BN_CLICKED(IDC_BTN_FORWARD, &CDlgBhMsiViewer::OnBnClickedBtnForward)
	ON_BN_CLICKED(IDC_BTN_REVERSE, &CDlgBhMsiViewer::OnBnClickedBtnReverse)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_BH_BTN_OPEN, &CDlgBhMsiViewer::OnBnClickedBhBtnOpen)
	ON_BN_CLICKED(IDC_BH_BTN_SAVE, &CDlgBhMsiViewer::OnBnClickedBhBtnSave)
	ON_BN_CLICKED(IDC_BH_BTN_META_MODAL, &CDlgBhMsiViewer::OnBnClickedBhBtnMetaModal)
	ON_MESSAGE(WM_USER_RESET_BUTTON, &CDlgBhMsiViewer::OnResetButton)
	ON_BN_CLICKED(IDC_BH_BTN_META_SAM, &CDlgBhMsiViewer::OnBnClickedBhBtnMetaSam)
	ON_BN_CLICKED(IDC_BH_BTN_AI_MSI_REPORT, &CDlgBhMsiViewer::OnBnClickedBhBtnAiMsiReport)
	ON_BN_CLICKED(IDC_BH_BTN_AI_EXPLAIN, &CDlgBhMsiViewer::OnBnClickedBhBtnAiExplain)

	ON_MESSAGE(WM_USER_AI_DONE, &CDlgBhMsiViewer::OnAiDone)
	ON_MESSAGE(WM_MSI_SEG_DONE, &CDlgBhMsiViewer::OnSegDone2Spinner)
#ifdef BH_ENABLE_FAL_SAM
	ON_MESSAGE(WM_USER_FAL_DONE, &CDlgBhMsiViewer::OnFalSamDone)
#endif

	ON_BN_CLICKED(IDC_BH_BTN_META_LLAMA_API, &CDlgBhMsiViewer::OnBnClickedBhBtnMetaLlamaApi)
	ON_COMMAND(ID_OPERATION_CHECKCREDIENTIAL, &CDlgBhMsiViewer::OnOperationCheckcrediential)
	ON_COMMAND(ID_OPERATION_FALAISAM, &CDlgBhMsiViewer::OnOperationFalaisam)
	ON_COMMAND(ID_EDIT_LOADIMAGESFORSEGMENTATION, &CDlgBhMsiViewer::OnEditLoadimagesforsegmentation)
	ON_COMMAND(ID_EDIT_FILE, &CDlgBhMsiViewer::OnEditFile)
	ON_COMMAND(ID_EDIT_FILEIMAGEEXPLANATION, &CDlgBhMsiViewer::OnEditFileimageexplanation)

	ON_BN_CLICKED(IDC_BTN_READ_ALOUD, &CDlgBhMsiViewer::OnBnClickedBtnReadAloud)
	ON_BN_CLICKED(IDC_BTN_STOP_READING, &CDlgBhMsiViewer::OnBnClickedBtnStopReading)
	ON_MESSAGE(WM_TTS_EVENT, &CDlgBhMsiViewer::OnTtsEvent)

	ON_COMMAND(ID_OPERATION_TESTLLAMA, &CDlgBhMsiViewer::OnOperationTestllama)
	ON_COMMAND(ID_OPERATION_TESTMETAMODAL, &CDlgBhMsiViewer::OnOperationTestmetamodal)
	ON_BN_CLICKED(IDC_BTN_READ_ALOUD, &CDlgBhMsiViewer::OnBnClickedBtnReadAloud)
	ON_BN_CLICKED(IDC_BTN_STOP_READING, &CDlgBhMsiViewer::OnBnClickedBtnStopReading)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CDlgBhMsiViewer::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CheckDlgButton(IDC_BH_CHK_SHOW_SRC, BST_CHECKED);
	CheckDlgButton(IDC_BH_CHK_NEGATIVE, BST_CHECKED);
	CheckDlgButton(IDC_BH_CHK_CROSSHAIR, BST_CHECKED);
	m_sldThreshold.SetRange(0, 100);
	m_sldThreshold.SetPos((int)(m_param.dThreshold * 100 + 0.5));
	m_sldOpacity.SetRange(0, 100);
	m_sldOpacity.SetPos((int)(m_param.dOpacity * 100 + 0.5));
	readOptions();

	if (!m_sMriPath.IsEmpty()){
		m_mri.openFilePath(m_sMriPath);
	}
	else onMriChanged();
	if (!m_sSrcPath.IsEmpty()) {
		m_src.openFilePath(m_sSrcPath);
	}
	else onSourceChanged();

	m_spinner.SubclassDlgItem(IDC_SPINNER, this);
	m_spinner.SetColor(RGB(100, 120, 215));


	if (m_tts.Init(GetSafeHwnd()))
	{
		m_tts.SetRate(0);
		m_tts.SetVolume(100);
		// m_tts.SetVoiceByName(L"Zira");   // optional
	}
	GetDlgItem(IDC_BTN_STOP_READING)->EnableWindow(FALSE);
	return TRUE;
}

bool CDlgBhMsiViewer::setMri(const CBhMriVolume& mri)
{
	m_mri = mri;
	if (GetSafeHwnd()) onMriChanged();
	return !m_mri.isEmpty();
}

bool CDlgBhMsiViewer::setSource(const CBhSourceVolume& src)
{
	m_src = src;
	if (GetSafeHwnd()) onSourceChanged();
	return !m_src.isEmpty();
}

void CDlgBhMsiViewer::onMriChanged()
{
	SetDlgItemText(IDC_BH_EDIT_MRI_PATH, m_sMriPath);
	const bool bHasMri = !m_mri.isEmpty();
	m_sldSagittal.SetRange(0, bHasMri ? (int)m_mri.m_nW - 1 : 0, TRUE);
	m_sldAxial.SetRange(0, bHasMri ? (int)m_mri.m_nH - 1 : 0, TRUE);
	m_sldCoronal.SetRange(0, bHasMri ? (int)m_mri.m_nD - 1 : 0, TRUE);
	m_param.nCursor[0] = bHasMri ? m_mri.m_nW / 2 : 0;
	m_param.nCursor[1] = bHasMri ? m_mri.m_nH / 2 : 0;
	m_param.nCursor[2] = bHasMri ? m_mri.m_nD / 2 : 0;

	const int nMaxGray = bHasMri ? (std::max)(1, (int)m_mri.maxValue()) : 1;
	m_sldGrayLow.SetRange(0, nMaxGray, TRUE);
	m_sldGrayHigh.SetRange(0, nMaxGray, TRUE);
	double dLow = 0.0, dHigh = (double)nMaxGray;
	if (bHasMri) m_mri.intensityPercentiles(1.0, 99.5, dLow, dHigh);
	m_param.dGrayLow = 0.0;
	m_param.dGrayHigh = (std::max)(1.0, dHigh);
	m_sldGrayLow.SetPos((int)m_param.dGrayLow);
	m_sldGrayHigh.SetPos((int)m_param.dGrayHigh);

	GetDlgItem(IDC_BH_BTN_CENTER)->EnableWindow(bHasMri);
	syncSlidersFromCursor();
	updateInfo();
	refreshViews();
}

void CDlgBhMsiViewer::onSourceChanged()
{
	SetDlgItemText(IDC_BH_EDIT_SRC_PATH, m_sSrcPath);
	const bool bHasSrc = !m_src.isEmpty();
	m_sldTime.SetRange(0, bHasSrc ? (int)m_src.m_nTime - 1 : 0, TRUE);
	m_param.nTime = bHasSrc ? m_src.m_nTimeCurrent : 0;
	m_param.nItem = bHasSrc ? m_src.m_nItemActive : 0;
	m_sldTime.SetPos((int)m_param.nTime);

	m_cmbItem.ResetContent();
	for (int64_t iItem = 0; iItem < m_src.m_nItems; iItem++) {
		CString sItem;
		sItem.Format(_T("Item %I64d"), iItem);
		m_cmbItem.AddString(sItem);
	}
	if (bHasSrc) m_cmbItem.SetCurSel((int)m_param.nItem);

	GetDlgItem(IDC_BH_SLD_TIME)->EnableWindow(bHasSrc && m_src.m_nTime > 1);
	GetDlgItem(IDC_BH_CMB_ITEM)->EnableWindow(bHasSrc);
	GetDlgItem(IDC_BH_BTN_GOTO_PEAK)->EnableWindow(bHasSrc && !m_mri.isEmpty());
	updateSourceScale();
	updateInfo();
	refreshViews();
}

void CDlgBhMsiViewer::updateSourceScale()
{
	double dMin = 0.0, dMax = 0.0;
	m_dSrcMaxAbs = 1.0;
	if (m_src.valueRange(m_param.nTime, m_param.nItem, dMin, dMax)) {
		m_dSrcMaxAbs = m_param.bShowNegative ? (std::max)(std::fabs(dMin), std::fabs(dMax)) : dMax;
		if (!(m_dSrcMaxAbs > 0.0)) m_dSrcMaxAbs = 1.0;
	}
	m_param.dSrcMaxAbs = m_dSrcMaxAbs;
}

void CDlgBhMsiViewer::readOptions()
{
	m_param.bShowSource = IsDlgButtonChecked(IDC_BH_CHK_SHOW_SRC) == BST_CHECKED;
	m_param.bShowNegative = IsDlgButtonChecked(IDC_BH_CHK_NEGATIVE) == BST_CHECKED;
	m_param.bTrilinear = IsDlgButtonChecked(IDC_BH_CHK_TRILINEAR) == BST_CHECKED;
	m_param.bCrosshair = IsDlgButtonChecked(IDC_BH_CHK_CROSSHAIR) == BST_CHECKED;
	m_param.bFlipLR = IsDlgButtonChecked(IDC_BH_CHK_FLIP_LR) == BST_CHECKED;
	m_param.dThreshold = m_sldThreshold.GetPos() / 100.0;
	m_param.dOpacity = m_sldOpacity.GetPos() / 100.0;
}

void CDlgBhMsiViewer::syncSlidersFromCursor()
{
	m_sldSagittal.SetPos((int)m_param.nCursor[0]);
	m_sldAxial.SetPos((int)m_param.nCursor[1]);
	m_sldCoronal.SetPos((int)m_param.nCursor[2]);
}

void CDlgBhMsiViewer::updateLabels()
{
	CString s;
	s.Format(_T("y = %I64d"), m_param.nCursor[1]);
	SetDlgItemText(IDC_BH_TXT_AXIAL, s);
	s.Format(_T("z = %I64d"), m_param.nCursor[2]);
	SetDlgItemText(IDC_BH_TXT_CORONAL, s);
	s.Format(_T("x = %I64d"), m_param.nCursor[0]);
	SetDlgItemText(IDC_BH_TXT_SAGITTAL, s);
	if (m_src.isEmpty()) s = _T("-");
	else s.Format(_T("%I64d / %I64d"), m_param.nTime, m_src.m_nTime);
	SetDlgItemText(IDC_BH_TXT_TIME, s);
	s.Format(_T("%.0f%% (%.4g)"), m_param.dThreshold * 100.0, m_param.dThreshold * m_dSrcMaxAbs);
	SetDlgItemText(IDC_BH_TXT_THRESH, s);
	s.Format(_T("%.0f%%"), m_param.dOpacity * 100.0);
	SetDlgItemText(IDC_BH_TXT_OPACITY, s);
	s.Format(_T("Window %.0f - %.0f"), m_param.dGrayLow, m_param.dGrayHigh);
	SetDlgItemText(IDC_BH_TXT_GRAY, s);
}

void CDlgBhMsiViewer::updateStatus()
{
	CString s;
	if (m_mri.isEmpty()) {
		SetDlgItemText(IDC_BH_TXT_STATUS, _T("Load an MRI (CAnaData) file, then a MEG source (CVolumSourcImag) file."));
		return;
	}
	const int64_t* pCur = m_param.nCursor;
	double dMm[3];
	m_mri.voxelToHeadMm((double)pCur[0], (double)pCur[1], (double)pCur[2], dMm);
	s.Format(_T("Cursor voxel (%I64d, %I64d, %I64d)   head (%.1f, %.1f, %.1f) mm   MRI = %u"),
	         pCur[0], pCur[1], pCur[2], dMm[0], dMm[1], dMm[2], (unsigned)m_mri.valueAt(pCur[0], pCur[1], pCur[2]));
	if (!m_src.isEmpty()) {
		double dVal = 0.0;
		CString sSrc;
		if (m_src.sampleHeadMm(m_param.nTime, m_param.nItem, dMm, m_param.bTrilinear, dVal))
			sSrc.Format(_T("   source[t=%I64d, item=%I64d] = %.5g"), m_param.nTime, m_param.nItem, dVal);
		else
			sSrc = _T("   source: outside grid");
		s += sSrc;
	}
	SetDlgItemText(IDC_BH_TXT_STATUS, s);
}

void CDlgBhMsiViewer::updateInfo()
{
	CString s, sSrc;
	if (m_mri.isEmpty()) s = _T("MRI: none");
	else s.Format(_T("MRI: %I64d x %I64d x %I64d voxels, %.3f x %.3f x %.3f mm, origin (%.1f, %.1f, %.1f) mm"),
	              m_mri.m_nW, m_mri.m_nH, m_mri.m_nD, m_mri.m_dSpaceX, m_mri.m_dSpaceY, m_mri.m_dSpaceZ,
	              m_mri.m_dOriginMm[0], m_mri.m_dOriginMm[1], m_mri.m_dOriginMm[2]);
	if (m_src.isEmpty()) sSrc = _T("\nSource: none");
	else sSrc.Format(_T("\nSource: %I64d x %I64d x %I64d grid, step %.2f x %.2f x %.2f mm, origin (%.1f, %.1f, %.1f) mm, %I64d time slices, %I64d items, max |value| %.4g"),
	                 m_src.m_nW, m_src.m_nH, m_src.m_nD, m_src.m_dStepX, m_src.m_dStepY, m_src.m_dStepZ,
	                 m_src.m_dOriginMm[0], m_src.m_dOriginMm[1], m_src.m_dOriginMm[2],
	                 m_src.m_nTime, m_src.m_nItems, m_dSrcMaxAbs);
	SetDlgItemText(IDC_BH_TXT_INFO, s + sSrc);
}

void CDlgBhMsiViewer::refreshViews()
{
	updateLabels();
	updateStatus();
	for (int iView = 0; iView < BH_VIEW_COUNT; iView++) {
		CRect rcPane;
		if (getPaneRect(iView, rcPane)) InvalidateRect(rcPane, FALSE);
	}
}

bool CDlgBhMsiViewer::getPaneRect(int iView, CRect& rcPane)
{
	CWnd* pPane = GetDlgItem(g_uPaneId[iView]);
	if (!pPane) return false;
	pPane->GetWindowRect(&rcPane);
	ScreenToClient(&rcPane);
	return true;
}

BOOL CDlgBhMsiViewer::OnEraseBkgnd(CDC* pDC)
{
	CRect rcClient;
	GetClientRect(&rcClient);
	const int nSaved = pDC->SaveDC();
	for (int iView = 0; iView < BH_VIEW_COUNT; iView++) {
		CRect rcPane;
		if (getPaneRect(iView, rcPane)) pDC->ExcludeClipRect(&rcPane);
	}
	pDC->FillSolidRect(&rcClient, GetSysColor(COLOR_3DFACE));
	pDC->RestoreDC(nSaved);
	return TRUE;
}

void CDlgBhMsiViewer::OnPaint()
{
	CPaintDC dc(this);
	for (int iView = 0; iView < BH_VIEW_COUNT; iView++) {
		CRect rcPane;
		if (getPaneRect(iView, rcPane)) drawPane(dc, iView, rcPane);
	}
}

void CDlgBhMsiViewer::drawPane(CDC& dc, int iView, const CRect& rcPane)
{
	CDC dcMem;
	dcMem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, rcPane.Width(), rcPane.Height());
	CBitmap* pOldBmp = dcMem.SelectObject(&bmp);
	const CRect rcLocal(0, 0, rcPane.Width(), rcPane.Height());
	dcMem.FillSolidRect(&rcLocal, RGB(0, 0, 0));
	dcMem.SetBkMode(TRANSPARENT);
	CFont* pOldFont = dcMem.SelectObject(GetFont());

	m_rcImage[iView].SetRectEmpty();
	int nCol = 0, nRow = 0;
	double dMmPerCol = 1.0, dMmPerRow = 1.0;
	if (CBhSliceRenderer::imageSize(m_mri, toView(iView), nCol, nRow, dMmPerCol, dMmPerRow)
	    && CBhSliceRenderer::render(m_mri, m_src.isEmpty() ? nullptr : &m_src, toView(iView), m_param, m_vBgra, nCol, nRow)) {
		const int nMargin = 16;
		const double dWidthMm = nCol * dMmPerCol;
		const double dHeightMm = nRow * dMmPerRow;
		const double dScale = (std::min)((rcLocal.Width() - 2.0 * nMargin) / dWidthMm, (rcLocal.Height() - 2.0 * nMargin) / dHeightMm);
		const int nDstW = (std::max)(1, (int)(dWidthMm * dScale + 0.5));
		const int nDstH = (std::max)(1, (int)(dHeightMm * dScale + 0.5));
		const int nDstX = (rcLocal.Width() - nDstW) / 2;
		const int nDstY = (rcLocal.Height() - nDstH) / 2;

		BITMAPINFO bmi;
		ZeroMemory(&bmi, sizeof(bmi));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = nCol;
		bmi.bmiHeader.biHeight = -nRow;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		dcMem.SetStretchBltMode(COLORONCOLOR);
		::StretchDIBits(dcMem.GetSafeHdc(), nDstX, nDstY, nDstW, nDstH, 0, 0, nCol, nRow,
		                m_vBgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

		m_rcImage[iView].SetRect(rcPane.left + nDstX, rcPane.top + nDstY, rcPane.left + nDstX + nDstW, rcPane.top + nDstY + nDstH);
		m_nImageCol[iView] = nCol;
		m_nImageRow[iView] = nRow;

		LPCTSTR szLeft, szRight, szTop, szBottom;
		if (toView(iView) == EBhView::Sagittal) {
			szLeft = _T("A"); szRight = _T("P"); szTop = _T("S"); szBottom = _T("I");
		} else {
			szLeft = m_param.bFlipLR ? _T("L") : _T("R");
			szRight = m_param.bFlipLR ? _T("R") : _T("L");
			szTop = (toView(iView) == EBhView::Axial) ? _T("A") : _T("S");
			szBottom = (toView(iView) == EBhView::Axial) ? _T("P") : _T("I");
		}
		dcMem.SetTextColor(RGB(255, 255, 0));
		CRect rcText(rcLocal);
		dcMem.DrawText(szLeft, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		dcMem.DrawText(szRight, &rcText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		dcMem.DrawText(szBottom, &rcText, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
		rcText.left += 2 * nMargin;
		dcMem.DrawText(szTop, &rcText, DT_CENTER | DT_TOP | DT_SINGLELINE);
	} else {
		dcMem.SetTextColor(RGB(160, 160, 160));
		CRect rcText(rcLocal);
		dcMem.DrawText(_T("No MRI loaded"), &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	dcMem.SetTextColor(RGB(255, 255, 255));
	CRect rcTitle(rcLocal);
	rcTitle.DeflateRect(4, 2);
	dcMem.DrawText(g_szViewName[iView], &rcTitle, DT_LEFT | DT_TOP | DT_SINGLELINE);

	dc.BitBlt(rcPane.left, rcPane.top, rcPane.Width(), rcPane.Height(), &dcMem, 0, 0, SRCCOPY);
	dcMem.SelectObject(pOldFont);
	dcMem.SelectObject(pOldBmp);
}

bool CDlgBhMsiViewer::hitImage(CPoint point, int& iView, int& nCol, int& nRow) const
{
	for (iView = 0; iView < BH_VIEW_COUNT; iView++) {
		const CRect& rc = m_rcImage[iView];
		if (rc.IsRectEmpty() || !rc.PtInRect(point)) continue;
		nCol = (int)((point.x - rc.left) * (__int64)m_nImageCol[iView] / rc.Width());
		nRow = (int)((point.y - rc.top) * (__int64)m_nImageRow[iView] / rc.Height());
		nCol = (std::max)(0, (std::min)(m_nImageCol[iView] - 1, nCol));
		nRow = (std::max)(0, (std::min)(m_nImageRow[iView] - 1, nRow));
		return true;
	}
	iView = -1;
	return false;
}

void CDlgBhMsiViewer::moveCursorTo(int iView, int nCol, int nRow)
{
	int64_t nVxl[3];
	if (!CBhSliceRenderer::pixelToVoxel(m_mri, toView(iView), m_param.bFlipLR, nCol, nRow, m_param.nCursor, nVxl)) return;
	for (int i = 0; i < 3; i++) m_param.nCursor[i] = nVxl[i];
	syncSlidersFromCursor();
	refreshViews();
}

void CDlgBhMsiViewer::stepSlice(int iView, int nStep)
{
	if (m_mri.isEmpty()) return;
	const int iAxis = (toView(iView) == EBhView::Axial) ? 1 : (toView(iView) == EBhView::Coronal) ? 2 : 0;
	const int64_t nSize = (iAxis == 0) ? m_mri.m_nW : (iAxis == 1) ? m_mri.m_nH : m_mri.m_nD;
	m_param.nCursor[iAxis] = (std::max)((int64_t)0, (std::min)(nSize - 1, m_param.nCursor[iAxis] + nStep));
	syncSlidersFromCursor();
	refreshViews();
}

void CDlgBhMsiViewer::OnLButtonDown(UINT nFlags, CPoint point)
{
	int iView = -1, nCol = 0, nRow = 0;
	if (hitImage(point, iView, nCol, nRow)) {
		m_iDragView = iView;
		SetCapture();
		moveCursorTo(iView, nCol, nRow);
	}
	CDialogEx::OnLButtonDown(nFlags, point);
}

void CDlgBhMsiViewer::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_iDragView >= 0) {
		m_iDragView = -1;
		ReleaseCapture();
	}
	CDialogEx::OnLButtonUp(nFlags, point);
}

void CDlgBhMsiViewer::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_iDragView >= 0 && (nFlags & MK_LBUTTON)) {
		const CRect& rc = m_rcImage[m_iDragView];
		if (!rc.IsRectEmpty()) {
			const int nCol = (std::max)(0, (std::min)(m_nImageCol[m_iDragView] - 1,
				(int)((point.x - rc.left) * (__int64)m_nImageCol[m_iDragView] / rc.Width())));
			const int nRow = (std::max)(0, (std::min)(m_nImageRow[m_iDragView] - 1,
				(int)((point.y - rc.top) * (__int64)m_nImageRow[m_iDragView] / rc.Height())));
			moveCursorTo(m_iDragView, nCol, nRow);
		}
	}
	CDialogEx::OnMouseMove(nFlags, point);
}

BOOL CDlgBhMsiViewer::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CPoint ptClient(pt);
	ScreenToClient(&ptClient);
	int iView = -1, nCol = 0, nRow = 0;
	if (hitImage(ptClient, iView, nCol, nRow)) {
		stepSlice(iView, zDelta > 0 ? 1 : -1);
		return TRUE;
	}
	return CDialogEx::OnMouseWheel(nFlags, zDelta, pt);
}

void CDlgBhMsiViewer::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CWnd* pCtrl = (CWnd*)pScrollBar;
	if (pCtrl == &m_sldSagittal) m_param.nCursor[0] = m_sldSagittal.GetPos();
	else if (pCtrl == &m_sldAxial) m_param.nCursor[1] = m_sldAxial.GetPos();
	else if (pCtrl == &m_sldCoronal) m_param.nCursor[2] = m_sldCoronal.GetPos();
	else if (pCtrl == &m_sldTime) {
		m_param.nTime = m_sldTime.GetPos();
		updateSourceScale();
		updateInfo();
	} else if (pCtrl == &m_sldThreshold || pCtrl == &m_sldOpacity) {
		readOptions();
	} else if (pCtrl == &m_sldGrayLow || pCtrl == &m_sldGrayHigh) {
		int nLow = m_sldGrayLow.GetPos();
		int nHigh = m_sldGrayHigh.GetPos();
		if (nHigh <= nLow) {
			if (pCtrl == &m_sldGrayLow) { nHigh = nLow + 1; m_sldGrayHigh.SetPos(nHigh); }
			else { nLow = (std::max)(0, nHigh - 1); m_sldGrayLow.SetPos(nLow); }
		}
		m_param.dGrayLow = nLow;
		m_param.dGrayHigh = (std::max)(nLow + 1, nHigh);
	} else {
		CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	refreshViews();
}

void CDlgBhMsiViewer::OnBnClickedLoadMri()
{
	CWaitCursor wait;
	CBhMriVolume mri;
	mri.openStd();

	m_sMriPath = mri.m_sPathName;
	m_mri = std::move(mri);
	if (GetSafeHwnd()) onMriChanged();


}

void CDlgBhMsiViewer::OnBnClickedLoadSrc()
{
  	CWaitCursor wait;
	CBhSourceVolume src;
 	src.openStd();
	m_sSrcPath = src.m_sPathName;
	m_src = std::move(src);
	if (GetSafeHwnd()) onSourceChanged();

	UpdateData(FALSE);
	return;



	//CFileDialog dlg(TRUE, nullptr, nullptr, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, g_szFileFilter, this);
	//dlg.m_ofn.lpstrTitle = _T("Open MEG source (CVolumSourcImag) file");
	//if (dlg.DoModal() == IDOK) loadSourceFile(dlg.GetPathName());


}

void CDlgBhMsiViewer::OnBnClickedGotoPeak()
{
	int64_t nSrcVxl[3];
	double dPeak = 0.0;
	if (m_mri.isEmpty() || !m_src.peakVoxel(m_param.nTime, m_param.nItem, m_param.bShowNegative, nSrcVxl, dPeak)) return;
	double dMm[3], dMriVxl[3];
	m_src.voxelToHeadMm(nSrcVxl[0] + 0.5, nSrcVxl[1] + 0.5, nSrcVxl[2] + 0.5, dMm);
	m_mri.headMmToVoxel(dMm, dMriVxl);
	const int64_t nSize[3] = { m_mri.m_nW, m_mri.m_nH, m_mri.m_nD };
	for (int i = 0; i < 3; i++)
		m_param.nCursor[i] = (std::max)((int64_t)0, (std::min)(nSize[i] - 1, (int64_t)std::floor(dMriVxl[i] + 0.5)));
	syncSlidersFromCursor();
	refreshViews();
}

void CDlgBhMsiViewer::OnBnClickedCenter()
{
	if (m_mri.isEmpty()) return;
	m_param.nCursor[0] = m_mri.m_nW / 2;
	m_param.nCursor[1] = m_mri.m_nH / 2;
	m_param.nCursor[2] = m_mri.m_nD / 2;
	syncSlidersFromCursor();
	refreshViews();
}

void CDlgBhMsiViewer::OnBnClickedOption()
{
	const bool bNegBefore = m_param.bShowNegative;
	readOptions();
	if (bNegBefore != m_param.bShowNegative) {
		updateSourceScale();
		updateInfo();
	}
	refreshViews();
}

void CDlgBhMsiViewer::OnCbnSelchangeItem()
{
	const int nSel = m_cmbItem.GetCurSel();
	if (nSel < 0) return;
	m_param.nItem = nSel;
	updateSourceScale();
	updateInfo();
	refreshViews();
}


void CDlgBhMsiViewer::OnBnClickedBtnForward()
{
	UpdateData();

	if (m_src.isEmpty()) { UpdateData(FALSE); return; }

	const int64_t nTime = m_src.m_nTime;
	if (nTime <= 0) { UpdateData(FALSE); return; }
	m_src.m_nTime = nTime;

	int64_t nSlice = m_src.m_nTimeCurrent + 1;
	if (nSlice >= nTime) nSlice = 0;

	m_src.m_nTimeCurrent = nSlice;
	//m_nCrntSamCrntTimSlc = nSlice;
	m_param.nTime = nSlice;
	m_sldTime.SetPos((int)nSlice);

	updateSourceScale();
	updateInfo();
	refreshViews();

	UpdateData(FALSE);
}

void CDlgBhMsiViewer::OnBnClickedBtnReverse()
{
	UpdateData();

	if (m_src.isEmpty()) { UpdateData(FALSE); return; }

	const int64_t nTime = m_src.m_nTime;
	if (nTime <= 0) { UpdateData(FALSE); return; }
	m_src.m_nTime = nTime;

	int64_t nSlice = m_src.m_nTimeCurrent - 1;
	if (nSlice < 0) nSlice = nTime - 1;

	m_src.m_nTimeCurrent = nSlice;
	//m_nCrntSamCrntTimSlc = nSlice;
	m_param.nTime = nSlice;
	m_sldTime.SetPos((int)nSlice);

	updateSourceScale();
	updateInfo();
	refreshViews();

	UpdateData(FALSE);
}

void CDlgBhMsiViewer::OnBnClickedBtnPlayStop()
{
	UpdateData();

	if (m_bPlaySrcMovie) {
		KillTimer(SRCIMG_PLAY_MSG);
		m_uTimeID = 0;
		m_bPlaySrcMovie = FALSE;
	}
	else {
		if (m_src.isEmpty() || m_src.m_nTime <= 1) {	// nothing to animate
			UpdateData(FALSE);
			return;
		}
		m_uTimeID = SetTimer(SRCIMG_PLAY_MSG, m_nSrcPlayIntvl, NULL);
		if (m_uTimeID == 0) {	// SetTimer failed
			UpdateData(FALSE);
			return;
		}
		m_bPlaySrcMovie = TRUE;
	}

	m_btnSrcImgPlay.SetWindowTextW(m_bPlaySrcMovie ? _T("Stop") : _T("Start"));
	UpdateData(FALSE);
}

void CDlgBhMsiViewer::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == SRCIMG_PLAY_MSG && m_bPlaySrcMovie) {
		const int64_t nTime = m_src.m_nTime;
		if (m_src.isEmpty() || nTime <= 1) {
			KillTimer(SRCIMG_PLAY_MSG);
			m_uTimeID = 0;
			m_bPlaySrcMovie = FALSE;
			m_btnSrcImgPlay.SetWindowTextW(_T("Start"));
			UpdateData(FALSE);
			return;
		}

		int64_t nSlice = m_src.m_nTimeCurrent + 1;
		if (nSlice >= nTime) nSlice = 0;

		m_src.m_nTimeCurrent = nSlice;
		m_param.nTime = nSlice;
		m_sldTime.SetPos((int)nSlice);

		updateSourceScale();
		updateInfo();
		refreshViews();

		UpdateData(FALSE);
		return;
	}

	CDialogEx::OnTimer(nIDEvent);
}

void CDlgBhMsiViewer::OnBnClickedBhBtnOpen()
{
	CFileDialog dlg(TRUE, _T("bhv"), NULL, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		_T("BrainHifi Viewer Session (*.bhv)|*.bhv|All Files (*.*)|*.*||"), this);
	dlg.m_ofn.lpstrTitle = _T("Open viewer session");
	if (dlg.DoModal() != IDOK) return;

	// Stop playback before replacing any data.
	if (m_bPlaySrcMovie) {
		KillTimer(SRCIMG_PLAY_MSG);
		m_uTimeID = 0;
		m_bPlaySrcMovie = FALSE;
		m_btnSrcImgPlay.SetWindowTextW(_T("Start"));
	}

	CWaitCursor wait;
	try {
		CFile file(dlg.GetPathName(), CFile::modeRead | CFile::shareDenyWrite);
		CArchive ar(&file, CArchive::load);

		// Magic head and version.
		CString sMagicHead;
		DWORD dwVersion = 0;
		ar >> sMagicHead >> dwVersion;
		if (0 != sMagicHead.Compare(_T("BHMSIVIEWERSESSION"))) {
			ar.Close();
			file.Close();
			AfxMessageBox(_T("Invalid file format!"));
			return;
		}

		// Persistent data: both volumes, view parameters, paths, play state.
		if (!m_mri.serialize(ar)) { ar.Close(); file.Close(); return; }
		if (!m_src.serialize(ar)) { ar.Close(); file.Close(); return; }
		serializeParam(ar, m_param);
		ar >> m_dSrcMaxAbs;
		ar >> m_sMriPath >> m_sSrcPath;
		ar >> m_bPlaySrcMovie >> m_nSrcPlayIntvl;
		int nDragView = -1;
		ar >> nDragView;
		m_iDragView = nDragView;

		ar.Close();
		file.Close();
	}
	catch (CException* pEx) {
		pEx->ReportError();
		pEx->Delete();
		return;
	}

	m_bPlaySrcMovie = FALSE;	// never resume playing right after a load
	m_uTimeID = 0;

	// Rebuild all runtime state: slider/combo ranges, path edits, images.
	// onMriChanged()/onSourceChanged() reset parts of m_param, so the saved
	// values are re-applied and pushed into the controls afterwards.
	onMriChanged();
	onSourceChanged();
	if (!m_mri.isEmpty()) {
		m_param.nCursor[0] = (std::max)((int64_t)0, (std::min)(m_mri.m_nW - 1, m_param.nCursor[0]));
		m_param.nCursor[1] = (std::max)((int64_t)0, (std::min)(m_mri.m_nH - 1, m_param.nCursor[1]));
		m_param.nCursor[2] = (std::max)((int64_t)0, (std::min)(m_mri.m_nD - 1, m_param.nCursor[2]));
	}
	if (!m_src.isEmpty()) {
		m_param.nTime = (std::max)((int64_t)0, (std::min)(m_src.m_nTime - 1, m_param.nTime));
		m_param.nItem = (std::max)((int64_t)0, (std::min)(m_src.m_nItems - 1, m_param.nItem));
	}
	m_src.m_nTimeCurrent = m_param.nTime;
	m_src.m_nItemActive = m_param.nItem;
	m_dSrcMaxAbs = m_param.dSrcMaxAbs;

	syncSlidersFromCursor();
	m_sldTime.SetPos((int)m_param.nTime);
	m_sldThreshold.SetPos((int)(m_param.dThreshold * 100 + 0.5));
	m_sldOpacity.SetPos((int)(m_param.dOpacity * 100 + 0.5));
	m_sldGrayLow.SetPos((int)m_param.dGrayLow);
	m_sldGrayHigh.SetPos((int)m_param.dGrayHigh);
	if (!m_src.isEmpty()) m_cmbItem.SetCurSel((int)m_param.nItem);
	CheckDlgButton(IDC_BH_CHK_SHOW_SRC, m_param.bShowSource ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_BH_CHK_NEGATIVE, m_param.bShowNegative ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_BH_CHK_TRILINEAR, m_param.bTrilinear ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_BH_CHK_CROSSHAIR, m_param.bCrosshair ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_BH_CHK_FLIP_LR, m_param.bFlipLR ? BST_CHECKED : BST_UNCHECKED);
	m_btnSrcImgPlay.SetWindowTextW(_T("Start"));

	updateInfo();
	refreshViews();
	UpdateData(FALSE);
}

void CDlgBhMsiViewer::OnBnClickedBhBtnSave()
{
	UpdateData();

	if (m_mri.isEmpty() && m_src.isEmpty()) {
		AfxMessageBox(_T("There is no data to save."), MB_ICONINFORMATION);
		return;
	}

	CFileDialog dlg(FALSE, _T("bhv"), NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		_T("BrainHifi Viewer Session (*.bhv)|*.bhv|All Files (*.*)|*.*||"), this);
	dlg.m_ofn.lpstrTitle = _T("Save viewer session");
	if (dlg.DoModal() != IDOK) return;

	CString sPathName = dlg.GetPathName();
	if (sPathName.Right(4).CompareNoCase(_T(".bhv")) != 0) {
		sPathName += _T(".bhv");
	}

	CWaitCursor wait;
	try {
		CFile file(sPathName, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite);
		CArchive ar(&file, CArchive::store);

		// Magic head and version.
		ar << CString(_T("BHMSIVIEWERSESSION")) << (DWORD)20261101;

		// Persistent data: both volumes, view parameters, paths, play state.
		m_mri.serialize(ar);
		m_src.serialize(ar);
		serializeParam(ar, m_param);
		ar << m_dSrcMaxAbs;
		ar << m_sMriPath << m_sSrcPath;
		ar << m_bPlaySrcMovie << m_nSrcPlayIntvl;
		ar << m_iDragView;

		ar.Close();
		file.Close();
	}
	catch (CException* pEx) {
		pEx->ReportError();
		pEx->Delete();
		return;
	}
}

bool CDlgBhMsiViewer::renderViewBgra(int iView, std::vector<uint32_t>& vBgra,
	int& nCol, int& nRow, bool bCrosshair) const
{
	SBhRenderParam param = m_param;
	param.bCrosshair = bCrosshair;
	return renderViewBgra(iView, param, vBgra, nCol, nRow);
}

bool CDlgBhMsiViewer::renderViewBgra(int iView, const SBhRenderParam& param,
	std::vector<uint32_t>& vBgra, int& nCol, int& nRow) const
{
	if (iView < 0 || iView >= BH_VIEW_COUNT) {
		vBgra.clear();
		nCol = nRow = 0;
		return false;
	}
	return CBhSliceRenderer::render(m_mri, m_src.isEmpty() ? nullptr : &m_src,
		toView(iView), param, vBgra, nCol, nRow);
}

bool CDlgBhMsiViewer::saveViewImage(int iView, LPCTSTR szPath, bool bCrosshair) const
{
	if (!szPath || !*szPath) return false;
	std::vector<uint32_t> vBgra;
	int nCol = 0, nRow = 0;
	if (!renderViewBgra(iView, vBgra, nCol, nRow, bCrosshair)) return false;
	return bhSaveBgra(szPath, vBgra.data(), nCol, nRow);
}

int CDlgBhMsiViewer::saveAllViews(LPCTSTR szFolder, bool bCrosshair) const
{
	if (!szFolder || !*szFolder || m_mri.isEmpty()) return 0;
	CreateDirectory(szFolder, nullptr);
	int nSaved = 0;
	for (int iView = 0; iView < BH_VIEW_COUNT; iView++) {
		CString sPath;
		sPath.Format(_T("%s\\MSI_%s.png"), szFolder, g_szViewName[iView]);
		if (saveViewImage(iView, sPath, bCrosshair)) nSaved++;
	}
	return nSaved;
}

int CDlgBhMsiViewer::saveSliceSeries(int iView, LPCTSTR szFolder, int nStep, bool bCrosshair) const
{
	if (iView < 0 || iView >= BH_VIEW_COUNT || !szFolder || !*szFolder || m_mri.isEmpty()) return 0;
	// Axial steps along y, coronal along z, sagittal along x (as stepSlice()).
	const int iAxis = (toView(iView) == EBhView::Axial) ? 1 : (toView(iView) == EBhView::Coronal) ? 2 : 0;
	const int64_t nCount = (iAxis == 0) ? m_mri.m_nW : (iAxis == 1) ? m_mri.m_nH : m_mri.m_nD;
	nStep = (std::max)(1, nStep);
	CreateDirectory(szFolder, nullptr);

	SBhRenderParam param = m_param;
	param.bCrosshair = bCrosshair;
	int nSaved = 0;
	for (int64_t iSlice = 0; iSlice < nCount; iSlice += nStep) {
		param.nCursor[iAxis] = iSlice;
		std::vector<uint32_t> vBgra;
		int nCol = 0, nRow = 0;
		if (!renderViewBgra(iView, param, vBgra, nCol, nRow)) continue;
		CString sPath;
		sPath.Format(_T("%s\\MSI_%s_%04I64d.png"), szFolder, g_szViewName[iView], iSlice);
		if (bhSaveBgra(sPath, vBgra.data(), nCol, nRow)) nSaved++;
	}
	return nSaved;
}


 
void CDlgBhMsiViewer::ExecuteModalTest()
{
	// 1. Configure Modal endpoint details
	std::wstring server = L"username--brainhifi-sam-endpoint.modal.run";
	std::wstring path = L"/segment"; // Route for your Meta SAM / Spark model

	// Example JSON payload representing prompt/image reference data
	std::string jsonPayload = "{\"prompt\": \"segment object\", \"image_id\": \"sample_01\"}";

	HINTERNET hSession = WinHttpOpen(L"BrainHifi-MFC-Client/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) return;

	HINTERNET hConnect = WinHttpConnect(hSession, server.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return;
	}

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}

	std::wstring headers = L"Content-Type: application/json\r\n";

	BOOL bResults = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L, (LPVOID)jsonPayload.data(), (DWORD)jsonPayload.size(), (DWORD)jsonPayload.size(), 0);

	if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);

	if (bResults) {
		DWORD dwStatusCode = 0;
		DWORD dwSize = sizeof(dwStatusCode);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

		// Read response body
		std::string responseBody;
		DWORD dwDownloaded = 0;
		do {
			DWORD dwSizeAvailable = 0;
			if (!WinHttpQueryDataAvailable(hRequest, &dwSizeAvailable) || dwSizeAvailable == 0) break;
			auto buffer = std::make_unique<char[]>(dwSizeAvailable + 1);
			ZeroMemory(buffer.get(), dwSizeAvailable + 1);
			if (WinHttpReadData(hRequest, buffer.get(), dwSizeAvailable, &dwDownloaded)) {
				responseBody.append(buffer.get(), dwDownloaded);
			}
		} while (dwDownloaded > 0);

		// Safely communicate back to the MFC UI thread using PostMessage or OutputDebugString
		if (dwStatusCode == 200) {
			OutputDebugStringA("[BrainHifi] Modal Inference Success:\n");
			OutputDebugStringA(responseBody.c_str());

			// Optional: Use PostMessage to update UI elements safely across threads
			// ::PostMessage(m_hWnd, WM_USER_MODEL_COMPLETE, 0, (LPARAM)new std::string(responseBody));
		}
		else {
			OutputDebugStringW(std::format(L"[BrainHifi] Modal Error HTTP Status: {}\n", dwStatusCode).c_str());
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	// Re-enable UI button on main thread (or via message passing)
	// Note: Direct UI manipulation from background threads is unsafe in MFC.
	::PostMessage(m_hWnd, WM_USER_RESET_BUTTON, 0, 0);
}
// The handler runs safely on the main UI thread when posted from the background worker
LRESULT CDlgBhMsiViewer::OnResetButton(WPARAM wParam, LPARAM lParam)
{
	// Re-enable your test button
	if (CWnd* pBtn = GetDlgItem(IDC_BH_BTN_META_MODAL)) {
		pBtn->EnableWindow(TRUE);
	}

	// Optional: Update status text or progress indicators here

	return 0; // Return 0 to indicate the message was handled
}
void CDlgBhMsiViewer::OnBnClickedBhBtnMetaModal()
{
	// Disable button or show status if desired...
	GetDlgItem(IDC_BH_BTN_META_MODAL)->EnableWindow(FALSE);

	// Launch the network request asynchronously so the MFC UI message loop never freezes
	m_modalFuture = std::async(std::launch::async, [this]() {
		this->ExecuteModalTest();
		});
}

// ======================================================================
// Meta AI MSI analysis
//   IDC_BH_BTN_AI_MSI_REPORT : segment the MSI -> peak/location text report
//   IDC_BH_BTN_AI_EXPLAIN    : 3D peaks + cursor -> location / meaning
// The prompt edit (IDC_EDIT_META_MODAL_LINK_PROMPT) is sent as study context,
// results are appended to IDC_EDIT_META_MODAL_LINK_RESULT.
// ======================================================================
namespace {

const LPCTSTR g_szDefaultSamPrompt = _T("Segment the objects in this image.");

void clampCursorToMri(const CBhMriVolume& mri, const double dMm[3], int64_t nCursor[3])
{
	double dVxl[3];
	mri.headMmToVoxel(dMm, dVxl);
	const int64_t nSize[3] = { mri.m_nW, mri.m_nH, mri.m_nD };
	for (int i = 0; i < 3; i++)
		nCursor[i] = (std::max)((int64_t)0, (std::min)(nSize[i] - 1, (int64_t)std::floor(dVxl[i] + 0.5)));
}

}

bool CDlgBhMsiViewer::segmentCurrentSource(SBhMsiSegmentResult& seg, CString& sError) const
{
	if (m_mri.isEmpty() || m_src.isEmpty()) { sError = _T("Load an MRI and a MEG source image first."); return false; }
	SBhMsiSegmentParam prm;
	prm.nTime = m_param.nTime;
	prm.nItem = m_param.nItem;
	prm.dThresholdFrac = (std::max)(0.0, (std::min)(1.0, m_param.dThreshold));
	prm.bIncludeNegative = m_param.bShowNegative;
	prm.nMaxClusters = m_nAiMaxClusters;
	std::string sErr;
	if (!CBhMsiPeakAnalyzer::segment(m_src, &m_mri, prm, seg, &sErr)) { sError = CString(CA2W(sErr.c_str(), CP_UTF8)); return false; }
	if (seg.vClusters.empty()) { sError = _T("No source activity above the current threshold."); return false; }
	return true;
}

bool CDlgBhMsiViewer::buildAiJob(EBhMsiAiTask eTask, SBhMsiAiJob& job, CString& sError) const
{
	job = SBhMsiAiJob();
	job.eTask = eTask;
	job.sModel = m_sAiModel;
	if (m_sPrompt != g_szDefaultSamPrompt) job.sContext = (LPCSTR)CW2A(m_sPrompt, CP_UTF8);
	if (!segmentCurrentSource(job.seg, sError)) return false;

	if (eTask == EBhMsiAiTask::SegmentationReport) {
		const int nClusters = (std::min)((int)job.seg.vClusters.size(), m_nAiImageClusters);
		for (int iCl = 0; iCl < nClusters; iCl++) {
			SBhRenderParam param = m_param;   // same look as the screen, crosshair on the peak
			param.bCrosshair = true;
			clampCursorToMri(m_mri, job.seg.vClusters[iCl].dPeakMm, param.nCursor);
			for (int iView = 0; iView < BH_VIEW_COUNT; iView++) {
				std::vector<uint32_t> vBgra;
				int nCol = 0, nRow = 0;
				SBhLlamaImage im;
				if (!renderViewBgra(iView, param, vBgra, nCol, nRow) || !CBhMsiAiAnalyst::encodePng(vBgra, nCol, nRow, im.sBase64))
					continue;
				CStringA sCap;
				sCap.Format("%S view through peak %d (%s), %dx%d px, left-right %s", g_szViewName[iView], iCl + 1,
				            job.seg.vClusters[iCl].bPositive ? "positive" : "negative", nCol, nRow,
				            param.bFlipLR ? "flipped" : "as displayed (R/L labels in viewer)");
				im.sCaption = (LPCSTR)sCap;
				job.vImages.push_back(im);
			}
		}
	}
	else {
		job.vPoints = CBhMsiPeakAnalyzer::peakPoints(job.seg);
		if ((int)job.vPoints.size() > m_nAiExplainPeaks) job.vPoints.resize((size_t)m_nAiExplainPeaks);
		double dMm[3];
		m_mri.voxelToHeadMm((double)m_param.nCursor[0], (double)m_param.nCursor[1], (double)m_param.nCursor[2], dMm);
		job.vPoints.push_back(CBhMsiPeakAnalyzer::makePoint(m_src, &m_mri, m_param.nTime, m_param.nItem, dMm,
		                                                    job.seg.dMaxAbs, "Cursor"));
	}
	return true;
}

void CDlgBhMsiViewer::setAiButtonsEnabled(BOOL bEnable)
{
	for (UINT nId : { (UINT)IDC_BH_BTN_AI_MSI_REPORT, (UINT)IDC_BH_BTN_AI_EXPLAIN, (UINT)IDC_BH_BTN_META_SAM })
		if (CWnd* p = GetDlgItem(nId)) p->EnableWindow(bEnable);
}

void CDlgBhMsiViewer::appendAiResult(const CString& s)
{
	m_sAiAnalysisResult += s;
	if (!m_sAiAnalysisResult.IsEmpty() && m_sAiAnalysisResult.Right(2) != _T("\r\n")) m_sAiAnalysisResult += _T("\r\n");
	SetDlgItemText(IDC_EDIT_META_MODAL_LINK_RESULT, m_sAiAnalysisResult);
	if (CEdit* pEdit = (CEdit*)GetDlgItem(IDC_EDIT_META_MODAL_LINK_RESULT))
		pEdit->LineScroll(pEdit->GetLineCount());
}

void CDlgBhMsiViewer::startAiJobImgFiles(SBhMsiAiJob&& job)
{
	m_bAiBusy = true;
	setAiButtonsEnabled(FALSE);
	CString sStart;
	if (job.eTask == EBhMsiAiTask::SegmentationReport) sStart = _T("Sending segmented MSI to Meta AI...");
	else if (job.eTask == EBhMsiAiTask::CoordinateExplain) sStart = _T("Sending 3D coordinates to Meta AI...");
	else sStart.Format(_T("Sending %d image(s) to Meta SAM 3.1 + Muse for segmentation..."), (int)job.vImageFiles.size());
	appendAiResult(sStart);
	const HWND hWnd = m_hWnd;
	m_aiFuture = std::async(std::launch::async, [hWnd, job = std::move(job)]() {
		SBhMsiAiResult* pResult = new SBhMsiAiResult(CBhMsiAiAnalyst::runFile(job));
		if (!::IsWindow(hWnd) || !::PostMessage(hWnd, WM_USER_AI_DONE, 0, (LPARAM)pResult)) delete pResult;
	});
}

void CDlgBhMsiViewer::startAiJobImgPanel(SBhMsiAiJob&& job)
{
	m_bAiBusy = true;
	setAiButtonsEnabled(FALSE);

	CString sStart;
	if (job.eTask == EBhMsiAiTask::SegmentationReport) sStart = _T("Sending segmented MSI to Meta AI...");
	else if (job.eTask == EBhMsiAiTask::CoordinateExplain) sStart = _T("Sending 3D coordinates to Meta AI...");
	else sStart.Format(
		_T("Sending %d image(s) to Meta SAM + Muse for segmentation..."),
		static_cast<int>(job.vImages.size() + job.vImageFiles.size()));

	appendAiResult(sStart);
	const HWND hWnd = m_hWnd;
	m_aiFuture = std::async(std::launch::async, [hWnd, job = std::move(job)]() {
		SBhMsiAiResult* pResult = new SBhMsiAiResult(CBhMsiAiAnalyst::runPanel(job));
		if (!::IsWindow(hWnd) || !::PostMessage(hWnd, WM_USER_AI_DONE, 0, (LPARAM)pResult)) delete pResult;
	});
}

void CDlgBhMsiViewer::OnBnClickedBhBtnAiMsiReport()
{
	if (m_bAiBusy) return;
	UpdateData(TRUE);
	SBhMsiAiJob job;
	CString sErr;
	if (!buildAiJob(EBhMsiAiTask::SegmentationReport, job, sErr)) { appendAiResult(sErr); return; }
	startAiJobImgPanel(std::move(job));
}

void CDlgBhMsiViewer::OnBnClickedBhBtnAiExplain()
{
	if (m_bAiBusy) return;
	UpdateData(TRUE);
	SBhMsiAiJob job;
	CString sErr;
	if (!buildAiJob(EBhMsiAiTask::CoordinateExplain, job, sErr)) { appendAiResult(sErr); return; }
	startAiJobImgPanel(std::move(job));
}

#ifdef BH_ENABLE_FAL_SAM
// fal.ai SAM 3 (third-party, https://fal.run — not affiliated with Meta).
// Isolated opt-in extra: only compiled and only called when BH_ENABLE_FAL_SAM
// is defined and FAL_KEY is set. Runs on a worker thread (startFalSamJob) so
// the UI thread is never blocked by the WinHTTP calls.
CString CDlgBhMsiViewer::segmentWithFalSam(const std::vector<CString>& vFiles, const CString& sPrompt)
{
	delete m_pSam3Client;
	m_pSam3Client = new CMetaSAM3Client();   // https://fal.run/fal-ai/sam-3/image, key from FAL_KEY
	m_pSam3Client->SetTimeouts(5000, 10000, 120000, 300000);

	CString sOut;
	int nOk = 0, nFail = 0;
	for (const CString& sFile : vFiles) {
		std::vector<CSam3Instance> instances;
		int nWidth = 0, nHeight = 0;
		CString sLine;
		if (m_pSam3Client->Segment(sFile, sPrompt, instances, nWidth, nHeight)) {
			nOk++;
			sLine.Format(_T("fal.ai SAM 3: %s - %d mask(s), %dx%d px"),
				(LPCTSTR)sFile, (int)instances.size(), nWidth, nHeight);
		}
		else {
			nFail++;
			sLine = _T("fal.ai SAM 3 failed: ") + sFile + _T("\r\nError: ") + m_pSam3Client->m_lastError;
		}
		sOut += sLine + _T("\r\n");
	}

	CString sSum;
	sSum.Format(_T("fal.ai SAM 3 complete. Success: %d  Failed: %d"), nOk, nFail);
	sOut += sSum;
	return sOut;
}

void CDlgBhMsiViewer::startFalSamJob(std::vector<CString> vFiles, CString sPrompt)
{
	m_bFalBusy = true;
	CString sStart;
	sStart.Format(_T("Sending %d image(s) to fal.ai SAM 3 (third-party, not Meta)..."), (int)vFiles.size());
	appendAiResult(sStart);

	const HWND hWnd = m_hWnd;
	m_falFuture = std::async(std::launch::async,
		[this, hWnd, vFiles = std::move(vFiles), sPrompt = std::move(sPrompt)]() {
			CString* pResult = new CString(segmentWithFalSam(vFiles, sPrompt));
			if (!::IsWindow(hWnd) || !::PostMessage(hWnd, WM_USER_FAL_DONE, 0, (LPARAM)pResult))
				delete pResult;
		});
}

LRESULT CDlgBhMsiViewer::OnFalSamDone(WPARAM, LPARAM lParam)
{
	std::unique_ptr<CString> pResult(reinterpret_cast<CString*>(lParam));
	m_bFalBusy = false;
	if (pResult) appendAiResult(*pResult);
	return 0;
}
#endif // BH_ENABLE_FAL_SAM

#ifdef BH_ENABLE_FAL_SAM
// Original fal.ai SAM 3 batch, built on a worker thread (startFalSamJob) so the
// UI thread is never blocked by the WinHTTP calls (up to several minutes per image).
CString CDlgBhMsiViewer::segmentWithFalSam(const std::vector<CString>& vFiles, const CString& sPrompt)
{
	delete m_pSam3Client;
	m_pSam3Client = new CMetaSAM3Client();   // https://fal.run/fal-ai/sam-3/image, key from FAL_KEY
	m_pSam3Client->SetTimeouts(5000, 10000, 120000, 300000);

	CString sOut;
	int nOk = 0, nFail = 0;
	for (const CString& sFile : vFiles) {
		std::vector<CSam3Instance> instances;
		int nWidth = 0, nHeight = 0;
		CString sLine;
		if (m_pSam3Client->Segment(sFile, sPrompt, instances, nWidth, nHeight)) {
			nOk++;
			sLine.Format(_T("fal.ai SAM 3: %s - %d mask(s), %dx%d px"),
				(LPCTSTR)sFile, (int)instances.size(), nWidth, nHeight);
		}
		else {
			nFail++;
			sLine = _T("fal.ai SAM 3 failed: ") + sFile + _T("\r\nError: ") + m_pSam3Client->m_lastError;
		}
		sOut += sLine + _T("\r\n");
	}

	CString sSum;
	sSum.Format(_T("fal.ai SAM 3 complete. Success: %d  Failed: %d"), nOk, nFail);
	sOut += sSum;
	return sOut;
}

void CDlgBhMsiViewer::startFalSamJob(std::vector<CString> vFiles, CString sPrompt)
{
	m_bFalBusy = true;
	CString sStart;
	sStart.Format(_T("Sending %d image(s) to fal.ai SAM 3..."), (int)vFiles.size());
	appendAiResult(sStart);

	const HWND hWnd = m_hWnd;
	m_falFuture = std::async(std::launch::async,
		[this, hWnd, vFiles = std::move(vFiles), sPrompt = std::move(sPrompt)]() {
			CString* pResult = new CString(segmentWithFalSam(vFiles, sPrompt));
			if (!::IsWindow(hWnd) || !::PostMessage(hWnd, WM_USER_FAL_DONE, 0, (LPARAM)pResult))
				delete pResult;
		});
}

LRESULT CDlgBhMsiViewer::OnFalSamDone(WPARAM, LPARAM lParam)
{
	std::unique_ptr<CString> pResult(reinterpret_cast<CString*>(lParam));
	m_bFalBusy = false;
	if (pResult) appendAiResult(*pResult);
	return 0;
}

#endif

LRESULT CDlgBhMsiViewer::OnAiDone(WPARAM, LPARAM lParam)
{
	std::unique_ptr<SBhMsiAiResult> pResult(reinterpret_cast<SBhMsiAiResult*>(lParam));
	m_bAiBusy = false;
	setAiButtonsEnabled(TRUE);
	if (!pResult) return 0;
	appendAiResult(pResult->sText);
	if (!pResult->bOk && !pResult->sError.IsEmpty() && pResult->eTask != EBhMsiAiTask::ImageSegmentation)
		appendAiResult(_T("Meta AI request failed: ") + pResult->sError);

	::PostMessage(GetSafeHwnd(), WM_MSI_SEG_DONE, 0, 0);
	return 0;
	//::PostMessage(GetSafeHwnd(), WM_MSI_SEG_DONE, 0, (LPARAM)pResult->sError.GetString());
}

void CDlgBhMsiViewer::OnBnClickedBhBtnMetaLlamaApi()
{
	UpdateData(TRUE);
	CString sResult =	CallMetaApiFromCpp();
	m_sAiAnalysisResult += _T("\r\nMeta LLaMA API result:\r\n") + sResult;
	UpdateData(FALSE);
}

void CDlgBhMsiViewer::OnOperationCheckcrediential()
{
	// Meta SAM 3.1 and Muse both require a Meta AI credential (api.meta.ai),
	// independent of fal.ai, which is a separate, optional third-party provider.
	CMetaAiClient metaClient;
	if (!metaClient.HasApiKey()) {
		appendAiResult(
			_T("Meta SAM 3.1 cannot start: ") +
			metaClient.m_lastError);
		AfxMessageBox(
			_T("Meta AI credential not found.\n\n")
			_T("Please set the environment variable META_AI_API_KEY to your Meta AI key.\n")
			_T("See https://llama.developer.meta.com for details."), MB_ICONERROR);
		return;
	}
	else {
		appendAiResult(_T("Meta AI credential found. Ready to run SAM 3.1 and Muse."));
		AfxMessageBox(_T("Meta AI credential found. Ready to run SAM 3.1 and Muse."), MB_ICONINFORMATION);
	}
}

void CDlgBhMsiViewer::OnOperationFalaisam()
{
#ifdef BH_ENABLE_FAL_SAM

	if (m_bAiBusy) return;
	UpdateData(TRUE);


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
		_T("Image Files (*.png; *.jpg; *.jpeg; *.bmp; *.gif; *.tif)|")
		_T("*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|")
		_T("All Files (*.*)|*.*||"),
		this);

	dlg.m_ofn.lpstrFile = fileBuffer.data();
	dlg.m_ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());

	if (dlg.DoModal() != IDOK) return;
	SBhMsiAiJob job;
	job.eTask = EBhMsiAiTask::ImageSegmentation;
	job.sModel = m_sAiModel;

	if (m_sPrompt != g_szDefaultSamPrompt) {
		job.sInstruction = static_cast<LPCSTR>(CW2A(m_sPrompt, CP_UTF8));
	}

	POSITION pos = dlg.GetStartPosition();
	while (pos != nullptr) {
		job.vImageFiles.push_back(dlg.GetNextPathName(pos));
	}

	if (job.vImageFiles.empty()) {
		return;
	}

	// Meta SAM 3.1 runs through job.bUseSam inside CBhMsiAiAnalyst / CMetaAiClient
	// (api.meta.ai). The separate fal.ai path below is a third-party, opt-in
	// extra, isolated behind BH_ENABLE_FAL_SAM; it is not part of the Meta flow.
	const bool hasFalKey = (GetEnvironmentVariableW(L"FAL_KEY", nullptr, 0) > 1);
	if (hasFalKey && !m_bFalBusy) {
		startFalSamJob(job.vImageFiles, m_sPrompt);
	}

	return;
#endif

	AfxMessageBox(_T("fal.ai SAM 3.1 is not enabled in this build. Rebuild with BH_ENABLE_FAL_SAM defined to enable it."), MB_ICONINFORMATION);
}

int CDlgBhMsiViewer::openSegmentImgFiles()
{
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
		_T("Image Files (*.png; *.jpg; *.jpeg; *.bmp; *.gif; *.tif)|")
		_T("*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|")
		_T("All Files (*.*)|*.*||"),
		this);

	dlg.m_ofn.lpstrFile = fileBuffer.data();
	dlg.m_ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());

	if (dlg.DoModal() != IDOK) return false;
	SBhMsiAiJob job;
	job.eTask = EBhMsiAiTask::ImageSegmentation;
	job.sModel = m_sAiModel;

	if (m_sPrompt != g_szDefaultSamPrompt) {
		job.sInstruction = static_cast<LPCSTR>(CW2A(m_sPrompt, CP_UTF8));
	}

	POSITION pos = dlg.GetStartPosition();
	while (pos != nullptr) {
		job.vImageFiles.push_back(dlg.GetNextPathName(pos));
	}

	if (job.vImageFiles.empty()) {
		return false;
	}


	startAiJobImgFiles(std::move(job));
	return true;

	//if (m_bAiBusy) return 0;

	//// Temp folder for the three exported views.
	//TCHAR szTemp[MAX_PATH + 1] = {};
	//if (::GetTempPath(MAX_PATH, szTemp) == 0) {
	//	appendAiResult(_T("Cannot get the temp folder."));
	//	return 0;
	//}
	//CString sFolder = CString(szTemp) + _T("BhMsiSam\\");
	//::CreateDirectory(sFolder, nullptr);

	//static const LPCTSTR kszViewName[BH_VIEW_COUNT] = { _T("axial"), _T("coronal"), _T("sagittal") };

	//SBhMsiAiJob job;
	//job.eTask = EBhMsiAiTask::ImageSegmentation;
	//job.sModel = m_sAiModel;
	//job.bUseSam = true;   // SAM 3.1 segmentation, then the Muse model analyses it

	//if (m_sPrompt != g_szDefaultSamPrompt) {
	//	job.sInstruction = static_cast<LPCSTR>(CW2A(m_sPrompt, CP_UTF8));
	//}

	//// Render the current MRI + source overlay of each pane directly to an image.
	//// Pane order matches g_uPaneId: IDC_BH_PANE_AXIAL, _CORONAL, _SAGITTAL.
	//for (int iView = 0; iView < BH_VIEW_COUNT; ++iView) {
	//	CString sPath;
	//	sPath.Format(_T("%sMSI_%s.png"), (LPCTSTR)sFolder, kszViewName[iView]);
	//	if (saveViewImage(iView, sPath, false)) {
	//		job.vImageFiles.push_back(sPath);
	//	}
	//}

	//if (job.vImageFiles.empty()) {
	//	appendAiResult(_T("No MSI view could be rendered. Load an MRI first."));
	//	return 0;
	//}

	//const int nCount = static_cast<int>(job.vImageFiles.size());
	//startAiJob(std::move(job));
	//return nCount;
}
 
int CDlgBhMsiViewer::setPanelImagToSegment()
{

	if (m_bAiBusy) return 0;
	if (!UpdateData(TRUE)) return 0;

	static const char* const kszViewName[BH_VIEW_COUNT] = {
		"Axial", "Coronal", "Sagittal"
	};

	SBhMsiAiJob job;
	job.eTask = EBhMsiAiTask::ImageSegmentation;
	job.sModel = m_sAiModel;
	job.bUseSam = true;

	if (m_sPrompt != g_szDefaultSamPrompt) {
		job.sInstruction = static_cast<LPCSTR>(CW2A(m_sPrompt, CP_UTF8));
	}

	job.sContext =
		"These images are orthogonal views of the current MRI with a "
		"MEG/EEG source overlay. Identify the view by its caption.";

	job.vImages.reserve(BH_VIEW_COUNT);

	// Same view order as g_uPaneId: axial, coronal, sagittal.
	// Render the image content, not the window borders or controls.
	for (int iView = 0; iView < BH_VIEW_COUNT; ++iView) {
		std::vector<uint32_t> vBgra;
		int nCol = 0, nRow = 0;

		if (!renderViewBgra(iView, vBgra, nCol, nRow, false)) {
			appendAiResult(_T("Cannot render all three MSI views. Load an MRI first."));
			return 0;
		}

		SBhLlamaImage im;
		im.sMime = "image/png";
		im.sCaption = kszViewName[iView];

		if (!CBhMsiAiAnalyst::encodePng(vBgra, nCol, nRow, im.sBase64)) {
			appendAiResult(_T("Cannot encode an MSI view as an in-memory PNG."));
			return 0;
		}

		job.vImages.push_back(std::move(im));
	}

	const int nCount = static_cast<int>(job.vImages.size());
	startAiJobImgPanel(std::move(job));
	return nCount;
 }

void CDlgBhMsiViewer::OnBnClickedBhBtnMetaSam()
{
	if (m_bAiBusy) return;
	UpdateData(TRUE);
	GetDlgItem(IDC_SPINNER)->EnableWindow(FALSE);
	m_spinner.Start();

	setPanelImagToSegment();
}

void CDlgBhMsiViewer::OnEditLoadimagesforsegmentation()
{
	if (m_bAiBusy) return;
	UpdateData(TRUE);
	GetDlgItem(IDC_SPINNER)->EnableWindow(FALSE);
	m_spinner.Start();
	openSegmentImgFiles();
}

void CDlgBhMsiViewer::OnEditFile()
{
	if (m_bAiBusy) return;
	UpdateData(TRUE);
	SBhMsiAiJob job;
	CString sErr;
	if (!buildAiJob(EBhMsiAiTask::SegmentationReport, job, sErr)) { appendAiResult(sErr); return; }
	startAiJobImgFiles(std::move(job));

}

void CDlgBhMsiViewer::OnEditFileimageexplanation()
{
	if (m_bAiBusy) return;
	UpdateData(TRUE);
	SBhMsiAiJob job;
	CString sErr;
	if (!buildAiJob(EBhMsiAiTask::CoordinateExplain, job, sErr)) { appendAiResult(sErr); return; }
	startAiJobImgFiles(std::move(job));
}
LRESULT CDlgBhMsiViewer::OnSegDone2Spinner(WPARAM, LPARAM lp)
{
	std::unique_ptr<CString> report((CString*)lp);
	m_spinner.Stop();
	m_bBusy = false;
	GetDlgItem(IDC_SPINNER)->EnableWindow(TRUE);
	// ... show *report ...
	return 0;
}
void CDlgBhMsiViewer::OnOperationTestllama()
{
	UpdateData(TRUE);
	CString sResult = CallMetaApiFromCpp();
	m_sAiAnalysisResult += _T("\r\nMeta LLaMA API result:\r\n") + sResult;
	UpdateData(FALSE);
}

void CDlgBhMsiViewer::OnOperationTestmetamodal()
{
	// Disable button or show status if desired...
	//GetDlgItem(IDC_BH_BTN_META_MODAL)->EnableWindow(FALSE);

	// Launch the network request asynchronously so the MFC UI message loop never freezes
	m_modalFuture = std::async(std::launch::async, [this]() {
		this->ExecuteModalTest();
		});
}

void CDlgBhMsiViewer::OnBnClickedBtnReadAloud()
{
	CString sText = m_sPrompt + m_sAiAnalysisResult;
	if (m_tts.Speak(sText))
	{
		GetDlgItem(IDC_BTN_READ_ALOUD)->EnableWindow(FALSE);
		GetDlgItem(IDC_BTN_STOP_READING)->EnableWindow(TRUE);
	}
}

void CDlgBhMsiViewer::OnBnClickedBtnStopReading()
{
	m_tts.Stop();
	GetDlgItem(IDC_BTN_READ_ALOUD)->EnableWindow(TRUE);
	GetDlgItem(IDC_BTN_STOP_READING)->EnableWindow(FALSE);
}

LRESULT CDlgBhMsiViewer::OnTtsEvent(WPARAM, LPARAM)
{
	if (m_tts.HandleEvent())
	{
		GetDlgItem(IDC_BTN_READ_ALOUD)->EnableWindow(TRUE);
		GetDlgItem(IDC_BTN_STOP_READING)->EnableWindow(FALSE);
	}
	return 0;
}
void CDlgBhMsiViewer::OnDestroy()
{
	m_tts.Shutdown();      // before the window goes away


	CDialogEx::OnDestroy();

	// TODO: Add your message handler code here
}
