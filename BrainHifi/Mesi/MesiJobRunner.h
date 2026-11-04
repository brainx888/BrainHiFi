// MesiJobRunner.h - fixed-size worker pool for background inference / fusion
// jobs with per-job cancellation, progress and status queries.
//
// Callbacks (progress, completion) run on the worker thread.  UI code must
// marshal them to the GUI thread (CMesiWorkspaceDlg does so via PostMessage).
#pragma once

#include "MesiEngines.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mesi {

using JobId = std::uint64_t;
constexpr JobId kInvalidJobId = 0;

enum class JobStatus : std::uint8_t { Queued, Running, Completed, Failed, Cancelled };

struct JobInfo
{
	JobId id = kInvalidJobId;
	std::string kind;          // "segmentation", "fusion", ...
	std::string subjectId;
	JobStatus status = JobStatus::Queued;
	int percent = 0;
	std::string stage;
	std::string error;
};

class MesiJobRunner
{
public:
	// The job body receives the token and a progress sink; throwing marks the
	// job Failed (or Cancelled for CancellationToken::OperationCancelled).
	using JobBody = std::function<void(const CancellationToken&, const ProgressFn&)>;
	using JobObserver = std::function<void(const JobInfo&)>;

	explicit MesiJobRunner(std::size_t workerThreads = 1);
	~MesiJobRunner();                  // cancels pending jobs and joins workers
	MesiJobRunner(const MesiJobRunner&) = delete;
	MesiJobRunner& operator=(const MesiJobRunner&) = delete;

	JobId submit(std::string kind, std::string subjectId, JobBody body);
	bool cancel(JobId id);             // true if the job existed and was not finished
	void cancelAll();
	std::optional<JobInfo> info(JobId id) const;
	std::vector<JobInfo> activeJobs() const;   // queued + running
	std::size_t activeCount() const;
	bool isSubjectBusy(std::string_view subjectId) const;
	void waitIdle();                   // blocks until no job is queued or running
	bool waitIdleFor(std::chrono::milliseconds timeout);
	// Called on every status / progress change (worker thread!).
	void setObserver(JobObserver observer);
	std::size_t workerCount() const noexcept { return m_workers.size(); }

private:
	struct Job
	{
		JobInfo info;
		JobBody body;
		CancellationToken token;
	};

	void workerLoop();
	void publish(const JobInfo& info);
	void forgetFinishedLocked();

	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::condition_variable m_idle;
	std::deque<std::shared_ptr<Job>> m_queue;
	std::map<JobId, std::shared_ptr<Job>> m_jobs;    // all known jobs (finished ones pruned)
	std::vector<std::thread> m_workers;
	JobId m_nextId = 1;
	std::size_t m_running = 0;
	bool m_stopping = false;

	mutable std::mutex m_observerMutex;
	JobObserver m_observer;
	static constexpr std::size_t kMaxFinishedHistory = 64;
};

} // namespace mesi
