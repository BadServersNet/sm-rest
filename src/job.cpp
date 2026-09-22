#include "job.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>

#include <unistd.h>

#include "text.h"

static const size_t kMaxErrorBody = size_t { 64 } * 1024;
static const long long kStreamHighWater = 4LL * 1024 * 1024;
static const long long kStreamLowWater = 1LL * 1024 * 1024;
static const long kBackoffBaseMs = 1000;
static const long kBackoffCapMs = 30000;
static const long kRetryAfterCapMs = 60000;

static const char *MethodName(RestMethod method)
{
	switch (method)
	{
		case RestMethod::Get:
			return "GET";
		case RestMethod::Head:
			return "HEAD";
		case RestMethod::Post:
			return "POST";
		case RestMethod::Put:
			return "PUT";
		case RestMethod::Patch:
			return "PATCH";
		case RestMethod::Delete:
			return "DELETE";
		case RestMethod::Options:
			return "OPTIONS";
	}

	return "GET";
}

static bool IsSuccess(long httpStatus)
{
	return httpStatus >= 200 && httpStatus < 300;
}

static bool IsRedirect(long httpStatus)
{
	return httpStatus == 301 || httpStatus == 302 || httpStatus == 303 || httpStatus == 307 || httpStatus == 308;
}

static long long FileSizeOf(const std::string &path)
{
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);

	if (error)
	{
		return -1;
	}

	return static_cast<long long>(size);
}

static void EnsureParentDirectory(const std::string &path)
{
	const std::filesystem::path parent = std::filesystem::path(path).parent_path();
	std::error_code error;
	std::filesystem::create_directories(parent, error);
}

static long ParseRetryAfterMs(const rest::HeaderList &headers)
{
	const std::string *value = rest::FindHeader(headers, "retry-after");

	if (value == nullptr || value->empty())
	{
		return -1;
	}

	char *end = nullptr;
	const long seconds = strtol(value->c_str(), &end, 10);

	if (end == value->c_str() || seconds < 0)
	{
		return -1;
	}

	return std::min(kRetryAfterCapMs, seconds * 1000);
}

RestJob::RestJob(RequestSpec spec, MainThreadQueue *queue) : m_queue(queue), m_spec(std::move(spec))
{
	m_errorBuffer[0] = '\0';
}

RestJob::~RestJob()
{
	CloseFiles();

	if (m_headerList != nullptr)
	{
		curl_slist_free_all(m_headerList);
	}

	if (m_mime != nullptr)
	{
		curl_mime_free(m_mime);
	}

	if (m_easy != nullptr)
	{
		curl_easy_cleanup(m_easy);
	}
}

bool RestJob::IsRetryDue(std::chrono::steady_clock::time_point now) const
{
	return m_waitingForRetry && now >= m_retryAt;
}

void RestJob::ResetAttemptState()
{
	m_responseBody.clear();
	m_responseHeaders.clear();
	m_headersPosted = false;
	m_bodyLength = 0;
	m_pendingStreamBytes = 0;
	m_paused = false;
	m_tooLarge = false;
	m_httpStatus = 0;
	m_errorBuffer[0] = '\0';
	m_restartedFullBody = false;
	m_lastProgressPost = std::chrono::steady_clock::time_point();
	m_waitingForRetry = false;
}

bool RestJob::BeginAttempt(CURLM *multi)
{
	m_attempt++;
	ResetAttemptState();

	if (m_cancelRequested)
	{
		Finish(RestStatus::Cancelled, "cancelled");

		return false;
	}

	m_easy = curl_easy_init();

	if (m_easy == nullptr)
	{
		Finish(RestStatus::NetworkError, "curl_easy_init failed");

		return false;
	}

	if (!OpenFiles())
	{
		const std::string error = std::string(strerror(errno));
		Finish(RestStatus::IoError, error);

		return false;
	}

	BuildHeaders();
	ApplyCommonOptions();
	ApplyAuth();
	ApplyMethod();
	ApplyBody();
	curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_headerList);

	const CURLMcode added = curl_multi_add_handle(multi, m_easy);

	if (added != CURLM_OK)
	{
		Finish(RestStatus::NetworkError, curl_multi_strerror(added));

		return false;
	}

	m_attached = true;

	return true;
}

bool RestJob::OpenFiles()
{
	if (!OpenUploadFile())
	{
		return false;
	}

	return OpenOutputFile();
}

bool RestJob::OpenUploadFile()
{
	if (m_spec.bodyKind != BodyKind::File)
	{
		return true;
	}

	m_uploadFile = fopen(m_spec.bodyFilePath.c_str(), "rb");

	if (m_uploadFile == nullptr)
	{
		return false;
	}

	const long long size = FileSizeOf(m_spec.bodyFilePath);
	m_uploadSize = size < 0 ? 0 : static_cast<curl_off_t>(size);

	return true;
}

bool RestJob::OpenOutputFile()
{
	if (m_spec.outputPath.empty())
	{
		return true;
	}

	m_partPath = m_spec.outputPath + ".part";
	EnsureParentDirectory(m_partPath);
	const long long existing = m_spec.resume ? FileSizeOf(m_partPath) : -1;

	if (existing > 0)
	{
		m_outputFile = fopen(m_partPath.c_str(), "ab");
		m_resumeOffset = static_cast<curl_off_t>(existing);
	}
	else
	{
		m_outputFile = fopen(m_partPath.c_str(), "wb");
		m_resumeOffset = 0;
	}

	return m_outputFile != nullptr;
}

void RestJob::CloseFiles()
{
	if (m_uploadFile != nullptr)
	{
		fclose(m_uploadFile);
		m_uploadFile = nullptr;
	}

	if (m_outputFile != nullptr)
	{
		fclose(m_outputFile);
		m_outputFile = nullptr;
	}
}

void RestJob::BuildHeaders()
{
	if (m_headerList != nullptr)
	{
		curl_slist_free_all(m_headerList);
		m_headerList = nullptr;
	}

	for (const auto &entry : m_spec.config.headers)
	{
		const std::string line = entry.second.empty() ? entry.first + ";" : entry.first + ": " + entry.second;
		m_headerList = curl_slist_append(m_headerList, line.c_str());
	}

	const bool hasContentType =
		m_spec.bodyKind == BodyKind::Memory || m_spec.bodyKind == BodyKind::File || m_spec.bodyKind == BodyKind::Form;

	if (hasContentType && !m_spec.bodyContentType.empty())
	{
		const std::string line = "Content-Type: " + m_spec.bodyContentType;
		m_headerList = curl_slist_append(m_headerList, line.c_str());
	}

	if (m_resumeOffset > 0)
	{
		const std::string range = "Range: bytes=" + std::to_string(static_cast<long long>(m_resumeOffset)) + "-";
		m_headerList = curl_slist_append(m_headerList, range.c_str());
	}

	m_headerList = curl_slist_append(m_headerList, "Expect:");
}

void RestJob::ApplyCommonOptions()
{
	const ClientConfig &config = m_spec.config;
	curl_easy_setopt(m_easy, CURLOPT_URL, m_spec.url.c_str());
	curl_easy_setopt(m_easy, CURLOPT_PRIVATE, this);
	curl_easy_setopt(m_easy, CURLOPT_ERRORBUFFER, m_errorBuffer);
	curl_easy_setopt(m_easy, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(m_easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(m_easy, CURLOPT_FOLLOWLOCATION, config.followRedirects ? 1L : 0L);
	curl_easy_setopt(m_easy, CURLOPT_MAXREDIRS, static_cast<long>(config.maxRedirects));
	curl_easy_setopt(m_easy, CURLOPT_SSL_VERIFYPEER, config.verifyCertificate ? 1L : 0L);
	curl_easy_setopt(m_easy, CURLOPT_SSL_VERIFYHOST, config.verifyCertificate ? 2L : 0L);
	curl_easy_setopt(m_easy, CURLOPT_CONNECTTIMEOUT, static_cast<long>(config.connectTimeout));
	curl_easy_setopt(m_easy, CURLOPT_TIMEOUT, static_cast<long>(config.absoluteTimeout));
	curl_easy_setopt(m_easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(m_easy, CURLOPT_LOW_SPEED_TIME, static_cast<long>(config.timeout));
	curl_easy_setopt(m_easy, CURLOPT_MAX_SEND_SPEED_LARGE, static_cast<curl_off_t>(config.maxSendSpeed));
	curl_easy_setopt(m_easy, CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(config.maxRecvSpeed));
	curl_easy_setopt(m_easy, CURLOPT_ACCEPT_ENCODING, "");
	const long chunkSize =
		std::max(1024L, std::min(static_cast<long>(CURL_MAX_READ_SIZE), static_cast<long>(config.chunkSize)));
	curl_easy_setopt(m_easy, CURLOPT_BUFFERSIZE, chunkSize);
	curl_easy_setopt(m_easy, CURLOPT_HEADERFUNCTION, HeaderCallback);
	curl_easy_setopt(m_easy, CURLOPT_HEADERDATA, this);
	curl_easy_setopt(m_easy, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(m_easy, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(m_easy, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
	curl_easy_setopt(m_easy, CURLOPT_XFERINFODATA, this);
	curl_easy_setopt(m_easy, CURLOPT_NOPROGRESS, 0L);

	const char *userAgent = config.userAgent.empty() ? "sm-rest/" SM_REST_VERSION : config.userAgent.c_str();
	curl_easy_setopt(m_easy, CURLOPT_USERAGENT, userAgent);

	if (!m_spec.caBundle.empty())
	{
		curl_easy_setopt(m_easy, CURLOPT_CAINFO, m_spec.caBundle.c_str());
	}

	if (m_spec.verbose)
	{
		curl_easy_setopt(m_easy, CURLOPT_VERBOSE, 1L);
	}
}

void RestJob::ApplyAuth()
{
	const ClientConfig &config = m_spec.config;

	if (!config.bearerToken.empty())
	{
		curl_easy_setopt(m_easy, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
		curl_easy_setopt(m_easy, CURLOPT_XOAUTH2_BEARER, config.bearerToken.c_str());

		return;
	}

	if (config.basicUser.empty())
	{
		return;
	}

	curl_easy_setopt(m_easy, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
	curl_easy_setopt(m_easy, CURLOPT_USERNAME, config.basicUser.c_str());
	curl_easy_setopt(m_easy, CURLOPT_PASSWORD, config.basicPassword.c_str());
}

void RestJob::ApplyMethod()
{
	const RestMethod method = m_spec.method;

	if (m_spec.bodyKind == BodyKind::None && method == RestMethod::Get)
	{
		curl_easy_setopt(m_easy, CURLOPT_HTTPGET, 1L);

		return;
	}

	if (m_spec.bodyKind == BodyKind::None && method == RestMethod::Head)
	{
		curl_easy_setopt(m_easy, CURLOPT_NOBODY, 1L);

		return;
	}

	if (m_spec.bodyKind == BodyKind::None && method == RestMethod::Post)
	{
		curl_easy_setopt(m_easy, CURLOPT_POST, 1L);
		curl_easy_setopt(m_easy, CURLOPT_POSTFIELDSIZE, 0L);

		return;
	}

	const bool nativeMethod = (m_spec.bodyKind == BodyKind::File && method == RestMethod::Put)
							  || (m_spec.bodyKind != BodyKind::File && method == RestMethod::Post);

	if (!nativeMethod)
	{
		curl_easy_setopt(m_easy, CURLOPT_CUSTOMREQUEST, MethodName(method));
	}

	if (method == RestMethod::Head)
	{
		curl_easy_setopt(m_easy, CURLOPT_NOBODY, 1L);
	}
}

void RestJob::ApplyBody()
{
	switch (m_spec.bodyKind)
	{
		case BodyKind::None:
		{
			break;
		}

		case BodyKind::Memory:
		case BodyKind::Form:
		{
			curl_easy_setopt(m_easy, CURLOPT_POSTFIELDS, m_spec.body.data());
			curl_easy_setopt(m_easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(m_spec.body.size()));
			break;
		}

		case BodyKind::File:
		{
			curl_easy_setopt(m_easy, CURLOPT_UPLOAD, 1L);
			curl_easy_setopt(m_easy, CURLOPT_READFUNCTION, ReadCallback);
			curl_easy_setopt(m_easy, CURLOPT_READDATA, this);
			curl_easy_setopt(m_easy, CURLOPT_SEEKFUNCTION, SeekCallback);
			curl_easy_setopt(m_easy, CURLOPT_SEEKDATA, this);
			curl_easy_setopt(m_easy, CURLOPT_INFILESIZE_LARGE, m_uploadSize);
			break;
		}

		case BodyKind::Multipart:
		{
			ApplyMultipart();
			break;
		}
	}
}

void RestJob::ApplyMultipart()
{
	if (m_mime != nullptr)
	{
		curl_mime_free(m_mime);
	}

	m_mime = curl_mime_init(m_easy);

	for (const FormField &field : m_spec.formFields)
	{
		curl_mimepart *part = curl_mime_addpart(m_mime);
		curl_mime_name(part, field.name.c_str());

		if (field.filePath.empty())
		{
			curl_mime_data(part, field.value.data(), field.value.size());
			continue;
		}

		curl_mime_filedata(part, field.filePath.c_str());

		if (!field.fileName.empty())
		{
			curl_mime_filename(part, field.fileName.c_str());
		}

		if (!field.contentType.empty())
		{
			curl_mime_type(part, field.contentType.c_str());
		}
	}

	curl_easy_setopt(m_easy, CURLOPT_MIMEPOST, m_mime);
}

size_t RestJob::HeaderCallback(char *data, size_t size, size_t count, void *userdata)
{
	RestJob *job = static_cast<RestJob *>(userdata);
	const size_t total = size * count;
	job->OnHeaderLine(std::string(data, total));

	return total;
}

void RestJob::OnHeaderLine(const std::string &line)
{
	if (line.compare(0, 5, "HTTP/") == 0)
	{
		m_responseHeaders.clear();
		m_headersPosted = false;

		return;
	}

	const std::string trimmed = rest::Trim(line);

	if (trimmed.empty())
	{
		OnHeadersComplete();

		return;
	}

	const size_t colon = trimmed.find(':');

	if (colon == std::string::npos)
	{
		return;
	}

	const std::string name = rest::Trim(trimmed.substr(0, colon));
	const std::string value = rest::Trim(trimmed.substr(colon + 1));
	m_responseHeaders.emplace_back(name, value);
}

void RestJob::OnHeadersComplete()
{
	if (!m_spec.wantHeaders || m_headersPosted)
	{
		return;
	}

	long status = 0;
	curl_easy_getinfo(m_easy, CURLINFO_RESPONSE_CODE, &status);

	if (status < 200)
	{
		return;
	}

	const bool willFollow =
		IsRedirect(status) && m_spec.config.followRedirects && rest::FindHeader(m_responseHeaders, "location");

	if (willFollow)
	{
		return;
	}

	m_headersPosted = true;
	TransferEvent event;
	event.kind = TransferEvent::Kind::Headers;
	event.jobId = m_spec.id;
	event.httpStatus = status;
	event.headers = m_responseHeaders;
	m_queue->Post(std::move(event));
}

RestJob::Sink RestJob::SinkFor(long status) const
{
	if (!IsSuccess(status))
	{
		return Sink::Memory;
	}

	if (!m_spec.outputPath.empty())
	{
		return Sink::File;
	}

	if (m_spec.streamData)
	{
		return Sink::Stream;
	}

	return Sink::Memory;
}

size_t RestJob::WriteCallback(char *data, size_t size, size_t count, void *userdata)
{
	RestJob *job = static_cast<RestJob *>(userdata);
	const size_t total = size * count;
	long status = 0;
	curl_easy_getinfo(job->m_easy, CURLINFO_RESPONSE_CODE, &status);

	switch (job->SinkFor(status))
	{
		case Sink::File:
			return job->WriteFile(data, total);
		case Sink::Stream:
			return job->WriteStream(data, total);
		case Sink::Memory:
			return job->WriteMemory(data, total);
	}

	return total;
}

size_t RestJob::WriteMemory(const char *data, size_t total)
{
	const bool primary = m_spec.outputPath.empty() && !m_spec.streamData;
	const size_t cap = primary ? static_cast<size_t>(m_spec.config.maxBodySize) : kMaxErrorBody;
	const size_t room = cap > m_responseBody.size() ? cap - m_responseBody.size() : 0;

	if (total > room && primary)
	{
		m_tooLarge = true;

		return 0;
	}

	m_responseBody.append(data, std::min(total, room));
	m_bodyLength += static_cast<long long>(total);

	return total;
}

size_t RestJob::WriteFile(const char *data, size_t total)
{
	long status = 0;
	curl_easy_getinfo(m_easy, CURLINFO_RESPONSE_CODE, &status);
	const bool serverIgnoredRange = status == 200 && m_resumeOffset > 0 && !m_restartedFullBody;

	if (serverIgnoredRange)
	{
		m_restartedFullBody = true;
		m_resumeOffset = 0;
		FILE *reopened = freopen(m_partPath.c_str(), "wb", m_outputFile);

		if (reopened == nullptr)
		{
			m_outputFile = nullptr;

			return 0;
		}

		m_outputFile = reopened;
	}

	const size_t written = fwrite(data, 1, total, m_outputFile);
	m_bodyLength += static_cast<long long>(written);

	return written;
}

size_t RestJob::WriteStream(const char *data, size_t total)
{
	if (m_pendingStreamBytes >= kStreamHighWater)
	{
		m_paused = true;

		return CURL_WRITEFUNC_PAUSE;
	}

	m_pendingStreamBytes += static_cast<long long>(total);
	m_bodyLength += static_cast<long long>(total);

	TransferEvent event;
	event.kind = TransferEvent::Kind::Data;
	event.jobId = m_spec.id;
	event.body.assign(data, total);
	m_queue->Post(std::move(event));

	return total;
}

void RestJob::OnStreamConsumed(long long bytes)
{
	m_pendingStreamBytes = std::max(0LL, m_pendingStreamBytes - bytes);

	if (!m_paused || m_pendingStreamBytes >= kStreamLowWater || !m_attached)
	{
		return;
	}

	m_paused = false;
	curl_easy_pause(m_easy, CURLPAUSE_CONT);
}

size_t RestJob::ReadCallback(char *buffer, size_t size, size_t count, void *userdata)
{
	RestJob *job = static_cast<RestJob *>(userdata);

	if (job->m_uploadFile == nullptr)
	{
		return CURL_READFUNC_ABORT;
	}

	return fread(buffer, 1, size * count, job->m_uploadFile);
}

int RestJob::SeekCallback(void *userdata, curl_off_t offset, int origin)
{
	RestJob *job = static_cast<RestJob *>(userdata);

	if (job->m_uploadFile == nullptr)
	{
		return CURL_SEEKFUNC_CANTSEEK;
	}

	const int result = fseeko(job->m_uploadFile, static_cast<off_t>(offset), origin);

	return result == 0 ? CURL_SEEKFUNC_OK : CURL_SEEKFUNC_CANTSEEK;
}

int RestJob::ProgressCallback(
	void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow
)
{
	RestJob *job = static_cast<RestJob *>(userdata);

	if (job->m_cancelRequested)
	{
		return 1;
	}

	job->PostProgress(dltotal, dlnow, ultotal, ulnow);

	return 0;
}

void RestJob::PostProgress(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	if (dlnow <= 0 && ulnow <= 0)
	{
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProgressPost).count();
	const bool downloadComplete = dltotal > 0 && dlnow >= dltotal;
	const bool uploadComplete = ultotal > 0 && ulnow >= ultotal;

	if (!downloadComplete && !uploadComplete && elapsed < 100)
	{
		return;
	}

	m_lastProgressPost = now;
	const long long offset = static_cast<long long>(m_resumeOffset);

	TransferEvent event;
	event.kind = TransferEvent::Kind::Progress;
	event.jobId = m_spec.id;
	event.downloaded = static_cast<long long>(dlnow) + offset;
	event.downloadTotal = dltotal > 0 ? static_cast<long long>(dltotal) + offset : 0;
	event.uploaded = static_cast<long long>(ulnow);
	event.uploadTotal = static_cast<long long>(ultotal);
	m_queue->Post(std::move(event));
}

void RestJob::CaptureAttemptInfo()
{
	curl_easy_getinfo(m_easy, CURLINFO_RESPONSE_CODE, &m_httpStatus);

	char *url = nullptr;
	curl_easy_getinfo(m_easy, CURLINFO_EFFECTIVE_URL, &url);
	m_effectiveUrl = url == nullptr ? "" : url;

	curl_off_t totalTime = 0;
	curl_easy_getinfo(m_easy, CURLINFO_TOTAL_TIME_T, &totalTime);
	m_elapsedMs = static_cast<long long>(totalTime / 1000);
}

void RestJob::ReleaseCurl(CURLM *multi)
{
	if (m_easy == nullptr)
	{
		return;
	}

	if (m_attached)
	{
		curl_multi_remove_handle(multi, m_easy);
		m_attached = false;
	}

	curl_easy_cleanup(m_easy);
	m_easy = nullptr;

	if (m_mime != nullptr)
	{
		curl_mime_free(m_mime);
		m_mime = nullptr;
	}
}

bool RestJob::CanRetry() const
{
	if (m_attempt > m_spec.config.maxRetries)
	{
		return false;
	}

	const bool streamedSomething = m_spec.streamData && m_bodyLength > 0;

	return !streamedSomething;
}

bool RestJob::ShouldRetryStatus(long httpStatus) const
{
	return httpStatus == 429 || httpStatus == 502 || httpStatus == 503 || httpStatus == 504;
}

void RestJob::ScheduleRetry()
{
	static thread_local std::mt19937 generator { std::random_device {}() };
	const long retryAfter = ParseRetryAfterMs(m_responseHeaders);
	const long exponent = std::min(m_attempt - 1, 10);
	const long base = std::min(kBackoffCapMs, kBackoffBaseMs * (1L << exponent));
	std::uniform_int_distribution<long> jitter(-base / 4, base / 4);
	const long backoff = base + jitter(generator);
	const long delay = retryAfter >= 0 ? retryAfter : backoff;
	m_retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
	m_waitingForRetry = true;
}

bool RestJob::FinalizeDownload(std::string &error)
{
	if (rename(m_partPath.c_str(), m_spec.outputPath.c_str()) != 0)
	{
		error = std::string("rename failed: ") + strerror(errno);

		return false;
	}

	return true;
}

void RestJob::HandleTransportFailure(CURLcode code, const std::string &curlError)
{
	if (code == CURLE_ABORTED_BY_CALLBACK)
	{
		Finish(RestStatus::Cancelled, "cancelled");

		return;
	}

	if (code == CURLE_WRITE_ERROR && m_tooLarge)
	{
		const std::string error = "response body exceeds " + std::to_string(m_spec.config.maxBodySize) + " bytes";
		Finish(RestStatus::TooLarge, error);

		return;
	}

	if (code == CURLE_WRITE_ERROR || code == CURLE_READ_ERROR)
	{
		Finish(RestStatus::IoError, curlError);

		return;
	}

	if (code == CURLE_SSL_CACERT_BADFILE || code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_TOO_MANY_REDIRECTS
		|| code == CURLE_URL_MALFORMAT || code == CURLE_UNSUPPORTED_PROTOCOL)
	{
		Finish(RestStatus::NetworkError, curlError);

		return;
	}

	if (CanRetry())
	{
		ScheduleRetry();

		return;
	}

	const bool timedOut = code == CURLE_OPERATION_TIMEDOUT;
	const RestStatus status = timedOut ? RestStatus::Timeout : RestStatus::NetworkError;
	Finish(status, curlError);
}

void RestJob::HandleHttpFailure()
{
	if (ShouldRetryStatus(m_httpStatus) && CanRetry())
	{
		ScheduleRetry();

		return;
	}

	const bool clientError = m_httpStatus >= 400 && m_httpStatus < 500;

	if (!m_partPath.empty() && clientError)
	{
		unlink(m_partPath.c_str());
	}

	Finish(RestStatus::HttpError, "HTTP " + std::to_string(m_httpStatus));
}

void RestJob::HandleHttpSuccess()
{
	if (m_spec.outputPath.empty())
	{
		Finish(RestStatus::Ok, "");

		return;
	}

	std::string error;
	const bool finalized = FinalizeDownload(error);

	if (!finalized)
	{
		Finish(RestStatus::IoError, error);

		return;
	}

	Finish(RestStatus::Ok, "");
}

void RestJob::OnAttemptDone(CURLM *multi, CURLcode code)
{
	CaptureAttemptInfo();
	const bool hasErrorText = m_errorBuffer[0] != '\0';
	const std::string curlError = hasErrorText ? m_errorBuffer : curl_easy_strerror(code);
	CloseFiles();
	ReleaseCurl(multi);

	if (m_cancelRequested)
	{
		Finish(RestStatus::Cancelled, "cancelled");

		return;
	}

	if (code != CURLE_OK)
	{
		HandleTransportFailure(code, curlError);

		return;
	}

	if (IsSuccess(m_httpStatus))
	{
		HandleHttpSuccess();

		return;
	}

	HandleHttpFailure();
}

void RestJob::Cancel(CURLM *multi)
{
	m_cancelRequested = true;
	CloseFiles();
	ReleaseCurl(multi);
	Finish(RestStatus::Cancelled, "cancelled");
}

void RestJob::Finish(RestStatus status, const std::string &error)
{
	if (m_finished)
	{
		return;
	}

	m_finished = true;
	m_waitingForRetry = false;

	TransferEvent event;
	event.kind = TransferEvent::Kind::Complete;
	event.jobId = m_spec.id;
	event.status = status;
	event.httpStatus = m_httpStatus;
	event.error = error;
	event.url = m_effectiveUrl;
	event.elapsedMs = m_elapsedMs;
	event.bodyLength = m_bodyLength;
	event.headers = m_responseHeaders;
	event.body = std::move(m_responseBody);

	const std::string *lengthHeader = rest::FindHeader(m_responseHeaders, "content-length");

	if (lengthHeader != nullptr)
	{
		event.contentLength = atoll(lengthHeader->c_str());
	}

	m_queue->Post(std::move(event));
}
