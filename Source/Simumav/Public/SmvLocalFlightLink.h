// Misafir kip fiziği: oyunla dağıtılan ArduPilot SITL (ArduCopter/ArduPlane/ArduRover/ArduHeli 4.7.2, resmi Windows derlemesi) alt süreç olarak
// bu bilgisayarda çalışır; oyun MAVLink'i TCP SERIAL0 üzerinden kendisi konuşur. Sunucu oturum mantığının istemci karşılığı:
//   • süreç: drone_sitl._baslat_port (komut satırı, temiz eeprom, varsayilan.parm, 10'luk port bloğu, 40 sn bağlanma denemesi)
//   • ilk HEARTBEAT: flightsim._ilk_heartbeat (akış istekleri, HOME, SERVOn_FUNCTION, kayıtlı parametreler, rüzgâr, RC/failsafe, GPS RTK,
//     kalkış/flap/düzen seti, INTERCEPTOR seti, azami hız, SIM_SPEEDUP)
//   • tele üretimi: flightsim._isle (alan eşlemesi, 18 ms SITL saati kapısı, 0,25 sn bekçi), güç/yakıt (SmvPower), esinti 1 Hz
//   • komutlar: arm/disarm/mode/takeoff (kopter GUIDED+NAV_TAKEOFF; sabit kanat kalkış dizisi wheels/ramp/vehicle; VTOL/INTERCEPTOR dikey),
//     rtl, set_home, speed, servo_set, motor_cut, reset (Sıfırla korunumu), crash, servo_trigger, engage (hedef kilidi)
//   • Mission Planner: ham MAVLink UDP köprüsü (varsayılan 127.0.0.1:14550 çıkışı; ilk yanıt veren adres kilitlenir, 30 sn sessizlikte kalkar)
// Yerelde OLMAYANLAR (docs/ozellik-esligi.md): arazi sunumu (TERRAIN_DATA), HEPGO yamalı ikili parametreleri (SIM_SRV_REV, SIM_PLN_THR_MUL),
// .BIN'in sunucu "Uçuş Logları"na taşınması (yerelde Saved/Simumav/Logs'a kopyalanır).
#pragma once

#include "CoreMinimal.h"
#include "SmvFlightLink.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"   // FProcHandle (IWYU: CoreMinimal getirmez)
#include "HAL/PlatformTime.h"
#include "SmvLocalFlightLink.generated.h"

class FSocket;
class FInternetAddr;
namespace SmvMav { class FParser; struct FFrame; }
namespace SmvCore { class FPowerState; class FTargetTracker; class FServoTrigger; struct FSitlSession; }

UCLASS(Config = Game)
class SIMUMAV_API USmvLocalFlightLink : public USmvFlightLink
{
	GENERATED_BODY()

public:
	USmvLocalFlightLink();
	virtual ~USmvLocalFlightLink();
	virtual void BeginDestroy() override;

	virtual void Start(const FSmvFlightRequest& Request) override;
	virtual void Stop(const FString& Reason) override;
	virtual void SendRc(const TArray<int32>& Channels) override;
	virtual FString Command(const FString& Name, const TSharedPtr<FJsonObject>& InParams) override;
	virtual double Now() const override;
	virtual bool IsReady() const override { return bHeartbeatSeen; }
	virtual FString MissionPlannerInfo() const override;
	virtual ESmvFlightBackend Backend() const override { return ESmvFlightBackend::LocalSitl; }
	virtual bool AcceptsRangeSensors() const override { return Tcp != nullptr && bHeartbeatSeen && (bSensRng || bSensPrx); }
	virtual void SendRangeSensors(const FSmvRangeScan& Scan) override;

	/** Mission Planner UDP çıkışı (ayarlardan; Start'tan önce). */
	void SetMissionPlannerOut(bool bEnable, const FString& Host, int32 Port) { bMpOut = bEnable; MpHost = Host; MpPort = Port; }

	/** SITL ikili klasörü — boşsa <Proje>/ThirdParty/ArduPilotSITL/Win64. */
	UPROPERTY(Config) FString SitlDirOverride;
	/** Port bloğu taban adresi (sunucuyla aynı: 15670, 10'luk adım). */
	UPROPERTY(Config) int32 PortBase = 15670;
	/** SITL ayağa kalkma + bağlanma süresi sınırı (sn). */
	UPROPERTY(Config) float ConnectTimeoutS = 40.f;

	/** İkililer kurulu mu (yoksa arayüz "Bulutta uç" önerir). */
	static bool SitlAvailable(FString* OutDir = nullptr);
	static FString SitlDir();

private:
	// ---- süreç ----
	FProcHandle Proc;
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	FString WorkDir;
	int32 BasePort = 0;
	double ProcStartedAt = 0;
	FString ProcLogTail;
	bool LaunchSitl(bool bClean);
	void KillSitl();
	int32 FindFreePortBlock() const;
	static bool PortFree(int32 Port);

	// ---- TCP (SERIAL0) ----
	FSocket* Tcp = nullptr;
	double NextConnectAt = 0;
	bool TryConnect();
	void CloseTcp();
	void SendFrame(uint32 MsgId, const uint8* Payload, int32 Len);
	void SendRaw(const uint8* Data, int32 Num);
	uint8 Seq = 0;

	// ---- MAVLink ----
	// Saf C++ çekirdek nesneleri (tam tür yalnız .cpp'de; ham işaretçi + açık silme — UHT üretimi eksik türle derlenebilsin)
	SmvMav::FParser* Parser = nullptr;
	SmvMav::FParser* MpParser = nullptr;
	uint8 TargetSys = 1, TargetComp = 1;
	uint8 VehicleMavType = 2;
	bool bHeartbeatSeen = false;
	FString ModeVehicle;      // copter | plane | rover
	void HandleFrame(const SmvMav::FFrame& F);
	void OnFirstHeartbeat();
	void RequestStreams(bool bFull);
	bool bStreamFallback = false;
	double LastStreamRefresh = 0, LastGcsHb = 0, LastGust = 0;
	void ParamSet(const FString& Name, float Value, uint8 Type = 9);
	void CommandLong(uint16 Cmd, float P1 = 0, float P2 = 0, float P3 = 0, float P4 = 0, float P5 = 0, float P6 = 0, float P7 = 0);
	bool SetMode(const FString& Name);

	// ---- tele ----
	double LastAttWall = 0, LastPublishWall = 0;
	int64 LastPublishTbm = -1000;
	// tele.ts: SITL önyükleme saatinden (tbm) türetilir, duvar saatine yavaşça bağlanır — UDP varış titremesi enterpolasyona girmesin
	double LastTs = 0;
	int64 LastTsTbm = -1;
	TArray<int32> Servo16;
	void Publish(bool bFromAttitude);
	double HomeAltMsl = 0;
	bool bHomeKnown = false;

	// ---- oturum mantığı ----
	SmvCore::FSitlSession* Session = nullptr;
	SmvCore::FPowerState* Power = nullptr;
	SmvCore::FTargetTracker* Targets = nullptr;
	SmvCore::FServoTrigger* ServoTrig = nullptr;
	void FreeCore();
	TArray<TPair<FString, float>> SessionUserParams;   // oturumda yazılan (MP/komut) — Sıfırla sonrası yeniden
	bool bAfterReset = false;
	bool bResetting = false;
	FString LaunchPhase;     // armed | launch | released
	double LaunchT0 = 0;
	bool bMotorCut = false;
	bool bCrashed = false;
	/** Fizik sensörleri (UE ışın izleri → DISTANCE_SENSOR / OBSTACLE_DISTANCE) ve SITL parametreleri (RNGFND1 / PRX1 / AVOID) */
	bool bSensRng = false, bSensPrx = false, bSensAvoid = true;
	/** Araç UE'de yapı/arazi üstünde durdu, motorlar durduruldu (SITL zemini daha aşağıda): SITL "Hit ground" çarpışma sayılmaz */
	bool bUeRest = false;
	double CrashCandidateUntil = 0;
	double CrashCandidateSpeed = 0;
	double FirstArmWall = 0;
	double StartedWall = 0;
	FString EndReason;
	bool bStopped = false;
	void Crash(const FString& Reason, double Speed);
	void ForcedDisarm();
	void LaunchEvent(const FString& Phase);
	void StartTakeoff(float AltM);
	void TickLaunch();
	void BeginReset();
	// ---- helikopter motor kilidi (ArduHeli her zaman RC8 MOTOR_INTERLOCK kullanır: ARM için düşük, rotor için yüksek) ----
	bool bHeliInterlock = false;
	double NextInterlockAt = 0;
	/** RC8 = ARM'lı ? 2000 : 1000 (yalnız ArduHeli; RC_OVERRIDE_TIME dolmasın diye 0,25 sn'de bir). */
	void PumpHeliInterlock(double Wall, bool bForce = false);

	// ---- zamanlanmış işler (kalkış dizileri) ----
	struct FTimed { double At; TFunction<void()> Fn; };
	TArray<FTimed> Timed;
	void After(double DelayS, TFunction<void()> Fn) { Timed.Add({ FPlatformTime::Seconds() + DelayS, MoveTemp(Fn) }); }

	// ---- Mission Planner UDP köprüsü ----
	bool bMpOut = false;
	FString MpHost = TEXT("127.0.0.1");
	int32 MpPort = 14550;
	FSocket* Udp = nullptr;
	TSharedPtr<FInternetAddr> MpAddr;
	TSharedPtr<FInternetAddr> MpLocked;
	double MpLastRx = 0;
	bool bGcsConnected = false;
	void OpenMp();
	void PumpMp();

	FTSTicker::FDelegateHandle TickHandle;
	bool OnTick(float Dt);
	void ReadProcPipe();
	void Finish();
};
