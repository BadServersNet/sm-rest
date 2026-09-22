#pragma once

#include <string>
#include <vector>

#include "text.h"

enum class RestMethod
{
	Get = 0,
	Head,
	Post,
	Put,
	Patch,
	Delete,
	Options,
};

enum class BodyKind
{
	None,
	Memory,
	File,
	Form,
	Multipart,
};

struct FormField
{
	std::string name;
	std::string value;
	std::string filePath;
	std::string fileName;
	std::string contentType;
};

struct ClientConfig
{
	std::string baseUrl;
	rest::HeaderList headers;
	std::string userAgent;
	std::string basicUser;
	std::string basicPassword;
	std::string bearerToken;
	int connectTimeout = 10;
	int timeout = 60;
	int absoluteTimeout = 0;
	int maxRetries = 3;
	int maxRedirects = 10;
	long maxSendSpeed = 0;
	long maxRecvSpeed = 0;
	long long maxBodySize = 4LL * 1024 * 1024;
	bool followRedirects = true;
	bool verifyCertificate = true;
};

struct RequestSpec
{
	int id = 0;
	RestMethod method = RestMethod::Get;
	std::string path;
	std::string query;
	ClientConfig config;
	BodyKind bodyKind = BodyKind::None;
	std::string body;
	std::string bodyContentType;
	std::string bodyFilePath;
	std::vector<FormField> formFields;
	std::string outputPath;
	bool resume = false;
	bool streamData = false;
	bool wantHeaders = false;
	std::string url;
	std::string caBundle;
	bool verbose = false;
};
