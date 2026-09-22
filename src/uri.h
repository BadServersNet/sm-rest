#pragma once

#include <string>

namespace rest
{

std::string UriEncode(const std::string &input);
bool IsAbsoluteUrl(const std::string &value);
std::string JoinUrl(const std::string &baseUrl, const std::string &path);
std::string AppendQuery(const std::string &url, const std::string &query);

}
