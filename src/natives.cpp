#include <cstring>
#include <type_traits>

#include "extension.h"
#include "text.h"
#include "uri.h"

template <typename T>
static T *ReadObject(IPluginContext *pContext, cell_t handle, HandleType_t type, const char *typeName)
{
	HandleSecurity const security(pContext->GetIdentity(), myself->GetIdentity());
	T *object;
	const HandleError error = handlesys->ReadHandle(handle, type, &security, reinterpret_cast<void **>(&object));

	if (error != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid %s handle %x (error %d)", typeName, handle, error);

		return nullptr;
	}

	return object;
}

static ClientObject *ReadClient(IPluginContext *pContext, cell_t handle)
{
	return ReadObject<ClientObject>(pContext, handle, g_ClientType, "RESTClient");
}

static RequestObject *ReadRequest(IPluginContext *pContext, cell_t handle)
{
	return ReadObject<RequestObject>(pContext, handle, g_RequestType, "RESTRequest");
}

static ResponseObject *ReadResponse(IPluginContext *pContext, cell_t handle)
{
	return ReadObject<ResponseObject>(pContext, handle, g_ResponseType, "RESTResponse");
}

static ClientConfig *ReadConfig(IPluginContext *pContext, cell_t handle, HandleType_t type)
{
	if (type == g_ClientType)
	{
		ClientObject *client = ReadClient(pContext, handle);

		return client == nullptr ? nullptr : &client->config;
	}

	RequestObject *request = ReadRequest(pContext, handle);

	return request == nullptr ? nullptr : &request->spec.config;
}

static std::string ReadString(IPluginContext *pContext, cell_t param)
{
	char *value;
	pContext->LocalToString(param, &value);

	return std::string(value);
}

static std::string ResolveGamePath(const std::string &relative)
{
	char path[PLATFORM_MAX_PATH];
	smutils->BuildPath(Path_Game, path, sizeof(path), "%s", relative.c_str());

	return std::string(path);
}

static IChangeableForward *CreateCallbackForward(
	IPluginContext *pContext, cell_t function, ExecType execType, int paramCount, const ParamType *types
)
{
	IPluginFunction *callback = pContext->GetFunctionById(function);

	if (callback == nullptr)
	{
		return nullptr;
	}

	IChangeableForward *forward = forwards->CreateForwardEx(nullptr, execType, paramCount, types);

	if (forward == nullptr)
	{
		return nullptr;
	}

	forward->AddFunction(callback);

	return forward;
}

static IChangeableForward *CreateCompletedForward(IPluginContext *pContext, cell_t function)
{
	static const ParamType types[] = { Param_Cell, Param_Cell, Param_Cell };

	return CreateCallbackForward(pContext, function, ET_Ignore, 3, types);
}

static IChangeableForward *CreateHeadersForward(IPluginContext *pContext, cell_t function)
{
	static const ParamType types[] = { Param_Cell, Param_Cell, Param_Cell };

	return CreateCallbackForward(pContext, function, ET_Single, 3, types);
}

static IChangeableForward *CreateDataForward(IPluginContext *pContext, cell_t function)
{
	static const ParamType types[] = { Param_Cell, Param_String, Param_Cell, Param_Cell };

	return CreateCallbackForward(pContext, function, ET_Ignore, 4, types);
}

static IChangeableForward *CreateProgressForward(IPluginContext *pContext, cell_t function)
{
	static const ParamType types[] = { Param_Cell, Param_Cell, Param_Cell, Param_Cell, Param_Cell, Param_Cell };

	return CreateCallbackForward(pContext, function, ET_Ignore, 6, types);
}

template <HandleType_t *Type, auto Field> static cell_t Native_ConfigGet(IPluginContext *pContext, const cell_t *params)
{
	ClientConfig *config = ReadConfig(pContext, params[1], *Type);

	if (config == nullptr)
	{
		return 0;
	}

	return ClampToCell(static_cast<long long>(config->*Field));
}

template <HandleType_t *Type, auto Field> static cell_t Native_ConfigSet(IPluginContext *pContext, const cell_t *params)
{
	ClientConfig *config = ReadConfig(pContext, params[1], *Type);

	if (config == nullptr)
	{
		return 0;
	}

	using FieldType = std::remove_reference_t<decltype(config->*Field)>;
	config->*Field = static_cast<FieldType>(params[2]);

	return 1;
}

template <HandleType_t *Type> static cell_t Native_SetHeader(IPluginContext *pContext, const cell_t *params)
{
	ClientConfig *config = ReadConfig(pContext, params[1], *Type);

	if (config == nullptr)
	{
		return 0;
	}

	const std::string name = rest::Trim(ReadString(pContext, params[2]));

	if (name.empty())
	{
		return pContext->ThrowNativeError("Header name is empty");
	}

	const std::string value = ReadString(pContext, params[3]);
	rest::SetHeader(config->headers, name, value);

	return 1;
}

template <HandleType_t *Type> static cell_t Native_RemoveHeader(IPluginContext *pContext, const cell_t *params)
{
	ClientConfig *config = ReadConfig(pContext, params[1], *Type);

	if (config == nullptr)
	{
		return 0;
	}

	const std::string name = ReadString(pContext, params[2]);
	rest::RemoveHeader(config->headers, name);

	return 1;
}

template <HandleType_t *Type> static cell_t Native_SetBasicAuth(IPluginContext *pContext, const cell_t *params)
{
	ClientConfig *config = ReadConfig(pContext, params[1], *Type);

	if (config == nullptr)
	{
		return 0;
	}

	config->basicUser = ReadString(pContext, params[2]);
	config->basicPassword = ReadString(pContext, params[3]);
	config->bearerToken.clear();

	return 1;
}

template <HandleType_t *Type> static cell_t Native_SetBearerToken(IPluginContext *pContext, const cell_t *params)
{
	ClientConfig *config = ReadConfig(pContext, params[1], *Type);

	if (config == nullptr)
	{
		return 0;
	}

	config->bearerToken = ReadString(pContext, params[2]);
	config->basicUser.clear();
	config->basicPassword.clear();

	return 1;
}

template <HandleType_t *Type> static cell_t Native_SetUserAgent(IPluginContext *pContext, const cell_t *params)
{
	ClientConfig *config = ReadConfig(pContext, params[1], *Type);

	if (config == nullptr)
	{
		return 0;
	}

	config->userAgent = ReadString(pContext, params[2]);

	return 1;
}

static cell_t Native_Client_Create(IPluginContext *pContext, const cell_t *params)
{
	ClientObject *client = new ClientObject();
	client->config.baseUrl = ReadString(pContext, params[1]);

	HandleError error;
	const Handle_t handle =
		handlesys->CreateHandle(g_ClientType, client, pContext->GetIdentity(), myself->GetIdentity(), &error);

	if (handle == BAD_HANDLE)
	{
		delete client;

		return pContext->ThrowNativeError("Could not create RESTClient handle (error %d)", error);
	}

	client->handle = handle;

	return handle;
}

static cell_t Native_Client_SetBaseUrl(IPluginContext *pContext, const cell_t *params)
{
	ClientObject *client = ReadClient(pContext, params[1]);

	if (client == nullptr)
	{
		return 0;
	}

	client->config.baseUrl = ReadString(pContext, params[2]);

	return 1;
}

static cell_t Native_Client_Request(IPluginContext *pContext, const cell_t *params)
{
	ClientObject *client = ReadClient(pContext, params[1]);

	if (client == nullptr)
	{
		return 0;
	}

	const cell_t method = params[2];

	if (method < 0 || method > static_cast<cell_t>(RestMethod::Options))
	{
		return pContext->ThrowNativeError("Invalid RESTMethod %d", method);
	}

	RequestObject *request = new RequestObject();
	request->clientHandle = client->handle;
	request->spec.method = static_cast<RestMethod>(method);
	request->spec.path = ReadString(pContext, params[3]);
	request->spec.config = client->config;

	HandleError error;
	const Handle_t handle =
		handlesys->CreateHandle(g_RequestType, request, pContext->GetIdentity(), myself->GetIdentity(), &error);

	if (handle == BAD_HANDLE)
	{
		delete request;

		return pContext->ThrowNativeError("Could not create RESTRequest handle (error %d)", error);
	}

	return handle;
}

static cell_t Native_Client_Cancel(IPluginContext *pContext, const cell_t *params)
{
	ClientObject *client = ReadClient(pContext, params[1]);

	if (client == nullptr)
	{
		return 0;
	}

	return g_RestExtension.CancelJob(params[2], pContext->GetIdentity()) ? 1 : 0;
}

static cell_t Native_Client_CancelAll(IPluginContext *pContext, const cell_t *params)
{
	ClientObject *client = ReadClient(pContext, params[1]);

	if (client == nullptr)
	{
		return 0;
	}

	return g_RestExtension.CancelJobsForClient(client->handle, pContext->GetIdentity());
}

static cell_t Native_Request_SetQuery(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	const std::string name = rest::UriEncode(ReadString(pContext, params[2]));
	const std::string value = rest::UriEncode(ReadString(pContext, params[3]));
	std::string &query = request->spec.query;

	if (!query.empty())
	{
		query += "&";
	}

	query += name + "=" + value;

	return 1;
}

static cell_t Native_Request_SetBody(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	const cell_t length = params[4];
	RequestSpec &spec = request->spec;

	if (length < 0)
	{
		spec.body = ReadString(pContext, params[2]);
	}
	else
	{
		cell_t *address;
		pContext->LocalToPhysAddr(params[2], &address);
		spec.body.assign(reinterpret_cast<const char *>(address), static_cast<size_t>(length));
	}

	spec.bodyKind = BodyKind::Memory;
	spec.bodyContentType = ReadString(pContext, params[3]);
	spec.bodyFilePath.clear();
	spec.formFields.clear();

	return 1;
}

static cell_t Native_Request_SetBodyFile(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	RequestSpec &spec = request->spec;
	spec.bodyKind = BodyKind::File;
	spec.bodyFilePath = ResolveGamePath(ReadString(pContext, params[2]));
	spec.bodyContentType = ReadString(pContext, params[3]);
	spec.body.clear();
	spec.formFields.clear();

	return 1;
}

static void SetFormKind(RequestSpec &spec, bool multipart)
{
	const bool alreadyMultipart = spec.bodyKind == BodyKind::Multipart;
	spec.bodyKind = multipart || alreadyMultipart ? BodyKind::Multipart : BodyKind::Form;
	spec.body.clear();
	spec.bodyFilePath.clear();
}

static cell_t Native_Request_AddFormField(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	FormField field;
	field.name = ReadString(pContext, params[2]);
	field.value = ReadString(pContext, params[3]);
	request->spec.formFields.push_back(field);
	SetFormKind(request->spec, false);

	return 1;
}

static cell_t Native_Request_AddFormFile(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	FormField field;
	field.name = ReadString(pContext, params[2]);
	field.filePath = ResolveGamePath(ReadString(pContext, params[3]));
	field.contentType = ReadString(pContext, params[4]);
	field.fileName = ReadString(pContext, params[5]);
	request->spec.formFields.push_back(field);
	SetFormKind(request->spec, true);

	return 1;
}

static cell_t Native_Request_SetOutputFile(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	request->spec.outputPath = ResolveGamePath(ReadString(pContext, params[2]));
	request->spec.resume = params[3] != 0;

	return 1;
}

static cell_t Native_Request_OnHeaders(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	IChangeableForward *forward = CreateHeadersForward(pContext, params[2]);

	if (forward == nullptr)
	{
		return pContext->ThrowNativeError("Invalid callback function");
	}

	ReleaseForward(request->headers);
	request->headers = forward;
	request->spec.wantHeaders = true;

	return 1;
}

static cell_t Native_Request_OnData(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	IChangeableForward *forward = CreateDataForward(pContext, params[2]);

	if (forward == nullptr)
	{
		return pContext->ThrowNativeError("Invalid callback function");
	}

	ReleaseForward(request->data);
	request->data = forward;
	request->spec.streamData = true;

	return 1;
}

static cell_t Native_Request_OnProgress(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	IChangeableForward *forward = CreateProgressForward(pContext, params[2]);

	if (forward == nullptr)
	{
		return pContext->ThrowNativeError("Invalid callback function");
	}

	ReleaseForward(request->progress);
	request->progress = forward;

	return 1;
}

static std::string BuildFormBody(const std::vector<FormField> &fields)
{
	std::string body;

	for (const FormField &field : fields)
	{
		if (!body.empty())
		{
			body += "&";
		}

		body += rest::UriEncode(field.name) + "=" + rest::UriEncode(field.value);
	}

	return body;
}

static cell_t Native_Request_Send(IPluginContext *pContext, const cell_t *params)
{
	RequestObject *request = ReadRequest(pContext, params[1]);

	if (request == nullptr)
	{
		return 0;
	}

	RequestSpec &spec = request->spec;
	const std::string url = rest::JoinUrl(spec.config.baseUrl, spec.path);

	if (!rest::IsAbsoluteUrl(url))
	{
		return pContext->ThrowNativeError(
			"Request URL \"%s\" is not absolute and the client has no base URL", url.c_str()
		);
	}

	IChangeableForward *completed = CreateCompletedForward(pContext, params[2]);

	if (completed == nullptr)
	{
		return pContext->ThrowNativeError("Invalid callback function");
	}

	spec.url = rest::AppendQuery(url, spec.query);

	if (spec.bodyKind == BodyKind::Form)
	{
		spec.body = BuildFormBody(spec.formFields);
		spec.bodyContentType = "application/x-www-form-urlencoded";
	}

	JobRecord record;
	record.clientHandle = request->clientHandle;
	record.owner = pContext->GetIdentity();
	record.completed = completed;
	record.headers = request->headers;
	record.data = request->data;
	record.progress = request->progress;
	record.context = params[3];
	request->headers = nullptr;
	request->data = nullptr;
	request->progress = nullptr;

	const int id = g_RestExtension.SubmitJob(std::move(spec), record);

	HandleSecurity const security(pContext->GetIdentity(), myself->GetIdentity());
	handlesys->FreeHandle(params[1], &security);

	return id;
}

static cell_t Native_Response_GetStatus(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	return response == nullptr ? 0 : static_cast<cell_t>(response->status);
}

static cell_t Native_Response_GetHttpStatus(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	return response == nullptr ? 0 : static_cast<cell_t>(response->httpStatus);
}

static cell_t Native_Response_GetContentLength(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	return response == nullptr ? -1 : ClampToCell(response->contentLength);
}

static cell_t Native_Response_GetBodyLength(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	return response == nullptr ? 0 : ClampToCell(response->bodyLength);
}

static cell_t Native_Response_GetElapsedMs(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	return response == nullptr ? 0 : ClampToCell(response->elapsedMs);
}

static cell_t Native_Response_GetHeaderCount(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	return response == nullptr ? 0 : static_cast<cell_t>(response->headers.size());
}

static cell_t Native_Response_GetError(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	if (response == nullptr)
	{
		return 0;
	}

	pContext->StringToLocalUTF8(params[2], params[3], response->error.c_str(), nullptr);

	return 1;
}

static cell_t Native_Response_GetUrl(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	if (response == nullptr)
	{
		return 0;
	}

	pContext->StringToLocalUTF8(params[2], params[3], response->url.c_str(), nullptr);

	return 1;
}

static cell_t Native_Response_GetHeader(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	if (response == nullptr)
	{
		return 0;
	}

	const std::string name = ReadString(pContext, params[2]);
	const std::string *value = rest::FindHeader(response->headers, name);

	if (value == nullptr)
	{
		return 0;
	}

	pContext->StringToLocalUTF8(params[3], params[4], value->c_str(), nullptr);

	return 1;
}

static cell_t Native_Response_GetHeaderAt(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	if (response == nullptr)
	{
		return 0;
	}

	const cell_t index = params[2];

	if (index < 0 || static_cast<size_t>(index) >= response->headers.size())
	{
		return pContext->ThrowNativeError(
			"Index %d is out of bounds (size %d)", index, static_cast<int>(response->headers.size())
		);
	}

	const auto &entry = response->headers[index];
	pContext->StringToLocalUTF8(params[3], params[4], entry.first.c_str(), nullptr);
	pContext->StringToLocalUTF8(params[5], params[6], entry.second.c_str(), nullptr);

	return 1;
}

static cell_t Native_Response_GetBody(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	if (response == nullptr)
	{
		return 0;
	}

	pContext->StringToLocalUTF8(params[2], params[3], response->body.c_str(), nullptr);

	return 1;
}

static cell_t Native_Response_ReadBody(IPluginContext *pContext, const cell_t *params)
{
	ResponseObject *response = ReadResponse(pContext, params[1]);

	if (response == nullptr)
	{
		return 0;
	}

	const cell_t offset = params[2];
	const cell_t maxlength = params[4];

	if (offset < 0 || maxlength < 0)
	{
		return pContext->ThrowNativeError("Offset and length must not be negative");
	}

	const size_t size = response->body.size();
	const size_t start = std::min(static_cast<size_t>(offset), size);
	const size_t count = std::min(static_cast<size_t>(maxlength), size - start);

	cell_t *address;
	pContext->LocalToPhysAddr(params[3], &address);
	memcpy(address, response->body.data() + start, count);

	return static_cast<cell_t>(count);
}

const sp_nativeinfo_t g_Natives[] = {
	{ "RESTClient.RESTClient", Native_Client_Create },
	{ "RESTClient.SetBaseUrl", Native_Client_SetBaseUrl },
	{ "RESTClient.SetHeader", Native_SetHeader<&g_ClientType> },
	{ "RESTClient.RemoveHeader", Native_RemoveHeader<&g_ClientType> },
	{ "RESTClient.SetBasicAuth", Native_SetBasicAuth<&g_ClientType> },
	{ "RESTClient.SetBearerToken", Native_SetBearerToken<&g_ClientType> },
	{ "RESTClient.SetUserAgent", Native_SetUserAgent<&g_ClientType> },
	{ "RESTClient.ConnectTimeout.get", Native_ConfigGet<&g_ClientType, &ClientConfig::connectTimeout> },
	{ "RESTClient.ConnectTimeout.set", Native_ConfigSet<&g_ClientType, &ClientConfig::connectTimeout> },
	{ "RESTClient.Timeout.get", Native_ConfigGet<&g_ClientType, &ClientConfig::timeout> },
	{ "RESTClient.Timeout.set", Native_ConfigSet<&g_ClientType, &ClientConfig::timeout> },
	{ "RESTClient.AbsoluteTimeout.get", Native_ConfigGet<&g_ClientType, &ClientConfig::absoluteTimeout> },
	{ "RESTClient.AbsoluteTimeout.set", Native_ConfigSet<&g_ClientType, &ClientConfig::absoluteTimeout> },
	{ "RESTClient.MaxRetries.get", Native_ConfigGet<&g_ClientType, &ClientConfig::maxRetries> },
	{ "RESTClient.MaxRetries.set", Native_ConfigSet<&g_ClientType, &ClientConfig::maxRetries> },
	{ "RESTClient.MaxRedirects.get", Native_ConfigGet<&g_ClientType, &ClientConfig::maxRedirects> },
	{ "RESTClient.MaxRedirects.set", Native_ConfigSet<&g_ClientType, &ClientConfig::maxRedirects> },
	{ "RESTClient.MaxSendSpeed.get", Native_ConfigGet<&g_ClientType, &ClientConfig::maxSendSpeed> },
	{ "RESTClient.MaxSendSpeed.set", Native_ConfigSet<&g_ClientType, &ClientConfig::maxSendSpeed> },
	{ "RESTClient.MaxRecvSpeed.get", Native_ConfigGet<&g_ClientType, &ClientConfig::maxRecvSpeed> },
	{ "RESTClient.MaxRecvSpeed.set", Native_ConfigSet<&g_ClientType, &ClientConfig::maxRecvSpeed> },
	{ "RESTClient.MaxBodySize.get", Native_ConfigGet<&g_ClientType, &ClientConfig::maxBodySize> },
	{ "RESTClient.MaxBodySize.set", Native_ConfigSet<&g_ClientType, &ClientConfig::maxBodySize> },
	{ "RESTClient.ChunkSize.get", Native_ConfigGet<&g_ClientType, &ClientConfig::chunkSize> },
	{ "RESTClient.ChunkSize.set", Native_ConfigSet<&g_ClientType, &ClientConfig::chunkSize> },
	{ "RESTClient.FollowRedirects.get", Native_ConfigGet<&g_ClientType, &ClientConfig::followRedirects> },
	{ "RESTClient.FollowRedirects.set", Native_ConfigSet<&g_ClientType, &ClientConfig::followRedirects> },
	{ "RESTClient.VerifyCertificate.get", Native_ConfigGet<&g_ClientType, &ClientConfig::verifyCertificate> },
	{ "RESTClient.VerifyCertificate.set", Native_ConfigSet<&g_ClientType, &ClientConfig::verifyCertificate> },
	{ "RESTClient.Request", Native_Client_Request },
	{ "RESTClient.Cancel", Native_Client_Cancel },
	{ "RESTClient.CancelAll", Native_Client_CancelAll },
	{ "RESTRequest.SetHeader", Native_SetHeader<&g_RequestType> },
	{ "RESTRequest.RemoveHeader", Native_RemoveHeader<&g_RequestType> },
	{ "RESTRequest.SetBasicAuth", Native_SetBasicAuth<&g_RequestType> },
	{ "RESTRequest.SetBearerToken", Native_SetBearerToken<&g_RequestType> },
	{ "RESTRequest.SetUserAgent", Native_SetUserAgent<&g_RequestType> },
	{ "RESTRequest.ConnectTimeout.get", Native_ConfigGet<&g_RequestType, &ClientConfig::connectTimeout> },
	{ "RESTRequest.ConnectTimeout.set", Native_ConfigSet<&g_RequestType, &ClientConfig::connectTimeout> },
	{ "RESTRequest.Timeout.get", Native_ConfigGet<&g_RequestType, &ClientConfig::timeout> },
	{ "RESTRequest.Timeout.set", Native_ConfigSet<&g_RequestType, &ClientConfig::timeout> },
	{ "RESTRequest.AbsoluteTimeout.get", Native_ConfigGet<&g_RequestType, &ClientConfig::absoluteTimeout> },
	{ "RESTRequest.AbsoluteTimeout.set", Native_ConfigSet<&g_RequestType, &ClientConfig::absoluteTimeout> },
	{ "RESTRequest.MaxRetries.get", Native_ConfigGet<&g_RequestType, &ClientConfig::maxRetries> },
	{ "RESTRequest.MaxRetries.set", Native_ConfigSet<&g_RequestType, &ClientConfig::maxRetries> },
	{ "RESTRequest.MaxRedirects.get", Native_ConfigGet<&g_RequestType, &ClientConfig::maxRedirects> },
	{ "RESTRequest.MaxRedirects.set", Native_ConfigSet<&g_RequestType, &ClientConfig::maxRedirects> },
	{ "RESTRequest.MaxSendSpeed.get", Native_ConfigGet<&g_RequestType, &ClientConfig::maxSendSpeed> },
	{ "RESTRequest.MaxSendSpeed.set", Native_ConfigSet<&g_RequestType, &ClientConfig::maxSendSpeed> },
	{ "RESTRequest.MaxRecvSpeed.get", Native_ConfigGet<&g_RequestType, &ClientConfig::maxRecvSpeed> },
	{ "RESTRequest.MaxRecvSpeed.set", Native_ConfigSet<&g_RequestType, &ClientConfig::maxRecvSpeed> },
	{ "RESTRequest.MaxBodySize.get", Native_ConfigGet<&g_RequestType, &ClientConfig::maxBodySize> },
	{ "RESTRequest.MaxBodySize.set", Native_ConfigSet<&g_RequestType, &ClientConfig::maxBodySize> },
	{ "RESTRequest.ChunkSize.get", Native_ConfigGet<&g_RequestType, &ClientConfig::chunkSize> },
	{ "RESTRequest.ChunkSize.set", Native_ConfigSet<&g_RequestType, &ClientConfig::chunkSize> },
	{ "RESTRequest.FollowRedirects.get", Native_ConfigGet<&g_RequestType, &ClientConfig::followRedirects> },
	{ "RESTRequest.FollowRedirects.set", Native_ConfigSet<&g_RequestType, &ClientConfig::followRedirects> },
	{ "RESTRequest.VerifyCertificate.get", Native_ConfigGet<&g_RequestType, &ClientConfig::verifyCertificate> },
	{ "RESTRequest.VerifyCertificate.set", Native_ConfigSet<&g_RequestType, &ClientConfig::verifyCertificate> },
	{ "RESTRequest.SetQuery", Native_Request_SetQuery },
	{ "RESTRequest.SetBody", Native_Request_SetBody },
	{ "RESTRequest.SetBodyFile", Native_Request_SetBodyFile },
	{ "RESTRequest.AddFormField", Native_Request_AddFormField },
	{ "RESTRequest.AddFormFile", Native_Request_AddFormFile },
	{ "RESTRequest.SetOutputFile", Native_Request_SetOutputFile },
	{ "RESTRequest.OnHeaders", Native_Request_OnHeaders },
	{ "RESTRequest.OnData", Native_Request_OnData },
	{ "RESTRequest.OnProgress", Native_Request_OnProgress },
	{ "RESTRequest.Send", Native_Request_Send },
	{ "RESTResponse.Status.get", Native_Response_GetStatus },
	{ "RESTResponse.HttpStatus.get", Native_Response_GetHttpStatus },
	{ "RESTResponse.ContentLength.get", Native_Response_GetContentLength },
	{ "RESTResponse.BodyLength.get", Native_Response_GetBodyLength },
	{ "RESTResponse.ElapsedMs.get", Native_Response_GetElapsedMs },
	{ "RESTResponse.HeaderCount.get", Native_Response_GetHeaderCount },
	{ "RESTResponse.GetError", Native_Response_GetError },
	{ "RESTResponse.GetUrl", Native_Response_GetUrl },
	{ "RESTResponse.GetHeader", Native_Response_GetHeader },
	{ "RESTResponse.GetHeaderAt", Native_Response_GetHeaderAt },
	{ "RESTResponse.GetBody", Native_Response_GetBody },
	{ "RESTResponse.ReadBody", Native_Response_ReadBody },
	{ nullptr, nullptr },
};
