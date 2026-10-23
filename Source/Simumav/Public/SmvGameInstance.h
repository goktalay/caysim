// Simumav oyun örneği — CAYSIM örneğinden türer (HEPGO kurumsal istemcisi aynı yapıyla kullanılabilir kalır).
// Uygulama ömrü boyunca: simumav.com istemcisi, veri hizmeti, akış.
#pragma once

#include "CoreMinimal.h"
#include "CaySimGameInstance.h"
#include "SmvGameInstance.generated.h"

class USimumavClient;
class USmvDataService;
class USmvFlow;

UCLASS()
class SIMUMAV_API USmvGameInstance : public UCaySimGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	UFUNCTION(BlueprintPure, Category = "Simumav") USimumavClient* GetClient() const { return Client; }
	UFUNCTION(BlueprintPure, Category = "Simumav") USmvDataService* GetData() const { return Data; }
	UFUNCTION(BlueprintPure, Category = "Simumav") USmvFlow* GetFlow() const { return Flow; }

	UFUNCTION(BlueprintPure, Category = "Simumav", meta = (WorldContext = "WorldContextObject"))
	static USmvGameInstance* GetSmv(const UObject* WorldContextObject);

	/** Grafik kalitesi (0–4) — ayarlardan. */
	void ApplyGraphicsQuality(int32 Q) const;

private:
	UPROPERTY() TObjectPtr<USimumavClient> Client;
	UPROPERTY() TObjectPtr<USmvDataService> Data;
	UPROPERTY() TObjectPtr<USmvFlow> Flow;
};
