#include "control/crash_log.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>

#include "common/log.hpp"

namespace bfvr::control
{
	namespace
	{
		std::atomic<int> logged{};
		constexpr int max_logged = 8;

		bool fatal(DWORD code)
		{
			switch (code)
			{
			case EXCEPTION_ACCESS_VIOLATION:
			case EXCEPTION_ILLEGAL_INSTRUCTION:
			case EXCEPTION_PRIV_INSTRUCTION:
			case EXCEPTION_STACK_OVERFLOW:
			case EXCEPTION_INT_DIVIDE_BY_ZERO:
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			case EXCEPTION_IN_PAGE_ERROR:
			case 0xC0000409: // fast fail / security check
				return true;
			default:
				return false;
			}
		}

		LONG CALLBACK handler(EXCEPTION_POINTERS* info)
		{
#if defined(_M_X64)
			const EXCEPTION_RECORD& record = *info->ExceptionRecord;
			if (!fatal(record.ExceptionCode) || logged.fetch_add(1) >= max_logged)
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}
			CONTEXT context = *info->ContextRecord;
			const ULONG_PTR access = record.NumberParameters >= 2 ? record.ExceptionInformation[0] : 0;
			const ULONG_PTR target = record.NumberParameters >= 2 ? record.ExceptionInformation[1] : 0;
			log::write("exception %08lX thread %lu at %llX (%s %llX)", record.ExceptionCode, GetCurrentThreadId(),
				context.Rip, access == 1 ? "write" : access == 8 ? "execute" : "read", static_cast<unsigned long long>(target));
			log::write("  rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX rbp=%llX rsp=%llX", context.Rax, context.Rbx,
				context.Rcx, context.Rdx, context.Rsi, context.Rdi, context.Rbp, context.Rsp);
			log::write("  r8=%llX r9=%llX r10=%llX r11=%llX r12=%llX r13=%llX r14=%llX r15=%llX", context.R8, context.R9,
				context.R10, context.R11, context.R12, context.R13, context.R14, context.R15);
			char line[16 * 18 + 16]{};
			int used = 0;
			for (int depth = 0; depth < 16 && context.Rip; ++depth)
			{
				used += std::snprintf(line + used, sizeof(line) - used, " %llX", context.Rip);
				DWORD64 image_base{};
				PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
				if (!function)
				{
					MEMORY_BASIC_INFORMATION memory{};
					if (!VirtualQuery(reinterpret_cast<void*>(context.Rsp), &memory, sizeof(memory)) || memory.State != MEM_COMMIT)
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
			log::write("  stack:%s", line);
#else
			(void)info;
#endif
			return EXCEPTION_CONTINUE_SEARCH;
		}
	}

	void install_crash_log()
	{
		AddVectoredExceptionHandler(1, &handler);
		log::write("crash log: vectored exception handler installed");
	}
}
