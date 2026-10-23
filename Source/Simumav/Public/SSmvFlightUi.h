// Uçuş arayüzü (web HEPGO flightsim / SIMUMAV fly ekranının eşi) — KODLA kurulan Slate (UMG/.uasset yok), oyun görünümünün DPI ölçeğiyle büyür:
//   sol üst  : bilgi kartı (hesap · fizik · hız · kip · ARM · GPS rozetleri, Mission Planner satırı)
//   sağ üst  : DRON PANELİ (tek kart; başlık: durum noktası, ad, irtifa + kip özeti, ARM rozeti) — uçuş göstergesi (PFD: yapay ufuk, pitch
//              merdiveni, roll yayı, hız/irtifa şeritleri, yön şeridi, dikey hız), telemetri karoları 4×2, HABERLEŞME günlüğü (önem rengiyle)
//   alt orta : güç kadranları (YAKIT küçük · HIZ büyük · MOTOR büyük · BATARYA küçük; 240° süpürme, kırmızı bölge, dijital değer) + ALT BANT
//              (ARM/DISARM · Kalkış · İniş · RTL · uçuş modu seçici · Sıfırla · kamera kipi · Kumanda · Panel)
//   sol alt  : sanal kumanda (iki çubuk; fare ile sürüklenir, klavye/gamepad konumunu gösterir; ch5 anahtarı) + solup kaybolan ileti akışı
// Tema: koyu yarı saydam kartlar, 8 px köşe, ince kenar; renkler web (#0b0f14 zemin, turuncu vurgu, yeşil ARM, kırmızı uyarı).
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
	FString ModeKind = TEXT("copter");   // copter | plane | vtol | icept | rover
	bool bTele = false, bFresh = false, bArmed = false, bMotorCut = false, bGcs = false, bHasHome = false, bPlane = false, bRover = false;
	FString Mode;
	float Roll = 0, Pitch = 0;      // rad (çizilen poz)
	float Heading = 0;              // derece 0–360
	float AltRel = 0, AltMsl = 0, Gs = 0, As = 0, Climb = 0, Throttle = 0, BattV = 0, BattPct = -1, Speedup = 1;
	float DistHome = 0;
	int32 GpsFix = 0, Sats = -1;
	bool bFuel = false, bElec = false;
	float FuelPct = 0, FuelL = 0, RangeKm = 0, ElecPct = 0, ElecV = 0, ElecW = 0, ElecMin = -1;
	float MaxKmh = 80;              // hız kadranı ölçeği
	TArray<FSmvUiLog> Log;
	FVector2D StickL = FVector2D::ZeroVector, StickR = FVector2D::ZeroVector;
	int32 Ch5 = 1000;
	bool bThrUsed = true, bStickMode2 = true, bRcDevice = false;
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
	void ToggleSticks() { bSticks = !bSticks; }
	void ToggleModeMenu() { bModeMenu = !bModeMenu; }
	bool IsPanelOn() const { return bPanel; }
	bool AreSticksOn() const { return bSticks; }

private:
	TWeakObjectPtr<ASmvGameMode> GM;
	FSmvFlightUiState St;
	bool bPanel = true, bSticks = true, bModeMenu = false;

	// tema
	FSlateBrush CardBrush, HeadBrush, ChipBrush, TileBrush, ConsoleBrush, BarBrush, CircleBrush;
	FButtonStyle BtnStyle;   // beyaz yuvarlak köşeli; renk ButtonColorAndOpacity ile (durumla değişir)
	TSharedPtr<SBox> ModeBox;
	FString BuiltModeKind;
	int32 BuiltModeNum = -1;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void Act(const FString& Name, const FString& Arg = FString());
	TSharedRef<SWidget> InfoCard();
	TSharedRef<SWidget> DronePanel();
	TSharedRef<SWidget> TeleTile(const FString& Label, TFunction<FString()> Value, TFunction<FLinearColor()> Color = nullptr);
	TSharedRef<SWidget> ConsoleBox();
	TSharedRef<SWidget> BottomBar();
	TSharedRef<SWidget> ModeMenu();
	void RebuildModeGrid();
	TSharedRef<SWidget> Gauges();
	TSharedRef<SWidget> Sticks();
	TSharedRef<SWidget> Feed();
	TSharedRef<SWidget> Btn(TAttribute<FText> Label, TFunction<void()> OnClick, TFunction<FLinearColor()> Color, float MinW = 0.f);
	TSharedRef<SWidget> Chip(TAttribute<FText> Label, TFunction<FLinearColor()> Color);
};
