#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace bfvr::launcher
{
	// PE header of bf4.exe build 179547 (x64, linked 2017-02-20; docs/FINDINGS.md).
	inline constexpr std::uint32_t bf4_179547_timestamp = 0x58AB1CC0;
	inline constexpr std::uint16_t bf4_179547_machine = 0x8664;

	struct game_install
	{
		std::filesystem::path dir;
		std::wstring exe = L"bf4.exe";
	};

	// "Install Dir" under HKLM\SOFTWARE\WOW6432Node\EA Games\Battlefield 4; empty when absent.
	std::filesystem::path find_install();

	enum class build_check { ok, missing, unreadable, mismatch };
	struct build_info
	{
		build_check result{};
		std::uint32_t timestamp{};
		std::uint16_t machine{};
	};
	build_info check_build(const game_install& game);

	// The proxy is ours only while bfvr.deployed.json records its current SHA-256 (as tools/deploy.ps1 does).
	enum class proxy_state { absent, ours, foreign };
	struct proxy_status
	{
		proxy_state state{};
		std::string config;
		std::string deployed;
	};
	proxy_status read_proxy(const game_install& game);

	bool game_running(const game_install& game);

	struct result
	{
		bool ok{};
		std::wstring message;
	};

	// Refuses on another build, while the game runs, or when a dinput8.dll that is not ours is present.
	result deploy(const game_install& game, const std::filesystem::path& dll, const char* config);
	result remove(const game_install& game);

	// Upper-case hex like PowerShell's Get-FileHash; empty when the file cannot be read.
	std::string sha256_file(const std::filesystem::path& file);
}
