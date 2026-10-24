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
		S->ApplySettings(false);
	}
	// atmosfer kalitesi (hacimsel sis/bulut örnekleri, Lumen kalitesi, UDS bulut kipi) aynı ön ayara bağlı
	FSmvAtmosphere::ApplyQuality(GetWorld(), FMath::Clamp(Q, 0, 4));
}
