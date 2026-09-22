#include "extension.h"

#include <cstdlib>
#include <iterator>

#include <curl/curl.h>

#include "job.h"

RestExtension g_RestExtension;
MainThreadQueue g_MainQueue;
TransferThread g_TransferThread;
HandleType_t g_ClientType = 0;
HandleType_t g_RequestType = 0;
HandleType_t g_ResponseType = 0;

SMEXT_LINK(&g_RestExtension);

static const char kSystemCaBundle[] = "/etc/ssl/certs/ca-certificates.crt";

static void OnGameFrame(bool)
{
	if (!g_MainQueue.HasEvents())
	{
		return;
	}

	const std::vector<TransferEvent> events = g_MainQueue.Drain();

	for (const TransferEvent &event : events)
	{
		g_RestExtension.HandleEvent(event);
	}
}

bool RestExtension::SDK_OnLoad(char *error, size_t maxlength, bool)
{
	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
	{
		ke::SafeStrcpy(error, maxlength, "curl_global_init failed");

		return false;
	}

	if (!CreateHandleTypes(error, maxlength))
	{
		return false;
	}

	DetectCaBundle();
	const char *verbose = getenv("SM_REST_VERBOSE");
	m_verbose = verbose != nullptr && verbose[0] == '1';

	if (!g_TransferThread.Start())
	{
		ke::SafeStrcpy(error, maxlength, "Could not start the transfer thread");

		return false;
	}

	sharesys->AddNatives(myself, g_Natives);
	sharesys->RegisterLibrary(myself, "rest");
	plsys->AddPluginsListener(this);
	smutils->AddGameFrameHook(OnGameFrame);

	return true;
}

bool RestExtension::CreateHandleTypes(char *error, size_t maxlength)
{
	HandleError handleError;
	g_ClientType = handlesys->CreateType("RESTClient", this, 0, nullptr, nullptr, myself->GetIdentity(), &handleError);

	if (g_ClientType == 0)
	{
		ke::SafeSprintf(error, maxlength, "Could not create RESTClient handle type (error %d)", handleError);

		return false;
	}

	g_RequestType =
		handlesys->CreateType("RESTRequest", this, 0, nullptr, nullptr, myself->GetIdentity(), &handleError);

	if (g_RequestType == 0)
	{
		ke::SafeSprintf(error, maxlength, "Could not create RESTRequest handle type (error %d)", handleError);

		return false;
	}

	g_ResponseType =
		handlesys->CreateType("RESTResponse", this, 0, nullptr, nullptr, myself->GetIdentity(), &handleError);

	if (g_ResponseType == 0)
	{
		ke::SafeSprintf(error, maxlength, "Could not create RESTResponse handle type (error %d)", handleError);

		return false;
	}

	return true;
}

void RestExtension::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(OnGameFrame);
	plsys->RemovePluginsListener(this);
	g_MainQueue.SetEnabled(false);
	g_TransferThread.Stop();
	g_MainQueue.Drain();

	for (auto &entry : m_jobs)
	{
		ReleaseRecord(entry.second);
	}

	m_jobs.clear();

	handlesys->RemoveType(g_ResponseType, myself->GetIdentity());
	handlesys->RemoveType(g_RequestType, myself->GetIdentity());
	handlesys->RemoveType(g_ClientType, myself->GetIdentity());
	curl_global_cleanup();
}

void RestExtension::DetectCaBundle()
{
	char path[PLATFORM_MAX_PATH];
	smutils->BuildPath(Path_SM, path, sizeof(path), "configs/rest/ca-bundle.crt");

	if (libsys->PathExists(path))
	{
		m_caBundlePath = path;

		return;
	}

	if (libsys->PathExists(kSystemCaBundle))
	{
		m_caBundlePath = kSystemCaBundle;

		return;
	}

	smutils->LogError(myself, "No CA bundle found at %s or %s; HTTPS requests will fail", path, kSystemCaBundle);
}

void RestExtension::OnHandleDestroy(HandleType_t type, void *object)
{
	if (type == g_ClientType)
	{
		ClientObject *client = static_cast<ClientObject *>(object);
		DropJobsForClient(client->handle);
		delete client;

		return;
	}

	if (type == g_RequestType)
	{
		RequestObject *request = static_cast<RequestObject *>(object);
		ReleaseForward(request->headers);
		ReleaseForward(request->data);
		ReleaseForward(request->progress);
		delete request;

		return;
	}

	if (type == g_ResponseType)
	{
		delete static_cast<ResponseObject *>(object);
	}
}

void RestExtension::OnPluginUnloaded(IPlugin *plugin)
{
	IdentityToken_t *identity = plugin->GetIdentity();

	for (auto it = m_jobs.begin(); it != m_jobs.end();)
	{
		if (it->second.owner != identity)
		{
			++it;
			continue;
		}

		it = DropJob(it);
	}
}

void RestExtension::DropJobsForClient(Handle_t clientHandle)
{
	for (auto it = m_jobs.begin(); it != m_jobs.end();)
	{
		if (it->second.clientHandle != clientHandle)
		{
			++it;
			continue;
		}

		it = DropJob(it);
	}
}

RestExtension::JobMap::iterator RestExtension::DropJob(JobMap::iterator it)
{
	g_TransferThread.Cancel(it->first);

	if (it->second.dispatching)
	{
		it->second.dropped = true;

		return std::next(it);
	}

	ReleaseRecord(it->second);

	return m_jobs.erase(it);
}

void RestExtension::ReleaseRecord(JobRecord &record)
{
	ReleaseForward(record.completed);
	ReleaseForward(record.headers);
	ReleaseForward(record.data);
	ReleaseForward(record.progress);
}

int RestExtension::SubmitJob(RequestSpec spec, JobRecord record)
{
	const int id = m_nextJobId++;
	spec.id = id;
	spec.caBundle = m_caBundlePath;
	spec.verbose = m_verbose;
	record.id = id;
	m_jobs[id] = record;
	g_TransferThread.Enqueue(std::make_unique<RestJob>(std::move(spec), &g_MainQueue));

	return id;
}

bool RestExtension::CancelJob(int jobId, IdentityToken_t *requester)
{
	auto it = m_jobs.find(jobId);

	if (it == m_jobs.end())
	{
		return false;
	}

	if (it->second.owner != requester)
	{
		return false;
	}

	g_TransferThread.Cancel(jobId);

	return true;
}

int RestExtension::CancelJobsForClient(Handle_t clientHandle, IdentityToken_t *requester)
{
	int cancelled = 0;

	for (const auto &entry : m_jobs)
	{
		if (entry.second.clientHandle != clientHandle || entry.second.owner != requester)
		{
			continue;
		}

		g_TransferThread.Cancel(entry.first);
		cancelled++;
	}

	return cancelled;
}

void RestExtension::HandleEvent(const TransferEvent &event)
{
	auto it = m_jobs.find(event.jobId);

	if (it == m_jobs.end())
	{
		return;
	}

	if (event.kind != TransferEvent::Kind::Complete)
	{
		HandleIntermediate(it->second, event);

		return;
	}

	JobRecord record = it->second;
	m_jobs.erase(it);
	DispatchComplete(record, event);
	ReleaseRecord(record);
}

void RestExtension::HandleIntermediate(JobRecord &record, const TransferEvent &event)
{
	record.dispatching = true;

	switch (event.kind)
	{
		case TransferEvent::Kind::Progress:
			DispatchProgress(record, event);
			break;
		case TransferEvent::Kind::Headers:
			DispatchHeaders(record, event);
			break;
		case TransferEvent::Kind::Data:
			DispatchData(record, event);
			break;
		case TransferEvent::Kind::Complete:
			break;
	}

	record.dispatching = false;

	if (!record.dropped)
	{
		return;
	}

	ReleaseRecord(record);
	m_jobs.erase(event.jobId);
}

void RestExtension::DispatchProgress(const JobRecord &record, const TransferEvent &event)
{
	if (record.progress == nullptr || record.progress->GetFunctionCount() == 0)
	{
		return;
	}

	record.progress->PushCell(record.clientHandle);
	record.progress->PushCell(ClampToCell(event.downloaded));
	record.progress->PushCell(ClampToCell(event.downloadTotal));
	record.progress->PushCell(ClampToCell(event.uploaded));
	record.progress->PushCell(ClampToCell(event.uploadTotal));
	record.progress->PushCell(record.context);
	record.progress->Execute(nullptr);
}

void RestExtension::DispatchHeaders(const JobRecord &record, const TransferEvent &event)
{
	if (record.headers == nullptr || record.headers->GetFunctionCount() == 0)
	{
		return;
	}

	const Handle_t responseHandle = CreateResponseHandle(record, event);

	if (responseHandle == BAD_HANDLE)
	{
		return;
	}

	cell_t result = Pl_Continue;
	record.headers->PushCell(record.clientHandle);
	record.headers->PushCell(responseHandle);
	record.headers->PushCell(record.context);
	record.headers->Execute(&result);
	FreeOwnedHandle(responseHandle, record.owner);

	if (result >= Pl_Handled)
	{
		g_TransferThread.Cancel(record.id);
	}
}

void RestExtension::DispatchData(const JobRecord &record, const TransferEvent &event)
{
	const long long length = static_cast<long long>(event.body.size());

	if (record.data != nullptr && record.data->GetFunctionCount() > 0)
	{
		char *chunk = const_cast<char *>(event.body.c_str());
		const size_t chunkSize = event.body.size() + 1;
		record.data->PushCell(record.clientHandle);
		record.data->PushStringEx(chunk, chunkSize, SM_PARAM_STRING_BINARY | SM_PARAM_STRING_COPY, 0);
		record.data->PushCell(ClampToCell(length));
		record.data->PushCell(record.context);
		record.data->Execute(nullptr);
	}

	g_TransferThread.Consume(record.id, length);
}

void RestExtension::FreeOwnedHandle(Handle_t handle, IdentityToken_t *owner)
{
	if (handle == BAD_HANDLE)
	{
		return;
	}

	HandleSecurity const security(owner, myself->GetIdentity());
	handlesys->FreeHandle(handle, &security);
}

Handle_t RestExtension::CreateResponseHandle(const JobRecord &record, const TransferEvent &event)
{
	ResponseObject *response = new ResponseObject();
	response->status = event.status;
	response->httpStatus = event.httpStatus;
	response->contentLength = event.contentLength;
	response->bodyLength = event.bodyLength;
	response->elapsedMs = event.elapsedMs;
	response->error = event.error;
	response->url = event.url;
	response->headers = event.headers;
	response->body = event.body;

	HandleError handleError;
	const Handle_t handle =
		handlesys->CreateHandle(g_ResponseType, response, record.owner, myself->GetIdentity(), &handleError);

	if (handle == BAD_HANDLE)
	{
		delete response;
		smutils->LogError(myself, "Could not create RESTResponse handle (error %d)", handleError);
	}

	return handle;
}

void RestExtension::DispatchComplete(JobRecord &record, const TransferEvent &event)
{
	if (record.completed == nullptr || record.completed->GetFunctionCount() == 0)
	{
		return;
	}

	const Handle_t responseHandle = CreateResponseHandle(record, event);

	if (responseHandle == BAD_HANDLE)
	{
		return;
	}

	record.completed->PushCell(record.clientHandle);
	record.completed->PushCell(responseHandle);
	record.completed->PushCell(record.context);
	record.completed->Execute(nullptr);
	FreeOwnedHandle(responseHandle, record.owner);
}
