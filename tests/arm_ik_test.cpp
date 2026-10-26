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

	std::printf(failures ? "arm_ik_test: %d failures\n" : "arm_ik_test: all passed\n", failures);
	return failures ? 1 : 0;
}
