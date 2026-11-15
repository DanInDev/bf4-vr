// Checks the launcher's install logic, the VR setup of the BF4 profile and bfvr.ini, and bfvr.ini editing against a throwaway folder in %TEMP%
// with a fake game exe; the real game folder is never touched.
//
// Usage: bfvr_launcher_test.exe [--keep]   (--keep leaves the folder for inspection; --online only asks GitHub for the latest release)
// Exit code 0 when every check passes.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "launcher/ini_file.hpp"
#include "launcher/install.hpp"
#include "launcher/play_mode.hpp"
#include "launcher/update.hpp"
#include "launcher/vr_setup.hpp"

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
	void write_fake_exe(const fs::path& path, std::uint32_t timestamp, std::uint16_t machine = bf4_179547_machine)
	{
		std::string image(512, '\0');
		image[0] = 'M';
		image[1] = 'Z';
		const std::uint32_t pe = 0x80;
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

	// Another game's exe (BF3's x86 bf3.exe) is not taken for BF4.
	void test_other_game(const fs::path& root)
	{
		const game_install game{ root / L"othergame", L"bfvr_fake_other.exe", &bf4_profile };
		fs::create_directories(game.dir);
		write_fake_exe(game.dir / game.exe, 0x511C9356, 0x014C);
		check(check_build(game).result == build_check::mismatch, "BF3's exe is not accepted as BF4");
		write_fake_exe(game.dir / game.exe, bf4_179547_timestamp, 0x014C);
		check(check_build(game).result == build_check::mismatch, "BF4's timestamp on an x86 image is not accepted");
		check(std::wstring_view(bf4_profile.launch_link).starts_with(L"origin2://game/launch/"), "BF4 starts through the EA app link");
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

	void test_vr_setup(const fs::path& root)
	{
		const fs::path profile = root / L"settings" / L"PROFSAVE_profile";
		fs::create_directories(profile.parent_path());
		const std::string original = "GstAudio.AudioQuality 1\nGstRender.AntiAliasingPost 3\nGstRender.MantleEnabled 1\nGstRender.MotionBlur 0.000000\nGstRender.WeaponDOF 1\nGstRender.VSyncEnabled 0\n";
		write_file(profile, original);

		check(profile_pending(profile).size() == 5, "profile: post AA, Mantle, weapon DOF, motion blur enabled, vibration pending; 0.000000 counts as 0");
		check(set_profile_for_vr(profile).ok && profile_pending(profile).empty(), "profile set for VR");
		const std::string applied = read_file(profile);
		check(applied.rfind("GstAudio.AudioQuality 1\nGstRender.AntiAliasingPost 0\nGstRender.MantleEnabled 0\nGstRender.MotionBlur 0\nGstRender.WeaponDOF 0\nGstRender.VSyncEnabled 0\n", 0) == 0,
			"other lines and order kept, values replaced in place");
		check(applied.find("GstInput.Vibration 1\n") != std::string::npos && applied.find("GstRender.MotionBlurEnabled 0\n") != std::string::npos, "missing keys appended");
		check(!fs::exists(profile_backup(profile)) && !fs::exists(fs::path(profile) += L".tmp"), "no backup or temporary file of its own");
		check(!set_profile_for_vr(root / L"nowhere" / L"PROFSAVE_profile").ok, "missing profile: asks to start BF4 once");

		const fs::path seed = root / L"vr.ini.example";
		const fs::path ini_file = root / L"vrgame" / L"bfvr.ini";
		fs::create_directories(ini_file.parent_path());
		write_file(seed, "# template\r\nStereo = 0\r\nview_crop_h = 0.780\r\nresearch = 0\r\n");
		check(ini_pending(ini_file).size() == vr_ini_settings().size() - 2, "no bfvr.ini: all but theater and gamepad (on by default) pending");
		check(apply_ini(ini_file, seed).ok && ini_pending(ini_file).empty(), "mod settings set for VR");
		const ini::values values = ini::read(ini_file);
		check(values.at("research") == "0" && read_file(ini_file).rfind("# template\r\nstereo = 1\r\n", 0) == 0, "template kept, keys case-insensitive");

		check(manifest_name("{\"file_format_version\": \"1.0.0\", \"runtime\": {\"name\": \"SteamVR\", \"library_path\": \"x.dll\"}}") == "SteamVR",
			"runtime manifest name");
		check(manifest_name("{}").empty(), "manifest without a name");
	}

	void test_play_mode(const fs::path& root)
	{
		const fs::path profile = root / L"modes" / L"PROFSAVE_profile";
		const fs::path progress = root / L"modes" / L"PROFSAVE";
		fs::create_directories(profile.parent_path());
		const std::string normal = "GstRender.AntiAliasingPost 3\nGstRender.VSyncEnabled 1\nGstInput.MouseSensitivity 0.3\n";
		write_file(profile, normal);
		write_file(progress, "campaign progress");

		check(current_mode(profile, false) == play_mode::normal && current_mode(profile, true) == play_mode::vr, "mode: no marker, the mod decides");
		check(current_mode(fs::path(), true) == play_mode::vr, "mode: no profile path, the mod decides");
		check(!switch_profile(profile, play_mode::normal, play_mode::vr, true).ok && read_file(profile) == normal && !fs::exists(mode_marker(profile)),
			"mode: nothing switched while the game runs");
		check(!switch_profile(root / L"nowhere" / L"PROFSAVE_profile", play_mode::normal, play_mode::vr, false).ok, "mode: missing profile refused");

		check(switch_profile(profile, play_mode::normal, play_mode::vr, false).ok, "mode: first switch to VR");
		check(read_file(mode_copy(profile, play_mode::normal)) == normal, "mode: the normal settings are kept");
		check(profile_pending(profile).empty() && read_file(profile).find("GstInput.MouseSensitivity 0.3\n") != std::string::npos,
			"mode: VR values set on top of the normal settings");
		check(current_mode(profile, false) == play_mode::vr, "mode: marker says VR, whatever the mod");

		// The user changes settings in VR (in the game or in the file), then goes back to normal.
		const std::string vr = read_file(profile) + "GstAudio.Volume 0.5\n";
		write_file(profile, vr);
		check(switch_profile(profile, play_mode::vr, play_mode::normal, false).ok, "mode: switch to normal");
		check(read_file(profile) == normal && read_file(mode_copy(profile, play_mode::vr)) == vr, "mode: normal settings back, VR ones kept with the change");
		check(current_mode(profile, true) == play_mode::normal, "mode: marker says normal");

		// A normal-mode change survives the next round trip; the VR copy comes back as it was.
		const std::string normal2 = normal + "GstRender.FieldOfView 90\n";
		write_file(profile, normal2);
		check(switch_profile(profile, play_mode::normal, play_mode::vr, false).ok && read_file(profile) == vr, "mode: VR copy comes back unchanged");
		check(read_file(mode_copy(profile, play_mode::normal)) == normal2, "mode: the changed normal settings are kept");
		check(switch_profile(profile, play_mode::vr, play_mode::normal, false).ok && read_file(profile) == normal2, "mode: changed normal settings back");

		check(switch_profile(profile, play_mode::normal, play_mode::normal, false).ok && read_file(profile) == normal2, "mode: same mode changes nothing");
		check(switch_profile(profile, play_mode::normal, play_mode::vr, false, true).ok && profile_pending(profile).empty(),
			"mode: Set up for VR sets the VR values on the VR copy");
		check(read_file(progress) == "campaign progress", "mode: campaign progress untouched");
		check(!fs::exists(fs::path(profile) += L".tmp") && !fs::exists(fs::path(mode_copy(profile, play_mode::vr)) += L".tmp"), "mode: no temporary files left");

		// An install that an older launcher set up: no copies, but its backup of the original.
		const fs::path old = root / L"oldsetup" / L"PROFSAVE_profile";
		fs::create_directories(old.parent_path());
		write_file(old, "vr settings\n");
		write_file(profile_backup(old), normal);
		check(switch_profile(old, current_mode(old, true), play_mode::normal, false).ok && read_file(old) == normal, "mode: normal from an older backup");
		check(read_file(mode_copy(old, play_mode::vr)) == "vr settings\n" && fs::exists(profile_backup(old)), "mode: VR kept, the old backup left alone");

		// Never set up and no backup: going normal keeps the current settings.
		const fs::path bare = root / L"bare" / L"PROFSAVE_profile";
		fs::create_directories(bare.parent_path());
		write_file(bare, "tuned for vr\n");
		check(switch_profile(bare, play_mode::vr, play_mode::normal, false).ok && read_file(bare) == "tuned for vr\n", "mode: no normal copy, the profile stays");
		check(current_mode(bare, true) == play_mode::normal, "mode: still marked normal");
	}
}

namespace
{
	void test_update()
	{
		const release latest = parse_release(R"({"url":"https://api.github.com/x","html_url":"https://github.com/DanInDev/bf4-vr/releases/tag/v0.2.0",)"
			R"( "tag_name" : "v0.2.0", "name":"bf-vr 0.2.0 \"hands\""})");
		check(latest.ok && latest.tag == "v0.2.0", "update: tag_name read");
		check(latest.url == "https://github.com/DanInDev/bf4-vr/releases/tag/v0.2.0", "update: html_url read (not url)");
		check(!parse_release(R"({"message":"Not Found"})").ok, "update: an answer without a tag is no release");
		check(compare_versions("v0.2.0", "0.1.0") > 0 && compare_versions("0.1.0", "v0.2.0") < 0, "update: newer and older");
		check(compare_versions("v0.1", "0.1.0") == 0 && compare_versions("1.10.0", "1.9.3") > 0, "update: missing parts are 0, parts compare as numbers");
		check(!mod_version().empty() && compare_versions(mod_version(), "0.0.1") >= 0, "update: this build has a version");
	}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc > 1 && _wcsicmp(argv[1], L"--online") == 0)
	{
		// Not part of ctest: asks GitHub as the launcher's "Updates?" button does.
		const release latest = latest_release();
		std::printf("version %s; latest release: %s %s %s\n", std::string(mod_version()).c_str(), latest.ok ? latest.tag.c_str() : "-",
			latest.url.c_str(), latest.error.c_str());
		return latest.ok || latest.error == "no release published yet" ? 0 : 1;
	}
	test_update();
	const bool keep = argc > 1 && _wcsicmp(argv[1], L"--keep") == 0;
	const fs::path root = fs::temp_directory_path() / (L"bfvr_launcher_test_" + std::to_wstring(GetCurrentProcessId()));
	fs::create_directories(root);

	test_hash(root);
	test_install(root);
	test_other_game(root);
	test_ini(root);
	test_vr_setup(root);
	test_play_mode(root);

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
