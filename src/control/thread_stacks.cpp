#include "control/thread_stacks.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>

namespace bfvr::control
{
	std::string thread_stacks()
	{
#if defined(_M_X64)
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			return "error: no thread snapshot";
		}
		std::string text;
		THREADENTRY32 entry{ sizeof(entry) };
		for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
		{
			if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId())
			{
				continue;
			}
			const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
			if (!thread)
			{
				continue;
			}
			FILETIME created{}, exited{}, kernel{}, user{};
			GetThreadTimes(thread, &created, &exited, &kernel, &user);
			const double cpu_s = (static_cast<double>((static_cast<ULONGLONG>(user.dwHighDateTime) << 32) | user.dwLowDateTime)
				+ static_cast<double>((static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime)) / 1e7;

			// Nothing may allocate while the thread is suspended (it could hold the heap lock).
			DWORD64 frames[16]{};
			int depth = 0;
			if (SuspendThread(thread) != static_cast<DWORD>(-1))
			{
				CONTEXT context{};
				context.ContextFlags = CONTEXT_FULL;
				if (GetThreadContext(thread, &context))
				{
					// Unwinding reads only the suspended thread's stack and the images' unwind tables.
					for (; depth < 16 && context.Rip; ++depth)
					{
						frames[depth] = context.Rip;
						DWORD64 image_base{};
						PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
						if (!function)
						{
							// Leaf function: the return address is on top of the stack.
							MEMORY_BASIC_INFORMATION info{};
							if (!VirtualQuery(reinterpret_cast<void*>(context.Rsp), &info, sizeof(info)) || info.State != MEM_COMMIT)
							{
								break;
							}
							context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
							context.Rsp += 8;
							continue;
						}
						void* handler_data{};
						DWORD64 establisher{};
						RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data, &establisher, nullptr);
					}
				}
				ResumeThread(thread);
			}
			CloseHandle(thread);
			char line[64];
			std::snprintf(line, sizeof(line), "t%lu cpu=%.1fs:", entry.th32ThreadID, cpu_s);
			text += line;
			for (int i = 0; i < depth; ++i)
			{
				std::snprintf(line, sizeof(line), " %llX", frames[i]);
				text += line;
			}
			text += "\n";
		}
		CloseHandle(snapshot);
		return text;
#else
		return "error: x64 only";
#endif
	}
}
