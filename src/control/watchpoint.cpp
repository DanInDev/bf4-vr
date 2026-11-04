#include "control/watchpoint.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

#include "common/log.hpp"

namespace bfvr::control
{
#if !defined(_M_X64)
	std::string watch_writes(std::uintptr_t, unsigned, unsigned, bool)
	{
		return "error: watch is implemented for x64 (BF4) only";
	}
#else
	namespace
	{
		// Data breakpoints trap after the writing instruction, so RIP is the *next* instruction.
		constexpr int backtrace_depth = 6;

		struct hit
		{
			std::atomic<std::uint64_t> rip{};
			std::atomic<std::uint32_t> count{};
			std::uint64_t backtrace[backtrace_depth]{}; // written once, by the first hit's thread
		};

		// Walks the faulting thread's stack with the image's unwind tables (.pdata). A frame
		// without unwind data is treated as a leaf: its return address is at [rsp].
		void unwind(CONTEXT context, std::uint64_t (&frames)[backtrace_depth])
		{
			for (auto& frame : frames)
			{
				DWORD64 image_base = 0;
				const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
				if (function)
				{
					PVOID handler_data = nullptr;
					DWORD64 establisher = 0;
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data, &establisher, nullptr);
				}
				else if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(context.Rsp), &context.Rip, sizeof(context.Rip), nullptr))
				{
					return;
				}
				else
				{
					context.Rsp += 8;
				}
				if (!context.Rip)
				{
					return;
				}
				frame = context.Rip;
			}
		}
		constexpr int max_distinct_hits = 32;
		hit hits[max_distinct_hits];
		std::atomic<bool> armed{};

		LONG CALLBACK on_exception(EXCEPTION_POINTERS* info)
		{
			if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1))
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}
			info->ContextRecord->Dr6 = 0;
			if (!armed)
			{
				// A late trap after disarming: clear this thread's breakpoint and carry on.
				info->ContextRecord->Dr7 &= ~std::uint64_t{ 0xF0003 };
				return EXCEPTION_CONTINUE_EXECUTION;
			}

			const std::uint64_t rip = info->ContextRecord->Rip;
			for (hit& entry : hits)
			{
				std::uint64_t expected = 0;
				if (entry.rip == rip || entry.rip.compare_exchange_strong(expected, rip))
				{
					if (!entry.count++)
					{
						unwind(*info->ContextRecord, entry.backtrace);
					}
					break;
				}
			}
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		// Applies DR0/DR7 to every other thread of the process; returns how many were updated.
		int set_on_all_threads(std::uintptr_t address, std::uint64_t dr7)
		{
			const DWORD process = GetCurrentProcessId();
			const DWORD self = GetCurrentThreadId();
			const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
			if (snapshot == INVALID_HANDLE_VALUE)
			{
				return 0;
			}

			int updated = 0;
			THREADENTRY32 entry{ sizeof(entry) };
			for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
			{
				if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
				{
					continue;
				}
				const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
				if (!thread)
				{
					continue;
				}
				if (SuspendThread(thread) != static_cast<DWORD>(-1))
				{
					CONTEXT context{};
					context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
					if (GetThreadContext(thread, &context))
					{
						context.Dr0 = address;
						context.Dr7 = (context.Dr7 & ~std::uint64_t{ 0xF0003 }) | dr7;
						updated += SetThreadContext(thread, &context) ? 1 : 0;
					}
					ResumeThread(thread);
				}
				CloseHandle(thread);
			}
			CloseHandle(snapshot);
			return updated;
		}
	}

	std::string watch_writes(std::uintptr_t address, unsigned length, unsigned duration_ms, bool reads)
	{
		std::uint64_t length_bits = 0;
		switch (length)
		{
		case 1: length_bits = 0; break;
		case 2: length_bits = 1; break;
		case 4: length_bits = 3; break;
		case 8: length_bits = 2; break;
		default: return "error: length must be 1, 2, 4 or 8";
		}
		if (address % length)
		{
			return "error: address must be aligned to length";
		}

		static const PVOID handler = AddVectoredExceptionHandler(1, &on_exception);
		if (!handler)
		{
			return "error: AddVectoredExceptionHandler failed";
		}

		for (hit& entry : hits)
		{
			entry.rip = 0;
			entry.count = 0;
			std::fill(std::begin(entry.backtrace), std::end(entry.backtrace), 0);
		}

		// DR7: local enable L0 (bit 0), RW0 = 01 write or 11 read/write (bits 16-17), LEN0 (bits 18-19).
		const std::uint64_t dr7 = 1 | (std::uint64_t{ reads ? 3u : 1u } << 16) | (length_bits << 18);
		armed = true;
		const int threads = set_on_all_threads(address, dr7);
		Sleep(duration_ms);
		armed = false;
		set_on_all_threads(0, 0);
		log::write("watch: %p len=%u for %u ms on %d threads", reinterpret_cast<void*>(address), length, duration_ms, threads);

		std::string reply = "threads=" + std::to_string(threads);
		for (const hit& entry : hits)
		{
			if (!entry.rip)
			{
				break;
			}
			char line[96];
			std::snprintf(line, sizeof(line), "\nrip_after_write=0x%llx count=%u backtrace=",
				static_cast<unsigned long long>(entry.rip.load()), entry.count.load());
			reply += line;
			for (const std::uint64_t frame : entry.backtrace)
			{
				if (!frame)
				{
					break;
				}
				std::snprintf(line, sizeof(line), "0x%llx ", static_cast<unsigned long long>(frame));
				reply += line;
			}
		}
		return reply;
	}
#endif
}
