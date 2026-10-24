// Çevrim içi karşılaşma istemcisi (giriş gerekir): salon (davet kodu + şifre), yapay zekâ rakipli karşılaşma, hazır / başlat, WS /ws/match/{id}
// (pos 10 Hz, ring, finish, state, countdown, dogfight, result). Hakem sunucudur; istemci yalnız gösterir ve kendi uçuşunu uçurur
// (sunucunun açtığı uçuşa USmvCloudFlightLink::AttachExisting ile bağlanır).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SimumavTypes.h"
#include "SmvMatchClient.generated.h"

class USimumavClient;
class FSmvSocket;

/** Diğer oyuncuların anlık konumu (`pos.p[]`). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvGhostPos
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int64 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Lat = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Lon = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double AltMsl = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Yaw = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Roll = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Pitch = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Gs = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Rings = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Ts = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSmvMatchUpdated, const FSmvMatch&);
DECLARE_MULTICAST_DELEGATE_OneParam(FSmvMatchPositions, const TArray<FSmvGhostPos>&);
DECLARE_MULTICAST_DELEGATE_OneParam(FSmvMatchEvent, const TSharedPtr<FJsonObject>&);

UCLASS()
class SIMUMAV_API USmvMatchClient : public UObject
{
	GENERATED_BODY()

public:
	/** Gövde .cpp'de: USimumavClient burada yalnız ileri bildirimli (TObjectPtr ataması tam tür ister). */
	void Init(USimumavClient* InClient);
	virtual void BeginDestroy() override;

	/** kind: rings | dogfight; Rings/Dogfight: sunucu ayar nesneleri (null → varsayılan). */
	void Create(const FSmvFlightRequest& Draft, const FString& Kind, const FString& Password, int32 MaxPlayers, TSharedPtr<FJsonObject> Rings, TSharedPtr<FJsonObject> Dogfight);
	void CreateAi(const FSmvFlightRequest& Draft, const FString& Kind, const FString& Level, int32 Bots, TSharedPtr<FJsonObject> Rings, TSharedPtr<FJsonObject> Dogfight);
	void Join(const FString& Code, const FString& Password);
	void SetReady(bool bReady);
	void Start();
	void Leave();
	void Cancel();
	void AddBot(const FString& Level);
	void Refresh();
	/** Davet kabulünün döndürdüğü karşılaşma anlık görüntüsüne bağlan (WS açılır) */
	void Attach(const TSharedPtr<FJsonObject>& O) { if (O.IsValid() && O->HasField(TEXT("id"))) Apply(O); }

	const FSmvMatch& Current() const { return Snapshot; }
	bool HasMatch() const { return !Snapshot.Id.IsEmpty(); }
	FString LastError;

	FSmvMatchUpdated OnUpdated;
	FSmvMatchPositions OnPositions;
	FSmvMatchEvent OnEvent;

private:
	UPROPERTY() TObjectPtr<USimumavClient> Client;
	TSharedPtr<FSmvSocket> Ws;
	FSmvMatch Snapshot;
	void Apply(const TSharedPtr<FJsonObject>& O);
	void Post(const FString& Path, const TSharedRef<FJsonObject>& Body);
	void OpenWs();
	void HandleWs(const TSharedPtr<FJsonObject>& O);
	TSharedRef<FJsonObject> BaseBody(const FSmvFlightRequest& Draft, const FString& Kind, TSharedPtr<FJsonObject> Rings, TSharedPtr<FJsonObject> Dogfight) const;
};
