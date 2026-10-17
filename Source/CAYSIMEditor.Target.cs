// CAYSIM — düzenleyici hedefi
using UnrealBuildTool;
using System.Collections.Generic;

public class CAYSIMEditorTarget : TargetRules
{
	public CAYSIMEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("CaySim");
	}
}
