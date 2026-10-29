// Uçuş bağlantısı soyutlaması: oyun katmanı (dron aktörü, HUD, uyarılar, kumanda) fiziğin nerede çalıştığını bilmez.
//   USmvCloudFlightLink  — simumav.com sunucusundaki SITL (WS /ws/fly/{sim_id}); giriş gerekir ("Bulutta uç").
//   USmvLocalFlightLink  — oyunla dağıtılan ArduPilot SITL bu bilgisayarda (misafir kip; MAVLink'i oyun konuşur).
// İki uygulama da sunucu WS sözleşmesindeki ileti şekillerini üretir: `tele` (tipli) + `event` / `state` / `launch` / `crash` / `warn` /
// `target` / `engage` / `servo_trigger` / `res` / `end` (JSON). Böylece HUD ve özet tek yoldan beslenir.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HAL/PlatformTime.h"   // satır içi FPlatformTime::Seconds
#include "CaySimTypes.h"
#include "SimumavTypes.h"
#include "SmvFlightLink.generated.h"

/** Fizik sensörleri (UE ışın izleri; SmvPhysics::Scan) — otopilota iletilecek ölçümler. Mesafe m, −1 = menzil içinde engel yok. */
struct FSmvRangeScan
{
	float DownM = -1.f;          // aşağı menzil bulucu (gövde ekseni)
	float UpM = -1.f;            // yukarı
	TArray<float> SectorM;       // 360° yakınlık: 0 = burun, saat yönünde eşit aralık (72 × 5° ya da 8 × 45°)
	float MinM = 0.2f;
	float DownMaxM = 40.f;
	float ProxMaxM = 30.f;
};

/** Otopilot görev noktası (MISSION_ITEM_INT; mini harita) */
struct FSmvMissionWp { double Lat = 0, Lon = 0; float Alt = 0; uint16 Cmd = 16; uint16 Seq = 0; uint8 Frame = 3; };

UCLASS(Abstract)
class SIMUMAV_API USmvFlightLink : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTele, const FCaySimTelemetry&, const FSmvTeleExtra&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessage, const TSharedPtr<FJsonObject>&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStarted, bool /*bOk*/);

	/** Oturumu başlatır. Asenkron: OnStarted(bOk) → ardından `state` HAZIR ve `tele` akışı. */
	virtual void Start(const FSmvFlightRequest& Request) PURE_VIRTUAL(USmvFlightLink::Start, );
	/** Uçuşu bitirir; özet OnSummary ile gelir. Reason: user | crash | servo_trigger | timeout | match */
	virtual void Stop(const FString& Reason) PURE_VIRTUAL(USmvFlightLink::Stop, );
	/** RC 8 kanal 1000–2000 (0 = dokunma; 8×0 = bırak). */
	virtual void SendRc(const TArray<int32>& Channels) PURE_VIRTUAL(USmvFlightLink::SendRc, );
	/** Sunucu komut adları (arm, disarm, mode{mode}, takeoff{alt}, rtl, set_home, speed{factor}, servo_set{ch,pwm}, servo_trigger{…},
	 *  motor_cut{on}, reset, crash{kind,speed}, engage{action,i}, camera{type}, gpws{events,cfg}). Dönüş: istek kimliği (rid). */
	virtual FString Command(const FString& Name, const TSharedPtr<FJsonObject>& InParams) PURE_VIRTUAL(USmvFlightLink::Command, return FString(););
	/** tele.ts ile aynı ölçekte şimdiki zaman (Unix sn). */
	virtual double Now() const PURE_VIRTUAL(USmvFlightLink::Now, return 0.0;);
	virtual bool IsReady() const PURE_VIRTUAL(USmvFlightLink::IsReady, return false;);
	/** Mission Planner bağlantı bilgisi (ör. "UDP 127.0.0.1:14550 çıkışı" ya da "TCP simumav.com:15612"). */
	virtual FString MissionPlannerInfo() const { return FString(); }
	virtual ESmvFlightBackend Backend() const PURE_VIRTUAL(USmvFlightLink::Backend, return ESmvFlightBackend::LocalSitl;);
	/** Fizik sensörleri otopilota iletilebiliyor mu (yalnız yerel SITL: MAVLink DISTANCE_SENSOR / OBSTACLE_DISTANCE). */
	virtual bool AcceptsRangeSensors() const { return false; }
	virtual void SendRangeSensors(const FSmvRangeScan& /*Scan*/) {}
	/** Otopilotta yüklü görev (mini harita çizimi): konumlu noktalar sırayla; sürüm her yeni indirmede artar. Yoksa boş. */
	virtual const TArray<FSmvMissionWp>& Mission() const { static const TArray<FSmvMissionWp> None; return None; }
	virtual int32 MissionVersion() const { return 0; }
	/** Şu anki görev noktası (MISSION_CURRENT; −1 bilinmiyor) */
	virtual int32 MissionCurrent() const { return -1; }

	const FSmvFlightInfo& Info() const { return FlightInfo; }
	const FSmvFlightRequest& RequestData() const { return Req; }
	/** Son tele (ham). */
	const FCaySimTelemetry& LastTele() const { return Tele; }
	const FSmvTeleExtra& LastExtra() const { return Extra; }
	/** Bağlantı yaşı (ms) — GPWS LINK. */
	double LinkAgeMs() const { return LastTeleWall > 0 ? (FPlatformTime::Seconds() - LastTeleWall) * 1000.0 : -1.0; }

	FOnTele OnTele;
	FOnMessage OnMessage;
	FOnStarted OnStarted;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSummary, const FSmvFlightSummary&);
	FOnSummary OnSummary;

	/** Kolay kurucu: {t:"cmd"...} biçiminde parametre nesnesi. */
	static TSharedRef<FJsonObject> Params() { return MakeShared<FJsonObject>(); }

protected:
	FSmvFlightRequest Req;
	FSmvFlightInfo FlightInfo;
	FCaySimTelemetry Tele;
	FSmvTeleExtra Extra;
	double LastTeleWall = 0;
	int32 RidCounter = 0;

	FString NextRid() { return FString::Printf(TEXT("r%d"), ++RidCounter); }
	void EmitTele() { LastTeleWall = FPlatformTime::Seconds(); OnTele.Broadcast(Tele, Extra); }
	void EmitMessage(const TSharedRef<FJsonObject>& M) { OnMessage.Broadcast(M); }
	void EmitEvent(const FString& Text, int32 Severity);
	void EmitState(const FString& State);
	void EmitResult(const FString& Rid, const FString& Name, bool bOk, const FString& Error = FString());
};
