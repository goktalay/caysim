// HEPGO istemcisi — sözleşme v1 (docs/caysim-api.md): REST (Bearer) + WebSocket (?key=), yeniden bağlanma, komut, RC
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "Containers/Ticker.h"
#include "CaySimTypes.h"
#include "HepgoClient.generated.h"

class IWebSocket;

/** İstemci kodu `<domain>/<key>` (§0): ilk `/` konumundan ikiye bölünür. */
USTRUCT(BlueprintType)
struct CAYSIM_API FHepgoClientCode
{
	GENERATED_BODY()
	/** ör. caymaz.app ya da 10.0.0.5:8000 */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Domain;
	/** Bearer anahtar `[A-Za-z0-9_-]{43}`; günlüğe yazılmaz */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Key;
	/** https/wss (varsayılan); yerel ağ ya da `http://` ön eki → http/ws */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bSecure = true;

	FString HttpBase() const;   // https://<domain>/api/caysim/v1
	FString WsBase() const;     // wss://<domain>/ws/caysim/v1
	FString Origin() const;     // https://<domain>
	bool IsValid() const { return !Domain.IsEmpty() && !Key.IsEmpty(); }

	static bool Parse(const FString& Text, FHepgoClientCode& Out, FString& Error);
};

UENUM(BlueprintType)
enum class EHepgoWsState : uint8
{
	Disconnected,
	Connecting,
	Connected,
	Reconnecting,
	/** 4403/4404/4429 ya da `end`: yeniden denenmez */
	Failed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHepgoHelloDelegate, bool, bOk, const FCaySimHello&, Hello);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHepgoErrorDelegate, const FString&, Code, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoCentersDelegate, const TArray<FCaySimCenter>&, Centers);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoTypesDelegate, const TArray<FCaySimDroneTypeInfo>&, Types);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoModelsDelegate, const TArray<FCaySimModelInfo>&, Models);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoLogsDelegate, const TArray<FCaySimLogInfo>&, Logs);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoWorldDefaultsDelegate, const FCaySimWorld&, World);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHepgoSessionDelegate, bool, bOk, const FCaySimSession&, Session);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoSessionsDelegate, const TArray<FCaySimSession>&, Sessions);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHepgoMavlinkDelegate, bool, bOk, const FCaySimMavlinkInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoTelemetryDelegate, const FCaySimTelemetry&, Tele);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoEventDelegate, const FCaySimEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoStateDelegate, const FCaySimSession&, Session);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoLaunchDelegate, const FCaySimLaunchEvent&, Launch);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoEndDelegate, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoWsStateDelegate, EHepgoWsState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FHepgoCmdResultDelegate, const FString&, Name, const FString&, Rid, bool, bOk, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHepgoReplayCtlDelegate, const FString&, Action);

DECLARE_DELEGATE_TwoParams(FHepgoBytesNative, bool /*bOk*/, const TArray<uint8>& /*Bytes*/);
DECLARE_DELEGATE_TwoParams(FHepgoJsonNative, bool /*bOk*/, const TSharedPtr<FJsonObject>& /*Obj*/);

/**
 * HEPGO `/api/caysim/v1` istemcisi (UCaySimGameInstance ömrü). Tüm geri çağrımlar oyun iş parçacığında.
 * Hata biçimi: HTTP durumu + `{"detail": {"error": "<kod>", "message": "<metin>"}}` → OnError(kod, metin).
 */
UCLASS(BlueprintType)
class CAYSIM_API UHepgoClient : public UObject
{
	GENERATED_BODY()

public:
	UHepgoClient();
	virtual void BeginDestroy() override;

	/** CAYSIM sürümü — `X-Caysim-Ver` */
	static const TCHAR* ClientVersion() { return TEXT("0.1.0"); }

	// ---- kimlik ----
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") bool SetClientCode(const FString& Text, FString& Error);
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") FHepgoClientCode GetClientCode() const { return Code; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") bool HasClientCode() const { return Code.IsValid(); }
	/** Göreli yol (`/api/caysim/v1/...` ya da `/centers`) → tam URL. */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") FString ResolveUrl(const FString& PathOrUrl) const;
	/** Son `/hello` yanıtı. */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") FCaySimHello GetHello() const { return LastHello; }

	// ---- REST (§2) ----
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void Hello();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchCenters();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchTypes();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchModels(const FString& TypeKey);
	/** `GET /models` yanıtındaki `defaults:{type: builtin_id}` */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") FString GetDefaultModelFor(const FString& TypeKey) const;
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchWorldDefaults();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchLogs(const FString& Query);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchSessions();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchSession(const FString& SessionId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void FetchMavlinkInfo(const FString& SessionId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO")
	void CreateSitlSession(const FString& TypeKey, int32 CenterId, const FCaySimWorld& World, const FString& ModelId, const FCaySimModelOrient& Orient, bool bSendOrient);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO")
	void CreateReplaySession(int32 LogId, const FCaySimWorld& World, const FString& ModelId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void DeleteSession(const FString& SessionId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void SendCommandRest(const FString& SessionId, const FString& Name, const FString& ParamsJson);
	/** `GET /g3d-key` → anahtar; kapalıysa (404) sessizce false. */
	void FetchGoogle3dKey(TFunction<void(bool, const FString&)> Done);
	/** `GET /terrain?lat&lon&size_m` / `GET /osm?...` ham JSON (HEPGO arazi vekili). */
	void FetchTerrain(double Lat, double Lon, float SizeM, FHepgoJsonNative Done);
	void FetchOsm(double Lat, double Lon, float SizeM, FHepgoJsonNative Done);
	/** Ham bayt indirme (Bearer): logo, GLB, thumb, tile, replay gz. */
	void FetchBytes(const FString& PathOrUrl, FHepgoBytesNative Done, bool bSilent = false);
	void GetJson(const FString& Path, FHepgoJsonNative Done, bool bSilent = false);

	// ---- WebSocket (§3) ----
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void ConnectSession(const FString& SessionId);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void Disconnect();
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") EHepgoWsState GetWsState() const { return WsState; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") FString GetConnectedSessionId() const { return WsSessionId; }
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString SendCommand(const FString& Name, const FString& ParamsJson);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString CmdArm(bool bArm);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString CmdMode(const FString& ModeName);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString CmdTakeoff(float AltM);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString CmdRtl();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString CmdReset();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString CmdSetHome(double Lat, double Lon, double Alt);
	/** İstemci çarpışma algıladı (§5 `crash`): zorla disarm + olay. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") FString CmdCrash();
	/** RC 8 kanal 1000–2000 (0 = dokunma; hepsi 0 = override bırak). `limits.rc_hz`'e göre kısılır. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void SendRc(const TArray<int32>& Channels);
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|HEPGO") void ReleaseRc();

	UFUNCTION(BlueprintPure, Category = "CAYSIM|HEPGO") float GetRttMs() const { return RttMs; }
	/** `tele.ts` − yerel saat (EMA). */
	double GetServerClockOffset() const { return ServerClockOffset; }
	/** Yerel UTC + ofset = tahminî sunucu zamanı (Unix sn). */
	double NowServer() const;
	static double LocalUnixNow();

	// ---- olaylar ----
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoHelloDelegate OnHello;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoErrorDelegate OnError;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoCentersDelegate OnCenters;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoTypesDelegate OnTypes;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoModelsDelegate OnModels;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoLogsDelegate OnLogs;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoWorldDefaultsDelegate OnWorldDefaults;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoSessionDelegate OnSessionCreated;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoSessionDelegate OnSessionFetched;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoSessionsDelegate OnSessions;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoMavlinkDelegate OnMavlinkInfo;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoWsStateDelegate OnWsStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoStateDelegate OnWsHello;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoTelemetryDelegate OnTelemetry;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoEventDelegate OnEvent;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoStateDelegate OnSessionState;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoLaunchDelegate OnLaunch;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoEndDelegate OnEnd;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoCmdResultDelegate OnCommandResult;
	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|HEPGO") FHepgoReplayCtlDelegate OnReplayControl;

	DECLARE_MULTICAST_DELEGATE_OneParam(FTelemetryNative, const FCaySimTelemetry&);
	FTelemetryNative OnTelemetryNative;

	// ---- ayarlar ----
	/** Geri çekilme dizisi 0,5 → 1 → 2 → 4 → 8 → 10 sn (§3.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HEPGO") float MaxBackoffS = 10.f;
	/** `ping` 10–30 sn'de bir (§3.2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HEPGO") float PingIntervalS = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HEPGO") float HttpTimeoutS = 20.f;
	/** `tele` gelmeden geçen süre (sn) — bağlantı ölü sayılır ve yeniden bağlanılır (bekçi ≥ 4 Hz olduğundan 5 sn bol). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|HEPGO") float TeleTimeoutS = 5.f;

private:
	FHepgoClientCode Code;
	FCaySimHello LastHello;
	TMap<FString, FString> ModelDefaults;

	// HTTP
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Verb, const FString& Url) const;
	void RequestJson(const FString& Verb, const FString& Path, const FString& Body, FHepgoJsonNative Done, bool bSilent = false);
	/** Sarmalanmış liste: `{<Key>:[…]}`. */
	void RequestList(const FString& Path, const FString& Key, TFunction<void(bool, const TArray<TSharedPtr<FJsonValue>>&, const TSharedPtr<FJsonObject>&)> Done);
	static bool ParseJson(const FString& Text, TSharedPtr<FJsonObject>& Out);
	void ReportError(const FString& Code, const FString& Msg);
	void ParseError(int32 Status, const FString& Body, FString& OutCode, FString& OutMsg) const;

	// WS
	TSharedPtr<IWebSocket> Ws;
	FString WsSessionId;
	EHepgoWsState WsState = EHepgoWsState::Disconnected;
	bool bWantConnected = false;
	int32 BackoffIndex = 0;
	double NextReconnectAt = 0.0;
	double LastPingAt = 0.0;
	double LastRxAt = 0.0;
	double PingSentAt = 0.0;
	float RttMs = 0.f;
	double ServerClockOffset = 0.0;
	bool bClockSynced = false;
	int32 RidCounter = 0;

	// RC kısma
	TArray<int32> PendingRc;
	bool bRcPending = false;
	double LastRcSentAt = 0.0;

	FTSTicker::FDelegateHandle TickHandle;
	bool Tick(float Dt);

	void SetWsState(EHepgoWsState S);
	void OpenWs();
	void CloseWs();
	void ScheduleReconnect(double DelayOverrideS = -1.0);
	void TryReconnect();
	void HandleWsMessage(const FString& Msg);
	void SendWsJson(const TSharedRef<FJsonObject>& Obj);
	static FString JsonToString(const TSharedRef<FJsonObject>& Obj);
	static TSharedPtr<FJsonObject> ParamsFromString(const FString& ParamsJson);
	static FString PlatformName();
};
