// Checks ladders by hand (src/weapon/ladder_climb, #134): the rungs where they were measured on Shanghai's wall
// ladder, the stick the game climbs by, and the climb: a fresh squeeze takes the nearest rung, pulling down
// lifts the body as far as the hand travelled (pushing up lowers it), hand over hand never jumps, two hands
// average, a blocked body builds up nothing, leaving the ladder lets go, and a reach from the ground walks on.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "weapon/ladder_climb.hpp"

using namespace bfvr::weapon;
using namespace bfvr::weapon::ladder;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

	// SP_Shanghai "Reach the Hotel", the alley's wall ladder (live 2026-11-25): its entity's transform and the
	// climbing state's length.
	frame shanghai()
	{
		frame f;
		f.foot = { 171.5197f, 63.1151f, -52.6987f };
		f.right = { 0, 0, 1 };
		f.up = { 0, 1, 0 };
		f.out = { -1, 0, 0 };
		f.length = 10.3875f;
		return f;
	}

	// A player climbing: the palms move in tracking space, the game moves the body by the stick.
	struct rig
	{
		climber climb;
		climb_in in;
		float body_y = 64.205f; // the character's height on the ladder
		v3 palm_local[2]{ { -0.2f, 1.2f, -0.4f }, { 0.2f, 1.2f, -0.4f } };

		rig()
		{
			in.dt = 1.0f / 90.0f;
			in.on_ladder = true;
			in.have_ladder = true;
			in.ladder = shanghai();
			in.speed = 2.0f;
			for (auto& hand : in.hands)
			{
				hand.tracked = true;
			}
			settle(false, false, 60); // on the ladder, the game's own placing over
		}

		// The tracking space rides with the body: a palm's world height is the body's plus its tracked height.
		v3 world_of(int h) const
		{
			return { 171.42f, body_y + palm_local[h].y - 0.4f, -52.70f + palm_local[h].x };
		}

		climb_out step(bool grip_left, bool grip_right)
		{
			in.hands[0].grip = grip_left;
			in.hands[1].grip = grip_right;
			for (int h = 0; h < 2; ++h)
			{
				in.hands[h].palm = palm_local[h];
				in.hands[h].palm_world = world_of(h);
			}
			in.body_y = body_y;
			const climb_out out = climb.update(in);
			if (in.on_ladder && out.drive)
			{
				body_y += climb_speed(climb.config().curve, in.speed, out.stick_y) * in.dt;
			}
			return out;
		}

		climb_out settle(bool l, bool r, int frames = 90)
		{
			climb_out out;
			for (int i = 0; i < frames; ++i)
			{
				out = step(l, r);
			}
			return out;
		}

		// Put a palm on the nearest rung (as a player reaching for it would).
		void reach_rung(int h)
		{
			const v3 at = world_of(h);
			v3 rung;
			int index = 0;
			nearest_rung(in.ladder, climb.config().rungs, { at.x, at.y, at.z }, 1.0f, rung, index);
			palm_local[h].y += rung.y - at.y;
			in.hands[h].palm_world = world_of(h);
		}
	};
}

int main()
{
	const rung_model rungs;
	{
		// The rungs measured from two posed screenshots (rms 0.6 mm): plane x 171.495, centres 65.071, 65.368,
		// 65.666, 65.964, 66.261, 66.559.
		const frame f = shanghai();
		const float measured[] = { 65.071f, 65.368f, 65.666f, 65.964f, 66.261f, 66.559f };
		for (float y : measured)
		{
			v3 rung;
			int index = 0;
			const bool found = nearest_rung(f, rungs, { 171.47f, y + 0.03f, -52.60f }, 0.12f, rung, index);
			expect(found && near(rung.y, y, 0.006f) && near(rung.x, 171.495f, 0.003f) && near(rung.z, -52.60f), "a palm at a measured rung takes it there");
		}
		v3 rung;
		int index = 0;
		expect(!nearest_rung(f, rungs, { 171.47f, 65.52f, -52.70f }, 0.12f, rung, index), "halfway between two rungs (15 cm off) is no rung");
		expect(!nearest_rung(f, rungs, { 171.20f, 65.666f, -52.70f }, 0.12f, rung, index), "30 cm in front of the ladder is no rung");
		expect(nearest_rung(f, rungs, { 171.47f, 65.666f, -52.40f }, 0.12f, rung, index) && near(rung.z, -52.70f + rungs.half_width),
			"a hand out past the rail holds the rung at the rail");
		expect(!nearest_rung(f, rungs, { 171.47f, 65.666f, -52.20f }, 0.12f, rung, index), "half a metre to the side is no rung");
		expect(!nearest_rung(f, rungs, { 171.47f, 63.115f + 10.3875f + 0.30f, -52.70f }, 0.12f, rung, index), "above the top rung is air");
		expect(nearest_rung(f, rungs, { 171.47f, 63.115f + 0.17f, -52.70f }, 0.12f, rung, index) && index == 0, "the lowest rung is rung 0");
		expect(nearest_rung(f, rungs, { 171.47f, 63.115f + 0.166f + 34 * 0.2978f, -52.70f }, 0.12f, rung, index) && index == 34, "the top rung is rung 34");
	}
	{
		// The stick the game climbs by (monitor 2026-11-25, 2 m/s state speed).
		const stick_curve curve;
		expect(near(climb_speed(curve, 2.0f, 0.3f), 0.0f), "0.3 is inside the game's dead zone");
		expect(near(climb_speed(curve, 2.0f, 0.5f), 0.64f, 0.05f) && near(climb_speed(curve, 2.0f, 0.7f), 1.28f, 0.05f)
				&& near(climb_speed(curve, 2.0f, 0.9f), 1.92f, 0.05f) && near(climb_speed(curve, 2.0f, 1.0f), 2.0f),
			"0.5 / 0.7 / 0.9 / 1.0 climb 0.6 / 1.25 / 1.9 / 2.0 m/s as measured");
		expect(near(climb_speed(curve, 2.0f, -0.7f), -1.28f, 0.05f), "down as fast as up");
		bool round_trip = true;
		for (float v = -1.9f; v <= 1.9f; v += 0.1f)
		{
			round_trip = round_trip && near(climb_speed(curve, 2.0f, stick_for(curve, 2.0f, v)), v, 0.001f);
		}
		expect(round_trip, "the stick asked for climbs at the speed wanted");
		expect(stick_for(curve, 2.0f, 0.0f) == 0.0f, "no speed, no stick");
		expect(near(stick_for(curve, 2.0f, 5.0f), 0.925f, 0.01f) && near(stick_for(curve, 4.0f, 4.0f), 0.925f, 0.01f), "faster than the state allows: its full speed");
	}
	{
		// One hand: grab a rung and pull it down 30 cm: the body rises 30 cm and the drawn hand stays on the rung.
		rig r;
		r.reach_rung(1);
		climb_out out = r.step(false, true);
		expect(out.events[1] == event::grabbed && out.holding[1], "a fresh squeeze at a rung takes it");
		const v3 rung = out.rung[1];
		const float start = r.body_y;
		for (int i = 0; i < 30; ++i)
		{
			r.palm_local[1].y -= 0.01f;
			out = r.step(false, true);
		}
		out = r.settle(false, true);
		expect(near(r.body_y - start, 0.30f, 0.01f), "pulling the rung down 30 cm lifts the body 30 cm");
		expect(near(out.rung[1].y, rung.y, 1e-5f) && out.holding[1], "the hand stays on its rung");
		expect(std::fabs(out.stick_y) < 0.31f, "arrived: the stick rests");
		expect(near(r.world_of(1).y, rung.y, 0.01f), "where the body arrived the controller is at the rung again");
	}
	{
		// The game follows the stick a frame late. A pull still lifts the body all 30 cm, at 90 fps (30 cm in 0.1 s)
		// and at the headless 4 fps (monitor 2026-11-26: the first pull lost 28 cm there): the frame of lag at its
		// start and end is not taken for the game's own move.
		for (const float fps : { 90.0f, 3.7f })
		{
			rig r;
			r.in.dt = 1.0f / fps;
			r.reach_rung(1);
			r.step(false, true);
			const float start = r.body_y;
			float queued = 0.0f;
			const int pull_frames = fps > 10.0f ? 9 : 3;
			for (int i = 0; i < (fps > 10.0f ? 100 : 30); ++i)
			{
				if (i < pull_frames)
				{
					r.palm_local[1].y -= 0.30f / static_cast<float>(pull_frames);
				}
				r.in.hands[1].grip = true;
				r.in.hands[1].palm = r.palm_local[1];
				r.in.hands[1].palm_world = r.world_of(1);
				r.in.body_y = r.body_y;
				const climb_out out = r.climb.update(r.in);
				r.body_y += climb_speed(r.climb.config().curve, r.in.speed, queued) * r.in.dt; // last frame's stick
				queued = out.drive ? out.stick_y : 0.0f;
			}
			expect(near(r.body_y - start, 0.30f, 0.02f), fps > 10.0f ? "a fast pull at 90 fps, the game a frame late: still 30 cm up"
				: "a pull at 4 fps, the game a frame late: still 30 cm up");
		}
	}
	{
		// Pushing the rung up lowers the body (climbing down).
		rig r;
		r.body_y = 66.0f;
		r.reach_rung(0);
		r.step(true, false);
		const float start = r.body_y;
		bool down = false;
		for (int i = 0; i < 40; ++i)
		{
			r.palm_local[0].y += 0.01f;
			down = r.step(true, false).stick_y < 0.0f || down;
		}
		r.settle(true, false);
		expect(down && near(r.body_y - start, -0.40f, 0.01f), "pushing the rung up 40 cm lowers the body 40 cm");
	}
	{
		// Hand over hand: right pulls 30 cm, the left takes a higher rung, the right lets go, the left pulls 30 cm.
		rig r;
		r.reach_rung(1);
		r.step(false, true);
		const float start = r.body_y;
		for (int i = 0; i < 30; ++i)
		{
			r.palm_local[1].y -= 0.01f;
			r.step(false, true);
		}
		r.settle(false, true);
		r.palm_local[0].y = r.palm_local[1].y + 0.6f;
		r.reach_rung(0);
		const float before = r.body_y;
		climb_out out = r.step(true, true);
		expect(out.events[0] == event::grabbed && out.holding[0] && out.holding[1], "the second hand takes a higher rung");
		r.settle(true, true, 20);
		expect(near(r.body_y, before, 0.005f), "taking a second rung moves nothing");
		out = r.step(true, false);
		expect(out.events[1] == event::released && !out.holding[1] && out.holding[0], "the first hand lets go");
		r.settle(true, false, 20);
		expect(near(r.body_y, before, 0.005f), "letting go of one rung while the other holds moves nothing");
		for (int i = 0; i < 30; ++i)
		{
			r.palm_local[0].y -= 0.01f;
			r.step(true, false);
		}
		r.settle(true, false);
		expect(near(r.body_y - start, 0.60f, 0.015f), "two pulls of 30 cm, hand over hand: 60 cm up");
	}
	{
		// Both hands pulling 20 cm together lift the body 20 cm, not 40.
		rig r;
		r.reach_rung(0);
		r.reach_rung(1);
		r.step(true, true);
		const float start = r.body_y;
		for (int i = 0; i < 20; ++i)
		{
			r.palm_local[0].y -= 0.01f;
			r.palm_local[1].y -= 0.01f;
			r.step(true, true);
		}
		r.settle(true, true);
		expect(near(r.body_y - start, 0.20f, 0.01f), "two hands pulling together: their travel, not twice it");
	}
	{
		// A squeeze held from elsewhere never takes a rung; a busy hand does not either.
		rig r;
		r.step(false, false);
		r.palm_local[1].y += 0.5f;
		r.step(false, true);
		r.reach_rung(1);
		expect(!r.step(false, true).holding[1], "a grip already held when the hand reaches the rung takes nothing");
		r.reach_rung(0);
		r.in.hands[0].busy = true;
		expect(!r.step(true, true).holding[0], "a busy hand takes no rung");
	}
	{
		// Both hands open: the body stays where it is on the ladder (BF4 lets you hang), the stick is the player's.
		rig r;
		r.reach_rung(1);
		r.step(false, true);
		r.palm_local[1].y -= 0.2f;
		r.step(false, true);
		const climb_out out = r.step(false, false);
		expect(!out.drive && out.stick_y == 0.0f && !out.holding[1], "let go of everything: no drive, hanging");
	}
	{
		// Blocked: the body does not move (top, bottom, something in the way). A pull of a metre leads by 40 cm at
		// most, and the hand coming back up takes effect at once (nothing piled up).
		rig r;
		r.reach_rung(1);
		const v3 held = r.step(false, true).rung[1];
		const float body = r.body_y;
		climb_out out;
		for (int i = 0; i < 50; ++i)
		{
			r.palm_local[1].y -= 0.02f;
			r.in.body_y = body;
			r.in.hands[1].palm = r.palm_local[1];
			r.in.hands[1].palm_world = held; // keep the break distance out of this one
			r.in.hands[1].grip = true;
			out = r.climb.update(r.in);
		}
		expect(out.target_y - body <= 0.40f + 1e-4f && out.target_y > body, "a blocked pull leads by the limit at most");
		const float led = out.target_y;
		r.palm_local[1].y += 0.1f;
		r.in.hands[1].palm = r.palm_local[1];
		out = r.climb.update(r.in);
		// (a blocked body may also pull the target towards itself in the same frame: at least the hand's 10 cm)
		expect(out.target_y <= led - 0.09f, "the hand coming back counts at once");
	}
	{
		// The game's climb-off at the top (monitor 2026-11-26): pushing the top rung down, the game lifts the view
		// 1.5 m in a second while the hand still holds and the state is still Climbing. Nothing pushes against it.
		rig r;
		r.reach_rung(1);
		const v3 rung = r.step(false, true).rung[1];
		bool against = false, held = true;
		for (int i = 0; i < 45; ++i)
		{
			r.palm_local[1].y -= 0.005f;
			r.body_y += 0.035f; // the game's own lift, far faster than the hand
			r.in.hands[1].grip = true;
			r.in.hands[1].palm = r.palm_local[1];
			r.in.hands[1].palm_world = rung; // the break distance is not what this checks
			r.in.body_y = r.body_y;
			const climb_out out = r.climb.update(r.in);
			against = against || (out.drive && out.stick_y < -0.6f);
			held = held && out.holding[1];
		}
		expect(held && !against, "the game's own climb-off: the stick never pushes hard against it");
	}
	{
		// At the foot the body stops (the ground) while the hand pushes the rung on up: the stick keeps pushing down,
		// which is what makes BF4 step the soldier off (monitor 2026-11-26: with the stick at rest he stayed on).
		rig r;
		r.reach_rung(1);
		const v3 held = r.step(false, true).rung[1];
		const float body = r.body_y;
		climb_out out;
		for (int i = 0; i < 90; ++i)
		{
			if (i < 30)
			{
				r.palm_local[1].y += 0.01f;
			}
			r.in.body_y = body; // the ground: he goes no lower
			r.in.hands[1].palm = r.palm_local[1];
			r.in.hands[1].palm_world = held;
			r.in.hands[1].grip = true;
			out = r.climb.update(r.in);
		}
		expect(out.drive && out.stick_y < -0.9f, "on the ground at the foot, a push further down keeps the stick hard down");
	}
	{
		// Off the ladder (the game's climb-off at the top or the ground at the bottom): everything lets go, no stick.
		rig r;
		r.reach_rung(0);
		r.reach_rung(1);
		r.step(true, true);
		r.in.on_ladder = false;
		r.in.have_ladder = false;
		const climb_out out = r.step(true, true);
		expect(out.events[0] == event::released && out.events[1] == event::released && !out.holding[0] && !out.holding[1] && !out.drive && !out.mount,
			"leaving the ladder lets go of both rungs");
		r.in.on_ladder = true;
		r.in.have_ladder = true;
		expect(!r.step(true, true).holding[0], "back on, a grip still held from before takes nothing");
	}
	{
		// A reach from the ground: a fresh squeeze at a rung walks the soldier onto the ladder (towards it), the
		// hand already on the rung; once the game has him on it, that hand holds the rung.
		rig r;
		r.in.on_ladder = false;
		r.reach_rung(1);
		climb_out out = r.step(false, true);
		expect(out.mount && out.holding[1] && near(out.mount_direction.x, 1.0f) && near(out.mount_direction.z, 0.0f), "a reach at a rung walks towards the ladder");
		const v3 rung = out.rung[1];
		out = r.step(false, true);
		expect(out.mount, "still walking on while the grip is held");
		r.in.on_ladder = true;
		out = r.step(false, true);
		expect(!out.mount && out.holding[1] && near(out.rung[1].y, rung.y, 1e-5f), "on the ladder, the reaching hand keeps its rung");
	}
	{
		// Getting on, the game moves the body itself (monitor 2026-11-26: up 0.9 m and back within a second, at the
		// headless ~4 fps). The hand keeps its rung through it, nothing drives against it, and pulling works after.
		rig r;
		r.in.on_ladder = false;
		r.reach_rung(1);
		r.step(false, true);
		r.in.on_ladder = true;
		const float ground = r.body_y;
		bool drove = false, held = true;
		for (int i = 0; i < 90; ++i)
		{
			const float t = static_cast<float>(i) / 90.0f;
			r.body_y = ground + (t < 0.5f ? 0.9f * std::sin(t * 6.2832f) : 0.0f); // up and back within half a second
			r.in.hands[1].grip = true;
			r.in.hands[1].palm = r.palm_local[1];
			r.in.hands[1].palm_world = r.world_of(1);
			r.in.body_y = r.body_y;
			const climb_out out = r.climb.update(r.in);
			drove = drove || (out.drive && std::fabs(out.stick_y) > climb_config{}.curve.dead + 0.01f); // a push, not a resting stick
			held = held && out.holding[1];
		}
		expect(!drove && held, "the game's own mount move: the hand holds on and nothing drives against it");
		const float start = r.body_y;
		for (int i = 0; i < 30; ++i)
		{
			r.palm_local[1].y -= 0.01f;
			r.step(false, true);
		}
		r.settle(false, true);
		expect(near(r.body_y - start, 0.30f, 0.01f), "after it, a 30 cm pull lifts 30 cm");
	}
	{
		// The mount as seen on the monitor: the view ends 0.6 m higher and the body 0.43 m closer to the ladder (the
		// controller then sits beyond the rung). The hold stays, and closes on the rung nearest the controller.
		rig r;
		r.in.on_ladder = false;
		r.reach_rung(1);
		const v3 first = r.step(false, true).rung[1];
		r.in.on_ladder = true;
		r.body_y += 0.6f;
		climb_out out;
		for (int i = 0; i < 60; ++i)
		{
			r.in.hands[1].grip = true;
			r.in.hands[1].palm = r.palm_local[1];
			r.in.hands[1].palm_world = r.world_of(1);
			r.in.hands[1].palm_world.x += 0.45f; // into the wall
			r.in.body_y = r.body_y;
			out = r.climb.update(r.in);
		}
		expect(out.holding[1], "a controller pushed past the rung by the mount keeps its hold");
		expect(near(out.rung[1].y, first.y + 2 * 0.2978f, 0.01f), "after the lift the hand closes on the rung at its controller (two up)");
		expect(near(out.rung[1].x, 171.495f, 0.003f), "on the rungs' plane, not in the wall");
	}
	{
		rig r;
		r.in.on_ladder = false;
		r.reach_rung(1);
		r.step(false, true);
		const climb_out out = r.settle(false, true, 80);
		expect(!out.mount && !out.holding[1], "no ladder after the walk-on time: the reach gives up");
		r.in.have_ladder = false;
		r.step(false, false);
		expect(!r.step(false, true).mount, "no known ladder: a squeeze walks nowhere");
	}
	{
		// From the ground (feet below the rung) the walk-on pushes forward.
		rig r;
		r.in.on_ladder = false;
		r.in.has_feet = true;
		r.in.feet_y = 64.205f;
		r.reach_rung(1);
		const climb_out out = r.step(false, true);
		expect(out.mount && near(out.mount_stick, 0.8f), "from the ground: walk on, stick forward");
	}
	{
		// From the roof (monitor 2026-11-26): a hand on the hooks over the edge, the soldier steps back off it, and once
		// falling pushes towards the ladder, which the game catches; on it, the hand keeps its hold.
		const frame f = shanghai();
		v3 hook;
		expect(top_grab(f, rungs, { 171.62f, 63.115f + 10.50f, -52.65f }, 0.12f, hook) && near(hook.y, 63.115f + 10.50f, 0.001f)
				&& near(hook.x, 171.52f + 0.10f, 0.001f),
			"standing on top, the hooks over the edge can be held");
		expect(!top_grab(f, rungs, { 171.62f, 63.115f + 11.20f, -52.65f }, 0.12f, hook), "half a metre above the hooks is air");
		expect(!top_grab(f, rungs, { 172.20f, 63.115f + 10.50f, -52.65f }, 0.12f, hook), "on the roof behind the hooks is no hold");
		climber c;
		climb_in in;
		in.dt = 1.0f / 90.0f;
		in.have_ladder = true;
		in.ladder = f;
		in.has_feet = true;
		in.feet_y = 63.115f + f.length - 0.26f; // the roof
		in.body_y = in.feet_y + 1.68f;
		for (auto& hand : in.hands)
		{
			hand.tracked = true;
			hand.palm_world = { 172.3f, in.feet_y + 1.0f, -52.70f };
		}
		c.update(in);
		in.hands[1].palm_world = { 171.62f, 63.115f + 10.50f, -52.65f };
		in.hands[1].grip = true;
		climb_out out = c.update(in);
		expect(out.mount && out.holding[1] && out.mount_stick < -0.3f && out.events[1] == event::grabbed, "a hand on the hooks: the soldier backs off the edge");
		out = c.update(in);
		expect(out.mount && out.mount_stick < -0.3f, "still backing while on the roof");
		in.airborne = true;
		in.body_y += 0.3f; // on the hooks the state flickers to InAir, the view higher than on the roof
		out = c.update(in);
		expect(out.mount && out.mount_stick < -0.3f, "on the hooks (InAir, but not dropping): still backing");
		in.body_y -= 0.6f;
		out = c.update(in);
		expect(out.mount && near(out.mount_stick, 1.0f) && out.holding[1], "falling: towards the ladder, the hand on it");
		in.airborne = false;
		in.on_ladder = true;
		out = c.update(in);
		expect(!out.mount && out.holding[1], "caught: on the ladder, the hand keeps its hold");
	}
	{
		// Backing off the top that never leaves the roof gives up (nothing pushes on forever).
		climber c;
		climb_in in;
		in.dt = 1.0f / 90.0f;
		in.have_ladder = true;
		in.ladder = shanghai();
		in.has_feet = true;
		in.feet_y = 73.24f;
		for (auto& hand : in.hands)
		{
			hand.tracked = true;
		}
		c.update(in);
		in.hands[0].palm_world = { 171.60f, 73.60f, -52.70f };
		in.hands[0].grip = true;
		climb_out out = c.update(in);
		expect(out.mount && out.mount_stick < 0.0f, "the top: backing off");
		for (int i = 0; i < 300; ++i)
		{
			out = c.update(in);
		}
		expect(!out.mount && !out.holding[0], "never off the roof within its time: gives up");
	}
	if (failures == 0)
	{
		std::printf("ladder_climb: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
