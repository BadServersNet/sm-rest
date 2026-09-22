#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rest
{

using HeaderList = std::vector<std::pair<std::string, std::string>>;

std::string ToLower(const std::string &value);
std::string Trim(const std::string &value);
bool EqualsIgnoreCase(const std::string &left, const std::string &right);
void SetHeader(HeaderList &headers, const std::string &name, const std::string &value);
void RemoveHeader(HeaderList &headers, const std::string &name);
const std::string *FindHeader(const HeaderList &headers, const std::string &name);

}
