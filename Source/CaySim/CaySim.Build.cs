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
		// Eklenti Plugins/CesiumForUnreal altında ya da motorun Marketplace (Fab) eklentilerinde olabilir.
		// EngineDirectory mutlak yoldur (Target.RelativeEnginePath çalışma dizinine göreli olduğundan güvenilmez).
		// Launcher/Fab kurulumunda klasör adı karışıktır (ör. Marketplace/Cesiumfo5b7c98b8073cV11) → .uplugin dosyası aranır.
		bool bCesium = HasPlugin(PluginDirectory(Target), "CesiumForUnreal")
			|| HasPlugin(Path.Combine(EngineDirectory, "Plugins", "Marketplace"), "CesiumForUnreal")
			|| HasPlugin(Path.Combine(EngineDirectory, "Plugins", "Fab"), "CesiumForUnreal");
		System.Console.WriteLine("CaySim: Cesium for Unreal " + (bCesium ? "found (CAYSIM_WITH_CESIUM=1)" : "missing (CAYSIM_WITH_CESIUM=0)"));
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

	/** Kök altında (bir düzey alt klasör dahil) <Ad>.uplugin var mı */
	private static bool HasPlugin(string Root, string Name)
	{
		if (!Directory.Exists(Root)) return false;
		if (File.Exists(Path.Combine(Root, Name, Name + ".uplugin"))) return true;
		foreach (string D in Directory.GetDirectories(Root))
		{
			if (File.Exists(Path.Combine(D, Name + ".uplugin"))) return true;
		}
		return false;
	}

	private static string PluginDirectory(ReadOnlyTargetRules Target)
	{
		// <Proje>/Plugins (proje dosyası yoksa boş dizin → eklenti bulunamaz)
		if (Target.ProjectFile == null) return Path.Combine(Path.GetTempPath(), "caysim-yok");
		return Path.Combine(Target.ProjectFile.Directory.FullName, "Plugins");
	}
}
