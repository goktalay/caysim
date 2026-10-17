// CaySim çalışma zamanı modülü
using UnrealBuildTool;
using System.IO;

public class CaySim : ModuleRules
{
	public CaySim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"HTTP",
			"WebSockets",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI"
		});

		// gzip çözme (kayıttan oynatma kareleri) için motorun zlib üçüncü taraf modülü
		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

		// Cesium for Unreal eklentisi varsa georeference / 3D Tiles kodu derlenir.
		// Eklenti Plugins/CesiumForUnreal altında ya da motorun Marketplace eklentilerinde olabilir.
		bool bCesium = Directory.Exists(Path.Combine(PluginDirectory(Target), "CesiumForUnreal"))
			|| Directory.Exists(Path.Combine(Target.RelativeEnginePath, "Plugins", "Marketplace", "CesiumForUnreal"));
		if (bCesium)
		{
			PublicDependencyModuleNames.Add("CesiumRuntime");
			PublicDefinitions.Add("CAYSIM_WITH_CESIUM=1");
		}
		else
		{
			PublicDefinitions.Add("CAYSIM_WITH_CESIUM=0");
		}

		// glTFRuntime eklentisi varsa HEPGO'dan inen GLB modelleri çalışma zamanında yüklenir.
		bool bGltf = Directory.Exists(Path.Combine(PluginDirectory(Target), "glTFRuntime"));
		if (bGltf)
		{
			PrivateDependencyModuleNames.Add("glTFRuntime");
			PublicDefinitions.Add("CAYSIM_WITH_GLTFRUNTIME=1");
		}
		else
		{
			PublicDefinitions.Add("CAYSIM_WITH_GLTFRUNTIME=0");
		}
	}

	private static string PluginDirectory(ReadOnlyTargetRules Target)
	{
		// <Proje>/Plugins (proje dosyası yoksa boş dizin → eklenti bulunamaz)
		if (Target.ProjectFile == null) return Path.Combine(Path.GetTempPath(), "caysim-yok");
		return Path.Combine(Target.ProjectFile.Directory.FullName, "Plugins");
	}
}
