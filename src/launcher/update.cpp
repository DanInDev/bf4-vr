#include "launcher/update.hpp"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <vector>

#ifndef BFVR_VERSION
#define BFVR_VERSION "0.0.0"
#endif

namespace bfvr::launcher
{
	namespace
	{
		// The value of a top-level string field ("name": "value"); JSON escapes other than \" and \\ are kept as written.
		std::string string_field(std::string_view json, std::string_view name)
		{
			const std::string key = "\"" + std::string(name) + "\"";
			std::size_t at = json.find(key);
			if (at == std::string_view::npos)
			{
				return {};
			}
			at = json.find(':', at + key.size());
			if (at == std::string_view::npos)
			{
				return {};
			}
			at = json.find_first_not_of(" \t\r\n", at + 1);
			if (at == std::string_view::npos || json[at] != '"')
			{
				return {};
			}
			std::string value;
			for (std::size_t i = at + 1; i < json.size(); ++i)
			{
				if (json[i] == '\\' && i + 1 < json.size())
				{
					value += json[++i];
				}
				else if (json[i] == '"')
				{
					return value;
				}
				else
				{
					value += json[i];
				}
			}
			return {};
		}

		struct handle
		{
			HINTERNET value{};
			~handle()
			{
				if (value)
				{
					WinHttpCloseHandle(value);
				}
			}
		};
	}

	std::string_view mod_version()
	{
		return BFVR_VERSION;
	}

	release parse_release(std::string_view json)
	{
		release out;
		out.tag = string_field(json, "tag_name");
		out.url = string_field(json, "html_url");
		out.ok = !out.tag.empty();
		if (!out.ok)
		{
			out.error = "no tag_name in GitHub's answer";
		}
		return out;
	}

	int compare_versions(std::string_view a, std::string_view b)
	{
		const auto strip = [](std::string_view v) {
			return !v.empty() && (v.front() == 'v' || v.front() == 'V') ? v.substr(1) : v;
		};
		a = strip(a);
		b = strip(b);
		while (!a.empty() || !b.empty())
		{
			const auto take = [](std::string_view& v) {
				long part = 0;
				std::size_t i = 0;
				for (; i < v.size() && std::isdigit(static_cast<unsigned char>(v[i])); ++i)
				{
					part = part * 10 + (v[i] - '0');
				}
				const std::size_t dot = v.find('.', i);
				v = dot == std::string_view::npos ? std::string_view{} : v.substr(dot + 1);
				return part;
			};
			const long x = take(a), y = take(b);
			if (x != y)
			{
				return x < y ? -1 : 1;
			}
		}
		return 0;
	}

	release latest_release()
	{
		release out;
		handle session{ WinHttpOpen(L"bf-vr-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
		if (!session.value)
		{
			out.error = "WinHttpOpen failed";
			return out;
		}
		WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000);
		handle connection{ WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) };
		handle request{ connection.value ? WinHttpOpenRequest(connection.value, L"GET", L"/repos/DanInDev/bf4-vr/releases/latest", nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr };
		if (!request.value
			|| !WinHttpSendRequest(request.value, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			|| !WinHttpReceiveResponse(request.value, nullptr))
		{
			out.error = "GitHub could not be reached (error " + std::to_string(GetLastError()) + ")";
			return out;
		}
		DWORD status = 0, size = sizeof(status);
		WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		std::string body;
		for (DWORD available = 0; WinHttpQueryDataAvailable(request.value, &available) && available > 0 && body.size() < (1u << 20);)
		{
			std::vector<char> chunk(available);
			DWORD read = 0;
			if (!WinHttpReadData(request.value, chunk.data(), available, &read) || read == 0)
			{
				break;
			}
			body.append(chunk.data(), read);
		}
		if (status == 404)
		{
			out.error = "no release published yet";
			return out;
		}
		if (status != 200)
		{
			out.error = "GitHub answered " + std::to_string(status);
			return out;
		}
		return parse_release(body);
	}
}
