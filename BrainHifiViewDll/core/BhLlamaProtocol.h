#pragma once
/*
 * CBhLlamaProtocol - MFC-free request/response layer for the Meta Llama API
 * (https://api.llama.com, native "POST /v1/chat/completions").
 *
 *  - builds chat requests with text + optional PNG images (data URIs)
 *  - parses native ("completion_message") and OpenAI-compatible
 *    ("choices[0].message") responses, API error bodies and /v1/models
 *  - builds the two MSI prompts (segmentation report, coordinate meaning)
 *    and parses the per-point JSON the model is asked to return
 * Transport (WinHTTP) and key storage live in CMetaLlamaClient.
 */
#include "BhExport.h"
#include "BhMsiPeakAnalyzer.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

/// Minimal JSON value (objects, arrays, strings, numbers, bools, null).
struct BH_API SBhJson
{
	enum EType { Null, Bool, Number, String, Array, Object } eType = Null;
	bool b = false;
	double d = 0.0;
	std::string s;
	std::vector<SBhJson> a;
	std::vector<std::pair<std::string, SBhJson>> o;

	const SBhJson* find(const std::string& sKey) const;   // object member or null
	const SBhJson* at(size_t i) const;                    // array item or null
	std::string str(const std::string& sDefault = std::string()) const;
	double num(double dDefault = 0.0) const;

	static bool parse(const std::string& sText, SBhJson& out, std::string* pError = nullptr);
	static std::string escape(const std::string& sUtf8);  // with quotes
};

struct SBhLlamaImage
{
	std::string sMime = "image/png";
	std::string sBase64;          // no data: prefix
	std::string sCaption;         // sent as text right before the image
};

struct SBhLlamaRequest
{
	std::string sModel = "Llama-4-Maverick-17B-128E-Instruct-FP8";
	std::string sSystem;
	std::string sUser;
	std::vector<SBhLlamaImage> vImages;
	int nMaxTokens = 2048;
	double dTemperature = 0.2;
	bool bCompat = false;         // OpenAI-compatible body (max_tokens) for /compat/v1
};

struct SBhLlamaReply
{
	bool bOk = false;
	std::string sText;            // assistant text
	std::string sStopReason;
	std::string sError;           // API or parse error
	int nPromptTokens = -1, nCompletionTokens = -1;
};

/// AI interpretation of one SBhMsiPoint.
struct SBhMsiPointExplanation
{
	std::string sLabel, sHemisphere, sLobe, sRegion, sBrodmann, sNetwork, sFunction, sInterpretation, sConfidence;
};

class BH_API CBhLlamaProtocol
{
public:
	static std::string buildChatRequest(const SBhLlamaRequest& req);
	static SBhLlamaReply parseChatResponse(int nHttpStatus, const std::string& sBody);
	static bool parseModelList(const std::string& sBody, std::vector<std::string>& vIds, std::string* pError = nullptr);

	static std::string systemPrompt();
	/// Task 1: describe the segmented MSI (local cluster table + rendered views).
	static std::string buildSegmentationPrompt(const SBhMsiSegmentResult& seg, const std::string& sContext,
	                                           const std::vector<std::string>& vViewCaptions);
	/// Task 2: location and meaning of 3D points; asks for a JSON array reply.
	static std::string buildCoordinatePrompt(const std::vector<SBhMsiPoint>& vPoints, const std::string& sContext);
	static bool parseExplanations(const std::string& sReplyText, std::vector<SBhMsiPointExplanation>& v);
	static std::string formatExplanations(const std::vector<SBhMsiPointExplanation>& v);

	/// Prompt for one user-supplied image (e.g. an exported MSI slice) sent to a vision model.
	static std::string buildImageSegmentationPrompt(const std::string& sInstruction, const std::string& sContext,
	                                                const std::string& sImageCaption);
	/// "image/png", "image/jpeg", "image/gif", "image/bmp", "image/webp" from a file name; "" if unknown.
	static std::string imageMimeFromPath(const std::string& sPath);
	/// sPreferred if it is enabled (case-insensitive), else the first enabled Llama 4 (vision) model,
	/// else (text-only task) the first enabled model; "" if nothing fits.
	static std::string chooseModel(const std::vector<std::string>& vEnabled, const std::string& sPreferred, bool bNeedVision);
	/// True for "invalid model name" / "model not available for your application" style errors.
	static bool isModelError(const std::string& sError);

	static std::string base64(const unsigned char* p, size_t n);
	/// Removes anything that looks like a Llama key ("LLM|..." / "LLM_...") from text before it is shown or logged.
	static std::string redactKeys(const std::string& s);
};
