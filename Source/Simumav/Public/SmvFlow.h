// Oyun akışı (durum makinesi): Hoş geldin (Giriş yap / Misafir olarak devam et) → Ana menü → Hangar / Mürettebat / Parkurlar / Görevler /
// Karşılaşma / TOP 10 / Kayıtlar / Ayarlar / Hesap → Uç sihirbazı → Yükleniyor → Uçuş → Özet. Arayüz (SSmvShell) yalnız gösterir; veri ve
// geçişler burada. Uçuş başlatma: misafir kipte yerel SITL; hesapla isteğe bağlı "Bulutta uç".
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SimumavTypes.h"
#include "SmvFpv.h"
#include "SmvReplay.h"
#include "SmvFlow.generated.h"

class USimumavClient;
class USmvDataService;
class USmvFlightLink;
class USmvMatchClient;

UENUM(BlueprintType)
enum class ESmvScreen : uint8
{
	Welcome, Login, Menu, Hangar, Crew, Tracks, Missions, Wizard, Loading, Flight, Summary, Lobby, Leaderboard, Logbook, Settings, Account,
	/** "Nasıl uçmak istersin?" (tek / FPV parkuru / çok oyunculu / yapay zekâ) */
	FlyMode,
	/** Hazır FPV parkurları galerisi (zaman denemesi) */
	FpvGallery,
	/** Kayıttan oynatma */
	Replay,
	/** Destek: sorun bildir / taleplerim */
	Support,
	// ---- yol haritası (docs/yol-haritasi.md) ----
	/** Hangar → "+ Yeni" sihirbazı (tip → model 3B → parametreler → kamera → eklentiler → görünüm) / kayıtlı dronu düzenle */
	HangarWizard,
	/** Parkur kategorisi listesi (Karşılaşma / Özel / Gerçek Dünya — USmvFlow::ParkurCat) */
	ParkurList,
	/** Özel parkur sihirbazı (Orman / Şehir / Dağlık + hava + ad + deniz seviyesi) */
	ParkurWizard,
	/** Gerçek dünya sihirbazı (harita: adres arama, işaret sürükleme, harita boyutu + hava + ad) */
	RealWizard,
	/** Yeni pilot sihirbazı (cinsiyet → vücut → yüz → aksesuar → ad soyad) */
	CrewWizard,
	/** Uçuş akışı (dron → parkur → oyun türü → tek / çok oyunculu → UÇ) */
	FlyFlow,
	/** Uçuş sonrası deneyim ekranı (XP, manevralar, seviye atlama) */
	Progress,
	/** Pilot menüsü → Arkadaşlarım (birlikte uçulanlar + davetler) */
	Friends,
	/** Pilot menüsü → Özelleştir (etkin pilotun görünümü) */
	Customize
};

/** Yarış sonu sırası (uçuş özeti podyumu) */
struct FSmvPodiumRow
{
	FString Name;
	int32 Passed = 0, Total = 0;
	double FinishT = -1;
	bool bMe = false;
};

/** Yerel karşılaşma (misafir): yapay zekâ pilotlarla halka yarışı / it dalaşı. Çevrim içi karşılaşmada sunucu hakemdir (USmvMatchClient). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvLocalRace
{
	GENERATED_BODY()
	/** none | rings | dogfight */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Kind = TEXT("none");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Bots = 2;
	/** easy | normal | hard */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Level = TEXT("normal");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 RingCount = 8;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Seed = 0;
	/** random | host_chaser | host_evader */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Roles = TEXT("random");
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSmvScreenChanged, ESmvScreen, Screen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSmvToast, const FString&, Text, int32, Severity);

UCLASS()
class SIMUMAV_API USmvFlow : public UObject
{
	GENERATED_BODY()

public:
	void Init(USimumavClient* InClient, USmvDataService* InData);

	UFUNCTION(BlueprintPure, Category = "Simumav") ESmvScreen GetScreen() const { return Screen; }
	UFUNCTION(BlueprintCallable, Category = "Simumav") void SetScreen(ESmvScreen S);
	UFUNCTION(BlueprintCallable, Category = "Simumav") void Back();
	/** İlk açılış mı (Hoş geldin ekranı gösterilir; sonraki açılışlarda doğrudan ana menü, misafir). */
	bool IsFirstRun() const { return bFirstRun; }
	void MarkWelcomeDone();

	// ---- hesap ----
	void Login(const FString& Email, const FString& Password);
	void ContinueAsGuest();
	void Logout();
	bool bLoginBusy = false;
	FString LoginError;
	/** Giriş sonrası "misafir verisini hesaba aktar" teklifi. */
	bool bOfferImport = false;
	void ImportGuest();

	// ---- sihirbaz ----
	FSmvFlightRequest Draft;
	FSmvLocalRace Race;
	/** Hazır FPV parkuru zaman denemesi (boş CourseId = yok) */
	FSmvFpvCfg Fpv;
	/** FPV parkuru için sanal parkur (pist merkezi = başlangıç pedi; düz prosedürel dünya, ortam parkurdan) */
	static FSmvTrack MakeFpvTrack(const FSmvFpvCourse& C, const FSmvTrack& Base);
	void NewDraft();
	/** Taslağı doğrular; hata metni (boş = geçerli). */
	FString ValidateDraft() const;
	void StartFlight();
	/** Kayıttan oynat (3B sahne + 0,25–8× telemetri; kumanda yok) */
	bool StartReplay(const FString& File, float Speed = 1.f);
	bool IsReplay() const { return bReplay; }
	/** Son uçuşun 5 Hz izi (rota haritası, rapor, kayıt) */
	TArray<FSmvReplayFrame> LastPath;
	FSmvFlightRequest LastRequest;
	FString LastReplayFile;
	/** Uçuşu bitir (kullanıcı, çarpışma, servo tetik, süre). Özet gelince Summary ekranı. */
	void EndFlight(const FString& Reason);

	// ---- uçuş ----
	UFUNCTION(BlueprintPure, Category = "Simumav") USmvFlightLink* GetLink() const { return Link; }
	USmvMatchClient* GetMatch() const { return Match; }
	FSmvFlightSummary LastSummary;
	/** Son yarışın sırası ve oyun puanı (web match: halka 100, bitiren 250 + sıra 150/100/50; it dalaşı kazanan 1000 + tutma ×100 ≤ 500) */
	TArray<FSmvPodiumRow> LastPodium;
	int32 LastScore = -1;
	FString LastRaceKind;
	/** Son FPV zaman denemesi sonucu */
	struct FFpvResult
	{
		bool bValid = false, bFinished = false, bNewPb = false;
		FString CourseId;
		int32 Laps = 0, Misses = 0, Hits = 0, Score = 0;
		double TotalS = -1, PenS = 0, BestLapS = -1, PrevPbS = -1;
		TArray<double> LapTimes;
		TArray<TArray<double>> Sectors;
	} LastFpv;
	FString LoadingText;
	/** Bağlantı hazır (HAZIR / hello) — Yükleniyor → Uçuş geçişi ayrıca sahne yükleme kapısını bekler (GameMode FlightGate). */
	bool bLinkHazir = false;
	/** Sahne yükleme kapısı (GameMode): true → tüm aşamalar bitti; atanmamışsa bağlantı hazır olunca doğrudan Uçuş. */
	TFunction<bool()> FlightGate;
	/** Bağlantı hazır: kapı açıksa Uçuş'a geç (değilse GameMode her karede yeniden dener) */
	void RequestFlight();
	/** GameMode özet ekler (GPWS, halkalar, yarış sonucu) — bulut özetine üstün gelmez, eksikleri tamamlar. */
	TFunction<void(FSmvFlightSummary&)> SummaryAugmenter;

	// ---- yol haritası: uçuş akışı / oyun türü / ilerleme / sosyal ----
	/** Uçuş akışı adımı (0 dron, 1 parkur kategorisi, 2 parkur, 3 oyun türü, 4 oyuncular, 5 özet) */
	int32 FlyStep = 0;
	/** Parkur kategorisi: encounter | custom | real */
	FString ParkurCat = TEXT("encounter");
	/** Çok oyunculu mu; davet edilecek e-postalar ve arkadaş kimlikleri; eklenecek yapay zekâ sayısı / zorluk */
	bool bMulti = false;
	TArray<FString> InviteEmails;
	TArray<int64> InviteFriends;
	int32 AiCount = 0;
	FString AiLevel = TEXT("normal");
	/** Seçili oyun türünü taslağa ve yerel yarış ayarına uygular (halka/it dalaşı yerel botlar; diğer türler SmvGameRules) */
	void ApplyGameMode();
	/** "UÇ": tek oyunculu → StartFlight; çok oyunculu (halka / it dalaşı) → karşılaşma kur + davet + yapay zekâ → bekleme salonu */
	void StartGame();
	/** Oyun türünün gerektirdiği eklenti (cargo_drop → gripper, spraying → sprayer; yoksa boş) */
	static FString RequiredAddon(const FString& Mode);
	/** Tür bu araç tipiyle oynanabilir mi (sunucu modes.MODES types; yerelde savunma tüm uçan tiplerle) */
	static bool ModeAllowsType(const FString& Mode, const FString& Type, bool bOnline);
	static bool ModeIsMultiplayer(const FString& Mode) { return Mode == TEXT("rings") || Mode == TEXT("dogfight"); }

	/** Yerel oyun mantığının sonucu (SmvGameRules aktörü uçuş sonunda yazar) */
	struct FGameResult
	{
		FString Mode;
		int32 Score = 0;
		bool bReleased = false; double DropBestM = -1; int32 Payloads = 0, Hits = 0;
		double SprayCoverage = -1; double SprayTankLeft = -1;
		double DefenseScore = -1; bool bDefenseOk = false; int32 Neutralized = 0, Impacts = 0, Hp = -1, Threats = 0;
	} LastGame;

	/** Uçuş sonu deneyim (yerel hesap ya da sunucu yanıtı) */
	struct FXpResult
	{
		bool bValid = false, bServer = false, bPending = false;
		int32 Xp = 0;
		int64 Before = 0, After = 0;
		int32 LevelBefore = 1, LevelAfter = 1;
		TArray<TPair<FString, int32>> Parts;
		TArray<TPair<FString, int32>> Maneuvers;   // anahtar → sayı
		TArray<FString> NewMedals;
		FString Note;
	} LastXp;

	/** Arkadaşlarım (GET /api/friends) */
	struct FFriend { int64 Id = 0; FString Name, Email, Last; int32 Played = 0; bool bAuto = false, bFriend = false; FSmvAvatar Avatar; };
	TArray<FFriend> Friends;
	bool bFriendsBusy = false;
	FString FriendsError;
	void FetchFriends();
	void AddFriend(const FString& Email);
	void RemoveFriend(int64 Id);
	/** Gelen davetler (GET /api/invites) */
	TArray<TSharedPtr<FJsonObject>> Invites;
	void FetchInvites();
	void AnswerInvite(int64 Id, bool bAccept);
	/** Pilot seviyesi (hesap: GET /api/crew/{id}/level → Xp/Level güncellenir) */
	void FetchPilotLevel(const FString& PilotId);
	/** Pilota göre uçuşlar (hesap: GET /api/crew/{id}/flights; misafir: yerel kayıtlar pilot kimliğiyle süzülür) */
	TArray<FSmvFlightSummary> PilotFlights;
	bool bPilotFlightsBusy = false;
	void FetchPilotFlights(const FString& PilotId);
	/** Etkin pilot (mürettebatta "pilot değiştir"): varsayılan pilot yapılır, taslağa yazılır */
	void SwitchPilot(const FString& PilotId);

	// ---- liderlik ----
	TArray<FSmvLeaderRow> Leaderboard;
	FString LeaderboardPeriod = TEXT("30d");
	void FetchLeaderboard();

	// ---- karşılaşma (çevrim içi) ----
	void EnsureMatchClient();

	UPROPERTY(BlueprintAssignable, Category = "Simumav") FSmvScreenChanged OnScreenChanged;
	UPROPERTY(BlueprintAssignable, Category = "Simumav") FSmvToast OnToast;
	void Toast(const FString& Text, int32 Severity = 6) { OnToast.Broadcast(Text, Severity); }

	USimumavClient* Client() const { return ClientObj; }
	USmvDataService* Data() const { return DataObj; }

private:
	UPROPERTY() TObjectPtr<USimumavClient> ClientObj;
	UPROPERTY() TObjectPtr<USmvDataService> DataObj;
	UPROPERTY() TObjectPtr<USmvFlightLink> Link;
	UPROPERTY() TObjectPtr<USmvMatchClient> Match;
	ESmvScreen Screen = ESmvScreen::Welcome;
	TArray<ESmvScreen> History;
	bool bFirstRun = true;
	bool bEnding = false;
	void HandleSummary(const FSmvFlightSummary& S);
	void BindLink();
	bool bReplay = false;
	FSmvFlightRequest ReplaySavedDraft;
	FSmvLocalRace ReplaySavedRace;
	FSmvFpvCfg ReplaySavedFpv;
	void EvaluateAchievements(const FSmvFlightSummary& S);
	/** Uçuş sonu XP: yerel (misafir) hesap + pilot kaydı; hesapta bulut uçuşu → sunucu seviyesi, yerel SITL → xp/import */
	void AwardXp();
	bool bPendingInvites = false;
	void SendPendingInvites();
};
