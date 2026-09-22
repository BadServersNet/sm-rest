#include "uri.h"

namespace rest
{

static bool IsUnreserved(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
	{
		return true;
	}

	if (c >= 'a' && c <= 'z')
	{
		return true;
	}

	if (c >= '0' && c <= '9')
	{
		return true;
	}

	return c == '-' || c == '_' || c == '.' || c == '~';
}

std::string UriEncode(const std::string &input)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(input.size() * 3);

	for (unsigned char const c : input)
	{
		if (IsUnreserved(c))
		{
			out.push_back(static_cast<char>(c));
			continue;
		}

		out.push_back('%');
		out.push_back(hex[c >> 4]);
		out.push_back(hex[c & 0x0F]);
	}

	return out;
}

bool IsAbsoluteUrl(const std::string &value)
{
	return value.find("://") != std::string::npos;
}

std::string JoinUrl(const std::string &baseUrl, const std::string &path)
{
	if (IsAbsoluteUrl(path) || baseUrl.empty())
	{
		return path;
	}

	std::string base = baseUrl;

	while (!base.empty() && base.back() == '/')
	{
		base.pop_back();
	}

	if (path.empty())
	{
		return base;
	}

	if (path.front() == '/')
	{
		return base + path;
	}

	return base + "/" + path;
}

std::string AppendQuery(const std::string &url, const std::string &query)
{
	if (query.empty())
	{
		return url;
	}

	const bool hasQuery = url.find('?') != std::string::npos;
	const char separator = hasQuery ? '&' : '?';

	return url + separator + query;
}

}
