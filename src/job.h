#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

#include <curl/curl.h>

#include "main_thread_queue.h"
#include "request.h"

class RestJob
{
public:
	RestJob(RequestSpec spec, MainThreadQueue *queue);
	~RestJob();

	int Id() const { return m_spec.id; }

	bool IsFinished() const { return m_finished; }

	bool IsWaitingForRetry() const { return m_waitingForRetry; }

	bool IsRetryDue(std::chrono::steady_clock::time_point now) const;

	std::chrono::steady_clock::time_point RetryAt() const { return m_retryAt; }

	bool BeginAttempt(CURLM *multi);
	void OnAttemptDone(CURLM *multi, CURLcode code);
	void OnStreamConsumed(long long bytes);
	void Cancel(CURLM *multi);

private:
	enum class Sink
	{
		Memory,
		File,
		Stream,
	};

	bool OpenFiles();
	bool OpenUploadFile();
	bool OpenOutputFile();
	void CloseFiles();
	void BuildHeaders();
	void ApplyCommonOptions();
	void ApplyAuth();
	void ApplyMethod();
	void ApplyBody();
	void ApplyMultipart();
	void ResetAttemptState();
	void ReleaseCurl(CURLM *multi);
	Sink SinkFor(long status) const;
	size_t WriteMemory(const char *data, size_t total);
	size_t WriteFile(const char *data, size_t total);
	size_t WriteStream(const char *data, size_t total);
	void OnHeaderLine(const std::string &line);
	void OnHeadersComplete();
	void PostProgress(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
	void CaptureAttemptInfo();
	bool CanRetry() const;
	bool ShouldRetryStatus(long httpStatus) const;
	void ScheduleRetry();
	void HandleTransportFailure(CURLcode code, const std::string &curlError);
	void HandleHttpFailure();
	void HandleHttpSuccess();
	bool FinalizeDownload(std::string &error);
	void Finish(RestStatus status, const std::string &error);

	static size_t WriteCallback(char *data, size_t size, size_t count, void *userdata);
	static size_t ReadCallback(char *buffer, size_t size, size_t count, void *userdata);
	static int SeekCallback(void *userdata, curl_off_t offset, int origin);
	static size_t HeaderCallback(char *data, size_t size, size_t count, void *userdata);
	static int
	ProgressCallback(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

	MainThreadQueue *m_queue;
	CURL *m_easy = nullptr;
	curl_slist *m_headerList = nullptr;
	curl_mime *m_mime = nullptr;
	FILE *m_uploadFile = nullptr;
	FILE *m_outputFile = nullptr;
	curl_off_t m_resumeOffset = 0;
	curl_off_t m_uploadSize = 0;
	long long m_bodyLength = 0;
	long long m_pendingStreamBytes = 0;
	long m_httpStatus = 0;
	long long m_elapsedMs = 0;
	std::chrono::steady_clock::time_point m_retryAt;
	std::chrono::steady_clock::time_point m_lastProgressPost;
	rest::HeaderList m_responseHeaders;
	std::string m_partPath;
	std::string m_responseBody;
	std::string m_effectiveUrl;
	RequestSpec m_spec;
	int m_attempt = 0;
	bool m_attached = false;
	bool m_restartedFullBody = false;
	bool m_headersPosted = false;
	bool m_paused = false;
	bool m_tooLarge = false;
	bool m_waitingForRetry = false;
	bool m_finished = false;
	std::atomic<bool> m_cancelRequested { false };
	char m_errorBuffer[CURL_ERROR_SIZE];
};
