// tests/sitl/sitl_varyant.py yardımcısı: C++ çekirdeğin (SmvVehicles + SmvSitlConfig) her çeşit için ürettiği SITL girdilerini JSON satırı olarak yazar
// (ikili, --model, varsayilan.parm, JSON dosyası, ilk HEARTBEAT parametreleri). Unreal'sız; g++ ile derlenir.
#include "SmvSitlConfig.h"
#include "SmvVehicles.h"
#include <cstdio>
#include <string>

static std::string Esc(const std::string& S)
{
	std::string O;
	for (char C : S)
	{
		if (C == '"' || C == '\\') { O += '\\'; O += C; }
		else if (C == '\n') O += "\\n";
		else O += C;
	}
	return O;
}

int main()
{
	using namespace SmvCore;
	std::printf("[\n");
	bool bFirst = true;
	for (const FVehicleSpec& V : VehicleSpecs())
	{
		FSitlSession S;
		S.SitlType = SitlTypeFromSmv(V.SmvType);
		S.Vehicle = V.Key;
		S.Airframe = V.Airframe.empty() ? "classic" : V.Airframe;
		S.HomeLat = 39.9334; S.HomeLon = 32.8597; S.HomeAlt = 100;
		S.bBinLog = false;
		std::string JName, JText;
		const bool bJ = SessionJson(S, JName, JText);
		std::string Hb;
		for (auto& P : FirstHeartbeatParams(S, false))
		{
			char B[128];
			std::snprintf(B, sizeof(B), "%s[\"%s\", %.10g]", Hb.empty() ? "" : ", ", P.first.c_str(), P.second);
			Hb += B;
		}
		const std::string Kind = (S.SitlType == "sabit_kanat") ? "plane" : (IsPlaneType(S.SitlType) ? "vtol" : (S.SitlType == "rover" ? "rover" : "copter"));
		std::printf("%s  {\"name\": \"%s\", \"bin\": \"%s\", \"model\": \"%s\", \"kind\": \"%s\", \"json_name\": \"%s\", \"json\": \"%s\", \"parm\": \"%s\", \"hb\": [%s]}",
			bFirst ? "" : ",\n", V.Key.c_str(), SessionBinary(S).c_str(), SessionModel(S).c_str(), Kind.c_str(), bJ ? JName.c_str() : "",
			Esc(bJ ? JText : std::string()).c_str(), Esc(DefaultsParmText(S)).c_str(), Hb.c_str());
		bFirst = false;
	}
	std::printf("\n]\n");
	return 0;
}
