#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "job.h"

class TransferThread
{
public:
	bool Start();
	void Stop();
	void Enqueue(std::unique_ptr<RestJob> job);
	void Cancel(int jobId);
	void Consume(int jobId, long long bytes);

private:
	using ConsumeRequest = std::pair<int, long long>;

	void Run();
	void TakePending(
		std::vector<std::unique_ptr<RestJob>> &incoming,
		std::vector<int> &cancelRequests,
		std::vector<ConsumeRequest> &consumeRequests
	);
	void AdmitIncoming(std::vector<std::unique_ptr<RestJob>> &incoming);
	void ApplyCancellations(const std::vector<int> &cancelRequests);
	void ApplyConsumed(const std::vector<ConsumeRequest> &consumeRequests);
	void ReapCompleted();
	void StartDueRetries();
	long ComputeWaitMs() const;
	void RemoveFinished();
	RestJob *FindActive(int jobId);

	std::thread m_thread;
	CURLM *m_multi = nullptr;
	std::atomic<bool> m_running { false };
	std::mutex m_mutex;
	std::vector<std::unique_ptr<RestJob>> m_incoming;
	std::vector<int> m_cancelRequests;
	std::vector<ConsumeRequest> m_consumeRequests;
	std::vector<std::unique_ptr<RestJob>> m_active;
};
