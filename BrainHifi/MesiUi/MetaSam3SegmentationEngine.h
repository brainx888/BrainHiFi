// MetaSam3SegmentationEngine.h - adapter that plugs the existing
// BrainHifiViewDll CMetaSAM3Client (remote SAM 3.x segmentation) into the
// MFC-free mesi::IAISegmentationEngine interface, so MESIManager can drive it
// on a worker thread with cancellation and progress reporting.
//
// The API key is never stored in source: it is read from the environment
// (MESI_SAM3_API_KEY, then FAL_KEY) or set explicitly by the host.
#pragma once

#include "../Mesi/MesiEngines.h"

#include <afxstr.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CMetaSAM3Client;
struct CSam3Instance;


class CMetaSam3SegmentationEngine final : public mesi::IAISegmentationEngine
{
public:
	struct Settings
	{
		CString host = _T("api.fal.ai");
		int     port = 443;
		bool    https = true;
		CString apiKey;                      // empty -> ApiKeyFromEnvironment()
		DWORD   resolveTimeoutMs = 5000;
		DWORD   connectTimeoutMs = 10000;
		DWORD   sendTimeoutMs = 120000;
		DWORD   receiveTimeoutMs = 300000;
	};

	// Chooses which 2-D image of a subject is sent to the model.  The default
	// takes the first image-typed path among coregisteredMriPath / t1MriPath
	// (rendered source maps exported by the MSI viewer).
	using ImageResolver = std::function<std::string(const mesi::SubjectRecord&)>;
	// Converts one client instance into the core representation.  The default
	// fills label / className only; supply a lambda that copies the score and
	// box members of your CSam3Instance to get confidence filtering.
	using InstanceMapper = std::function<mesi::SegmentedInstance(const CSam3Instance&, int index)>;

	explicit CMetaSam3SegmentationEngine(Settings settings = {}, ImageResolver resolver = {}, InstanceMapper mapper = {});
	~CMetaSam3SegmentationEngine() override;

	std::string name() const override { return "Meta SAM 3 (remote)"; }
	bool supports(mesi::RuntimeType runtime) const override { return runtime == mesi::RuntimeType::Remote; }
	mesi::SegmentationOutput run(const mesi::SegmentationInput& in, const mesi::CancellationToken& cancel,
		const mesi::ProgressFn& progress) override;

	// One-shot helper shared with the batch "segment image files" button.
	// Returns false and fills `error` on failure.
	static bool SegmentImage(const Settings& settings, const CString& imagePath, const CString& prompt,
		std::vector<CSam3Instance>& instances, int& width, int& height, CString& error);

	static CString ApiKeyFromEnvironment();
	static bool HasImageExtension(const std::string& utf8Path);
	static std::string DefaultImageResolver(const mesi::SubjectRecord& subject);

private:
	Settings m_settings;
	ImageResolver m_resolver;
	InstanceMapper m_mapper;
};
