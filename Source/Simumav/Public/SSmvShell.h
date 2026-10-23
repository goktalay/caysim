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

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FNavigationReply OnNavigation(const FGeometry& G, const FNavigationEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;

private:
	TWeakObjectPtr<USmvFlow> Flow;
	TWeakObjectPtr<ASmvGameMode> GameMode;
	TSharedPtr<SBox> Content;
	TSharedPtr<SBox> BackdropBox;
	TSharedPtr<SVerticalBox> ToastStack;
	TSharedPtr<SBox> OverlayBox;
	TSharedPtr<SWidget> DefaultFocus;
	FSlateBrush PreviewBrush;
	FEditableTextBoxStyle TextStyle;
	ESmvScreen Built = ESmvScreen::Welcome;
	bool bBuiltPause = false;
	bool bDirty = true;
	bool bOverlay = false;
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
	TSharedRef<SWidget> Wizard();
	TSharedRef<SWidget> Loading();
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
	double SummaryAt = 0;
	FString FpvSel, FpvDroneId;
	int32 CrewTab = 0;
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
public:
	/** Bayram / mevsim teması (tarih tabanlı TR takvimi; yoksa boş) */
	static FString SeasonId();
	static FLinearColor SeasonColor(const FString& Id);
};
