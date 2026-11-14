#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace bfvr::launcher
{
	// PE header of bf4.exe build 179547 (x64, linked 2017-02-20; docs/FINDINGS.md).
	inline constexpr std::uint32_t bf4_179547_timestamp = 0x58AB1CC0;
	inline constexpr std::uint16_t bf4_179547_machine = 0x8664;

	// The game the launcher installs the mod into (Battlefield 4 only since 2026-11-14; the BF3 research
	// build lives in ..\bf3-vr).
	struct game_profile
	{
		const char* id; // "bf4", as in bfvr.deployed.json and tools/deploy.ps1
		const wchar_t* name; // "Battlefield 4"
		const wchar_t* brand; // "BF4"
		const wchar_t* exe;
		const wchar_t* registry_key; // under HKLM, holds "Install Dir"
		const wchar_t* build; // the one supported build, as shown to the user
		std::uint32_t timestamp;
		std::uint16_t machine;
		const wchar_t* launch_link; // the EA app link the Start button opens
		const wchar_t* status; // one line under the header: what the mod does
		const wchar_t* warning; // the multiplayer warning on the Setup page
	};

	// The content IDs bf4.exe itself hands to the EA app (tools/game.ps1); the Play button's offer
	// key is rejected over this link as "EntitlementNotFound".
	inline constexpr game_profile bf4_profile{
		"bf4", L"Battlefield 4", L"BF4", L"bf4.exe", L"SOFTWARE\\WOW6432Node\\EA Games\\Battlefield 4", L"179547",
		bf4_179547_timestamp, bf4_179547_machine,
		L"origin2://game/launch/?offerIds=1007968,1011575,1011576,1011577,1010268,1010269,1010270,"
		L"1010271,1010958,1010959,1010960,1010961,1007077,1016751,1016757,1016754,1015365,1015364,1015363,1015362",
		L"Battlefield 4 single-player campaign in VR  \x00B7  Quest 3 through Steam Link",
		L"Campaign only. Remove the mod before joining any multiplayer server: PunkBuster bans for foreign DLLs.",
	};

	struct game_install
	{
		std::filesystem::path dir;
		std::wstring exe = L"bf4.exe";
		const game_profile* profile = &bf4_profile;
	};

	// The profile's "Install Dir" from the registry; empty when absent.
	std::filesystem::path find_install(const game_profile& profile = bf4_profile);

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
