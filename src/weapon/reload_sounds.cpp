#include "weapon/reload_sounds.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "common/config.hpp"

namespace bfvr::weapon
{
	namespace
	{
		constexpr int rate = 44100;
		constexpr float pi = 3.14159265f;

		std::atomic<bool> enabled{ true };
		std::once_flag built;
		constexpr int cue_count = static_cast<int>(cue::count);
		std::vector<std::uint8_t> waves[cue_count];
		std::wstring files[cue_count];

		const char* name_of(cue which)
		{
			switch (which)
			{
			case cue::cloth: return "cloth";
			case cue::magazine_in: return "magazine_in";
			case cue::magazine_out: return "magazine_out";
			case cue::chamber: return "chamber";
			case cue::pin: return "pin";
			case cue::bolt_back: return "bolt_back";
			case cue::bolt_home: return "bolt_home";
			case cue::count: break;
			}
			return "cloth";
		}

		// Deterministic noise in [-1, 1].
		float noise(std::uint32_t& state)
		{
			state = state * 1664525u + 1013904223u;
			return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
		}

		// A sharp metallic tick: decaying partials plus a noise transient.
		void tick(std::vector<float>& out, float at, float pitch, float gain, std::uint32_t seed)
		{
			const int start = static_cast<int>(at * rate);
			const int length = rate / 25; // 40 ms
			for (int i = 0; i < length && start + i < static_cast<int>(out.size()); ++i)
			{
				const float t = static_cast<float>(i) / rate;
				const float envelope = std::exp(-t * 180.0f);
				const float tone = std::sin(2.0f * pi * pitch * t) + 0.6f * std::sin(2.0f * pi * pitch * 2.7f * t);
				out[start + i] += gain * envelope * (0.6f * tone + 0.8f * noise(seed) * std::exp(-t * 600.0f));
			}
		}

		// Rustling: low-passed noise under a slow envelope.
		void rustle(std::vector<float>& out, float at, float seconds, float gain, float smoothing, std::uint32_t seed)
		{
			const int start = static_cast<int>(at * rate);
			const int length = static_cast<int>(seconds * rate);
			float low = 0.0f;
			for (int i = 0; i < length && start + i < static_cast<int>(out.size()); ++i)
			{
				const float t = static_cast<float>(i) / length;
				const float envelope = std::sin(pi * t) * (0.7f + 0.3f * std::sin(2.0f * pi * 7.0f * t));
				low += smoothing * (noise(seed) - low);
				out[start + i] += gain * envelope * low;
			}
		}

		std::vector<std::uint8_t> to_wave(const std::vector<float>& samples)
		{
			const std::uint32_t data_bytes = static_cast<std::uint32_t>(samples.size() * 2);
			std::vector<std::uint8_t> wave(44 + data_bytes);
			auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(&wave[at], &v, 4); };
			auto put16 = [&](std::size_t at, std::uint16_t v) { std::memcpy(&wave[at], &v, 2); };
			std::memcpy(&wave[0], "RIFF", 4);
			put32(4, 36 + data_bytes);
			std::memcpy(&wave[8], "WAVEfmt ", 8);
			put32(16, 16);
			put16(20, 1); // PCM
			put16(22, 1); // mono
			put32(24, rate);
			put32(28, rate * 2);
			put16(32, 2);
			put16(34, 16);
			std::memcpy(&wave[36], "data", 4);
			put32(40, data_bytes);
			for (std::size_t i = 0; i < samples.size(); ++i)
			{
				const float s = std::fmax(-1.0f, std::fmin(1.0f, samples[i]));
				const auto v = static_cast<std::int16_t>(std::lround(s * 32000.0f));
				std::memcpy(&wave[44 + i * 2], &v, 2);
			}
			return wave;
		}

		void build()
		{
			std::vector<float> cloth(static_cast<std::size_t>(rate * 0.32f));
			rustle(cloth, 0.0f, 0.30f, 0.9f, 0.08f, 11);
			std::vector<float> in(static_cast<std::size_t>(rate * 0.16f));
			tick(in, 0.0f, 1400.0f, 0.55f, 21);
			tick(in, 0.07f, 2600.0f, 0.75f, 22);
			std::vector<float> out(static_cast<std::size_t>(rate * 0.10f));
			tick(out, 0.0f, 1900.0f, 0.6f, 31);
			std::vector<float> chamber(static_cast<std::size_t>(rate * 0.34f));
			tick(chamber, 0.0f, 1700.0f, 0.5f, 41);
			rustle(chamber, 0.02f, 0.16f, 0.35f, 0.6f, 42); // the bolt sliding back
			tick(chamber, 0.22f, 2300.0f, 0.85f, 43);       // and slamming home
			std::vector<float> pin(static_cast<std::size_t>(rate * 0.25f));
			for (std::size_t i = 0; i < pin.size(); ++i)
			{
				const float t = static_cast<float>(i) / rate;
				pin[i] = 0.4f * std::exp(-t * 18.0f) * std::sin(2.0f * pi * 3800.0f * t);
			}
			tick(pin, 0.0f, 3000.0f, 0.4f, 51);
			// The rack in two parts, for a stroke held at the rear: the slide back and its stop, then home.
			std::vector<float> back(static_cast<std::size_t>(rate * 0.20f));
			tick(back, 0.0f, 1700.0f, 0.4f, 41);
			rustle(back, 0.01f, 0.12f, 0.35f, 0.6f, 42);
			tick(back, 0.12f, 1500.0f, 0.5f, 44);
			std::vector<float> home(static_cast<std::size_t>(rate * 0.12f));
			tick(home, 0.0f, 2300.0f, 0.85f, 43);
			const std::vector<float>* sources[cue_count] = { &cloth, &in, &out, &chamber, &pin, &back, &home };
			const std::filesystem::path folder = std::filesystem::path(config::ini_path()).parent_path() / "bfvr_sounds";
			for (int i = 0; i < cue_count; ++i)
			{
				waves[i] = to_wave(*sources[i]);
				const std::filesystem::path file = folder / (std::string(name_of(static_cast<cue>(i))) + ".wav");
				std::error_code error;
				if (std::filesystem::exists(file, error))
				{
					files[i] = file.wstring();
				}
			}
		}
	}

	void set_sounds(bool on)
	{
		enabled = on;
	}

	void play(cue which)
	{
		if (!enabled.load(std::memory_order_relaxed))
		{
			return;
		}
		std::call_once(built, build);
		const int index = static_cast<int>(which);
		if (index < 0 || index >= cue_count)
		{
			return;
		}
		if (!files[index].empty())
		{
			PlaySoundW(files[index].c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
			return;
		}
		PlaySoundW(reinterpret_cast<LPCWSTR>(waves[index].data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
	}
}
