#include "launcher/install.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bfvr::launcher
{
	namespace
	{
		std::filesystem::path proxy_path(const game_install& game) { return game.dir / L"dinput8.dll"; }
		std::filesystem::path marker_path(const game_install& game) { return game.dir / L"bfvr.deployed.json"; }

		std::string read_text(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			std::stringstream contents;
			contents << file.rdbuf();
			return contents.str();
		}

		// Enough JSON for the marker: the string value of a key.
		std::string json_string(const std::string& text, const char* key)
		{
			const auto name = text.find('"' + std::string(key) + '"');
			const auto colon = name == std::string::npos ? name : text.find(':', name);
			const auto open = colon == std::string::npos ? colon : text.find('"', colon);
			if (open == std::string::npos)
			{
				return {};
			}

			std::string value;
			for (auto i = open + 1; i < text.size() && text[i] != '"'; ++i)
			{
				if (text[i] == '\\' && i + 1 < text.size())
				{
					++i;
				}
				value += text[i];
			}
			return value;
		}

		std::string json_escape(const std::string& text)
		{
			std::string escaped;
			for (const char c : text)
			{
				if (c == '"' || c == '\\')
				{
					escaped += '\\';
				}
				escaped += c;
			}
			return escaped;
		}

		std::string to_utf8(const std::wstring& text)
		{
			if (text.empty())
			{
				return {};
			}
			const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string utf8(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
			return utf8;
		}

		std::wstring system_error(DWORD code)
		{
			wchar_t* buffer{};
			const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
			std::wstring text = length ? std::wstring(buffer, length) : L"error " + std::to_wstring(code);
			LocalFree(buffer);
			while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L'.'))
			{
				text.pop_back();
			}
			return text;
		}

		std::string local_timestamp()
		{
			SYSTEMTIME now{};
			GetLocalTime(&now);
			char text[32]{};
			std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u",
				now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
			return text;
		}

		bool write_marker(const game_install& game, const std::filesystem::path& source, const std::string& hash, const char* config)
		{
			// Same fields as tools/deploy.ps1, so either tool can update or remove the other's install.
			std::ofstream file(marker_path(game), std::ios::binary | std::ios::trunc);
			file << "{\r\n"
				<< "    \"game\":  \"" << game.profile->id << "\",\r\n"
				<< "    \"config\":  \"" << json_escape(config) << "\",\r\n"
				<< "    \"source\":  \"" << json_escape(to_utf8(source.wstring())) << "\",\r\n"
				<< "    \"sha256\":  \"" << hash << "\",\r\n"
				<< "    \"deployed\":  \"" << local_timestamp() << "\"\r\n"
				<< "}\r\n";
			return static_cast<bool>(file.flush());
		}
	}

	std::filesystem::path find_install(const game_profile& profile)
	{
		wchar_t value[MAX_PATH]{};
		DWORD size = sizeof(value);
		if (RegGetValueW(HKEY_LOCAL_MACHINE, profile.registry_key, L"Install Dir",
			RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
		{
			return {};
		}

		std::wstring dir = value;
		while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
		{
			dir.pop_back();
		}
		return dir;
	}

	build_info check_build(const game_install& game)
	{
		build_info info{};
		std::ifstream file(game.dir / game.exe, std::ios::binary);
		if (!file)
		{
			info.result = build_check::missing;
			return info;
		}

		std::array<char, 4096> header{};
		file.read(header.data(), header.size());
		const auto length = static_cast<std::size_t>(file.gcount());
		const auto read = [&](std::size_t offset, auto& value) { std::memcpy(&value, header.data() + offset, sizeof(value)); };

		std::uint32_t pe{};
		if (length >= 0x40 && header[0] == 'M' && header[1] == 'Z')
		{
			read(0x3C, pe);
		}
		if (pe == 0 || pe > length - 12 || std::memcmp(header.data() + pe, "PE\0\0", 4) != 0)
		{
			info.result = build_check::unreadable;
			return info;
		}

		read(pe + 4, info.machine);
		read(pe + 8, info.timestamp);
		info.result = info.machine == game.profile->machine && info.timestamp == game.profile->timestamp ? build_check::ok : build_check::mismatch;
		return info;
	}

	std::string sha256_file(const std::filesystem::path& path)
	{
		const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return {};
		}

		BCRYPT_ALG_HANDLE algorithm{};
		BCRYPT_HASH_HANDLE hash{};
		std::array<UCHAR, 32> digest{};
		bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
			&& BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));

		std::array<UCHAR, 64 * 1024> buffer{};
		DWORD length{};
		while (ok && ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &length, nullptr) && length > 0)
		{
			ok = BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), length, 0));
		}
		ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0));

		if (hash)
		{
			BCryptDestroyHash(hash);
		}
		if (algorithm)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
		}
		CloseHandle(file);
		if (!ok)
		{
			return {};
		}

		static constexpr char digits[] = "0123456789ABCDEF";
		std::string hex;
		for (const UCHAR byte : digest)
		{
			hex += digits[byte >> 4];
			hex += digits[byte & 0xF];
		}
		return hex;
	}

	proxy_status read_proxy(const game_install& game)
	{
		std::error_code error;
		if (!std::filesystem::exists(proxy_path(game), error))
		{
			return { proxy_state::absent };
		}

		const std::string marker = read_text(marker_path(game));
		const std::string recorded = json_string(marker, "sha256");
		const std::string actual = sha256_file(proxy_path(game));
		if (recorded.empty() || actual.empty() || _stricmp(recorded.c_str(), actual.c_str()) != 0)
		{
			return { proxy_state::foreign };
		}
		return { proxy_state::ours, json_string(marker, "config"), json_string(marker, "deployed") };
	}

	bool game_running(const game_install& game)
	{
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		PROCESSENTRY32W entry{ sizeof(entry) };
		bool found = false;
		for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry))
		{
			found = _wcsicmp(entry.szExeFile, game.exe.c_str()) == 0;
		}
		CloseHandle(snapshot);
		return found;
	}

	result deploy(const game_install& game, const std::filesystem::path& dll, const char* config)
	{
		const build_info build = check_build(game);
		if (build.result != build_check::ok)
		{
			return { false, L"Not installed: " + (game.dir / game.exe).wstring() + L" is not " + game.profile->name + L" build " + game.profile->build + L", the only build the mod supports." };
		}
		if (game_running(game))
		{
			return { false, L"Not installed: " + game.exe + L" is running. Close it first so the next start loads the new DLL." };
		}
		std::error_code error;
		if (!std::filesystem::exists(dll, error))
		{
			return { false, L"Not installed: the mod's dinput8.dll is missing (" + dll.wstring() + L")." };
		}
		if (read_proxy(game).state == proxy_state::foreign)
		{
			return { false, L"Not installed: " + proxy_path(game).wstring() + L" belongs to another mod (a proxy or ReShade?). Leaving it untouched." };
		}

		const auto target = proxy_path(game);
		if (!CopyFileW(dll.c_str(), target.c_str(), FALSE))
		{
			return { false, L"Copying dinput8.dll failed: " + system_error(GetLastError()) + L"." };
		}

		// Without a marker nothing could tell the DLL is ours, so it must not stay behind.
		const std::string hash = sha256_file(target);
		if (hash.empty() || !write_marker(game, dll, hash, config))
		{
			DeleteFileW(target.c_str());
			return { false, L"Writing " + marker_path(game).wstring() + L" failed; the DLL was taken out again." };
		}
		return { true, L"Mod installed. Remove it before any multiplayer game." };
	}

	result remove(const game_install& game)
	{
		if (game_running(game))
		{
			return { false, L"Not removed: " + game.exe + L" is running. Close it first." };
		}

		const proxy_status proxy = read_proxy(game);
		if (proxy.state == proxy_state::foreign)
		{
			return { false, L"Not removed: " + proxy_path(game).wstring() + L" was not installed by bf-vr. Leaving it untouched." };
		}
		if (proxy.state == proxy_state::ours && !DeleteFileW(proxy_path(game).c_str()))
		{
			return { false, L"Deleting dinput8.dll failed: " + system_error(GetLastError()) + L"." };
		}

		std::error_code error;
		std::filesystem::remove(marker_path(game), error);
		return { true, proxy.state == proxy_state::ours ? L"Mod removed; the game folder is back to normal." : L"No mod was installed." };
	}
}
