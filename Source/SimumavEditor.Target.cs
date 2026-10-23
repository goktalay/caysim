// Simumav — düzenleyici hedefi
using UnrealBuildTool;
using System.Collections.Generic;

public class SimumavEditorTarget : TargetRules
{
	public SimumavEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "CaySim", "Simumav" });
	}
}
