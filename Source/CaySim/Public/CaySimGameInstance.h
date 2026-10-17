// Oyun örneği — HEPGO istemcisi ve sihirbaz uygulama ömrü boyunca burada yaşar
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "CaySimGameInstance.generated.h"

class UHepgoClient;
class UCaySimSessionWizard;
class UCaySimReplayPlayer;

UCLASS()
class CAYSIM_API UCaySimGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	UFUNCTION(BlueprintPure, Category = "CAYSIM") UHepgoClient* GetHepgo() const { return Hepgo; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM") UCaySimSessionWizard* GetWizard() const { return Wizard; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM") UCaySimReplayPlayer* GetReplay() const { return Replay; }

	/** Herhangi bir dünya bağlamından erişim. */
	UFUNCTION(BlueprintPure, Category = "CAYSIM", meta = (WorldContext = "WorldContextObject"))
	static UCaySimGameInstance* Get(const UObject* WorldContextObject);

private:
	UPROPERTY() TObjectPtr<UHepgoClient> Hepgo;
	UPROPERTY() TObjectPtr<UCaySimSessionWizard> Wizard;
	UPROPERTY() TObjectPtr<UCaySimReplayPlayer> Replay;
};
