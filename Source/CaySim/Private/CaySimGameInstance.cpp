#include "CaySimGameInstance.h"
#include "CaySim.h"
#include "HepgoClient.h"
#include "SessionWizard.h"
#include "ReplayPlayer.h"
#include "Kismet/GameplayStatics.h"

void UCaySimGameInstance::Init()
{
	Super::Init();
	Hepgo = NewObject<UHepgoClient>(this, TEXT("HepgoClient"));
	Wizard = NewObject<UCaySimSessionWizard>(this, TEXT("SessionWizard"));
	Replay = NewObject<UCaySimReplayPlayer>(this, TEXT("ReplayPlayer"));
	Wizard->Init(Hepgo);
	UE_LOG(LogCaySim, Log, TEXT("CAYSIM oyun örneği hazır"));
}

void UCaySimGameInstance::Shutdown()
{
	if (Hepgo) Hepgo->Disconnect();
	Super::Shutdown();
}

UCaySimGameInstance* UCaySimGameInstance::Get(const UObject* WorldContextObject)
{
	return Cast<UCaySimGameInstance>(UGameplayStatics::GetGameInstance(WorldContextObject));
}
