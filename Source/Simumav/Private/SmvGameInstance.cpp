#include "SmvGameInstance.h"
#include "SmvAtmosphere.h"
#include "SimumavModule.h"
#include "SimumavClient.h"
#include "SmvDataService.h"
#include "SmvFlow.h"
#include "SmvSteam.h"
#include "SmvI18n.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

namespace
{
	/** OverCrowd eklentisinin ayar CDO'su (GC dışı havuzda) Blueprint sınıflarına (BP_NiagaraCrowd …) sert başvuru tutuyor; paketlenmiş
	 *  oyunda ilk çöp toplamada "Disregard for GC assumptions" ölümcül hatası (Development) / sallanan işaretçi (Shipping) olur.
	 *  Başvurulan nesneler kök kümesine alınır (eklenti yoksa hiçbir şey yapılmaz). */
	void SmvPinPluginSettingRefs()
	{
		// başvurulan Blueprint sınıfları önceden yüklenip köke alınır (CDO onları tembel yükleyebiliyor: ilk denemede 0 nesne bulundu)
		static bool bPreloaded = false;
		if (!bPreloaded)
		{
			bPreloaded = true;
			for (const TCHAR* Cls : { TEXT("/OverCrowd/Blueprints/Niagara/BP_NiagaraCrowd.BP_NiagaraCrowd_C"), TEXT("/OverCrowd/Blueprints/Niagara/BP_BlueprintActorCrowd.BP_BlueprintActorCrowd_C"),
				TEXT("/OverCrowd/Blueprints/Niagara/BP_NiagaraCrowdSpline.BP_NiagaraCrowdSpline_C"), TEXT("/OverCrowd/Blueprints/Base/BP_CrowdMember_Actor.BP_CrowdMember_Actor_C") })
			{
				if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(FString(Cls)))) continue;
				if (UClass* C = StaticLoadClass(UObject::StaticClass(), nullptr, Cls, nullptr, LOAD_NoWarn | LOAD_Quiet)) { C->AddToRoot(); UE_LOG(LogSimumav, Log, TEXT("Eklenti sınıfı köke alındı: %s"), Cls); }
			}
		}
		for (const TCHAR* Path : { TEXT("/Script/OverCrowd.OverCrowdDeveloperSettings") })
		{
			UClass* C = FindObject<UClass>(nullptr, Path);
			UObject* Cdo = C ? C->GetDefaultObject() : nullptr;
			if (!Cdo) continue;
			int32 N = 0;
			for (TPropertyValueIterator<FObjectPropertyBase> It(C, Cdo); It; ++It)
			{
				UObject* O = It.Key()->GetObjectPropertyValue(It.Value());
				if (O && !O->IsRooted() && !GUObjectArray.IsDisregardForGC(O)) { O->AddToRoot(); ++N; }
			}
			if (N) UE_LOG(LogSimumav, Log, TEXT("Eklenti ayar başvuruları kök kümesine alındı: %s (%d nesne)"), Path, N);
		}
	}

	/** GPU çökmesi bekçisi: Saved/Crashes altında son denetimden sonra oluşmuş GPU çökmesi (D3D12 device removed / hung, GPU crash dump)
	 *  varsa güvenli grafik kademesi bir artar (GameUserSettings.ini [Simumav.GpuGuard] Level, en çok 2) ve her açılışta uygulanır:
	 *  1 → eşzamansız hesaplama kapalı (r.RDG.AsyncCompute 0) + Lumen yazılım izleme (r.Lumen.HardwareRayTracing 0);
	 *  2 → ayrıca sanal gölge haritası yerine gölge haritası, Lumen yansıma kalitesi düşük. -SmvGpuSafe=0 kademeyi sıfırlar.
	 *  true → bu açılışta yeni GPU çökmesi bulundu (kullanıcıya bildirilir). */
	bool SmvGpuCrashGuard(int32& OutLevel)
	{
		const TCHAR* Sec = TEXT("Simumav.GpuGuard");
		int32 Level = 0;
		FString SeenS;
		GConfig->GetInt(Sec, TEXT("Level"), Level, GGameUserSettingsIni);
		GConfig->GetString(Sec, TEXT("LastSeen"), SeenS, GGameUserSettingsIni);
		FDateTime Seen(0);
		FDateTime::ParseIso8601(*SeenS, Seen);
		int32 Force = -1;
		if (FParse::Value(FCommandLine::Get(), TEXT("SmvGpuSafe="), Force)) Level = FMath::Clamp(Force, 0, 2);
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Crashes");
		TArray<FString> Subs;
		IFileManager::Get().FindFiles(Subs, *(Dir / TEXT("*")), false, true);
		FDateTime Newest = Seen;
		bool bNew = false;
		for (const FString& Sd : Subs)
		{
			const FString F = Dir / Sd / TEXT("CrashContext.runtime-xml");
			const FDateTime Ts = IFileManager::Get().GetTimeStamp(*F);
			if (Ts <= Seen || Ts == FDateTime::MinValue()) continue;
			if (Ts > Newest) Newest = Ts;
			FString X;
			if (!FFileHelper::LoadFileToString(X, *F)) continue;
			if (X.Contains(TEXT("DEVICE_REMOVED")) || X.Contains(TEXT("DEVICE_HUNG")) || X.Contains(TEXT("Device Removed")) || X.Contains(TEXT("GPU Crash"))
				|| X.Contains(TEXT("GPUCrash")) || (X.Contains(TEXT("D3D12RHI")) && X.Contains(TEXT("removed"))))
			{
				bNew = true;
				UE_LOG(LogSimumav, Warning, TEXT("GPU çökmesi kaydı bulundu: %s (%s)"), *Sd, *Ts.ToString());
			}
		}
		if (bNew && Force < 0) Level = FMath::Min(2, Level + 1);
		if (Newest > Seen) GConfig->SetString(Sec, TEXT("LastSeen"), *Newest.ToIso8601(), GGameUserSettingsIni);
		GConfig->SetInt(Sec, TEXT("Level"), Level, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		auto Set = [](const TCHAR* N, int32 V) { if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(N)) C->Set(V, ECVF_SetByGameSetting); };
		if (Level >= 1) { Set(TEXT("r.RDG.AsyncCompute"), 0); Set(TEXT("r.Lumen.HardwareRayTracing"), 0); }
		if (Level >= 2) { Set(TEXT("r.Shadow.Virtual.Enable"), 0); Set(TEXT("r.Lumen.Reflections.HardwareRayTracing"), 0); }
		UE_LOG(LogSimumav, Log, TEXT("GPU bekçisi: güvenli grafik kademesi %d%s"), Level, bNew ? TEXT(" (yeni GPU çökmesi)") : TEXT(""));
		OutLevel = Level;
		return bNew;
	}
}

void USmvGameInstance::Init()
{
	Super::Init();
	SmvPinPluginSettingRefs();
	// her çöp toplamadan önce yeniden (tembel yüklenen başvurular için; ucuz)
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(&SmvPinPluginSettingRefs);
	// motorun ekran üstü hata ayıklama iletileri (ör. "Runtime Virtual Texture ... needs to be rebuilt") oyuncuya gösterilmez
	// (Development paketinde de; günlükte kalır). -SmvScreenMessages ile açılabilir.
	if (!FParse::Param(FCommandLine::Get(), TEXT("SmvScreenMessages")))
	{
		if (GEngine)
		{
			GEngine->bEnableOnScreenDebugMessages = false;
			GEngine->Exec(nullptr, TEXT("DisableAllScreenMessages"));   // RVT / gölge uyarı satırları da gizlenir
		}
	}
	Client = NewObject<USimumavClient>(this, TEXT("SimumavClient"));
	Data = NewObject<USmvDataService>(this, TEXT("SmvData"));
	Flow = NewObject<USmvFlow>(this, TEXT("SmvFlow"));

	const USmvSteamSubsystem* Steam = GetSubsystem<USmvSteamSubsystem>();
	Data->Init(Client, Steam ? Steam->GetSteamId() : FString(TEXT("local")), Steam ? Steam->GetPersonaName() : FString());
	Data->LoadLocal();
	FSmvI18n::SetLanguage(Data->Settings().Lang.IsEmpty() ? FSmvI18n::SystemLanguage() : Data->Settings().Lang);
	ApplyGraphicsQuality(Data->Settings().GraphicsQuality);
	Flow->Init(Client, Data);
	int32 GpuLevel = 0;
	if (SmvGpuCrashGuard(GpuLevel)) Flow->StartupNotice = FSmvI18n::Format(TEXT("gpu.safe_mode"), { { TEXT("n"), FString::FromInt(GpuLevel) } }).ToString();

	// kayıtlı simumav.com oturumu varsa sessizce aç (yoksa misafir; giriş zorunlu değil)
	TWeakObjectPtr<USmvGameInstance> Weak(this);
	Client->ResumeSession([Weak](bool bOk)
	{
		USmvGameInstance* Self = Weak.Get();
		if (!Self || !bOk) return;
		Self->Data->RefreshOnline(FSmvDone::CreateLambda([Weak](bool, const FString&)
		{
			if (USmvGameInstance* S2 = Weak.Get()) S2->Flow->NewDraft();
		}));
	});
	Client->FetchMeta(nullptr);
	UE_LOG(LogSimumav, Log, TEXT("Simumav oyun örneği hazır (dil %s)"), *FSmvI18n::Language());
}

void USmvGameInstance::Shutdown()
{
	if (Flow && Flow->GetLink()) Flow->EndFlight(TEXT("quit"));
	Super::Shutdown();
}

USmvGameInstance* USmvGameInstance::GetSmv(const UObject* WorldContextObject)
{
	return Cast<USmvGameInstance>(UGameplayStatics::GetGameInstance(WorldContextObject));
}

void USmvGameInstance::ApplyGraphicsQuality(int32 Q) const
{
	if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		S->SetOverallScalabilityLevel(FMath::Clamp(Q, 0, 4));
		// çözünürlük / pencere kipi burada değiştirilmez (yalnız kalite): kayıtlı çözünürlük -ResX/-ResY komut satırını ve ultra geniş
		// (3440×1440) pencereyi 1600×900'e çekiyordu — çözünürlük Ayarlar → Görüntü → Uygula ile
		S->ApplyNonResolutionSettings();
		S->SaveSettings();
	}
	// atmosfer kalitesi (hacimsel sis/bulut örnekleri, Lumen kalitesi, UDS bulut kipi) aynı ön ayara bağlı
	FSmvAtmosphere::ApplyQuality(GetWorld(), FMath::Clamp(Q, 0, 4));
}
