#pragma once
/*
 * CDlgBhMsiViewer - BrainHifi MRI + MEG source viewer.
 *
 * Shows the MRI (CAnaData file) in axial, coronal and sagittal 2D views with
 * the MEG volumetric source image (CVolumSourcImag file) overlaid in a hot
 * (positive) / cool (negative) colour map.  Click or drag in a view to move
 * the cursor, use the mouse wheel over a view to step its slice.
 *
 * Usage from the host:
 *     CDlgBhMsiViewer dlg(this);
 *     dlg.m_sMriPath = _T("...");      // optional, loaded in OnInitDialog
 *     dlg.m_sSrcPath = _T("...");      // optional
 *     dlg.DoModal();
 * or setMri()/setSource() with volumes already in memory
 * (CBhAnaImport::fromAnaData / CBhSrcImport::fromVolumSourcImag).
 */

#include <future>
#include <string>
#include <vector>
#include <atomic>
#include "ResourceBhMsiViewer.h"
#include "core/BhMriVolume.h"
#include "core/BhSourceVolume.h"
#include "core/BhSliceRenderer.h"
#include "CBhMsiAiAnalyst.h"
#include "CStaticBusySpinnerx.h"
#include "CTtsReader.h"

 // This project targets Meta AI exclusively. fal.ai (CMetaSAM3Client, a
 // third-party provider at https://fal.run, unaffiliated with Meta) is an
 // optional, isolated extra kept only for users who separately hold a
 // fal.ai key. Define BH_ENABLE_FAL_SAM in the project settings to compile
 // it back in; it is OFF by default so no fal.ai code ships or runs
 // otherwise.
#ifdef BH_ENABLE_FAL_SAM
#include "CMetaSAM3Client.h"
#endif

enum SEGMENT_IMAGE_TYPE
{
   PANEL_MSI,
   FILE_MSI
};

#define SRCIMG_PLAY_MSG	  100
#define WM_USER_RESET_BUTTON  (WM_USER + 100)
#define WM_USER_AI_DONE       (WM_USER + 101)   // LPARAM = SBhMsiAiResult*, owned by the handler
#define WM_USER_FAL_DONE      (WM_USER + 102)   // LPARAM = CString*, owned by the handler
#define WM_MSI_SEG_DONE (WM_APP + 103)


class AFX_EXT_CLASS CDlgBhMsiViewer : public CDialogEx
{
	DECLARE_DYNAMIC(CDlgBhMsiViewer)

public:
	enum { IDD = IDD_BH_MSI_VIEWER };
	enum { BH_VIEW_COUNT = 3 };

	explicit CDlgBhMsiViewer(CWnd* pParent = nullptr);
	virtual ~CDlgBhMsiViewer();
	DECLARE_MESSAGE_MAP()
public:
	CBhMriVolume m_mri;
	CBhSourceVolume m_src;
	SBhRenderParam m_param;
	double m_dSrcMaxAbs = 1.0;

	int m_bPlaySrcMovie = FALSE;
	int m_nSrcPlayIntvl = 100;     // ms per frame
	UINT_PTR m_uTimeID = 0;
	CButton m_btnSrcImgPlay;
	CString m_sSrcPath;
	CString m_sMriPath;

protected:
	CSliderCtrl m_sldAxial;
	CSliderCtrl m_sldCoronal;
	CSliderCtrl m_sldSagittal;
	CSliderCtrl m_sldTime;
	CSliderCtrl m_sldThreshold;
	CSliderCtrl m_sldOpacity;
	CSliderCtrl m_sldGrayLow;
	CSliderCtrl m_sldGrayHigh;
	CComboBox m_cmbItem;

	CRect m_rcImage[BH_VIEW_COUNT];
	int m_nImageCol[BH_VIEW_COUNT] = { 0, 0, 0 };
	int m_nImageRow[BH_VIEW_COUNT] = { 0, 0, 0 };
	std::vector<uint32_t> m_vBgra;
	int m_iDragView = -1;

public:
	CWnd* m_pParent = nullptr;


	CString m_sPrompt = _T("Segment the objects in this image.");
	CString m_sAiAnalysisResult;

public:
#ifdef BH_ENABLE_FAL_SAM
	CMetaSAM3Client* m_pSam3Client = nullptr;   // fal.ai (third-party, not Meta) - opt-in only
#endif



	// ---- Meta AI MSI analysis (CBhMsiAiAnalyst) ----
public:
	std::string m_sAiModel = "muse-wonder-1.0";
	int m_nAiMaxClusters = 8;        // clusters in the segmentation table
	int m_nAiImageClusters = 2;      // clusters rendered as 3 views each for the vision model
	int m_nAiExplainPeaks = 5;       // peaks sent with the cursor for coordinate explanation
	SEGMENT_IMAGE_TYPE m_nSegmentImageType = PANEL_MSI;   // 0 = panel image, 1 =  file image;
private:
	std::future<void> m_aiFuture;
	std::atomic<bool> m_bAiBusy{ false };
	std::future<void> m_modalFuture;        // Stores the async task handle

	CStaticBusySpinnerx m_spinner;
	std::thread      m_worker;
	std::atomic<bool> m_bBusy{ false };

	CTtsReader m_tts;  // Text-to-speech for AI explanation
private:
	void ExecuteModalTest();                // Background worker function
public:
	// Export the MSI (MRI + MEG/EEG source overlay) shown in the
	// IDC_BH_PANE_AXIAL / IDC_BH_PANE_CORONAL / IDC_BH_PANE_SAGITTAL panes
	// to image files (PNG / JPEG / BMP / TIFF, chosen by the extension) for
	// segmentation with external tools such as Meta SAM 3.0.
	// Images are native voxel resolution, 24-bit RGB, crosshair off by default.
	bool renderViewBgra(int iView, std::vector<uint32_t>& vBgra, int& nCol, int& nRow,
		bool bCrosshair = false) const;
	bool renderViewBgra(int iView, const SBhRenderParam& param,
		std::vector<uint32_t>& vBgra, int& nCol, int& nRow) const;
	bool saveViewImage(int iView, LPCTSTR szPath, bool bCrosshair = false) const;
	/// Save the three current views; returns how many were written.
	int  saveAllViews(LPCTSTR szFolder, bool bCrosshair = false) const;
	/// Save every nStep-th slice of one view (frame series for SAM 3 video
	/// segmentation); returns how many were written.
	int  saveSliceSeries(int iView, LPCTSTR szFolder, int nStep = 1, bool bCrosshair = false) const;
public:

	/// Segments the current time/item (threshold = m_param.dThreshold) into peak clusters.
	bool segmentCurrentSource(SBhMsiSegmentResult& seg, CString& sError) const;
	bool buildAiJob(EBhMsiAiTask eTask, SBhMsiAiJob& job, CString& sError) const;
	void startAiJobImgFiles(SBhMsiAiJob&& job);
	void startAiJobImgPanel(SBhMsiAiJob&& job);
	void setAiButtonsEnabled(BOOL bEnable);
	void appendAiResult(const CString& s);
public:
	bool setMri(const CBhMriVolume& mri);
	bool setSource(const CBhSourceVolume& src);

	const CBhMriVolume& getMri() const { return m_mri; }
	const CBhSourceVolume& getSource() const { return m_src; }
	const SBhRenderParam& getRenderParam() const { return m_param; }

	void onMriChanged();
	void onSourceChanged();
	void updateSourceScale();
	void syncSlidersFromCursor();
	void updateLabels();
	void updateStatus();
	void updateInfo();
	void readOptions();
	void refreshViews();

	bool getPaneRect(int iView, CRect& rcPane);
	void drawPane(CDC& dc, int iView, const CRect& rcPane);
	bool hitImage(CPoint point, int& iView, int& nCol, int& nRow) const;
	void moveCursorTo(int iView, int nCol, int nRow);
	void stepSlice(int iView, int nStep);

	int openSegmentImgFiles();
	int setPanelImagToSegment();


#ifdef BH_ENABLE_FAL_SAM
	// fal.ai SAM 3 (third-party, not Meta AI) - isolated opt-in extra.
	// Needs FAL_KEY; never used unless BH_ENABLE_FAL_SAM is defined.
	std::future<void> m_falFuture;
	std::atomic<bool> m_bFalBusy{ false };
	void startFalSamJob(std::vector<CString> vFiles, CString sPrompt);
	CString segmentWithFalSam(const std::vector<CString>& vFiles, const CString& sPrompt);
#endif
#ifdef BH_ENABLE_FAL_SAM
	afx_msg LRESULT OnFalSamDone(WPARAM wParam, LPARAM lParam);
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnBnClickedLoadMri();
	afx_msg void OnBnClickedLoadSrc();
	afx_msg void OnBnClickedGotoPeak();
	afx_msg void OnBnClickedCenter();
	afx_msg void OnBnClickedOption();
	afx_msg void OnCbnSelchangeItem();
public:
	afx_msg void OnBnClickedBtnPlayStop();
	afx_msg void OnBnClickedBtnForward();
	afx_msg void OnBnClickedBtnReverse();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnBnClickedBhBtnOpen();
	afx_msg void OnBnClickedBhBtnSave();
	afx_msg void OnBnClickedBhBtnMetaModal();
	afx_msg void OnBnClickedBhBtnMetaSam();

	afx_msg LRESULT OnResetButton(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnAiDone(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnSegDone2Spinner(WPARAM, LPARAM);
	afx_msg LRESULT OnTtsEvent(WPARAM, LPARAM);

	afx_msg void OnBnClickedBhBtnAiMsiReport();
	afx_msg void OnBnClickedBhBtnAiExplain();
	afx_msg void OnBnClickedBhBtnMetaLlamaApi();
	afx_msg void OnOperationCheckcrediential();
	afx_msg void OnOperationFalaisam();
	afx_msg void OnEditLoadimagesforsegmentation();
	afx_msg void OnEditFile();
	afx_msg void OnEditFileimageexplanation();
	afx_msg void OnOperationTestllama();
	afx_msg void OnOperationTestmetamodal();
	afx_msg void OnBnClickedBtnReadAloud();
	afx_msg void OnBnClickedBtnStopReading();
	afx_msg void OnDestroy();
};
