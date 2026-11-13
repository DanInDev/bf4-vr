#pragma once

// Sprint and vault toward the view (#52). The game walks relative to its soldier's facing, and with
// aim_stays_with_body that facing is the mod's body yaw, not the head: a forward push turned to where
// the head looks reaches the game as a diagonal, and BF4 only sprints and vaults on a mostly forward
// stick. While the player sprints (left stick click with the stick forward) or jumps forward, this asks
// for the soldier to face the move direction; the stick is always turned from the head into the
// soldier's frame, so once the soldier faces the way it goes the game sees a straight push. The view
// does not move: the world camera follows the body yaw, not the soldier. Engine-free; angles in
// radians, camera-yaw convention (positive to the left). Tested by tests/move_steer_test.cpp.
namespace bfvr::input
{
	float wrap_angle(float radians);

	struct steer_config
	{
		float push{ 0.6f };          // stick magnitude that counts as a push
		float release{ 0.3f };       // below this the stick is let go: sprint and vault end
		float cone{ 0.785f };        // a push within 45 degrees of the head is forward
		float leave{ 1.047f };       // a sprint ends when the push swings past 60 degrees
		float vault_seconds{ 0.7f }; // a forward jump faces the soldier this long (enough to vault)
	};

	struct steer_input
	{
		float seconds{};
		float x{}, y{};        // left stick, -1..1, y forward
		float head_yaw{};      // the head's yaw in the world (body yaw + the head's own yaw)
		float soldier_yaw{};   // the yaw the game moves relative to (its aimer)
		bool sprint_click{};   // left stick click held
		bool jump{};           // jump held
		bool fire{};           // fire trigger held: the game ends a sprint
	};

	struct steer_output
	{
		float x{}, y{};      // the stick for the game
		bool steer{};        // the soldier should face yaw
		float yaw{};         // the move direction in the world
		bool sprinting{};
	};

	class move_steer
	{
	public:
		explicit move_steer(const steer_config& config = {}) : config_(config) {}
		steer_output update(const steer_input& in);
		void reset();

	private:
		steer_config config_;
		bool sprinting_{};
		bool click_was_{};
		bool jump_was_{};
		float vault_left_{};
	};
}
