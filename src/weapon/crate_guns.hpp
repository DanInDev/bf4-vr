#pragma once

#include <string>
#include <string_view>

// #34: the guns of the campaign weapon crate (re/notes/weapon.md "The weapon crate and unlocks", 51 in the crate's
// order), as the own crate's choices (settings crate_primary / crate_secondary). Engine-free; tested by
// tests/crate_guns_test.cpp.
namespace bfvr::weapon::crate
{
	constexpr int gun_count = 51;
	// "keep" (no change), then each gun's SoldierWeaponUnlockAsset debug id.
	inline constexpr const char* values = "keep|U_MP412Rex|U_CBJ-MS|U_Scorpion|U_JS2|U_MX4|U_P90|U_MagpulPDR|U_PP2000|U_UMP45|U_870|U_HAWK|U_M1014|U_QBS09_SP|U_SAIGA_20K|U_SPAS12|U_USAS-12_SP|U_UTAS|U_A91|U_GalilACE52|U_AK5C|U_G36C|U_M4A1|U_SG553LB|U_AEK971|U_AK12|U_SteyrAug|U_CZ805|U_FAMAS|U_M16A4|U_M416|U_QBZ951|U_SAR21|U_SCAR-H|U_M240|U_M249|U_MG4|U_Pecheneg|U_QBB95|U_Type88|U_Ultimax|U_MK11|U_M39EBR|U_QBU88|U_RFB_SP|U_SCAR-HSV|U_SVD12|U_SRS|U_FY-JS|U_M82A3_SP|U_Scout|U_SV98";
	// The same in display order with readable names, for the panel.
	inline constexpr const char* choices = "keep=Keep|U_MP412Rex=MP412 Rex|U_CBJ-MS=CBJ-MS|U_Scorpion=CS-LR4 Scorpion|U_JS2=JS2|U_MX4=MX4|U_P90=P90|U_MagpulPDR=PDW-R|U_PP2000=PP-2000|U_UMP45=UMP-45|U_870=870 MCS|U_HAWK=Hawk 12G|U_M1014=M1014|U_QBS09_SP=QBS-09|U_SAIGA_20K=Saiga 12K|U_SPAS12=SPAS-12|U_USAS-12_SP=USAS-12|U_UTAS=UTS-15|U_A91=A-91|U_GalilACE52=ACE 52 CQB|U_AK5C=AK 5C|U_G36C=G36C|U_M4A1=M4|U_SG553LB=SG553|U_AEK971=AEK-971|U_AK12=AK-12|U_SteyrAug=AUG A3|U_CZ805=CZ-805|U_FAMAS=FAMAS|U_M16A4=M16A4|U_M416=M416|U_QBZ951=QBZ-95-1|U_SAR21=SAR-21|U_SCAR-H=SCAR-H|U_M240=M240B|U_M249=M249|U_MG4=MG4|U_Pecheneg=PKP Pecheneg|U_QBB95=QBB-95-1|U_Type88=Type 88 LMG|U_Ultimax=U-100 MK5|U_MK11=MK11 MOD 0|U_M39EBR=M39 EMR|U_QBU88=QBU-88|U_RFB_SP=RFB|U_SCAR-HSV=SCAR-H SV|U_SVD12=SVD-12|U_SRS=SRR-61|U_FY-JS=FY-JS|U_M82A3_SP=M82A3|U_Scout=Scout Elite|U_SV98=SV-98";

	// Settings are applied every frame: true once when the value changes to a gun, never for the first value seen
	// (the ini's, at start-up) or for "keep".
	class change_watch
	{
	public:
		bool update(std::string_view value);

	private:
		std::string last_;
		bool seen_{};
	};
}
