// "Bulutta uç": simumav.com sunucu SITL'i (POST /api/fly/start → WS /ws/fly/{sim_id} → POST /api/fly/{fid}/stop).
#pragma once

#include "CoreMinimal.h"
#include "SmvFlightLink.h"
#include "Containers/Ticker.h"
#include "SmvCloudFlightLink.generated.h"

class USimumavClient;
class FSmvSocket;

UCLASS()
class SIMUMAV_API USmvCloudFlightLink : public USmvFlightLink
{
	GENERATED_BODY()

public:
	void Init(USimumavClient* InClient) { Client = InClient; }
	/** Karşılaşma uçuşu: sunucu uçuşu zaten açtı (flight_id) → yalnız bağlan. */
	void AttachExisting(const FString& FlightId);

	virtual void Start(const FSmvFlightRequest& Request) override;
	virtual void Stop(const FString& Reason) override;
	virtual void SendRc(const TArray<int32>& Channels) override;
	virtual FString Command(const FString& Name, const TSharedPtr<FJsonObject>& Params) override;
	virtual double Now() const override;
	virtual bool IsReady() const override { return bReady; }
	virtual FString MissionPlannerInfo() const override;
	virtual ESmvFlightBackend Backend() const override { return ESmvFlightBackend::Cloud; }
	virtual void BeginDestroy() override;

	/** Halka sonucu (stop gövdesi `gates`). */
	int32 GatesPassed = 0, GatesMissed = 0, Laps = 0;
	double BestLapS = 0;

private:
	UPROPERTY() TObjectPtr<USimumavClient> Client;
	TSharedPtr<FSmvSocket> Ws;
	bool bReady = false;
	bool bStopping = false;
	double ClockOffset = 0;
	bool bClockSynced = false;
	TArray<int32> PendingRc;
	bool bRcPending = false;
	double LastRcAt = 0;
	FTSTicker::FDelegateHandle TickHandle;

	void Connect();
	void HandleWs(const TSharedPtr<FJsonObject>& O);
	bool OnTick(float Dt);
	void FetchSummary(const FString& Reason);
};
