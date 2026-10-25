#include "body/arm_pose.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include <MinHook.h>

#include "common/log.hpp"

namespace bfvr::body
{
	namespace
	{
		// BF4 179547: local ClientSoldierEntity*; soldier+0x1E8 first-person ClientAntAnimatableComponent,
		// component+0x128 Animatable. The pose object handed to the conversion is Animatable - 0xD0;
		// its local pose (QuatTransform[bones]: translation+scale, rotation) is at Animatable+0x48.
		constexpr std::uint64_t local_soldier_global = 0x14230f260;
		constexpr std::uint64_t pose_conversion = 0x140716050;
		// push rbx; sub rsp,20h; xor r9d,r9d; mov rbx,rcx; cmp [rcx+16Ah],r9b
		constexpr std::uint8_t expected_prologue[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x45, 0x33, 0xc9, 0x48, 0x8b, 0xd9, 0x44, 0x38, 0x89, 0x6a, 0x01 };
		constexpr std::size_t quat_transform_size = 0x20;
		constexpr int max_bones = 256;

		using pose_conversion_t = void(__fastcall*)(void* pose);
		pose_conversion_t original_conversion{};

		std::atomic<int> research_bone{ -1 };
		std::atomic<float> research_offset[3]{};

		template <typename T>
		bool read(std::uint64_t address, T& value)
		{
			SIZE_T done = 0;
			return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		// The local soldier's first-person Animatable buffers (+0x40, +0x48 local pose, +0x50 model pose).
		bool first_person_buffers(std::uint64_t (&buffers)[3])
		{
			std::uint64_t soldier = 0, component = 0, animatable = 0;
			return read(local_soldier_global, soldier) && read(soldier + 0x1e8, component) && read(component + 0x128, animatable)
				&& read(animatable + 0x40, buffers[0]) && read(animatable + 0x48, buffers[1]) && read(animatable + 0x50, buffers[2]);
		}

		std::atomic<std::uint64_t> conversions{};
		std::atomic<std::uint64_t> matched{};
		std::atomic<bool> layout_logged{};

		// Research: recognise the local soldier's first-person pose object by any of its buffer
		// pointers in the first 0x200 bytes, log where they sit once, and edit the local pose.
		void __fastcall conversion_detour(void* pose)
		{
			++conversions;
			const int bone = research_bone.load(std::memory_order_relaxed);
			if (bone >= 0)
			{
				std::uint64_t buffers[3]{};
				std::uint64_t fields[0x40]{};
				SIZE_T done = 0;
				if (first_person_buffers(buffers) && ReadProcessMemory(GetCurrentProcess(), pose, fields, sizeof(fields), &done) && done == sizeof(fields))
				{
					bool ours = false;
					for (int i = 0; i < 0x40; ++i)
					{
						for (int b = 0; b < 3; ++b)
						{
							if (buffers[b] != 0 && fields[i] == buffers[b])
							{
								ours = true;
								if (!layout_logged)
								{
									log::write("arms: pose object %p +%#x = Animatable buffer +%#x", pose, i * 8, 0x40 + b * 8);
								}
							}
						}
					}
					if (ours)
					{
						layout_logged = true;
					}
					const std::uint64_t wanted = buffers[1];
					if (ours && wanted != 0)
					{
						++matched;
						auto* translation = reinterpret_cast<float*>(static_cast<std::uintptr_t>(wanted + bone * quat_transform_size));
						for (int axis = 0; axis < 3; ++axis)
						{
							translation[axis] += research_offset[axis].load(std::memory_order_relaxed);
						}
					}
				}
			}
			original_conversion(pose);
		}

		DWORD WINAPI wait_and_hook(LPVOID)
		{
			auto* const target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(pose_conversion));
			std::uint8_t bytes[sizeof(expected_prologue)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr) && std::memcmp(bytes, expected_prologue, sizeof(bytes)) == 0)
				{
					MH_STATUS status = MH_Initialize();
					if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
					{
						status = MH_CreateHook(target, reinterpret_cast<void*>(&conversion_detour), reinterpret_cast<void**>(&original_conversion));
						if (status == MH_OK)
						{
							status = MH_EnableHook(target);
						}
					}
					log::write("arms: pose-conversion hook at %p: %s", target, MH_StatusToString(status));
					return 0;
				}
				Sleep(200);
			}
			log::write("arms: pose conversion never matched build 179547; hook not installed");
			return 0;
		}
	}

	bool install_arm_pose_hook()
	{
#if !defined(_M_X64)
		log::write("arms: hook targets BF4 x64 only; not installed");
		return false;
#else
		const HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr);
		if (!thread)
		{
			return false;
		}
		CloseHandle(thread);
		return true;
#endif
	}

	arm_pose_stats stats()
	{
		return { conversions.load(), matched.load() };
	}

	void set_bone_offset(int bone, float dx, float dy, float dz)
	{
		research_bone = -1; // never pair a new bone with the old offset
		research_offset[0] = dx;
		research_offset[1] = dy;
		research_offset[2] = dz;
		research_bone = (bone >= 0 && bone < max_bones) ? bone : -1;
	}
}
