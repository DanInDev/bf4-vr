#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "body/finger_pose.hpp"

// The pose editor (panel page Skeleton): hand-made finger corrections per grip, in degrees, added on top
// of whatever that grip draws (shaped curls, the game's captured grips, fingers closed by contact), so
// what you tune in the headset is what you get. Kept in bfvr_poses.ini beside bfvr.ini; the built-in
// defaults are the same text (pose_tweaks.cpp). Engine-free; tested by tests/pose_tweaks_test.cpp.
namespace bfvr::body::poses
{
	enum class grip
	{
		relaxed,     // a free hand (the controller's squeeze and trigger)
		reach,       // open, about to grab
		fist,        // a magazine or grenade in the fist
		cup,         // a magazine standing on the upturned palm
		palm,        // palming: the magazine held aside, fingers closed by contact (#40)
		magazine_ak, // the game's own grip on an AK-pattern magazine
		magazine_ar, // the same on an AR-pattern magazine
		hook,        // the charging handle in the index's curl
		pinch,       // the charging handle between thumb and index (the game's AK-12 rack)
		underhand,   // palm up under the charging handle, index and middle hooked over it
		count,
		none = count,
	};
	constexpr int grip_count = static_cast<int>(grip::count);

	std::string_view name_of(grip which);
	// none when unknown.
	grip grip_named(std::string_view name);

	// Limits (degrees); the panel's sliders use the same.
	constexpr float bend_limit = 60.0f;
	constexpr float side_limit = 30.0f;
	constexpr float thumb_base_limit = 60.0f;

	struct finger_tweak
	{
		float bend{}; // added at each bending joint (thumb: Thumb2, Thumb3); + closes
		float side{}; // about the first bending joint, before its bend; + towards the thumb
	};

	struct tweak
	{
		finger_tweak fingers[fingers::count];
		float thumb_across{}; // Thumb1 about its local z (+ folds the thumb over the palm; live 2026-11-15)
		float thumb_out{};    // Thumb1 about its local y (+ opens the thumb away from the index; live 2026-11-15)
		bool empty() const;
	};
	using table = std::array<tweak, grip_count>;

	// Values clamped to the limits; non-finite ones become 0.
	tweak clamped(const tweak& value);

	// bfvr_poses.ini: one line per grip,
	//   <grip> = <thumb bend side>, <index ..>, <middle ..>, <ring ..>, <pinky ..>, <thumb_base across out>
	// '#' and ';' start comments. Grips not in the text keep their value in `into`. Returns one warning per
	// line it could not use.
	std::vector<std::string> parse(std::string_view text, table& into);
	std::string format(const table& values);

	// The built-in tweaks (parsed from the text in pose_tweaks.cpp; paste a saved bfvr_poses.ini there).
	const table& defaults();
	std::string_view builtin_text();

	// The drawn local rotation of a finger's bending joint (finger_pose's joint 0..2 from the knuckle; the
	// thumb's are Thumb2..4) with the tweak. right: the right hand (bone axes mirror in z, so side turns the
	// other way to mean the same).
	ik::quat tweaked(ik::quat rotation, fingers::finger which, int joint, const tweak& value, bool right);
	// Thumb1 (the thumb's base) with the tweak.
	ik::quat tweaked_thumb_base(ik::quat rotation, const tweak& value, bool right);

	// Eases current towards target (time constant seconds), so a change of grip does not pop.
	void ease(tweak& current, const tweak& target, float seconds, float time_constant = 0.05f);
}
