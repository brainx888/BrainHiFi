#include "pch.h"
#include "CBhMsiAiAnalyst.h"
#include "CMetaAiClient.h"
#include <gdiplus.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")

namespace {

bool pngEncoderClsid(CLSID& clsid)
{
	UINT nNum = 0, nSize = 0;
	if (Gdiplus::GetImageEncodersSize(&nNum, &nSize) != Gdiplus::Ok || nSize == 0) return false;
	std::vector<BYTE> buf(nSize);
	auto* pInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
	if (Gdiplus::GetImageEncoders(nNum, nSize, pInfo) != Gdiplus::Ok) return false;
	for (UINT i = 0; i < nNum; i++)
		if (wcscmp(pInfo[i].MimeType, L"image/png") == 0) { clsid = pInfo[i].Clsid; return true; }
	return false;
}

const ULONGLONG kMaxFileBytes = 64ull << 20;
const ULONGLONG kMaxRawBytes = 8ull << 20;

std::string utf8(const CString& s) { return std::string(CW2A(s, CP_UTF8)); }

std::string modelFromEnvironment(const std::string& sDefault)
{
	wchar_t buf[256] = {};
	const DWORD n = GetEnvironmentVariableW(L"META_AI_MODEL", buf, 256);
	if (n == 0 || n >= 256) return sDefault;
	CStringW s(buf);
	s.Trim();
	return s.IsEmpty() ? sDefault : std::string(CW2A(s, CP_UTF8));
}

// Chat; on a model-name / entitlement error look up the enabled models and retry once with one of them.
// sModel is updated to the model that worked so later requests skip the failed one.
bool chatWithModelFallback(CMetaAiClient& client, SBhLlamaRequest& q, SBhLlamaReply& reply,
                           std::string& sModel, std::string& sNote)
{
	q.sModel = sModel;
	if (client.Chat(q, reply)) return true;
	if (!CBhLlamaProtocol::isModelError(utf8(client.m_lastError))) return false;
	const CString sChatError = client.m_lastError;
	std::vector<std::string> vIds;
	if (!client.ListModels(vIds)) { client.m_lastError = sChatError; return false; }
	if (vIds.empty()) {
		client.m_lastError = sChatError +
			_T("\r\nGET https://api.meta.ai/v1/models lists no models for this API key. Check the plan / model access ")
			_T("for this key on the Meta developer site, or store a key that has models with tools\\StoreMetaAiKey.ps1 ")
			_T("(or META_AI_API_KEY).");
		return false;
	}
	std::string sList;
	for (const std::string& m : vIds) sList += (sList.empty() ? "" : ", ") + m;
	const std::string sAlt = CBhLlamaProtocol::chooseModel(vIds, sModel, !q.vImages.empty());
	if (sAlt.empty() || sAlt == sModel) {
		client.m_lastError = sChatError + _T("\r\nModels enabled for this key: ") + CString(CA2W(sList.c_str(), CP_UTF8))
			+ (q.vImages.empty() ? CString() : CString(_T(" (images need muse-wonder or muse-spark)")));
		return false;
	}
	sNote += "Model '" + sModel + "' is not enabled for this key; using '" + sAlt + "' (set META_AI_MODEL or m_sAiModel to choose).\n";
	sModel = sAlt;
	q.sModel = sAlt;
	return client.Chat(q, reply);
}

// SAM wants a short noun phrase ("colored blob"); a long instruction goes to the vision model only.
std::string samPromptFor(const SBhMsiAiJob& job)
{
	if (!job.sSamPrompt.empty()) return job.sSamPrompt;
	const std::string& s = job.sInstruction;
	if (!s.empty() && s.size() <= 40 && s.find_first_of(".?!:\n") == std::string::npos) return s;
	return "colored blob";
}

bool isFatalForBatch(const CString& sError)
{
	const std::string e = utf8(sError);
	return e.find("HTTP 401") != std::string::npos || e.find("No Meta AI API key") != std::string::npos
	    || CBhLlamaProtocol::isModelError(e);
}

//open and loaded imagine files;
SBhMsiAiResult runImageFiles(const SBhMsiAiJob& job)
{
	SBhMsiAiResult r;
	r.eTask = job.eTask;
	std::string sModel = modelFromEnvironment(job.sModel);
	std::string sOut;
	CString sFatal;
	CMetaAiClient client;
	client.SetReasoningEffort(job.sEffort);
	const int nFiles = (int)job.vImageFiles.size();
	for (int i = 0; i < nFiles; i++) {
		const CString& sPath = job.vImageFiles[(size_t)i];
		sOut += "\n=== " + utf8(sPath) + " ===\n";
		if (!sFatal.IsEmpty()) { r.nFailed++; sOut += "Skipped (see the error above).\n"; continue; }
		SBhLlamaRequest q;
		q.nMaxTokens = job.nMaxTokens;
		q.sSystem = CBhLlamaProtocol::systemPrompt();
		SBhLlamaImage im;
		CString sErr;
		if (!CBhMsiAiAnalyst::loadImageFile(sPath, im, sErr)) {
			r.nFailed++;
			sOut += "Failed to read image: " + utf8(sErr) + "\n";
			continue;
		}
		CStringA sCap;
		sCap.Format("user image %d of %d", i + 1, nFiles);   // the file name is not sent
		q.sUser = CBhLlamaProtocol::buildImageSegmentationPrompt(job.sInstruction, job.sContext, (LPCSTR)sCap);
		if (job.bUseSam) {
			const std::string sSamPrompt = samPromptFor(job);
			std::vector<SBhSam3Box> vBoxes;
			if (client.Segment(sSamPrompt, im, vBoxes)) {
				const std::string sSam = CBhMetaAiProtocol::formatSam3(vBoxes, sSamPrompt);
				sOut += "--- Meta " + client.m_sSamModel + " ---\n" + sSam;
				q.sUser += "\n\nMeta SAM 3.1 segmentation of this image (use these regions):\n" + sSam;
			}
			else sOut += "SAM 3.1 segmentation failed: " + utf8(client.m_lastError) + "\n";
		}
		q.vImages.push_back(im);
		SBhLlamaReply reply;
		std::string sNote;
		if (chatWithModelFallback(client, q, reply, sModel, sNote)) {
			r.nSucceeded++;
			sOut += sNote + "--- Meta " + sModel + " ---\n" + reply.sText + "\n";
		}
		else {
			r.nFailed++;
			sOut += sNote + "Failed to segment: " + utf8(client.m_lastError) + "\n";
			if (isFatalForBatch(client.m_lastError)) sFatal = client.m_lastError;
		}
	}
	CStringA sSum;
	sSum.Format("\nBatch processing complete.\nSuccess: %d\nFailed: %d\n", r.nSucceeded, r.nFailed);
	sOut += (LPCSTR)sSum;
	if (r.nSucceeded > 0) {
		sOut += "(AI output for research use only (e.g., brain signal decoding, BCI command, robotic control).)\n";
		//sOut += "(AI output for research use only - not a clinical diagnosis.)\n";
	}
	r.bOk = r.nFailed == 0 && r.nSucceeded > 0;
	r.sText = CBhMsiAiAnalyst::toDisplay(sOut);
	if (!r.bOk && !sFatal.IsEmpty()) r.sError = sFatal;
	return r;
}

//images rendered from the 3 orthogonal panes of the MSI viewer, with the current MRI and source overlay.
SBhMsiAiResult runImagePanels(const SBhMsiAiJob& job)
{
	SBhMsiAiResult r;
	r.eTask = job.eTask;
	std::string sModel = modelFromEnvironment(job.sModel);
	std::string sOut;
	CString sFatal;
	CMetaAiClient client;
	client.SetReasoningEffort(job.sEffort);
	const size_t nMemoryImages = job.vImages.size();
	const size_t nImages = nMemoryImages + job.vImageFiles.size();

	for (size_t i = 0; i < nImages; ++i) {
		const bool bMemoryImage = i < nMemoryImages;
		const std::string sLabel = bMemoryImage
			? (job.vImages[i].sCaption.empty()
				? "Panel image " + std::to_string(i + 1)
				: job.vImages[i].sCaption)
			: utf8(job.vImageFiles[i - nMemoryImages]);

		sOut += "\n=== " + sLabel + " ===\n";
		if (!sFatal.IsEmpty()) {
			r.nFailed++;
			sOut += "Skipped (see the error above).\n";
			continue;
		}

		SBhLlamaRequest q;
		q.nMaxTokens = job.nMaxTokens;
		q.sSystem = CBhLlamaProtocol::systemPrompt();

		SBhLlamaImage im;
		if (bMemoryImage) {
			im = job.vImages[i];
			if (im.sBase64.empty()) {
				r.nFailed++;
				sOut += "Failed to read image: empty in-memory image.\n";
				continue;
			}
		}
		else {
			CString sErr;
			if (!CBhMsiAiAnalyst::loadImageFile(
				job.vImageFiles[i - nMemoryImages], im, sErr)) {
				r.nFailed++;
				sOut += "Failed to read image: " + utf8(sErr) + "\n";
				continue;
			}
		}

		const std::string sCaption = bMemoryImage
			? sLabel
			: "user image " + std::to_string(i + 1) +
			" of " + std::to_string(nImages);

		q.sUser = CBhLlamaProtocol::buildImageSegmentationPrompt(
			job.sInstruction, job.sContext, sCaption);
		
		if (job.bUseSam) {
			const std::string sSamPrompt = samPromptFor(job);
			std::vector<SBhSam3Box> vBoxes;
			if (client.Segment(sSamPrompt, im, vBoxes)) {
				const std::string sSam = CBhMetaAiProtocol::formatSam3(vBoxes, sSamPrompt);
				sOut += "--- Meta " + client.m_sSamModel + " ---\n" + sSam;
				q.sUser += "\n\nMeta SAM 3.1 segmentation of this image (use these regions):\n" + sSam;
			}
			else sOut += "SAM 3.1 segmentation failed: " + utf8(client.m_lastError) + "\n";
		}
		q.vImages.push_back(im);
		SBhLlamaReply reply;
		std::string sNote;
		if (chatWithModelFallback(client, q, reply, sModel, sNote)) {
			r.nSucceeded++;
			sOut += sNote + "--- Meta " + sModel + " ---\n" + reply.sText + "\n";
		}
		else {
			r.nFailed++;
			sOut += sNote + "Failed to segment: " + utf8(client.m_lastError) + "\n";
			if (isFatalForBatch(client.m_lastError)) sFatal = client.m_lastError;
		}
	}
	
	
	
	CStringA sSum;
	sSum.Format("\nBatch processing complete.\nSuccess: %d\nFailed: %d\n", r.nSucceeded, r.nFailed);
	sOut += (LPCSTR)sSum;
	if (r.nSucceeded > 0) {
		sOut += "(AI output for research use only (e.g., brain signal decoding, BCI command, robotic control).)\n";
		//sOut += "(AI output for research use only - not a clinical diagnosis.)\n";
	}
	r.bOk = r.nFailed == 0 && r.nSucceeded > 0;
	r.sText = CBhMsiAiAnalyst::toDisplay(sOut);
	if (!r.bOk && !sFatal.IsEmpty()) r.sError = sFatal;
	return r;
}

}

bool CBhMsiAiAnalyst::encodePng(const std::vector<uint32_t>& vBgra, int nCol, int nRow, std::string& sBase64)
{
	sBase64.clear();
	if (nCol <= 0 || nRow <= 0 || vBgra.size() < (size_t)nCol * nRow) return false;
	Gdiplus::GdiplusStartupInput gsi;
	ULONG_PTR nToken = 0;
	if (Gdiplus::GdiplusStartup(&nToken, &gsi, nullptr) != Gdiplus::Ok) return false;
	struct CShutdown { ULONG_PTR n; ~CShutdown() { Gdiplus::GdiplusShutdown(n); } } shutdown{ nToken };
	CLSID clsid;
	if (!pngEncoderClsid(clsid)) return false;
	{
	Gdiplus::Bitmap bmp(nCol, nRow, nCol * 4, PixelFormat32bppRGB, (BYTE*)vBgra.data());
	IStream* pStream = nullptr;
	if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &pStream))) return false;
	bool bOk = bmp.Save(pStream, &clsid, nullptr) == Gdiplus::Ok;
	if (bOk) {
		HGLOBAL hMem = nullptr;
		STATSTG st{};
		bOk = SUCCEEDED(GetHGlobalFromStream(pStream, &hMem)) && SUCCEEDED(pStream->Stat(&st, STATFLAG_NONAME));
		if (bOk) {
			const unsigned char* p = (const unsigned char*)GlobalLock(hMem);
			bOk = p != nullptr;
			if (bOk) sBase64 = CBhLlamaProtocol::base64(p, (size_t)st.cbSize.QuadPart);
			GlobalUnlock(hMem);
		}
	}
	pStream->Release();
	}
	return !sBase64.empty();
}

bool CBhMsiAiAnalyst::loadImageFile(const CString& sPath, SBhLlamaImage& im, CString& sError)
{
	im = SBhLlamaImage();
	sError.Empty();
	CFile f;
	if (!f.Open(sPath, CFile::modeRead | CFile::shareDenyWrite)) { sError = _T("cannot open ") + sPath; return false; }
	const ULONGLONG nLen = f.GetLength();
	if (nLen == 0 || nLen > kMaxFileBytes) { sError = _T("file is empty or larger than 64 MB"); return false; }
	std::vector<unsigned char> vRaw((size_t)nLen);
	if (f.Read(vRaw.data(), (UINT)nLen) != (UINT)nLen) { sError = _T("cannot read ") + sPath; return false; }
	f.Close();
	const std::string sMime = CBhLlamaProtocol::imageMimeFromPath(utf8(sPath));

	Gdiplus::GdiplusStartupInput gsi;
	ULONG_PTR nToken = 0;
	if (Gdiplus::GdiplusStartup(&nToken, &gsi, nullptr) != Gdiplus::Ok) { sError = _T("GDI+ startup failed"); return false; }
	struct CShutdown { ULONG_PTR n; ~CShutdown() { Gdiplus::GdiplusShutdown(n); } } shutdown{ nToken };
	std::vector<uint32_t> vBgra;
	int nCol = 0, nRow = 0;
	{
	const CStringW sPathW(sPath);
	Gdiplus::Bitmap src(sPathW);
	if (src.GetLastStatus() != Gdiplus::Ok || src.GetWidth() == 0 || src.GetHeight() == 0) {
		sError = _T("not a readable image (use PNG, JPEG, BMP, GIF or TIFF)");
		return false;
	}
	const int nW = (int)src.GetWidth(), nH = (int)src.GetHeight();
	if ((sMime == "image/png" || sMime == "image/jpeg") && nW <= kMaxSide && nH <= kMaxSide && nLen <= kMaxRawBytes) {
		im.sMime = sMime;
		im.sBase64 = CBhLlamaProtocol::base64(vRaw.data(), vRaw.size());
		return true;
	}
	const double dScale = (std::min)(1.0, (double)kMaxSide / (double)(std::max)(nW, nH));
	nCol = (std::max)(1, (int)(nW * dScale + 0.5));
	nRow = (std::max)(1, (int)(nH * dScale + 0.5));
	Gdiplus::Bitmap dst(nCol, nRow, PixelFormat32bppRGB);
	{
	Gdiplus::Graphics g(&dst);
	g.Clear(Gdiplus::Color(255, 0, 0, 0));
	g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
	g.DrawImage(&src, 0, 0, nCol, nRow);
	}
	Gdiplus::BitmapData bd{};
	Gdiplus::Rect rc(0, 0, nCol, nRow);
	if (dst.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppRGB, &bd) != Gdiplus::Ok) {
		sError = _T("cannot convert the image");
		return false;
	}
	vBgra.resize((size_t)nCol * nRow);
	for (int y = 0; y < nRow; y++)
		memcpy(&vBgra[(size_t)y * nCol], (const BYTE*)bd.Scan0 + (ptrdiff_t)y * bd.Stride, (size_t)nCol * 4);
	dst.UnlockBits(&bd);
	}
	if (!encodePng(vBgra, nCol, nRow, im.sBase64)) { sError = _T("PNG encoding failed"); return false; }
	im.sMime = "image/png";
	return true;
}

CString CBhMsiAiAnalyst::toDisplay(const std::string& sUtf8)
{
	CString s(CA2W(sUtf8.c_str(), CP_UTF8));
	s.Replace(_T("\r\n"), _T("\n"));
	s.Replace(_T("\n"), _T("\r\n"));
	return s;
}

SBhMsiAiResult CBhMsiAiAnalyst::runFile(const SBhMsiAiJob& job)
{
	if (job.eTask == EBhMsiAiTask::ImageSegmentation) {
		return runImageFiles(job);
	}
	SBhMsiAiResult r;
	r.eTask = job.eTask;
	std::string sModel = modelFromEnvironment(job.sModel);
	SBhLlamaRequest q;
	q.nMaxTokens = job.nMaxTokens;
	q.sSystem = CBhLlamaProtocol::systemPrompt();
	std::string sLocal;
	if (job.eTask == EBhMsiAiTask::SegmentationReport) {
		std::vector<std::string> vCaptions;
		for (const SBhLlamaImage& im : job.vImages) vCaptions.push_back(im.sCaption);
		q.sUser = CBhLlamaProtocol::buildSegmentationPrompt(job.seg, job.sContext, vCaptions);
		q.vImages = job.vImages;
		sLocal = CBhMsiPeakAnalyzer::formatReport(job.seg);
	}
	else {
		q.sUser = CBhLlamaProtocol::buildCoordinatePrompt(job.vPoints, job.sContext);
		sLocal = "Points sent:\n" + CBhMsiPeakAnalyzer::formatPoints(job.vPoints);
	}

	CMetaAiClient client;
	client.SetReasoningEffort(job.sEffort);
	SBhLlamaReply reply;
	std::string sNote;
	const bool bOk = chatWithModelFallback(client, q, reply, sModel, sNote);
	std::string sOut = sLocal + (sNote.empty() ? std::string() : "\n" + sNote);
	if (bOk) {
		sOut += "\n--- Meta " + sModel + " ---\n";
		if (job.eTask == EBhMsiAiTask::CoordinateExplain && CBhLlamaProtocol::parseExplanations(reply.sText, r.vExplanations))
			sOut += CBhLlamaProtocol::formatExplanations(r.vExplanations);
		else
			sOut += reply.sText + "\n";
		sOut += "(AI output for research use only (e.g., brain signal decoding, BCI command, robotic control).)\n";
		//sOut += "(AI output for research use only (e.g, brain signals decoding, robotic instruction).)\n";
	}
	r.bOk = bOk;
	r.sText = toDisplay(sOut);
	if (!bOk) r.sError = client.m_lastError;
	return r;
}

SBhMsiAiResult CBhMsiAiAnalyst::runPanel(const SBhMsiAiJob& job)
{
	if (job.eTask == EBhMsiAiTask::ImageSegmentation) {
		return runImagePanels(job);
	}
	SBhMsiAiResult r;
	r.eTask = job.eTask;
	std::string sModel = modelFromEnvironment(job.sModel);
	SBhLlamaRequest q;
	q.nMaxTokens = job.nMaxTokens;
	q.sSystem = CBhLlamaProtocol::systemPrompt();
	std::string sLocal;
	if (job.eTask == EBhMsiAiTask::SegmentationReport) {
		std::vector<std::string> vCaptions;
		for (const SBhLlamaImage& im : job.vImages) vCaptions.push_back(im.sCaption);
		q.sUser = CBhLlamaProtocol::buildSegmentationPrompt(job.seg, job.sContext, vCaptions);
		q.vImages = job.vImages;
		sLocal = CBhMsiPeakAnalyzer::formatReport(job.seg);
	}
	else {
		q.sUser = CBhLlamaProtocol::buildCoordinatePrompt(job.vPoints, job.sContext);
		sLocal = "Points sent:\n" + CBhMsiPeakAnalyzer::formatPoints(job.vPoints);
	}

	CMetaAiClient client;
	client.SetReasoningEffort(job.sEffort);
	SBhLlamaReply reply;
	std::string sNote;
	const bool bOk = chatWithModelFallback(client, q, reply, sModel, sNote);
	std::string sOut = sLocal + (sNote.empty() ? std::string() : "\n" + sNote);
	if (bOk) {
		sOut += "\n--- Meta " + sModel + " ---\n";
		if (job.eTask == EBhMsiAiTask::CoordinateExplain && CBhLlamaProtocol::parseExplanations(reply.sText, r.vExplanations))
			sOut += CBhLlamaProtocol::formatExplanations(r.vExplanations);
		else
			sOut += reply.sText + "\n";
		sOut += "(AI output for research use only (e.g., brain signal decoding, BCI command, robotic control).)\n";
		//sOut += "(AI output for research use only (e.g, brain signals decoding, robotic instruction).)\n";
	}
	r.bOk = bOk;
	r.sText = toDisplay(sOut);
	if (!bOk) r.sError = client.m_lastError;
	return r;
}
