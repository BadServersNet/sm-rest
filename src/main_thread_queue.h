#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "text.h"

enum class RestStatus
{
	Ok = 0,
	HttpError,
	NetworkError,
	Timeout,
	IoError,
	TooLarge,
	Cancelled,
};

struct TransferEvent
{
	enum class Kind
	{
		Progress,
		Headers,
		Data,
		Complete,
	};

	Kind kind = Kind::Complete;
	int jobId = 0;
	long long downloaded = 0;
	long long downloadTotal = 0;
	long long uploaded = 0;
	long long uploadTotal = 0;
	RestStatus status = RestStatus::Ok;
	long httpStatus = 0;
	long long contentLength = -1;
	long long bodyLength = 0;
	long long elapsedMs = 0;
	std::string error;
	std::string url;
	rest::HeaderList headers;
	std::string body;
};

class MainThreadQueue
{
public:
	void Post(TransferEvent &&event);
	std::vector<TransferEvent> Drain();
	void SetEnabled(bool enabled);

	bool HasEvents() const { return m_hasEvents; }

private:
	std::mutex m_mutex;
	std::vector<TransferEvent> m_events;
	std::atomic<bool> m_hasEvents { false };
	std::atomic<bool> m_enabled { true };
};
