#include <sourcemod>
#include <rest>

#pragma newdecls required
#pragma semicolon 1

public Plugin myinfo =
{
	name = "REST Example",
	author = "BuSheezy",
	description = "Exercises the REST extension from the server console",
	version = "0.1.0",
	url = "https://github.com/BadServersNet/sm-rest"
};

RESTClient gH_Client;

public void OnPluginStart()
{
	gH_Client = new RESTClient();
	gH_Client.SetHeader("Accept", "application/json");

	RegServerCmd("sm_rest_get", Command_Get, "sm_rest_get <url>");
	RegServerCmd("sm_rest_post", Command_Post, "sm_rest_post <url> <json>");
	RegServerCmd("sm_rest_download", Command_Download, "sm_rest_download <url> <path>");
	RegServerCmd("sm_rest_upload", Command_Upload, "sm_rest_upload <url> <path>");
	RegServerCmd("sm_rest_form", Command_Form, "sm_rest_form <url> <field> <path>");
	RegServerCmd("sm_rest_stream", Command_Stream, "sm_rest_stream <url>");
	RegServerCmd("sm_rest_cancel", Command_Cancel, "sm_rest_cancel <requestId>");
}

static void PrintResponse(const char[] what, RESTResponse response)
{
	char error[256];
	response.GetError(error, sizeof(error));
	char url[512];
	response.GetUrl(url, sizeof(url));
	PrintToServer("rest-example: %s -> status=%d http=%d body=%d bytes in %dms url=%s error=%s",
		what, response.Status, response.HttpStatus, response.BodyLength, response.ElapsedMs, url, error);
}

public void OnRequestDone(RESTClient client, RESTResponse response, any data)
{
	PrintResponse("request", response);

	char body[1024];
	response.GetBody(body, sizeof(body));

	if (body[0] != '\0')
	{
		PrintToServer("%s", body);
	}
}

public Action OnHeaders(RESTClient client, RESTResponse response, any data)
{
	char name[128];
	char value[512];

	for (int i = 0; i < response.HeaderCount; i++)
	{
		response.GetHeaderAt(i, name, sizeof(name), value, sizeof(value));
		PrintToServer("  %s: %s", name, value);
	}

	return Plugin_Continue;
}

public void OnData(RESTClient client, const char[] chunk, int length, any data)
{
	PrintToServer("rest-example: chunk of %d bytes", length);
}

public void OnProgress(RESTClient client, int downloaded, int downloadTotal, int uploaded, int uploadTotal, any data)
{
	PrintToServer("rest-example: down %d/%d up %d/%d", downloaded, downloadTotal, uploaded, uploadTotal);
}

public Action Command_Get(int args)
{
	if (args < 1)
	{
		return Plugin_Handled;
	}

	char url[512];
	GetCmdArg(1, url, sizeof(url));

	RESTRequest request = gH_Client.Request(RESTMethod_Get, url);
	request.OnHeaders(OnHeaders);
	int id = request.Send(OnRequestDone);
	PrintToServer("rest-example: get %s (request %d)", url, id);

	return Plugin_Handled;
}

public Action Command_Post(int args)
{
	if (args < 2)
	{
		return Plugin_Handled;
	}

	char url[512];
	char json[1024];
	GetCmdArg(1, url, sizeof(url));
	GetCmdArg(2, json, sizeof(json));

	RESTRequest request = gH_Client.Request(RESTMethod_Post, url);
	request.SetBody(json);
	int id = request.Send(OnRequestDone);
	PrintToServer("rest-example: post %s (request %d)", url, id);

	return Plugin_Handled;
}

public Action Command_Download(int args)
{
	if (args < 2)
	{
		return Plugin_Handled;
	}

	char url[512];
	char path[PLATFORM_MAX_PATH];
	GetCmdArg(1, url, sizeof(url));
	GetCmdArg(2, path, sizeof(path));

	RESTRequest request = gH_Client.Request(RESTMethod_Get, url);
	request.SetOutputFile(path, true);
	request.OnProgress(OnProgress);
	int id = request.Send(OnRequestDone);
	PrintToServer("rest-example: download %s -> %s (request %d)", url, path, id);

	return Plugin_Handled;
}

public Action Command_Upload(int args)
{
	if (args < 2)
	{
		return Plugin_Handled;
	}

	char url[512];
	char path[PLATFORM_MAX_PATH];
	GetCmdArg(1, url, sizeof(url));
	GetCmdArg(2, path, sizeof(path));

	RESTRequest request = gH_Client.Request(RESTMethod_Put, url);
	request.SetBodyFile(path);
	request.OnProgress(OnProgress);
	int id = request.Send(OnRequestDone);
	PrintToServer("rest-example: upload %s <- %s (request %d)", url, path, id);

	return Plugin_Handled;
}

public Action Command_Form(int args)
{
	if (args < 3)
	{
		return Plugin_Handled;
	}

	char url[512];
	char field[64];
	char path[PLATFORM_MAX_PATH];
	GetCmdArg(1, url, sizeof(url));
	GetCmdArg(2, field, sizeof(field));
	GetCmdArg(3, path, sizeof(path));

	RESTRequest request = gH_Client.Request(RESTMethod_Post, url);
	request.AddFormField("source", "rest-example");
	request.AddFormFile(field, path);
	request.OnProgress(OnProgress);
	int id = request.Send(OnRequestDone);
	PrintToServer("rest-example: form %s <- %s (request %d)", url, path, id);

	return Plugin_Handled;
}

public Action Command_Stream(int args)
{
	if (args < 1)
	{
		return Plugin_Handled;
	}

	char url[512];
	GetCmdArg(1, url, sizeof(url));

	RESTRequest request = gH_Client.Request(RESTMethod_Get, url);
	request.OnData(OnData);
	int id = request.Send(OnRequestDone);
	PrintToServer("rest-example: stream %s (request %d)", url, id);

	return Plugin_Handled;
}

public Action Command_Cancel(int args)
{
	if (args < 1)
	{
		return Plugin_Handled;
	}

	int id = GetCmdArgInt(1);
	bool cancelled = gH_Client.Cancel(id);
	PrintToServer("rest-example: cancel %d -> %s", id, cancelled ? "ok" : "not found");

	return Plugin_Handled;
}
