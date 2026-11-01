// Simumav arayüz kabuğu — tüm menü ekranları KODLA kurulur (Slate; UMG/.uasset gerekmez), görsel dil SmvUiKit (SIMUMAV web teması:
// koyu cam kartlar, neon mavi vurgular, animasyonlu arka plan + arkada canlı 3B menü sahnesi). USmvFlow ekranına göre içerik yeniden kurulur;
// düzenleme alanları yerel tampon yapılara yazar (yazarken yeniden kurulmaz → odak kaybolmaz).
// Gezinme: fare, klavye (oklar / Enter / Esc / Q-E sekme), gamepad (sol çubuk-yön tuşları / A / B / LB-RB); F1 kısayol yardımı.
// Ekranlar: Hoş geldin · Giriş · Ana menü · Hangar (3B önizleme) · Mürettebat (pilot oluşturucu) · Parkurlar · Görevler · Uç (adımlı:
// araç → harita → ortam → kumanda → görev/kurallar → gelişmiş → özet) · Yükleniyor · Duraklatma · Uçuş özeti · Karşılaşma · TOP 10 ·
// Kayıtlar · Ayarlar (sekmeli: genel, görüntü, ses, kumanda, tuşlar, uyarılar, bağlantı) · Hesap. İlk açılışta kısa tanıtım.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SimumavTypes.h"
#include "SmvFlow.h"
#include "SmvUiKit.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

class ASmvGameMode;
class SBox;
class SVerticalBox;
class FSmvLoadGate;

/** Yükleme ekranı görünümü (uçuşa giriş / harita turu): arka plan görseli, harita adı, oyun türü, alt satır, aşamalar (kapı) */
struct FSmvLoadView
{
	TSharedPtr<FSlateBrush> Bg;
	FString MapName, Mode, Sub, Tod, Status;
	double T0 = 0;
	uint32 Seed = 0;
	const FSmvLoadGate* Gate = nullptr;
	/** arka planı altta uydu haritası çiziyor (gerçek dünya parkuru): opak taban / gök degradesi çizilmez, yalnız okunurluk karartması */
	bool bSatBg = false;
};

class SIMUMAV_API SSmvShell : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvShell) {}
		SLATE_ARGUMENT(USmvFlow*, Flow)
		SLATE_ARGUMENT(ASmvGameMode*, GameMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** Ekran değişti / veri değişti: içerik yeniden kurulur. */
	void Refresh();
	void ShowToast(const FString& Text, int32 Severity);
	/** Kısayol yardımı (F1) aç/kapa */
	void ToggleHelp();
	bool IsOverlayOpen() const { return bOverlay; }
	/** Onay diyaloğu (web confirmDlg): başlık, açıklama, onay düğmesi (tehlikeli = kırmızı); odak Vazgeç'te; Esc/B kapatır */
	void Confirm(const FText& Title, const FText& Body, const FText& YesLabel, bool bDanger, TFunction<void()> OnYes);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FNavigationReply OnNavigation(const FGeometry& G, const FNavigationEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;

private:
	TWeakObjectPtr<USmvFlow> Flow;
	TWeakObjectPtr<ASmvGameMode> GameMode;
	TSharedPtr<SBox> Content;
	TSharedPtr<SBox> BackdropBox;
	/** ana menü arka plan videosu (ASmvMenuScene::GetMenuVideo); oynarken dünya çizimi kapalı */
	FSlateBrush MenuVideoBrush;
	/** fırçanın gösterdiği video dokusu (menü sahnesi kapanınca GC; boyamada geçerlilik denetimi — Askeri Üs yüklemesinde çökme) */
	TWeakObjectPtr<UTexture> MenuVideoTex;
	bool bMenuVideoOn = false;
	/** -SmvRecordMenu: menü sinematiği sabit adımlı kare kaydı (menu-video.yml) */
	void TickRecordMenu();
	TSharedPtr<SVerticalBox> ToastStack;
	TSharedPtr<SBox> OverlayBox;
	TSharedPtr<SWidget> DefaultFocus;
	FSlateBrush PreviewBrush;
	/** TOP10 avatar fırçaları (render hedefi adına göre) */
	TMap<FString, TSharedPtr<FSlateBrush>> AvatarBrushes;
	FEditableTextBoxStyle TextStyle;
	ESmvScreen Built = ESmvScreen::Welcome;
	bool bBuiltPause = false;
	bool bDirty = true;
	bool bOverlay = false;
	/** Sahne geçişi başlangıcı (içerik solup kayarak gelir + ışık süpürmesi) */
	double TransAt = -100.0;
	/** Açılış logosu animasyonu (oyun başlangıcında bir kez; tuş/tık geçer) */
	double LogoAt = -100.0;
	FSlateBrush LogoBrush;
	TStrongObjectPtr<class UTexture2D> LogoTex;
	/** RC eksen algılama (Ayarlar → Kumanda → Algıla): yuva, başlangıç, taban değerler */
	int32 DetectSlot = -1;
	double DetectAt = 0;
	float DetectBase[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int32 TransDir = 1;
	bool bWantFocus = true;
	bool bPreviewVisible = false;

	struct FToast { FString Text; int32 Sev = 6; double At = 0; };
	TArray<FToast> Toasts;
	int32 ToastsBuilt = -1;
	void RebuildToasts();

	// düzenleme tamponları
	FString LoginEmail, LoginPassword;
	FSmvDrone EditDrone; FSmvPilot EditPilot; FSmvTrack EditTrack; FSmvMission EditMission;
	FSmvSettings EditSettings;
	FString MatchCode, MatchPassword, MatchKind = TEXT("rings"), MatchLevel = TEXT("normal");
	int32 MatchBots = 3, MatchMax = 4;
	int32 ServoCh = 9, ServoPwm = 1900, ServoDelay = 0; FString ServoCmp = TEXT("ge");
	int32 TargetCount = 0; FString TargetModel = TEXT("tgt:sw300"); float TargetAlt = 120, TargetDia = 800, TargetSpeed = 28;
	int32 WizStep = 0;
	int32 SetTab = 0;
	bool bPauseAdvanced = false;
	/** Sekme / adım değiştirme (Q/E, LB/RB) — o ekran tanımlar */
	TFunction<void(int32)> TabStep;
	// seçici tamponları (sihirbaz)
	FString WizDroneId, WizPilotId, WizTrackId, WizMissionId, WizBackend, WizSpeed, WizStickMode, WizDevice;

	// ekranlar
	TSharedRef<SWidget> BuildScreen(ESmvScreen S);
	TSharedRef<SWidget> Welcome();
	TSharedRef<SWidget> Login();
	TSharedRef<SWidget> Menu();
	TSharedRef<SWidget> Hangar();
	TSharedRef<SWidget> Crew();
	TSharedRef<SWidget> Tracks();
	TSharedRef<SWidget> Missions();
	// ---- Görevler v2 (SSmvMissions.cpp): liste + 3B önizleme + özet, sihirbaz, tam ekran Cesium düzenleyici ----
	TSharedPtr<struct FSmvMissionEdit> MEd;
	bool bMLegacy = false;
	void MEdLoad(const FSmvMission& M);
	FString MEdSave() const;
	TSharedRef<SWidget> MissionOverlay(bool bEditor);
	TSharedRef<SWidget> MissionsLegacy();
	TSharedRef<SWidget> MissionWizard();
	TSharedRef<SWidget> MissionEditor();
	TSharedRef<SWidget> MPointDialog();
	void MOpenPointDlg(int32 Index);
	void MTogglePlay();
	FVector2D MissionHome() const;
	TSharedRef<SWidget> Wizard();
	TSharedRef<SWidget> Loading();
	/** Tam ekran yükleme ekranı çizimi (Loading ve harita turu katmanı ortak) */
	static void PaintLoad(const FSmvLoadView& V, const FGeometry& G, FSlateWindowElementList& Out, int32 L);
	static FSmvLoadView MakeLoadView(const FSmvTrack& Track, const FString& GameModeId, const FString& DroneName);
	/** Açılış: menü sahnesi (harita + atmosfer) hazır olana dek logo + ilerleme; hazır olunca solar */
	bool bBootGate = false;
	double BootReadyAt = -1.0;
	bool BootFinished() const;
	int32 SmokeBoot = 0;
	double SmokeBootT0 = 0;
	TSharedRef<SWidget> FlightPause();
	TSharedRef<SWidget> Summary();
	TSharedRef<SWidget> Lobby();
	TSharedRef<SWidget> Leaderboard();
	TSharedRef<SWidget> Logbook();
	TSharedRef<SWidget> Settings();
	TSharedRef<SWidget> Account();
	TSharedRef<SWidget> FlyMode();
	TSharedRef<SWidget> FpvGallery();
	TSharedRef<SWidget> ReplayScreen();
	TSharedRef<SWidget> Support();
	/** Destek (web support/): sorun bildir formu + Taleplerim + talep ayrıntısı (yazışma, yanıt) — /api/support */
	struct FSupport
	{
		FString Cat = TEXT("flight"), Subject, Body, Reply, ShotPath, OpenId, Err;
		bool bCtx = true, bLoading = false, bSending = false;
		TArray<TSharedPtr<class FJsonValue>> Tickets;
		TSharedPtr<class FJsonObject> Detail;
	} Sup;
	void SupLoad();
	void SupOpen(const FString& Id);
	void SupSend();
	void SupReplySend();
	double SummaryAt = 0;
	FString FpvSel, FpvDroneId;
	int32 CrewTab = 0;
	int32 LogTab = 0;
	TSharedRef<SWidget> Achievements();
	/** Parkur haritası: kip (konum / halka düzenleyici), arama metni */
	FString MapMode = TEXT("pick"), MapQuery;
	uint32 WorldPrevHash = 0;
	/** Duman testi (-SmvSmoke): ekranları sırayla açar, her birinde ekran görüntüsü alır (Saved/Screenshots/Smoke), uçuş + duraklatma + özet, çıkış */
	int32 SmokeStep = -1;
	double SmokeAt = 0;
	/** duman testi uçuş aşaması: uçuş sırası, alt adım, bekleme sınırı */
	int32 SmokeFl = 0, SmokeSub = 0, SmokeRetries = 0;
	/** bölge uçuşu: paket hazır karesi (Uçuş akışı bölge kartı) alındı mı; uçuş için değiştirilen Gerçek dünya tercihi */
	bool bRegionShot = false;
	int32 SmokeRegionShot = 0;
	double SmokeRegT0 = 0;
	FString SmokePrevRealWorld;
	double SmokeDeadline = 0, SmokeT1 = 0;
	void TickSmoke();
	double WorldPrevAt = 0;
	bool bWorldPrevDirty = false;
	TSharedPtr<class SSmvTileMap> TrackMap;
	/** Kayıttan oynatma ekranı durumu */
	TArray<FSmvReplayInfo> RepList;
	/** Ana menü: son uçuşlar şeridi (rota küçük haritaları) */
	TArray<FSmvReplayInfo> MenuRecent;
	TArray<FSmvReplayFrame> MenuRecentFrames[4];
	int32 RepSel = 0;
	TArray<FSmvReplayFrame> RepFrames;
	FSmvFlightRequest RepReq;
	FString RepLoaded, RepSpeed = TEXT("1");
	void RepSelect(int32 I);
	/** 2B rota haritası (kuzey yukarı; irtifaya göre renk; başlangıç/bitiş) */
	TSharedRef<SWidget> RouteMap(const TArray<FSmvReplayFrame>* Frames, float W, float H);
	void OpenReport(const FSmvFlightRequest& Req, const FSmvFlightSummary& Sum, const TArray<FSmvReplayFrame>& Frames);
	/** Görev haritası düzenleyici durumu (web missions/editor.js eşi): şekil noktaları (enlem, boylam), FPV kapı sırası, parametreler */
	struct FMPlan
	{
		FString Kind = TEXT("free");
		TArray<FVector2D> Shape;   // X enlem, Y boylam
		TArray<int32> Order;
		double Alt = 50, Width = 40, Spacing = 20, Angle = 0; int32 Laps = 1;
		int32 Waves = 5, Interceptors = 10, Seed = 1; double RadiusM = 30, PrepS = 45; FString Difficulty = TEXT("normal");
		FString LoadedFor;
	} MPlan;
	double MZoom = 1.0; FVector2D MPan = FVector2D::ZeroVector; int32 MDrag = -1, MHover = -1;
	void MPlanLoad();
	FString MPlanSave() const;
	TSharedRef<SWidget> MissionMap(float W, float H);
	bool bFaceZoom = false, bKeepBody = false;
	FSmvFpvCfg FpvEdit;
	/** FPV parkuru kuş bakışı çizimi */
	TSharedRef<SWidget> FpvMap(const FString& CourseId, float W, float H);

	// sihirbaz adımları
	TSharedRef<SWidget> WizVehicle();
	TSharedRef<SWidget> WizMap();
	TSharedRef<SWidget> WizEnv();
	TSharedRef<SWidget> WizControl();
	TSharedRef<SWidget> WizRules();
	TSharedRef<SWidget> WizAdvanced();
	TSharedRef<SWidget> WizGcs();
	TSharedRef<SWidget> WizSummary();
	void StartFromWizard();

	// ayar sekmeleri
	TSharedRef<SWidget> SetGeneral();
	TSharedRef<SWidget> SetDisplay();
	TSharedRef<SWidget> SetAudio();
	TSharedRef<SWidget> SetControls();
	TSharedRef<SWidget> SetKeys();
	TSharedRef<SWidget> SetAlerts();
	TSharedRef<SWidget> SetLink();

	// yardımcılar
	TSharedRef<SWidget> Page(const FText& Title, const FText& Sub, ESmvIcon Icon, TSharedRef<SWidget> Body, bool bBack = true, bool bScroll = true, bool bTabs = false);
	TSharedRef<SWidget> Btn(const FText& InLabel, TFunction<void()> OnClick, bool bPrimary = false, TAttribute<bool> Enabled = true, ESmvIcon Icon = ESmvIcon::None);
	TSharedRef<SWidget> TextEdit(FString* Target, bool bPassword = false, const FText& Hint = FText::GetEmpty(), float MinW = 300.f);
	TSharedRef<SWidget> Spin(double* Target, double Min, double Max, double Step = 1.0);
	TSharedRef<SWidget> Slider(double* Target, double Min, double Max, double Step, TFunction<FString(double)> Fmt = nullptr, TFunction<void()> After = nullptr);
	TSharedRef<SWidget> SliderF(float* Target, float Min, float Max, float Step, TFunction<FString(double)> Fmt = nullptr, TFunction<void()> After = nullptr);
	TSharedRef<SWidget> SliderI(int32* Target, int32 Min, int32 Max, int32 Step = 1, TFunction<FString(double)> Fmt = nullptr, TFunction<void()> After = nullptr);
	TSharedRef<SWidget> Toggle(bool* Target, TFunction<void()> After = nullptr);
	/** Seçici: değerler + görünen etiketler (‹ › döngü) */
	TSharedRef<SWidget> Cycle(FString* Target, const TArray<FString>& Values, const TArray<FText>& Labels, TFunction<void()> OnPick = nullptr, float Width = 300.f);
	/** Çip grubu (tek seçim; simgeli) */
	TSharedRef<SWidget> Chips(FString* Target, const TArray<FString>& Values, const TArray<FText>& Labels, const TArray<ESmvIcon>& Icons, TFunction<void()> OnPick = nullptr, int32 PerRow = 6);
	TSharedRef<SWidget> ColorPick(FString* Hex, TFunction<void()> After = nullptr);
	/** Kart listesi öğesi (sol sütun) */
	TSharedRef<SWidget> ListItem(const FText& Title, const FText& Sub, ESmvIcon Icon, bool bSelected, bool bDefault, TFunction<void()> OnClick, const FLinearColor& Accent);
	/** Sekme şeridi */
	TSharedRef<SWidget> Tabs(const TArray<FText>& Labels, const TArray<ESmvIcon>& Icons, int32* Index);
	/** 3B önizleme kartı (yoksa çizilmiş siluet) */
	TSharedRef<SWidget> PreviewCard(const FString& Type, const FString& Model, const FString& Color, float W, float H);
	/** Pilot portresi (avatar renkleri) */
	TSharedRef<SWidget> Portrait(const FSmvAvatar* Av, float Size);
	/** Harita önizleme çizimi (tohumlu topoğrafya) */
	TSharedRef<SWidget> MapThumb(const FString& Seed, const FString& Mode, float W, float H);
	TSharedRef<SWidget> StatBar(const FText& Label, float V01, const FLinearColor& C);
	TSharedRef<SWidget> HintFooter(bool bBack = true, bool bTabs = false);
	TSharedRef<SWidget> AccountChip();
	void Toast(const FString& T, int32 Sev = 6);
	void SaveSettingsQuiet();
	void OpenTutorial();
	void CloseOverlay();
	void FocusDefault();
	class USmvDataService* Data() const;
	class USimumavClient* Client() const;
	class ASmvMenuScene* Scene() const;
	static FText Tx(const TCHAR* Key);
	// ---- yol haritası ekranları (docs/yol-haritasi.md; SSmvShellYol.cpp) ----
	TSharedRef<SWidget> MenuV2();
	TSharedRef<SWidget> HangarV2();
	TSharedRef<SWidget> HangarWizard();
	TSharedRef<SWidget> HwType();
	TSharedRef<SWidget> HwModel();
	TSharedRef<SWidget> HwParams();
	TSharedRef<SWidget> HwCamera();
	TSharedRef<SWidget> HwAddons();
	TSharedRef<SWidget> HwLook();
	void HwSave();
	TSharedRef<SWidget> ParkurHub();
	TSharedRef<SWidget> ParkurList();
	TSharedRef<SWidget> ParkurWizard();
	TSharedRef<SWidget> RealWizard();
	/** Bölge haritası kartı (SmvRegion): durum, indirme ilerlemesi, İndir / Güncelle / İptal / Sil. Tp kalıcı bir parkura işaret etmeli. */
	TSharedRef<SWidget> RegionCard(FSmvTrack* Tp, bool bCompact);
	TSharedRef<SWidget> CrewV2();
	TSharedRef<SWidget> CrewWizard();
	TSharedRef<SWidget> FlyFlow();
	TSharedRef<SWidget> ProgressScreen();
	TSharedRef<SWidget> FriendsScreen();
	/** Sağ üst Pilot menüsü (Özelleştir · Önceki uçuşlarım · Arkadaşlarım · TOP 10 · Ayarlar · Hesap · Destek · Çıkış …) */
	void OpenPilotMenu();
	/** Seçenek diyaloğu (ör. kayıtlı dron: Düzenle / Sil) */
	struct FChoice { FText Label; ESmvIcon Icon = ESmvIcon::None; int32 Kind = 0; TFunction<void()> Fn; };
	void OpenChoice(const FText& Title, const FText& Body, const TArray<FChoice>& Items);
	/** Ekranın canlı 3B sahne istasyonu (kamera, vitrin aracı / pilot, harita) */
	void UpdateStation();
	TSharedRef<SWidget> YhPage(const FText& InTitle, const FText& Sub, TSharedRef<SWidget> Body, TFunction<void()> OnBack = nullptr, TSharedPtr<SWidget> TitleExtra = nullptr);
	TSharedRef<SWidget> YhSteps(const TArray<FText>& Steps, int32 Cur, TFunction<void(int32)> OnPick);
	TSharedRef<SWidget> PilotChip();
	TSharedRef<SWidget> XpBar(int64 Xp, float W, float H = 6.f);
	TSharedRef<SWidget> DroneThumb(const FSmvDrone& D, float W, float H);
	TSharedRef<SWidget> ModelThumb(const FString& Type, const FString& Model, float W, float H);
	/** Menü sahnesinin dönen, arka plansız küçük görüntüsü (ASmvMenuScene::Request*Spin); kareler gelene dek Fallback */
	TSharedRef<SWidget> SpinThumb(const FString& Key, float W, float H, TSharedRef<SWidget> Fallback);
	/** Pilotun dönen baş-omuz görüntüsü (yoksa çizilmiş portre); yükseklik Size × 1,1 */
	TSharedRef<SWidget> AvatarThumb(const FSmvAvatar* Av, float Size);
	TSharedRef<SWidget> SatThumb(double Lat, double Lon, float W, float H, bool bAnimate);
	// sihirbaz durumları
	int32 HwStep = 0;
	FString HwKind = TEXT("multi");
	bool bHwEdit = false;
	FString HwParamName, HwParamValue;
	double HwMaxSpeed = -1, HwMaxAlt = -1;
	int32 PwStep = 0;
	/** Özel Parkurlar: seçili biyom (boş = önce Orman / Şehir / Dağlık seçimi) */
	FString ParkurBiome;
	int32 RwStep = 0;
	int32 CwStep = 0;
	FSmvPilot NewPilot;
	FSmvTrack NewTrack;
	FString FlyEmail, FriendEmail, TourKey, FlyTrackCat;
	FString DecalSlot = TEXT("top");
	double DecalScale = 1.0;
	double ProgressAt = 0;
	bool bPilotMenu = false;
	TSharedPtr<class SSmvTileMap> RealMap;
	TArray<TSharedPtr<FSlateBrush>> YhBrushes;
	/** Parkur merkezi: Özel kartın canlı prosedürel dünya önizlemesi kuruldu mu (ekrana her girişte bir kez) */
	bool bHubWorld = false;
	/** Parkur listesinde eski gelişmiş parkur düzenleyicisi (halka/kapı düzeni) açık */
	bool bTrackEditor = false;
	/** Yeni pilot sihirbazı: ad / soyad */
	FString CwFirst, CwLast;
	/** Uçuş akışı: bir önceki adım (parkur kategorisi atlanırken yön) */
	int32 FlyPrevStep = 0;
	/** Gerçek dünya sihirbazı: adres arama metni */
	FString RealQuery;
	/** Uçuş akışı → parkur: sanal dünyada konum seçici açık; kayıt adı; seçici haritası */
	bool bLocPick = false;
	FString LocName;
	TSharedPtr<class SSmvTileMap> LocMap;
	TSharedRef<SWidget> LocCard();
	TSharedRef<SWidget> LocPicker();
	/** Uçuş akışı / ilerleme ekranı yardımcıları (SSmvShellYol.cpp) */
	TSharedRef<SWidget> FlyDrones();
	TSharedRef<SWidget> FlyCats();
	TSharedRef<SWidget> FlyTracks();
	TSharedRef<SWidget> FlyModes();
	TSharedRef<SWidget> FlyPlayers();
	TSharedRef<SWidget> FlySummary();
	TSharedRef<SWidget> EnvEditor(FSmvTrack& T);
	void OpenTrackChoice(const FSmvTrack& T);
public:
	/** Bu ekranın arkasında canlı 3B sahne tam görünür mü (sinematik arka plan; düz degrade yok) */
	static bool IsSceneScreen(ESmvScreen S);
	/** Bayram / mevsim teması (tarih tabanlı TR takvimi; yoksa boş) */
	static FString SeasonId();
	static FLinearColor SeasonColor(const FString& Id);
	static FString HolidayLine();
};
