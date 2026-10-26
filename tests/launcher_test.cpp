// Checks the launcher's install logic and bfvr.ini editing against a throwaway folder in %TEMP%
// with a fake game exe; the real game folder is never touched.
//
// Usage: bfvr_launcher_test.exe [--keep]   (--keep leaves the folder for inspection)
// Exit code 0 when every check passes.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "launcher/ini_file.hpp"
#include "launcher/install.hpp"

namespace
{
	namespace fs = std::filesystem;
	using namespace bfvr::launcher;

	int failures{};

	void check(bool condition, const char* what)
	{
		std::printf("%s  %s\n", condition ? "ok  " : "FAIL", what);
		failures += condition ? 0 : 1;
	}

	void write_file(const fs::path& path, const std::string& contents)
	{
		std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
	}

	std::string read_file(const fs::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::stringstream contents;
		contents << file.rdbuf();
		return contents.str();
	}

	// Just the headers check_build reads: MZ, e_lfanew, PE signature, machine and timestamp.
	void write_fake_exe(const fs::path& path, std::uint32_t timestamp)
	{
		std::string image(512, '\0');
		image[0] = 'M';
		image[1] = 'Z';
		const std::uint32_t pe = 0x80;
		const std::uint16_t machine = bf4_179547_machine;
		std::memcpy(image.data() + 0x3C, &pe, sizeof(pe));
		std::memcpy(image.data() + pe, "PE\0\0", 4);
		std::memcpy(image.data() + pe + 4, &machine, sizeof(machine));
		std::memcpy(image.data() + pe + 8, &timestamp, sizeof(timestamp));
		write_file(path, image);
	}

	void test_install(const fs::path& root)
	{
		const game_install game{ root / L"game", L"bfvr_fake_game.exe" };
		const fs::path dll = root / L"build" / L"dinput8.dll";
		const fs::path target = game.dir / L"dinput8.dll";
		const fs::path marker = game.dir / L"bfvr.deployed.json";
		fs::create_directories(game.dir);
		fs::create_directories(dll.parent_path());
		write_file(dll, "bf-vr proxy v1");

		check(check_build(game).result == build_check::missing, "missing exe is reported");
		write_fake_exe(game.dir / game.exe, 0x12345678);
		check(check_build(game).result == build_check::mismatch, "other build is a mismatch");
		check(!deploy(game, dll, "Test").ok && !fs::exists(target), "deploy refused on another build");

		write_fake_exe(game.dir / game.exe, bf4_179547_timestamp);
		check(check_build(game).result == build_check::ok, "build 179547 recognised");
		check(read_proxy(game).state == proxy_state::absent, "no proxy at first");

		check(deploy(game, dll, "Test").ok, "deploy");
		const proxy_status deployed = read_proxy(game);
		check(deployed.state == proxy_state::ours && deployed.config == "Test" && !deployed.deployed.empty(), "deployed proxy is ours");
		check(read_file(target) == "bf-vr proxy v1", "deployed bytes match");

		write_file(dll, "bf-vr proxy v2");
		check(deploy(game, dll, "Test").ok && read_file(target) == "bf-vr proxy v2", "redeploy replaces our own proxy");

		check(remove(game).ok && !fs::exists(target) && !fs::exists(marker), "remove deletes proxy and marker");
		check(remove(game).ok, "remove with nothing installed succeeds");

		write_file(target, "someone else's proxy");
		check(read_proxy(game).state == proxy_state::foreign, "unmarked dinput8.dll is foreign");
		check(!deploy(game, dll, "Test").ok && read_file(target) == "someone else's proxy", "deploy leaves a foreign proxy alone");
		check(!remove(game).ok && fs::exists(target), "remove leaves a foreign proxy alone");

		// Marker as tools/deploy.ps1 writes it (PowerShell 5.1: UTF-8 BOM, ConvertTo-Json layout).
		write_file(dll, "bf-vr proxy v3");
		fs::copy_file(dll, target, fs::copy_options::overwrite_existing);
		write_file(marker, "\xEF\xBB\xBF{\r\n    \"game\":  \"bf4\",\r\n    \"config\":  \"Debug\",\r\n    \"source\":  \"C:\\\\x\\\\dinput8.dll\",\r\n"
			"    \"sha256\":  \"" + sha256_file(target) + "\",\r\n    \"deployed\":  \"2026-10-25T01:00:00\"\r\n}\r\n");
		const proxy_status scripted = read_proxy(game);
		check(scripted.state == proxy_state::ours && scripted.config == "Debug", "deploy.ps1 marker is understood");
		check(remove(game).ok && !fs::exists(target), "remove after deploy.ps1");

		check(deploy(game, dll, "Test").ok, "deploy again (marker left for --keep inspection)");
	}

	void test_hash(const fs::path& root)
	{
		write_file(root / L"abc.txt", "abc");
		check(sha256_file(root / L"abc.txt") == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "SHA-256 test vector");
		check(sha256_file(root / L"absent.bin").empty(), "SHA-256 of a missing file is empty");
	}

	void test_ini(const fs::path& root)
	{
		const fs::path seed = root / L"bfvr.ini.example";
		const fs::path file = root / L"game" / L"bfvr.ini";
		write_file(seed, "# template\r\ntheater = 1\r\n\r\n# runtime\r\nxr_runtime = auto\r\nStereo = 0 # experimental\r\ncontrol_pipe = 1\r\n");

		check(ini::write(file, { { "stereo", "1" }, { "xr_runtime", "steamvr" }, { "gamepad", "0" } }, seed), "write seeds from the template");
		check(read_file(file) == "# template\r\ntheater = 1\r\n\r\n# runtime\r\nxr_runtime = steamvr\r\nstereo = 1 # experimental\r\ncontrol_pipe = 1\r\ngamepad = 0\r\n",
			"comments, order and other keys kept; new key appended");

		const ini::values values = ini::read(file);
		check(values.at("stereo") == "1" && values.at("xr_runtime") == "steamvr" && values.at("control_pipe") == "1", "read back");
		check(!ini::get_bool(values, "gamepad", true) && ini::get_bool(values, "theater", false) && ini::get_bool(values, "absent", true), "booleans and fallbacks");

		write_file(file, "stereo = 0\r\nstereo = 0\r\n");
		check(ini::write(file, { { "stereo", "1" } }, seed) && read_file(file) == "stereo = 1\r\nstereo = 1\r\n", "every duplicate is updated");
		check(!fs::exists(fs::path(file) += L".tmp"), "no temporary file left");
	}
}

int wmain(int argc, wchar_t** argv)
{
	const bool keep = argc > 1 && _wcsicmp(argv[1], L"--keep") == 0;
	const fs::path root = fs::temp_directory_path() / (L"bfvr_launcher_test_" + std::to_wstring(GetCurrentProcessId()));
	fs::create_directories(root);

	test_hash(root);
	test_install(root);
	test_ini(root);

	if (keep)
	{
		std::printf("kept: %ls\n", root.c_str());
	}
	else
	{
		std::error_code error;
		fs::remove_all(root, error);
	}
	std::printf("%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
