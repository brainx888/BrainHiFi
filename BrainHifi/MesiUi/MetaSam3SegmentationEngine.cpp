// MetaSam3SegmentationEngine.cpp
#include "..\pch.h"
#include "..\framework.h"
#include "MetaSam3SegmentationEngine.h"
#include "MesiStringUtil.h"

#include "..\..\BrainHifiViewDll\CMetaSAM3Client.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace mesi;
using namespace mesiui;

namespace {

CString ReadEnv(LPCTSTR name)
{
	const DWORD needed = ::GetEnvironmentVariable(name, nullptr, 0);
	if (needed == 0) return {};
	CString value;
	::GetEnvironmentVariable(name, value.GetBuffer(static_cast<int>(needed)), needed);
	value.ReleaseBuffer();
	return value.Trim();
}

SegmentedInstance DefaultMapper(const CSam3Instance&, int index)
{
	SegmentedInstance inst;
	inst.label = index + 1;
	inst.confidence = 1.0;           // the client already applied the model threshold
	inst.className = "sam3";
	return inst;
}

} // namespace

CMetaSam3SegmentationEngine::CMetaSam3SegmentationEngine(Settings settings, ImageResolver resolver, InstanceMapper mapper)
	: m_settings(std::move(settings))
	, m_resolver(resolver ? std::move(resolver) : ImageResolver(&DefaultImageResolver))
	, m_mapper(mapper ? std::move(mapper) : InstanceMapper(&DefaultMapper))
{
	if (m_settings.apiKey.IsEmpty())
		m_settings.apiKey = ApiKeyFromEnvironment();
}

CMetaSam3SegmentationEngine::~CMetaSam3SegmentationEngine() = default;

CString CMetaSam3SegmentationEngine::ApiKeyFromEnvironment()
{
	CString key = ReadEnv(_T("MESI_SAM3_API_KEY"));
	if (key.IsEmpty()) key = ReadEnv(_T("FAL_KEY"));
	return key;
}

bool CMetaSam3SegmentationEngine::HasImageExtension(const std::string& utf8Path)
{
	const std::size_t dot = utf8Path.find_last_of('.');
	if (dot == std::string::npos) return false;
	std::string ext = utf8Path.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" || ext == "bmp";
}

std::string CMetaSam3SegmentationEngine::DefaultImageResolver(const SubjectRecord& subject)
{
	for (const std::string* p : { &subject.modality.coregisteredMriPath, &subject.t1MriPath })
		if (HasImageExtension(*p)) return *p;
	return {};
}

bool CMetaSam3SegmentationEngine::SegmentImage(const Settings& settings, const CString& imagePath, const CString& prompt,
	std::vector<CSam3Instance>& instances, int& width, int& height, CString& error)
{
	instances.clear();
	width = height = 0;
	CString key = settings.apiKey.IsEmpty() ? ApiKeyFromEnvironment() : settings.apiKey;
	if (key.IsEmpty()) {
		error = _T("SAM 3 API key is not set (environment variable MESI_SAM3_API_KEY or FAL_KEY).");
		return false;
	}
	try {
		// One client per call: WinInet handles are cheap and this keeps the
		// adapter usable from several worker threads at once.
		auto client = std::make_unique<CMetaSAM3Client>(CStringW(settings.host), settings.port, settings.https);
		client->SetTimeouts(settings.resolveTimeoutMs, settings.connectTimeoutMs, settings.sendTimeoutMs, settings.receiveTimeoutMs);
		client->SetApiKey(key);
		if (!client->Segment(imagePath, prompt, instances, width, height)) {
			error = client->m_lastError;
			if (error.IsEmpty()) error = _T("SAM 3 request failed.");
			return false;
		}
		return true;
	}
	catch (const CException* e) {
		TCHAR msg[512] = {};
		const_cast<CException*>(e)->GetErrorMessage(msg, _countof(msg));
		const_cast<CException*>(e)->Delete();
		error = msg;
		return false;
	}
	catch (const std::exception& e) {
		error = CString(e.what());
		return false;
	}
}

SegmentationOutput CMetaSam3SegmentationEngine::run(const SegmentationInput& in, const CancellationToken& cancel, const ProgressFn& progress)
{
	const auto start = std::chrono::steady_clock::now();
	cancel.throwIfCancelled();

	const std::string imagePath = m_resolver(in.subject);
	if (imagePath.empty())
		throw std::runtime_error("No 2-D image (jpg/png/webp/bmp) to segment for subject '" + in.subject.subjectId +
			"' - point coregisteredMriPath or t1MriPath to a rendered source-map image.");
	if (progress) progress(5, "Uploading " + imagePath);

	std::vector<CSam3Instance> raw;
	int width = 0, height = 0;
	CString error;
	// The HTTP call itself is not interruptible; cancellation is honoured
	// before and after it, which bounds the latency by the client timeouts.
	if (!SegmentImage(m_settings, FromUtf8(imagePath), FromUtf8(in.config.promptText), raw, width, height, error))
		throw std::runtime_error(ToUtf8(error));
	cancel.throwIfCancelled();
	if (progress) progress(80, "Parsing " + std::to_string(raw.size()) + " instance(s)");

	SegmentationOutput out;
	out.imageWidth = width;
	out.imageHeight = height;
	out.imageDepth = 1;
	out.instances.reserve(raw.size());
	for (std::size_t i = 0; i < raw.size(); ++i) {
		SegmentedInstance inst = m_mapper(raw[i], static_cast<int>(i));
		if (inst.confidence + 1e-12 < in.config.confidenceThreshold) continue;
		if (static_cast<int>(out.instances.size()) >= in.config.maxInstances) break;
		out.instances.push_back(std::move(inst));
	}
	out.computeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	out.log = "SAM3 host=" + ToUtf8(m_settings.host) + " image=" + imagePath + " instances=" + std::to_string(out.instances.size());
	if (progress) progress(100, "Segmentation finished");
	return out;
}
