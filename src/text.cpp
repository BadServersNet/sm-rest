#include "text.h"

#include <algorithm>
#include <cctype>

namespace rest
{

static char LowerChar(unsigned char c)
{
	return static_cast<char>(std::tolower(c));
}

std::string ToLower(const std::string &value)
{
	std::string out = value;
	std::transform(out.begin(), out.end(), out.begin(), LowerChar);

	return out;
}

std::string Trim(const std::string &value)
{
	const size_t start = value.find_first_not_of(" \t\r\n");

	if (start == std::string::npos)
	{
		return "";
	}

	const size_t end = value.find_last_not_of(" \t\r\n");

	return value.substr(start, end - start + 1);
}

bool EqualsIgnoreCase(const std::string &left, const std::string &right)
{
	if (left.size() != right.size())
	{
		return false;
	}

	for (size_t i = 0; i < left.size(); i++)
	{
		if (LowerChar(left[i]) != LowerChar(right[i]))
		{
			return false;
		}
	}

	return true;
}

void SetHeader(HeaderList &headers, const std::string &name, const std::string &value)
{
	for (auto &entry : headers)
	{
		if (EqualsIgnoreCase(entry.first, name))
		{
			entry.second = value;

			return;
		}
	}

	headers.emplace_back(name, value);
}

void RemoveHeader(HeaderList &headers, const std::string &name)
{
	const auto matches = [&name](const std::pair<std::string, std::string> &entry)
	{
		return EqualsIgnoreCase(entry.first, name);
	};
	headers.erase(std::remove_if(headers.begin(), headers.end(), matches), headers.end());
}

const std::string *FindHeader(const HeaderList &headers, const std::string &name)
{
	for (const auto &entry : headers)
	{
		if (EqualsIgnoreCase(entry.first, name))
		{
			return &entry.second;
		}
	}

	return nullptr;
}

}
