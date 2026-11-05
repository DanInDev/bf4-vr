// Checks the two-bone arm solver on a synthetic chain with twisted joints and a roll bone.
#include <cmath>
#include <cstdio>
#include <random>

#include "body/arm_ik.hpp"

using namespace bfvr::body::ik;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what, int round)
	{
		if (!condition)
		{
			std::printf("FAIL round %d: %s\n", round, what);
			++failures;
		}
	}

	quat random_rotation(std::mt19937& random, float max_angle)
	{
		std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
		const vec3 axis = normalize(vec3{ unit(random), unit(random), unit(random) });
		const float half = 0.5f * max_angle * unit(random);
		return { axis.x * std::sin(half), axis.y * std::sin(half), axis.z * std::sin(half), std::cos(half) };
	}
}

int main()
{
	// root, upper arm, upper roll, forearm, forearm roll, hand, weapon (child of the hand)
	const int parents[] = { -1, 0, 1, 2, 3, 4, 5 };
	const arm_bones bones{ 1, 3, 5, 4 };
	std::mt19937 random(11);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);

	for (int round = 0; round < 500; ++round)
	{
		bone_transform local[7];
		local[0].rotation = random_rotation(random, 3.0f);
		local[1].position = { 0.2f, 1.5f, 0.0f };
		local[2].position = { 0.14f, 0.01f, 0.0f };
		local[3].position = { 0.15f, 0.0f, 0.02f };
		local[4].position = { 0.10f, 0.0f, 0.0f };
		local[5].position = { 0.16f, 0.0f, 0.0f };
		local[6].position = { 0.05f, -0.05f, 0.1f };
		for (int bone = 1; bone < 7; ++bone)
		{
			local[bone].rotation = random_rotation(random, 2.0f);
		}

		const vec3 shoulder = model_of(local, parents, 1).position;
		const float a = length(model_of(local, parents, 3).position - shoulder);
		const float b = length(model_of(local, parents, 5).position - model_of(local, parents, 3).position);
		const bool reachable = round % 4 != 0;
		const vec3 direction = normalize(vec3{ unit(random), unit(random), unit(random) });
		const float distance = reachable ? std::fabs(a - b) + 0.05f + (a + b - std::fabs(a - b) - 0.1f) * (0.5f + 0.5f * unit(random))
		                                 : (a + b) * 1.5f;
		arm_goal goal;
		goal.wrist = shoulder + direction * distance;
		goal.hand = random_rotation(random, 3.0f);
		goal.pole = shoulder + normalize(cross(direction, vec3{ unit(random), unit(random), unit(random) })) * 0.5f;

		const vec3 weapon_offset_before = rotate(conjugate(model_of(local, parents, 5).rotation),
			model_of(local, parents, 6).position - model_of(local, parents, 5).position);

		expect(solve_arm(local, parents, bones, goal), "solver refused", round);
		const bone_transform hand = model_of(local, parents, 5);
		const vec3 elbow = model_of(local, parents, 3).position;
		const vec3 expected_wrist = reachable ? goal.wrist : shoulder + direction * ((a + b) * 0.9999f);
		expect(length(hand.position - expected_wrist) < 1e-3f, "wrist misses the goal", round);
		const quat q = normalize(goal.hand);
		const float alignment = std::fabs(hand.rotation.x * q.x + hand.rotation.y * q.y + hand.rotation.z * q.z + hand.rotation.w * q.w);
		expect(alignment > 0.9999f, "hand rotation differs from the goal", round);
		expect(std::fabs(length(elbow - shoulder) - a) < 1e-3f, "upper arm length changed", round);
		expect(std::fabs(length(hand.position - elbow) - b) < 1e-3f, "forearm length changed", round);
		const vec3 n = direction;
		const vec3 elbow_side = (elbow - shoulder) - n * dot(elbow - shoulder, n);
		const vec3 pole_side = (goal.pole - shoulder) - n * dot(goal.pole - shoulder, n);
		expect(!reachable || dot(elbow_side, pole_side) > 0.0f, "elbow bends away from the pole", round);
		const vec3 weapon_offset_after = rotate(conjugate(hand.rotation), model_of(local, parents, 6).position - hand.position);
		expect(length(weapon_offset_after - weapon_offset_before) < 1e-4f, "weapon left the hand", round);

		// A bone placed at a model transform lands there.
		bone_transform wanted;
		wanted.position = shoulder + direction * 0.3f;
		wanted.rotation = random_rotation(random, 3.0f);
		set_model_transform(local, parents, 6, wanted);
		const bone_transform placed = model_of(local, parents, 6);
		const float placed_alignment = std::fabs(placed.rotation.x * wanted.rotation.x + placed.rotation.y * wanted.rotation.y
			+ placed.rotation.z * wanted.rotation.z + placed.rotation.w * wanted.rotation.w);
		expect(length(placed.position - wanted.position) < 1e-4f && placed_alignment > 0.9999f, "set_model_transform misplaced the bone", round);
	}

	// Out of reach with max_stretch: the arm lengthens (up to the limit) and the wrist lands on the
	// goal; shoulder, segment directions and the gun's place in the hand are kept.
	for (int round = 0; round < 200; ++round)
	{
		bone_transform local[7];
		local[0].rotation = random_rotation(random, 3.0f);
		local[1].position = { 0.2f, 1.5f, 0.0f };
		local[2].position = { 0.14f, 0.01f, 0.0f };
		local[3].position = { 0.15f, 0.0f, 0.02f };
		local[4].position = { 0.10f, 0.0f, 0.0f };
		local[5].position = { 0.16f, 0.0f, 0.0f };
		local[6].position = { 0.05f, -0.05f, 0.1f };
		for (int bone = 1; bone < 7; ++bone)
		{
			local[bone].rotation = random_rotation(random, 2.0f);
		}
		const vec3 shoulder = model_of(local, parents, 1).position;
		const float a = length(model_of(local, parents, 3).position - shoulder);
		const float b = length(model_of(local, parents, 5).position - model_of(local, parents, 3).position);
		const vec3 direction = normalize(vec3{ unit(random), unit(random), unit(random) });
		const bool within_stretch = round % 2 == 0;
		const float factor = within_stretch ? 1.15f : 1.6f;
		arm_goal goal;
		goal.wrist = shoulder + direction * ((a + b) * factor);
		goal.hand = random_rotation(random, 3.0f);
		goal.pole = shoulder + normalize(cross(direction, vec3{ unit(random), unit(random), unit(random) })) * 0.5f;
		goal.max_stretch = 0.25f;
		expect(std::fabs(reach_ratio(local, parents, bones, goal.wrist) - factor) < 1e-3f, "reach ratio", round);
		const vec3 weapon_before = rotate(conjugate(model_of(local, parents, 5).rotation), model_of(local, parents, 6).position - model_of(local, parents, 5).position);

		expect(solve_arm(local, parents, bones, goal), "stretch: solver refused", round);
		const bone_transform hand = model_of(local, parents, 5);
		const vec3 elbow = model_of(local, parents, 3).position;
		const float limit = within_stretch ? factor : 1.25f;
		const vec3 expected_wrist = within_stretch ? goal.wrist : shoulder + direction * ((a + b) * limit * 0.9999f);
		expect(length(hand.position - expected_wrist) < 2e-3f, "stretch: wrist misses", round);
		expect(length(model_of(local, parents, 1).position - shoulder) < 1e-5f, "stretch: shoulder moved", round);
		expect(std::fabs(length(elbow - shoulder) - a * limit) < 2e-3f, "stretch: upper arm not scaled by the factor", round);
		expect(std::fabs(length(hand.position - elbow) - b * limit) < 2e-3f, "stretch: forearm not scaled by the factor", round);
		const vec3 weapon_after = rotate(conjugate(hand.rotation), model_of(local, parents, 6).position - hand.position);
		expect(length(weapon_after - weapon_before) < 1e-4f, "stretch: weapon left the hand", round);
	}

	// Out of reach with protraction (and stretch): the shoulder moves towards the goal by up to
	// max_protraction first, segment lengths unchanged; stretch only covers the rest.
	for (int round = 0; round < 200; ++round)
	{
		bone_transform local[7];
		local[0].rotation = random_rotation(random, 3.0f);
		local[1].position = { 0.2f, 1.5f, 0.0f };
		local[2].position = { 0.14f, 0.01f, 0.0f };
		local[3].position = { 0.15f, 0.0f, 0.02f };
		local[4].position = { 0.10f, 0.0f, 0.0f };
		local[5].position = { 0.16f, 0.0f, 0.0f };
		for (int bone = 1; bone < 7; ++bone)
		{
			local[bone].rotation = random_rotation(random, 2.0f);
		}
		const vec3 shoulder = model_of(local, parents, 1).position;
		const float a = length(model_of(local, parents, 3).position - shoulder);
		const float b = length(model_of(local, parents, 5).position - model_of(local, parents, 3).position);
		const vec3 direction = normalize(vec3{ unit(random), unit(random), unit(random) });
		const bool protraction_covers = round % 2 == 0;
		const float excess = protraction_covers ? 0.08f : 0.4f;
		arm_goal goal;
		goal.wrist = shoulder + direction * (a + b + excess);
		goal.hand = random_rotation(random, 3.0f);
		goal.pole = shoulder + normalize(cross(direction, vec3{ unit(random), unit(random), unit(random) })) * 0.5f;
		goal.max_protraction = 0.12f;
		goal.max_stretch = protraction_covers ? 0.0f : 0.25f;

		expect(solve_arm(local, parents, bones, goal), "protraction: solver refused", round);
		const vec3 moved = model_of(local, parents, 1).position;
		const float shift = protraction_covers ? excess : 0.12f;
		expect(length(moved - (shoulder + direction * shift)) < 1e-3f, "protraction: shoulder not moved towards the goal", round);
		const vec3 elbow = model_of(local, parents, 3).position;
		const vec3 wrist = model_of(local, parents, 5).position;
		if (protraction_covers)
		{
			expect(length(wrist - goal.wrist) < 2e-3f, "protraction: wrist misses", round);
			expect(std::fabs(length(elbow - moved) - a) < 1e-3f && std::fabs(length(wrist - elbow) - b) < 1e-3f, "protraction: arm length changed", round);
		}
		else
		{
			const vec3 expected = moved + direction * ((a + b) * 1.25f * 0.9999f);
			expect(length(wrist - expected) < 2e-3f, "protraction + stretch: wrist misses the stretched reach", round);
		}
	}

	// Reachable goals are never stretched.
	{
		bone_transform local[7];
		local[1].position = { 0.2f, 1.5f, 0.0f };
		local[3].position = { 0.3f, 0.0f, 0.0f };
		local[5].position = { 0.25f, 0.0f, 0.0f };
		arm_goal goal;
		goal.wrist = { 0.2f, 1.5f + 0.4f, 0.0f };
		goal.pole = { 0.2f, 1.5f, 1.0f };
		goal.max_stretch = 0.5f;
		const bone_transform fore_before = local[3];
		expect(solve_arm(local, parents, bones, goal), "reachable: solver refused", -1);
		expect(length(local[3].position - fore_before.position) < 1e-6f, "reachable goal stretched the arm", -1);
	}

	// The elbow is a hinge: after the solve the forearm bends about the same axis of the upper arm
	// as in the animated pose (no upper-arm twist that bends the elbow sideways).
	for (int round = 0; round < 300; ++round)
	{
		bone_transform local[7];
		local[0].rotation = random_rotation(random, 3.0f);
		local[1].position = { 0.2f, 1.5f, 0.0f };
		local[2].position = { 0.14f, 0.01f, 0.0f };
		local[3].position = { 0.15f, 0.0f, 0.02f };
		local[4].position = { 0.10f, 0.0f, 0.0f };
		local[5].position = { 0.16f, 0.0f, 0.0f };
		for (int bone = 1; bone < 7; ++bone)
		{
			local[bone].rotation = random_rotation(random, 2.0f);
		}
		const auto hinge_in_upper = [&]
		{
			const bone_transform upper = model_of(local, parents, 1);
			const vec3 elbow = model_of(local, parents, 3).position;
			const vec3 wrist = model_of(local, parents, 5).position;
			return rotate(conjugate(upper.rotation), normalize(cross(elbow - upper.position, wrist - elbow)));
		};
		const vec3 shoulder = model_of(local, parents, 1).position;
		const vec3 elbow0 = model_of(local, parents, 3).position;
		const vec3 wrist0 = model_of(local, parents, 5).position;
		const float a = length(elbow0 - shoulder);
		const float b = length(wrist0 - elbow0);
		if (length(cross(elbow0 - shoulder, wrist0 - elbow0)) < 0.3f * a * b)
		{
			continue; // nearly straight: the animated hinge is not defined well enough to compare
		}
		const vec3 before = hinge_in_upper();
		const vec3 direction = normalize(vec3{ unit(random), unit(random), unit(random) });
		arm_goal goal;
		goal.wrist = shoulder + direction * ((a + b) * (0.45f + 0.4f * (0.5f + 0.5f * unit(random))));
		goal.hand = random_rotation(random, 3.0f);
		goal.pole = shoulder + normalize(cross(direction, vec3{ unit(random), unit(random), unit(random) })) * 0.5f;
		expect(solve_arm(local, parents, bones, goal), "hinge: solver refused", round);
		expect(dot(hinge_in_upper(), before) > 0.999f, "hinge: the elbow bends about another axis of the upper arm", round);
	}

	// A hand moving through the pole's direction (left arm, elbow hint down-out-back: the belt and
	// hip) moves the elbow smoothly: no flip where the pole's side of the arm line is undefined.
	{
		const vec3 pole_direction = normalize(vec3{ -0.5f, -1.0f, 0.3f });
		vec3 last_elbow{};
		float worst = 0.0f;
		for (int step = 0; step <= 400; ++step)
		{
			bone_transform local[7];
			local[1].position = { -0.18f, 1.4f, 0.0f };
			local[3].position = { 0.0f, -0.28f, -0.05f };
			local[5].position = { 0.0f, -0.05f, -0.26f };
			const float t = static_cast<float>(step) / 400.0f;
			// A line through the pole's direction, 0.4 m from the shoulder at its middle.
			const vec3 across = normalize(cross(pole_direction, vec3{ 0.0f, 0.0f, 1.0f }));
			arm_goal goal;
			goal.wrist = local[1].position + pole_direction * 0.4f + across * (0.3f * (t - 0.5f)) + vec3{ 0.0f, 0.0f, 0.02f * (t - 0.5f) };
			goal.pole = local[1].position + pole_direction;
			goal.pole_fallback = local[1].position + normalize(vec3{ -0.6f, 0.0f, 1.0f });
			goal.has_pole_fallback = true;
			expect(solve_arm(local, parents, bones, goal), "sweep: solver refused", step);
			const vec3 elbow = model_of(local, parents, 3).position;
			if (step > 0)
			{
				worst = std::fmax(worst, length(elbow - last_elbow));
			}
			last_elbow = elbow;
		}
		std::printf("pole sweep: largest elbow step %.4f m\n", worst);
		expect(worst < 0.01f, "sweep: the elbow jumps as the hand passes the pole's direction", -1);
	}

	// The forearm roll bone takes half the hand's twist the short way round, whatever sign the goal
	// quaternion comes with (the game's often have w < 0): a small wrist turn never wraps the forearm.
	for (int round = 0; round < 300; ++round)
	{
		bone_transform start[7];
		start[0].rotation = random_rotation(random, 3.0f);
		start[1].position = { 0.2f, 1.5f, 0.0f };
		start[2].position = { 0.14f, 0.01f, 0.0f };
		start[3].position = { 0.15f, 0.0f, 0.02f };
		start[4].position = { 0.10f, 0.0f, 0.0f };
		start[5].position = { 0.16f, 0.0f, 0.0f };
		for (int bone = 1; bone < 7; ++bone)
		{
			start[bone].rotation = random_rotation(random, 2.0f);
		}
		const vec3 shoulder = model_of(start, parents, 1).position;
		const float a = length(model_of(start, parents, 3).position - shoulder);
		const float b = length(model_of(start, parents, 5).position - model_of(start, parents, 3).position);
		arm_goal goal;
		goal.wrist = shoulder + normalize(vec3{ unit(random), unit(random), unit(random) }) * ((a + b) * 0.7f);
		goal.pole = shoulder + vec3{ 0.0f, -0.5f, 0.3f };
		// The hand's current rotation turned a little: as the controller moves the authored grip.
		goal.hand = normalize(random_rotation(random, 0.6f) * model_of(start, parents, 5).rotation);
		bone_transform plus[7], minus[7];
		for (int bone = 0; bone < 7; ++bone)
		{
			plus[bone] = minus[bone] = start[bone];
		}
		arm_goal negated = goal;
		negated.hand = { -goal.hand.x, -goal.hand.y, -goal.hand.z, -goal.hand.w };
		expect(solve_arm(plus, parents, bones, goal) && solve_arm(minus, parents, bones, negated), "roll: solver refused", round);
		const auto roll_turn = [&](const bone_transform* local)
		{
			const quat q = normalize(conjugate(model_of(local, parents, 3).rotation) * model_of(local, parents, 4).rotation);
			const quat r = normalize(conjugate(model_of(start, parents, 3).rotation) * model_of(start, parents, 4).rotation);
			const quat d = q * conjugate(r);
			return 2.0f * std::acos(std::fmin(1.0f, std::fabs(d.w)));
		};
		expect(std::fabs(roll_turn(plus) - roll_turn(minus)) < 1e-3f, "roll: the goal's sign changes the forearm twist", round);
		// Half of a twist taken the short way round (at most pi) is at most a quarter turn.
		expect(roll_turn(minus) < 0.5f * 3.14159265f + 1e-3f, "roll: the forearm roll bone turned the long way round", round);
	}

	std::printf(failures ? "arm_ik_test: %d failures\n" : "arm_ik_test: all passed\n", failures);
	return failures ? 1 : 0;
}
