// Simumav — oyun (Steam) hedefi. CaySim modülü (ortak görselleştirme çekirdeği + HEPGO kurumsal istemcisi) de derlenir.
using UnrealBuildTool;
using System.Collections.Generic;

public class SimumavTarget : TargetRules
{
	public SimumavTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "CaySim", "Simumav" });
		// Steam: Shipping'de steam_appid.txt aranmaz; geliştirmede Binaries/Win64/steam_appid.txt (CI yazar) kullanılır
		bUsesSteam = true;
	}
}
