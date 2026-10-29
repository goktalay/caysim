// Simumav oyun kipi — arayüz kabuğunu (SSmvShell) açar; akış Uçuş'a geçince sahneyi kurar: dünya (Cesium gerçek arazi / prosedürel),
// dron, pilot piyonu, HUD, halkalar/kapılar, hedefler, yerel yarış botları ya da çevrim içi rakipler, uçuş uyarıları, ses; bağlantı
// olaylarını dağıtır; çarpışmayı Unreal fizik sorgularıyla algılar (gövde süpürmesi, SmvPhysics) ve engel sensörlerini SITL'e iletir.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CaySimTypes.h"
#include "SimumavTypes.h"
#include "SmvMatchClient.h"
#include "SmvFlow.h"
#include "SmvVideoRec.h"
#include "SSmvFlightUi.h"
#include "SmvLoadGate.h"
#include "SmvGameMode.generated.h"

class ACaySimWorldSetup;
class ASmvDroneActor;
class ASmvGateActor;
class ASmvProcTerrain;
class ASmvPilotPawn;
class ULevelStreamingDynamic;
class UCaySimHudWidget;
class USmvHudOverlay;
class USmvFlow;
class USmvFlightLink;
class USmvToneSynth;
class USmvMusicSynth;
class USoundBase;
class ASmvMenuScene;
class ASmvFpvCourseActor;
class ASmvPilotFigure;
class SSmvShell;
class SWidget;
struct FSmvContactEvent;
class ULevel;
namespace SmvCore { class FGpws; class FAlertPolicy; class FRingTracker; class FDogfight; class FBotPilot; struct FRing; }

UCLASS()
class SIMUMAV_API ASmvGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASmvGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- duraklatma menüsü / kabuk eylemleri ----
	void TogglePause();
	bool IsPausedMenu() const { return bPauseMenu; }
	void Command(const FString& Name, TSharedPtr<FJsonObject> Params = nullptr);
	ASmvPilotPawn* GetPilot() const;
	/** Uçuş olay akışına bildirim (arayüzden) */
	void UiEvent(const FString& Text, int32 Sev) { PushEvent(Text, Sev); }
	FString MissionPlannerLine() const;
	/** Uçuş arayüzü düğmeleri (SSmvFlightUi): Mode{arg}, CamCycle, IntroBeep{count|go}, Help ya da pilot eylem adları (ArmToggle, Takeoff, Rtl, Land, Reset, Pause …). */
	void UiAction(const FString& Name, const FString& Arg = FString());
	/** Menü 3B sahnesi (uçuşta yok) */
	ASmvMenuScene* GetMenuScene() const { return MenuScene; }
	/** Arayüz tercihlerini uygula: ses mikseri (arayüz / müzik / uyarı / motor), kumanda modu, tuş atamaları, hareket azaltma */
	void ApplyUiPrefs();

protected:
	UPROPERTY() TObjectPtr<ACaySimWorldSetup> WorldSetup;
	UPROPERTY() TObjectPtr<ASmvProcTerrain> Terrain;
	/** Çevrimdışı bölge paketinden gerçek dünya (SmvRegionWorld) */
	UPROPERTY() TObjectPtr<class ASmvRegionWorld> RegionWorld;
	bool bRegionWorld = false;
	/** Fab haritası (parkur dünya kipi "fab:<anahtar>") — [0] kalıcı seviye, ardından onun alt seviyeleri; hepsi görünür olunca
	 *  merkezi orijine (SITL ev konumu) oturtulur */
	UPROPERTY() TArray<TObjectPtr<ULevelStreamingDynamic>> FabLevels;
	bool bFabSubsRequested = false;
	bool bFabPlaced = false;
	UFUNCTION() void HandleFabLevelShown();
	/** Fab Ultra Dynamic Sky aktörleri (düz dünyada yerleşik gök yerine) ve sahne dekoru (yalnız görsel statik ağlar) */
	UPROPERTY() TArray<TObjectPtr<AActor>> FabSkyActors;
	UPROPERTY() TArray<TObjectPtr<AActor>> FabDecor;
	/** Kodla kurulan şehirde yüklenen City Sample bina seviyeleri (yerleri baştan verilir; merkezleme yok) */
	UPROPERTY() TArray<TObjectPtr<ULevelStreamingDynamic>> CityLevels;
	/** Uçulan harita anahtarı (kayıt tablosu; başlangıç kaydırması / dekor için) */
	FString FabMapKey;
	/** Sahne dekoru: Origin (cm) çevresine; bTraceGround ise her modelin zemini aşağı ışınla bulunur (Fab haritası) */
	void SpawnFabDecor(const FVector& Origin = FVector::ZeroVector, bool bTraceGround = false);
	void PlaceFabLevels();
	/** Kodla kurulan şehir bloğu (European Buildings ağları + City Sample bina seviyeleri) */
	void SpawnFabCity();
	/** Fab/şehir aktörlerini ve seviyelerini kaldırır (uçuş sonu / yeni uçuş) */
	void ClearFabWorld();
	/** Şehir: City Sample bina seviyeleri görünür olunca pencere malzemeleri Interior Cubemap System iç mekân malzemesiyle
	 *  değiştirilir (gece/alacakaranlıkta aşırı parlayan pencereler; ayar CityWindowSwap) — bir kez */
	void TickCityWindows();
	/** Fare: imleç görünür, yalnız sağ tuşla yakalama, kilit yok; arayüz tıklamasından sonra klavye odağı oyuna (her karede) */
	void EnsureMouse();
	/** Uçak/VTOL FPV'de Fab "Aircraft HUD" (WBP_Jet_HUD; yeşil jet göstergesi: yunuslama/yatış merdiveni, pusula, irtifa, hız).
	 *  Hedef kilidi öğeleri her zaman gizli (silah benzeri davranış yok). Ayar [/Script/Simumav.SmvScenery] JetHud */
	void UpdateJetHud(const struct FCaySimTelemetry& T, class ASmvPilotPawn* Pw);
	UPROPERTY() TObjectPtr<class UUserWidget> JetHud;
	bool bJetHudTried = false;
	/** Şehir sokak dokusu: yol çizgileri, yaya geçitleri, sokak lambaları, meydan ağaçları, şehir dışı park ve ufuk dağları */
	void SpawnCityStreetscape(double Cell, double Road);
	bool bCityWindowsDone = false;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Drone;
	/** uçuş mini haritası yakalaması (açıkken kurulur) */
	UPROPERTY() TObjectPtr<class ASmvMapCam> MapCam;
	bool bMapWorldOff = false;
	void UpdateMap(FSmvFlightUiState& U);
public:
	/** duman testi: 0 harita kapalı, 1 mini harita, 2 büyük harita */
	void SmokeMap(int32 Mode);
protected:
	/** Oyun türü mantığı (yük bırakma / ilaçlama / tesis ve gemi savunma; SmvGameRulesActor) */
	UPROPERTY() TObjectPtr<class ASmvGameRulesActor> Rules;
	UPROPERTY() TArray<TObjectPtr<ASmvGateActor>> Gates;
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> Ghosts;       // botlar / rakipler
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> TargetActors; // hedefler
	UPROPERTY() TObjectPtr<UCaySimHudWidget> Hud;
	UPROPERTY() TObjectPtr<USmvHudOverlay> Overlay;
	UPROPERTY() TObjectPtr<USmvToneSynth> Tones;
	UPROPERTY() TObjectPtr<AActor> ToneHost;
	/** Arayüz sesleri (ayrı kazanç) ve menü müziği (sentez) */
	UPROPERTY() TObjectPtr<USmvToneSynth> UiTones;
	UPROPERTY() TObjectPtr<USmvMusicSynth> Music;
	UPROPERTY() TObjectPtr<ASmvMenuScene> MenuScene;
	/** Arayüz sesleri: Fab Airplane Controller & Instruments düğme/anahtar sesleri pakette varsa onlar (tık, aç/kapa, açılış),
	 *  yoksa / diğerleri için prosedürel ton */
	UPROPERTY() TMap<int32, TObjectPtr<USoundBase>> UiSounds;
	bool bUiSoundsLoaded = false;
	void PlayUiSound(int32 Kind);
	void UpdateMenuScene(ESmvScreen Screen);
	void UpdateMusic(ESmvScreen Screen);

private:
	TWeakObjectPtr<USmvFlow> Flow;
	TSharedPtr<SSmvShell> Shell;
	TSharedPtr<SSmvFlightUi> FlightUi;
	TSharedPtr<SWidget> FlightUiHost;
	TArray<FSmvUiLog> UiLog;
	double ArmConfirmUntil = 0;
	bool bScene = false;
	bool bPauseMenu = false;
	bool bCrashed = false;
	double SceneT0 = 0;
	FVector LastDronePos = FVector::ZeroVector;
	bool bHasLastPos = false;
	FSmvFlightRequest Req;

	// uyarılar
	SmvCore::FGpws* Gpws = nullptr;
	TArray<TSharedPtr<FJsonObject>> PendingGpws;
	double LastGpwsFlush = 0;
	bool bAlertUnacked = false;
	TArray<TPair<FString, bool>> ActiveAlerts;

	// halkalar (parkur kapıları / yerel yarış / çevrim içi karşılaşma)
	TArray<SmvCore::FRing>* Rings = nullptr;
	SmvCore::FRingTracker* MyTracker = nullptr;
	double RaceStartWall = 0;
	bool bRaceStarted = false;
	bool bRaceDone = false;
	double MyFinishT = -1;
	/** Hazır FPV parkuru (zaman denemesi) */
	UPROPERTY() TObjectPtr<ASmvFpvCourseActor> FpvActor;
	double FpvEndAt = 0;
	/** Sahnede RC kumanda tutan 3B pilot (ev konumunun 8 m gerisinde, 3 m solunda) */
	UPROPERTY() TObjectPtr<ASmvPilotFigure> PilotFigure;
	/** Sabit kanat kalkış / fırlatma düzeneği (rampa / araç / el / pist; yalnız görsel) */
	UPROPERTY() TObjectPtr<class ASmvLaunchRig> LaunchRig;
	/** Çarpma efekti: enkaz parçaları (elle balistik) */
	UPROPERTY() TArray<TObjectPtr<AActor>> Debris;
	TArray<FVector> DebrisVel;
	double DebrisGroundZ = 0, DebrisUntil = 0;
	void CrashFx();
	UPROPERTY() TObjectPtr<class ASmvSiteActor> Site;
	UPROPERTY() TObjectPtr<class ASmvWindSock> Sock;
	UPROPERTY() TObjectPtr<class ASmvHolidayDecor> Decor;
	bool bRadarOn = true, bEoOn = true;
	int32 EoSel = 0;          // 0 = kendi İHA, i>0 = hedef i
	int32 EoTypeIdx = 0;      // 0 gündüz, 1 termal, 2 gece
	uint32 EoFrame = 0;
	void UpdateSite(struct FSmvFlightUiState& U);
	double PathAt = 0, PathT0 = 0;
	TUniquePtr<FSmvVideoRec> VideoRec;
public:
	/** Video kaydını aç/kapat (uçuş arayüzü düğmesi) */
	void ToggleVideo();
	/** Radar ekranında temas seç (EO izler; hedefse kilit) */
	void RadarPick(int32 Id);
	void CycleEoType();
	/** Uçuşa giriş yükleme kapısı (yükleme ekranı aşamaları ve ilerlemesi) */
	const FSmvLoadGate& GetLoadGate() const { return Gate; }
	/** Yükleme başarısız (harita / zemin): haritayı yeniden yükle, kapıyı baştan başlat */
	void RetryWorldLoad();
	/** Yükleniyor → Uçuş geçiş anı (yükleme ekranının kararma çıkışı için) */
	double GetFlightEnterAt() const { return FlightEnterAt; }
	/** Uçuşu bitir (arayüz düğmesi; onay sonrası) */
	void EndFlightUi() { if (Flow.IsValid()) Flow->EndFlight(TEXT("user")); }
	/** Uçuşta onay diyaloğu (kabuk katmanı) */
	void ConfirmUi(const FText& Title, const FText& Body, const FText& Yes, bool bDanger, TFunction<void()> OnYes);
	bool IsRecording() const;
	double RecSeconds() const;
	/** Duman testi: bağlantı var mı + irtifa (AGL m), ARM, konum hazırlığı (−1/0/1) */
		bool SmokeTele(float& OutAltRel, bool& bOutArmed, int32& OutPosReady) const;
		/** Duman testi: kamera kipi (0 takip, 1 FPV, 7 pilot omuz görünümü) */
		void SmokeCam(int32 Mode);
		/** Duman testi: verilen irtifaya kalkış (sabit kanat: kalkış yöntemiyle TAKEOFF) */
		void SmokeTakeoff(float AltM);
		/** Duman testi: görünümün (Fx, Fy) oranındaki noktaya sentetik sol tık (Slate); günlükte "SMOKE mouse" */
		bool SmokeClick(float Fx, float Fy, const TCHAR* What);
		bool SmokePanelOpen() const;
		/** Duman testi — bilerek çarpma: çevrede (72 yön, 8–400 m) Prefer türündeki (building | terrain | boş = her şey) en yakın yüzeyi bulur,
		 *  araç burnunu ona çevirir (GUIDED CONDITION_YAW). OutDesc: hedef aktör / tür / uzaklık. */
		bool SmokeRamBegin(const FString& Prefer, FString& OutDesc);
		/** Çarpma koşusu: kaçınma kapalı (AVOID_ENABLE 0), ALT_HOLD, RC ileri yunuslama (sabit kanatta doğrudan hedefe dalış GUIDED değil — yalnız çok rotor). */
		void SmokeRamGo();
		void SmokeRamStop();
		/** Çarpma durumu: çarpışma oldu mu, ARM, hedefe uzaklık (m), son temas satırı */
		bool SmokeRamStatus(bool& bOutArmed, float& OutDistM, FString& OutContact, float* OutBelowM = nullptr, float* OutSpeed = nullptr) const;
	private:
		bool bSmokeRam = false;
		bool bGoogleCity = false;   // Şehir parkuru Google 3B gerçek şehir olarak (Fab blok şehri yok)
		void TickCreditsPlacement();
		double LastCreditsFix = 0;
		float CreditsLiftPx = 56.f;   // alt bant yüksekliği + pay (Slate birimi)
		FVector SmokeRamTarget = FVector::ZeroVector;
		FString SmokeRamLastContact;
		FString HudLang;
	void TickDebris(float Dt);
	/** Saha çiti (FSmvMapDef::FenceM): sınıra 15 m kala uyarı, aşınca hata + uçuş 3 sn sonra biter (neden geofence) */
	void TickFence();
	bool bFence = false, bFenceWarned = false, bFenceBreached = false;
	FBox FenceBox = FBox(ForceInit);
	double FenceTopZ = 0;
	void UpdateFpv(const FCaySimTelemetry& T, double Now);
	FString RaceResultBadge;
	// yerel botlar
	struct FBot { SmvCore::FBotPilot* Pilot = nullptr; SmvCore::FRingTracker* Tracker = nullptr; FString Name; double FinishT = -1; };
	TArray<FBot> Bots;
	SmvCore::FDogfight* Dogfight = nullptr;
	bool bPlayerChaser = true;
	double GroundMsl = 0;
	// çevrim içi rakipler
	TMap<int64, int32> GhostIndex;

	// olay şeridi + mesaj
	TArray<TPair<FString, int32>> Events;
	FString ToastText; int32 ToastSev = 6; double ToastUntil = 0;
	double EndAt = 0; FString EndReasonPending;

	// ---- gerçek dünya (Cesium) içine oturtulan Fab haritası ----
	bool bFabSelfContained = false;
	bool bRealSurround = false, bRimPending = false, bRimDone = false, bRimStarted = false, bTilesSkipped = false;
	double RimAt = 0, RimStartAt = 0;
	FBox FabPlacedBox = FBox(ForceInit);
	FString FabLevelPath;
	bool LoadFabLevel(const FString& Level);
	bool AlignRealRim();
	/** Fab haritası kenarı ↔ gerçek dünya karoları arası geçiş eteği (kalan basamak / gök boşluğu) */
	void BuildRimApron(const TArray<FVector>& InP, const TArray<FVector>& OutP, const TArray<double>& Zo, const TArray<bool>& Ok, const TArray<int32>& Idx, int32 N, double ShiftCm, const FString& Biome);
	// ---- yükleme kapısı (SmvLoadGate): tüm aşamalar bitmeden Uçuş ekranına geçilmez ----
	FSmvLoadGate Gate;
	void TickLoadGate();
	bool bGlbPending = false;
	bool bIntroPending = false;
	double AtmosT0 = 0, WarmT0 = 0, FlightEnterAt = -100.0, StreamT0 = 0;
	uint64 AtmosF0 = 0, WarmF0 = 0;
	int32 WarmRecap = 0, StreamPeak = 0, ShaderPeak = 0;
	/** Yükleme sırasında hızlı göz uyumu (otomatik pozlama hızı 30) — girişte kaldırılır */
	UPROPERTY() TObjectPtr<class APostProcessVolume> LoadPp;
	UFUNCTION() void HandleScreen(ESmvScreen Screen);
	UFUNCTION() void HandleToast(const FString& Text, int32 Severity);
	void BuildScene();
	void StopScene();
	void SetupRace();
	void HandleTele(const FCaySimTelemetry& T, const FSmvTeleExtra& X);
	void HandleMessage(const TSharedPtr<FJsonObject>& M);
	void HandleAction(FName Action);
	void HandleMatchPositions(const TArray<FSmvGhostPos>& P);
	void UpdateGpws(const FCaySimTelemetry& T, const FSmvTeleExtra& X);
	void UpdateRace(float Dt);
	void UpdateTargets();
	void UpdateHud();
	/** Gerçek fizik: dron gövde çözücüsünün temas olayları (çarpışma → SITL crash, sürtünme → olay), engel sensörleri → SITL, HUD yakınlık */
	void TickPhysics(float Dt);
	void HandleContact(const FSmvContactEvent& E);
	/** Akışla gelen her seviye (Fab haritası, alt seviyeler, LevelInstance, şehir binaları): kapalı çarpışmaları aç + günlük */
	void HandleLevelAdded(ULevel* Level, UWorld* InWorld);
	FDelegateHandle LevelAddedHandle;
	TArray<TWeakObjectPtr<ULevel>> PhysPreparedLevels;
	void PreparePhysicsLevel(ULevel* Level);
	double LastSensorAt = 0, LastGrazeEventAt = 0;
	FString ProxLine;
	int32 ProxLevel = 0;
	void AugmentSummary(FSmvFlightSummary& S);
	/** Olay: bStrip = uçuş olay şeridine de (false → yalnız günlük paneli) */
	void PushEvent(const FString& Text, int32 Sev, bool bStrip = true);
	void HandleApText(const FString& Text, int32 Sev);
	void ReportArmFail(const FString& Why);
	FString LastPreArm, LastArmFail;
	double LastPreArmAt = 0, LastArmFailAt = 0;
	/** Uyarı gösterim politikası (evre, gecikme, tekrar bastırma, kademe) */
	SmvCore::FAlertPolicy* AlertPolicy = nullptr;
	/** Kalkış / fırlatma: armed | launch | (boş) — yöntem ve aşama zamanı (HUD geri sayımı / durum) */
	FString LaunchPhaseUi, LaunchMethodUi;
	double LaunchPhaseAt = 0, LaunchReleasedAt = 0;
	void HandleLaunchMsg(const TSharedPtr<FJsonObject>& M);
	/** Fırlatma düzeneği (rampa / pikap / el atışı) aşama değişimi */
	void OnLaunchPhase(const FString& Method, const FString& Phase);
	void FreeRace();
	USmvFlightLink* Link() const;
	FVector Geo(double Lat, double Lon, double AltMsl) const;
	ASmvDroneActor* SpawnGhost(const FString& Type, const FLinearColor& Color);
	void PlaceRingActors();
};
