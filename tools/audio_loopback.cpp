// Records what the default playback device plays (WASAPI loopback) for sound research on the game
// (re/notes/sound.md): a mono float WAV whose first sample is at the Unix time written beside it.
//
//   bfvr_audio_loopback <seconds> <out.wav>      writes out.wav and out.wav.start (Unix time, ms)
//
// Loopback delivers nothing while the device is silent; packets are placed by their timestamps and
// the gaps stay zero, so the timeline matches the wall clock.
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace
{
	template <typename T>
	struct com_ptr
	{
		T* p{};
		~com_ptr()
		{
			if (p)
			{
				p->Release();
			}
		}
		T** operator&() { return &p; }
		T* operator->() const { return p; }
	};

	double unix_ms_now()
	{
		FILETIME now{};
		GetSystemTimePreciseAsFileTime(&now);
		const std::uint64_t ticks = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
		return static_cast<double>(ticks - 116444736000000000ull) / 10000.0;
	}

	bool write_wav(const char* path, const std::vector<float>& samples, std::uint32_t rate)
	{
		std::ofstream out(path, std::ios::binary);
		const std::uint32_t data = static_cast<std::uint32_t>(samples.size() * sizeof(float));
		const auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
		const auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
		out.write("RIFF", 4);
		u32(36 + data);
		out.write("WAVEfmt ", 8);
		u32(16);
		u16(3); // IEEE float
		u16(1);
		u32(rate);
		u32(rate * 4);
		u16(4);
		u16(32);
		out.write("data", 4);
		u32(data);
		out.write(reinterpret_cast<const char*>(samples.data()), data);
		return static_cast<bool>(out);
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::printf("usage: bfvr_audio_loopback <seconds> <out.wav>\n");
		return 2;
	}
	const double seconds = std::atof(argv[1]);
	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
	{
		return 1;
	}
	com_ptr<IMMDeviceEnumerator> devices;
	com_ptr<IMMDevice> device;
	com_ptr<IAudioClient> client;
	com_ptr<IAudioCaptureClient> capture;
	WAVEFORMATEX* format{};
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&devices)))
		|| FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device))
		|| FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client)))
		|| FAILED(client->GetMixFormat(&format))
		|| FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, format, nullptr))
		|| FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture))))
	{
		std::printf("error: could not open loopback capture on the default playback device\n");
		return 1;
	}
	const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
	const bool is_float = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
		|| (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && extensible->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT);
	if (!is_float || format->wBitsPerSample != 32)
	{
		std::printf("error: mix format is not 32-bit float (tag %u, %u bits)\n", format->wFormatTag, format->wBitsPerSample);
		return 1;
	}
	const std::uint32_t rate = format->nSamplesPerSec;
	const int channels = format->nChannels;

	LARGE_INTEGER frequency{}, start_qpc{};
	QueryPerformanceFrequency(&frequency);
	std::vector<float> samples(static_cast<std::size_t>(seconds * rate) + rate, 0.0f);
	client->Start();
	QueryPerformanceCounter(&start_qpc);
	const double start_unix = unix_ms_now();
	std::size_t furthest = 0;
	while (true)
	{
		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);
		if (static_cast<double>(now.QuadPart - start_qpc.QuadPart) / static_cast<double>(frequency.QuadPart) >= seconds)
		{
			break;
		}
		Sleep(5);
		UINT32 packet = 0;
		while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0)
		{
			BYTE* data{};
			UINT32 frames{};
			DWORD flags{};
			UINT64 qpc_position{}; // 100 ns units of the performance counter
			if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, &qpc_position)))
			{
				break;
			}
			const double at = static_cast<double>(qpc_position) * 1e-7 - static_cast<double>(start_qpc.QuadPart) / static_cast<double>(frequency.QuadPart);
			const long long first = static_cast<long long>(std::llround(at * rate));
			const auto* frame_data = reinterpret_cast<const float*>(data);
			for (UINT32 i = 0; i < frames; ++i)
			{
				const long long index = first + i;
				if (index < 0 || index >= static_cast<long long>(samples.size()))
				{
					continue;
				}
				float mono = 0;
				if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
				{
					for (int c = 0; c < channels; ++c)
					{
						mono += frame_data[i * channels + c];
					}
					mono /= static_cast<float>(channels);
				}
				samples[static_cast<std::size_t>(index)] = mono;
				furthest = std::max(furthest, static_cast<std::size_t>(index) + 1);
			}
			capture->ReleaseBuffer(frames);
		}
	}
	client->Stop();
	samples.resize(std::max<std::size_t>(furthest, static_cast<std::size_t>(seconds * rate)));
	CoTaskMemFree(format);

	char start_path[MAX_PATH];
	std::snprintf(start_path, sizeof(start_path), "%s.start", argv[2]);
	std::ofstream(start_path) << std::fixed << start_unix << "\n";
	const bool written = write_wav(argv[2], samples, rate);
	std::printf("%s: %.1f s at %u Hz, %d channels mixed to mono, start %.0f\n", written ? "written" : "failed", static_cast<double>(samples.size()) / rate, rate,
		channels, start_unix);
	CoUninitialize();
	return written ? 0 : 1;
}
