// Checks the campaign-only rules (src/safety/mp_rules): which command lines and which level paths let
// the mod run, for Battlefield 4 and Battlefield 3. Command lines are the shapes seen in the games.
#include <cstdio>
#include <string>

#include "safety/mp_rules.hpp"

using namespace bfvr::safety;

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
}

int main()
{
	const std::string bf3_campaign = R"("E:\EA\Battlefield 3\bf3.exe" -webMode SP -Origin_NoAppFocus  -AuthCode x -requestState State_ResumeCampaign )"
		R"(-requestStateParams "<data personaref=\"1\" levelmode=\"sp\" logintoken=\"y\"></data>" -Online.BlazeLogLevel 2)";
	expect(command_line_refusal(game::bf3, bf3_campaign) == nullptr, "BF3: the campaign line from Battlelog is allowed");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webmode sp -requestState State_ResumeCampaign)") == nullptr, "BF3: case does not matter");
	expect(command_line_refusal(game::bf3, R"("bf3.exe")") != nullptr, "BF3: no mode on the line: off");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode MP -requestState State_ConnectToGameId -requestStateParams "<data levelmode=\"mp\"></data>")") != nullptr,
		"BF3: joining a server: off");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode COOP -requestState State_MatchmakeCoOp)") != nullptr, "BF3: co-op matchmaking: off");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode SP -requestState State_CreateCoOpPeer)") != nullptr, "BF3: a co-op request under -webMode SP: off");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode SP -requestStateParams "<data levelmode=\"coop\"></data>")") != nullptr,
		"BF3: single-player switch with another level mode: off");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode SPECTATOR)") != nullptr, "BF3: another mode that starts with SP: off");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode SP)") == nullptr, "BF3: -webMode SP at the end of the line is allowed");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode SP -requestStateParams "<data levelmode="sp"></data>")") == nullptr,
		"BF3: level mode without escaped quotes");
	expect(command_line_refusal(game::bf3, R"("bf3.exe" -webMode SP -requestStateParams "<data levelmode=\"spx\"></data>")") != nullptr,
		"BF3: a level mode that only starts with sp: off");

	expect(command_line_refusal(game::bf4, R"("E:\EA\Battlefield 4\bf4.exe")") == nullptr, "BF4: a plain start is allowed (the level guard decides)");
	expect(command_line_refusal(game::bf4, R"("bf4.exe" -webMode MP -requestState State_ConnectToGameId)") != nullptr, "BF4: joining a server: off");
	expect(command_line_refusal(game::bf4, R"("bf4.exe" -requestState State_ClaimReservation)") != nullptr, "BF4: claiming a reservation: off");
	// Stricter than before this file existed: these four also keep the BF4 mod off.
	expect(command_line_refusal(game::bf4, R"("bf4.exe" -requestState State_ConnectToUserId)") != nullptr, "BF4: joining a player: off");
	expect(command_line_refusal(game::bf4, R"("bf4.exe" -requestState State_MatchmakeCoOp)") != nullptr, "BF4: co-op matchmaking: off");
	expect(command_line_refusal(game::bf4, R"("bf4.exe" -requestState State_CreateCoOpPeer)") != nullptr, "BF4: hosting co-op: off");
	expect(command_line_refusal(game::bf4, R"("bf4.exe" -webMode COOP)") != nullptr, "BF4: co-op mode: off");
	expect(command_line_refusal(game::bf4, R"("bf4.exe" -webMode SP -requestState State_ResumeCampaign)") == nullptr, "BF4: a campaign request is allowed");

	expect(judge_level(game::bf3, "") == level_kind::none, "no level");
	expect(judge_level(game::bf3, "Levels/Web_Loading/Web_Loading") == level_kind::front_end, "BF3: the campaign front end");
	expect(judge_level(game::bf3, "Levels/FrontEnd/FrontEnd") == level_kind::front_end, "BF3: the front end");
	expect(judge_level(game::bf3, "Levels/SP_Earthquake/SP_Earthquake") == level_kind::campaign, "BF3: a campaign level");
	expect(judge_level(game::bf3, "levels/sp_paris/sp_paris") == level_kind::campaign, "BF3: case does not matter");
	expect(judge_level(game::bf3, "Levels/COOP_007/COOP_007") == level_kind::forbidden, "BF3: a co-op level");
	expect(judge_level(game::bf3, "Levels/MP_001/MP_001") == level_kind::forbidden, "BF3: a multiplayer level");
	expect(judge_level(game::bf3, "Levels/XP1_001/XP1_001") == level_kind::forbidden, "BF3: an expansion level");
	expect(judge_level(game::bf3, "Levels/SP/SP_Tanker/SP_Tanker") == level_kind::forbidden, "BF3: BF4's campaign folder is not BF3's");

	expect(judge_level(game::bf4, "Levels/SP/SP_Tanker/SP_Tanker") == level_kind::campaign, "BF4: a campaign level");
	expect(judge_level(game::bf4, "Levels/FrontEnd/FrontEnd") == level_kind::front_end, "BF4: the front end");
	expect(judge_level(game::bf4, "Levels/MP/MP_Abandoned/MP_Abandoned") == level_kind::forbidden, "BF4: a multiplayer level");
	expect(judge_level(game::bf4, "Levels/Web_Loading/Web_Loading") == level_kind::forbidden, "BF4: BF3's front end is not BF4's");
	expect(judge_level(game::bf4, "Levels/SP_Earthquake/SP_Earthquake") == level_kind::forbidden, "BF4: BF3's campaign folder is not BF4's");

	std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
	return failures ? 1 : 0;
}
