#include "common/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace bfvr::log
{
	namespace
	{
		bool log_path(wchar_t (&path)[MAX_PATH])
		{
			const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
			if (length == 0 || length >= MAX_PATH)
			{
				return false;
			}

			wchar_t* separator = wcsrchr(path, L'\\');
			if (!separator)
			{
				return false;
			}

			separator[1] = L'\0';
			return wcscat_s(path, L"bfvr.log") == 0;
		}
	}

	void write(const char* format, ...)
	{
		char line[1024];

		SYSTEMTIME time{};
		GetLocalTime(&time);
		int offset = std::snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [%lu] ",
			time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId());
		if (offset < 0)
		{
			return;
		}

		va_list arguments;
		va_start(arguments, format);
		const int written = std::vsnprintf(line + offset, sizeof(line) - offset - 2, format, arguments);
		va_end(arguments);
		if (written < 0)
		{
			return;
		}

		offset += written;
		if (offset > static_cast<int>(sizeof(line)) - 3)
		{
			offset = static_cast<int>(sizeof(line)) - 3;
		}
		line[offset++] = '\r';
		line[offset++] = '\n';

		wchar_t path[MAX_PATH];
		if (!log_path(path))
		{
			return;
		}

		// FILE_APPEND_DATA makes each write an atomic append, so threads cannot interleave lines.
		const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return;
		}

		DWORD bytes{};
		WriteFile(file, line, static_cast<DWORD>(offset), &bytes, nullptr);
		CloseHandle(file);
	}
}
