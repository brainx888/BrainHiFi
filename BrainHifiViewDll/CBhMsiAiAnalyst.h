#pragma once
/*
 * CBhMsiAiAnalyst - the two MSI AI tasks on top of CMetaAiClient (https://api.meta.ai/v1/responses).
 *
 *   Task 1 (SegmentationReport): local cluster segmentation of the source map
 *     (CBhMsiPeakAnalyzer) + rendered MSI views through the peaks are sent to
 *     a Meta AI model (muse-wonder-1.0), which returns a text report on the peaks and
 *     their anatomical locations.
 *   Task 2 (CoordinateExplain): 3D head-mm points with source values are sent
 *     to the model, which returns location / function / meaning per point.
 *   Task 3 (ImageSegmentation): user-picked image files (e.g. exported MSI
 *     slices) are sent one by one to Meta sam-3.1 (pixel boxes of the
 *     activations) and then, with those boxes, to muse-wonder-1.0, which
 *     describes their locations.  Uses the same Meta AI key - no fal.ai key.
 *
 * Model: job.sModel, overridden by the META_AI_MODEL environment variable.
 * If the API rejects the model name, the enabled models are listed
 * (GET /v1/models) and the request is retried once with an enabled one.
 *
 * Build an SBhMsiAiJob on the UI thread (rendering touches the volumes),
 * then call run() on a worker thread; it does not touch the dialog.
 * Only coordinates, values and rendered pixels are sent - no names, IDs or
 * file paths.
 */
#include "core/BhMsiPeakAnalyzer.h"
#include "core/BhLlamaProtocol.h"
#include <cstdint>
#include <string>
#include <vector>

enum class EBhMsiAiTask { SegmentationReport, CoordinateExplain, ImageSegmentation };

struct SBhMsiAiJob
{
	EBhMsiAiTask eTask = EBhMsiAiTask::SegmentationReport;
	SBhMsiSegmentResult seg;
	std::vector<SBhMsiPoint> vPoints;          // task 2
	std::vector<SBhLlamaImage> vImages;        // task 1, PNG base64
	std::string sContext;                      // free text from the user (UTF-8)
	std::vector<CString> vImageFiles;          // task 3, read on the worker thread
	std::string sInstruction;                  // task 3, user's segmentation prompt (UTF-8)
	std::string sModel = "muse-wonder-1.0";    // Meta AI model for text / image reports
	std::string sEffort = "high";              // reasoning effort: "low" | "medium" | "high" | ""
	bool bUseSam = true;                       // task 3: run Meta sam-3.1 first for pixel boxes
	std::string sSamPrompt;                    // task 3: SAM noun phrase ("" = short instruction or "colored blob")
	int nMaxTokens = 2048;
};

struct SBhMsiAiResult
{
	EBhMsiAiTask eTask = EBhMsiAiTask::SegmentationReport;
	bool bOk = false;
	CString sText;                             // report to show, CRLF line ends
	CString sError;
	std::vector<SBhMsiPointExplanation> vExplanations;   // task 2, if the reply was JSON
	int nSucceeded = 0, nFailed = 0;           // task 3, per image
};

class   AFX_EXT_CLASS CBhMsiAiAnalyst
{
public:
	/// 32-bit BGRA (CBhSliceRenderer output) -> PNG -> base64.  Needs GDI+ started.
	static bool encodePng(const std::vector<uint32_t>& vBgra, int nCol, int nRow, std::string& sBase64);

	/// Image file -> base64 for the API.  PNG/JPEG up to kMaxSide px are sent as is; other formats
	/// (BMP, GIF, TIFF) and larger images are scaled down and re-encoded as PNG.  Needs no GDI+ startup.
	static bool loadImageFile(const CString& sPath, SBhLlamaImage& im, CString& sError);
	static constexpr int kMaxSide = 2048;

	/// Blocking; call from a worker thread.
	static SBhMsiAiResult runFile(const SBhMsiAiJob& job);
	static SBhMsiAiResult runPanel(const SBhMsiAiJob& job);

	static CString toDisplay(const std::string& sUtf8);  // UTF-8, LF -> CString, CRLF
};
