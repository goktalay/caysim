// Uçuş arayüzü (web HEPGO flightsim / SIMUMAV fly ekranının eşi) — KODLA kurulan Slate (UMG/.uasset yok), oyun görünümünün DPI ölçeğiyle büyür:
//   sol üst  : bilgi kartı (hesap · fizik · hız · kip · ARM · GPS rozetleri, Mission Planner satırı)
//   sağ üst  : DRON PANELİ (tek kart; başlık: durum noktası, ad, irtifa + kip özeti, ARM rozeti) — uçuş göstergesi (PFD: yapay ufuk, pitch
//              merdiveni, roll yayı, hız/irtifa şeritleri, yön şeridi, dikey hız), telemetri karoları 4×2, HABERLEŞME günlüğü (önem rengiyle)
//   alt orta : güç kadranları (YAKIT küçük · HIZ büyük · MOTOR büyük · BATARYA küçük; 240° süpürme, kırmızı bölge, dijital değer) + ALT BANT
//              (ARM/DISARM · Kalkış · İniş · RTL · uçuş modu seçici · Sıfırla · kamera kipi · Kumanda · Panel)
//   sol alt  : sanal kumanda (iki çubuk; fare ile sürüklenir, klavye/gamepad konumunu gösterir; ch5 anahtarı) + solup kaybolan ileti akışı
//   orta     : uçuş modu RADYAL TEKERLEĞİ (F / gamepad View; fare açısı ya da sağ çubuk vurgular, tık / A / F seçer)
//   alt orta : (sabit kanat / VTOL, isteğe bağlı) klasik KOKPİT gösterge paneli — altılı: hız, yapay ufuk, irtifa, dönüş, yön, dikey hız (G)
//   üst orta : giriş sekansı 3-2-1-BAŞLA (yarış dışı), ilk uçuşta kısa ipucu kartı
// Tema: SIMUMAV web — koyu cam kartlar, neon mavi vurgu (#38bdf8), yeşil ARM, kırmızı uyarı; tüm metinler 7 dil (fui.*).
// Veri: GameMode her kare SetState() çağırır; düğmeler ASmvGameMode::UiAction(ad, arg) ile aynı eylem yoluna gider (klavye kısayollarıyla ortak).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

class ASmvGameMode;
class SBox;

/** Günlük satırı (MAVLink önem derecesi: 0–3 hata, 4 uyarı, 5 bildiri, 6 bilgi, 7 ayıklama). */
struct FSmvUiLog
{
	FString Text;
	int32 Sev = 6;
	double Wall = 0;   // FPlatformTime::Seconds()
	FString Clock;     // "12:04:31"
};

/** Arayüz durumu — GameMode her kare doldurur. */
struct FSmvFlightUiState
{
	FString Title, Account, Backend, MpLine, CamLabel;
	/** PiP (gimbal kamerası) render hedefi — sürüklenebilir / boyutlandırılabilir pencere */
	class UTextureRenderTarget2D* Pip = nullptr;
	FString PipLabel;
	/** 3B mini harita (ASmvMapCam): görüntü, büyük kipte oyuncu kamera görünümü, işaretler (0..1 görüntü konumu) ve görev çizgisi */
	class UTextureRenderTarget2D* MapRt = nullptr;
	class UTextureRenderTarget2D* ViewRt = nullptr;
	struct FMapMark { FVector2D Uv = FVector2D::ZeroVector; float Hdg = 0.f; int32 Kind = 0; FString Label; FVector2D GroundUv = FVector2D::ZeroVector; bool bGround = false; };   // Kind 0 dron, 1 rakip, 2 ev
	/** harita yukarısının kuzeyden açısı (fareyle döndürme; yön okları buna göre) */
	float MapYawDeg = 0.f;
	TArray<FMapMark> MapMarks;
	TArray<FVector2D> MapPath;
	int32 MapPathCur = -1;
	FString ModeKind = TEXT("copter");   // copter | plane | vtol | icept | rover
	bool bTele = false, bFresh = false, bArmed = false, bMotorCut = false, bGcs = false, bHasHome = false, bPlane = false, bRover = false;
	FString Mode;
	float Roll = 0, Pitch = 0;      // rad (çizilen poz)
	float Heading = 0;              // derece 0–360
	float AltRel = 0, AltMsl = 0, Gs = 0, As = 0, Climb = 0, Throttle = 0, BattV = 0, BattPct = -1, Speedup = 1;
	float DistHome = 0;
	int32 GpsFix = 0, Sats = -1;
	/** Konum hazırlığı: −1 bilinmiyor, 0 GPS/EKF bekleniyor, 1 hazır; kalkış/ARM kuyrukta */
	int32 PosReady = -1;
	bool bCmdQueued = false;
	bool bFuel = false, bElec = false;
	float FuelPct = 0, FuelL = 0, RangeKm = 0, ElecPct = 0, ElecV = 0, ElecW = 0, ElecMin = -1;
	float MaxKmh = 80;              // hız kadranı ölçeği
	TArray<FSmvUiLog> Log;
	FVector2D StickL = FVector2D::ZeroVector, StickR = FVector2D::ZeroVector;
	int32 Ch5 = 1000;
	bool bThrUsed = true, bRcDevice = false;
	/** Kumanda modu 1–4 (çubuk etiketleri / klavye ipucu) */
	int32 StickMode = 2;
	/** Gamepad sağ çubuk (radyal menü seçimi; −1..1, yukarı +) */
	FVector2D PadRight = FVector2D::ZeroVector;
	/** Giriş sekansı (3-2-1) gösterilsin mi (yarış/karşılaşmada sunucu/HUD geri sayımı var) */
	bool bIntroAllowed = true;
	FString TrackName;
	/** Parkur rüzgârı (m/s, nereden esdiği derece, esinti m/s) — rüzgâr göstergesi */
	float WindMs = 0.f, WindDir = 0.f, WindGust = 0.f;
	/** FPV parkuru zaman denemesi şeridi */
	bool bFpv = false, bFpvFinished = false;
	/** Kayıttan oynatma şeridi */
	bool bReplay = false;
	/** Yer kontrol sahası: radar PPI + EO penceresi */
	struct FBlip { float E = 0, N = 0, Alt = 0; int32 Id = 0; bool bOwn = false, bSel = false; };
	bool bRadar = false, bEo = false, bRadarOn = true, bEoOn = true;
	float RadarKm = 20.f, EoKm = 5.f, Sweep = 0.f, EoFov = 48.f;
	TArray<FBlip> Blips;
	class UTextureRenderTarget2D* EoRt = nullptr;
	FString EoState, EoInfo, EoType;
	float ReplayT = 0.f, ReplayDur = 0.f, ReplaySpeed = 1.f;
	int32 FpvLap = 0, FpvLaps = 0, FpvNext = 0, FpvTotalCp = 0, FpvMisses = 0, FpvHits = 0;
	float FpvLapT = 0.f, FpvBest = -1.f, FpvTotal = 0.f, FpvPen = 0.f, FpvDist = 0.f;
	/** hayalete göre fark (sn; + geride); çok büyükse yok */
	float FpvGhost = 1e9f;
	/** sektör renkleri bu tur: 1 genel en iyi (mor), 2 kişisel en iyi (yeşil), 3 yavaş (sarı) */
	TArray<int32> FpvSectors;
	int32 FpvSectorCount = 1;
	TArray<FString> Modes;
	double FlightS = 0;
	bool bHideGauges = false;       // FPV / gimbal HUD açıkken kadranlar gizli (web)
	bool bMenuOpen = false;         // duraklatma menüsü açık: alt bant ve kumanda gizli
};

class SIMUMAV_API SSmvFlightUi : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvFlightUi) {}
		SLATE_ARGUMENT(ASmvGameMode*, GameMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetState(const FSmvFlightUiState& S) { St = S; }
	const FSmvFlightUiState& State() const { return St; }

	void TogglePanel() { bPanel = !bPanel; }
	/** HUD yerleşimi: sağ üst dron paneli / sol alt sanal kumanda açık mı */
	bool IsPanelOpen() const { return bPanel; }
	bool SticksShown() const { return bSticks; }
	void ToggleSticks() { bSticks = !bSticks; }
	void ToggleModeMenu();
	bool IsRadialOpen() const { return bModeMenu; }
	/** Radyal menüde vurgulananı seç (yoksa kapat) */
	void ConfirmRadial();
	void CloseRadial() { bModeMenu = false; }
	void ToggleCockpit();
	/** mini harita açık mı / büyük harita kipi (simülasyon görüntüsü gizli, kamera küçük pencerede) */
	bool IsMapOn() const;
	bool IsMapBig() const { return bMapBig; }
	void ToggleMap();
	void SetMapBig(bool bOn);
	/** Ctrl + sol tık (harita): bekleyen tık görüntü konumu (0..1); GameMode alır, ışınla zemini bulup SetGotoTarget çağırır */
	bool TakeMapClick(FVector2D& OutUv) { if (!bClickPending) return false; bClickPending = false; OutUv = ClickUv; return true; }
	void SetGotoTarget(double Lat, double Lon, bool bOk);
	bool IsPanelOn() const { return bPanel; }
	/** Tam ekran renk parlaması (çarpma vb.; ~0.9 sn'de söner) */
	void Flash(const FLinearColor& C) { FlashC = C; FlashAt = FPlatformTime::Seconds(); }
	bool AreSticksOn() const { return bSticks; }

private:
	TWeakObjectPtr<ASmvGameMode> GM;
	FSmvFlightUiState St;
	bool bPanel = true, bSticks = true, bModeMenu = false;

	// tema
	FSlateBrush CardBrush, HeadBrush, ChipBrush, TileBrush, ConsoleBrush, BarBrush, CircleBrush;
	FButtonStyle BtnStyle;   // beyaz yuvarlak köşeli; renk ButtonColorAndOpacity ile (durumla değişir)
	TSharedPtr<class SSmvRadial> Radial;
	float PrevHdg = 0.f, TurnRate = 0.f;
	bool bHdgInit = false;
	int32 LastIntroSec = 99;
	bool bTips = false;
	double TipsUntil = 0;
	FLinearColor FlashC = FLinearColor::Transparent;
	double FlashAt = -100.0;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void Act(const FString& Name, const FString& Arg = FString());
	TSharedRef<SWidget> InfoCard();
	TSharedRef<SWidget> DronePanel();
	TSharedRef<SWidget> TeleTile(const FString& Label, TFunction<FString()> Value, TFunction<FLinearColor()> Color = nullptr);
	TSharedRef<SWidget> ConsoleBox();
	TSharedRef<SWidget> BottomBar();
	TSharedRef<SWidget> ModeMenu();
	TSharedRef<SWidget> Gauges();
	TSharedRef<SWidget> Cockpit();
	TSharedRef<SWidget> Intro();
	TSharedRef<SWidget> Tips();
	TSharedRef<SWidget> WindCard();
	TSharedRef<SWidget> FpvBar();
	TSharedRef<SWidget> PipWindow();
	TSharedRef<SWidget> MapWindow();
	TSharedRef<SWidget> MapBigView();
	TSharedRef<SWidget> ViewWindow();
	void PaintMapMarks(const FGeometry& G, FSlateWindowElementList& Out, int32 L, const FVector2D& Org, const FVector2D& Sz, const FVector2D& Hover) const;
	bool bMapBig = false;
	bool bClickPending = false, bGoto = false, bGotoOk = false;
	FVector2D ClickUv = FVector2D::ZeroVector;
	double GotoLat = 0, GotoLon = 0;
	float GotoAlt = 30.f;
	/** fare haritanın üzerinde (yerel piksel) — dron ikonuna gelince telemetri etiketi */
	FVector2D MapHover = FVector2D(-1, -1), BigHover = FVector2D(-1, -1), BigU0 = FVector2D::ZeroVector, BigU1 = FVector2D(1, 1);
	TSharedRef<SWidget> GotoPanel();
	int32 MapDragMode = 0, ViewDragMode = 0;
	FVector2D MapDragAbs, MapStart, ViewDragAbs, ViewStart;
	FSlateBrush MapBrush, ViewBrush;
	TSharedRef<SWidget> ReplayBar();
	TSharedRef<SWidget> RecChip();
	/** Grafik ▾ açılır menüsü (web flightsim_gfx_ui): ön ayar, canlı FPS, kare hızı sınırı, köşe FPS sayacı */
	TSharedRef<SWidget> GfxPopup();
	bool bGfx = false, bFpsCounter = false;
	float FpsAvg = 60.f;
	TSharedRef<SWidget> RadarWindow();
	/** Dron kartı (web flightsim_panel.js / GÖKTUNGU .dron-kart.ayrik birebir): başlık şeridi, UÇUŞA HAZIR + İŞLEMLER, katlanır bölümler */
	TSharedRef<SWidget> GtPanel();
	TSharedRef<SWidget> GtSection(const FString& Key, TAttribute<FText> Title, TSharedRef<SWidget> Body, TSharedPtr<SWidget> Extra = nullptr);
	TSharedRef<SWidget> GtTile(const FString& Icon, TAttribute<FText> Label, TFunction<FString()> Value, TFunction<FString()> Sub, TFunction<FLinearColor()> IconCol, TFunction<void()> OnClick);
	TSharedRef<SWidget> GtCell(const FString& Icon, TAttribute<FText> Label, TFunction<void()> OnClick, bool bDanger = false);
	bool IsFolded(const FString& Key) const;
	void SetFolded(const FString& Key, bool bFold);
	void ArmToggleConfirm();
	FSlateBrush GtTint, GtCard, GtHead, GtTileB, GtCellB, GtCellH, GtInput, GtCapsule, GtSectB, GtConsole;
	FVector2D PanelDragAbs = FVector2D::ZeroVector, PanelStart = FVector2D::ZeroVector;
	TWeakPtr<SWidget> PanelHead;
	FSlateBrush BarPill, BarBtnB, BarBg;
	TSharedRef<SWidget> EoWindow();
	FSlateBrush EoBrush;
	FSlateBrush PipBrush;
	FVector2D PipDragAbs = FVector2D::ZeroVector, PipStart = FVector2D::ZeroVector;
	int32 PipDragMode = 0;   // 1 taşı, 2 boyutlandır
	TSharedRef<SWidget> Sticks();
	TSharedRef<SWidget> Feed();
	TSharedRef<SWidget> Btn(TAttribute<FText> Label, TFunction<void()> OnClick, TFunction<FLinearColor()> Color, float MinW = 0.f);
	TSharedRef<SWidget> Chip(TAttribute<FText> Label, TFunction<FLinearColor()> Color);
};
