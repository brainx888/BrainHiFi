#include "MesiJobRunner.h"

#include <algorithm>

namespace mesi {

MesiJobRunner::MesiJobRunner(std::size_t workerThreads)
{
	workerThreads = (std::max)(static_cast<std::size_t>(1), workerThreads);
	m_workers.reserve(workerThreads);
	for (std::size_t i = 0; i < workerThreads; ++i)
		m_workers.emplace_back([this] { workerLoop(); });
}

MesiJobRunner::~MesiJobRunner()
{
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
		for (auto& j : m_queue) j->token.cancel();
		for (auto& [id, j] : m_jobs) j->token.cancel();
		m_queue.clear();
	}
	m_wake.notify_all();
	for (auto& t : m_workers)
		if (t.joinable()) t.join();
}

void MesiJobRunner::setObserver(JobObserver observer)
{
	std::lock_guard lock(m_observerMutex);
	m_observer = std::move(observer);
}

void MesiJobRunner::publish(const JobInfo& info)
{
	JobObserver obs;
	{
		std::lock_guard lock(m_observerMutex);
		obs = m_observer;
	}
	if (obs) obs(info);
}

JobId MesiJobRunner::submit(std::string kind, std::string subjectId, JobBody body)
{
	auto job = std::make_shared<Job>();
	job->body = std::move(body);
	job->info.kind = std::move(kind);
	job->info.subjectId = std::move(subjectId);
	JobInfo snapshot;
	{
		std::lock_guard lock(m_mutex);
		if (m_stopping) return kInvalidJobId;
		job->info.id = m_nextId++;
		m_jobs.emplace(job->info.id, job);
		m_queue.push_back(job);
		forgetFinishedLocked();
		snapshot = job->info;
	}
	publish(snapshot);
	m_wake.notify_one();
	return snapshot.id;
}

bool MesiJobRunner::cancel(JobId id)
{
	std::shared_ptr<Job> job;
	bool wasQueued = false;
	{
		std::lock_guard lock(m_mutex);
		auto it = m_jobs.find(id);
		if (it == m_jobs.end()) return false;
		job = it->second;
		if (job->info.status != JobStatus::Queued && job->info.status != JobStatus::Running) return false;
		job->token.cancel();
		auto q = std::find(m_queue.begin(), m_queue.end(), job);
		if (q != m_queue.end()) {
			m_queue.erase(q);
			job->info.status = JobStatus::Cancelled;
			job->info.stage = "Cancelled before start";
			wasQueued = true;
		}
	}
	if (wasQueued) {
		publish(job->info);
		m_idle.notify_all();
	}
	return true;   // running jobs finish on their own once they observe the token
}

void MesiJobRunner::cancelAll()
{
	std::vector<JobId> ids;
	{
		std::lock_guard lock(m_mutex);
		for (auto& [id, j] : m_jobs)
			if (j->info.status == JobStatus::Queued || j->info.status == JobStatus::Running) ids.push_back(id);
	}
	for (const JobId id : ids) cancel(id);
}

std::optional<JobInfo> MesiJobRunner::info(JobId id) const
{
	std::lock_guard lock(m_mutex);
	auto it = m_jobs.find(id);
	if (it == m_jobs.end()) return std::nullopt;
	return it->second->info;
}

std::vector<JobInfo> MesiJobRunner::activeJobs() const
{
	std::lock_guard lock(m_mutex);
	std::vector<JobInfo> out;
	for (const auto& [id, j] : m_jobs)
		if (j->info.status == JobStatus::Queued || j->info.status == JobStatus::Running) out.push_back(j->info);
	return out;
}

std::size_t MesiJobRunner::activeCount() const
{
	std::lock_guard lock(m_mutex);
	return m_queue.size() + m_running;
}

bool MesiJobRunner::isSubjectBusy(std::string_view subjectId) const
{
	std::lock_guard lock(m_mutex);
	for (const auto& [id, j] : m_jobs)
		if (j->info.subjectId == subjectId && (j->info.status == JobStatus::Queued || j->info.status == JobStatus::Running))
			return true;
	return false;
}

void MesiJobRunner::waitIdle()
{
	std::unique_lock lock(m_mutex);
	m_idle.wait(lock, [this] { return m_queue.empty() && m_running == 0; });
}

bool MesiJobRunner::waitIdleFor(std::chrono::milliseconds timeout)
{
	std::unique_lock lock(m_mutex);
	return m_idle.wait_for(lock, timeout, [this] { return m_queue.empty() && m_running == 0; });
}

void MesiJobRunner::forgetFinishedLocked()
{
	// Keep a bounded history of finished jobs so info() works briefly after
	// completion without growing forever.
	std::size_t finished = 0;
	for (const auto& [id, j] : m_jobs)
		if (j->info.status != JobStatus::Queued && j->info.status != JobStatus::Running) ++finished;
	for (auto it = m_jobs.begin(); finished > kMaxFinishedHistory && it != m_jobs.end();) {
		if (it->second->info.status != JobStatus::Queued && it->second->info.status != JobStatus::Running) {
			it = m_jobs.erase(it);
			--finished;
		} else ++it;
	}
}

void MesiJobRunner::workerLoop()
{
	for (;;) {
		std::shared_ptr<Job> job;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
			if (m_stopping) return;
			job = std::move(m_queue.front());
			m_queue.pop_front();
			++m_running;
			job->info.status = JobStatus::Running;
			job->info.stage = "Starting";
		}
		publish(job->info);

		const ProgressFn progress = [this, job](int percent, const std::string& stage) {
			JobInfo snapshot;
			{
				std::lock_guard lock(m_mutex);
				job->info.percent = (std::max)(0, (std::min)(100, percent));
				job->info.stage = stage;
				snapshot = job->info;
			}
			publish(snapshot);
		};

		JobStatus finalStatus = JobStatus::Completed;
		std::string error;
		try {
			if (job->token.isCancelled()) throw CancellationToken::OperationCancelled();
			job->body(job->token, progress);
			if (job->token.isCancelled()) finalStatus = JobStatus::Cancelled;
		} catch (const CancellationToken::OperationCancelled&) {
			finalStatus = JobStatus::Cancelled;
		} catch (const std::exception& e) {
			finalStatus = JobStatus::Failed;
			error = e.what();
		} catch (...) {
			finalStatus = JobStatus::Failed;
			error = "unknown exception";
		}

		JobInfo snapshot;
		{
			std::lock_guard lock(m_mutex);
			job->info.status = finalStatus;
			job->info.error = error;
			if (finalStatus == JobStatus::Completed) job->info.percent = 100;
			--m_running;
			snapshot = job->info;
		}
		publish(snapshot);
		m_idle.notify_all();
	}
}

} // namespace mesi
