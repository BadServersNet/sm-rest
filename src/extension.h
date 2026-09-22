#pragma once

#include <string>
#include <unordered_map>

#include "smsdk_ext.h"

#include "main_thread_queue.h"
#include "request.h"
#include "transfer_thread.h"

struct ClientObject
{
	ClientConfig config;
	Handle_t handle = BAD_HANDLE;
};

struct RequestObject
{
	RequestSpec spec;
	Handle_t clientHandle = BAD_HANDLE;
	IChangeableForward *headers = nullptr;
	IChangeableForward *data = nullptr;
	IChangeableForward *progress = nullptr;
};

struct ResponseObject
{
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

struct JobRecord
{
	int id = 0;
	Handle_t clientHandle = BAD_HANDLE;
	IdentityToken_t *owner = nullptr;
	IChangeableForward *completed = nullptr;
	IChangeableForward *headers = nullptr;
	IChangeableForward *data = nullptr;
	IChangeableForward *progress = nullptr;
	cell_t context = 0;
	bool dispatching = false;
	bool dropped = false;
};

class RestExtension : public SDKExtension, public IHandleTypeDispatch, public IPluginsListener
{
public:
	bool SDK_OnLoad(char *error, size_t maxlength, bool late) override;
	void SDK_OnUnload() override;

	void OnHandleDestroy(HandleType_t type, void *object) override;
	void OnPluginUnloaded(IPlugin *plugin) override;

	int SubmitJob(RequestSpec spec, JobRecord record);
	bool CancelJob(int jobId, IdentityToken_t *requester);
	int CancelJobsForClient(Handle_t clientHandle, IdentityToken_t *requester);
	void HandleEvent(const TransferEvent &event);

private:
	using JobMap = std::unordered_map<int, JobRecord>;

	bool CreateHandleTypes(char *error, size_t maxlength);
	void DropJobsForClient(Handle_t clientHandle);
	JobMap::iterator DropJob(JobMap::iterator it);
	void HandleIntermediate(JobRecord &record, const TransferEvent &event);
	void ReleaseRecord(JobRecord &record);
	void DispatchProgress(const JobRecord &record, const TransferEvent &event);
	void DispatchHeaders(const JobRecord &record, const TransferEvent &event);
	void DispatchData(const JobRecord &record, const TransferEvent &event);
	void DispatchComplete(JobRecord &record, const TransferEvent &event);
	Handle_t CreateResponseHandle(const JobRecord &record, const TransferEvent &event);
	void FreeOwnedHandle(Handle_t handle, IdentityToken_t *owner);
	void DetectCaBundle();

	JobMap m_jobs;
	int m_nextJobId = 1;
	std::string m_caBundlePath;
	bool m_verbose = false;
};

extern RestExtension g_RestExtension;
extern MainThreadQueue g_MainQueue;
extern TransferThread g_TransferThread;
extern HandleType_t g_ClientType;
extern HandleType_t g_RequestType;
extern HandleType_t g_ResponseType;
extern const sp_nativeinfo_t g_Natives[];

static inline cell_t ClampToCell(long long value)
{
	if (value > 0x7FFFFFFFLL)
	{
		return 0x7FFFFFFF;
	}

	if (value < -0x80000000LL)
	{
		return static_cast<cell_t>(-0x80000000LL);
	}

	return static_cast<cell_t>(value);
}

static inline void ReleaseForward(IChangeableForward *&forward)
{
	if (forward == nullptr)
	{
		return;
	}

	forwards->ReleaseForward(forward);
	forward = nullptr;
}
