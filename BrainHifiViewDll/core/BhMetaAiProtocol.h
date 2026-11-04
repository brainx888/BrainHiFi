#pragma once
/*
 * CBhMetaAiProtocol - MFC-free request/response code for the Meta AI API:
 *   POST https://api.meta.ai/v1/responses   (OpenAI "Responses" format, Authorization: Bearer <key>)
 *   GET  https://api.meta.ai/v1/models
 * Models checked live: muse-wonder-1.0 and muse-spark-1.3 (text + images, reasoning effort),
 * sam-3.1 (text prompt + image -> numbered boxes + masks in the output text).
 * Transport lives in BhHttps / CMetaAiClient.
 */
#include "BhLlamaProtocol.h"
#include <string>
#include <vector>

struct SBhSam3Box
{
	int iIndex = -1;                   // instance number from SAM (0 = first)
	int x1 = 0, y1 = 0, x2 = 0, y2 = 0;   // pixel box, inclusive
	int nImgW = 0, nImgH = 0;          // image size SAM saw
	int nMaskW = 0, nMaskH = 0, nMaskX = 0, nMaskY = 0;
	std::string sMaskData;             // encoded mask; format is undocumented, kept as is
	int width() const { return x2 - x1 + 1; }
	int height() const { return y2 - y1 + 1; }
	double centreFracX() const { return nImgW > 0 ? (x1 + x2 + 1) * 0.5 / nImgW : 0.0; }
	double centreFracY() const { return nImgH > 0 ? (y1 + y2 + 1) * 0.5 / nImgH : 0.0; }
};

class BH_API CBhMetaAiProtocol
{
public:
	/// Responses body.  req.sSystem -> "instructions"; images follow the user text as input_image
	/// data URIs (each caption as input_text before it).  sEffort ("low"/"medium"/"high", "" = server
	/// default) and nMaxOutputTokens (<= 0 = omitted; reasoning tokens count too) are skipped for SAM.
	static std::string buildResponsesRequest(const SBhLlamaRequest& req, const std::string& sEffort, int nMaxOutputTokens);
	/// bOk = HTTP 2xx, no error object and status completed/incomplete.  sText joins every
	/// output_text of the message outputs; it may be empty (e.g. SAM found nothing).
	static SBhLlamaReply parseResponsesReply(int nHttpStatus, const std::string& sBody);
	/// Parses SAM 3.1 text such as "<0f>0<|box;x1=..;y1=..;x2=..;y2=..;w=..;h=..|><|mask;x=0;y=0;data=W,H,...|>,1<|box;...".
	static bool parseSam3(const std::string& sText, std::vector<SBhSam3Box>& v);
	static std::string formatSam3(const std::vector<SBhSam3Box>& v, const std::string& sPrompt);
	static bool isSamModel(const std::string& sModel);
};
