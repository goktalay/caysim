// Oyun akışı (durum makinesi): Hoş geldin (Giriş yap / Misafir olarak devam et) → Ana menü → Hangar / Mürettebat / Parkurlar / Görevler /
// Karşılaşma / TOP 10 / Kayıtlar / Ayarlar / Hesap → Uç sihirbazı → Yükleniyor → Uçuş → Özet. Arayüz (SSmvShell) yalnız gösterir; veri ve
// geçişler burada. Uçuş başlatma: misafir kipte yerel SITL; hesapla isteğe bağlı "Bulutta uç".
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SimumavTypes.h"
#include "SmvFpv.h"
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
	Support
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
	/** GameMode özet ekler (GPWS, halkalar, yarış sonucu) — bulut özetine üstün gelmez, eksikleri tamamlar. */
	TFunction<void(FSmvFlightSummary&)> SummaryAugmenter;

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
	void EvaluateAchievements(const FSmvFlightSummary& S);
};
