// CAYSIM — oyun (çalıştırılabilir) hedefi
using UnrealBuildTool;
using System.Collections.Generic;

public class CAYSIMTarget : TargetRules
{
	public CAYSIMTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("CaySim");
	}
}
