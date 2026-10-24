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

void USmvGameInstance::Init()
{
	Super::Init();
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
