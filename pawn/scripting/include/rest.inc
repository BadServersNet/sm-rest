#if defined _rest_included
 #endinput
#endif
#define _rest_included

enum RESTStatus
{
	RESTStatus_Ok = 0,
	RESTStatus_HttpError,
	RESTStatus_NetworkError,
	RESTStatus_Timeout,
	RESTStatus_IoError,
	RESTStatus_TooLarge,
	RESTStatus_Cancelled,
};

enum RESTMethod
{
	RESTMethod_Get = 0,
	RESTMethod_Head,
	RESTMethod_Post,
	RESTMethod_Put,
	RESTMethod_Patch,
	RESTMethod_Delete,
	RESTMethod_Options,
};

methodmap RESTResponse < Handle
{
	property RESTStatus Status
	{
		public native get();
	}

	property int HttpStatus
	{
		public native get();
	}

	// Content-Length header, or -1 if absent.
	property int ContentLength
	{
		public native get();
	}

	// Bytes received into memory, the output file, or the data callback.
	property int BodyLength
	{
		public native get();
	}

	property int ElapsedMs
	{
		public native get();
	}

	property int HeaderCount
	{
		public native get();
	}

	public native void GetError(char[] buffer, int maxlength);

	// Final URL after redirects.
	public native void GetUrl(char[] buffer, int maxlength);

	public native bool GetHeader(const char[] name, char[] buffer, int maxlength);
	public native void GetHeaderAt(int index, char[] name, int nameMaxlength, char[] value, int valueMaxlength);

	// Body as a string. Empty when the body went to a file or the data callback.
	public native void GetBody(char[] buffer, int maxlength);

	// Copies raw body bytes starting at offset. Returns the number of bytes copied.
	public native int ReadBody(int offset, char[] buffer, int maxlength);
};

typeset RESTCallback
{
	function void (RESTClient client, RESTResponse response, any data);
};

// Return Plugin_Handled or Plugin_Stop to abort the transfer. The response has no body yet.
typeset RESTHeadersCallback
{
	function Action (RESTClient client, RESTResponse response, any data);
};

typeset RESTDataCallback
{
	function void (RESTClient client, const char[] chunk, int length, any data);
};

typeset RESTProgressCallback
{
	function void (RESTClient client, int downloaded, int downloadTotal, int uploaded, int uploadTotal, any data);
};

// A request is consumed by Send, which frees the handle.
methodmap RESTRequest < Handle
{
	public native void SetHeader(const char[] name, const char[] value);
	public native void RemoveHeader(const char[] name);
	public native void SetQuery(const char[] name, const char[] value);
	public native void SetBasicAuth(const char[] user, const char[] password);
	public native void SetBearerToken(const char[] token);
	public native void SetUserAgent(const char[] userAgent);

	// Raw body. length -1 sends the string up to its terminator, otherwise exactly length bytes.
	public native void SetBody(const char[] body, const char[] contentType = "application/json", int length = -1);

	// Streams a file from disk as the body. Path is relative to the game directory.
	public native void SetBodyFile(const char[] path, const char[] contentType = "application/octet-stream");

	// Form fields are sent urlencoded, or as multipart/form-data once a file is added.
	public native void AddFormField(const char[] name, const char[] value);
	public native void AddFormFile(const char[] name, const char[] path, const char[] contentType = "application/octet-stream", const char[] fileName = "");

	// Streams a 2xx body into "<path>.part" and renames it onto path. Resume continues an existing .part with a Range request.
	public native void SetOutputFile(const char[] path, bool resume = false);

	property int ConnectTimeout
	{
		public native get();
		public native set(int value);
	}

	// Stall timeout in seconds.
	property int Timeout
	{
		public native get();
		public native set(int value);
	}

	// Whole-transfer timeout in seconds, 0 for none.
	property int AbsoluteTimeout
	{
		public native get();
		public native set(int value);
	}

	// Retries for network errors, timeouts, 429, 502, 503 and 504. Retry-After is honoured.
	property int MaxRetries
	{
		public native get();
		public native set(int value);
	}

	property int MaxRedirects
	{
		public native get();
		public native set(int value);
	}

	property int MaxSendSpeed
	{
		public native get();
		public native set(int value);
	}

	property int MaxRecvSpeed
	{
		public native get();
		public native set(int value);
	}

	// In-memory body cap in bytes. Larger bodies fail with RESTStatus_TooLarge.
	property int MaxBodySize
	{
		public native get();
		public native set(int value);
	}

	// Receive buffer size in bytes (1024 to 10 MiB), which bounds the size of each OnData chunk.
	property int ChunkSize
	{
		public native get();
		public native set(int value);
	}

	property bool FollowRedirects
	{
		public native get();
		public native set(bool value);
	}

	property bool VerifyCertificate
	{
		public native get();
		public native set(bool value);
	}

	public native void OnHeaders(RESTHeadersCallback callback);

	// Receives the 2xx body in chunks instead of buffering it. Non-2xx bodies are still buffered.
	public native void OnData(RESTDataCallback callback);

	public native void OnProgress(RESTProgressCallback callback);

	// Starts the request and frees this handle. Returns a request id usable with RESTClient.Cancel.
	public native int Send(RESTCallback callback, any data = 0);
};

// Holds defaults copied into every request it creates. Deleting the client drops its in-flight requests without callbacks.
methodmap RESTClient < Handle
{
	public native RESTClient(const char[] baseUrl = "");

	public native void SetBaseUrl(const char[] baseUrl);
	public native void SetHeader(const char[] name, const char[] value);
	public native void RemoveHeader(const char[] name);
	public native void SetBasicAuth(const char[] user, const char[] password);
	public native void SetBearerToken(const char[] token);
	public native void SetUserAgent(const char[] userAgent);

	property int ConnectTimeout
	{
		public native get();
		public native set(int value);
	}

	property int Timeout
	{
		public native get();
		public native set(int value);
	}

	property int AbsoluteTimeout
	{
		public native get();
		public native set(int value);
	}

	property int MaxRetries
	{
		public native get();
		public native set(int value);
	}

	property int MaxRedirects
	{
		public native get();
		public native set(int value);
	}

	property int MaxSendSpeed
	{
		public native get();
		public native set(int value);
	}

	property int MaxRecvSpeed
	{
		public native get();
		public native set(int value);
	}

	property int MaxBodySize
	{
		public native get();
		public native set(int value);
	}

	// Receive buffer size in bytes (1024 to 10 MiB), which bounds the size of each OnData chunk.
	property int ChunkSize
	{
		public native get();
		public native set(int value);
	}

	property bool FollowRedirects
	{
		public native get();
		public native set(bool value);
	}

	property bool VerifyCertificate
	{
		public native get();
		public native set(bool value);
	}

	// Path is resolved against the base URL unless it is an absolute URL.
	public native RESTRequest Request(RESTMethod method, const char[] path);

	// The callback still runs with RESTStatus_Cancelled.
	public native bool Cancel(int requestId);
	public native int CancelAll();
};

public Extension __ext_rest =
{
	name = "REST",
	file = "rest.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_rest_SetNTVOptional()
{
	MarkNativeAsOptional("RESTClient.RESTClient");
	MarkNativeAsOptional("RESTClient.SetBaseUrl");
	MarkNativeAsOptional("RESTClient.SetHeader");
	MarkNativeAsOptional("RESTClient.RemoveHeader");
	MarkNativeAsOptional("RESTClient.SetBasicAuth");
	MarkNativeAsOptional("RESTClient.SetBearerToken");
	MarkNativeAsOptional("RESTClient.SetUserAgent");
	MarkNativeAsOptional("RESTClient.ConnectTimeout.get");
	MarkNativeAsOptional("RESTClient.ConnectTimeout.set");
	MarkNativeAsOptional("RESTClient.Timeout.get");
	MarkNativeAsOptional("RESTClient.Timeout.set");
	MarkNativeAsOptional("RESTClient.AbsoluteTimeout.get");
	MarkNativeAsOptional("RESTClient.AbsoluteTimeout.set");
	MarkNativeAsOptional("RESTClient.MaxRetries.get");
	MarkNativeAsOptional("RESTClient.MaxRetries.set");
	MarkNativeAsOptional("RESTClient.MaxRedirects.get");
	MarkNativeAsOptional("RESTClient.MaxRedirects.set");
	MarkNativeAsOptional("RESTClient.MaxSendSpeed.get");
	MarkNativeAsOptional("RESTClient.MaxSendSpeed.set");
	MarkNativeAsOptional("RESTClient.MaxRecvSpeed.get");
	MarkNativeAsOptional("RESTClient.MaxRecvSpeed.set");
	MarkNativeAsOptional("RESTClient.MaxBodySize.get");
	MarkNativeAsOptional("RESTClient.MaxBodySize.set");
	MarkNativeAsOptional("RESTClient.ChunkSize.get");
	MarkNativeAsOptional("RESTClient.ChunkSize.set");
	MarkNativeAsOptional("RESTClient.FollowRedirects.get");
	MarkNativeAsOptional("RESTClient.FollowRedirects.set");
	MarkNativeAsOptional("RESTClient.VerifyCertificate.get");
	MarkNativeAsOptional("RESTClient.VerifyCertificate.set");
	MarkNativeAsOptional("RESTClient.Request");
	MarkNativeAsOptional("RESTClient.Cancel");
	MarkNativeAsOptional("RESTClient.CancelAll");
	MarkNativeAsOptional("RESTRequest.SetHeader");
	MarkNativeAsOptional("RESTRequest.RemoveHeader");
	MarkNativeAsOptional("RESTRequest.SetBasicAuth");
	MarkNativeAsOptional("RESTRequest.SetBearerToken");
	MarkNativeAsOptional("RESTRequest.SetUserAgent");
	MarkNativeAsOptional("RESTRequest.ConnectTimeout.get");
	MarkNativeAsOptional("RESTRequest.ConnectTimeout.set");
	MarkNativeAsOptional("RESTRequest.Timeout.get");
	MarkNativeAsOptional("RESTRequest.Timeout.set");
	MarkNativeAsOptional("RESTRequest.AbsoluteTimeout.get");
	MarkNativeAsOptional("RESTRequest.AbsoluteTimeout.set");
	MarkNativeAsOptional("RESTRequest.MaxRetries.get");
	MarkNativeAsOptional("RESTRequest.MaxRetries.set");
	MarkNativeAsOptional("RESTRequest.MaxRedirects.get");
	MarkNativeAsOptional("RESTRequest.MaxRedirects.set");
	MarkNativeAsOptional("RESTRequest.MaxSendSpeed.get");
	MarkNativeAsOptional("RESTRequest.MaxSendSpeed.set");
	MarkNativeAsOptional("RESTRequest.MaxRecvSpeed.get");
	MarkNativeAsOptional("RESTRequest.MaxRecvSpeed.set");
	MarkNativeAsOptional("RESTRequest.MaxBodySize.get");
	MarkNativeAsOptional("RESTRequest.MaxBodySize.set");
	MarkNativeAsOptional("RESTRequest.ChunkSize.get");
	MarkNativeAsOptional("RESTRequest.ChunkSize.set");
	MarkNativeAsOptional("RESTRequest.FollowRedirects.get");
	MarkNativeAsOptional("RESTRequest.FollowRedirects.set");
	MarkNativeAsOptional("RESTRequest.VerifyCertificate.get");
	MarkNativeAsOptional("RESTRequest.VerifyCertificate.set");
	MarkNativeAsOptional("RESTRequest.SetQuery");
	MarkNativeAsOptional("RESTRequest.SetBody");
	MarkNativeAsOptional("RESTRequest.SetBodyFile");
	MarkNativeAsOptional("RESTRequest.AddFormField");
	MarkNativeAsOptional("RESTRequest.AddFormFile");
	MarkNativeAsOptional("RESTRequest.SetOutputFile");
	MarkNativeAsOptional("RESTRequest.OnHeaders");
	MarkNativeAsOptional("RESTRequest.OnData");
	MarkNativeAsOptional("RESTRequest.OnProgress");
	MarkNativeAsOptional("RESTRequest.Send");
	MarkNativeAsOptional("RESTResponse.Status.get");
	MarkNativeAsOptional("RESTResponse.HttpStatus.get");
	MarkNativeAsOptional("RESTResponse.ContentLength.get");
	MarkNativeAsOptional("RESTResponse.BodyLength.get");
	MarkNativeAsOptional("RESTResponse.ElapsedMs.get");
	MarkNativeAsOptional("RESTResponse.HeaderCount.get");
	MarkNativeAsOptional("RESTResponse.GetError");
	MarkNativeAsOptional("RESTResponse.GetUrl");
	MarkNativeAsOptional("RESTResponse.GetHeader");
	MarkNativeAsOptional("RESTResponse.GetHeaderAt");
	MarkNativeAsOptional("RESTResponse.GetBody");
	MarkNativeAsOptional("RESTResponse.ReadBody");
}
#endif
