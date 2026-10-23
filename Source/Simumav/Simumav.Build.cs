// Simumav birincil oyun modülü — SIMUMAV hesap/API istemcisi, yerel ArduPilot SITL (misafir kip), Steam, arayüz, girdi, ses, dünya
using UnrealBuildTool;
using System.IO;

public class Simumav : ModuleRules
{
	public Simumav(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// birleşik (unity) derleme kapalı: arayüz dosyaları (SmvUiKit, SSmvShell, SSmvFlightUi) dosya kapsamında farklı tema ad alanları
		// kullanır (using namespace) ve aynı adlı yardımcılar (Accent, Dim, DrText …) unity birleşiminde belirsizleşir
		bUseUnity = false;

		// Saf C++ çekirdek (Unreal'sız; tests/cpp ile g++'ta sınanır) özel alt klasörlerde
		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private", "Core"),
			Path.Combine(ModuleDirectory, "Private", "Mavlink"),
			Path.Combine(ModuleDirectory, "Private", "Crypto")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Slate",
			"SlateCore",
			"UMG",
			"HTTP",
			"WebSockets",
			"Json",
			"JsonUtilities",
			"CaySim",
			// AudioMixer: Public/SmvMotorSynth.h ve SmvToneSynth.h "Components/SynthComponent.h" içeriyor → herkese açık bağımlılık
			"AudioMixer"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"Sockets",
			"Networking",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"ProceduralMeshComponent",
			"RenderCore",
			// video kaydı: JPEG kareler (SmvVideoRec)
			"ImageWrapper",
			"Projects",
			// Fab dron gövdesi /Game/Fab/Drone altında adı bilinmeden aranır (SmvFabContent)
			"AssetRegistry",
			// ana menü sahnesi: motor Niagara şablonundan kopyalanan ışık zerreleri (/Game/Simumav/FX/NS_SmvMotes; yoksa atlanır)
			"Niagara"
		});

		// Steam başlatma OnlineSubsystemSteam eklentisiyle yapılır (DefaultEngine.ini); modül yalnız OSS arayüzlerini kullanır.
		DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Windows Kimlik Bilgisi Yöneticisi (CredWriteW/CredReadW/CredDeleteW) — simumav.com belirteci
			PublicSystemLibraries.Add("Advapi32.lib");
			// GPWS konuşması: Windows SAPI (ISpVoice; COM) — SmvSpeech
			PublicSystemLibraries.Add("ole32.lib");
			PublicDefinitions.Add("SIMUMAV_WITH_WINCRED=1");
		}
		else
		{
			PublicDefinitions.Add("SIMUMAV_WITH_WINCRED=0");
		}

		// Yerel ArduPilot SITL ikilileri (misafir kip fiziği). CI, Tools/sitl/sitl-manifest.json'daki sabit sürümü sha256 ile
		// doğrulayıp ThirdParty/ArduPilotSITL/Win64 altına indirir; burada paket içine NonUFS olarak eklenir.
		string SitlDir = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "ArduPilotSITL", "Win64");
		if (Target.Platform == UnrealTargetPlatform.Win64 && Directory.Exists(SitlDir))
		{
			foreach (string F in Directory.GetFiles(SitlDir))
			{
				string Name = Path.GetFileName(F);
				RuntimeDependencies.Add("$(ProjectDir)/ThirdParty/ArduPilotSITL/Win64/" + Name, StagedFileType.NonUFS);
			}
			PublicDefinitions.Add("SIMUMAV_SITL_BUNDLED=1");
		}
		else
		{
			PublicDefinitions.Add("SIMUMAV_SITL_BUNDLED=0");
		}
	}
}
